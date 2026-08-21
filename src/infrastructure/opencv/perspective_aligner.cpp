#include <fac_lpr/infrastructure/opencv/perspective_aligner.hpp>

#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/image_validation.hpp>
#include <fac_lpr/infrastructure/native_image/native_image.hpp>
#include <fac_lpr/infrastructure/opencv_image_view.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace fac_lpr::infrastructure::opencv {
namespace {

[[nodiscard]] std::size_t checked_multiply(
    const std::size_t left,
    const std::size_t right,
    const char* field) {
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        throw application::ResourceExhaustedError(std::string{field} + " overflows size_t");
    }
    return left * right;
}

void check_context(const application::OperationContext& context) {
    if (context.cancellation_requested()) {
        throw application::CancelledError("perspective alignment cancelled");
    }
    if (context.deadline_exceeded()) {
        throw application::TimeoutError("perspective alignment deadline exceeded");
    }
}

[[nodiscard]] int cv_type(const application::PixelFormat format) {
    switch (format) {
        case application::PixelFormat::gray8: return CV_8UC1;
        case application::PixelFormat::bgr8:
        case application::PixelFormat::rgb8: return CV_8UC3;
    }
    throw application::InvalidImageError("unsupported pixel format for perspective alignment");
}

[[nodiscard]] application::ImageBuffer allocate_image(
    const std::size_t width,
    const std::size_t height,
    const application::PixelFormat format) {
    const auto channels = application::pixel_format_channels(format);
    if (width == 0U || height == 0U || channels == 0U) {
        throw application::InvalidImageError("invalid perspective output shape");
    }
    const auto stride = checked_multiply(width, channels, "perspective output stride");
    const auto bytes = checked_multiply(stride, height, "perspective output bytes");
    application::ImageBuffer result{};
    try {
        result.bytes.resize(bytes);
    } catch (const std::bad_alloc&) {
        throw application::ResourceExhaustedError("cannot allocate perspective output image");
    } catch (const std::length_error&) {
        throw application::ResourceExhaustedError("perspective output image exceeds vector limits");
    }
    result.width = width;
    result.height = height;
    result.stride_bytes = stride;
    result.format = format;
    return result;
}

[[nodiscard]] float edge_length(const domain::Point2f& left, const domain::Point2f& right) noexcept {
    return std::hypot(right.x - left.x, right.y - left.y);
}

[[nodiscard]] domain::Point2f normalized_direction(
    const domain::Point2f& from,
    const domain::Point2f& to) {
    const auto length = edge_length(from, to);
    if (!std::isfinite(length) || length <= 1.0e-4F) {
        throw application::ProviderError("cannot normalize degenerate plate edge");
    }
    return domain::Point2f{(to.x - from.x) / length, (to.y - from.y) / length};
}

[[nodiscard]] domain::Point2f clamp_point(
    domain::Point2f point,
    const application::ImageView& source) noexcept {
    const auto max_x = static_cast<float>(source.width - 1U);
    const auto max_y = static_cast<float>(source.height - 1U);
    point.x = std::clamp(point.x, 0.0F, max_x);
    point.y = std::clamp(point.y, 0.0F, max_y);
    return point;
}

[[nodiscard]] std::array<domain::Point2f, 4> rotate_to_landscape(
    const std::array<domain::Point2f, 4>& corners) noexcept {
    return {corners[3], corners[0], corners[1], corners[2]};
}

