option(FAC_LPR_WITH_SPDLOG "Build the optional spdlog logger adapter" OFF)
option(FAC_LPR_WITH_ONNX_RUNTIME "Build ONNX Runtime infrastructure" OFF)
option(FAC_LPR_WITH_OPENCV "Build OpenCV infrastructure adapters" OFF)

add_library(fac_lpr_native_image STATIC native_image/native_image.cpp)
add_library(fac_lpr::native_image ALIAS fac_lpr_native_image)
target_compile_features(fac_lpr_native_image PUBLIC cxx_std_20)
target_include_directories(fac_lpr_native_image PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(fac_lpr_native_image PUBLIC fac_lpr::application)

add_library(fac_lpr_crypto STATIC crypto/sha256.cpp)
add_library(fac_lpr::crypto ALIAS fac_lpr_crypto)
target_compile_features(fac_lpr_crypto PUBLIC cxx_std_20)
target_include_directories(fac_lpr_crypto PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)

add_library(fac_lpr_model STATIC model/model_lifecycle.cpp)
add_library(fac_lpr::model ALIAS fac_lpr_model)
target_compile_features(fac_lpr_model PUBLIC cxx_std_20)
target_include_directories(fac_lpr_model PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(fac_lpr_model PUBLIC fac_lpr::application fac_lpr::crypto)

add_library(fac_lpr_paddle_adapter STATIC paddle/paddle_ocr_adapter.cpp)
add_library(fac_lpr::paddle_adapter ALIAS fac_lpr_paddle_adapter)
target_compile_features(fac_lpr_paddle_adapter PUBLIC cxx_std_20)
target_include_directories(fac_lpr_paddle_adapter PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(fac_lpr_paddle_adapter PUBLIC fac_lpr::application fac_lpr::crypto)

add_library(fac_lpr_yolo STATIC yolo/yolo_pose_preprocessor.cpp yolo/yolo_pose_parser.cpp)
add_library(fac_lpr::yolo ALIAS fac_lpr_yolo)
target_compile_features(fac_lpr_yolo PUBLIC cxx_std_20)
target_include_directories(fac_lpr_yolo PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(fac_lpr_yolo PUBLIC fac_lpr::application fac_lpr::domain fac_lpr::native_image)

add_library(fac_lpr_lprnet STATIC lprnet/lprnet_preprocessor.cpp lprnet/ctc_decoder.cpp lprnet/constrained_ctc_beam_search.cpp)
add_library(fac_lpr::lprnet ALIAS fac_lpr_lprnet)
target_compile_features(fac_lpr_lprnet PUBLIC cxx_std_20)
target_include_directories(fac_lpr_lprnet PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(fac_lpr_lprnet PUBLIC fac_lpr::application fac_lpr::domain fac_lpr::native_image)

add_library(fac_lpr_geometry STATIC geometry/plate_geometry_validator.cpp geometry/plate_geometry_evaluator.cpp)
add_library(fac_lpr::geometry ALIAS fac_lpr_geometry)
target_compile_features(fac_lpr_geometry PUBLIC cxx_std_20)
target_include_directories(fac_lpr_geometry PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(fac_lpr_geometry PUBLIC fac_lpr::application fac_lpr::domain)

add_library(fac_lpr_adaptive_detection STATIC adaptive/adaptive_tile_detector.cpp)
add_library(fac_lpr::adaptive_detection ALIAS fac_lpr_adaptive_detection)
target_compile_features(fac_lpr_adaptive_detection PUBLIC cxx_std_20)
target_include_directories(fac_lpr_adaptive_detection PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(fac_lpr_adaptive_detection PUBLIC fac_lpr::application fac_lpr::domain fac_lpr::native_image)

add_library(fac_lpr_crop STATIC crop/crop_hypothesis_generator.cpp)
add_library(fac_lpr::crop ALIAS fac_lpr_crop)
target_compile_features(fac_lpr_crop PUBLIC cxx_std_20)
target_include_directories(fac_lpr_crop PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(fac_lpr_crop PUBLIC fac_lpr::application fac_lpr::domain fac_lpr::native_image)

find_package(Threads REQUIRED)
add_library(fac_lpr_concurrency STATIC concurrency/bounded_worker_pool.cpp)
add_library(fac_lpr::concurrency ALIAS fac_lpr_concurrency)
target_compile_features(fac_lpr_concurrency PUBLIC cxx_std_20)
target_include_directories(fac_lpr_concurrency PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(fac_lpr_concurrency PUBLIC fac_lpr::application fac_lpr::native_image Threads::Threads)

add_subdirectory(config)

add_library(fac_lpr_infrastructure INTERFACE)
add_library(fac_lpr::infrastructure ALIAS fac_lpr_infrastructure)
target_link_libraries(fac_lpr_infrastructure INTERFACE fac_lpr::application fac_lpr::domain fac_lpr::native_image fac_lpr::concurrency fac_lpr::crypto fac_lpr::model fac_lpr::paddle_adapter fac_lpr::yolo fac_lpr::lprnet fac_lpr::geometry fac_lpr::adaptive_detection fac_lpr::crop fac_lpr::json_config)

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
    add_library(fac_lpr_onnx_runtime STATIC onnx/execution_provider.cpp onnx/onnx_session.cpp)
    add_library(fac_lpr::onnx_runtime ALIAS fac_lpr_onnx_runtime)
    target_compile_features(fac_lpr_onnx_runtime PUBLIC cxx_std_20)
    target_include_directories(fac_lpr_onnx_runtime PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
    target_link_libraries(fac_lpr_onnx_runtime PUBLIC fac_lpr::application onnxruntime::onnxruntime)

    add_library(fac_lpr_generic_onnx_ocr STATIC onnx/generic_ocr_recognizer.cpp)
    add_library(fac_lpr::generic_onnx_ocr ALIAS fac_lpr_generic_onnx_ocr)
    target_compile_features(fac_lpr_generic_onnx_ocr PUBLIC cxx_std_20)
    target_include_directories(fac_lpr_generic_onnx_ocr PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
    target_link_libraries(fac_lpr_generic_onnx_ocr PUBLIC fac_lpr::onnx_runtime fac_lpr::native_image)

    add_library(fac_lpr_lprnet_onnx STATIC
        lprnet/lprnet_onnx_contract.cpp
        lprnet/lprnet_onnx_ocr_adapter.cpp
    )
    add_library(fac_lpr::lprnet_onnx ALIAS fac_lpr_lprnet_onnx)
    target_compile_features(fac_lpr_lprnet_onnx PUBLIC cxx_std_20)
    target_include_directories(fac_lpr_lprnet_onnx PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
    target_link_libraries(fac_lpr_lprnet_onnx PUBLIC fac_lpr::lprnet fac_lpr::onnx_runtime fac_lpr::generic_onnx_ocr)

    add_library(fac_lpr_yolo_onnx STATIC yolo/yolo_pose_onnx_detector.cpp)
    add_library(fac_lpr::yolo_onnx ALIAS fac_lpr_yolo_onnx)
    target_compile_features(fac_lpr_yolo_onnx PUBLIC cxx_std_20)
    target_include_directories(fac_lpr_yolo_onnx PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
    target_link_libraries(fac_lpr_yolo_onnx PUBLIC fac_lpr::yolo fac_lpr::onnx_runtime fac_lpr::native_image)

    target_link_libraries(fac_lpr_infrastructure INTERFACE fac_lpr::lprnet_onnx fac_lpr::generic_onnx_ocr fac_lpr::yolo_onnx)
endif()

if(FAC_LPR_WITH_OPENCV)
    find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc)
    add_library(fac_lpr_opencv_adapter STATIC opencv_image_view.cpp opencv/perspective_aligner.cpp opencv/crop_enhancers.cpp opencv/double_row_normalizer.cpp opencv/connected_component_layout_analyzer.cpp)
    add_library(fac_lpr::opencv_adapter ALIAS fac_lpr_opencv_adapter)
    target_compile_features(fac_lpr_opencv_adapter PUBLIC cxx_std_20)
    target_include_directories(fac_lpr_opencv_adapter PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include> ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(fac_lpr_opencv_adapter PUBLIC fac_lpr::application fac_lpr::geometry fac_lpr::native_image PRIVATE ${OpenCV_LIBS})
endif()

if(FAC_LPR_WITH_ONNX_RUNTIME AND FAC_LPR_WITH_OPENCV)
    add_library(fac_lpr_production_factory STATIC
        ${PROJECT_SOURCE_DIR}/tools/lpr-cli/cli_engine_factory.cpp
        ${PROJECT_SOURCE_DIR}/tools/lpr-cli/production_activation_gate.cpp
    )
    add_library(fac_lpr::production_factory ALIAS fac_lpr_production_factory)
    target_compile_features(fac_lpr_production_factory PUBLIC cxx_std_20)
    target_include_directories(fac_lpr_production_factory
        PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/tools/lpr-cli>
        PRIVATE ${OpenCV_INCLUDE_DIRS}
    )
    target_link_libraries(fac_lpr_production_factory PUBLIC
        fac_lpr::application
        fac_lpr::model
        fac_lpr::generic_onnx_ocr
        fac_lpr::lprnet_onnx
        fac_lpr::yolo_onnx
        fac_lpr::geometry
        fac_lpr::crop
        fac_lpr::opencv_adapter
    )
    if(MSVC)
        target_compile_options(fac_lpr_production_factory PRIVATE
            /W4
            /permissive-
            /EHsc
            $<$<BOOL:${FAC_LPR_WARNINGS_AS_ERRORS}>:/WX>
        )
    else()
        target_compile_options(fac_lpr_production_factory PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            $<$<BOOL:${FAC_LPR_WARNINGS_AS_ERRORS}>:-Werror>
        )
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(fac_lpr_production_factory PRIVATE -Wno-error=dangling-reference)
        endif()
    endif()
    target_link_libraries(fac_lpr_infrastructure INTERFACE fac_lpr::production_factory)
    target_compile_definitions(fac_lpr_infrastructure INTERFACE FAC_LPR_PRODUCTION_FACTORY_AVAILABLE=1)
endif()
