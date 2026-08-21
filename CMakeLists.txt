cmake_minimum_required(VERSION 3.25)

project(
    fac_lpr_engine
    VERSION 0.1.0
    DESCRIPTION "Reusable production-grade license plate recognition engine"
    LANGUAGES CXX
)

option(FAC_LPR_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(FAC_LPR_BUILD_MODEL_INSPECTOR "Build the ONNX model inspector CLI" OFF)
option(FAC_LPR_BUILD_TESTS "Build FAC LPR Engine tests" OFF)

if(FAC_LPR_ONNXRUNTIME_ROOT)
    include(cmake/OnnxRuntimePrebuilt.cmake)
    fac_lpr_import_onnxruntime("${FAC_LPR_ONNXRUNTIME_ROOT}")
endif()

add_subdirectory(src/domain)
add_subdirectory(src/application)
add_subdirectory(src/infrastructure)

add_library(fac_lpr_engine STATIC
    src/fac_lpr_engine.cpp
)

add_library(fac_lpr::engine ALIAS fac_lpr_engine)

target_compile_features(fac_lpr_engine PUBLIC cxx_std_20)
set_target_properties(fac_lpr_engine PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(fac_lpr_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(fac_lpr_engine
    PUBLIC fac_lpr::domain
    PRIVATE fac_lpr::infrastructure
)

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

if(FAC_LPR_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
