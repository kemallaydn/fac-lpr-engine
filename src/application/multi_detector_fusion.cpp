#include <fac_lpr/application/multi_detector_fusion.hpp>

#include <fac_lpr/application/error.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace fac_lpr::application {
namespace {

struct BoxOverlap final {
    float iou{0.0F};
    float intersection_over_min_area{0.0F};
};

[[nodiscard]] BoxOverlap overlap(
    const domain::BoundingBox& lhs,
    const domain::BoundingBox& rhs) noexcept {
    if (!lhs.is_valid() || !rhs.is_valid()) {
        return {};
    }

    const auto left = std::max(lhs.x, rhs.x);
    const auto top = std::max(lhs.y, rhs.y);
    const auto right = std::min(lhs.x + lhs.width, rhs.x + rhs.width);
    const auto bottom = std::min(lhs.y + lhs.height, rhs.y + rhs.height);
    const auto width = std::max(0.0F, right - left);
    const auto height = std::max(0.0F, bottom - top);
    const auto intersection = width * height;
    const auto lhs_area = lhs.area();
    const auto rhs_area = rhs.area();
    const auto union_area = lhs_area + rhs_area - intersection;
    const auto min_area = std::min(lhs_area, rhs_area);

    return {
        .iou = union_area > 0.0F ? intersection / union_area : 0.0F,
        .intersection_over_min_area = min_area > 0.0F ? intersection / min_area : 0.0F};
}

[[nodiscard]] bool overlaps_enough(
    const domain::BoundingBox& lhs,
    const domain::BoundingBox& rhs,
    const float iou_threshold,
    const float min_area_threshold) noexcept {
    const auto metrics = overlap(lhs, rhs);
    return metrics.iou >= iou_threshold ||
           metrics.intersection_over_min_area >= min_area_threshold;
}

void validate_probability(const float value, const char* field) {
    if (!std::isfinite(value) || value < 0.0F || value > 1.0F) {
        throw ConfigurationError(std::string{field} + " must be finite and in [0,1]");
    }
}

[[nodiscard]] bool evidence_order(
    const DetectionEvidence& lhs,
    const DetectionEvidence& rhs) noexcept {
    if (lhs.provider != rhs.provider) {
        return lhs.provider < rhs.provider;
    }
    if (lhs.detection.confidence != rhs.detection.confidence) {
        return lhs.detection.confidence > rhs.detection.confidence;
    }
    if (lhs.detection.bbox.x != rhs.detection.bbox.x) {
        return lhs.detection.bbox.x < rhs.detection.bbox.x;
    }
    if (lhs.detection.bbox.y != rhs.detection.bbox.y) {
        return lhs.detection.bbox.y < rhs.detection.bbox.y;
    }
    if (lhs.detection.bbox.width != rhs.detection.bbox.width) {
        return lhs.detection.bbox.width < rhs.detection.bbox.width;
    }
    return lhs.detection.bbox.height < rhs.detection.bbox.height;
}

[[nodiscard]] std::vector<DetectionEvidence> deduplicate_same_model(
    std::vector<DetectionEvidence> evidence,
    const MultiDetectorFusionConfig& config) {
    std::sort(evidence.begin(), evidence.end(), evidence_order);
    std::vector<DetectionEvidence> deduplicated{};
    deduplicated.reserve(evidence.size());

    for (auto& candidate : evidence) {
        const auto duplicate = std::any_of(
            deduplicated.begin(),
            deduplicated.end(),
            [&](const DetectionEvidence& accepted) {
                return accepted.provider == candidate.provider &&
                       overlaps_enough(
                           accepted.detection.bbox,
                           candidate.detection.bbox,
                           config.same_model_iou_threshold,
                           config.same_model_min_area_overlap_threshold);
            });
        if (!duplicate) {
            deduplicated.push_back(std::move(candidate));
        }
    }
    return deduplicated;
}

class DisjointSet final {
public:
    explicit DisjointSet(const std::size_t size)
        : parent_(size), rank_(size, 0U) {
        std::iota(parent_.begin(), parent_.end(), 0U);
    }

