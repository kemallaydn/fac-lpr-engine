cmake_minimum_required(VERSION 3.25)

project(
    fac_lpr_engine
    VERSION 0.1.0
    DESCRIPTION "Reusable production-grade license plate recognition engine"
    LANGUAGES C CXX
)

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
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/onnxruntime-version.txt" FAC_LPR_PACKAGE_ONNXRUNTIME_VERSION LIMIT_COUNT 1)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/runtime-manifest.json.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/runtime-manifest.json"
    @ONLY
)
install(
    TARGETS fac_lpr_engine
    EXPORT fac_lpr_engine_targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/fac_lpr DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/runtime-manifest.json"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/fac-lpr-engine
)
install(
    EXPORT fac_lpr_engine_targets
    FILE fac_lpr_engine-targets.cmake
    NAMESPACE fac_lpr::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fac_lpr_engine
)

if(FAC_LPR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
