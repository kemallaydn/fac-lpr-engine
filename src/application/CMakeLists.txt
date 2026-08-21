add_library(fac_lpr_application INTERFACE)
add_library(fac_lpr::application ALIAS fac_lpr_application)

target_link_libraries(fac_lpr_application INTERFACE fac_lpr::domain)
