#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace fac_lpr::infrastructure::model {

struct ModelManifestEntry final {
    std::string name{};
    std::string type{};
    std::string version{};
    std::filesystem::path relative_path{};
    std::string sha256{};
    std::size_t size_bytes{0U};
};

struct ModelManifest final {
    std::filesystem::path root_directory{};
    std::vector<ModelManifestEntry> entries{};
    std::size_t maximum_model_bytes{512U * 1024U * 1024U};
};

struct ActiveModelInfo final {
    std::string name{};
    std::string type{};
    std::string version{};
    std::filesystem::path resolved_path{};
    std::string sha256{};
    std::size_t size_bytes{0U};
    bool integrity_verified{false};
};

class ModelLifecycleManager final {
public:
    explicit ModelLifecycleManager(ModelManifest manifest);

    [[nodiscard]] std::vector<ActiveModelInfo> validate_and_activate() const;
    [[nodiscard]] const ModelManifest& manifest() const noexcept { return manifest_; }

private:
    ModelManifest manifest_{};
};

} // namespace fac_lpr::infrastructure::model
