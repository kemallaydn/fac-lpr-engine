if(NOT FAC_LPR_WITH_ONNX_RUNTIME)
    message(FATAL_ERROR "FAC_LPR_BUILD_LPR_CLI requires FAC_LPR_WITH_ONNX_RUNTIME=ON")
endif()
if(NOT FAC_LPR_WITH_OPENCV)
    message(FATAL_ERROR "FAC_LPR_BUILD_LPR_CLI requires FAC_LPR_WITH_OPENCV=ON")
endif()

find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc imgcodecs)

add_executable(fac-lpr-cli
    main.cpp
    cli_engine_factory.cpp
    production_activation_gate.cpp
)

target_compile_features(fac-lpr-cli PRIVATE cxx_std_20)
target_include_directories(fac-lpr-cli PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(fac-lpr-cli PRIVATE
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
    target_compile_options(fac-lpr-cli PRIVATE
        /W4
        /permissive-
        /EHsc
        $<$<BOOL:${FAC_LPR_WARNINGS_AS_ERRORS}>:/WX>
    )
else()
    target_compile_options(fac-lpr-cli PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        $<$<BOOL:${FAC_LPR_WARNINGS_AS_ERRORS}>:-Werror>
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # GCC can conservatively assume required(contract, temporary-key) may
        # return a reference tied to the temporary lookup key. The helper
        # actually returns the std::string stored in the long-lived contract map.
        target_compile_options(fac-lpr-cli PRIVATE -Wno-error=dangling-reference)
    endif()
endif()

set_target_properties(fac-lpr-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FAC_LPR_OUTPUT_ROOT}/bin"
)

install(TARGETS fac-lpr-cli RUNTIME DESTINATION bin)

if(FAC_LPR_BUILD_TESTS)
    set(FAC_LPR_REAL_FIXTURE_DIR "${PROJECT_SOURCE_DIR}/tests/fixtures/real")
    set(FAC_LPR_REAL_EXPECTATIONS "${FAC_LPR_REAL_FIXTURE_DIR}/expected-results.txt")
    set(FAC_LPR_GOLDEN_MANIFEST "${FAC_LPR_REAL_FIXTURE_DIR}/golden-manifest.tsv")
    set(FAC_LPR_CLI_CONTRACT "${PROJECT_SOURCE_DIR}/docs/lpr-cli-contract.example")
    set(FAC_LPR_DETECTOR_MODEL "${PROJECT_SOURCE_DIR}/models/best.onnx")
    set(FAC_LPR_OCR_MODEL "${PROJECT_SOURCE_DIR}/models/lprnet_turkey.onnx")

    if(EXISTS "${FAC_LPR_DETECTOR_MODEL}" AND
       EXISTS "${FAC_LPR_OCR_MODEL}" AND
       EXISTS "${FAC_LPR_REAL_EXPECTATIONS}" AND
       EXISTS "${FAC_LPR_GOLDEN_MANIFEST}" AND
       EXISTS "${FAC_LPR_CLI_CONTRACT}")
        add_test(
            NAME fac_lpr_cli_real_e2e
            COMMAND ${CMAKE_COMMAND}
                -DCLI_PATH=$<TARGET_FILE:fac-lpr-cli>
                -DMODEL_DIR=${PROJECT_SOURCE_DIR}/models
                -DCONTRACT_PATH=${FAC_LPR_CLI_CONTRACT}
                -DFIXTURE_DIR=${FAC_LPR_REAL_FIXTURE_DIR}
                -DEXPECTATIONS_FILE=${FAC_LPR_REAL_EXPECTATIONS}
                -P ${PROJECT_SOURCE_DIR}/tests/real_cli_e2e.cmake
        )
        set_tests_properties(fac_lpr_cli_real_e2e PROPERTIES
            LABELS "integration;real-model;cli"
            TIMEOUT 120
        )

        add_test(
            NAME fac_lpr_golden_regression
            COMMAND ${CMAKE_COMMAND}
                -DCLI_PATH=$<TARGET_FILE:fac-lpr-cli>
                -DMODEL_DIR=${PROJECT_SOURCE_DIR}/models
                -DCONTRACT_PATH=${FAC_LPR_CLI_CONTRACT}
                -DFIXTURE_DIR=${FAC_LPR_REAL_FIXTURE_DIR}
                -DMANIFEST_FILE=${FAC_LPR_GOLDEN_MANIFEST}
                -DREPORT_PATH=${CMAKE_BINARY_DIR}/artifacts/reports/golden-regression.json
                -DMIN_EXACT_ACCURACY_BPS=10000
                -DMIN_CHARACTER_ACCURACY_BPS=9500
                -DMAX_DETECTION_FAILURE_BPS=0
                -DMAX_ALIGNMENT_FAILURE_BPS=0
                -P ${PROJECT_SOURCE_DIR}/tests/golden_regression.cmake
        )
        set_tests_properties(fac_lpr_golden_regression PROPERTIES
            LABELS "integration;real-model;golden;regression"
            TIMEOUT 180
        )
        message(STATUS "FAC LPR real CLI E2E and golden regression tests enabled")
    else()
        message(STATUS
            "FAC LPR real-model tests not registered: required model/fixture/contract artifacts are missing")
    endif()
endif()
