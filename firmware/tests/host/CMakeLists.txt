# Host-side unit tests for the hardware-independent components (gfx, buddy, layout). No ESP32 or ESP-IDF needed.
# Windows: tools/host-test.ps1 (MSVC). Linux/macOS: cmake -S firmware/tests/host -B build && cmake --build build
#                                                    && ctest --test-dir build --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(deskbuddy_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(HOST_SANITIZE "Build with AddressSanitizer (and UBSan where available)" ON)

set(COMPONENTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../components)
# The same sources the firmware compiles.
file(GLOB CORE_SRCS CONFIGURE_DEPENDS
     ${COMPONENTS_DIR}/gfx/src/*.c
     ${COMPONENTS_DIR}/buddy/src/*.c
     ${COMPONENTS_DIR}/layout/src/*.c
     ${COMPONENTS_DIR}/office/src/*.c
     ${COMPONENTS_DIR}/link/src/*.c
     ${COMPONENTS_DIR}/marquee/src/*.c)

if(MSVC)
    set(WARNINGS /W4 /WX)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    if(HOST_SANITIZE)
        add_compile_options(/fsanitize=address)
    endif()
else()
    set(WARNINGS -Wall -Wextra -Werror)
    if(HOST_SANITIZE)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

add_library(deskbuddy_core STATIC ${CORE_SRCS})
target_include_directories(deskbuddy_core PUBLIC
    ${COMPONENTS_DIR}/gfx/include
    ${COMPONENTS_DIR}/buddy/include
    ${COMPONENTS_DIR}/layout/include
    ${COMPONENTS_DIR}/office/include
    ${COMPONENTS_DIR}/link/include
    ${COMPONENTS_DIR}/marquee/include)
target_compile_options(deskbuddy_core PRIVATE ${WARNINGS})
if(NOT MSVC)
    target_link_libraries(deskbuddy_core PUBLIC m)
endif()

add_executable(host_tests main.c test_util.c test_dirty.c test_scene.c test_repair.c test_anim.c test_buddy.c
               test_monster.c test_mood.c test_office.c test_link.c test_marquee.c)
target_link_libraries(host_tests PRIVATE deskbuddy_core)
target_compile_options(host_tests PRIVATE ${WARNINGS})

add_executable(buddy_snapshot snapshot.c png.c)
target_link_libraries(buddy_snapshot PRIVATE deskbuddy_core)
target_compile_options(buddy_snapshot PRIVATE ${WARNINGS})

# Buddy's artwork as PNGs and a Windows icon for the companion and other apps (tools/host-test.ps1 -Icons).
add_executable(buddy_icons icons.c png.c)
target_link_libraries(buddy_icons PRIVATE deskbuddy_core)
target_compile_options(buddy_icons PRIVATE ${WARNINGS})

enable_testing()
add_test(NAME host_tests COMMAND host_tests)