    [[nodiscard]] std::size_t find(const std::size_t value) {
        if (parent_[value] != value) {
            parent_[value] = find(parent_[value]);
        }
        return parent_[value];
    }

    void unite(const std::size_t lhs, const std::size_t rhs) {
        auto lhs_root = find(lhs);
        auto rhs_root = find(rhs);
        if (lhs_root == rhs_root) {
            return;
        }
        if (rank_[lhs_root] < rank_[rhs_root]) {
            std::swap(lhs_root, rhs_root);
        }
        parent_[rhs_root] = lhs_root;
        if (rank_[lhs_root] == rank_[rhs_root]) {
            ++rank_[lhs_root];
        }
    }

private:
    std::vector<std::size_t> parent_{};
    std::vector<std::size_t> rank_{};
};

[[nodiscard]] DetectionGroup build_group(std::vector<DetectionEvidence> evidence) {
    std::map<std::string, DetectionEvidence> strongest_by_provider{};
    for (auto& item : evidence) {
        const auto score = item.detection.confidence * item.provider_weight;
        const auto iterator = strongest_by_provider.find(item.provider);
        if (iterator == strongest_by_provider.end()) {
            strongest_by_provider.emplace(item.provider, std::move(item));
            continue;
        }
        const auto current_score =
            iterator->second.detection.confidence * iterator->second.provider_weight;
        if (score > current_score) {
            iterator->second = std::move(item);
        }
    }

    DetectionGroup group{};
    group.evidence.reserve(strongest_by_provider.size());
    float total_weight = 0.0F;
    float weighted_x = 0.0F;
    float weighted_y = 0.0F;
    float weighted_width = 0.0F;
    float weighted_height = 0.0F;
    float weighted_geometry = 0.0F;
    float fused_confidence_complement = 1.0F;
    float best_score = -std::numeric_limits<float>::infinity();
    const domain::Detection* best_detection = nullptr;

    for (auto& [provider, item] : strongest_by_provider) {
        item.detection.provider = provider;
        const auto contribution = std::clamp(
            item.detection.confidence * item.provider_weight,
            0.0F,
            1.0F);
        const auto box_weight = std::max(contribution, 1.0e-6F);
        total_weight += box_weight;
        weighted_x += item.detection.bbox.x * box_weight;
        weighted_y += item.detection.bbox.y * box_weight;
        weighted_width += item.detection.bbox.width * box_weight;
        weighted_height += item.detection.bbox.height * box_weight;
        weighted_geometry += item.detection.geometry_score * box_weight;
        fused_confidence_complement *= (1.0F - contribution);
        if (contribution > best_score) {
            best_score = contribution;
            best_detection = &item.detection;
        }
        group.evidence.push_back(std::move(item));
    }

    if (total_weight <= 0.0F || best_detection == nullptr) {
        throw InternalError("multi-detector fusion produced an empty group");
    }

    group.fused.bbox = {
        .x = weighted_x / total_weight,
        .y = weighted_y / total_weight,
        .width = weighted_width / total_weight,
        .height = weighted_height / total_weight};
    group.fused.quadrilateral = best_detection->quadrilateral;
    group.fused.confidence = std::clamp(1.0F - fused_confidence_complement, 0.0F, 1.0F);
    group.fused.geometry_score = std::clamp(weighted_geometry / total_weight, 0.0F, 1.0F);
    group.fused.provider = "multi_detector_fusion";
    return group;
}

[[nodiscard]] bool group_order(
    const DetectionGroup& lhs,
    const DetectionGroup& rhs) noexcept {
    if (lhs.fused.confidence != rhs.fused.confidence) {
        return lhs.fused.confidence > rhs.fused.confidence;
    }
    if (lhs.fused.bbox.x != rhs.fused.bbox.x) {
        return lhs.fused.bbox.x < rhs.fused.bbox.x;
    }
    if (lhs.fused.bbox.y != rhs.fused.bbox.y) {
        return lhs.fused.bbox.y < rhs.fused.bbox.y;
    }
    if (lhs.fused.bbox.width != rhs.fused.bbox.width) {
        return lhs.fused.bbox.width < rhs.fused.bbox.width;
    }
    return lhs.fused.bbox.height < rhs.fused.bbox.height;
}

} // namespace

