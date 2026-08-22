#pragma once

#include <fac_lpr/application/lpr_pipeline.hpp>

#include <filesystem>
#include <memory>

namespace fac_lpr::cli {

[[nodiscard]] std::shared_ptr<application::LprPipeline> build_pipeline_from_contract(
    const std::filesystem::path& model_directory,
    const std::filesystem::path& contract_path);

} // namespace fac_lpr::cli
