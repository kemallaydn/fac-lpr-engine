if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "FAC_LPR_BUILD_FUZZERS requires Clang/AppleClang with libFuzzer support")
endif()

if(NOT FAC_LPR_SANITIZER STREQUAL "address-undefined")
    message(FATAL_ERROR
        "FAC_LPR_BUILD_FUZZERS requires -DFAC_LPR_SANITIZER=address-undefined so linked engine code is instrumented")
endif()

function(fac_lpr_add_fuzzer target source)
    add_executable(${target} ${source})
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_options(${target} PRIVATE -fsanitize=fuzzer)
    target_link_options(${target} PRIVATE -fsanitize=fuzzer)
    target_link_libraries(${target} PRIVATE ${ARGN})
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FAC_LPR_OUTPUT_ROOT}/fuzz"
    )
endfunction()

fac_lpr_add_fuzzer(fac_lpr_fuzz_image image_fuzz.cpp fac_lpr::application)
fac_lpr_add_fuzzer(fac_lpr_fuzz_config config_fuzz.cpp fac_lpr::engine)
fac_lpr_add_fuzzer(fac_lpr_fuzz_abi abi_fuzz.cpp fac_lpr::engine)
fac_lpr_add_fuzzer(fac_lpr_fuzz_grammar grammar_fuzz.cpp fac_lpr::application)
fac_lpr_add_fuzzer(fac_lpr_fuzz_ctc_decoder ctc_decoder_fuzz.cpp fac_lpr::lprnet)
