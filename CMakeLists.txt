cmake_minimum_required(VERSION 3.14)
project(IDMPopupGuard VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies ────────────────────────────────────────────────────────

include(FetchContent)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

# Only build what we need — skip tests/benchmarks in the JSON library
set(JSON_BuildTests    OFF CACHE BOOL "" FORCE)
set(JSON_Install       OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(json)

# ── IDM Popup Guard  (WIN32 = no console window) ───────────────────────

add_executable(IDMPopupGuard WIN32
    src/core/Config.cpp
    src/core/RuleEngine.cpp
    src/platform/windows/ProcessDetector.cpp
    src/platform/windows/WindowMonitor.cpp
    src/platform/windows/main.cpp
)

target_include_directories(IDMPopupGuard PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/platform/windows
)

target_link_libraries(IDMPopupGuard PRIVATE nlohmann_json::nlohmann_json)

# Compiler warnings and link options
if(MSVC)
    target_compile_options(IDMPopupGuard PRIVATE /W4)
else()
    target_compile_options(IDMPopupGuard PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(IDMPopupGuard PRIVATE -mwindows -static -static-libgcc -static-libstdc++)
endif()

# ── Window Inspector  (console development tool) ───────────────────────

add_executable(WindowInspector
    tools/WindowInspector.cpp
)

if(MSVC)
    target_compile_options(WindowInspector PRIVATE /W4)
else()
    target_compile_options(WindowInspector PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(WindowInspector PRIVATE -static -static-libgcc -static-libstdc++)
endif()


# ── Tests ──────────────────────────────────────────────────────────────

add_executable(test_rules
    tests/test_rules.cpp
    src/core/Config.cpp
    src/core/RuleEngine.cpp
)

target_include_directories(test_rules PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(test_rules PRIVATE nlohmann_json::nlohmann_json)

# ── Post-build: copy config.json next to the executable ────────────────

add_custom_command(TARGET IDMPopupGuard POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/config.json"
        "$<TARGET_FILE_DIR:IDMPopupGuard>/config.json"
    COMMENT "Copying config.json to output directory"
)

