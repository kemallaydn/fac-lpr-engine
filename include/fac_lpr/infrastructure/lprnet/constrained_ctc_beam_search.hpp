#pragma once

#include <fac_lpr/application/turkish_plate_grammar.hpp>
#include <fac_lpr/domain/recognition.hpp>
#include <fac_lpr/infrastructure/lprnet/ctc_decoder.hpp>

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace fac_lpr::infrastructure::lprnet {

struct ConstrainedCtcBeamSearchConfig final {
    GreedyCtcDecoderConfig ctc{};
    std::size_t beam_width{16U};
    std::size_t result_limit{5U};
    std::size_t classes_per_step{6U};
    float confusion_weight{0.15F};
    std::vector<std::pair<char, char>> confusion_pairs{
        {'0', 'O'},
        {'1', 'I'},
        {'8', 'B'},
        {'5', 'S'},
        {'6', 'G'}};
};

struct ConstrainedCtcBeamSearchResult final {
    std::vector<domain::PlateCandidate> candidates{};
    CtcDecodeResult greedy{};
    float top_margin{0.0F};
    bool fallback_used{false};
};

class ConstrainedCtcBeamSearch final {
public:
    ConstrainedCtcBeamSearch(
        ConstrainedCtcBeamSearchConfig config,
        application::TurkishPlateGrammar grammar = {});

    [[nodiscard]] const ConstrainedCtcBeamSearchConfig& config() const noexcept {
        return config_;
    }

    [[nodiscard]] ConstrainedCtcBeamSearchResult decode(
        std::span<const float> logits,
        std::size_t timesteps,
        std::size_t classes) const;

private:
    ConstrainedCtcBeamSearchConfig config_{};
    application::TurkishPlateGrammar grammar_{};
    GreedyCtcDecoder greedy_decoder_;
};

} // namespace fac_lpr::infrastructure::lprnet
