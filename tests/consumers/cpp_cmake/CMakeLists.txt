cmake_minimum_required(VERSION 3.25)
project(fac_lpr_cpp_consumer LANGUAGES CXX)

find_package(fac-lpr-engine CONFIG REQUIRED)

add_executable(fac_lpr_cpp_consumer main.cpp)
target_compile_features(fac_lpr_cpp_consumer PRIVATE cxx_std_20)
target_link_libraries(fac_lpr_cpp_consumer PRIVATE fac_lpr::engine)