MultiDetectorFusion::MultiDetectorFusion(
    std::vector<DetectorRegistration> detectors,
    MultiDetectorFusionConfig config)
    : detectors_(std::move(detectors)),
      config_(config) {
    if (detectors_.empty()) {
        throw ConfigurationError("multi-detector fusion requires at least one detector");
    }
    validate_probability(config_.same_model_iou_threshold, "same-model IoU threshold");
    validate_probability(
        config_.same_model_min_area_overlap_threshold,
        "same-model min-area overlap threshold");
    validate_probability(config_.cross_model_iou_threshold, "cross-model IoU threshold");
    validate_probability(
        config_.cross_model_min_area_overlap_threshold,
        "cross-model min-area overlap threshold");
    if (config_.maximum_groups == 0U) {
        throw ConfigurationError("maximum detector group count must be greater than zero");
    }

    std::vector<std::string> names{};
    for (const auto& registration : detectors_) {
        if (!registration.provider) {
            throw ConfigurationError("detector registration provider cannot be null");
        }
        validate_probability(registration.weight, "detector weight");
        const std::string name{registration.provider->name()};
        if (name.empty()) {
            throw ConfigurationError("detector provider name cannot be empty");
        }
        if (std::find(names.begin(), names.end(), name) != names.end()) {
            throw ConfigurationError("detector provider names must be unique");
        }
        names.push_back(name);
    }
}

std::vector<domain::Detection> MultiDetectorFusion::detect(
    const ImageView& image,
    const OperationContext& context) {
    auto groups = detect_groups(image, context);
    std::vector<domain::Detection> detections{};
    detections.reserve(groups.size());
    for (auto& group : groups) {
        detections.push_back(std::move(group.fused));
    }
    return detections;
}

std::vector<DetectionGroup> MultiDetectorFusion::detect_groups(
    const ImageView& image,
    const OperationContext& context) {
    std::vector<DetectionEvidence> evidence{};
    for (const auto& registration : detectors_) {
        if (!registration.enabled || registration.weight <= 0.0F) {
            continue;
        }
        auto detections = registration.provider->detect(image, context);
        for (auto& detection : detections) {
            if (!detection.bbox.is_valid() || !std::isfinite(detection.confidence) ||
                detection.confidence < 0.0F || detection.confidence > 1.0F) {
                throw ProviderError(
                    std::string{"detector returned invalid evidence: "} +
                    std::string{registration.provider->name()});
            }
            evidence.push_back(DetectionEvidence{
                .detection = std::move(detection),
                .provider = std::string{registration.provider->name()},
                .provider_weight = registration.weight});
        }
    }

    evidence = deduplicate_same_model(std::move(evidence), config_);
    if (evidence.empty()) {
        return {};
    }

    std::sort(evidence.begin(), evidence.end(), evidence_order);
    DisjointSet groups{evidence.size()};
    for (std::size_t lhs = 0U; lhs < evidence.size(); ++lhs) {
        for (std::size_t rhs = lhs + 1U; rhs < evidence.size(); ++rhs) {
            if (evidence[lhs].provider == evidence[rhs].provider) {
                continue;
            }
            if (overlaps_enough(
                    evidence[lhs].detection.bbox,
                    evidence[rhs].detection.bbox,
                    config_.cross_model_iou_threshold,
                    config_.cross_model_min_area_overlap_threshold)) {
                groups.unite(lhs, rhs);
            }
        }
    }

    std::map<std::size_t, std::vector<DetectionEvidence>> grouped{};
    for (std::size_t index = 0U; index < evidence.size(); ++index) {
        grouped[groups.find(index)].push_back(std::move(evidence[index]));
    }

    std::vector<DetectionGroup> result{};
    result.reserve(std::min(grouped.size(), config_.maximum_groups));
    for (auto& [root, group_evidence] : grouped) {
        static_cast<void>(root);
        result.push_back(build_group(std::move(group_evidence)));
    }
    std::sort(result.begin(), result.end(), group_order);
    if (result.size() > config_.maximum_groups) {
        result.resize(config_.maximum_groups);
    }
    return result;
}

} // namespace fac_lpr::application
