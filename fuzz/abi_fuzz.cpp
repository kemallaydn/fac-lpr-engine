#include <fac_lpr/fac_lpr_engine.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

std::uint32_t read_u32(const std::uint8_t* data) noexcept {
    std::uint32_t value = 0U;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size) {
    if (data == nullptr || size < 24U) {
        return 0;
    }

    fac_lpr_engine_config_v1 config = FAC_LPR_ENGINE_CONFIG_V1_INIT;
    fac_lpr_engine_handle* handle = nullptr;
    if (fac_lpr_engine_create_v1(&config, &handle) != FAC_LPR_STATUS_OK || handle == nullptr) {
        return 0;
    }

    fac_lpr_image_view_v1 image = FAC_LPR_IMAGE_VIEW_V1_INIT;
    image.struct_size = read_u32(data);
    image.abi_version = read_u32(data + 4U);
    image.width = read_u32(data + 8U);
    image.height = read_u32(data + 12U);
    image.stride_bytes = read_u32(data + 16U);
    image.pixel_format = static_cast<fac_lpr_pixel_format>(data[20U]);
    image.data = data + 21U;
    image.data_size = size - 21U;

    std::array<std::byte, 64U * 1024U> output{};
    const auto requested_capacity = static_cast<std::size_t>(read_u32(data + 4U)) % (output.size() + 1U);
    void* output_pointer = (data[21U] & 1U) != 0U ? output.data() : nullptr;
    std::size_t required = 0U;
    std::size_t* required_pointer = (data[22U] & 1U) != 0U ? &required : nullptr;

    (void)fac_lpr_engine_recognize_v1(
        handle,
        (data[23U] & 1U) != 0U ? &image : nullptr,
        output_pointer,
        requested_capacity,
        required_pointer);

    (void)fac_lpr_engine_destroy_v1(&handle);
    (void)fac_lpr_engine_destroy_v1(&handle);
    return 0;
}
