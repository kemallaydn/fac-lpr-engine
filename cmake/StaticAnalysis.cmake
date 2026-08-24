option(FAC_LPR_ENABLE_CLANG_TIDY "Run clang-tidy during compilation" OFF)

if(NOT FAC_LPR_ENABLE_CLANG_TIDY)
    return()
endif()

find_program(FAC_LPR_CLANG_TIDY_EXECUTABLE NAMES clang-tidy clang-tidy-20 clang-tidy-19 clang-tidy-18)
if(NOT FAC_LPR_CLANG_TIDY_EXECUTABLE)
    message(FATAL_ERROR "FAC_LPR_ENABLE_CLANG_TIDY=ON but clang-tidy was not found")
endif()

set(CMAKE_CXX_CLANG_TIDY
    "${FAC_LPR_CLANG_TIDY_EXECUTABLE};--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy"
    CACHE STRING "FAC LPR clang-tidy command" FORCE)
message(STATUS "FAC LPR clang-tidy enabled: ${FAC_LPR_CLANG_TIDY_EXECUTABLE}")
