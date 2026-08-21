find_package(GTest CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_executable(fac_lpr_native_tests
    adaptive_tile_detector_tests.cpp
    error_boundary_tests.cpp
    geometry_validator_tests.cpp
    image_validation_tests.cpp
    logging_tests.cpp
    native_image_tests.cpp
    yolo_pose_parser_tests.cpp
    yolo_preprocessor_tests.cpp
)
target_compile_features(fac_lpr_native_tests PRIVATE cxx_std_20)
target_link_libraries(fac_lpr_native_tests PRIVATE fac_lpr::adaptive_detection fac_lpr::geometry fac_lpr::native_image fac_lpr::yolo GTest::gtest_main Threads::Threads)

if(FAC_LPR_WITH_OPENCV)
    find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc)
    target_sources(fac_lpr_native_tests PRIVATE perspective_aligner_tests.cpp yolo_preprocessor_opencv_reference_tests.cpp)
    target_include_directories(fac_lpr_native_tests PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(fac_lpr_native_tests PRIVATE fac_lpr::opencv_adapter ${OpenCV_LIBS})
endif()

include(GoogleTest)
gtest_discover_tests(fac_lpr_native_tests)
