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

find_package(SDL2 REQUIRED)
add_executable(emu src/frontend/main.cpp)
target_link_libraries(emu PRIVATE emu_core)

if(WIN32)
    # Resolve the platform's actual library file. This works with both
    # MSVC's comdlg32.lib and MinGW's libcomdlg32.a.
    find_library(COMDLG32_LIBRARY NAMES comdlg32)
    if(NOT COMDLG32_LIBRARY)
        message(FATAL_ERROR "Could not find the Windows common-dialog library (comdlg32)")
    endif()
    target_link_libraries(emu PRIVATE "${COMDLG32_LIBRARY}")
endif()

find_package(SDL2_ttf REQUIRED)
if(TARGET SDL2_ttf::SDL2_ttf)
    target_link_libraries(emu PRIVATE SDL2_ttf::SDL2_ttf)
elseif(TARGET SDL2_ttf::SDL2_ttf-static)
    target_link_libraries(emu PRIVATE SDL2_ttf::SDL2_ttf-static)
else()
    find_path(SDL2_TTF_INCLUDE_DIR SDL_ttf.h PATH_SUFFIXES SDL2)
    # Prefer MinGW's DLL import library. The static archive also needs
    # FreeType and HarfBuzz to be linked explicitly.
    find_library(SDL2_TTF_LINK_LIBRARY NAMES SDL2_ttf.dll SDL2_ttf)
    if(NOT SDL2_TTF_INCLUDE_DIR OR NOT SDL2_TTF_LINK_LIBRARY)
        message(FATAL_ERROR "SDL2_ttf was found, but its headers or library are missing")
    endif()
    target_include_directories(emu PRIVATE "${SDL2_TTF_INCLUDE_DIR}")
    target_link_libraries(emu PRIVATE "${SDL2_TTF_LINK_LIBRARY}")
endif()

if(TARGET SDL2::SDL2)
    target_link_libraries(emu PRIVATE SDL2::SDL2)
else()
    target_include_directories(emu PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(emu PRIVATE ${SDL2_LIBRARIES})
endif()

if(WIN32 AND TARGET SDL2::SDL2)
    add_custom_command(TARGET emu POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:emu>)
endif()

if(WIN32 AND SDL2_TTF_LINK_LIBRARY)
    get_filename_component(SDL2_TTF_LIB_DIR "${SDL2_TTF_LINK_LIBRARY}" DIRECTORY)
    get_filename_component(SDL2_TTF_PREFIX "${SDL2_TTF_LIB_DIR}/.." ABSOLUTE)
    set(SDL2_TTF_RUNTIME_DLL "${SDL2_TTF_PREFIX}/bin/SDL2_ttf.dll")
    if(EXISTS "${SDL2_TTF_RUNTIME_DLL}")
        add_custom_command(TARGET emu POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${SDL2_TTF_RUNTIME_DLL}" $<TARGET_FILE_DIR:emu>)
    endif()
endif()
