add_library(fac_lpr_application STATIC
    image_validation.cpp
)
add_library(fac_lpr::application ALIAS fac_lpr_application)

target_compile_features(fac_lpr_application PUBLIC cxx_std_20)
target_include_directories(
    fac_lpr_application
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(fac_lpr_application PUBLIC fac_lpr::domain)
