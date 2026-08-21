function(fac_lpr_import_onnxruntime root)
    if(TARGET onnxruntime::onnxruntime)
        return()
    endif()

    if(NOT IS_ABSOLUTE "${root}")
        get_filename_component(_ort_root "${root}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
    else()
        set(_ort_root "${root}")
    endif()

    set(_ort_include "${_ort_root}/include")
    if(NOT EXISTS "${_ort_include}/onnxruntime_cxx_api.h")
        message(FATAL_ERROR "Invalid FAC_LPR_ONNXRUNTIME_ROOT: missing include/onnxruntime_cxx_api.h under ${_ort_root}")
    endif()

    if(WIN32)
        set(_ort_implib "${_ort_root}/lib/onnxruntime.lib")
        set(_ort_dll "${_ort_root}/lib/onnxruntime.dll")
        if(NOT EXISTS "${_ort_dll}")
            set(_ort_dll "${_ort_root}/bin/onnxruntime.dll")
        endif()
        if(NOT EXISTS "${_ort_implib}" OR NOT EXISTS "${_ort_dll}")
            message(FATAL_ERROR "Invalid Windows ONNX Runtime package under ${_ort_root}")
        endif()

        add_library(onnxruntime::onnxruntime SHARED IMPORTED GLOBAL)
        set_target_properties(onnxruntime::onnxruntime PROPERTIES
            IMPORTED_IMPLIB "${_ort_implib}"
            IMPORTED_LOCATION "${_ort_dll}"
            INTERFACE_INCLUDE_DIRECTORIES "${_ort_include}"
        )
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_ort_library "${_ort_root}/lib/libonnxruntime.so")
        if(NOT EXISTS "${_ort_library}")
            file(GLOB _ort_candidates "${_ort_root}/lib/libonnxruntime.so.*")
            list(SORT _ort_candidates)
            list(GET _ort_candidates 0 _ort_library)
        endif()
        if(NOT EXISTS "${_ort_library}")
            message(FATAL_ERROR "Invalid Linux ONNX Runtime package under ${_ort_root}")
        endif()

        add_library(onnxruntime::onnxruntime SHARED IMPORTED GLOBAL)
        set_target_properties(onnxruntime::onnxruntime PROPERTIES
            IMPORTED_LOCATION "${_ort_library}"
            INTERFACE_INCLUDE_DIRECTORIES "${_ort_include}"
        )
    else()
        message(FATAL_ERROR "Prebuilt ONNX Runtime bootstrap currently supports Windows x64 and Linux x64")
    endif()
endfunction()
