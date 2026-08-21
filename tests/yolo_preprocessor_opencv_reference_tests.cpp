#include <fac_lpr/application/config.hpp>
#include <fac_lpr/application/image_validation.hpp>
#include <fac_lpr/infrastructure/yolo/yolo_pose_preprocessor.hpp>

#include <gtest/gtest.h>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {
using fac_lpr::application::ImageView;
using fac_lpr::application::PerformanceConfig;
using fac_lpr::application::PixelFormat;
using fac_lpr::application::validate_image;
using fac_lpr::infrastructure::yolo::YoloInputSpec;
using fac_lpr::infrastructure::yolo::YoloPosePreprocessor;

TEST(YoloPreprocessor, MatchesOpenCvReferenceWithinBilinearRoundingTolerance) {
    constexpr std::size_t width = 13U;
    constexpr std::size_t height = 7U;
    std::vector<std::byte> bytes(width * height * 3U);
    for (std::size_t i = 0U; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::byte>((i * 37U) % 256U);
    }
    const auto image = validate_image(ImageView{bytes, width, height, width * 3U, PixelFormat::bgr8}, PerformanceConfig{});
    const YoloInputSpec spec{20U, 20U, 3U, 1.0F / 255.0F, 114.0F};
    const YoloPosePreprocessor preprocessor{spec};
    const auto custom = preprocessor.preprocess(image);

    cv::Mat bgr(static_cast<int>(height), static_cast<int>(width), CV_8UC3, bytes.data(), width * 3U);
    cv::Mat rgb{};
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    cv::Mat resized{};
    cv::resize(rgb, resized, cv::Size(static_cast<int>(custom.letterbox.resized_width), static_cast<int>(custom.letterbox.resized_height)), 0.0, 0.0, cv::INTER_LINEAR);
    const auto right = spec.width - custom.letterbox.resized_width - custom.letterbox.pad_left;
    const auto bottom = spec.height - custom.letterbox.resized_height - custom.letterbox.pad_top;
    cv::Mat letterboxed{};
    cv::copyMakeBorder(resized, letterboxed, static_cast<int>(custom.letterbox.pad_top), static_cast<int>(bottom), static_cast<int>(custom.letterbox.pad_left), static_cast<int>(right), cv::BORDER_CONSTANT, cv::Scalar(spec.pad_value, spec.pad_value, spec.pad_value));

    const auto plane = spec.width * spec.height;
    float max_difference = 0.0F;
    double difference_sum = 0.0;
    for (std::size_t y = 0U; y < spec.height; ++y) {
        const auto* row = letterboxed.ptr<cv::Vec3b>(static_cast<int>(y));
        for (std::size_t x = 0U; x < spec.width; ++x) {
            const auto index = (y * spec.width) + x;
            for (std::size_t channel = 0U; channel < 3U; ++channel) {
                const auto reference = static_cast<float>(row[x][channel]) * spec.scale;
                const auto difference = std::abs(custom.chw[(channel * plane) + index] - reference);
                max_difference = std::max(max_difference, difference);
                difference_sum += static_cast<double>(difference);
            }
        }
    }
    const auto mean_difference = difference_sum / static_cast<double>(custom.chw.size());
    EXPECT_LE(max_difference, 2.1F / 255.0F);
    EXPECT_LE(mean_difference, 0.75 / 255.0);
}

} // namespace
