#pragma once

#include <fac_lpr/application/lpr_pipeline.hpp>

#include <filesystem>
#include <memory>

namespace fac_lpr::cli {

void activate_production_pipeline(
    const std::shared_ptr<application::LprPipeline>& pipeline,
    const std::filesystem::path& model_directory,
    const std::filesystem::path& contract_path);

} // namespace fac_lpr::cli
