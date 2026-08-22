#pragma once

#include <fac_lpr/application/image.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace fac_lpr::infrastructure::native_image {

struct NativeImageWorkspaceConfig final {
    std::size_t max_tensor_elements{16U * 1024U * 1024U};
    std::size_t max_scratch_bytes{128U * 1024U * 1024U};
};

struct NativeImageWorkspaceStats final {
    std::size_t tensor_capacity{0U};
    std::size_t scratch_capacity{0U};
    std::size_t tensor_growth_count{0U};
    std::size_t scratch_growth_count{0U};
};

class NativeImageWorkspace final {
public:
    explicit NativeImageWorkspace(NativeImageWorkspaceConfig config = {});

    [[nodiscard]] std::span<float> prepare_tensor(std::size_t elements);
    [[nodiscard]] application::MutableImageView prepare_image(
        std::size_t width,
        std::size_t height,
        application::PixelFormat format);

    [[nodiscard]] std::size_t tensor_capacity() const noexcept { return tensor_.capacity(); }
    [[nodiscard]] std::size_t scratch_capacity() const noexcept { return scratch_.capacity(); }
    [[nodiscard]] NativeImageWorkspaceStats stats() const noexcept;

private:
    NativeImageWorkspaceConfig config_{};
    std::vector<float> tensor_{};
    std::vector<std::byte> scratch_{};
    std::size_t tensor_growth_count_{0U};
    std::size_t scratch_growth_count_{0U};
};

[[nodiscard]] application::ImageView make_crop_view(
    const application::ImageView& source,
    const application::ImageRegion& region);

[[nodiscard]] application::MutableImageView make_crop_view(
    const application::MutableImageView& source,
    const application::ImageRegion& region);

void copy_crop(
    const application::ImageView& source,
    const application::ImageRegion& region,
    application::MutableImageView destination);

struct CropQualityConfig final {
    std::size_t min_width{16U};
    std::size_t min_height{8U};
    float target_exposure{128.0F};
    unsigned int clipping_low{4U};
    unsigned int clipping_high{251U};
    float max_clipped_ratio{0.15F};
    float sharpness_reference{400.0F};
    float sharpness_weight{0.55F};
    float exposure_weight{0.25F};
    float clipping_weight{0.20F};
};

struct CropQuality final {
    float sharpness{0.0F};
    float exposure{0.0F};
    float clipping{0.0F};
    float overall{0.0F};
    float mean_intensity{0.0F};
    float clipped_ratio{0.0F};
    float laplacian_variance{0.0F};
};

[[nodiscard]] CropQuality evaluate_crop_quality(
    const application::ImageView& image,
    const CropQualityConfig& config = {});

} // namespace fac_lpr::infrastructure::native_image
