#include <fac_lpr/infrastructure/yolo/yolo_pose_preprocessor.hpp>

#include <fac_lpr/application/error.hpp>
#include <fac_lpr/infrastructure/opencv_image_view.hpp>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fac_lpr::infrastructure::yolo {
namespace {

[[nodiscard]] int checked_int(const std::size_t value, const char* field) {
    if (value == 0U || value > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw application::ConfigurationError(std::string{field} + " is outside OpenCV dimension limits");
    }
    return static_cast<int>(value);
}

[[nodiscard]] std::size_t checked_tensor_size(const YoloInputSpec& spec) {
    if (spec.width == 0U || spec.height == 0U || spec.channels != 3U) {
        throw application::ConfigurationError("YOLO input must be non-zero HxW with exactly 3 channels");
    }
    if (!std::isfinite(spec.scale) || spec.scale <= 0.0F ||
        !std::isfinite(spec.pad_value) || spec.pad_value < 0.0F || spec.pad_value > 255.0F) {
        throw application::ConfigurationError("YOLO normalization or padding configuration is invalid");
    }
    if (spec.width > std::numeric_limits<std::size_t>::max() / spec.height) {
        throw application::ConfigurationError("YOLO input dimensions overflow");
    }
    const auto pixels = spec.width * spec.height;
    if (pixels > std::numeric_limits<std::size_t>::max() / spec.channels) {
        throw application::ConfigurationError("YOLO input tensor size overflows");
    }
    return pixels * spec.channels;
}

} // namespace

YoloPosePreprocessor::YoloPosePreprocessor(YoloInputSpec spec)
    : spec_(spec) {
    static_cast<void>(checked_tensor_size(spec_));
}

YoloInputTensor YoloPosePreprocessor::preprocess(
    const application::ValidatedImage& image) const {
    const auto tensor_size = checked_tensor_size(spec_);
    const OpenCvImageView source_view{image};
    const auto& source = source_view.mat();

    cv::Mat rgb{};
    switch (image.view.format) {
        case application::PixelFormat::gray8:
            cv::cvtColor(source, rgb, cv::COLOR_GRAY2RGB);
            break;
        case application::PixelFormat::bgr8:
            cv::cvtColor(source, rgb, cv::COLOR_BGR2RGB);
            break;
        case application::PixelFormat::rgb8:
            rgb = source;
            break;
    }

    const auto scale_x = static_cast<double>(spec_.width) / static_cast<double>(image.view.width);
    const auto scale_y = static_cast<double>(spec_.height) / static_cast<double>(image.view.height);
    const auto scale = std::min(scale_x, scale_y);
    if (!std::isfinite(scale) || scale <= 0.0) {
        throw application::InvalidImageError("cannot compute a valid YOLO letterbox scale");
    }

    const auto resized_width = std::clamp<std::size_t>(
        static_cast<std::size_t>(std::llround(static_cast<double>(image.view.width) * scale)),
        1U,
        spec_.width);
    const auto resized_height = std::clamp<std::size_t>(
        static_cast<std::size_t>(std::llround(static_cast<double>(image.view.height) * scale)),
        1U,
        spec_.height);

    cv::Mat resized{};
    cv::resize(
        rgb,
        resized,
        cv::Size{checked_int(resized_width, "resized width"), checked_int(resized_height, "resized height")},
        0.0,
        0.0,
        cv::INTER_LINEAR);

    const auto remaining_width = spec_.width - resized_width;
    const auto remaining_height = spec_.height - resized_height;
    const auto pad_left = remaining_width / 2U;
    const auto pad_top = remaining_height / 2U;
    const auto pad_right = remaining_width - pad_left;
    const auto pad_bottom = remaining_height - pad_top;

    cv::Mat letterboxed{};
    cv::copyMakeBorder(
        resized,
        letterboxed,
        checked_int(pad_top, "pad top"),
        checked_int(pad_bottom, "pad bottom"),
        checked_int(pad_left, "pad left"),
        checked_int(pad_right, "pad right"),
        cv::BORDER_CONSTANT,
        cv::Scalar{spec_.pad_value, spec_.pad_value, spec_.pad_value});

    if (letterboxed.cols != checked_int(spec_.width, "input width") ||
        letterboxed.rows != checked_int(spec_.height, "input height") ||
        letterboxed.channels() != 3) {
        throw application::InternalError("YOLO letterbox output shape is inconsistent");
    }

    YoloInputTensor result{};
    result.chw.resize(tensor_size);
    result.letterbox = LetterboxMetadata{
        .source_width = image.view.width,
        .source_height = image.view.height,
        .input_width = spec_.width,
        .input_height = spec_.height,
        .scale = static_cast<float>(scale),
        .pad_left = pad_left,
        .pad_top = pad_top,
        .resized_width = resized_width,
        .resized_height = resized_height,
    };

    const auto plane_size = spec_.width * spec_.height;
    for (std::size_t y = 0; y < spec_.height; ++y) {
        const auto* row = letterboxed.ptr<cv::Vec3b>(checked_int(y, "row"));
        for (std::size_t x = 0; x < spec_.width; ++x) {
            const auto& pixel = row[x];
            const auto index = y * spec_.width + x;
            result.chw[index] = static_cast<float>(pixel[0]) * spec_.scale;
            result.chw[plane_size + index] = static_cast<float>(pixel[1]) * spec_.scale;
            result.chw[(2U * plane_size) + index] = static_cast<float>(pixel[2]) * spec_.scale;
        }
    }

    return result;
}

} // namespace fac_lpr::infrastructure::yolo
