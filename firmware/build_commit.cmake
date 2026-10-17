# The Git commit a build comes from, for `info build=` (docs/protocol.md): the full 40-digit hash, with "-dirty"
# appended when tracked files had uncommitted changes; "unknown" without Git. Evaluated when CMake configures, like
# ESP-IDF's own version: tools/package-firmware.ps1 always configures a fresh build and checks the result against
# HEAD, so a stale value never reaches a firmware package. Used by the firmware (deskbuddy/main) and the simulator.
set(DESKBUDDY_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR})
function(deskbuddy_build_commit out_var)
    find_package(Git QUIET)
    set(commit unknown)
    if(GIT_FOUND)
        execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --verify HEAD
                        WORKING_DIRECTORY ${DESKBUDDY_SOURCE_DIR}
                        OUTPUT_VARIABLE head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE failed)
        if(NOT failed AND head MATCHES "^[0-9a-f]+$")
            set(commit ${head})
            execute_process(COMMAND ${GIT_EXECUTABLE} status --porcelain --untracked-files=no
                            WORKING_DIRECTORY ${DESKBUDDY_SOURCE_DIR}
                            OUTPUT_VARIABLE changes OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(changes)
                set(commit ${commit}-dirty)
            endif()
        endif()
    endif()
    set(${out_var} ${commit} PARENT_SCOPE)
endfunction()
