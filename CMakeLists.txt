cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
endif()

project(
    fac_lpr_engine
    VERSION 0.1.0
    DESCRIPTION "Reusable production-grade license plate recognition engine"
    LANGUAGES C CXX
)

if(APPLE)
    add_link_options("LINKER:-no_warn_duplicate_libraries")
endif()

option(FAC_LPR_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(FAC_LPR_BUILD_SHARED "Build fac_lpr_engine as a shared library" OFF)
option(FAC_LPR_BUILD_MODEL_INSPECTOR "Build the ONNX model inspector CLI" OFF)
option(FAC_LPR_BUILD_WORKSPACE_PROBE "Build the workspace buffer reuse probe" OFF)
option(FAC_LPR_BUILD_LPR_CLI "Build the offline JPG/PNG recognition CLI" OFF)
option(FAC_LPR_BUILD_MEMORY_STRESS "Build the long-run memory stress harness" OFF)
option(FAC_LPR_BUILD_BENCHMARK "Build the stage latency benchmark CLI" OFF)
option(FAC_LPR_BUILD_FUZZERS "Build libFuzzer sanitizer targets" OFF)
option(FAC_LPR_BUILD_TESTS "Build FAC LPR Engine tests" OFF)

if(FAC_LPR_BUILD_SHARED)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(FAC_LPR_LIBRARY_TYPE SHARED)
else()
    set(FAC_LPR_LIBRARY_TYPE STATIC)
endif()

include(cmake/Sanitizers.cmake)
include(cmake/StaticAnalysis.cmake)
include(cmake/Coverage.cmake)

if(FAC_LPR_BUILD_TESTS)
    include(CTest)
    enable_testing()
endif()

if(FAC_LPR_ONNXRUNTIME_ROOT)
    include(cmake/OnnxRuntimePrebuilt.cmake)
    fac_lpr_import_onnxruntime("${FAC_LPR_ONNXRUNTIME_ROOT}")
endif()

add_subdirectory(src/domain)
add_subdirectory(src/application)
add_subdirectory(src/infrastructure)

add_library(fac_lpr_engine ${FAC_LPR_LIBRARY_TYPE}
    src/fac_lpr_engine.cpp
    src/c_api/fac_lpr_engine_c_api.cpp
    src/c_api/result_buffer.cpp
)

add_library(fac_lpr::engine ALIAS fac_lpr_engine)

target_compile_features(fac_lpr_engine PUBLIC cxx_std_20)
set_target_properties(fac_lpr_engine PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_include_directories(fac_lpr_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(fac_lpr_engine
    PRIVATE
        fac_lpr::domain
        fac_lpr::infrastructure
)

target_compile_definitions(fac_lpr_engine PRIVATE
    FAC_LPR_ENGINE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    FAC_LPR_ENGINE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    FAC_LPR_ENGINE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

if(FAC_LPR_BUILD_SHARED)
    target_compile_definitions(fac_lpr_engine
        PUBLIC FAC_LPR_SHARED
        PRIVATE FAC_LPR_EXPORTS
    )
endif()

if(MSVC)
    target_compile_options(fac_lpr_engine PRIVATE
        /W4
        /permissive-
        /EHsc
        $<$<BOOL:${FAC_LPR_WARNINGS_AS_ERRORS}>:/WX>
    )
else()
    target_compile_options(fac_lpr_engine PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        $<$<BOOL:${FAC_LPR_WARNINGS_AS_ERRORS}>:-Werror>
    )
endif()

set(FAC_LPR_OUTPUT_ROOT "${CMAKE_BINARY_DIR}/artifacts")
set_target_properties(fac_lpr_engine PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${FAC_LPR_OUTPUT_ROOT}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${FAC_LPR_OUTPUT_ROOT}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${FAC_LPR_OUTPUT_ROOT}/bin"
)

if(FAC_LPR_BUILD_MODEL_INSPECTOR)
    add_subdirectory(tools/model-inspector)
endif()

if(FAC_LPR_BUILD_WORKSPACE_PROBE)
    add_subdirectory(tools/workspace-probe)
endif()

if(FAC_LPR_BUILD_LPR_CLI)
    add_subdirectory(tools/lpr-cli)
endif()

if(FAC_LPR_BUILD_MEMORY_STRESS)
    add_subdirectory(tools/memory-stress)
endif()

if(FAC_LPR_BUILD_BENCHMARK)
    add_subdirectory(tools/benchmark)
endif()

if(FAC_LPR_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/onnxruntime-version.txt" FAC_LPR_PACKAGE_ONNXRUNTIME_VERSION LIMIT_COUNT 1)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/runtime-manifest.json.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/runtime-manifest.json"
    @ONLY
)

set(FAC_LPR_CMAKE_PACKAGE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/fac-lpr-engine")
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fac-lpr-engine-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/fac-lpr-engine-config.cmake"
    INSTALL_DESTINATION "${FAC_LPR_CMAKE_PACKAGE_DIR}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/generated/fac-lpr-engine-config-version.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

# A static fac_lpr_engine archive keeps link-only references to its internal
# component targets. Exporting only the facade therefore produces an invalid
# CMake package. Keep every transitive internal target in the same export set
# and preserve the public fac_lpr::<component> target names for consumers.
set_target_properties(fac_lpr_engine PROPERTIES EXPORT_NAME engine)
set_target_properties(fac_lpr_domain PROPERTIES EXPORT_NAME domain)
set_target_properties(fac_lpr_application PROPERTIES EXPORT_NAME application)
set_target_properties(fac_lpr_native_image PROPERTIES EXPORT_NAME native_image)
set_target_properties(fac_lpr_crypto PROPERTIES EXPORT_NAME crypto)
set_target_properties(fac_lpr_model PROPERTIES EXPORT_NAME model)
set_target_properties(fac_lpr_paddle_adapter PROPERTIES EXPORT_NAME paddle_adapter)
set_target_properties(fac_lpr_yolo PROPERTIES EXPORT_NAME yolo)
set_target_properties(fac_lpr_lprnet PROPERTIES EXPORT_NAME lprnet)
set_target_properties(fac_lpr_geometry PROPERTIES EXPORT_NAME geometry)
set_target_properties(fac_lpr_adaptive_detection PROPERTIES EXPORT_NAME adaptive_detection)
set_target_properties(fac_lpr_crop PROPERTIES EXPORT_NAME crop)
set_target_properties(fac_lpr_concurrency PROPERTIES EXPORT_NAME concurrency)
set_target_properties(fac_lpr_json_config PROPERTIES EXPORT_NAME json_config)
set_target_properties(fac_lpr_infrastructure PROPERTIES EXPORT_NAME infrastructure)

set(FAC_LPR_EXPORT_TARGETS
    fac_lpr_engine
    fac_lpr_domain
    fac_lpr_application
    fac_lpr_native_image
    fac_lpr_crypto
    fac_lpr_model
    fac_lpr_paddle_adapter
    fac_lpr_yolo
    fac_lpr_lprnet
    fac_lpr_geometry
    fac_lpr_adaptive_detection
    fac_lpr_crop
    fac_lpr_concurrency
    fac_lpr_json_config
    fac_lpr_infrastructure
)

if(FAC_LPR_WITH_ONNX_RUNTIME)
    set_target_properties(fac_lpr_onnx_runtime PROPERTIES EXPORT_NAME onnx_runtime)
    set_target_properties(fac_lpr_generic_onnx_ocr PROPERTIES EXPORT_NAME generic_onnx_ocr)
    set_target_properties(fac_lpr_lprnet_onnx PROPERTIES EXPORT_NAME lprnet_onnx)
    set_target_properties(fac_lpr_yolo_onnx PROPERTIES EXPORT_NAME yolo_onnx)
    list(APPEND FAC_LPR_EXPORT_TARGETS
        fac_lpr_onnx_runtime
        fac_lpr_generic_onnx_ocr
        fac_lpr_lprnet_onnx
        fac_lpr_yolo_onnx
    )
endif()

if(FAC_LPR_WITH_OPENCV)
    set_target_properties(fac_lpr_opencv_adapter PROPERTIES EXPORT_NAME opencv_adapter)
    list(APPEND FAC_LPR_EXPORT_TARGETS fac_lpr_opencv_adapter)
endif()

if(FAC_LPR_WITH_ONNX_RUNTIME AND FAC_LPR_WITH_OPENCV)
    set_target_properties(fac_lpr_production_factory PROPERTIES EXPORT_NAME production_factory)
    list(APPEND FAC_LPR_EXPORT_TARGETS fac_lpr_production_factory)
endif()

install(
    TARGETS ${FAC_LPR_EXPORT_TARGETS}
    EXPORT fac_lpr_engine_targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/fac_lpr DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/runtime-manifest.json"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/fac-lpr-engine
)
install(
    EXPORT fac_lpr_engine_targets
    FILE fac-lpr-engine-targets.cmake
    NAMESPACE fac_lpr::
    DESTINATION "${FAC_LPR_CMAKE_PACKAGE_DIR}"
)
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/generated/fac-lpr-engine-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/fac-lpr-engine-config-version.cmake"
    DESTINATION "${FAC_LPR_CMAKE_PACKAGE_DIR}"
)

if(FAC_LPR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
