cmake_minimum_required(VERSION 3.15)
project(PhasmophobiaInjector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 静态链接 CRT，注入器单 exe 不依赖外部 CRT DLL
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ------------------------------------------------------------
# 1. 构建生成器：将原始 payload.dll 字节输出到 payload_dll.h
# ------------------------------------------------------------
add_executable(gen_payload gen_payload.cpp)
toolsbox_enable_project_checks(gen_payload)
set_target_properties(gen_payload PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${TOOLSBOX_ARTIFACT_DIR}/tools"
)
target_compile_options(gen_payload PRIVATE /utf-8 /GS /sdl /permissive-)

if(BUILD_TESTING)
    add_executable(user_messages_tests user_messages_tests.cpp)
    toolsbox_enable_project_checks(user_messages_tests)
    target_compile_options(user_messages_tests PRIVATE /utf-8 /GS /sdl /permissive- /UNDEBUG)
    add_test(NAME user_messages_tests COMMAND user_messages_tests)

endif()

# ------------------------------------------------------------
# 2. 自定义命令：调用 gen_payload 生成 payload_dll.h
#    使用 $<TARGET_FILE:payload> 自动解析 payload.dll 的完整路径
#    （兼容多配置生成器，如 VS 的 Release/Debug 子目录）
# ------------------------------------------------------------
set(PAYLOAD_HEADER "${CMAKE_CURRENT_BINARY_DIR}/payload_dll.h")

add_custom_command(
    OUTPUT "${PAYLOAD_HEADER}"
    COMMAND $<TARGET_FILE:gen_payload>
            "$<TARGET_FILE:payload>"
            "${PAYLOAD_HEADER}"
    DEPENDS payload gen_payload
    COMMENT "Embedding raw payload.dll bytes -> payload_dll.h"
    VERBATIM
)

# 自定义目标包装生成的头文件，方便显式依赖
add_custom_target(injector_payload_header DEPENDS "${PAYLOAD_HEADER}")

# ------------------------------------------------------------
# 3. 主注入器：内嵌原始 payload 的单 exe
# ------------------------------------------------------------
add_executable(PhasmophobiaInjector
    main.cpp
    manual_pe.cpp
    resource.rc
    "${PAYLOAD_HEADER}"
)
# The generated header is included by main.cpp; make payload changes rebuild it.
set_property(SOURCE main.cpp APPEND PROPERTY OBJECT_DEPENDS "${PAYLOAD_HEADER}")
toolsbox_enable_project_checks(PhasmophobiaInjector)

target_include_directories(PhasmophobiaInjector PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_compile_options(PhasmophobiaInjector PRIVATE /utf-8 /GS /sdl /permissive- /guard:cf)
target_compile_definitions(PhasmophobiaInjector PRIVATE TOOLSBOX_INJECTOR=1)
target_link_options(PhasmophobiaInjector PRIVATE
    /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /GUARD:CF /CETCOMPAT /INCREMENTAL:NO
    $<$<CONFIG:Release>:/OPT:REF /OPT:ICF /Brepro>
)

target_link_libraries(PhasmophobiaInjector PRIVATE
    kernel32
    advapi32
    psapi
)

# 确保先生成 payload_dll.h 再编译 main.cpp
add_dependencies(PhasmophobiaInjector injector_payload_header)

set_target_properties(PhasmophobiaInjector PROPERTIES
    OUTPUT_NAME "PhasmophobiaInjector"
    RUNTIME_OUTPUT_DIRECTORY "${TOOLSBOX_ARTIFACT_DIR}/standalone"
    LINK_FLAGS "/SUBSYSTEM:CONSOLE"
)
