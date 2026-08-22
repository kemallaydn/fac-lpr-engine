#include <fac_lpr/infrastructure/model/model_lifecycle.hpp>

#include <fac_lpr/application/error.hpp>
#include <fac_lpr/infrastructure/crypto/sha256.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

namespace fac_lpr::infrastructure::model {
namespace {

[[nodiscard]] bool is_hex_sha256(const std::string& value) noexcept {
    return value.size() == 64U && std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
}

[[nodiscard]] std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

[[nodiscard]] bool contains_parent_traversal(const std::filesystem::path& path) {
    return std::any_of(path.begin(), path.end(), [](const std::filesystem::path& component) {
        return component == "..";
    });
}

[[nodiscard]] bool path_is_within(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) {
    auto root_it = root.begin();
    auto candidate_it = candidate.begin();
    for (; root_it != root.end(); ++root_it, ++candidate_it) {
        if (candidate_it == candidate.end() || *root_it != *candidate_it) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<std::byte> read_file_bounded(
    const std::filesystem::path& path,
    const std::size_t expected_size,
    const std::size_t maximum_size) {
    if (expected_size == 0U || expected_size > maximum_size) {
        throw application::ModelLoadError("model size is outside configured limits");
    }

    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw application::ModelLoadError("cannot open model file");
    }
    const auto end = stream.tellg();
    if (end < 0) {
        throw application::ModelLoadError("cannot determine model file size");
    }
    const auto actual_size = static_cast<std::uintmax_t>(end);
    if (actual_size != expected_size) {
        throw application::ModelLoadError("model file size does not match manifest");
    }
    if (actual_size > maximum_size) {
        throw application::ModelLoadError("model file exceeds configured size limit");
    }

    std::vector<std::byte> bytes(expected_size);
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        throw application::ModelLoadError("cannot read complete model file");
    }
    return bytes;
}

} // namespace

ModelLifecycleManager::ModelLifecycleManager(ModelManifest manifest)
    : manifest_(std::move(manifest)) {
    if (manifest_.root_directory.empty()) {
        throw application::ConfigurationError("model manifest root_directory is required");
    }
    if (manifest_.entries.empty()) {
        throw application::ConfigurationError("model manifest requires at least one entry");
    }
    if (manifest_.maximum_model_bytes == 0U ||
        manifest_.maximum_model_bytes > (2ULL * 1024ULL * 1024ULL * 1024ULL)) {
        throw application::ConfigurationError("maximum_model_bytes is outside safe limits");
    }

    std::set<std::pair<std::string, std::string>> identities{};
    for (const auto& entry : manifest_.entries) {
        if (entry.name.empty() || entry.type.empty() || entry.version.empty()) {
            throw application::ConfigurationError("model name, type and version are required");
        }
        if (entry.relative_path.empty() || entry.relative_path.is_absolute()) {
            throw application::ConfigurationError("model path must be non-empty and relative");
        }
        if (contains_parent_traversal(entry.relative_path.lexically_normal())) {
            throw application::ConfigurationError("model path traversal is not allowed");
        }
        if (!is_hex_sha256(entry.sha256)) {
            throw application::ConfigurationError("model sha256 must contain exactly 64 hex characters");
        }
        if (entry.size_bytes == 0U || entry.size_bytes > manifest_.maximum_model_bytes) {
            throw application::ConfigurationError("model size is outside configured limits");
        }
        if (!identities.emplace(entry.type, entry.name).second) {
            throw application::ConfigurationError("duplicate model type/name in manifest");
        }
    }
}

std::vector<ActiveModelInfo> ModelLifecycleManager::validate_and_activate() const {
    std::error_code error{};
    const auto canonical_root = std::filesystem::weakly_canonical(manifest_.root_directory, error);
    if (error || canonical_root.empty() || !std::filesystem::is_directory(canonical_root)) {
        throw application::ModelLoadError("model root directory is unavailable");
    }

    std::vector<ActiveModelInfo> validated{};
    validated.reserve(manifest_.entries.size());

    for (const auto& entry : manifest_.entries) {
        error.clear();
        const auto candidate = std::filesystem::weakly_canonical(
            canonical_root / entry.relative_path,
            error);
        if (error || candidate.empty() || !path_is_within(canonical_root, candidate)) {
            throw application::ModelLoadError("model path escapes configured root");
        }
        if (!std::filesystem::is_regular_file(candidate, error) || error) {
            throw application::ModelLoadError("model path is not a regular file");
        }

        const auto bytes = read_file_bounded(
            candidate,
            entry.size_bytes,
            manifest_.maximum_model_bytes);
        const auto actual_hash = lower_ascii(crypto::sha256_hex(bytes));
        if (actual_hash != lower_ascii(entry.sha256)) {
            throw application::ModelLoadError("model checksum does not match manifest");
        }

        validated.push_back(ActiveModelInfo{
            .name = entry.name,
            .type = entry.type,
            .version = entry.version,
            .resolved_path = candidate,
            .sha256 = actual_hash,
            .size_bytes = entry.size_bytes});
    }

    return validated;
}

} // namespace fac_lpr::infrastructure::model
