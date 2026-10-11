option(TOOLSBOX_ENABLE_MSVC_ANALYSIS "Enable MSVC native static analysis for first-party targets" OFF)

function(toolsbox_enable_project_checks target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR "Unknown target passed to toolsbox_enable_project_checks: ${target_name}")
    endif()

    if(MSVC)
        target_compile_options("${target_name}" PRIVATE /W4)

        if(TOOLSBOX_ENABLE_MSVC_ANALYSIS)
            # Security analysis is a blocking check: a new compiler or PREfast
            # warning must be fixed or deliberately suppressed at its source.
            target_compile_options("${target_name}" PRIVATE /analyze /analyze:external- /WX)
        endif()
    endif()
endfunction()
