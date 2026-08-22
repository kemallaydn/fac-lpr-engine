if(NOT FAC_LPR_WITH_ONNX_RUNTIME)
    message(FATAL_ERROR "FAC_LPR_BUILD_LPR_CLI requires FAC_LPR_WITH_ONNX_RUNTIME=ON")
endif()
if(NOT FAC_LPR_WITH_OPENCV)
    message(FATAL_ERROR "FAC_LPR_BUILD_LPR_CLI requires FAC_LPR_WITH_OPENCV=ON")
endif()

find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc imgcodecs)

add_executable(fac-lpr-cli
    main.cpp
    cli_engine_factory.cpp
)

target_compile_features(fac-lpr-cli PRIVATE cxx_std_20)
target_include_directories(fac-lpr-cli PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(fac-lpr-cli PRIVATE
    fac_lpr::application
    fac_lpr::generic_onnx_ocr
    fac_lpr::lprnet_onnx
    fac_lpr::yolo_onnx
    fac_lpr::geometry
    fac_lpr::crop
    fac_lpr::opencv_adapter
    ${OpenCV_LIBS}
)

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
