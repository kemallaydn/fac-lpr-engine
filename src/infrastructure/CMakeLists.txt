option(FAC_LPR_WITH_SPDLOG "Build the optional spdlog logger adapter" OFF)
option(FAC_LPR_WITH_ONNX_RUNTIME "Build ONNX Runtime infrastructure" OFF)
option(FAC_LPR_WITH_OPENCV "Build OpenCV infrastructure adapters" OFF)

add_library(fac_lpr_native_image STATIC
    native_image/native_image.cpp
)
add_library(fac_lpr::native_image ALIAS fac_lpr_native_image)
target_compile_features(fac_lpr_native_image PUBLIC cxx_std_20)
target_include_directories(
    fac_lpr_native_image
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(fac_lpr_native_image PUBLIC fac_lpr::application)

add_library(fac_lpr_yolo STATIC
    yolo/yolo_pose_preprocessor.cpp
    yolo/yolo_pose_parser.cpp
)
add_library(fac_lpr::yolo ALIAS fac_lpr_yolo)
target_compile_features(fac_lpr_yolo PUBLIC cxx_std_20)
target_include_directories(
    fac_lpr_yolo
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(
    fac_lpr_yolo
    PUBLIC
        fac_lpr::application
        fac_lpr::domain
        fac_lpr::native_image
)

add_library(fac_lpr_geometry STATIC
    geometry/plate_geometry_validator.cpp
)
add_library(fac_lpr::geometry ALIAS fac_lpr_geometry)
target_compile_features(fac_lpr_geometry PUBLIC cxx_std_20)
target_include_directories(
    fac_lpr_geometry
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(
    fac_lpr_geometry
    PUBLIC
        fac_lpr::application
        fac_lpr::domain
)

add_library(fac_lpr_infrastructure INTERFACE)
add_library(fac_lpr::infrastructure ALIAS fac_lpr_infrastructure)
target_link_libraries(
    fac_lpr_infrastructure
    INTERFACE
        fac_lpr::application
        fac_lpr::domain
        fac_lpr::native_image
        fac_lpr::yolo
        fac_lpr::geometry
)

if(FAC_LPR_WITH_SPDLOG)
    find_package(spdlog CONFIG REQUIRED)
    add_library(fac_lpr_spdlog_adapter STATIC spdlog_logger.cpp)
    add_library(fac_lpr::spdlog_adapter ALIAS fac_lpr_spdlog_adapter)
    target_compile_features(fac_lpr_spdlog_adapter PUBLIC cxx_std_20)
    target_include_directories(fac_lpr_spdlog_adapter PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
    target_link_libraries(fac_lpr_spdlog_adapter PUBLIC fac_lpr::application PRIVATE spdlog::spdlog)
endif()

if(FAC_LPR_WITH_ONNX_RUNTIME)
    if(NOT TARGET onnxruntime::onnxruntime)
        find_package(onnxruntime CONFIG REQUIRED)
    endif()
    add_library(fac_lpr_onnx_runtime STATIC onnx/onnx_session.cpp)
    add_library(fac_lpr::onnx_runtime ALIAS fac_lpr_onnx_runtime)
    target_compile_features(fac_lpr_onnx_runtime PUBLIC cxx_std_20)
    target_include_directories(fac_lpr_onnx_runtime PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
    target_link_libraries(fac_lpr_onnx_runtime PUBLIC fac_lpr::application PRIVATE onnxruntime::onnxruntime)
endif()

if(FAC_LPR_WITH_OPENCV)
    find_package(OpenCV CONFIG REQUIRED COMPONENTS core)
    add_library(fac_lpr_opencv_adapter STATIC opencv_image_view.cpp)
    add_library(fac_lpr::opencv_adapter ALIAS fac_lpr_opencv_adapter)
    target_compile_features(fac_lpr_opencv_adapter PUBLIC cxx_std_20)
    target_include_directories(fac_lpr_opencv_adapter PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include> ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(fac_lpr_opencv_adapter PUBLIC fac_lpr::application PRIVATE ${OpenCV_LIBS})
endif()
