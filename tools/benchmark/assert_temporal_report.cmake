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
string(JSON _per_frame_correct GET "${_report_json}" perFrameCorrect)
string(JSON _stable_emitted GET "${_report_json}" stableEmitted)
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
if(NOT _per_frame_correct EQUAL _expected_plate_frames)
    message(FATAL_ERROR
        "Real-model temporal baseline regressed: perFrameCorrect=${_per_frame_correct}, expected=${_expected_plate_frames}")
endif()
if(NOT _false_stable EQUAL 0)
    message(FATAL_ERROR "Temporal sequence emitted false stable result(s): ${_false_stable}")
endif()
if(_stable_emitted GREATER 0 AND _stable_correct LESS _stable_emitted)
    message(FATAL_ERROR
        "Temporal sequence emitted an incorrect stable result: stableEmitted=${_stable_emitted}, stableCorrect=${_stable_correct}")
endif()
if(_stable_correct GREATER 0 AND _first_correct_stable_frame LESS 2)
    message(FATAL_ERROR
        "Temporal convergence metric is invalid: firstCorrectStableFrame=${_first_correct_stable_frame}")
endif()
if(_max_history_observed GREATER 8)
    message(FATAL_ERROR
        "Temporal history exceeded configured bound: maxHistoryObserved=${_max_history_observed}")
endif()

# The production decision policy is intentionally fail-closed. A real frame may
# contain the correct plate text while remaining REVIEW; temporal consensus must
# not promote such evidence to ACCEPTED merely because it repeats. Deterministic
# stabilization and vehicle-transition convergence are release-gated separately
# by TemporalRegressionBenchmark unit sequences using accepted frame evidence.
message(STATUS
    "Temporal real-model gate passed: perFrameCorrect=${_per_frame_correct}/${_expected_plate_frames}, "
    "stableEmitted=${_stable_emitted}, stableCorrect=${_stable_correct}, falseStable=${_false_stable}, "
    "firstCorrectStableFrame=${_first_correct_stable_frame}, maxHistoryObserved=${_max_history_observed}")
