#pragma once

#include <fac_lpr/application/image_validation.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fac_lpr::infrastructure::yolo {

struct YoloInputSpec final {
    std::size_t width{0U};
    std::size_t height{0U};
    std::size_t channels{3U};
    float scale{1.0F / 255.0F};
    float pad_value{114.0F};
};

struct LetterboxMetadata final {
    std::size_t source_width{0U};
    std::size_t source_height{0U};
    std::size_t input_width{0U};
    std::size_t input_height{0U};
    float scale{1.0F};
    std::size_t pad_left{0U};
    std::size_t pad_top{0U};
    std::size_t resized_width{0U};
    std::size_t resized_height{0U};
};

struct YoloInputTensor final {
    std::vector<float> chw{};
    LetterboxMetadata letterbox{};
};

class YoloPosePreprocessor final {
public:
    explicit YoloPosePreprocessor(YoloInputSpec spec);

    [[nodiscard]] const YoloInputSpec& spec() const noexcept { return spec_; }
    [[nodiscard]] YoloInputTensor preprocess(
        const application::ValidatedImage& image) const;

private:
    YoloInputSpec spec_{};
};

} // namespace fac_lpr::infrastructure::yolo
