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
