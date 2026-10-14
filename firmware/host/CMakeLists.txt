# Host (PC) build of DeskBuddy: unit tests, snapshot tool and the Windows simulator. No ESP32 or ESP-IDF needed.
# The core library is compiled from the firmware's own component sources, so the host and the device run the same
# behavior, art, layout and rendering code.
#   Windows: tools/host-test.ps1 (tests), tools/sim.ps1 (simulator), both with MSVC.
#   Elsewhere: cmake -S firmware/host -B build && cmake --build build && ctest --test-dir build --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(deskbuddy_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(HOST_SANITIZE "Build with AddressSanitizer (and UBSan where available)" ON)

set(COMPONENTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components)
set(SHARED_COMPONENTS gfx buddy layout deskbuddy_core)

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

# Shared with the firmware: exactly the components' own sources and headers.
set(CORE_SRCS)
set(CORE_INCLUDES)
foreach(component ${SHARED_COMPONENTS})
    file(GLOB srcs CONFIGURE_DEPENDS ${COMPONENTS_DIR}/${component}/src/*.c)
    list(APPEND CORE_SRCS ${srcs})
    list(APPEND CORE_INCLUDES ${COMPONENTS_DIR}/${component}/include)
endforeach()
add_library(deskbuddy_core STATIC ${CORE_SRCS})
target_include_directories(deskbuddy_core PUBLIC ${CORE_INCLUDES})
target_compile_options(deskbuddy_core PRIVATE ${WARNINGS})

add_library(host_common STATIC common/png.c)
target_include_directories(host_common PUBLIC common)
target_link_libraries(host_common PUBLIC deskbuddy_core)
target_compile_options(host_common PRIVATE ${WARNINGS})

file(GLOB TEST_SRCS CONFIGURE_DEPENDS tests/*.c)
add_executable(host_tests ${TEST_SRCS})
target_link_libraries(host_tests PRIVATE host_common)
target_compile_options(host_tests PRIVATE ${WARNINGS})

add_executable(buddy_snapshot tools/snapshot.c)
target_link_libraries(buddy_snapshot PRIVATE host_common)
target_compile_options(buddy_snapshot PRIVATE ${WARNINGS})

enable_testing()
add_test(NAME host_tests COMMAND host_tests)
