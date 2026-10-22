# SDL game-controller wrapper shared by the launcher display layer and the
# desktop host: enumeration, hot-plug, the community mapping database and a
# generic Xbox-style fallback for third-party pads SDL does not recognise.

add_library(allnes_gamepad STATIC gamepad.cpp mapping.cpp)
target_include_directories(allnes_gamepad PUBLIC "${CMAKE_SOURCE_DIR}")
target_link_libraries(allnes_gamepad PUBLIC ${ALLNES_SDL2_LINK})

add_executable(allnes_gamepad_mapping_test test/mapping_test.cpp mapping.cpp)
target_include_directories(allnes_gamepad_mapping_test PRIVATE "${CMAKE_SOURCE_DIR}")
add_test(NAME gamepad_mapping COMMAND allnes_gamepad_mapping_test)
