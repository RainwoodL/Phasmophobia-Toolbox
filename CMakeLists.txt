cmake_minimum_required(VERSION 3.15)
project(PhasmophobiaToolbox LANGUAGES CXX)

include(CTest)
include(cmake/ProjectChecks.cmake)

# Release artifacts are intentionally separated from the CMake intermediates.
set(TOOLSBOX_RELEASE_DIR "${CMAKE_SOURCE_DIR}/release" CACHE PATH
    "Directory for packaged Release artifacts")
if(CMAKE_CONFIGURATION_TYPES)
    set(TOOLSBOX_ARTIFACT_DIR "${TOOLSBOX_RELEASE_DIR}/$<CONFIG>")
elseif(CMAKE_BUILD_TYPE MATCHES "^[Rr]elease$")
    set(TOOLSBOX_ARTIFACT_DIR "${TOOLSBOX_RELEASE_DIR}")
else()
    set(TOOLSBOX_ARTIFACT_DIR "${CMAKE_BINARY_DIR}/artifacts")
endif()

# payload 必须先于 injector 配置，因为 injector 的自定义命令依赖 payload 目标
add_subdirectory(payload)
add_subdirectory(injector)

add_executable(pe_embed tools/pe_embed.cpp)
target_compile_features(pe_embed PRIVATE cxx_std_17)
target_compile_options(pe_embed PRIVATE /utf-8 /GS /sdl /permissive-)
toolsbox_enable_project_checks(pe_embed)
set_target_properties(pe_embed PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${TOOLSBOX_ARTIFACT_DIR}/tools"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

if(BUILD_TESTING)
    add_custom_target(check
        COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure --build-config $<CONFIG>
        DEPENDS
            pe_embed
            PhasmophobiaInjector
            timer_logic_tests
            inventory_textures_tests
            settings_store_tests
            overlay_visibility_tests
            ghost_speed_sync_tests
            user_messages_tests
        USES_TERMINAL
        COMMENT "Running Phasmophobia Toolbox unit tests"
    )
endif()
