#include <fac_lpr/infrastructure/opencv/double_row_normalizer.hpp>

#include <fac_lpr/application/config.hpp>
#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/image_validation.hpp>
#include <fac_lpr/infrastructure/opencv_image_view.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>

namespace fac_lpr::infrastructure::opencv {
namespace {

void check_context(const application::OperationContext& context) {
    if (context.cancellation_requested()) {
        throw application::CancelledError("double-row normalization cancelled");
    }
    if (context.deadline_exceeded()) {
        throw application::TimeoutError("double-row normalization deadline exceeded");
    }
}

void require_ratio(const float value, const char* name) {
    if (!std::isfinite(value) || value <= 0.0F || value > 1.0F) {
        throw application::ConfigurationError(
            std::string{name} + " must be a finite value in (0,1]");
    }
}

[[nodiscard]] std::size_t checked_multiply(
    const std::size_t left,
    const std::size_t right,
    const char* name) {
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        throw application::ResourceExhaustedError(
            std::string{name} + " overflows size_t");
    }
    return left * right;
}

[[nodiscard]] application::ImageBuffer copy_mat(
    const cv::Mat& matrix,
    const application::PixelFormat format) {
    if (matrix.empty() || matrix.cols <= 0 || matrix.rows <= 0) {
        throw application::ProviderError("double-row normalizer produced an empty image");
    }

    const auto width = static_cast<std::size_t>(matrix.cols);
    const auto height = static_cast<std::size_t>(matrix.rows);
    const auto channels = application::pixel_format_channels(format);
    if (channels == 0U || matrix.channels() != static_cast<int>(channels)) {
        throw application::ProviderError(
            "double-row normalizer output pixel format is inconsistent");
    }

    const auto stride = checked_multiply(width, channels, "double-row output stride");
    const auto bytes = checked_multiply(stride, height, "double-row output bytes");

    application::ImageBuffer output{};
    try {
        output.bytes.resize(bytes);
    } catch (const std::bad_alloc&) {
        throw application::ResourceExhaustedError(
            "cannot allocate normalized double-row crop");
    } catch (const std::length_error&) {
        throw application::ResourceExhaustedError(
            "normalized double-row crop exceeds vector limits");
    }

    output.width = width;
    output.height = height;
    output.stride_bytes = stride;
    output.format = format;

    for (std::size_t row = 0U; row < height; ++row) {
        const auto* source = matrix.ptr<std::uint8_t>(static_cast<int>(row));
        auto* destination = output.bytes.data() + (row * stride);
        std::memcpy(destination, source, stride);
    }
    return output;
}

[[nodiscard]] cv::Mat to_gray(
    const cv::Mat& source,
    const application::PixelFormat format) {
    if (format == application::PixelFormat::gray8) {
        return source.clone();
    }

    cv::Mat gray{};
    const auto code = format == application::PixelFormat::rgb8
        ? cv::COLOR_RGB2GRAY
        : cv::COLOR_BGR2GRAY;
    cv::cvtColor(source, gray, code);
    return gray;
}

[[nodiscard]] double foreground_ratio(const cv::Mat& binary) {
    if (binary.empty() || binary.rows <= 0 || binary.cols <= 0) {
        return 0.0;
    }
    const auto foreground = static_cast<double>(cv::countNonZero(binary));
    const auto total = static_cast<double>(binary.rows) *
                       static_cast<double>(binary.cols);
    return total > 0.0 ? foreground / total : 0.0;
}

[[nodiscard]] std::optional<cv::Rect> trimmed_row_bounds(
    const cv::Mat& binary,
    const int y_begin,
    const int y_end) {
    if (y_begin < 0 || y_end <= y_begin || y_end > binary.rows) {
        return std::nullopt;
    }

    const cv::Rect row_rect{0, y_begin, binary.cols, y_end - y_begin};
    const cv::Mat row_mask = binary(row_rect);
    std::vector<cv::Point> foreground{};
    cv::findNonZero(row_mask, foreground);
    if (foreground.empty()) {
        return std::nullopt;
    }

    auto bounds = cv::boundingRect(foreground);
    if (bounds.width < 2 || bounds.height < 2) {
        return std::nullopt;
    }
    bounds.y += y_begin;
    return bounds;
}

[[nodiscard]] int find_split_row(
    const cv::Mat& binary,
    const DoubleRowNormalizerConfig& config) {
    const auto min_row = std::clamp(
        static_cast<int>(std::lround(
            static_cast<double>(binary.rows) * config.split_search_min_ratio)),
        1,
        binary.rows - 2);
    const auto max_row = std::clamp(
        static_cast<int>(std::lround(
            static_cast<double>(binary.rows) * config.split_search_max_ratio)),
        min_row,
        binary.rows - 1);

    int best_row = min_row;
    int best_foreground = std::numeric_limits<int>::max();
    for (int row = min_row; row <= max_row; ++row) {
        const auto foreground = cv::countNonZero(binary.row(row));
        if (foreground < best_foreground) {
            best_foreground = foreground;
            best_row = row;
        }
    }
    return best_row;
}

[[nodiscard]] cv::Mat resize_row(
    const cv::Mat& row,
    const std::size_t target_height) {
    if (row.empty() || row.rows <= 0 || row.cols <= 0) {
        throw application::ProviderError("double-row source row is empty");
    }
    if (target_height > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw application::ConfigurationError(
            "double-row target height exceeds OpenCV limits");
    }

    const auto scale = static_cast<double>(target_height) /
                       static_cast<double>(row.rows);
    const auto target_width_double =
        std::max(1.0, std::round(static_cast<double>(row.cols) * scale));
    if (target_width_double > static_cast<double>(std::numeric_limits<int>::max())) {
        throw application::ResourceExhaustedError(
            "double-row resized width exceeds OpenCV limits");
    }

    cv::Mat resized{};
    const auto interpolation = scale < 1.0 ? cv::INTER_AREA : cv::INTER_LINEAR;
    cv::resize(
        row,
        resized,
        cv::Size{
            static_cast<int>(target_width_double),
            static_cast<int>(target_height)},
        0.0,
        0.0,
        interpolation);
    return resized;
}

} // namespace

