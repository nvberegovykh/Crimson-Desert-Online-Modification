# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20)
project(cd_multiplayer_client LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The client plugin builds for Windows x64 only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- nlohmann/json --------------------------------------------------------
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# --- MinHook --------------------------------------------------------------
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.3
)
FetchContent_MakeAvailable(minhook)

# --- Dear ImGui (no CMake of its own; we compile the sources) -------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.4
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# --- Our proxy dxgi.dll ---------------------------------------------------
set(CDMP_SOURCES
    src/dllmain.cpp
    src/session.cpp
    src/proxy/dxgi_proxy.cpp
    src/game/scanner.cpp
    src/game/memory.cpp
    src/inventory/inventory_hook.cpp
    src/ui/modal.cpp
    src/hooks/render_hook.cpp
    src/net/client.cpp
    src/net/protocol.cpp
    src/sync/player_sync.cpp
    src/sync/combat_sync.cpp
    src/sync/cutscene_sync.cpp
    src/sync/puzzle_sync.cpp
    src/util/log.cpp
)

add_library(dxgi SHARED ${CDMP_SOURCES} src/proxy/dxgi.def)

set_target_properties(dxgi PROPERTIES
    OUTPUT_NAME "dxgi"
    PREFIX ""
)

# Use the .def file so we export CreateDXGIFactory et al. with the right names.
target_link_options(dxgi PRIVATE
    "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/proxy/dxgi.def"
)

target_compile_definitions(dxgi PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
)

target_include_directories(dxgi PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(dxgi PRIVATE
    imgui
    minhook
    nlohmann_json::nlohmann_json
    d3d11
    dxguid
    winhttp
    ws2_32
    crypt32
    shlwapi
)

# Optional post-build copy to a game folder, when -DOUTPUT_DIR=... is given.
if(DEFINED OUTPUT_DIR)
    add_custom_command(TARGET dxgi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:dxgi> "${OUTPUT_DIR}/dxgi.dll"
        COMMENT "Copying dxgi.dll to ${OUTPUT_DIR}"
    )
endif()