[[nodiscard]] std::array<domain::Point2f, 4> padded_corners(
    const std::array<domain::Point2f, 4>& corners,
    const application::ImageView& source,
    const PerspectiveAlignerConfig& config) {
    const auto top_direction = normalized_direction(corners[0], corners[1]);
    const auto bottom_direction = normalized_direction(corners[3], corners[2]);
    const auto left_direction = normalized_direction(corners[0], corners[3]);
    const auto right_direction = normalized_direction(corners[1], corners[2]);

    const auto average_width = (edge_length(corners[0], corners[1]) + edge_length(corners[3], corners[2])) * 0.5F;
    const auto average_height = (edge_length(corners[0], corners[3]) + edge_length(corners[1], corners[2])) * 0.5F;
    const auto horizontal_padding = average_width * config.horizontal_padding_ratio;
    const auto vertical_padding = average_height * config.vertical_padding_ratio;

    std::array<domain::Point2f, 4> result = corners;
    result[0].x -= (top_direction.x * horizontal_padding) + (left_direction.x * vertical_padding);
    result[0].y -= (top_direction.y * horizontal_padding) + (left_direction.y * vertical_padding);
    result[1].x += (top_direction.x * horizontal_padding) - (right_direction.x * vertical_padding);
    result[1].y += (top_direction.y * horizontal_padding) - (right_direction.y * vertical_padding);
    result[2].x += (bottom_direction.x * horizontal_padding) + (right_direction.x * vertical_padding);
    result[2].y += (bottom_direction.y * horizontal_padding) + (right_direction.y * vertical_padding);
    result[3].x -= (bottom_direction.x * horizontal_padding) - (left_direction.x * vertical_padding);
    result[3].y -= (bottom_direction.y * horizontal_padding) - (left_direction.y * vertical_padding);

    for (auto& point : result) {
        point = clamp_point(point, source);
    }
    return result;
}

[[nodiscard]] std::size_t bounded_dimension(
    const float measured,
    const std::size_t minimum,
    const std::size_t maximum) {
    if (!std::isfinite(measured) || measured <= 0.0F || minimum == 0U || minimum > maximum) {
        throw application::ProviderError("invalid perspective output dimension");
    }
    const auto rounded = static_cast<double>(std::llround(static_cast<double>(measured)));
    const auto bounded = std::clamp(
        rounded,
        static_cast<double>(minimum),
        static_cast<double>(maximum));
    return static_cast<std::size_t>(bounded);
}

[[nodiscard]] application::ImageRegion padded_bbox_region(
    const domain::BoundingBox& box,
    const application::ImageView& source,
    const PerspectiveAlignerConfig& config) {
    if (!box.is_valid()) {
        return {};
    }
    const auto horizontal_padding = box.width * config.horizontal_padding_ratio;
    const auto vertical_padding = box.height * config.vertical_padding_ratio;
    const auto left = std::clamp(box.x - horizontal_padding, 0.0F, static_cast<float>(source.width));
    const auto top = std::clamp(box.y - vertical_padding, 0.0F, static_cast<float>(source.height));
    const auto right = std::clamp(box.x + box.width + horizontal_padding, 0.0F, static_cast<float>(source.width));
    const auto bottom = std::clamp(box.y + box.height + vertical_padding, 0.0F, static_cast<float>(source.height));
    if (!(right > left && bottom > top)) {
        return {};
    }
    const auto x = static_cast<std::size_t>(std::floor(left));
    const auto y = static_cast<std::size_t>(std::floor(top));
    const auto right_index = std::min(source.width, static_cast<std::size_t>(std::ceil(right)));
    const auto bottom_index = std::min(source.height, static_cast<std::size_t>(std::ceil(bottom)));
    if (right_index <= x || bottom_index <= y) {
        return {};
    }
    return application::ImageRegion{x, y, right_index - x, bottom_index - y};
}

void validate_config(
    const PerspectiveAlignerConfig& config,
    const application::PerformanceConfig& limits) {
    if (!std::isfinite(config.horizontal_padding_ratio) || config.horizontal_padding_ratio < 0.0F || config.horizontal_padding_ratio > 0.50F ||
        !std::isfinite(config.vertical_padding_ratio) || config.vertical_padding_ratio < 0.0F || config.vertical_padding_ratio > 0.50F ||
        config.minimum_output_width == 0U || config.minimum_output_height == 0U ||
        config.minimum_output_width > config.maximum_output_width ||
        config.minimum_output_height > config.maximum_output_height ||
        config.maximum_output_width > limits.max_image_width ||
        config.maximum_output_height > limits.max_image_height ||
        !std::isfinite(config.landscape_rotation_threshold) || config.landscape_rotation_threshold < 1.0F) {
        throw application::ConfigurationError("perspective aligner configuration is invalid");
    }
}

} // namespace

