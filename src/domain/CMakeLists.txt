add_library(fac_lpr_domain INTERFACE)
add_library(fac_lpr::domain ALIAS fac_lpr_domain)

target_compile_features(fac_lpr_domain INTERFACE cxx_std_20)
target_include_directories(fac_lpr_domain
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
