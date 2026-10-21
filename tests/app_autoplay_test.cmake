# Runs the Debug autoplay hook and verifies one completed game is persisted
# exactly once, with no terminal autosave left behind.
if(NOT DEFINED REVERSI OR NOT DEFINED TEST_HOME)
    message(FATAL_ERROR "REVERSI and TEST_HOME are required")
endif()

file(REMOVE_RECURSE "${TEST_HOME}")
file(MAKE_DIRECTORY "${TEST_HOME}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "HOME=${TEST_HOME}"
        "XDG_DATA_HOME=${TEST_HOME}"
        "APPDATA=${TEST_HOME}"
        "REVERSI_AUTOPLAY=7"
        "${REVERSI}" --frames 600
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_QUIET
    TIMEOUT 90)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "autoplay exited with ${result}")
endif()

set(stats_files
    "${TEST_HOME}/Library/Application Support/Reversi/stats.dat"
    "${TEST_HOME}/reversi/stats.dat"
    "${TEST_HOME}/Reversi/stats.dat")
set(stats_file "")
foreach(candidate IN LISTS stats_files)
    if(EXISTS "${candidate}")
        set(stats_file "${candidate}")
        break()
    endif()
endforeach()

if(stats_file STREQUAL "")
    message(FATAL_ERROR "autoplay did not write stats.dat")
endif()

file(READ "${stats_file}" stats)
string(REGEX MATCH "pvp_games ([0-9]+)" pvp_record "${stats}")
if(NOT pvp_record STREQUAL "pvp_games 1")
    message(FATAL_ERROR
        "completed game was not recorded exactly once:\n${stats}")
endif()

get_filename_component(data_dir "${stats_file}" DIRECTORY)
if(EXISTS "${data_dir}/autosave.rvs")
    message(FATAL_ERROR "terminal autosave was not removed")
endif()
