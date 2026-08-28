find_package(GTest CONFIG REQUIRED)
find_package(Threads REQUIRED)

# Keep test translation units self-contained. A test that names one of the
# public FAC LPR exception types must include error.hpp directly instead of
# relying on transitive includes from an implementation header.
file(GLOB _fac_lpr_cpp_test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
set(_fac_lpr_missing_error_includes "")
foreach(_test_source IN LISTS _fac_lpr_cpp_test_sources)
    file(READ "${_test_source}" _test_contents)
    string(REGEX MATCH
        "(ConfigurationError|ModelLoadError|InferenceError|ProviderError|TimeoutError|CancelledError|InvalidImageError|ResourceExhaustedError|InternalError)"
        _uses_fac_lpr_error
        "${_test_contents}")
    if(_uses_fac_lpr_error)
        string(FIND "${_test_contents}" "#include <fac_lpr/application/error.hpp>" _error_include_position)
        if(_error_include_position EQUAL -1)
            get_filename_component(_test_name "${_test_source}" NAME)
            list(APPEND _fac_lpr_missing_error_includes "${_test_name}")
        endif()
    endif()
endforeach()
if(_fac_lpr_missing_error_includes)
    list(JOIN _fac_lpr_missing_error_includes ", " _missing_error_include_list)
    message(FATAL_ERROR
        "Tests naming FAC LPR exception types must include <fac_lpr/application/error.hpp> directly. Missing: ${_missing_error_include_list}")
endif()

add_executable(fac_lpr_native_tests
    adaptive_tile_detector_tests.cpp
    atomic_model_slot_tests.cpp
    bounded_worker_pool_tests.cpp
    c_api_error_detail_tests.cpp
    c_api_header_smoke.c
    c_api_thread_safety_tests.cpp
    c_api_v1_tests.cpp
    calibration_json_adapter_tests.cpp
    candidate_fusion_tests.cpp
    confidence_calibration_tests.cpp
    constrained_ctc_beam_search_tests.cpp
    crop_hypothesis_generator_tests.cpp
    ctc_decoder_tests.cpp
    engine_builder_tests.cpp
    engine_diagnostics_tests.cpp
    error_boundary_tests.cpp
    geometry_validator_tests.cpp
    image_validation_tests.cpp
    json_config_adapter_tests.cpp
    logging_tests.cpp
    lpr_pipeline_tests.cpp
    lprnet_preprocessor_tests.cpp
    model_lifecycle_tests.cpp
    multi_detector_fusion_tests.cpp
    native_image_tests.cpp
    paddle_ocr_adapter_tests.cpp
    recognition_ensemble_tests.cpp
    recognition_stream_emission_tests.cpp
    recognition_stream_session_tests.cpp
    resource_budget_tests.cpp
    result_buffer_tests.cpp
    safe_decision_policy_tests.cpp
    stable_plate_event_filter_tests.cpp
    startup_self_test_tests.cpp
    temporal_plate_consensus_tests.cpp
    turkish_plate_grammar_tests.cpp
    yolo_pose_parser_tests.cpp
    yolo_preprocessor_tests.cpp
)

if(WIN32 AND FAC_LPR_BUILD_SHARED)
    target_sources(fac_lpr_native_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src/c_api/result_buffer.cpp"
    )
endif()

target_compile_features(fac_lpr_native_tests PRIVATE c_std_11 cxx_std_20)
target_link_libraries(fac_lpr_native_tests PRIVATE fac_lpr::engine fac_lpr::adaptive_detection fac_lpr::concurrency fac_lpr::crop fac_lpr::crypto fac_lpr::geometry fac_lpr::json_config fac_lpr::lprnet fac_lpr::model fac_lpr::native_image fac_lpr::paddle_adapter fac_lpr::yolo GTest::gtest_main Threads::Threads)

if(FAC_LPR_WITH_ONNX_RUNTIME)
    target_sources(fac_lpr_native_tests PRIVATE
        best_onnx_contract_tests.cpp
        concrete_onnx_provider_tests.cpp
        generic_onnx_ocr_recognizer_tests.cpp
        lprnet_onnx_contract_tests.cpp
        onnx_execution_provider_tests.cpp
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

if(WIN32 AND FAC_LPR_BUILD_SHARED)
    add_custom_command(TARGET fac_lpr_native_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:fac_lpr_engine>
            $<TARGET_FILE_DIR:fac_lpr_native_tests>
        VERBATIM
    )
    if(FAC_LPR_WITH_ONNX_RUNTIME AND FAC_LPR_ONNXRUNTIME_ROOT)
        set(_fac_lpr_onnx_runtime_dll "${FAC_LPR_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")
        if(EXISTS "${_fac_lpr_onnx_runtime_dll}")
            add_custom_command(TARGET fac_lpr_native_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_fac_lpr_onnx_runtime_dll}"
                    $<TARGET_FILE_DIR:fac_lpr_native_tests>
                VERBATIM
            )
        endif()
    endif()
endif()

include(GoogleTest)
gtest_discover_tests(fac_lpr_native_tests DISCOVERY_MODE PRE_TEST)
