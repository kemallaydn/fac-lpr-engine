option(FAC_LPR_ENABLE_COVERAGE "Enable line/branch coverage instrumentation" OFF)

if(FAC_LPR_ENABLE_COVERAGE)
    if(MSVC)
        message(FATAL_ERROR "FAC_LPR_ENABLE_COVERAGE currently requires GCC or Clang")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Unsupported compiler for FAC_LPR_ENABLE_COVERAGE: ${CMAKE_CXX_COMPILER_ID}")
    endif()

    add_compile_options(-O0 -g --coverage)
    add_link_options(--coverage)
endif()
