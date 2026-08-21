#pragma once

#include <fac_lpr/application/image.hpp>
#include <fac_lpr/application/operation_context.hpp>
#include <fac_lpr/infrastructure/native_image/native_image.hpp>

#include <optional>
#include <string_view>

namespace fac_lpr::infrastructure::opencv {

class ICropEnhancer {
public:
    virtual ~ICropEnhancer() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual bool should_apply(
        const native_image::CropQuality& quality) const noexcept = 0;
    [[nodiscard]] virtual std::optional<application::ImageBuffer> enhance(
        const application::ImageView& input,
        const native_image::CropQuality& quality,
        const application::OperationContext& context) const = 0;
};

struct ClaheEnhancerConfig final {
    bool enabled{true};
    float maximum_overall_quality{0.80F};
    float maximum_exposure_score{0.85F};
    double clip_limit{2.0};
    int tile_grid_width{8};
    int tile_grid_height{8};
};

class ClaheCropEnhancer final : public ICropEnhancer {
public:
    explicit ClaheCropEnhancer(ClaheEnhancerConfig config = {});

    [[nodiscard]] std::string_view name() const noexcept override { return "clahe"; }
    [[nodiscard]] bool should_apply(
        const native_image::CropQuality& quality) const noexcept override;
    [[nodiscard]] std::optional<application::ImageBuffer> enhance(
        const application::ImageView& input,
        const native_image::CropQuality& quality,
        const application::OperationContext& context) const override;

private:
    ClaheEnhancerConfig config_{};
};

struct SharpenEnhancerConfig final {
    bool enabled{true};
    float maximum_sharpness_score{0.70F};
    double sigma{1.0};
    double amount{1.0};
};

class SharpenCropEnhancer final : public ICropEnhancer {
public:
    explicit SharpenCropEnhancer(SharpenEnhancerConfig config = {});

    [[nodiscard]] std::string_view name() const noexcept override { return "sharpen"; }
    [[nodiscard]] bool should_apply(
        const native_image::CropQuality& quality) const noexcept override;
    [[nodiscard]] std::optional<application::ImageBuffer> enhance(
        const application::ImageView& input,
        const native_image::CropQuality& quality,
        const application::OperationContext& context) const override;

private:
    SharpenEnhancerConfig config_{};
};

struct AdaptiveThresholdEnhancerConfig final {
    bool enabled{true};
    float maximum_overall_quality{0.65F};
    int block_size{15};
    double constant{5.0};
    bool invert{false};
};

class AdaptiveThresholdCropEnhancer final : public ICropEnhancer {
public:
    explicit AdaptiveThresholdCropEnhancer(
        AdaptiveThresholdEnhancerConfig config = {});

    [[nodiscard]] std::string_view name() const noexcept override {
        return "adaptive_threshold";
    }
    [[nodiscard]] bool should_apply(
        const native_image::CropQuality& quality) const noexcept override;
    [[nodiscard]] std::optional<application::ImageBuffer> enhance(
        const application::ImageView& input,
        const native_image::CropQuality& quality,
        const application::OperationContext& context) const override;

private:
    AdaptiveThresholdEnhancerConfig config_{};
};

} // namespace fac_lpr::infrastructure::opencv
