cmake_minimum_required(VERSION 3.25)

foreach(required_var CLI_PATH MODEL_DIR CONTRACT_PATH FIXTURE_DIR MANIFEST_FILE REPORT_PATH)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "Missing golden regression variable: ${required_var}")
    endif()
endforeach()

foreach(required_path CLI_PATH MODEL_DIR CONTRACT_PATH FIXTURE_DIR MANIFEST_FILE)
    if(NOT EXISTS "${${required_path}}")
        message(FATAL_ERROR "Golden regression dependency missing: ${required_path}=${${required_path}}")
    endif()
endforeach()

if(NOT DEFINED MIN_EXACT_ACCURACY_BPS)
    set(MIN_EXACT_ACCURACY_BPS 10000)
endif()
if(NOT DEFINED MIN_CHARACTER_ACCURACY_BPS)
    set(MIN_CHARACTER_ACCURACY_BPS 9500)
endif()
if(NOT DEFINED MAX_DETECTION_FAILURE_BPS)
    set(MAX_DETECTION_FAILURE_BPS 0)
endif()
if(NOT DEFINED MAX_ALIGNMENT_FAILURE_BPS)
    set(MAX_ALIGNMENT_FAILURE_BPS 0)
endif()

foreach(threshold_var
    MIN_EXACT_ACCURACY_BPS
    MIN_CHARACTER_ACCURACY_BPS
    MAX_DETECTION_FAILURE_BPS
    MAX_ALIGNMENT_FAILURE_BPS)
    if(${threshold_var} LESS 0 OR ${threshold_var} GREATER 10000)
        message(FATAL_ERROR "${threshold_var} must be in [0,10000]")
    endif()
endforeach()

function(json_escape input output_var)
    set(value "${input}")
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    string(REPLACE "\n" "\\n" value "${value}")
    string(REPLACE "\r" "\\r" value "${value}")
    string(REPLACE "\t" "\\t" value "${value}")
    set(${output_var} "${value}" PARENT_SCOPE)
endfunction()

function(levenshtein_distance lhs rhs output_var)
    string(LENGTH "${lhs}" lhs_len)
    string(LENGTH "${rhs}" rhs_len)

    foreach(j RANGE 0 ${rhs_len})
        set(prev_${j} ${j})
    endforeach()

    if(lhs_len GREATER 0)
        foreach(i RANGE 1 ${lhs_len})
            set(curr_0 ${i})
            math(EXPR lhs_index "${i} - 1")
            string(SUBSTRING "${lhs}" ${lhs_index} 1 lhs_char)

            if(rhs_len GREATER 0)
                foreach(j RANGE 1 ${rhs_len})
                    math(EXPR rhs_index "${j} - 1")
                    string(SUBSTRING "${rhs}" ${rhs_index} 1 rhs_char)
                    if(lhs_char STREQUAL rhs_char)
                        set(cost 0)
                    else()
                        set(cost 1)
                    endif()

                    math(EXPR j_minus_one "${j} - 1")
                    math(EXPR deletion "${prev_${j}} + 1")
                    math(EXPR insertion "${curr_${j_minus_one}} + 1")
                    math(EXPR substitution "${prev_${j_minus_one}} + ${cost}")
                    set(best ${deletion})
                    if(insertion LESS best)
                        set(best ${insertion})
                    endif()
                    if(substitution LESS best)
                        set(best ${substitution})
                    endif()
                    set(curr_${j} ${best})
                endforeach()
            endif()

            foreach(j RANGE 0 ${rhs_len})
                set(prev_${j} ${curr_${j}})
            endforeach()
        endforeach()
    endif()

    set(${output_var} ${prev_${rhs_len}} PARENT_SCOPE)
endfunction()

file(STRINGS "${MANIFEST_FILE}" manifest_lines)
set(fixture_count 0)
set(required_plate_count 0)
set(exact_hit_count 0)
set(character_correct_units 0)
set(character_total_units 0)
set(detection_failure_count 0)
set(alignment_failure_count 0)
set(status_mismatch_count 0)
set(optional_hit_count 0)
set(optional_miss_count 0)
set(case_json "")

