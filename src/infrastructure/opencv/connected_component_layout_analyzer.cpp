#include <fac_lpr/infrastructure/opencv/connected_component_layout_analyzer.hpp>

#include <fac_lpr/application/config.hpp>
#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/image_validation.hpp>
#include <fac_lpr/infrastructure/opencv_image_view.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <vector>

namespace fac_lpr::infrastructure::opencv {
namespace {

void check_context(const application::OperationContext& context) {
    if (context.cancellation_requested()) {
        throw application::CancelledError("layout analysis cancelled");
    }
    if (context.deadline_exceeded()) {
        throw application::TimeoutError("layout analysis deadline exceeded");
    }
}

[[nodiscard]] float clamp01(const float value) noexcept {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] cv::Mat to_gray(
    const cv::Mat& source,
    const application::PixelFormat format) {
    if (format == application::PixelFormat::gray8) {
        return source.clone();
    }
    cv::Mat gray{};
    cv::cvtColor(
        source,
        gray,
        format == application::PixelFormat::rgb8
            ? cv::COLOR_RGB2GRAY
            : cv::COLOR_BGR2GRAY);
    return gray;
}

[[nodiscard]] float median(std::vector<float> values) {
    if (values.empty()) {
        return 0.0F;
    }
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2U;
    if ((values.size() % 2U) == 1U) {
        return values[middle];
    }
    return (values[middle - 1U] + values[middle]) * 0.5F;
}

struct LayoutChoice final {
    int letter_group_size{0};
    float boundary_strength{0.0F};
};

[[nodiscard]] std::optional<LayoutChoice> choose_letter_group(
    const std::vector<cv::Rect>& components,
    const float minimum_strength) {
    if (components.size() < 5U) {
        return std::nullopt;
    }

    std::vector<float> gaps{};
    gaps.reserve(components.size() - 1U);
    for (std::size_t index = 0U; index + 1U < components.size(); ++index) {
        const auto current_right = components[index].x + components[index].width;
        const auto gap = std::max(0, components[index + 1U].x - current_right);
        gaps.push_back(static_cast<float>(gap));
    }
    const auto typical_gap = std::max(1.0F, median(gaps));

    std::optional<LayoutChoice> best{};
    for (int letters = 1; letters <= 3; ++letters) {
        const auto second_boundary_index = static_cast<std::size_t>(1 + letters);
        if (second_boundary_index >= gaps.size()) {
            continue;
        }
        const auto numeric_group_count = static_cast<int>(components.size()) - 2 - letters;
        if (numeric_group_count < 2 || numeric_group_count > 5) {
            continue;
        }

        const auto province_gap = gaps[1U];
        const auto letter_gap = gaps[second_boundary_index];
        const auto strength = ((province_gap / typical_gap) +
                               (letter_gap / typical_gap)) * 0.5F;
        if (strength < minimum_strength) {
            continue;
        }
        if (!best.has_value() || strength > best->boundary_strength ||
            (strength == best->boundary_strength && letters < best->letter_group_size)) {
            best = LayoutChoice{letters, strength};
        }
    }
    return best;
}

} // namespace

ConnectedComponentPlateLayoutAnalyzer::ConnectedComponentPlateLayoutAnalyzer(
    ConnectedComponentLayoutConfig config)
    : config_(config) {
    const auto valid_ratio = [](const float value) {
        return std::isfinite(value) && value > 0.0F && value <= 1.0F;
    };
    if (!std::isfinite(config_.clahe_clip_limit) ||
        config_.clahe_clip_limit <= 0.0 || config_.clahe_clip_limit > 64.0 ||
        config_.blur_kernel_size < 1 || (config_.blur_kernel_size % 2) == 0 ||
        config_.blur_kernel_size > 31 ||
        !valid_ratio(config_.minimum_component_height_ratio) ||
        !valid_ratio(config_.maximum_component_height_ratio) ||
        config_.minimum_component_height_ratio >= config_.maximum_component_height_ratio ||
        !valid_ratio(config_.minimum_component_width_ratio) ||
        !valid_ratio(config_.maximum_component_width_ratio) ||
        config_.minimum_component_width_ratio >= config_.maximum_component_width_ratio ||
        !std::isfinite(config_.minimum_component_aspect_ratio) ||
        !std::isfinite(config_.maximum_component_aspect_ratio) ||
        config_.minimum_component_aspect_ratio <= 0.0F ||
        config_.maximum_component_aspect_ratio <= config_.minimum_component_aspect_ratio ||
        config_.minimum_character_count < 4U ||
        config_.maximum_character_count < config_.minimum_character_count ||
        config_.maximum_character_count > 16U ||
        !valid_ratio(config_.maximum_height_coefficient_of_variation) ||
        !std::isfinite(config_.minimum_boundary_gap_strength) ||
        config_.minimum_boundary_gap_strength < 1.0F ||
        !valid_ratio(config_.minimum_confidence)) {
        throw application::ConfigurationError(
            "connected-component layout configuration is invalid");
    }
}

application::LayoutEvidence ConnectedComponentPlateLayoutAnalyzer::analyze(
    const application::ImageView& plate,
    const std::span<const domain::PlateCandidate> candidates,
    const application::OperationContext& context) {
    (void)candidates;
    if (!config_.enabled) {
        return {};
    }
    check_context(context);

    const auto validated = application::validate_image(
        plate,
        application::PerformanceConfig{});

    try {
        const OpenCvImageView source{validated};
        auto gray = to_gray(source.mat(), plate.format);

        const auto clahe = cv::createCLAHE(config_.clahe_clip_limit, cv::Size{8, 8});
        cv::Mat enhanced{};
        clahe->apply(gray, enhanced);

        cv::Mat blurred{};
        if (config_.blur_kernel_size > 1) {
            cv::GaussianBlur(
                enhanced,
                blurred,
                cv::Size{config_.blur_kernel_size, config_.blur_kernel_size},
                0.0,
                0.0,
                cv::BORDER_REPLICATE);
        } else {
            blurred = enhanced;
        }

        cv::Mat binary{};
        cv::threshold(
            blurred,
            binary,
            0.0,
            255.0,
            cv::THRESH_BINARY_INV | cv::THRESH_OTSU);

        cv::Mat labels{};
        cv::Mat stats{};
        cv::Mat centroids{};
        const auto label_count = cv::connectedComponentsWithStats(
            binary,
            labels,
            stats,
            centroids,
            8,
            CV_32S);

        std::vector<cv::Rect> components{};
        components.reserve(static_cast<std::size_t>(std::max(0, label_count - 1)));
        for (int label = 1; label < label_count; ++label) {
            const auto x = stats.at<int>(label, cv::CC_STAT_LEFT);
            const auto y = stats.at<int>(label, cv::CC_STAT_TOP);
            const auto width = stats.at<int>(label, cv::CC_STAT_WIDTH);
            const auto height = stats.at<int>(label, cv::CC_STAT_HEIGHT);
            if (width <= 0 || height <= 0) {
                continue;
            }

            const auto height_ratio = static_cast<float>(height) /
                                      static_cast<float>(binary.rows);
            const auto width_ratio = static_cast<float>(width) /
                                     static_cast<float>(binary.cols);
            const auto aspect = static_cast<float>(width) /
                                static_cast<float>(height);
            if (height_ratio < config_.minimum_component_height_ratio ||
                height_ratio > config_.maximum_component_height_ratio ||
                width_ratio < config_.minimum_component_width_ratio ||
                width_ratio > config_.maximum_component_width_ratio ||
                aspect < config_.minimum_component_aspect_ratio ||
                aspect > config_.maximum_component_aspect_ratio) {
                continue;
            }
            components.emplace_back(x, y, width, height);
        }

        if (components.size() < config_.minimum_character_count ||
            components.size() > config_.maximum_character_count) {
            return application::LayoutEvidence{
                .reliable = false,
                .character_count = static_cast<int>(components.size()),
                .letter_group_size = std::nullopt,
                .confidence = 0.0F};
        }

        std::stable_sort(
            components.begin(),
            components.end(),
            [](const cv::Rect& left, const cv::Rect& right) {
                if (left.x != right.x) {
                    return left.x < right.x;
                }
                return left.y < right.y;
            });

        std::vector<float> heights{};
        heights.reserve(components.size());
        float mean_height = 0.0F;
        std::size_t overlap_count = 0U;
        for (std::size_t index = 0U; index < components.size(); ++index) {
            const auto height = static_cast<float>(components[index].height);
            heights.push_back(height);
            mean_height += height;
            if (index + 1U < components.size()) {
                const auto right = components[index].x + components[index].width;
                if (components[index + 1U].x < right) {
                    ++overlap_count;
                }
            }
        }
        mean_height /= static_cast<float>(components.size());
        if (mean_height <= 0.0F) {
            return {};
        }

        float variance = 0.0F;
        for (const auto height : heights) {
            const auto delta = height - mean_height;
            variance += delta * delta;
        }
        variance /= static_cast<float>(heights.size());
        const auto coefficient_of_variation = std::sqrt(variance) / mean_height;
        if (!std::isfinite(coefficient_of_variation) ||
            coefficient_of_variation > config_.maximum_height_coefficient_of_variation ||
            overlap_count > 1U) {
            return application::LayoutEvidence{
                .reliable = false,
                .character_count = static_cast<int>(components.size()),
                .letter_group_size = std::nullopt,
                .confidence = 0.0F};
        }

        const auto choice = choose_letter_group(
            components,
            config_.minimum_boundary_gap_strength);
        if (!choice.has_value()) {
            return application::LayoutEvidence{
                .reliable = false,
                .character_count = static_cast<int>(components.size()),
                .letter_group_size = std::nullopt,
                .confidence = 0.0F};
        }

        const auto height_score = clamp01(
            1.0F - (coefficient_of_variation /
                    config_.maximum_height_coefficient_of_variation));
        const auto boundary_score = clamp01(
            (choice->boundary_strength - 1.0F) /
            std::max(0.25F, config_.minimum_boundary_gap_strength));
        const auto overlap_score = overlap_count == 0U ? 1.0F : 0.5F;
        const auto confidence = clamp01(
            (0.40F * height_score) +
            (0.45F * boundary_score) +
            (0.15F * overlap_score));

        check_context(context);
        return application::LayoutEvidence{
            .reliable = confidence >= config_.minimum_confidence,
            .character_count = static_cast<int>(components.size()),
            .letter_group_size = choice->letter_group_size,
            .confidence = confidence};
    } catch (const application::EngineError&) {
        throw;
    } catch (const cv::Exception&) {
        throw application::ProviderError("connected-component layout analysis failed");
    }
}

} // namespace fac_lpr::infrastructure::opencv
