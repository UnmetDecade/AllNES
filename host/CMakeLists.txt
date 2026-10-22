# SDL2 desktop host. SDL2 detection and the "is SDL2 available?" decision live
# in the top-level CMakeLists.txt, which only adds this directory when the
# launcher and the host can be built.

add_executable(allnes
    main.cpp
    sdl_video.cpp
    sdl_audio.cpp
    sdl_input.cpp
    keybindings.cpp
)
target_link_libraries(allnes PRIVATE allnes_core allnes_launcher allnes_gamepad ${ALLNES_SDL2_LINK})

if(TARGET SDL2::SDL2main AND WIN32)
    target_link_libraries(allnes PRIVATE SDL2::SDL2main)
endif()

if(APPLE)
    target_link_libraries(allnes PRIVATE "-framework CoreFoundation")
endif()

add_executable(allnes_keybindings_test keybindings_test.cpp keybindings.cpp)
target_link_libraries(allnes_keybindings_test PRIVATE allnes_core ${ALLNES_SDL2_LINK})
add_test(NAME keybindings
         COMMAND allnes_keybindings_test "${CMAKE_SOURCE_DIR}/keybindings.conf")

install(TARGETS allnes RUNTIME DESTINATION bin)
install(FILES "${CMAKE_SOURCE_DIR}/keybindings.conf" DESTINATION bin)

# Bundled SDL controller mappings; Gamepads loads it from the executable
# directory (or share/allnes on Linux).
set(ALLNES_CONTROLLER_DB "${CMAKE_SOURCE_DIR}/gamepad/data/gamecontrollerdb.txt")
add_custom_command(TARGET allnes POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ALLNES_CONTROLLER_DB}" "$<TARGET_FILE_DIR:allnes>/gamecontrollerdb.txt")
install(FILES "${ALLNES_CONTROLLER_DB}" DESTINATION bin)
install(FILES "${CMAKE_SOURCE_DIR}/gamepad/data/LICENSE.SDL_GameControllerDB"
        DESTINATION share/doc/allnes)
