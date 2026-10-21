# The launcher layer. Two libraries:
#   allnes_display  - windowing/presentation abstraction (SDL2, headless)
#   allnes_launcher - LVGL UI plus the game-library/config model
# The top-level CMakeLists.txt locates SDL2 (ALLNES_SDL2_LINK) and LVGL
# (ALLNES_LVGL_LINK); this file only consumes them.

add_library(allnes_display STATIC
    display/display.cpp
    display/display_sdl.cpp
    display/display_headless.cpp
)
target_include_directories(allnes_display PUBLIC "${CMAKE_SOURCE_DIR}")
target_link_libraries(allnes_display PUBLIC allnes_gamepad ${ALLNES_SDL2_LINK})

set(ALLNES_LAUNCHER_SOURCES
    model/library.cpp
    model/settings.cpp
    model/paths.cpp
)

if(ALLNES_LVGL_LINK)
    list(APPEND ALLNES_LAUNCHER_SOURCES
        ui/lvgl_launcher.cpp
        ui/stb_write_impl.cpp
    )
endif()

add_library(allnes_launcher STATIC ${ALLNES_LAUNCHER_SOURCES})

target_include_directories(allnes_launcher
    PUBLIC "${CMAKE_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/third_party"
)
target_link_libraries(allnes_launcher PUBLIC allnes_display)

if(ALLNES_LVGL_LINK)
    target_link_libraries(allnes_launcher PUBLIC ${ALLNES_LVGL_LINK})
    target_compile_definitions(allnes_launcher PUBLIC ALLNES_HAVE_LVGL=1)
else()
    message(STATUS "LVGL not found - the launcher UI is not built")
endif()

# Model-only test: no SDL needed, so it also runs on headless CI.
add_executable(allnes_launcher_model_test
    tests/launcher_model_test.cpp
    model/library.cpp
    model/settings.cpp
)
target_include_directories(allnes_launcher_model_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/model"
)
add_test(NAME launcher_model COMMAND allnes_launcher_model_test)

# Headless display backend test: no window, no video device.
add_executable(allnes_display_headless_test tests/display_test.cpp)
target_link_libraries(allnes_display_headless_test PRIVATE allnes_display)
add_test(NAME display_headless COMMAND allnes_display_headless_test)