DoubleRowPlateNormalizer::DoubleRowPlateNormalizer(DoubleRowNormalizerConfig config)
    : config_(config) {
    if (config_.minimum_width == 0U || config_.minimum_height == 0U ||
        !std::isfinite(config_.minimum_aspect_ratio) ||
        !std::isfinite(config_.maximum_aspect_ratio) ||
        config_.minimum_aspect_ratio <= 0.0F ||
        config_.maximum_aspect_ratio <= config_.minimum_aspect_ratio ||
        config_.maximum_aspect_ratio > 10.0F) {
        throw application::ConfigurationError(
            "double-row aspect/dimension configuration is invalid");
    }

    require_ratio(config_.split_search_min_ratio, "split_search_min_ratio");
    require_ratio(config_.split_search_max_ratio, "split_search_max_ratio");
    require_ratio(config_.minimum_row_foreground_ratio, "minimum_row_foreground_ratio");
    require_ratio(config_.maximum_row_foreground_ratio, "maximum_row_foreground_ratio");

    if (config_.split_search_min_ratio >= config_.split_search_max_ratio ||
        config_.minimum_row_foreground_ratio >= config_.maximum_row_foreground_ratio ||
        config_.target_row_height == 0U || config_.target_row_height > 4096U ||
        config_.separator_width > 128U || config_.maximum_output_width == 0U ||
        config_.maximum_output_width > 16384U) {
        throw application::ConfigurationError(
            "double-row normalizer configuration is invalid");
    }
}

bool DoubleRowPlateNormalizer::should_apply(
    const application::ImageView& input) const noexcept {
    if (!config_.enabled || input.width < config_.minimum_width ||
        input.height < config_.minimum_height || input.height == 0U) {
        return false;
    }

    const auto ratio = static_cast<float>(input.width) /
                       static_cast<float>(input.height);
    return std::isfinite(ratio) && ratio >= config_.minimum_aspect_ratio &&
           ratio <= config_.maximum_aspect_ratio;
}

std::optional<application::ImageBuffer> DoubleRowPlateNormalizer::normalize(
    const application::ImageView& input,
    const application::OperationContext& context) const {
    if (!should_apply(input)) {
        return std::nullopt;
    }
    check_context(context);

    const auto validated = application::validate_image(
        input,
        application::PerformanceConfig{});

    try {
        const OpenCvImageView source{validated};
        const auto gray = to_gray(source.mat(), input.format);

        cv::Mat foreground{};
        cv::threshold(
            gray,
            foreground,
            0.0,
            255.0,
            cv::THRESH_BINARY_INV | cv::THRESH_OTSU);

        const auto split = find_split_row(foreground, config_);
        if (split <= 1 || split + 1 >= foreground.rows) {
            return std::nullopt;
        }

        const cv::Mat upper_mask = foreground(cv::Rect{0, 0, foreground.cols, split});
        const cv::Mat lower_mask = foreground(
            cv::Rect{0, split, foreground.cols, foreground.rows - split});
        const auto upper_ratio = foreground_ratio(upper_mask);
        const auto lower_ratio = foreground_ratio(lower_mask);

        const auto valid_ratio = [this](const double ratio) {
            return ratio >= static_cast<double>(config_.minimum_row_foreground_ratio) &&
                   ratio <= static_cast<double>(config_.maximum_row_foreground_ratio);
        };
        if (!valid_ratio(upper_ratio) || !valid_ratio(lower_ratio)) {
            return std::nullopt;
        }

        const auto upper_bounds = trimmed_row_bounds(foreground, 0, split);
        const auto lower_bounds = trimmed_row_bounds(foreground, split, foreground.rows);
        if (!upper_bounds.has_value() || !lower_bounds.has_value()) {
            return std::nullopt;
        }

        const cv::Mat upper = source.mat()(*upper_bounds);
        const cv::Mat lower = source.mat()(*lower_bounds);
        auto upper_resized = resize_row(upper, config_.target_row_height);
        auto lower_resized = resize_row(lower, config_.target_row_height);

        const auto total_width = static_cast<std::size_t>(upper_resized.cols) +
                                 config_.separator_width +
                                 static_cast<std::size_t>(lower_resized.cols);
        if (total_width == 0U || total_width > config_.maximum_output_width ||
            total_width > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return std::nullopt;
        }

        cv::Mat output(
            static_cast<int>(config_.target_row_height),
            static_cast<int>(total_width),
            source.mat().type(),
            cv::Scalar::all(255.0));
        upper_resized.copyTo(output(cv::Rect{
            0,
            0,
            upper_resized.cols,
            upper_resized.rows}));
        lower_resized.copyTo(output(cv::Rect{
            upper_resized.cols + static_cast<int>(config_.separator_width),
            0,
            lower_resized.cols,
            lower_resized.rows}));

        check_context(context);
        return copy_mat(output, input.format);
    } catch (const application::EngineError&) {
        throw;
    } catch (const cv::Exception&) {
        throw application::ProviderError("double-row crop normalization failed");
    } catch (const std::exception&) {
        throw application::ProviderError("double-row crop normalization failed");
    }
}

} // namespace fac_lpr::infrastructure::opencv
