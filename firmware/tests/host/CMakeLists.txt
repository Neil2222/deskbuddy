# Host-side unit tests for the hardware-independent components (gfx, buddy, layout, menu...), and the desktop
# simulator (Windows: the board's application in a window, firmware/simulator). No ESP32 or ESP-IDF.
# Windows: tools/host-test.ps1 (MSVC), tools/simulator.ps1. Linux/macOS (tests only): cmake -S firmware/tests/host
#   -B build && cmake --build build && ctest --test-dir build --output-on-failure
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
     ${COMPONENTS_DIR}/marquee/src/*.c
     ${COMPONENTS_DIR}/menu/src/*.c
     ${COMPONENTS_DIR}/pdc/src/*.c
     ${COMPONENTS_DIR}/ttt/src/*.c
     ${COMPONENTS_DIR}/missile/src/*.c
     ${COMPONENTS_DIR}/usage/src/*.c
     ${COMPONENTS_DIR}/app/src/*.c)

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
    ${COMPONENTS_DIR}/marquee/include
    ${COMPONENTS_DIR}/menu/include
    ${COMPONENTS_DIR}/pdc/include
    ${COMPONENTS_DIR}/ttt/include
    ${COMPONENTS_DIR}/missile/include
    ${COMPONENTS_DIR}/usage/include
    ${COMPONENTS_DIR}/app/include)
target_compile_options(deskbuddy_core PRIVATE ${WARNINGS})
if(NOT MSVC)
    target_link_libraries(deskbuddy_core PUBLIC m)
endif()

add_executable(host_tests main.c test_util.c test_dirty.c test_scene.c test_repair.c test_anim.c test_buddy.c
               test_monster.c test_mood.c test_office.c test_link.c test_marquee.c test_menu.c test_zoom.c
               test_activity.c test_ask.c test_pdc.c test_focus.c test_kitt.c test_app.c test_ttt.c test_game.c
               test_md.c test_md_view.c test_usage.c)
target_link_libraries(host_tests PRIVATE deskbuddy_core)
target_compile_options(host_tests PRIVATE ${WARNINGS})

add_executable(buddy_snapshot snapshot.c png.c)
target_link_libraries(buddy_snapshot PRIVATE deskbuddy_core)
target_compile_options(buddy_snapshot PRIVATE ${WARNINGS})

# Buddy's artwork as PNGs and a Windows icon for the companion and other apps (tools/host-test.ps1 -Icons).
add_executable(buddy_icons icons.c png.c)
target_link_libraries(buddy_icons PRIVATE deskbuddy_core)
target_compile_options(buddy_icons PRIVATE ${WARNINGS})
target_include_directories(buddy_icons PRIVATE ${COMPONENTS_DIR}/menu/src) # menu_art.h: the menu's icons

# The Mechanical Skeleton's artwork as firmware data (tools/host-test.ps1 -Assets writes
# components/office/src/office_skeleton_assets.c; the host tests check that file against the drawing code).
add_executable(skeleton_assets skeleton_assets.c)
target_link_libraries(skeleton_assets PRIVATE deskbuddy_core)
target_compile_options(skeleton_assets PRIVATE ${WARNINGS})

# The desktop simulator: components/app with firmware/simulator as its platform (app_port.h). Its version is the
# firmware's (deskbuddy/version.txt), its build the Git commit (build_commit.cmake), as on the board.
if(WIN32)
    file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../../deskbuddy/version.txt SIM_VERSION LIMIT_COUNT 1)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 ${CMAKE_CURRENT_SOURCE_DIR}/../../deskbuddy/version.txt)
    include(${CMAKE_CURRENT_SOURCE_DIR}/../../build_commit.cmake)
    deskbuddy_build_commit(SIM_BUILD_COMMIT)
    set(SIM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../simulator)
    add_executable(deskbuddy_sim ${SIM_DIR}/sim_main.c ${SIM_DIR}/sim_port.c)
    target_include_directories(deskbuddy_sim PRIVATE ${SIM_DIR})
    target_compile_definitions(deskbuddy_sim PRIVATE DESKBUDDY_SIM_VERSION="${SIM_VERSION}"
                               DESKBUDDY_BUILD_COMMIT="${SIM_BUILD_COMMIT}")
    target_link_libraries(deskbuddy_sim PRIVATE deskbuddy_core ws2_32 winmm)
    target_compile_options(deskbuddy_sim PRIVATE ${WARNINGS})
endif()

enable_testing()
add_test(NAME host_tests COMMAND host_tests)
