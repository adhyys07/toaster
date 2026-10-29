cmake_minimum_required(VERSION 3.16)
project(emu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(emu_core
    src/core/registry.cpp
    systems/nes/nes_system.cpp
    systems/nes/cpu.cpp
    systems/nes/ppu.cpp
    systems/nes/apu.cpp
    systems/nes/bus.cpp
    systems/nes/cartridge.cpp
    systems/chip8/chip8.cpp
    systems/chip8/chip8_vm.cpp)
target_include_directories(emu_core PUBLIC src systems/nes systems/chip8)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(EMU_TEST_ROMS "${CMAKE_CURRENT_BINARY_DIR}/roms")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkroms.py" "${EMU_TEST_ROMS}"
    RESULT_VARIABLE ROM_GENERATION_RESULT
    OUTPUT_QUIET)
if(NOT ROM_GENERATION_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate test ROMs")
endif()

enable_testing()
add_executable(core_tests systems/test_core.cpp)
target_link_libraries(core_tests PRIVATE emu_core)
add_test(NAME core COMMAND core_tests)
set_tests_properties(core PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(binding_tests systems/test_bindings.cpp)
target_link_libraries(binding_tests PRIVATE emu_core)
add_test(NAME bindings COMMAND binding_tests)
set_tests_properties(bindings PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
