#include <fac_lpr/application/config.hpp>
#include <fac_lpr/application/error.hpp>
#include <fac_lpr/fac_lpr_engine.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

std::uint32_t read_u32(const std::uint8_t* data) noexcept {
    std::uint32_t value = 0U;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

float read_float(const std::uint8_t* data) noexcept {
    return std::bit_cast<float>(read_u32(data));
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size) {
    if (data == nullptr || size < 20U) {
        return 0;
    }

    fac_lpr_engine_config_v1 abi_config{};
    std::memcpy(&abi_config, data, sizeof(abi_config));
    fac_lpr_engine_handle* handle = nullptr;
    (void)fac_lpr_engine_create_v1(&abi_config, &handle);
    (void)fac_lpr_engine_destroy_v1(&handle);

    fac_lpr::application::EngineConfig config{};
    config.detector.confidence_threshold = read_float(data);
    config.detector.nms_iou_threshold = read_float(data + 4U);
    config.detector.max_detections = static_cast<std::size_t>(read_u32(data + 8U));
    config.detector.tile_width = static_cast<std::size_t>(read_u32(data + 12U));
    config.detector.tile_height = static_cast<std::size_t>(read_u32(data + 16U));

    if (size >= 40U) {
        config.detector.tile_overlap_ratio = read_float(data + 20U);
        config.recognition.beam_width = static_cast<std::size_t>(read_u32(data + 24U));
        config.recognition.result_limit = static_cast<std::size_t>(read_u32(data + 28U));
        config.recognition.classes_per_step = static_cast<std::size_t>(read_u32(data + 32U));
        config.crop.max_hypotheses = static_cast<std::size_t>(read_u32(data + 36U));
    }

    try {
        fac_lpr::application::validate_engine_config(config);
    } catch (const fac_lpr::application::ConfigurationError&) {
    }
    return 0;
}
