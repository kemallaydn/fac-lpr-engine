find_package(nlohmann_json CONFIG REQUIRED)

add_library(fac_lpr_json_config STATIC
    json_config_adapter.cpp
    calibration_json_adapter.cpp
)
add_library(fac_lpr::json_config ALIAS fac_lpr_json_config)
target_compile_features(fac_lpr_json_config PUBLIC cxx_std_20)
target_include_directories(fac_lpr_json_config PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(fac_lpr_json_config
    PUBLIC fac_lpr::application
    PRIVATE nlohmann_json::nlohmann_json
)
