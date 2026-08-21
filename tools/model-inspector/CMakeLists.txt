find_package(onnxruntime CONFIG REQUIRED)

add_executable(fac-lpr-model-info
    main.cpp
)

target_compile_features(fac-lpr-model-info PRIVATE cxx_std_20)
target_link_libraries(fac-lpr-model-info PRIVATE onnxruntime::onnxruntime)

if(MSVC)
    target_compile_options(fac-lpr-model-info PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(fac-lpr-model-info PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

set_target_properties(fac-lpr-model-info PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FAC_LPR_OUTPUT_ROOT}/bin"
)
