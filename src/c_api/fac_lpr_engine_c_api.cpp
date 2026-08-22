#include <fac_lpr/fac_lpr_engine.h>

#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/lpr_pipeline.hpp>
#include <fac_lpr/c_api/error_boundary.hpp>
#include <fac_lpr/c_api/result_buffer.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>

struct fac_lpr_engine_handle final {
    std::uint32_t abi_version{FAC_LPR_ABI_VERSION_V1};
    std::shared_ptr<fac_lpr::application::LprPipeline> pipeline{};
};

namespace {

std::mutex g_handle_mutex{};
std::unordered_set<fac_lpr_engine_handle*> g_live_handles{};
thread_local std::string g_last_error{};

void store_last_error(const std::string_view message) noexcept {
    try {
        g_last_error.assign(message.data(), message.size());
    } catch (...) {
        g_last_error.clear();
    }
}

template <typename Function>
[[nodiscard]] fac_lpr_status invoke_c_api(Function&& function) noexcept {
    g_last_error.clear();
    try {
        return std::forward<Function>(function)();
    } catch (const fac_lpr::application::EngineError& error) {
        store_last_error(error.what());
        return fac_lpr::c_api::to_c_status(error.code());
    } catch (const std::bad_alloc&) {
        store_last_error("C ABI allocation failed");
        return FAC_LPR_STATUS_RESOURCE_EXHAUSTED;
    } catch (...) {
        store_last_error("C ABI internal error");
        return FAC_LPR_STATUS_INTERNAL_ERROR;
    }
}

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
        case FAC_LPR_PIXEL_FORMAT_GRAY8: return fac_lpr::application::PixelFormat::gray8;
        case FAC_LPR_PIXEL_FORMAT_BGR8: return fac_lpr::application::PixelFormat::bgr8;
        case FAC_LPR_PIXEL_FORMAT_RGB8: return fac_lpr::application::PixelFormat::rgb8;
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
        static_cast<std::size_t>(image->width), channels, "C ABI packed row");
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
            reinterpret_cast<const std::byte*>(image->data), image->data_size},
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

extern "C" fac_lpr_status FAC_LPR_CALL fac_lpr_get_version_v1(
    fac_lpr_version_info_v1* out_version) {
    return invoke_c_api([&]() -> fac_lpr_status {
        if (out_version == nullptr) {
            throw fac_lpr::application::ConfigurationError("C ABI version output pointer is null");
        }
        if (out_version->struct_size < sizeof(fac_lpr_version_info_v1)) {
            throw fac_lpr::application::ConfigurationError("C ABI version info struct is too small");
        }
        if (out_version->abi_version != FAC_LPR_ABI_VERSION_V1) {
            throw fac_lpr::application::ConfigurationError("unsupported version query ABI");
        }
        out_version->semantic_major = FAC_LPR_ENGINE_VERSION_MAJOR;
        out_version->semantic_minor = FAC_LPR_ENGINE_VERSION_MINOR;
        out_version->semantic_patch = FAC_LPR_ENGINE_VERSION_PATCH;
        out_version->abi_major = FAC_LPR_ABI_VERSION_V1;
        return FAC_LPR_STATUS_OK;
    });
}

extern "C" fac_lpr_status FAC_LPR_CALL fac_lpr_engine_create_v1(
    const fac_lpr_engine_config_v1* config,
    fac_lpr_engine_handle** out_handle) {
    return invoke_c_api([&]() -> fac_lpr_status {
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
        return FAC_LPR_STATUS_OK;
    });
}

extern "C" fac_lpr_status FAC_LPR_CALL fac_lpr_engine_recognize_v1(
    fac_lpr_engine_handle* handle,
    const fac_lpr_image_view_v1* image,
    void* output_buffer,
    const size_t output_capacity,
    size_t* required_output_size) {
    return invoke_c_api([&]() -> fac_lpr_status {
        if (required_output_size == nullptr) {
            throw fac_lpr::application::ConfigurationError("C ABI required_output_size pointer is null");
        }
        *required_output_size = 0U;
        const auto pipeline = pipeline_for_live_handle(handle);
        const auto validated_image = validate_image(image);
        if (output_capacity > 0U && output_buffer == nullptr) {
            throw fac_lpr::application::ConfigurationError(
                "C ABI output buffer is null with non-zero capacity");
        }
        if (!pipeline) {
            throw fac_lpr::application::ConfigurationError(
                "C ABI engine composition is not configured yet");
        }

        const auto result = pipeline->recognize(validated_image);
        const auto status = fac_lpr::c_api::serialize_result_v1(
            result,
            output_buffer,
            output_capacity,
            required_output_size);
        if (status == FAC_LPR_STATUS_BUFFER_TOO_SMALL) {
            store_last_error("C ABI output buffer is too small");
        } else if (status != FAC_LPR_STATUS_OK) {
            store_last_error("C ABI result serialization failed");
        }
        return status;
    });
}

extern "C" fac_lpr_status FAC_LPR_CALL fac_lpr_engine_destroy_v1(
    fac_lpr_engine_handle** handle) {
    return invoke_c_api([&]() -> fac_lpr_status {
        if (handle == nullptr || *handle == nullptr) {
            return FAC_LPR_STATUS_OK;
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
        return FAC_LPR_STATUS_OK;
    });
}

extern "C" fac_lpr_status FAC_LPR_CALL fac_lpr_get_last_error_v1(
    char* buffer,
    const size_t buffer_capacity,
    size_t* required_size) {
    if (required_size == nullptr) {
        return FAC_LPR_STATUS_CONFIGURATION_ERROR;
    }
    if (g_last_error.size() == std::numeric_limits<std::size_t>::max()) {
        *required_size = 0U;
        return FAC_LPR_STATUS_RESOURCE_EXHAUSTED;
    }

    const auto required = g_last_error.size() + 1U;
    *required_size = required;
    if (buffer == nullptr || buffer_capacity < required) {
        return FAC_LPR_STATUS_BUFFER_TOO_SMALL;
    }

    if (!g_last_error.empty()) {
        std::memcpy(buffer, g_last_error.data(), g_last_error.size());
    }
    buffer[g_last_error.size()] = '\0';
    return FAC_LPR_STATUS_OK;
}
