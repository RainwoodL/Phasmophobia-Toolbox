cmake_minimum_required(VERSION 3.15)
project(GhostRoomPayload LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 静态链接 CRT，避免依赖 vcruntime140.dll / ucrtbase.dll / msvcp140.dll
# 手动 PE 映射时这些 DLL 可能未在目标进程加载，会导致导入表解析失败
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/imgui")

include(CTest)

set(DETOURS_DIR "${CMAKE_SOURCE_DIR}/third_party/detours/src")
add_library(detours STATIC
    "${DETOURS_DIR}/detours.cpp"
    "${DETOURS_DIR}/modules.cpp"
    "${DETOURS_DIR}/disasm.cpp"
    "${DETOURS_DIR}/image.cpp"
)
target_include_directories(detours PUBLIC "${DETOURS_DIR}")
target_compile_options(detours PRIVATE /W3)

add_executable(gen_inventory_resources gen_inventory_resources.cpp)
toolsbox_enable_project_checks(gen_inventory_resources)
target_compile_options(gen_inventory_resources PRIVATE /utf-8)

set(INVENTORY_PNG_FILES
    "${CMAKE_SOURCE_DIR}/resource/Crucifix 1 Square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Crucifix 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Crucifix 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/DOTS 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/DOTS 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/DOTS 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/EMF 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/EMF 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/EMF 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Firelight 1 Square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Firelight 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Firelight 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Flashlight 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Flashlight 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Flashlight 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Ghost w 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Ghost w 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Ghost w 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Head 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Head 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Head 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Igniter 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Igniter 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Igniter 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Repellent 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Repellent 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Repellent 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Motion 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Motion 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Motion 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Para 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Para 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Para 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Photo 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Photo 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Photo 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Salt 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Salt 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Salt 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Sanity 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Sanity 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Sanity 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Sound Recorder 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Sound Recorder 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Sound Recorder 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Sound 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Sound 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Sound 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Spirit box 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Spirit box 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Spirit box 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Thermo 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Thermo 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Thermo 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Tripod 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Tripod 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Tripod 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/UV 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/UV 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/UV 3 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Video 1 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Video 2 square-no-bg.png"
    "${CMAKE_SOURCE_DIR}/resource/Video 3 square-no-bg.png"
)
list(LENGTH INVENTORY_PNG_FILES INVENTORY_PNG_COUNT)
if(NOT INVENTORY_PNG_COUNT EQUAL 63)
    message(FATAL_ERROR "Inventory PNG manifest must contain exactly 63 files")
endif()

set(SPONSOR_PNG_FILE "${CMAKE_SOURCE_DIR}/resource/wx_zfb.png")
set(INVENTORY_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(INVENTORY_GENERATED_HEADER "${INVENTORY_GENERATED_DIR}/inventory_png_data.h")
add_custom_command(
    OUTPUT "${INVENTORY_GENERATED_HEADER}"
    COMMAND $<TARGET_FILE:gen_inventory_resources>
            "${INVENTORY_GENERATED_HEADER}" "${SPONSOR_PNG_FILE}" ${INVENTORY_PNG_FILES}
    DEPENDS gen_inventory_resources "${SPONSOR_PNG_FILE}" ${INVENTORY_PNG_FILES}
    COMMENT "Embedding 63 inventory PNG resources and sponsor PNG"
    COMMAND_EXPAND_LISTS
    VERBATIM
)
add_custom_target(inventory_png_header DEPENDS "${INVENTORY_GENERATED_HEADER}")

if(BUILD_TESTING)
    add_executable(timer_logic_tests
        timer_logic_tests.cpp
        timer_logic.cpp
        feature_logic.cpp
    )
    toolsbox_enable_project_checks(timer_logic_tests)
    target_sources(timer_logic_tests PRIVATE inventory_resources.cpp "${INVENTORY_GENERATED_HEADER}")
    target_include_directories(timer_logic_tests PRIVATE "${INVENTORY_GENERATED_DIR}")
    add_dependencies(timer_logic_tests inventory_png_header)
    target_compile_options(timer_logic_tests PRIVATE /utf-8 /UNDEBUG)
    add_test(NAME timer_logic_tests COMMAND timer_logic_tests)

    add_executable(inventory_textures_tests
        inventory_textures_tests.cpp
        inventory_textures.cpp
        inventory_resources.cpp
        "${INVENTORY_GENERATED_HEADER}"
    )
    toolsbox_enable_project_checks(inventory_textures_tests)
    target_include_directories(inventory_textures_tests PRIVATE "${INVENTORY_GENERATED_DIR}")
    add_dependencies(inventory_textures_tests inventory_png_header)
    target_compile_options(inventory_textures_tests PRIVATE /utf-8 /UNDEBUG)
    target_link_libraries(inventory_textures_tests PRIVATE d3d11 windowscodecs ole32)
    add_test(NAME inventory_textures_tests COMMAND inventory_textures_tests)

    add_executable(settings_store_tests
        settings_store_tests.cpp
        settings_store.cpp
        settings_wire.cpp
        timer_logic.cpp
        feature_logic.cpp
    )
    toolsbox_enable_project_checks(settings_store_tests)
    target_compile_definitions(settings_store_tests PRIVATE TOOLSBOX_SETTINGS_TESTING)
    target_compile_options(settings_store_tests PRIVATE /utf-8 /UNDEBUG)
    target_link_libraries(settings_store_tests PRIVATE shell32)
    add_test(NAME settings_store_tests COMMAND settings_store_tests)

    add_executable(overlay_visibility_tests
        overlay_visibility_tests.cpp hunt_timer.cpp player_nicknames.cpp
        font_store.cpp settings_store.cpp settings_wire.cpp timer_logic.cpp feature_logic.cpp
        ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp)
    toolsbox_enable_project_checks(overlay_visibility_tests)
    target_compile_definitions(overlay_visibility_tests PRIVATE TOOLSBOX_SETTINGS_TESTING)
    target_compile_options(overlay_visibility_tests PRIVATE /utf-8 /UNDEBUG)
    target_include_directories(overlay_visibility_tests PRIVATE ${IMGUI_DIR})
    target_link_libraries(overlay_visibility_tests PRIVATE shell32)
    add_test(NAME overlay_visibility_tests COMMAND overlay_visibility_tests)

    add_executable(ghost_speed_sync_tests
        ghost_speed_sync_tests.cpp
        ghost_speed_sync_protocol.cpp
    )
    toolsbox_enable_project_checks(ghost_speed_sync_tests)
    target_compile_options(ghost_speed_sync_tests PRIVATE /utf-8 /UNDEBUG)
    add_test(NAME ghost_speed_sync_tests COMMAND ghost_speed_sync_tests)
endif()

add_library(payload SHARED
    payload.cpp
    d3d11_hook.cpp
    overlay.cpp
    font_store.cpp
    smudge_timer.cpp
    game_time.cpp
    hunt_timer.cpp
    ghost_status.cpp
    ghost_runtime.cpp
    ghost_distance.cpp
    ghost_speed_display.cpp
    ghost_speed_sync.cpp
    ghost_speed_sync_protocol.cpp
    sanity_display.cpp
    ouija_translation.cpp
    crucifix_timer.cpp
    crosshair_info.cpp
    inventory_bar.cpp
    stamina_display.cpp
    inventory_resources.cpp
    inventory_textures.cpp
    "${INVENTORY_GENERATED_HEADER}"
    screen_crosshair.cpp
    player_nicknames.cpp
    player_sanity_lifecycle.cpp
    game_update_dispatcher.cpp
    inline_hook.cpp
    feature_logic.cpp
    il2cpp_runtime.cpp
    settings_store.cpp
    settings_wire.cpp
    timer_logic.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
)
toolsbox_enable_project_checks(payload)

if(MSVC AND TOOLSBOX_ENABLE_MSVC_ANALYSIS)
    # These SEH guards intentionally isolate faults raised while inspecting
    # another module. C6320 warns about that exact deliberate behavior.
    set_source_files_properties(
        game_update_dispatcher.cpp
        d3d11_hook.cpp
        il2cpp_runtime.cpp
        PROPERTIES COMPILE_OPTIONS "/wd6320"
    )

    # Vendored ImGui is checked through dependency updates; PREfast findings
    # for third-party sources must not hide warnings in first-party code.
    set_source_files_properties(
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
        ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
        PROPERTIES COMPILE_OPTIONS "/analyze-"
    )
endif()

target_include_directories(payload PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${INVENTORY_GENERATED_DIR}"
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

add_dependencies(payload inventory_png_header)
target_compile_options(payload PRIVATE /utf-8 /GS /sdl /permissive-)
target_link_options(payload PRIVATE
    /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /INCREMENTAL:NO
    $<$<CONFIG:Release>:/OPT:REF /OPT:ICF /Brepro>
)
target_link_libraries(payload PRIVATE
    user32
    kernel32
    shell32
    psapi
    d3d11
    dxgi
    windowscodecs
    ole32
    detours
)

set_target_properties(payload PROPERTIES
    OUTPUT_NAME "payload"
    RUNTIME_OUTPUT_DIRECTORY "${TOOLSBOX_ARTIFACT_DIR}/embedded"
    LIBRARY_OUTPUT_DIRECTORY "${TOOLSBOX_ARTIFACT_DIR}/embedded"
)