foreach(raw_line IN LISTS manifest_lines)
    string(STRIP "${raw_line}" line)
    if(line STREQUAL "" OR line MATCHES "^#")
        continue()
    endif()

    string(REPLACE "\t" ";" fields "${line}")
    list(LENGTH fields field_count)
    if(field_count LESS 3 OR field_count GREATER 4)
        message(FATAL_ERROR "Golden manifest line must have 3 or 4 TSV fields: ${line}")
    endif()

    list(GET fields 0 filename)
    list(GET fields 1 expected_status)
    list(GET fields 2 required_spec)
    if(field_count EQUAL 4)
        list(GET fields 3 optional_spec)
    else()
        set(optional_spec "")
    endif()

    string(STRIP "${filename}" filename)
    string(STRIP "${expected_status}" expected_status)
    string(STRIP "${required_spec}" required_spec)
    string(STRIP "${optional_spec}" optional_spec)

    if(filename STREQUAL "" OR expected_status STREQUAL "" OR required_spec STREQUAL "")
        message(FATAL_ERROR "Golden manifest filename/status/required plate cannot be empty: ${line}")
    endif()
    if(NOT expected_status MATCHES "^(ANY|ACCEPTED|REVIEW|REJECTED)$")
        message(FATAL_ERROR "Invalid expected status '${expected_status}' in ${filename}")
    endif()

    set(image_path "${FIXTURE_DIR}/${filename}")
    if(NOT EXISTS "${image_path}")
        message(FATAL_ERROR "Golden fixture image missing: ${image_path}")
    endif()

    execute_process(
        COMMAND "${CLI_PATH}" "${image_path}"
            --model-dir "${MODEL_DIR}"
            --config "${CONTRACT_PATH}"
            --log-level warn
            --json
        RESULT_VARIABLE cli_exit_code
        OUTPUT_VARIABLE cli_stdout
        ERROR_VARIABLE cli_stderr
        TIMEOUT 30
    )

    if(NOT cli_exit_code EQUAL 0)
        message(FATAL_ERROR
            "fac-lpr-cli failed for ${filename} (exit=${cli_exit_code})\n"
            "stdout:\n${cli_stdout}\n"
            "stderr:\n${cli_stderr}")
    endif()

    string(STRIP "${cli_stdout}" cli_json)
    string(JSON recognition_count ERROR_VARIABLE json_error LENGTH "${cli_json}" recognitions)
    if(json_error)
        message(FATAL_ERROR "Invalid CLI JSON for ${filename}: ${json_error}\n${cli_stdout}")
    endif()

    math(EXPR fixture_count "${fixture_count} + 1")
    if(recognition_count EQUAL 0)
        math(EXPR detection_failure_count "${detection_failure_count} + 1")
    endif()

    string(JSON failure_count LENGTH "${cli_json}" failures)
    set(alignment_failed false)
    if(failure_count GREATER 0)
        math(EXPR failure_last "${failure_count} - 1")
        foreach(failure_index RANGE 0 ${failure_last})
            string(JSON failure_provider GET "${cli_json}" failures ${failure_index} provider)
            if(failure_provider STREQUAL "alignment")
                set(alignment_failed true)
            endif()
        endforeach()
    endif()
    if(alignment_failed)
        math(EXPR alignment_failure_count "${alignment_failure_count} + 1")
    endif()

    set(actual_plates "")
    set(actual_statuses "")
    if(recognition_count GREATER 0)
        math(EXPR recognition_last "${recognition_count} - 1")
        foreach(recognition_index RANGE 0 ${recognition_last})
            string(JSON actual_plate GET "${cli_json}" recognitions ${recognition_index} plate)
            string(JSON actual_status GET "${cli_json}" recognitions ${recognition_index} status)
            list(APPEND actual_plates "${actual_plate}")
            list(APPEND actual_statuses "${actual_status}")
        endforeach()
    endif()

    string(REPLACE "," ";" required_plates "${required_spec}")
    set(case_required_hits 0)
    set(case_status_match true)
    foreach(expected_plate IN LISTS required_plates)
        string(STRIP "${expected_plate}" expected_plate)
        if(expected_plate STREQUAL "")
            continue()
        endif()
        math(EXPR required_plate_count "${required_plate_count} + 1")

        set(exact_index -1)
        list(FIND actual_plates "${expected_plate}" exact_index)
        if(NOT exact_index EQUAL -1)
            math(EXPR exact_hit_count "${exact_hit_count} + 1")
            math(EXPR case_required_hits "${case_required_hits} + 1")
            if(NOT expected_status STREQUAL "ANY")
                list(GET actual_statuses ${exact_index} matched_status)
                if(NOT matched_status STREQUAL expected_status)
                    set(case_status_match false)
                    math(EXPR status_mismatch_count "${status_mismatch_count} + 1")
                endif()
            endif()
        endif()

        string(LENGTH "${expected_plate}" expected_len)
        set(best_distance ${expected_len})
        set(best_actual "")
        foreach(actual_plate IN LISTS actual_plates)
            levenshtein_distance("${expected_plate}" "${actual_plate}" distance)
            if(distance LESS best_distance)
                set(best_distance ${distance})
                set(best_actual "${actual_plate}")
            endif()
        endforeach()
        if(actual_plates STREQUAL "")
            set(best_distance ${expected_len})
        endif()
        math(EXPR correct_units "${expected_len} - ${best_distance}")
        if(correct_units LESS 0)
            set(correct_units 0)
        endif()
        math(EXPR character_correct_units "${character_correct_units} + ${correct_units}")
        math(EXPR character_total_units "${character_total_units} + ${expected_len}")
    endforeach()

    if(NOT optional_spec STREQUAL "")
        string(REPLACE "," ";" optional_plates "${optional_spec}")
        foreach(optional_plate IN LISTS optional_plates)
            string(STRIP "${optional_plate}" optional_plate)
            if(optional_plate STREQUAL "")
                continue()
            endif()
            list(FIND actual_plates "${optional_plate}" optional_index)
            if(optional_index EQUAL -1)
                math(EXPR optional_miss_count "${optional_miss_count} + 1")
            else()
                math(EXPR optional_hit_count "${optional_hit_count} + 1")
            endif()
        endforeach()
    endif()

    string(JOIN "," actual_plates_csv ${actual_plates})
    json_escape("${filename}" filename_json)
    json_escape("${expected_status}" expected_status_json)
    json_escape("${required_spec}" required_json)
    json_escape("${optional_spec}" optional_json)
    json_escape("${actual_plates_csv}" actual_json)
    if(case_json STREQUAL "")
        set(case_separator "")
    else()
        set(case_separator ",")
    endif()
    set(case_json "${case_json}${case_separator}{\"file\":\"${filename_json}\",\"expectedStatus\":\"${expected_status_json}\",\"requiredPlates\":\"${required_json}\",\"optionalPlates\":\"${optional_json}\",\"actualPlates\":\"${actual_json}\",\"requiredHits\":${case_required_hits},\"alignmentFailed\":${alignment_failed},\"statusMatch\":${case_status_match}}")
