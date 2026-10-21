cmake_minimum_required(VERSION 3.24)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" REVERSI_VERSION
    LIMIT_COUNT 1)
if(NOT REVERSI_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR
        "VERSION must contain one semantic version such as 1.0.0")
endif()

project(Reversi
    VERSION ${REVERSI_VERSION}
    DESCRIPTION "A polished Reversi (Othello) desktop game written in ISO C11."
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(REVERSI_GIT_COMMIT "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE REVERSI_GIT_COMMIT
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE REVERSI_GIT_RESULT)
    if(NOT REVERSI_GIT_RESULT EQUAL 0)
        set(REVERSI_GIT_COMMIT "unknown")
    endif()
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
    "${CMAKE_BINARY_DIR}/generated/reversi/version.h"
    @ONLY)

if(MSVC)
    # Static CRT keeps the Windows package self-contained.
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING
        "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

option(REVERSI_BUILD_TESTS "Build the automated test suite" ON)
option(REVERSI_ENABLE_GUI_TESTS
    "Register the window smoke test with CTest (requires a display)" OFF)
option(REVERSI_ENABLE_ASAN "Build project sources with AddressSanitizer" OFF)
option(REVERSI_ENABLE_UBSAN "Build project sources with UndefinedBehaviorSanitizer" OFF)
option(REVERSI_FORCE_FETCH_RAYLIB "Ignore a system raylib and fetch the pinned version" OFF)

set(REVERSI_RAYLIB_VERSION "6.2")
set(REVERSI_RAYLIB_URL
    "https://github.com/raysan5/raylib/archive/refs/tags/${REVERSI_RAYLIB_VERSION}.tar.gz"
    CACHE STRING "URL of the pinned raylib source archive")
set(REVERSI_RAYLIB_SHA256
    "df7a13129b1bec9e73a6962efff170beba310ee6eab8f94cd000b7316b4db5ee"
    CACHE STRING "SHA256 of the pinned raylib source archive")

