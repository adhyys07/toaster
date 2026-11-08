cmake_minimum_required(VERSION 3.21)
if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()
if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

project(emu VERSION 0.2.0 LANGUAGES C CXX)



include(GNUInstallDirs)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Build type; RelWithDebInfo keeps symbols and still optimizes" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY
                 STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

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
    systems/chip8/chip8_vm.cpp
    systems/gb/game_boy_system.cpp
    systems/gb/timer.cpp
    systems/gb/cartridge.cpp
    systems/gb/memory.cpp
    systems/gb/cpu.cpp
    systems/gb/ppu.cpp
    systems/gb/apu.cpp
    systems/wasm4/wasm4.cpp
    systems/wasm4/wasm4_system.cpp
    systems/wasm4/wasm3_runtime.cpp
    src/frontend/save_backup.cpp)
target_include_directories(emu_core PUBLIC src systems/nes systems/chip8 systems/gb)

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

add_executable(cheat_tests
    tests/test_cheats.cpp
    src/frontend/cheat_list.cpp)
target_include_directories(cheat_tests PRIVATE src/frontend)
add_test(NAME cheats COMMAND cheat_tests)
set_tests_properties(cheats PROPERTIES
                     WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(manifest_tests
    tests/test_core_manifest.cpp
    src/frontend/core_manifest.cpp)
target_include_directories(manifest_tests PRIVATE src/frontend)
add_test(NAME manifest COMMAND manifest_tests)
set_tests_properties(manifest PROPERTIES
                     WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(gb_tests systems/gb/test_gb.cpp)
target_link_libraries(gb_tests PRIVATE emu_core)
add_test(NAME gameboy COMMAND gb_tests)
set_tests_properties(gameboy PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

find_package(Threads REQUIRED)

find_package(CURL REQUIRED)

find_package(SDL2 REQUIRED)
add_executable(settings_tests
    tests/test_settings.cpp
    src/frontend/settings.cpp)
target_include_directories(settings_tests PRIVATE
    src/frontend systems/psp ${SDL2_INCLUDE_DIRS})
if(TARGET SDL2::SDL2)
    target_link_libraries(settings_tests PRIVATE SDL2::SDL2)
else()
    target_link_libraries(settings_tests PRIVATE ${SDL2_LIBRARIES})
endif()
add_test(NAME settings COMMAND settings_tests)

add_executable(emu
    src/frontend/main.cpp
    src/frontend/settings.cpp
    src/frontend/gl_fsr_upscaler.cpp
    src/frontend/gl_crt_shader.cpp
    src/frontend/ui_font.cpp
    src/frontend/bios_manager.cpp
    src/frontend/core/core_manager.cpp
    src/frontend/game_metadata.cpp
    src/frontend/artwork_cache.cpp
    src/frontend/cheat_list.cpp
    src/frontend/core_manifest.cpp
    src/frontend/screenshot.cpp
    src/frontend/save_state_manager.cpp
    src/frontend/crash_logger.cpp
    src/frontend/library_client.cpp
    src/frontend/game_library.cpp
    src/frontend/ui/text_cache.cpp
    src/frontend/ui/painter.cpp
    src/frontend/ui/layout.cpp
    src/frontend/ui/widgets.cpp
    src/frontend/ui/game_view.cpp
    src/frontend/ui/input_map.cpp
    src/frontend/ui/library_screen.cpp
    src/frontend/ui/perf_overlay.cpp
    systems/psp/libretro_host.cpp
    systems/psp/psp_system.cpp
    systems/ps2/ps2_system.cpp
    systems/ps1/ps1_system.cpp
    src/frontend/ui/theme.cpp
    src/frontend/ui/theme_io.cpp
    src/frontend/ui/theme_screen.cpp
    src/frontend/ui/controller_view.cpp
    src/frontend/ui/game_overlay.cpp
    src/frontend/ui/gallery.cpp
    src/frontend/gl_overlay.cpp
    src/frontend/hotkeys.cpp
    src/frontend/run_ahead.cpp
    src/frontend/movie.cpp
    src/frontend/palette_io.cpp
    src/frontend/memory_scan.cpp
    src/frontend/achievements.cpp
    src/frontend/save_sync.cpp
    src/frontend/discord_presence.cpp
    src/frontend/link_cable.cpp
    src/frontend/ui/big_picture.cpp
    src/frontend/audio_sync.cpp
    # save_backup.cpp lives in emu_core now: game_boy_system.cpp (in
    # emu_core) needs rotateBackups for its destructor, so it moved there
    # rather than being compiled twice.
    src/frontend/save_file.cpp
    src/frontend/log_buffer.cpp)
# Resolved here rather than right after project(): target_include_directories
# needs the emu target to already exist, and add_executable(emu ...) is above
# this line.
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE TOASTER_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()
if(NOT TOASTER_COMMIT)
    set(TOASTER_COMMIT "unknown")
endif()

string(TIMESTAMP TOASTER_BUILD_DATE "%Y-%m-%d" UTC)

configure_file(src/frontend/version.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h @ONLY)
target_include_directories(emu PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_include_directories(emu PRIVATE systems/psp systems/ps2 systems/ps1)
target_link_libraries(emu PRIVATE emu_core Threads::Threads CURL::libcurl)

# The link cable and the Discord IPC socket use the platform sockets
# directly; on Windows that means linking Winsock explicitly.
if(WIN32)
    target_link_libraries(emu PRIVATE ws2_32)
endif()

if(MINGW)
    target_link_options(emu PRIVATE -static-libgcc -static-libstdc++)
endif()

add_custom_command(TARGET emu POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:emu>/assets")

if(WIN32)
    set(TOASTER_INSTALL_BINDIR ".")
else()
    set(TOASTER_INSTALL_BINDIR "${CMAKE_INSTALL_BINDIR}")
endif()

install(DIRECTORY assets/shaders
        DESTINATION ${TOASTER_INSTALL_BINDIR}/assets)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
    install(DIRECTORY assets/fonts
            DESTINATION ${TOASTER_INSTALL_BINDIR}/assets)
endif()
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

if(WIN32)
    get_filename_component(TOASTER_TOOLCHAIN_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)

    install(TARGETS emu
            RUNTIME_DEPENDENCY_SET toaster_runtime
            RUNTIME DESTINATION ${TOASTER_INSTALL_BINDIR})

    install(RUNTIME_DEPENDENCY_SET toaster_runtime
            PRE_EXCLUDE_REGEXES
                "^api-ms-win-.*" "^ext-ms-.*"
            POST_EXCLUDE_REGEXES
                "[Ss]ystem32" "SysWOW64" "[Ww]indows[\\/]"
            DIRECTORIES
                "${TOASTER_TOOLCHAIN_BIN}"
            DESTINATION ${TOASTER_INSTALL_BINDIR})
else()
    install(TARGETS emu RUNTIME DESTINATION ${TOASTER_INSTALL_BINDIR})
endif()

if(WIN32)
    find_library(OPENGL32_LIBRARY NAMES opengl32)
    if(NOT OPENGL32_LIBRARY)
        message(FATAL_ERROR "Could not find the Windows OpenGL library (opengl32)")
    endif()
    target_link_libraries(emu PRIVATE "${OPENGL32_LIBRARY}")

    find_library(COMDLG32_LIBRARY NAMES comdlg32)
    if(NOT COMDLG32_LIBRARY)
        message(FATAL_ERROR "Could not find the Windows common-dialog library (comdlg32)")
    endif()
    target_link_libraries(emu PRIVATE "${COMDLG32_LIBRARY}")
    find_library(WS2_32_LIBRARY NAMES ws2_32)
    if(NOT WS2_32_LIBRARY)
        message(FATAL_ERROR "Could not find the Windows sockets library (ws2_32)")
    endif()
    target_link_libraries(emu PRIVATE "${WS2_32_LIBRARY}")
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(emu PRIVATE OpenGL::GL ${CMAKE_DL_LIBS})
    if(UNIX)
        target_link_libraries(emu PRIVATE Threads::Threads)
    endif()
endif()

find_package(SDL2_image REQUIRED)
set(SDL2_IMAGE_TARGET_USABLE FALSE)
if(TARGET SDL2_image::SDL2_image)
    get_target_property(SDL2_IMAGE_IMPLIB SDL2_image::SDL2_image IMPORTED_IMPLIB)
    if(NOT SDL2_IMAGE_IMPLIB OR EXISTS "${SDL2_IMAGE_IMPLIB}")
        set(SDL2_IMAGE_TARGET_USABLE TRUE)
    endif()
endif()

if(SDL2_IMAGE_TARGET_USABLE)
    target_link_libraries(emu PRIVATE SDL2_image::SDL2_image)
else()
    find_path(SDL2_IMAGE_INCLUDE_DIR SDL_image.h PATH_SUFFIXES SDL2)
    find_library(SDL2_IMAGE_LINK_LIBRARY
                 NAMES SDL2_image libSDL2_image.dll.a SDL2_image.dll)
    if(NOT SDL2_IMAGE_INCLUDE_DIR OR NOT SDL2_IMAGE_LINK_LIBRARY)
        message(FATAL_ERROR
                "SDL2_image was found, but its headers or library are missing")
    endif()
    target_include_directories(emu PRIVATE "${SDL2_IMAGE_INCLUDE_DIR}")
    target_link_libraries(emu PRIVATE "${SDL2_IMAGE_LINK_LIBRARY}")
endif()


find_package(SDL2_ttf REQUIRED)
if(TARGET SDL2_ttf::SDL2_ttf)
    target_link_libraries(emu PRIVATE SDL2_ttf::SDL2_ttf)
elseif(TARGET SDL2_ttf::SDL2_ttf-static)
    target_link_libraries(emu PRIVATE SDL2_ttf::SDL2_ttf-static)
else()
    find_path(SDL2_TTF_INCLUDE_DIR SDL_ttf.h PATH_SUFFIXES SDL2)
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

# ui_tests has to come after SDL2_ttf is found, because it links it: the
# filtering, fuzzy search, layout arithmetic and ellipsizing are all pure
# logic with no window, but measuring text still needs TTF.
# gl_overlay.cpp and gallery.cpp stay out: the first needs a live GL context
# and the second needs SDL_image, neither of which a headless test has.
add_executable(ui_tests
    tests/test_ui.cpp
    src/frontend/ui/game_view.cpp
    src/frontend/ui/layout.cpp
    src/frontend/ui/text_cache.cpp
    src/frontend/ui/theme.cpp
    src/frontend/ui/theme_io.cpp
    src/frontend/ui/controller_view.cpp
    src/frontend/ui/painter.cpp
    src/frontend/ui/widgets.cpp
    src/frontend/ui/input_map.cpp
    src/frontend/ui/game_overlay.cpp
    src/frontend/cheat_list.cpp
    src/frontend/hotkeys.cpp
    src/frontend/settings.cpp
    src/frontend/save_state_manager.cpp
    src/frontend/save_file.cpp
    src/frontend/save_backup.cpp
    src/frontend/log_buffer.cpp
    src/frontend/ui_font.cpp)
target_include_directories(ui_tests PRIVATE systems/psp)
target_include_directories(ui_tests PRIVATE src/frontend)
target_include_directories(ui_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

if(TARGET SDL2::SDL2)
    target_link_libraries(ui_tests PRIVATE SDL2::SDL2)
else()
    target_include_directories(ui_tests PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(ui_tests PRIVATE ${SDL2_LIBRARIES})
endif()

if(TARGET SDL2_ttf::SDL2_ttf)
    target_link_libraries(ui_tests PRIVATE SDL2_ttf::SDL2_ttf)
elseif(TARGET SDL2_ttf::SDL2_ttf-static)
    target_link_libraries(ui_tests PRIVATE SDL2_ttf::SDL2_ttf-static)
else()
    target_include_directories(ui_tests PRIVATE "${SDL2_TTF_INCLUDE_DIR}")
    target_link_libraries(ui_tests PRIVATE "${SDL2_TTF_LINK_LIBRARY}")
endif()

add_test(NAME ui COMMAND ui_tests)
set_tests_properties(ui PROPERTIES
                     WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")


# build/emu.exe has no DLLs beside it, so launching it from Explorer fails with
# "SDL2.dll was not found" before main() runs. Staging them next to the
# executable is not an option: the packaging scan searches the executable's own
# directory first, so each DLL would resolve from both there and the toolchain
# and the install fails with "Multiple conflicting paths found".
#
# A complete, double-clickable tree is assembled in build/run instead:
#     cmake --build build --target run-tree
#
# Running build/emu.exe directly still works from a shell that has the
# toolchain on PATH, which is the normal development case.
if(WIN32)
    add_custom_target(run-tree
        COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
                --prefix "${CMAKE_BINARY_DIR}/run"
        DEPENDS emu
        USES_TERMINAL
        COMMENT "Staging a runnable tree with every DLL in build/run")
endif()

set(CPACK_PACKAGE_NAME "toaster")
set(CPACK_PACKAGE_VENDOR "Toaster")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A small retro game emulator")
set(CPACK_PACKAGE_VERSION "0.5.0")
set(CPACK_GENERATOR "ZIP")
set(CPACK_VERBATIM_VARIABLES ON)

if(WIN32)
    set(CPACK_PACKAGE_FILE_NAME "toaster-windows-x64")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
endif()

include(CPack)
