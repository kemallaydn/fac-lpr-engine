find_package(GTest CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_executable(fac_lpr_native_tests
    adaptive_tile_detector_tests.cpp
    bounded_worker_pool_tests.cpp
    c_api_error_detail_tests.cpp
    c_api_header_smoke.c
    c_api_v1_tests.cpp
    candidate_fusion_tests.cpp
    confidence_calibration_tests.cpp
    constrained_ctc_beam_search_tests.cpp
    crop_hypothesis_generator_tests.cpp
    ctc_decoder_tests.cpp
    engine_diagnostics_tests.cpp
    error_boundary_tests.cpp
    geometry_validator_tests.cpp
    image_validation_tests.cpp
    logging_tests.cpp
    lpr_pipeline_tests.cpp
    lprnet_preprocessor_tests.cpp
    model_lifecycle_tests.cpp
    multi_detector_fusion_tests.cpp
    native_image_tests.cpp
    paddle_ocr_adapter_tests.cpp
    recognition_ensemble_tests.cpp
    result_buffer_tests.cpp
    safe_decision_policy_tests.cpp
    startup_self_test_tests.cpp
    turkish_plate_grammar_tests.cpp
    yolo_pose_parser_tests.cpp
    yolo_preprocessor_tests.cpp
)
target_compile_features(fac_lpr_native_tests PRIVATE c_std_11 cxx_std_20)
target_link_libraries(fac_lpr_native_tests PRIVATE fac_lpr::engine fac_lpr::adaptive_detection fac_lpr::concurrency fac_lpr::crop fac_lpr::crypto fac_lpr::geometry fac_lpr::lprnet fac_lpr::model fac_lpr::native_image fac_lpr::paddle_adapter fac_lpr::yolo GTest::gtest_main Threads::Threads)

if(FAC_LPR_WITH_ONNX_RUNTIME)
    target_sources(fac_lpr_native_tests PRIVATE
        best_onnx_contract_tests.cpp
        concrete_onnx_provider_tests.cpp
        generic_onnx_ocr_recognizer_tests.cpp
        lprnet_onnx_contract_tests.cpp
        provider_stage_timing_tests.cpp
    )
    target_link_libraries(fac_lpr_native_tests PRIVATE
        fac_lpr::generic_onnx_ocr
        fac_lpr::lprnet_onnx
        fac_lpr::onnx_runtime
        fac_lpr::yolo_onnx
    )

    set(FAC_LPR_TEST_BEST_MODEL_PATH "" CACHE FILEPATH "Optional best.onnx artifact path for contract regression tests")
    if(FAC_LPR_TEST_BEST_MODEL_PATH)
        get_filename_component(_best_model_path "${FAC_LPR_TEST_BEST_MODEL_PATH}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
    elseif(EXISTS "${PROJECT_SOURCE_DIR}/models/best.onnx")
        set(_best_model_path "${PROJECT_SOURCE_DIR}/models/best.onnx")
    endif()
    if(DEFINED _best_model_path)
        target_compile_definitions(fac_lpr_native_tests PRIVATE FAC_LPR_TEST_BEST_MODEL_PATH="${_best_model_path}")
    endif()

    set(FAC_LPR_TEST_LPRNET_MODEL_PATH "" CACHE FILEPATH "Optional lprnet_turkey.onnx artifact path for contract regression tests")
    if(FAC_LPR_TEST_LPRNET_MODEL_PATH)
        get_filename_component(_lprnet_model_path "${FAC_LPR_TEST_LPRNET_MODEL_PATH}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
    elseif(EXISTS "${PROJECT_SOURCE_DIR}/models/lprnet_turkey.onnx")
        set(_lprnet_model_path "${PROJECT_SOURCE_DIR}/models/lprnet_turkey.onnx")
    endif()
    if(DEFINED _lprnet_model_path)
        target_compile_definitions(fac_lpr_native_tests PRIVATE FAC_LPR_TEST_LPRNET_MODEL_PATH="${_lprnet_model_path}")
    endif()
endif()

if(FAC_LPR_WITH_OPENCV)
    find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc)
    target_sources(fac_lpr_native_tests PRIVATE connected_component_layout_analyzer_tests.cpp crop_enhancers_tests.cpp double_row_normalizer_tests.cpp perspective_aligner_tests.cpp yolo_preprocessor_opencv_reference_tests.cpp)
    target_include_directories(fac_lpr_native_tests PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(fac_lpr_native_tests PRIVATE fac_lpr::opencv_adapter ${OpenCV_LIBS})
endif()

include(GoogleTest)
gtest_discover_tests(fac_lpr_native_tests)
