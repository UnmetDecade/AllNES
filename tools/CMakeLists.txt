add_executable(allnes_headless headless.cpp)
target_link_libraries(allnes_headless PRIVATE allnes_core)
target_compile_features(allnes_headless PRIVATE cxx_std_11)

# A tiny generated NROM ROM lets CI exercise the core without copyrighted games.
find_package(Python3 COMPONENTS Interpreter QUIET)

if(Python3_Interpreter_FOUND)
    set(TEST_ROM "${CMAKE_CURRENT_BINARY_DIR}/test_stripes.nes")
    add_custom_command(
        OUTPUT "${TEST_ROM}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/make_test_rom.py" "${TEST_ROM}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/make_test_rom.py"
        COMMENT "Generating test ROM"
        VERBATIM
    )
    add_custom_target(test_rom ALL DEPENDS "${TEST_ROM}")

    add_test(NAME headless_stripes
             COMMAND allnes_headless --rom "${TEST_ROM}" --frames 5 --check-stripes --repeat 2 --quiet)
    add_test(NAME headless_hash
             COMMAND allnes_headless --rom "${TEST_ROM}" --frames 3 --hash --quiet)
else()
    message(STATUS "Python3 not found - the generated test ROM is disabled")
endif()

install(TARGETS allnes_headless RUNTIME DESTINATION bin)
