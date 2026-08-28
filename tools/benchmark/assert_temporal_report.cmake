foreach(_required IN ITEMS BENCHMARK MANIFEST MODEL_DIR CONFIG REPORT)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

execute_process(
    COMMAND "${BENCHMARK}"
        "${MANIFEST}"
        --model-dir "${MODEL_DIR}"
        --config "${CONFIG}"
        --report "${REPORT}"
    RESULT_VARIABLE _benchmark_result
    OUTPUT_VARIABLE _benchmark_stdout
    ERROR_VARIABLE _benchmark_stderr
)

if(NOT _benchmark_result EQUAL 0)
    message(FATAL_ERROR
        "Temporal benchmark failed with exit code ${_benchmark_result}\n"
        "stdout:\n${_benchmark_stdout}\n"
        "stderr:\n${_benchmark_stderr}")
endif()

if(NOT EXISTS "${REPORT}")
    message(FATAL_ERROR "Temporal benchmark did not create report: ${REPORT}")
endif()

file(READ "${REPORT}" _report_json)
string(JSON _frames GET "${_report_json}" frames)
string(JSON _expected_plate_frames GET "${_report_json}" expectedPlateFrames)
string(JSON _stable_correct GET "${_report_json}" stableCorrect)
string(JSON _false_stable GET "${_report_json}" falseStable)
string(JSON _first_correct_stable_frame GET "${_report_json}" firstCorrectStableFrame)
string(JSON _max_history_observed GET "${_report_json}" maxHistoryObserved)

if(NOT _frames EQUAL 8)
    message(FATAL_ERROR "Expected 8 temporal frames, got ${_frames}")
endif()
if(NOT _expected_plate_frames EQUAL 8)
    message(FATAL_ERROR "Expected 8 labelled temporal frames, got ${_expected_plate_frames}")
endif()
if(_stable_correct LESS 2)
    message(FATAL_ERROR
        "Temporal sequence did not stabilize both real plate segments; stableCorrect=${_stable_correct}")
endif()
if(NOT _false_stable EQUAL 0)
    message(FATAL_ERROR "Temporal sequence emitted false stable result(s): ${_false_stable}")
endif()
if(_first_correct_stable_frame LESS 2)
    message(FATAL_ERROR
        "Temporal convergence metric is invalid: firstCorrectStableFrame=${_first_correct_stable_frame}")
endif()
if(_max_history_observed GREATER 8)
    message(FATAL_ERROR
        "Temporal history exceeded configured bound: maxHistoryObserved=${_max_history_observed}")
endif()

message(STATUS
    "Temporal benchmark gate passed: stableCorrect=${_stable_correct}, "
    "falseStable=${_false_stable}, firstCorrectStableFrame=${_first_correct_stable_frame}, "
    "maxHistoryObserved=${_max_history_observed}")
