cmake_minimum_required(VERSION 3.12)
project(AllNES LANGUAGES C CXX)

# LVGL is added from third_party/ but we attach FreeType to its target from
# this directory; CMP0079 permits target_link_libraries() across directories.
if(POLICY CMP0079)
    cmake_policy(SET CMP0079 NEW)
endif()

# Portable NES emulator, C++17. The C compiler is needed by the vendored LVGL
# launcher backend (third_party/lvgl is pure C).
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build: Debug or Release." FORCE)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall -Wextra -g")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")
endif()

enable_testing()

add_subdirectory(core)

# SDL2 is shared by the launcher layer and the desktop host. If it is missing
# both are skipped: the core and the headless tools still build, so CI can
# validate a machine without a graphics stack.

# Preferred: the SDL2 CMake package (Homebrew, vcpkg, MSYS2, most distros).
find_package(SDL2 CONFIG QUIET)

set(ALLNES_SDL2_LINK "")
if(TARGET SDL2::SDL2)
    # Some hand-extracted SDL2 packages hard-code a prefix (e.g. /usr) that does
    # not match where the files were unpacked. Only trust the target when its
    # include directories actually exist.
    get_target_property(_sdl2_includes SDL2::SDL2 INTERFACE_INCLUDE_DIRECTORIES)
    set(_sdl2_usable TRUE)
    if(_sdl2_includes)
        foreach(_dir IN LISTS _sdl2_includes)
            if(NOT EXISTS "${_dir}")
                set(_sdl2_usable FALSE)
            endif()
        endforeach()
    endif()
    if(_sdl2_usable)
        set(ALLNES_SDL2_LINK SDL2::SDL2)
    endif()
endif()

# Fallback: locate headers/libraries directly. Set SDL2_ROOT to point at an
# install prefix, e.g. -DSDL2_ROOT=$HOME/opt/sdl2/usr or SDL2_ROOT=... .
if(NOT ALLNES_SDL2_LINK)
    find_path(SDL2_INCLUDE_DIR
        NAMES SDL.h
        HINTS ${SDL2_ROOT} ENV SDL2_ROOT
        PATH_SUFFIXES SDL2 include/SDL2
    )
    find_library(SDL2_LIBRARY
        NAMES SDL2 SDL2-2.0
        HINTS ${SDL2_ROOT} ENV SDL2_ROOT
        PATH_SUFFIXES lib lib/x86_64-linux-gnu lib64
    )

    if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
        # Debian/Ubuntu ship SDL_config.h as a wrapper around a multiarch
        # _real_SDL_config.h; add that directory when present.
        get_filename_component(_sdl2_include_root "${SDL2_INCLUDE_DIR}" DIRECTORY)
        file(GLOB _sdl2_real_config
            "${_sdl2_include_root}/*/SDL2/_real_SDL_config.h"
            "${_sdl2_include_root}/SDL2/_real_SDL_config.h")

        add_library(allnes_sdl2 INTERFACE)
        target_include_directories(allnes_sdl2 INTERFACE "${SDL2_INCLUDE_DIR}")
        foreach(_real IN LISTS _sdl2_real_config)
            get_filename_component(_sdl2_multiarch "${_real}" DIRECTORY)
            get_filename_component(_sdl2_multiarch "${_sdl2_multiarch}" DIRECTORY)
            target_include_directories(allnes_sdl2 INTERFACE "${_sdl2_multiarch}")
        endforeach()
        target_link_libraries(allnes_sdl2 INTERFACE "${SDL2_LIBRARY}")
        set(ALLNES_SDL2_LINK allnes_sdl2)
        message(STATUS "Using SDL2 from ${SDL2_INCLUDE_DIR} / ${SDL2_LIBRARY}")
    endif()
endif()

# Optional LVGL backend for the launcher. The vendored LVGL tree lives in
# third_party/ (never edited); the project-owned lv_conf.h stays in
# launcher/lvgl/ and is passed to LVGL through LV_BUILD_CONF_PATH.
option(ALLNES_WITH_LVGL "Build the LVGL launcher backend" ON)
set(ALLNES_LVGL_LINK "")

if(ALLNES_SDL2_LINK AND ALLNES_WITH_LVGL)
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/lvgl/CMakeLists.txt")
        find_package(Freetype QUIET)
        if(TARGET Freetype::Freetype)
            set(LV_BUILD_CONF_PATH "${CMAKE_SOURCE_DIR}/launcher/lvgl/lv_conf.h"
                CACHE FILEPATH "AllNES LVGL configuration" FORCE)
            set(LV_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
            set(LV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
            set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
            set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
            set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
            set(CONFIG_LV_USE_PRIVATE_API OFF CACHE BOOL "" FORCE)

            add_subdirectory(third_party/lvgl lvgl_build EXCLUDE_FROM_ALL)

            # lv_freetype.h is part of LVGL's public headers, so consumers of
            # lvgl need FreeType's include directories as well.
            target_link_libraries(lvgl PUBLIC Freetype::Freetype)

            # LVGL configures lv_conf.h only for its own compilation. Every
            # consumer must see the same configuration (FreeType, the CLIB
            # allocator, the 'A' filesystem drive, ...), otherwise lvgl.h in
            # launcher/ picks the compiled-in defaults instead.
            target_compile_definitions(lvgl INTERFACE
                "LV_CONF_PATH=\"${CMAKE_SOURCE_DIR}/launcher/lvgl/lv_conf.h\""
                LV_KCONFIG_IGNORE
                LV_LVGL_H_INCLUDE_SIMPLE
            )
            if(UNIX AND NOT APPLE)
                target_link_libraries(lvgl PUBLIC m)
            endif()
            set(ALLNES_LVGL_LINK lvgl::lvgl)
            message(STATUS "LVGL launcher backend: third_party/lvgl (config: launcher/lvgl/lv_conf.h)")
        else()
            message(STATUS "Freetype not found - the LVGL launcher UI is disabled "
                           "(the display layer still builds)")
        endif()
    else()
        message(STATUS "third_party/lvgl not found - the LVGL launcher backend is disabled")
    endif()
endif()

if(ALLNES_SDL2_LINK)
    # The launcher is its own layer; the host is the composition root that
    # owns both the launcher and the emulator.
    add_subdirectory(launcher)
    add_subdirectory(host)
else()
    message(STATUS "SDL2 not found - skipping the launcher and the SDL host "
                   "(core and tools still build)")
endif()

add_subdirectory(tools)
