if(NOT FAC_LPR_WITH_ONNX_RUNTIME)
    message(FATAL_ERROR "FAC_LPR_BUILD_BENCHMARK requires FAC_LPR_WITH_ONNX_RUNTIME=ON")
endif()
if(NOT FAC_LPR_WITH_OPENCV)
    message(FATAL_ERROR "FAC_LPR_BUILD_BENCHMARK requires FAC_LPR_WITH_OPENCV=ON")
endif()

find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc imgcodecs)

add_executable(fac-lpr-benchmark
    runner.cpp
)

add_executable(fac-lpr-temporal-benchmark
    temporal_runner.cpp
)

foreach(_fac_lpr_benchmark_target IN ITEMS fac-lpr-benchmark fac-lpr-temporal-benchmark)
    target_compile_features(${_fac_lpr_benchmark_target} PRIVATE cxx_std_20)
    target_include_directories(${_fac_lpr_benchmark_target} PRIVATE
        ${OpenCV_INCLUDE_DIRS}
        ${PROJECT_SOURCE_DIR}/tools/lpr-cli
    )
    target_link_libraries(${_fac_lpr_benchmark_target} PRIVATE
        fac_lpr::production_factory
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
        target_compile_options(${_fac_lpr_benchmark_target} PRIVATE
            /W4
            /permissive-
            /EHsc
            $<$<BOOL:${FAC_LPR_WARNINGS_AS_ERRORS}>:/WX>
        )
    else()
        target_compile_options(${_fac_lpr_benchmark_target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            $<$<BOOL:${FAC_LPR_WARNINGS_AS_ERRORS}>:-Werror>
        )
    endif()

    set_target_properties(${_fac_lpr_benchmark_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FAC_LPR_OUTPUT_ROOT}/bin"
    )
endforeach()

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
        CONFIGURATIONS Release
        TIMEOUT 120
    )

    if(EXISTS "${PROJECT_SOURCE_DIR}/tests/fixtures/real/plate_03.jpg" AND
       EXISTS "${PROJECT_SOURCE_DIR}/tests/fixtures/real/temporal-sequence.tsv")
        add_test(
            NAME fac_lpr_temporal_benchmark_real_smoke
            COMMAND ${CMAKE_COMMAND}
                -DBENCHMARK=$<TARGET_FILE:fac-lpr-temporal-benchmark>
                -DMANIFEST=${PROJECT_SOURCE_DIR}/tests/fixtures/real/temporal-sequence.tsv
                -DMODEL_DIR=${PROJECT_SOURCE_DIR}/models
                -DCONFIG=${PROJECT_SOURCE_DIR}/docs/lpr-cli-contract.example
                -DREPORT=${CMAKE_BINARY_DIR}/artifacts/reports/temporal-benchmark-smoke.json
                -P ${CMAKE_CURRENT_SOURCE_DIR}/assert_temporal_report.cmake
        )
        set_tests_properties(fac_lpr_temporal_benchmark_real_smoke PROPERTIES
            LABELS "integration;real-model;benchmark;temporal"
            CONFIGURATIONS Release
            TIMEOUT 180
        )
    endif()
endif()
