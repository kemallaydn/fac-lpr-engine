option(FAC_LPR_REPRODUCIBLE_BUILD "Enable deterministic/reproducible compiler metadata settings" OFF)
set(FAC_LPR_BUILD_GIT_COMMIT "unknown" CACHE STRING "Source commit embedded in build provenance")

if(FAC_LPR_REPRODUCIBLE_BUILD)
    if(MSVC)
        add_compile_options(/Brepro)
        add_link_options(/Brepro)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(
            "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
            "-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=."
            "-fmacro-prefix-map=${CMAKE_SOURCE_DIR}=."
        )
    else()
        message(WARNING "No explicit reproducible-build flags for ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()
