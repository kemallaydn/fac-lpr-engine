#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace fac_lpr::infrastructure::lprnet {

struct GreedyCtcDecoderConfig final {
    std::vector<char> charset{};
    std::size_t blank_index{0U};
    std::size_t maximum_timesteps{256U};
    std::size_t maximum_classes{256U};
};

struct CtcDecodeResult final {
    std::string text{};
    float confidence{0.0F};
    std::vector<float> character_confidences{};
};

class GreedyCtcDecoder final {
public:
    explicit GreedyCtcDecoder(GreedyCtcDecoderConfig config);

    [[nodiscard]] const GreedyCtcDecoderConfig& config() const noexcept {
        return config_;
    }

    [[nodiscard]] std::size_t class_count() const noexcept {
        return config_.charset.size() + 1U;
    }

    [[nodiscard]] CtcDecodeResult decode(
        std::span<const float> logits,
        std::size_t timesteps,
        std::size_t classes) const;

private:
    GreedyCtcDecoderConfig config_{};
};

} // namespace fac_lpr::infrastructure::lprnet
