add_library(fac_lpr_infrastructure INTERFACE)
add_library(fac_lpr::infrastructure ALIAS fac_lpr_infrastructure)

target_link_libraries(
    fac_lpr_infrastructure
    INTERFACE
        fac_lpr::application
        fac_lpr::domain
)

option(FAC_LPR_WITH_SPDLOG "Build the optional spdlog logger adapter" OFF)
option(FAC_LPR_WITH_ONNX_RUNTIME "Build ONNX Runtime infrastructure" OFF)

if(FAC_LPR_WITH_SPDLOG)
    find_package(spdlog CONFIG REQUIRED)

    add_library(fac_lpr_spdlog_adapter STATIC
        spdlog_logger.cpp
    )
    add_library(fac_lpr::spdlog_adapter ALIAS fac_lpr_spdlog_adapter)

    target_compile_features(fac_lpr_spdlog_adapter PUBLIC cxx_std_20)
    target_include_directories(
        fac_lpr_spdlog_adapter
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(
        fac_lpr_spdlog_adapter
        PUBLIC fac_lpr::application
        PRIVATE spdlog::spdlog
    )
endif()

if(FAC_LPR_WITH_ONNX_RUNTIME)
    find_package(onnxruntime CONFIG REQUIRED)

    add_library(fac_lpr_onnx_runtime STATIC
        onnx/onnx_session.cpp
    )
    add_library(fac_lpr::onnx_runtime ALIAS fac_lpr_onnx_runtime)

    target_compile_features(fac_lpr_onnx_runtime PUBLIC cxx_std_20)
    target_include_directories(
        fac_lpr_onnx_runtime
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(
        fac_lpr_onnx_runtime
        PUBLIC fac_lpr::application
        PRIVATE onnxruntime::onnxruntime
    )
endif()
