cmake_minimum_required(VERSION 3.25)

foreach(required_var CLI_PATH MODEL_DIR CONTRACT_PATH FIXTURE_DIR EXPECTATIONS_FILE)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "Missing required E2E variable: ${required_var}")
    endif()
endforeach()

foreach(required_path CLI_PATH MODEL_DIR CONTRACT_PATH FIXTURE_DIR EXPECTATIONS_FILE)
    if(NOT EXISTS "${${required_path}}")
        message(FATAL_ERROR "E2E dependency does not exist: ${required_path}=${${required_path}}")
    endif()
endforeach()

file(STRINGS "${EXPECTATIONS_FILE}" expectation_lines)
set(executed_count 0)
set(optional_hit_count 0)
set(optional_miss_count 0)

foreach(raw_line IN LISTS expectation_lines)
    string(STRIP "${raw_line}" line)
    if(line STREQUAL "" OR line MATCHES "^#")
        continue()
    endif()

    string(FIND "${line}" "=" equals_index)
    if(equals_index LESS 1)
        message(FATAL_ERROR "Invalid fixture expectation line: ${line}")
    endif()

    string(SUBSTRING "${line}" 0 ${equals_index} filename)
    math(EXPR value_start "${equals_index} + 1")
    string(SUBSTRING "${line}" ${value_start} -1 expectation_spec)
    string(STRIP "${filename}" filename)
    string(STRIP "${expectation_spec}" expectation_spec)

    if(filename STREQUAL "" OR expectation_spec STREQUAL "")
        message(FATAL_ERROR "Empty fixture filename/expectation: ${line}")
    endif()

    set(required_spec "${expectation_spec}")
    set(optional_spec "")
    string(FIND "${expectation_spec}" "|" optional_separator)
    if(NOT optional_separator EQUAL -1)
        string(SUBSTRING "${expectation_spec}" 0 ${optional_separator} required_spec)
        math(EXPR optional_start "${optional_separator} + 1")
        string(SUBSTRING "${expectation_spec}" ${optional_start} -1 optional_spec)
    endif()

    set(image_path "${FIXTURE_DIR}/${filename}")
    if(NOT EXISTS "${image_path}")
        message(FATAL_ERROR "Fixture image is missing: ${image_path}")
    endif()

    execute_process(
        COMMAND "${CLI_PATH}"
            "${image_path}"
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

    math(EXPR executed_count "${executed_count} + 1")

    string(REPLACE "," ";" required_plates "${required_spec}")
    foreach(expected_plate IN LISTS required_plates)
        string(STRIP "${expected_plate}" expected_plate)
        if(expected_plate STREQUAL "")
            continue()
        endif()
        set(needle "\"plate\":\"${expected_plate}\"")
        string(FIND "${cli_stdout}" "${needle}" plate_index)
        if(plate_index EQUAL -1)
            message(FATAL_ERROR
                "Required plate ${expected_plate} was not recognized in ${filename}.\n"
                "CLI JSON:\n${cli_stdout}\n"
                "CLI stderr:\n${cli_stderr}")
        endif()
        message(STATUS "${filename}: required plate ${expected_plate} recognized")
    endforeach()

    if(NOT optional_spec STREQUAL "")
        string(REPLACE "," ";" optional_plates "${optional_spec}")
        foreach(optional_plate IN LISTS optional_plates)
            string(STRIP "${optional_plate}" optional_plate)
            if(optional_plate STREQUAL "")
                continue()
            endif()
            set(optional_needle "\"plate\":\"${optional_plate}\"")
            string(FIND "${cli_stdout}" "${optional_needle}" optional_index)
            if(optional_index EQUAL -1)
                math(EXPR optional_miss_count "${optional_miss_count} + 1")
                message(STATUS "${filename}: optional plate ${optional_plate} not recognized")
            else()
                math(EXPR optional_hit_count "${optional_hit_count} + 1")
                message(STATUS "${filename}: optional plate ${optional_plate} recognized")
            endif()
        endforeach()
    endif()
endforeach()

if(executed_count EQUAL 0)
    message(FATAL_ERROR "No real fixture expectations were executed")
endif()

message(STATUS
    "FAC LPR real CLI E2E passed: fixtures=${executed_count}, "
    "optional_hits=${optional_hit_count}, optional_misses=${optional_miss_count}")
