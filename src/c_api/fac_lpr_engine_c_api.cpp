#include <fac_lpr/fac_lpr_engine.h>

#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/lpr_pipeline.hpp>
#include <fac_lpr/c_api/error_boundary.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <string>
#include <unordered_set>

struct fac_lpr_engine_handle final {
    std::uint32_t abi_version{FAC_LPR_ABI_VERSION_V1};
    std::shared_ptr<fac_lpr::application::LprPipeline> pipeline{};
};

namespace {

std::mutex g_handle_mutex{};
std::unordered_set<fac_lpr_engine_handle*> g_live_handles{};

[[nodiscard]] std::size_t checked_multiply(
    const std::size_t left,
    const std::size_t right,
    const char* field) {
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        throw fac_lpr::application::InvalidImageError(std::string{field} + " overflows size_t");
    }
    return left * right;
}

[[nodiscard]] std::size_t checked_add(
    const std::size_t left,
    const std::size_t right,
    const char* field) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        throw fac_lpr::application::InvalidImageError(std::string{field} + " overflows size_t");
    }
    return left + right;
}

void validate_config(const fac_lpr_engine_config_v1* config) {
    if (config == nullptr) {
        return;
    }
    if (config->struct_size < sizeof(fac_lpr_engine_config_v1)) {
        throw fac_lpr::application::ConfigurationError("C ABI v1 config struct is too small");
    }
    if (config->abi_version != FAC_LPR_ABI_VERSION_V1) {
        throw fac_lpr::application::ConfigurationError("unsupported C ABI version");
    }
    if (config->reserved_flags != 0U || config->reserved_zero != 0U) {
        throw fac_lpr::application::ConfigurationError("C ABI v1 reserved config fields must be zero");
    }
}

[[nodiscard]] fac_lpr::application::PixelFormat to_pixel_format(
    const fac_lpr_pixel_format format) {
    switch (format) {
        case FAC_LPR_PIXEL_FORMAT_GRAY8:
            return fac_lpr::application::PixelFormat::gray8;
        case FAC_LPR_PIXEL_FORMAT_BGR8:
            return fac_lpr::application::PixelFormat::bgr8;
        case FAC_LPR_PIXEL_FORMAT_RGB8:
            return fac_lpr::application::PixelFormat::rgb8;
    }
    throw fac_lpr::application::InvalidImageError("unsupported C ABI pixel format");
}

[[nodiscard]] fac_lpr::application::ImageView validate_image(
    const fac_lpr_image_view_v1* image) {
    if (image == nullptr) {
        throw fac_lpr::application::InvalidImageError("C ABI image is null");
    }
    if (image->struct_size < sizeof(fac_lpr_image_view_v1) ||
        image->abi_version != FAC_LPR_ABI_VERSION_V1) {
        throw fac_lpr::application::InvalidImageError("C ABI image struct/version is invalid");
    }
    if (image->data == nullptr || image->data_size == 0U ||
        image->width == 0U || image->height == 0U || image->stride_bytes == 0U) {
        throw fac_lpr::application::InvalidImageError("C ABI image fields are incomplete");
    }

    const auto format = to_pixel_format(image->pixel_format);
    const auto channels = fac_lpr::application::pixel_format_channels(format);
    const auto packed_row = checked_multiply(
        static_cast<std::size_t>(image->width),
        channels,
        "C ABI packed row");
    if (static_cast<std::size_t>(image->stride_bytes) < packed_row) {
        throw fac_lpr::application::InvalidImageError("C ABI image stride is too small");
    }
    const auto tail = checked_multiply(
        static_cast<std::size_t>(image->height) - 1U,
        static_cast<std::size_t>(image->stride_bytes),
        "C ABI image tail");
    const auto required = checked_add(tail, packed_row, "C ABI image extent");
    if (required > image->data_size) {
        throw fac_lpr::application::InvalidImageError("C ABI image buffer is too small");
    }

    return fac_lpr::application::ImageView{
        std::span<const std::byte>{
            reinterpret_cast<const std::byte*>(image->data),
            image->data_size},
        image->width,
        image->height,
        image->stride_bytes,
        format};
}

[[nodiscard]] std::shared_ptr<fac_lpr::application::LprPipeline> pipeline_for_live_handle(
    fac_lpr_engine_handle* handle) {
    std::scoped_lock lock{g_handle_mutex};
    if (handle == nullptr || !g_live_handles.contains(handle)) {
        throw fac_lpr::application::ConfigurationError("C ABI engine handle is null or invalid");
    }
    return handle->pipeline;
}

} // namespace

extern "C" fac_lpr_status FAC_LPR_CALL fac_lpr_engine_create_v1(
    const fac_lpr_engine_config_v1* config,
    fac_lpr_engine_handle** out_handle) {
    return fac_lpr::c_api::invoke_noexcept([&] {
        if (out_handle == nullptr) {
            throw fac_lpr::application::ConfigurationError("C ABI output handle pointer is null");
        }
        *out_handle = nullptr;
        validate_config(config);

        auto handle = std::make_unique<fac_lpr_engine_handle>();
        auto* raw = handle.get();
        {
            std::scoped_lock lock{g_handle_mutex};
            g_live_handles.insert(raw);
        }
        *out_handle = handle.release();
    });
}

extern "C" fac_lpr_status FAC_LPR_CALL fac_lpr_engine_recognize_v1(
    fac_lpr_engine_handle* handle,
    const fac_lpr_image_view_v1* image,
    void* output_buffer,
    const size_t output_capacity,
    size_t* required_output_size) {
    return fac_lpr::c_api::invoke_noexcept([&] {
        if (required_output_size == nullptr) {
            throw fac_lpr::application::ConfigurationError("C ABI required_output_size pointer is null");
        }
        *required_output_size = 0U;
        const auto pipeline = pipeline_for_live_handle(handle);
        (void)validate_image(image);
        if (output_capacity > 0U && output_buffer == nullptr) {
            throw fac_lpr::application::ConfigurationError("C ABI output buffer is null with non-zero capacity");
        }

        if (!pipeline) {
            throw fac_lpr::application::ConfigurationError(
                "C ABI engine composition is not configured yet");
        }

        /*
         * The concrete caller-owned v1 result layout is intentionally completed
         * by roadmap issue #36. Keeping this branch unreachable until composition
         * root wiring (#56) prevents fake inference/output semantics.
         */
        throw fac_lpr::application::ConfigurationError(
            "C ABI v1 result buffer contract is not configured yet");
    });
}

extern "C" fac_lpr_status FAC_LPR_CALL fac_lpr_engine_destroy_v1(
    fac_lpr_engine_handle** handle) {
    return fac_lpr::c_api::invoke_noexcept([&] {
        if (handle == nullptr || *handle == nullptr) {
            return;
        }

        auto* raw = *handle;
        bool owned = false;
        {
            std::scoped_lock lock{g_handle_mutex};
            const auto iterator = g_live_handles.find(raw);
            if (iterator != g_live_handles.end()) {
                g_live_handles.erase(iterator);
                owned = true;
            }
        }
        *handle = nullptr;
        if (owned) {
            delete raw;
        }
    });
}