endforeach()

if(fixture_count EQUAL 0 OR required_plate_count EQUAL 0 OR character_total_units EQUAL 0)
    message(FATAL_ERROR "Golden regression manifest produced no executable required samples")
endif()

math(EXPR exact_accuracy_bps "${exact_hit_count} * 10000 / ${required_plate_count}")
math(EXPR character_accuracy_bps "${character_correct_units} * 10000 / ${character_total_units}")
math(EXPR detection_failure_bps "${detection_failure_count} * 10000 / ${fixture_count}")
math(EXPR alignment_failure_bps "${alignment_failure_count} * 10000 / ${fixture_count}")

get_filename_component(report_dir "${REPORT_PATH}" DIRECTORY)
file(MAKE_DIRECTORY "${report_dir}")
file(WRITE "${REPORT_PATH}"
    "{\n"
    "  \"schemaVersion\": 1,\n"
    "  \"fixtureCount\": ${fixture_count},\n"
    "  \"requiredPlateCount\": ${required_plate_count},\n"
    "  \"exactHitCount\": ${exact_hit_count},\n"
    "  \"exactAccuracyBps\": ${exact_accuracy_bps},\n"
    "  \"characterAccuracyBps\": ${character_accuracy_bps},\n"
    "  \"detectionFailureCount\": ${detection_failure_count},\n"
    "  \"detectionFailureBps\": ${detection_failure_bps},\n"
    "  \"alignmentFailureCount\": ${alignment_failure_count},\n"
    "  \"alignmentFailureBps\": ${alignment_failure_bps},\n"
    "  \"statusMismatchCount\": ${status_mismatch_count},\n"
    "  \"optionalHitCount\": ${optional_hit_count},\n"
    "  \"optionalMissCount\": ${optional_miss_count},\n"
    "  \"thresholds\": {\"minExactAccuracyBps\": ${MIN_EXACT_ACCURACY_BPS}, \"minCharacterAccuracyBps\": ${MIN_CHARACTER_ACCURACY_BPS}, \"maxDetectionFailureBps\": ${MAX_DETECTION_FAILURE_BPS}, \"maxAlignmentFailureBps\": ${MAX_ALIGNMENT_FAILURE_BPS}},\n"
    "  \"cases\": [${case_json}]\n"
    "}\n")

set(gate_failed false)
if(exact_accuracy_bps LESS MIN_EXACT_ACCURACY_BPS)
    set(gate_failed true)
endif()
if(character_accuracy_bps LESS MIN_CHARACTER_ACCURACY_BPS)
    set(gate_failed true)
endif()
if(detection_failure_bps GREATER MAX_DETECTION_FAILURE_BPS)
    set(gate_failed true)
endif()
if(alignment_failure_bps GREATER MAX_ALIGNMENT_FAILURE_BPS)
    set(gate_failed true)
endif()
if(status_mismatch_count GREATER 0)
    set(gate_failed true)
endif()

message(STATUS
    "Golden regression: exact=${exact_accuracy_bps}/10000, "
    "character=${character_accuracy_bps}/10000, "
    "detectionFailure=${detection_failure_bps}/10000, "
    "alignmentFailure=${alignment_failure_bps}/10000, report=${REPORT_PATH}")

if(gate_failed)
    message(FATAL_ERROR "Golden regression tolerance exceeded; see ${REPORT_PATH}")
endif()
