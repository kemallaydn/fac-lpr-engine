#pragma once

#include <fac_lpr/application/config.hpp>

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace fac_lpr::infrastructure {

inline constexpr std::uint32_t k_engine_config_schema_version = 1U;

enum class UnknownFieldPolicy {
    reject,
    ignore,
};

struct JsonConfigLoadOptions final {
    UnknownFieldPolicy unknown_fields{UnknownFieldPolicy::reject};
};

[[nodiscard]] application::EngineConfig load_engine_config_json(
    std::string_view json_text,
    JsonConfigLoadOptions options = {});

[[nodiscard]] application::EngineConfig load_engine_config_json_file(
    const std::filesystem::path& path,
    JsonConfigLoadOptions options = {});

} // namespace fac_lpr::infrastructure