# ---------------------------------------------------------------------------
# raylib: prefer a system installation, otherwise fetch the pinned release.
# ---------------------------------------------------------------------------
function(reversi_resolve_raylib_target out_var)
    if(TARGET raylib)
        set(${out_var} "raylib" PARENT_SCOPE)
    elseif(TARGET raylib::raylib)
        set(${out_var} "raylib::raylib" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()

set(REVERSI_RAYLIB_TARGET "")

if(NOT REVERSI_FORCE_FETCH_RAYLIB)
    find_package(raylib ${REVERSI_RAYLIB_VERSION} QUIET)
    if(raylib_FOUND)
        reversi_resolve_raylib_target(REVERSI_RAYLIB_TARGET)

        if(REVERSI_RAYLIB_TARGET)
            if(raylib_VERSION)
                message(STATUS "Reversi: using system raylib ${raylib_VERSION}")
            else()
                message(STATUS "Reversi: using system raylib")
            endif()
        else()
            message(WARNING
                "Reversi: raylib package found but no usable CMake target; "
                "falling back to FetchContent")
        endif()
    endif()
endif()

if(NOT REVERSI_RAYLIB_TARGET)
    include(FetchContent)

    set(BUILD_EXAMPLES OFF CACHE BOOL "Build raylib examples" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "Do not install GLFW with the game" FORCE)

    FetchContent_Declare(raylib
        URL "${REVERSI_RAYLIB_URL}"
        URL_HASH "SHA256=${REVERSI_RAYLIB_SHA256}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

    FetchContent_MakeAvailable(raylib)

    reversi_resolve_raylib_target(REVERSI_RAYLIB_TARGET)

    if(NOT REVERSI_RAYLIB_TARGET)
        message(FATAL_ERROR "raylib was fetched but did not define a usable target")
    endif()

    message(STATUS "Reversi: using fetched raylib ${REVERSI_RAYLIB_VERSION}")
endif()

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Optional sanitizers. Applied after raylib is configured so third-party
# sources are not instrumented by these flags.
# ---------------------------------------------------------------------------
set(REVERSI_SANITIZER_FLAGS "")
if(REVERSI_ENABLE_ASAN AND REVERSI_ENABLE_UBSAN)
    set(REVERSI_SANITIZER_FLAGS "address,undefined")
elseif(REVERSI_ENABLE_ASAN)
    set(REVERSI_SANITIZER_FLAGS "address")
elseif(REVERSI_ENABLE_UBSAN)
    set(REVERSI_SANITIZER_FLAGS "undefined")
endif()

if(REVERSI_SANITIZER_FLAGS)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(
            "-fsanitize=${REVERSI_SANITIZER_FLAGS}"
            -fno-omit-frame-pointer)
        add_link_options("-fsanitize=${REVERSI_SANITIZER_FLAGS}")
        message(STATUS "Reversi: sanitizers enabled (${REVERSI_SANITIZER_FLAGS})")
    else()
        message(WARNING
            "Reversi: REVERSI_ENABLE_ASAN/UBSAN support only Clang and GCC; "
            "the options are ignored for this compiler")
    endif()
endif()

# ---------------------------------------------------------------------------
# Warning policy for first-party code only.
# ---------------------------------------------------------------------------
add_library(reversi_warnings INTERFACE)
if(MSVC)
    # MSVC still gates C11 <stdatomic.h> behind this switch.
    target_compile_options(reversi_warnings INTERFACE
        /W4
        /experimental:c11atomics)
else()
    target_compile_options(reversi_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wstrict-prototypes
        -Wmissing-prototypes)
endif()

# ---------------------------------------------------------------------------
# Project targets. Dependencies point downwards only:
#   main -> app -> {ui, ai, persistence, audio}
#   ui -> raylib; audio -> raylib; persistence -> core
# ---------------------------------------------------------------------------
add_library(reversi_core STATIC
    src/core/board.c
    src/core/rules.c
    src/core/game.c
    src/core/history.c
    src/core/match.c)
target_include_directories(reversi_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(reversi_core PRIVATE reversi_warnings)

add_library(reversi_ai STATIC
    src/ai/ai.c
    src/ai/ai_eval.c
    src/ai/ai_search.c
    src/ai/ai_worker.c)
target_include_directories(reversi_ai PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(reversi_ai
    PUBLIC reversi_core
    PRIVATE reversi_warnings Threads::Threads)

add_library(reversi_persistence STATIC
    src/storage/storage.c
    src/storage/text.c
    src/storage/save.c
    src/storage/stats.c
    src/settings/settings.c)
target_include_directories(reversi_persistence PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(reversi_persistence
    PUBLIC reversi_core
    PRIVATE reversi_warnings)

add_library(reversi_audio STATIC
    src/audio/audio.c)
target_include_directories(reversi_audio PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(reversi_audio
    PUBLIC ${REVERSI_RAYLIB_TARGET}
    PRIVATE reversi_warnings)
if(UNIX AND NOT APPLE)
    target_link_libraries(reversi_audio PUBLIC m)
endif()

add_library(reversi_ui STATIC
    src/ui/animation.c
    src/ui/board_view.c
    src/ui/draw.c
    src/ui/fonts.c
    src/ui/game_view.c
    src/ui/help_view.c
    src/ui/history_view.c
    src/ui/menu_view.c
    src/ui/modal.c
    src/ui/pause_view.c
    src/ui/result_view.c
    src/ui/saves_view.c
    src/ui/settings_view.c
    src/ui/setup_view.c
    src/ui/stats_view.c
    src/ui/theme.c)
target_include_directories(reversi_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(reversi_ui
    PUBLIC ${REVERSI_RAYLIB_TARGET}
    PRIVATE reversi_warnings)
if(UNIX AND NOT APPLE)
    target_link_libraries(reversi_ui PUBLIC m)
endif()

add_library(reversi_app STATIC
    src/app/app.c)
target_include_directories(reversi_app PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(reversi_app
    PUBLIC reversi_core reversi_ui reversi_ai reversi_persistence
           reversi_audio
    PRIVATE reversi_warnings)

add_executable(reversi src/main.c)
target_link_libraries(reversi PRIVATE reversi_app reversi_warnings)
target_include_directories(reversi PRIVATE
    "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(reversi PRIVATE
    REVERSI_BUILD_TYPE="$<CONFIG>")
set_target_properties(reversi PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(APPLE)
    set_target_properties(reversi PROPERTIES
        OUTPUT_NAME "Reversi"
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Reversi"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.lightyourjourney.reversi"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT
            "Copyright (C) 2026 Yanjin Li. GPL-3.0."
        MACOSX_BUNDLE_ICON_FILE "Reversi.icns"
        MACOSX_BUNDLE_INFO_PLIST
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in")
    target_sources(reversi PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/icons/Reversi.icns")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/icons/Reversi.icns"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set(REVERSI_ASSET_DESTINATION
        "$<TARGET_BUNDLE_CONTENT_DIR:reversi>/Resources/assets")
else()
    set(REVERSI_ASSET_DESTINATION "$<TARGET_FILE_DIR:reversi>/assets")
endif()

if(WIN32)
    # Forward slashes: backslashes in the .rc string would be read as C escapes.
    set(REVERSI_ICON_ICO
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/icons/reversi.ico")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/version.rc.in"
        "${CMAKE_BINARY_DIR}/generated/version.rc"
        @ONLY)
    target_sources(reversi PRIVATE "${CMAKE_BINARY_DIR}/generated/version.rc")
endif()

# Assets are copied into the application directory (or the macOS bundle's
# Resources directory) so the game never depends on the source tree or the
# caller's working directory at runtime.
add_custom_command(TARGET reversi POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "${REVERSI_ASSET_DESTINATION}"
    COMMENT "Copying assets into the application directory")

if(APPLE)
    add_custom_command(TARGET reversi POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
            "$<TARGET_BUNDLE_CONTENT_DIR:reversi>/Resources"
        COMMENT "Copying licenses into Reversi.app")

    # Re-seal the bundle after adding Resources. The default ad-hoc signature
    # keeps local builds runnable; a Developer ID can be supplied for release.
    set(REVERSI_MACOS_CODESIGN_IDENTITY "-" CACHE STRING
        "macOS codesign identity ('-' for ad-hoc)")
    set(REVERSI_MACOS_CODESIGN_FLAGS "")
    if(NOT REVERSI_MACOS_CODESIGN_IDENTITY STREQUAL "-")
        list(APPEND REVERSI_MACOS_CODESIGN_FLAGS
            --options runtime --timestamp)
    endif()
    add_custom_command(TARGET reversi POST_BUILD
        COMMAND codesign --force --deep ${REVERSI_MACOS_CODESIGN_FLAGS}
            --sign "${REVERSI_MACOS_CODESIGN_IDENTITY}"
            "$<TARGET_BUNDLE_DIR:reversi>"
        COMMENT "Codesigning Reversi.app")
endif()

# A runnable distribution tree: application + assets + licenses + docs.
install(TARGETS reversi
    BUNDLE DESTINATION . COMPONENT Reversi
    RUNTIME DESTINATION . COMPONENT Reversi)
if(NOT APPLE)
    install(DIRECTORY assets DESTINATION . COMPONENT Reversi)
endif()
install(DIRECTORY docs DESTINATION . COMPONENT Reversi)
install(FILES LICENSE THIRD_PARTY_NOTICES.md README.md CHANGELOG.md VERSION
        DESTINATION . COMPONENT Reversi)

# ---------------------------------------------------------------------------
# Tests. Core tests are GUI-free and link only reversi_core; the optional
# app.smoke test opens a window and therefore requires a display.
# ---------------------------------------------------------------------------
function(reversi_add_core_test target source test_name)
    add_executable(${target} ${source})
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(${target} PRIVATE reversi_core reversi_warnings)

    add_test(NAME ${test_name} COMMAND ${target})
    set_tests_properties(${test_name} PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endfunction()

function(reversi_add_ui_test target source test_name)
    add_executable(${target} ${source})
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(${target} PRIVATE
        reversi_ui reversi_core reversi_warnings)

    add_test(NAME ${test_name} COMMAND ${target})
    set_tests_properties(${test_name} PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endfunction()

function(reversi_add_persistence_test target source test_name)
    add_executable(${target} ${source})
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(${target} PRIVATE
        reversi_persistence reversi_warnings)

    add_test(NAME ${test_name} COMMAND ${target})
    set_tests_properties(${test_name} PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endfunction()

function(reversi_add_audio_test target source test_name)
    add_executable(${target} ${source})
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(${target} PRIVATE
        reversi_audio reversi_warnings)

    add_test(NAME ${test_name} COMMAND ${target})
    set_tests_properties(${test_name} PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endfunction()

function(reversi_add_ai_test target source test_name)
    add_executable(${target} ${source})
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(${target} PRIVATE
        reversi_ai reversi_warnings Threads::Threads)

    add_test(NAME ${test_name} COMMAND ${target})
    set_tests_properties(${test_name} PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endfunction()

if(REVERSI_BUILD_TESTS)
    enable_testing()

    reversi_add_core_test(reversi_tests tests/test_board.c core.board)
    reversi_add_core_test(reversi_rules_tests tests/test_rules.c core.rules)
    reversi_add_core_test(reversi_game_tests tests/test_game.c core.game)
    reversi_add_core_test(reversi_random_tests tests/test_random_games.c
        core.random)
    reversi_add_core_test(reversi_history_tests tests/test_history.c
        core.history)
    reversi_add_core_test(reversi_match_tests tests/test_match.c
        core.match)
    reversi_add_ai_test(reversi_ai_tests tests/test_ai.c ai.core)
    reversi_add_ai_test(reversi_ai_worker_tests tests/test_ai_worker.c
        ai.worker)
    reversi_add_persistence_test(reversi_save_tests tests/test_save.c
        storage.save)
    reversi_add_persistence_test(reversi_settings_tests tests/test_settings.c
        storage.settings)
    reversi_add_persistence_test(reversi_stats_tests tests/test_stats.c
        storage.stats)
    reversi_add_audio_test(reversi_audio_tests tests/test_audio.c
        audio.noop)

    # UI logic tests use synthetic input and create no window.
    reversi_add_ui_test(reversi_ui_button_tests
        tests/test_ui_button.c ui.button)
    reversi_add_ui_test(reversi_ui_helper_tests
        tests/test_ui_helpers.c ui.helpers)
    reversi_add_ui_test(reversi_ui_views_tests
        tests/test_ui_views.c ui.views)

    # Benchmark tool: not registered with CTest (it runs many full games).
    add_executable(reversi_ai_bench tests/bench_ai.c)
    target_include_directories(reversi_ai_bench PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(reversi_ai_bench PRIVATE
        reversi_ai reversi_warnings)
    set_target_properties(reversi_ai_bench PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    if(REVERSI_ENABLE_GUI_TESTS)
        add_test(NAME app.smoke COMMAND reversi --frames 2)
        set_tests_properties(app.smoke PROPERTIES
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            ENVIRONMENT
                "HOME=${CMAKE_BINARY_DIR}/test-home;XDG_DATA_HOME=${CMAKE_BINARY_DIR}/test-home;APPDATA=${CMAKE_BINARY_DIR}/test-home"
            LABELS "gui"
            TIMEOUT 30)

        # Debug-only autoplay hook: one completed game, one stats record,
        # terminal autosave removed. Requires a display like app.smoke.
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            add_test(NAME app.autoplay
                COMMAND "${CMAKE_COMMAND}"
                    "-DREVERSI=$<TARGET_FILE:reversi>"
                    "-DTEST_HOME=${CMAKE_BINARY_DIR}/autoplay-test-home"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/app_autoplay_test.cmake")
            set_tests_properties(app.autoplay PROPERTIES
                WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
                LABELS "gui"
                TIMEOUT 120)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Release packaging. ZIP is available on every supported platform; Linux also
# produces a tar.gz. CPack writes a .sha256 file next to each archive.
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "Reversi")
set(CPACK_PACKAGE_VENDOR "Yanjin Li")
set(CPACK_PACKAGE_CONTACT "Yanjin Li")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Reversi")
set(CPACK_PACKAGE_FILE_NAME
    "Reversi-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
set(CPACK_INSTALL_CMAKE_PROJECTS
    "${CMAKE_BINARY_DIR};Reversi;Reversi;/")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
if(NOT MSVC)
    set(CPACK_STRIP_FILES TRUE)
endif()

if(WIN32 OR APPLE)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "ZIP;TGZ")
endif()

include(CPack)
