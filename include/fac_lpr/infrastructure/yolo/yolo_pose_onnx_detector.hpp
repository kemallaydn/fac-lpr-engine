#pragma once

#include <fac_lpr/application/config.hpp>
#include <fac_lpr/application/providers.hpp>
#include <fac_lpr/infrastructure/native_image/native_image.hpp>
#include <fac_lpr/infrastructure/onnx/onnx_session.hpp>
#include <fac_lpr/infrastructure/yolo/yolo_pose_parser.hpp>
#include <fac_lpr/infrastructure/yolo/yolo_pose_preprocessor.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace fac_lpr::infrastructure::yolo {

struct YoloPoseOnnxDetectorConfig final {
    std::string provider_name{"yolo_pose_onnx"};
    std::string input_name{};
    std::string output_name{};
    YoloInputSpec input{};
    YoloPoseOutputSpec output{};
    application::PerformanceConfig image_limits{};
};

class YoloPoseOnnxDetector final : public application::IPlateDetector {
public:
    YoloPoseOnnxDetector(
        std::shared_ptr<onnx::IOnnxInferenceSession> session,
        YoloPoseOnnxDetectorConfig config);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::vector<domain::Detection> detect(
        const application::ImageView& image,
        const application::OperationContext& context) override;

private:
    void validate_contract() const;
    static void check_context(const application::OperationContext& context);

    std::shared_ptr<onnx::IOnnxInferenceSession> session_{};
    YoloPoseOnnxDetectorConfig config_{};
    YoloPosePreprocessor preprocessor_;
    YoloPoseOutputParser parser_;
    mutable std::mutex execution_mutex_{};
    native_image::NativeImageWorkspace workspace_{};
};

} // namespace fac_lpr::infrastructure::yolo
