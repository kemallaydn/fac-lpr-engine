add_library(fac_lpr_infrastructure INTERFACE)
add_library(fac_lpr::infrastructure ALIAS fac_lpr_infrastructure)

target_link_libraries(
    fac_lpr_infrastructure
    INTERFACE
        fac_lpr::application
        fac_lpr::domain
)
