cmake_minimum_required(VERSION 3.16)
project(nes LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

set(NES_CORE
    src/cpu.cpp
    src/ppu.cpp
    src/apu.cpp
    src/bus.cpp
    src/cartridge.cpp)

find_package(SDL2 REQUIRED)

add_executable(nes src/main.cpp ${NES_CORE})

if(TARGET SDL2::SDL2main)
    target_link_libraries(nes PRIVATE SDL2::SDL2main)   # must precede SDL2
endif()
if(TARGET SDL2::SDL2)
    target_link_libraries(nes PRIVATE SDL2::SDL2)
else()
    target_include_directories(nes PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(nes PRIVATE ${SDL2_LIBRARIES})
endif()

if(WIN32 AND TARGET SDL2::SDL2)
    add_custom_command(TARGET nes POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:nes>)
endif()

enable_testing()
add_executable(nes_tests src/test_nes.cpp ${NES_CORE})
add_test(NAME core
         COMMAND nes_tests
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})