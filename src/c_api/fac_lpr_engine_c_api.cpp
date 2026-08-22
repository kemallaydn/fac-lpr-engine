#include <fac_lpr/fac_lpr_engine.h>

#include <fac_lpr/application/error.hpp>
#include <fac_lpr/application/image_validation.hpp>
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
        {
            std::scoped_lock lock{g_handle_mutex};
            g_live_handles.insert(handle.get());
        }
        *out_handle = handle.release();
        return FAC_LPR_STATUS_OK;
    });
}

extern "C" fac_lpr_status FAC_LPR_CALL fac_lpr_engine_destroy_v1(
    fac_lpr_engine_handle* handle) {
    return invoke_c_api([&]() -> fac_lpr_status {
        if (handle == nullptr) {
            return FAC_LPR_STATUS_OK;
        }
        {
            std::scoped_lock lock{g_handle_mutex};
            const auto erased = g_live_handles.erase(handle);
            if (erased == 0U) {
                throw fac_lpr::application::ConfigurationError("C ABI engine handle is invalid or already destroyed");
            }
        }
        delete handle;
        return FAC_LPR_STATUS_OK;
    });
}

extern "C" fac_lpr_status FAC_LPR_CALL fac_lpr_engine_recognize_v1(
    fac_lpr_engine_handle* handle,
    const fac_lpr_image_view_v1* image,
    fac_lpr_result_buffer_v1* result) {
    return invoke_c_api([&]() -> fac_lpr_status {
        if (result == nullptr) {
            throw fac_lpr::application::ConfigurationError("C ABI result buffer is null");
        }
        if (result->struct_size < sizeof(fac_lpr_result_buffer_v1) ||
            result->abi_version != FAC_LPR_ABI_VERSION_V1) {
            throw fac_lpr::application::ConfigurationError("C ABI result buffer struct/version is invalid");
        }

        const auto image_view = validate_image(image);
        const auto pipeline = pipeline_for_live_handle(handle);
        if (!pipeline) {
            throw fac_lpr::application::ConfigurationError("C ABI engine handle has no configured pipeline");
        }

        const auto recognition = pipeline->recognize(image_view);
        fac_lpr::c_api::write_result_buffer(recognition, result);
        return FAC_LPR_STATUS_OK;
    });
}

extern "C" const char* FAC_LPR_CALL fac_lpr_last_error_v1(void) {
    return g_last_error.c_str();
}
