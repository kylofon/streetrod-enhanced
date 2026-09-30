cmake_minimum_required(VERSION 3.24)
project(streetrod_enhanced VERSION 0.2.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Street Rod Enhanced: the faithful port of SR.EXE (VGA 16-colour mode 0Dh, PC speaker; ENGINE.md) with
# the enhancements in src/enhanced (ENHANCED.md).

find_package(SDL3 REQUIRED CONFIG)

file(GLOB_RECURSE SR_SOURCES CONFIGURE_DEPENDS src/*.c)
file(GLOB_RECURSE SR_ENHANCED_SOURCES CONFIGURE_DEPENDS src/enhanced/*.c)

add_executable(streetrod-enhanced ${SR_SOURCES})
target_include_directories(streetrod-enhanced PRIVATE src)
target_link_libraries(streetrod-enhanced PRIVATE SDL3::SDL3)
if(MSVC)
    target_compile_options(streetrod-enhanced PRIVATE /W3)
else()
    # The memory model reads multi-byte values through byte pointers (see src/mem.h).
    target_compile_options(streetrod-enhanced PRIVATE -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(streetrod-enhanced PRIVATE -Wno-format-truncation)
    endif()
    # The enhanced renderer does per-pixel work every frame; keep it optimised in Debug builds.
    if(SR_ENHANCED_SOURCES)
        set_source_files_properties(${SR_ENHANCED_SOURCES} PROPERTIES COMPILE_OPTIONS -O2)
    endif()
endif()
if(WIN32)
    add_custom_command(TARGET streetrod-enhanced POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:streetrod-enhanced>
                $<TARGET_FILE_DIR:streetrod-enhanced>
        COMMAND_EXPAND_LISTS)
    # And what those load in turn (MSYS2's SDL3.dll needs libiconv-2.dll), so the game starts outside
    # the MSYS2 shell, e.g. from the launcher.
    get_filename_component(SR_DLL_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
    add_custom_command(TARGET streetrod-enhanced POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:streetrod-enhanced> "-DSEARCH=${SR_DLL_DIR}"
                -DCMAKE_OBJDUMP=${CMAKE_OBJDUMP} -P ${CMAKE_CURRENT_SOURCE_DIR}/launcher/copy_dlls.cmake)
endif()

# The launcher ("Street Rod Enhanced"): the game folder, the original or the data disk's cars, the
# game's own switches and the display options; built beside streetrod-enhanced.exe. It needs
# wxWidgets 3.2 (MSYS2: mingw-w64-x86_64-wxwidgets3.2-msw) and a C++17 compiler.
option(SRE_LAUNCHER "Also build the launcher, Street Rod Enhanced.exe (wxWidgets)" ON)
if(SRE_LAUNCHER)
    add_subdirectory(launcher)
endif()
