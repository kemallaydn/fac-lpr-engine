#pragma once

#include <fac_lpr/application/image.hpp>
#include <fac_lpr/application/operation_context.hpp>

#include <cstddef>
#include <optional>
#include <string_view>

namespace fac_lpr::infrastructure::opencv {

class ICropNormalizer {
public:
    virtual ~ICropNormalizer() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual bool should_apply(
        const application::ImageView& input) const noexcept = 0;
    [[nodiscard]] virtual std::optional<application::ImageBuffer> normalize(
        const application::ImageView& input,
        const application::OperationContext& context) const = 0;
};

struct DoubleRowNormalizerConfig final {
    bool enabled{true};
    std::size_t minimum_width{40U};
    std::size_t minimum_height{28U};
    float minimum_aspect_ratio{0.65F};
    float maximum_aspect_ratio{2.20F};
    float split_search_min_ratio{0.30F};
    float split_search_max_ratio{0.70F};
    float minimum_row_foreground_ratio{0.015F};
    float maximum_row_foreground_ratio{0.55F};
    std::size_t target_row_height{32U};
    std::size_t separator_width{4U};
    std::size_t maximum_output_width{512U};
};

class DoubleRowPlateNormalizer final : public ICropNormalizer {
public:
    explicit DoubleRowPlateNormalizer(DoubleRowNormalizerConfig config = {});

    [[nodiscard]] std::string_view name() const noexcept override {
        return "double_row";
    }

    [[nodiscard]] bool should_apply(
        const application::ImageView& input) const noexcept override;

    [[nodiscard]] std::optional<application::ImageBuffer> normalize(
        const application::ImageView& input,
        const application::OperationContext& context) const override;

private:
    DoubleRowNormalizerConfig config_{};
};

} // namespace fac_lpr::infrastructure::opencv
