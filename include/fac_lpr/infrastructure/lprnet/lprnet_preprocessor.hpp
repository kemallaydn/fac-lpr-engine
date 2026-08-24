#pragma once

#include <fac_lpr/application/image_validation.hpp>
#include <fac_lpr/infrastructure/native_image/native_image.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace fac_lpr::infrastructure::lprnet {

enum class TensorLayout {
    nchw,
    nhwc
};

enum class InputColorOrder {
    gray,
    rgb,
    bgr
};

enum class TensorElementType {
    float32
};

struct LprNetInputSpec final {
    std::size_t width{0U};
    std::size_t height{0U};
    std::size_t channels{0U};
    TensorLayout layout{TensorLayout::nchw};
    InputColorOrder color_order{InputColorOrder::rgb};
    TensorElementType element_type{TensorElementType::float32};
    float input_scale{1.0F / 255.0F};
    std::array<float, 3U> mean{0.0F, 0.0F, 0.0F};
    std::array<float, 3U> standard_deviation{1.0F, 1.0F, 1.0F};
};

struct LprNetInputTensor final {
    std::vector<float> values{};
    std::array<std::int64_t, 4U> shape{};
};

struct LprNetInputTensorView final {
    std::span<const float> values{};
    std::array<std::int64_t, 4U> shape{};
};

class LprNetPreprocessor final {
public:
    explicit LprNetPreprocessor(LprNetInputSpec spec);

    [[nodiscard]] const LprNetInputSpec& spec() const noexcept { return spec_; }
    [[nodiscard]] std::size_t tensor_elements() const noexcept { return tensor_elements_; }
    [[nodiscard]] const std::array<std::int64_t, 4U>& tensor_shape() const noexcept {
        return tensor_shape_;
    }

    [[nodiscard]] LprNetInputTensor preprocess(
        const application::ValidatedImage& image) const;

    [[nodiscard]] LprNetInputTensorView preprocess(
        const application::ValidatedImage& image,
        native_image::NativeImageWorkspace& workspace) const;

    void preprocess_into(
        const application::ValidatedImage& image,
        std::span<float> output) const;

private:
    LprNetInputSpec spec_{};
    std::size_t tensor_elements_{0U};
    std::array<std::int64_t, 4U> tensor_shape_{};
};

} // namespace fac_lpr::infrastructure::lprnet
