if(NOT FAC_LPR_WITH_ONNX_RUNTIME)
    message(FATAL_ERROR "FAC_LPR_BUILD_BENCHMARK requires FAC_LPR_WITH_ONNX_RUNTIME=ON")
endif()
if(NOT FAC_LPR_WITH_OPENCV)
    message(FATAL_ERROR "FAC_LPR_BUILD_BENCHMARK requires FAC_LPR_WITH_OPENCV=ON")
endif()

find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc imgcodecs)

add_executable(fac-lpr-benchmark
    runner.cpp
    ../lpr-cli/cli_engine_factory.cpp
    ../lpr-cli/production_activation_gate.cpp
)

target_compile_features(fac-lpr-benchmark PRIVATE cxx_std_20)
target_include_directories(fac-lpr-benchmark PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/tools/lpr-cli
)
target_link_libraries(fac-lpr-benchmark PRIVATE
    fac_lpr::application
    fac_lpr::model
    fac_lpr::generic_onnx_ocr
    fac_lpr::lprnet_onnx
    fac_lpr::yolo_onnx
    fac_lpr::geometry
    fac_lpr::crop
    fac_lpr::opencv_adapter
    ${OpenCV_LIBS}
)

if(MSVC)
    target_compile_options(fac-lpr-benchmark PRIVATE
        /W4
        /permissive-
        /EHsc
        $<$<BOOL:${FAC_LPR_WARNINGS_AS_ERRORS}>:/WX>
    )
else()
    target_compile_options(fac-lpr-benchmark PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        $<$<BOOL:${FAC_LPR_WARNINGS_AS_ERRORS}>:-Werror>
    )
endif()

set_target_properties(fac-lpr-benchmark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FAC_LPR_OUTPUT_ROOT}/bin"
)

if(FAC_LPR_BUILD_TESTS AND
   EXISTS "${PROJECT_SOURCE_DIR}/models/best.onnx" AND
   EXISTS "${PROJECT_SOURCE_DIR}/models/lprnet_turkey.onnx" AND
   EXISTS "${PROJECT_SOURCE_DIR}/tests/fixtures/real/plate_01.jpg")
    add_test(
        NAME fac_lpr_benchmark_smoke
        COMMAND $<TARGET_FILE:fac-lpr-benchmark>
            ${PROJECT_SOURCE_DIR}/tests/fixtures/real/plate_01.jpg
            --model-dir ${PROJECT_SOURCE_DIR}/models
            --config ${PROJECT_SOURCE_DIR}/docs/lpr-cli-contract.example
            --warmup 1
            --iterations 3
            --report ${CMAKE_BINARY_DIR}/artifacts/reports/benchmark-smoke.json
    )
    set_tests_properties(fac_lpr_benchmark_smoke PROPERTIES
        LABELS "integration;real-model;benchmark"
        TIMEOUT 120
    )
endif()
