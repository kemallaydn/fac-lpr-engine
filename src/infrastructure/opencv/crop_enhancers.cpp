#include <fac_lpr/infrastructure/opencv/crop_enhancers.hpp>

#include <fac_lpr/application/config.hpp>
#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/image_validation.hpp>
#include <fac_lpr/infrastructure/opencv_image_view.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace fac_lpr::infrastructure::opencv {
namespace {

void check_context(const application::OperationContext& context) {
    if (context.cancellation_requested()) {
        throw application::CancelledError("crop enhancement cancelled");
    }
    if (context.deadline_exceeded()) {
        throw application::TimeoutError("crop enhancement deadline exceeded");
    }
}

[[nodiscard]] bool finite_quality(
    const native_image::CropQuality& quality) noexcept {
    return std::isfinite(quality.sharpness) &&
           std::isfinite(quality.exposure) &&
           std::isfinite(quality.clipping) &&
           std::isfinite(quality.overall);
}

void require_probability(const float value, const char* name) {
    if (!std::isfinite(value) || value < 0.0F || value > 1.0F) {
        throw application::ConfigurationError(
            std::string{name} + " must be a finite value in [0,1]");
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
        throw application::ProviderError("crop enhancer produced an empty image");
    }

    const auto width = static_cast<std::size_t>(matrix.cols);
    const auto height = static_cast<std::size_t>(matrix.rows);
    const auto channels = application::pixel_format_channels(format);
    if (channels == 0U || matrix.channels() != static_cast<int>(channels)) {
        throw application::ProviderError("crop enhancer output pixel format is inconsistent");
    }

    const auto stride = checked_multiply(width, channels, "enhancer output stride");
    const auto byte_count = checked_multiply(stride, height, "enhancer output bytes");

    application::ImageBuffer output{};
    try {
        output.bytes.resize(byte_count);
    } catch (const std::bad_alloc&) {
        throw application::ResourceExhaustedError("cannot allocate enhanced crop");
    } catch (const std::length_error&) {
        throw application::ResourceExhaustedError("enhanced crop exceeds vector limits");
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

[[nodiscard]] cv::Mat apply_clahe(
    const cv::Mat& source,
    const application::PixelFormat format,
    const ClaheEnhancerConfig& config) {
    const auto clahe = cv::createCLAHE(
        config.clip_limit,
        cv::Size{config.tile_grid_width, config.tile_grid_height});

    if (format == application::PixelFormat::gray8) {
        cv::Mat output{};
        clahe->apply(source, output);
        return output;
    }

    cv::Mat lab{};
    const auto to_lab = format == application::PixelFormat::rgb8
        ? cv::COLOR_RGB2Lab
        : cv::COLOR_BGR2Lab;
    const auto from_lab = format == application::PixelFormat::rgb8
        ? cv::COLOR_Lab2RGB
        : cv::COLOR_Lab2BGR;
    cv::cvtColor(source, lab, to_lab);

    std::vector<cv::Mat> channels{};
    cv::split(lab, channels);
    if (channels.size() != 3U) {
        throw application::ProviderError("CLAHE Lab conversion did not produce three channels");
    }
    cv::Mat enhanced_luminance{};
    clahe->apply(channels[0], enhanced_luminance);
    channels[0] = std::move(enhanced_luminance);
    cv::merge(channels, lab);

    cv::Mat output{};
    cv::cvtColor(lab, output, from_lab);
    return output;
}

} // namespace

ClaheCropEnhancer::ClaheCropEnhancer(ClaheEnhancerConfig config)
    : config_(config) {
    require_probability(config_.maximum_overall_quality, "maximum_overall_quality");
    require_probability(config_.maximum_exposure_score, "maximum_exposure_score");
    if (!std::isfinite(config_.clip_limit) || config_.clip_limit <= 0.0 ||
        config_.clip_limit > 64.0 || config_.tile_grid_width <= 0 ||
        config_.tile_grid_height <= 0 || config_.tile_grid_width > 64 ||
        config_.tile_grid_height > 64) {
        throw application::ConfigurationError("CLAHE enhancer configuration is invalid");
    }
}

bool ClaheCropEnhancer::should_apply(
    const native_image::CropQuality& quality) const noexcept {
    return config_.enabled && finite_quality(quality) &&
        (quality.overall <= config_.maximum_overall_quality ||
         quality.exposure <= config_.maximum_exposure_score);
}

std::optional<application::ImageBuffer> ClaheCropEnhancer::enhance(
    const application::ImageView& input,
    const native_image::CropQuality& quality,
    const application::OperationContext& context) const {
    if (!should_apply(quality)) {
        return std::nullopt;
    }
    check_context(context);
    const auto validated = application::validate_image(
        input,
        application::PerformanceConfig{});

    try {
        const OpenCvImageView source{validated};
        const auto enhanced = apply_clahe(source.mat(), input.format, config_);
        check_context(context);
        return copy_mat(enhanced, input.format);
    } catch (const application::EngineError&) {
        throw;
    } catch (const cv::Exception&) {
        throw application::ProviderError("CLAHE crop enhancement failed");
    } catch (const std::exception&) {
        throw application::ProviderError("CLAHE crop enhancement failed");
    }
}

SharpenCropEnhancer::SharpenCropEnhancer(SharpenEnhancerConfig config)
    : config_(config) {
    require_probability(config_.maximum_sharpness_score, "maximum_sharpness_score");
    if (!std::isfinite(config_.sigma) || config_.sigma <= 0.0 || config_.sigma > 20.0 ||
        !std::isfinite(config_.amount) || config_.amount <= 0.0 || config_.amount > 10.0) {
        throw application::ConfigurationError("sharpen enhancer configuration is invalid");
    }
}

bool SharpenCropEnhancer::should_apply(
    const native_image::CropQuality& quality) const noexcept {
    return config_.enabled && finite_quality(quality) &&
           quality.sharpness <= config_.maximum_sharpness_score;
}

std::optional<application::ImageBuffer> SharpenCropEnhancer::enhance(
    const application::ImageView& input,
    const native_image::CropQuality& quality,
    const application::OperationContext& context) const {
    if (!should_apply(quality)) {
        return std::nullopt;
    }
    check_context(context);
    const auto validated = application::validate_image(
        input,
        application::PerformanceConfig{});

    try {
        const OpenCvImageView source{validated};
        cv::Mat blurred{};
        cv::GaussianBlur(
            source.mat(),
            blurred,
            cv::Size{0, 0},
            config_.sigma,
            config_.sigma,
            cv::BORDER_REPLICATE);
        cv::Mat enhanced{};
        cv::addWeighted(
            source.mat(),
            1.0 + config_.amount,
            blurred,
            -config_.amount,
            0.0,
            enhanced);
        check_context(context);
        return copy_mat(enhanced, input.format);
    } catch (const application::EngineError&) {
        throw;
    } catch (const cv::Exception&) {
        throw application::ProviderError("sharpen crop enhancement failed");
    } catch (const std::exception&) {
        throw application::ProviderError("sharpen crop enhancement failed");
    }
}

AdaptiveThresholdCropEnhancer::AdaptiveThresholdCropEnhancer(
    AdaptiveThresholdEnhancerConfig config)
    : config_(config) {
    require_probability(config_.maximum_overall_quality, "maximum_overall_quality");
    if (config_.block_size < 3 || (config_.block_size % 2) == 0 ||
        config_.block_size > 255 || !std::isfinite(config_.constant) ||
        std::abs(config_.constant) > 255.0) {
        throw application::ConfigurationError(
            "adaptive threshold enhancer configuration is invalid");
    }
}

bool AdaptiveThresholdCropEnhancer::should_apply(
    const native_image::CropQuality& quality) const noexcept {
    return config_.enabled && finite_quality(quality) &&
           quality.overall <= config_.maximum_overall_quality;
}

std::optional<application::ImageBuffer> AdaptiveThresholdCropEnhancer::enhance(
    const application::ImageView& input,
    const native_image::CropQuality& quality,
    const application::OperationContext& context) const {
    if (!should_apply(quality)) {
        return std::nullopt;
    }
    check_context(context);
    const auto validated = application::validate_image(
        input,
        application::PerformanceConfig{});

    try {
        const OpenCvImageView source{validated};
        const auto gray = to_gray(source.mat(), input.format);
        cv::Mat enhanced{};
        cv::adaptiveThreshold(
            gray,
            enhanced,
            255.0,
            cv::ADAPTIVE_THRESH_GAUSSIAN_C,
            config_.invert ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY,
            config_.block_size,
            config_.constant);
        check_context(context);
        return copy_mat(enhanced, application::PixelFormat::gray8);
    } catch (const application::EngineError&) {
        throw;
    } catch (const cv::Exception&) {
        throw application::ProviderError("adaptive threshold crop enhancement failed");
    } catch (const std::exception&) {
        throw application::ProviderError("adaptive threshold crop enhancement failed");
    }
}

} // namespace fac_lpr::infrastructure::opencv
