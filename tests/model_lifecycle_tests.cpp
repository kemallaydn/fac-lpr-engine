#include <fac_lpr/application/error.hpp>
#include <fac_lpr/infrastructure/model/model_lifecycle.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
using fac_lpr::infrastructure::model::ModelLifecycleManager;
using fac_lpr::infrastructure::model::ModelManifest;
using fac_lpr::infrastructure::model::ModelManifestEntry;

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("fac-lpr-model-test-" + std::to_string(nonce));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error{};
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

    void write(const std::filesystem::path& relative, const std::string& content) const {
        const auto full = path_ / relative;
        std::filesystem::create_directories(full.parent_path());
        std::ofstream stream(full, std::ios::binary);
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

private:
    std::filesystem::path path_{};
};

constexpr auto abc_sha256 =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

ModelManifest manifest_for(
    const std::filesystem::path& root,
    std::string hash = abc_sha256,
    std::size_t size = 3U) {
    ModelManifest manifest{};
    manifest.root_directory = root;
    manifest.entries.push_back(ModelManifestEntry{
        .name = "detector-primary",
        .type = "detector",
        .version = "1.2.3",
        .relative_path = "detector.onnx",
        .sha256 = std::move(hash),
        .size_bytes = size});
    return manifest;
}

TEST(ModelLifecycle, ValidManifestProducesDiagnosticsReadyActiveMetadata) {
    TemporaryDirectory directory{};
    directory.write("detector.onnx", "abc");

    const ModelLifecycleManager manager{manifest_for(directory.path())};
    const auto active = manager.validate_and_activate();

    ASSERT_EQ(active.size(), 1U);
    EXPECT_EQ(active[0].name, "detector-primary");
    EXPECT_EQ(active[0].type, "detector");
    EXPECT_EQ(active[0].version, "1.2.3");
    EXPECT_EQ(active[0].sha256, abc_sha256);
    EXPECT_EQ(active[0].size_bytes, 3U);
    EXPECT_TRUE(active[0].resolved_path.is_absolute());
    EXPECT_TRUE(std::filesystem::is_regular_file(active[0].resolved_path));
}

TEST(ModelLifecycle, ChecksumMismatchPreventsActivation) {
    TemporaryDirectory directory{};
    directory.write("detector.onnx", "abc");
    auto manifest = manifest_for(
        directory.path(),
        "0000000000000000000000000000000000000000000000000000000000000000");

    const ModelLifecycleManager manager{std::move(manifest)};
    EXPECT_THROW(manager.validate_and_activate(), fac_lpr::application::ModelLoadError);
}

TEST(ModelLifecycle, SizeMismatchPreventsActivation) {
    TemporaryDirectory directory{};
    directory.write("detector.onnx", "abc");

    const ModelLifecycleManager manager{manifest_for(directory.path(), abc_sha256, 4U)};
    EXPECT_THROW(manager.validate_and_activate(), fac_lpr::application::ModelLoadError);
}

TEST(ModelLifecycle, ParentTraversalIsRejectedBeforeFilesystemAccess) {
    auto manifest = manifest_for("models");
    manifest.entries[0].relative_path = "../secret.onnx";
    EXPECT_THROW(ModelLifecycleManager{std::move(manifest)}, fac_lpr::application::ConfigurationError);
}

TEST(ModelLifecycle, AbsoluteModelPathIsRejected) {
    auto manifest = manifest_for("models");
    manifest.entries[0].relative_path = std::filesystem::temp_directory_path() / "model.onnx";
    EXPECT_THROW(ModelLifecycleManager{std::move(manifest)}, fac_lpr::application::ConfigurationError);
}

TEST(ModelLifecycle, DuplicateTypeAndNameIsRejected) {
    auto manifest = manifest_for("models");
    manifest.entries.push_back(manifest.entries.front());
    manifest.entries.back().version = "2.0.0";
    EXPECT_THROW(ModelLifecycleManager{std::move(manifest)}, fac_lpr::application::ConfigurationError);
}

TEST(ModelLifecycle, ActivationIsAllOrNothingWhenAnyArtifactFails) {
    TemporaryDirectory directory{};
    directory.write("detector.onnx", "abc");
    directory.write("ocr.onnx", "abc");

    auto manifest = manifest_for(directory.path());
    manifest.entries.push_back(ModelManifestEntry{
        .name = "ocr-primary",
        .type = "recognizer",
        .version = "4.5.6",
        .relative_path = "ocr.onnx",
        .sha256 = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        .size_bytes = 3U});

    const ModelLifecycleManager manager{std::move(manifest)};
    EXPECT_THROW(manager.validate_and_activate(), fac_lpr::application::ModelLoadError);
}

TEST(ModelLifecycle, InvalidShaAndOversizedManifestEntriesFailFast) {
    auto invalid_sha = manifest_for("models");
    invalid_sha.entries[0].sha256 = "not-a-sha";
    EXPECT_THROW(ModelLifecycleManager{std::move(invalid_sha)}, fac_lpr::application::ConfigurationError);

    auto oversized = manifest_for("models");
    oversized.maximum_model_bytes = 2U;
    EXPECT_THROW(ModelLifecycleManager{std::move(oversized)}, fac_lpr::application::ConfigurationError);
}

} // namespace
