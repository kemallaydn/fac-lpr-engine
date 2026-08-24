find_package(Threads REQUIRED)

add_library(fac_lpr_concurrency STATIC bounded_worker_pool.cpp)
add_library(fac_lpr::concurrency ALIAS fac_lpr_concurrency)
target_compile_features(fac_lpr_concurrency PUBLIC cxx_std_20)
target_include_directories(
    fac_lpr_concurrency
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(
    fac_lpr_concurrency
    PUBLIC
        fac_lpr::application
        fac_lpr::native_image
        Threads::Threads
)
