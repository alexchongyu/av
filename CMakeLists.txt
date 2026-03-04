cmake_minimum_required(VERSION 3.24)
project(av C CXX)   # C required for glad2 generated sources

# Static CRT for standalone deployment (no vcruntime DLL needed)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0")
    # Apple Silicon: arm64 only
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64")
    endif()
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ── SDL3 ──────────────────────────────────────────────────────────────────────
set(SDL_SHARED     OFF CACHE BOOL "" FORCE)
set(SDL_STATIC     ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS      OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(SDL3)

# ── glad2 ─────────────────────────────────────────────────────────────────────
FetchContent_Declare(glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG        v2.0.6
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake
)
FetchContent_MakeAvailable(glad)
glad_add_library(glad_gl STATIC REPRODUCIBLE API gl:core=3.3)

# ── Dear ImGui (docking branch) ───────────────────────────────────────────────
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC SDL3::SDL3-static glad_gl)
target_compile_definitions(imgui_lib PUBLIC
    # No IMGUI_IMPL_OPENGL_LOADER_CUSTOM: imgui will auto-detect glad2 via __has_include
    IMGUI_DEFINE_MATH_OPERATORS
)

# ── stb (header-only) ─────────────────────────────────────────────────────────
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)

add_library(stb_headers INTERFACE)
target_include_directories(stb_headers INTERFACE ${stb_SOURCE_DIR})

# ── lcms2 (optional) ──────────────────────────────────────────────────────────
find_package(PkgConfig QUIET)
set(LCMS2_FOUND FALSE)
if(PkgConfig_FOUND)
    pkg_check_modules(LCMS2 QUIET IMPORTED_TARGET lcms2)
    if(LCMS2_FOUND)
        set(LCMS2_TARGET PkgConfig::LCMS2)
    endif()
endif()
if(NOT LCMS2_FOUND)
    find_package(LCMS2 QUIET)
    if(LCMS2_FOUND)
        set(LCMS2_TARGET ${LCMS2_LIBRARIES})
    endif()
endif()

# ── Source files ──────────────────────────────────────────────────────────────
set(AV_SOURCES
    src/main.cpp
    src/app.cpp
    src/viewport.cpp
    src/image_loader.cpp
    src/gl_texture.cpp
    src/diff_engine.cpp
    src/stb_impl.cpp
    src/image_save.cpp
    src/image_open.cpp
    src/soft_renderer.cpp
    src/chart_export.cpp
    src/ui/main_window.cpp
    src/ui/image_panel.cpp
    src/ui/statusbar.cpp
    src/ui/chart_windows.cpp
)

add_executable(av ${AV_SOURCES})

target_include_directories(av PRIVATE
    src
)

target_link_libraries(av PRIVATE
    SDL3::SDL3-static
    glad_gl
    imgui_lib
    stb_headers
)

target_compile_definitions(av PRIVATE
    IMGUI_FONT_DIR="${imgui_SOURCE_DIR}/misc/fonts"
)

# lcms2 optional
if(LCMS2_FOUND)
    target_compile_definitions(av PRIVATE AV_HAS_LCMS2)
    target_link_libraries(av PRIVATE ${LCMS2_TARGET})
    message(STATUS "lcms2 found — color management enabled")
else()
    message(STATUS "lcms2 not found — color management disabled (build continues)")
endif()

# Platform-specific OpenGL
if(APPLE)
    target_link_libraries(av PRIVATE
        "-framework OpenGL"
        "-framework CoreFoundation"
    )
elseif(UNIX)
    find_package(OpenGL REQUIRED)
    target_link_libraries(av PRIVATE OpenGL::GL)
elseif(WIN32)
    target_link_libraries(av PRIVATE opengl32)
endif()

# Compiler warnings + ImTextureID 경고 억제 (clang-cl 64-bit)
if(MSVC)
    target_compile_options(av PRIVATE /W3 /wd4312)
else()
    target_compile_options(av PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# Windows: stb UTF-8 경로 지원
if(WIN32)
    target_compile_definitions(av PRIVATE STBI_WINDOWS_UTF8)
endif()

# Output directory: av/bin/  (multi-config 제너레이터도 동일 경로 사용)
set_target_properties(av PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY                ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG          ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE        ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     ${CMAKE_SOURCE_DIR}/bin
)

# Windows: 콘솔 창 숨기기 (GUI 서브시스템)
if(WIN32)
    set_target_properties(av PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(av PRIVATE /ENTRY:mainCRTStartup)
endif()

# Install + Auto-copy (플랫폼별 분기)
if(WIN32)
    install(TARGETS av RUNTIME DESTINATION $ENV{LOCALAPPDATA}/av)
    add_custom_command(TARGET av POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{LOCALAPPDATA}/av"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:av> "$ENV{LOCALAPPDATA}/av/"
        COMMENT "Installing av → %LOCALAPPDATA%/av/"
    )
else()
    install(TARGETS av RUNTIME DESTINATION $ENV{HOME}/.local/bin)
    add_custom_command(TARGET av POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $ENV{HOME}/.local/bin
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:av> $ENV{HOME}/.local/bin/av
        COMMENT "Installing av → ~/.local/bin/av"
    )
endif()

# ── Linux AppImage ────────────────────────────────────────────────────────────
if(UNIX AND NOT APPLE)
    set(APPIMAGE_DIR "${CMAKE_BINARY_DIR}/AppImage")
    set(LINUXDEPLOY  "${APPIMAGE_DIR}/linuxdeploy-x86_64.AppImage")

    # linuxdeploy 자동 다운로드
    add_custom_command(
        OUTPUT "${LINUXDEPLOY}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${APPIMAGE_DIR}"
        COMMAND curl -fSL -o "${LINUXDEPLOY}"
            "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
        COMMAND chmod +x "${LINUXDEPLOY}"
        COMMENT "Downloading linuxdeploy..."
    )

    add_custom_target(appimage
        DEPENDS av "${LINUXDEPLOY}"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${APPIMAGE_DIR}/AppDir"
        COMMAND ${CMAKE_COMMAND} -E env
            OUTPUT="${CMAKE_SOURCE_DIR}/bin/av-x86_64.AppImage"
            "${LINUXDEPLOY}"
                --appdir "${APPIMAGE_DIR}/AppDir"
                --executable $<TARGET_FILE:av>
                --desktop-file "${CMAKE_SOURCE_DIR}/packaging/av.desktop"
                --icon-file "${CMAKE_SOURCE_DIR}/packaging/av.svg"
                --output appimage
        COMMENT "Building AppImage -> bin/av-x86_64.AppImage"
    )
endif()

message(STATUS "av build configured — C++20, OpenGL 3.3 Core")
