find_package(GTest CONFIG REQUIRED)

add_executable(fac_lpr_native_tests
    error_boundary_tests.cpp
    native_image_tests.cpp
    yolo_preprocessor_tests.cpp
)
target_compile_features(fac_lpr_native_tests PRIVATE cxx_std_20)
target_link_libraries(
    fac_lpr_native_tests
    PRIVATE
        fac_lpr::native_image
        fac_lpr::yolo
        GTest::gtest_main
)

if(FAC_LPR_WITH_OPENCV)
    find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc)
    target_sources(fac_lpr_native_tests PRIVATE yolo_preprocessor_opencv_reference_tests.cpp)
    target_include_directories(fac_lpr_native_tests PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(fac_lpr_native_tests PRIVATE ${OpenCV_LIBS})
endif()

include(GoogleTest)
gtest_discover_tests(fac_lpr_native_tests)
