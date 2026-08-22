find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgcodecs)

add_executable(fac-lpr-cli
    main.cpp
)

target_compile_features(fac-lpr-cli PRIVATE cxx_std_20)
target_include_directories(fac-lpr-cli PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(fac-lpr-cli PRIVATE fac_lpr::engine ${OpenCV_LIBS})

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
endif()

set_target_properties(fac-lpr-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FAC_LPR_OUTPUT_ROOT}/bin"
)