OpenCvPerspectiveAligner::OpenCvPerspectiveAligner(
    PerspectiveAlignerConfig config,
    application::PerformanceConfig image_limits,
    geometry::GeometryConfig geometry_config)
    : config_(config),
      image_limits_(image_limits),
      geometry_validator_(geometry_config) {
    validate_config(config_, image_limits_);
}

std::optional<application::ImageBuffer> OpenCvPerspectiveAligner::bbox_fallback(
    const application::ImageView& source,
    const domain::Detection& detection) const {
    if (!config_.allow_bbox_fallback) {
        return std::nullopt;
    }
    const auto region = padded_bbox_region(detection.bbox, source, config_);
    if (region.empty()) {
        return std::nullopt;
    }
    auto output = allocate_image(region.width, region.height, source.format);
    native_image::copy_crop(source, region, output.mutable_view());
    return output;
}

std::optional<application::ImageBuffer> OpenCvPerspectiveAligner::align(
    const application::ImageView& source,
    const domain::Detection& detection,
    const application::OperationContext& context) {
    check_context(context);
    const auto validated = application::validate_image(source, image_limits_);
    const auto geometry = geometry_validator_.evaluate(detection);
    if (!geometry.valid) {
        return bbox_fallback(validated.view, detection);
    }

    auto corners = geometry.ordered_corners;
    auto width = (edge_length(corners[0], corners[1]) + edge_length(corners[3], corners[2])) * 0.5F;
    auto height = (edge_length(corners[0], corners[3]) + edge_length(corners[1], corners[2])) * 0.5F;
    if (config_.prefer_landscape && height > (width * config_.landscape_rotation_threshold)) {
        corners = rotate_to_landscape(corners);
        std::swap(width, height);
    }

    try {
        corners = padded_corners(corners, validated.view, config_);
        width = (edge_length(corners[0], corners[1]) + edge_length(corners[3], corners[2])) * 0.5F;
        height = (edge_length(corners[0], corners[3]) + edge_length(corners[1], corners[2])) * 0.5F;
        const auto output_width = bounded_dimension(width, config_.minimum_output_width, config_.maximum_output_width);
        const auto output_height = bounded_dimension(height, config_.minimum_output_height, config_.maximum_output_height);

        check_context(context);
        const OpenCvImageView source_view{validated};
        auto output = allocate_image(output_width, output_height, validated.view.format);
        cv::Mat destination(
            static_cast<int>(output_height),
            static_cast<int>(output_width),
            cv_type(validated.view.format),
            output.bytes.data(),
            output.stride_bytes);

        const std::array<cv::Point2f, 4> source_points{
            cv::Point2f{corners[0].x, corners[0].y},
            cv::Point2f{corners[1].x, corners[1].y},
            cv::Point2f{corners[2].x, corners[2].y},
            cv::Point2f{corners[3].x, corners[3].y},
        };
        const auto right = static_cast<float>(output_width - 1U);
        const auto bottom = static_cast<float>(output_height - 1U);
        const std::array<cv::Point2f, 4> destination_points{
            cv::Point2f{0.0F, 0.0F},
            cv::Point2f{right, 0.0F},
            cv::Point2f{right, bottom},
            cv::Point2f{0.0F, bottom},
        };
        const auto transform = cv::getPerspectiveTransform(source_points.data(), destination_points.data());
        if (transform.empty() || !cv::checkRange(transform)) {
            return bbox_fallback(validated.view, detection);
        }

        cv::warpPerspective(
            source_view.mat(),
            destination,
            transform,
            destination.size(),
            cv::INTER_LINEAR,
            cv::BORDER_REPLICATE);
        check_context(context);
        return output;
    } catch (const application::EngineError&) {
        throw;
    } catch (const cv::Exception&) {
        return bbox_fallback(validated.view, detection);
    } catch (const std::exception&) {
        throw application::ProviderError("perspective alignment failed");
    }
}

} // namespace fac_lpr::infrastructure::opencv
