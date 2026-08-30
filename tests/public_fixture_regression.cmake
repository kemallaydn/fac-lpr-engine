cmake_minimum_required(VERSION 3.25)

foreach(required_var CLI_PATH MODEL_DIR CONTRACT_PATH PUBLIC_MANIFEST DOWNLOAD_DIR REPORT_PATH)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "Missing public fixture regression variable: ${required_var}")
    endif()
endforeach()

foreach(required_path CLI_PATH MODEL_DIR CONTRACT_PATH PUBLIC_MANIFEST)
    if(NOT EXISTS "${${required_path}}")
        message(FATAL_ERROR "Public fixture regression dependency missing: ${required_path}=${${required_path}}")
    endif()
endforeach()

file(MAKE_DIRECTORY "${DOWNLOAD_DIR}")
file(STRINGS "${PUBLIC_MANIFEST}" manifest_lines)
set(golden_manifest "${DOWNLOAD_DIR}/golden-manifest.tsv")
file(WRITE "${golden_manifest}" "# filename\texpected_status\trequired_plates\toptional_plates\n")
set(fixture_count 0)

foreach(raw_line IN LISTS manifest_lines)
    string(STRIP "${raw_line}" line)
    if(line STREQUAL "" OR line MATCHES "^#")
        continue()
    endif()

    string(REPLACE "\t" ";" fields "${line}")
    list(LENGTH fields field_count)
    if(NOT field_count EQUAL 9)
        message(FATAL_ERROR "Public fixture manifest line must have 9 TSV fields: ${line}")
    endif()

    list(GET fields 0 filename)
    list(GET fields 1 expected_status)
    list(GET fields 2 required_plates)
    list(GET fields 3 optional_plates)
    list(GET fields 4 download_url)
    list(GET fields 5 expected_sha1)
    list(GET fields 6 source_page)
    list(GET fields 7 license_name)
    list(GET fields 8 author)

    foreach(field_var filename expected_status required_plates download_url expected_sha1 source_page license_name author)
        string(STRIP "${${field_var}}" ${field_var})
        if("${${field_var}}" STREQUAL "")
            message(FATAL_ERROR "Public fixture field '${field_var}' cannot be empty: ${line}")
        endif()
    endforeach()
    string(STRIP "${optional_plates}" optional_plates)

    set(image_path "${DOWNLOAD_DIR}/${filename}")
    file(DOWNLOAD
        "${download_url}"
        "${image_path}"
        STATUS download_status
        TLS_VERIFY ON
        TIMEOUT 30
        HTTPHEADER "User-Agent: FAC-LPR-Engine-Public-Fixture-Regression/1.0"
    )
    list(GET download_status 0 download_code)
    list(GET download_status 1 download_message)
    if(NOT download_code EQUAL 0)
        message(FATAL_ERROR "Failed to download ${filename}: ${download_message}")
    endif()

    file(SIZE "${image_path}" fixture_size)
    if(fixture_size EQUAL 0)
        message(FATAL_ERROR "Downloaded empty public fixture: ${filename}")
    endif()
    file(SHA1 "${image_path}" actual_sha1)
    string(TOLOWER "${actual_sha1}" actual_sha1)
    string(TOLOWER "${expected_sha1}" expected_sha1)
    if(expected_sha1 STREQUAL "pending")
        message(FATAL_ERROR "Fixture ${filename} needs checksum pinning; downloaded SHA1=${actual_sha1}")
    endif()
    if(NOT actual_sha1 STREQUAL expected_sha1)
        message(FATAL_ERROR
            "Checksum mismatch for ${filename}: expected=${expected_sha1}, actual=${actual_sha1}, source=${source_page}")
    endif()

    file(APPEND "${golden_manifest}"
        "${filename}\t${expected_status}\t${required_plates}\t${optional_plates}\n")
    math(EXPR fixture_count "${fixture_count} + 1")
    message(STATUS
        "Public fixture verified: ${filename}, sha1=${actual_sha1}, license=${license_name}, author=${author}, source=${source_page}")
endforeach()

if(fixture_count EQUAL 0)
    message(FATAL_ERROR "Public fixture manifest contains no executable fixtures")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -DCLI_PATH=${CLI_PATH}
        -DMODEL_DIR=${MODEL_DIR}
        -DCONTRACT_PATH=${CONTRACT_PATH}
        -DFIXTURE_DIR=${DOWNLOAD_DIR}
        -DMANIFEST_FILE=${golden_manifest}
        -DREPORT_PATH=${REPORT_PATH}
        -DMIN_EXACT_ACCURACY_BPS=10000
        -DMIN_CHARACTER_ACCURACY_BPS=10000
        -DMAX_DETECTION_FAILURE_BPS=0
        -DMAX_ALIGNMENT_FAILURE_BPS=0
        -P ${CMAKE_CURRENT_LIST_DIR}/golden_regression.cmake
    RESULT_VARIABLE regression_exit_code
    OUTPUT_VARIABLE regression_stdout
    ERROR_VARIABLE regression_stderr
    TIMEOUT 180
)

message("${regression_stdout}")
if(NOT regression_exit_code EQUAL 0)
    message(FATAL_ERROR "Public fixture golden regression failed:\n${regression_stderr}")
endif()
