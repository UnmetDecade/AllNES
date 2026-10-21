# Platform-independent NES core.
#
# Layering rules:
#   - no platform headers here (no SDL, no OS APIs)
#   - everything the core needs from the outside lives in include/port/
#   - hosts inject their own implementations of those ports
file(GLOB ALLNES_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/APU/*.cpp"
)

add_library(allnes_core STATIC ${ALLNES_CORE_SOURCES})
target_include_directories(allnes_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(allnes_core PUBLIC cxx_std_17)
