# SDL game-controller wrapper shared by the launcher display layer and the
# desktop host: enumeration, hot-plug and a generic Xbox-style mapping fallback
# for third-party pads SDL does not recognise out of the box.

add_library(allnes_gamepad STATIC gamepad.cpp)
target_include_directories(allnes_gamepad PUBLIC "${CMAKE_SOURCE_DIR}")
target_link_libraries(allnes_gamepad PUBLIC ${ALLNES_SDL2_LINK})
