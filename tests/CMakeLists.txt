cmake_minimum_required(VERSION 3.26)
project(SoHValidation LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

if(NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Validation configuration")
endif()

get_filename_component(SOH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(SOH_GAME_BUILD_DIR "${SOH_ROOT}/build/x64-v143" CACHE PATH "Existing game build (dependency headers only)")
find_path(SOH_JSON_INCLUDE_DIR nlohmann/json.hpp HINTS
    "$ENV{VCPKG_ROOT}/installed/x64-windows-static/include"
    "$ENV{USERPROFILE}/vcpkg/installed/x64-windows-static/include"
    "${SOH_GAME_BUILD_DIR}/_deps/json-src/include")
find_path(SOH_IMGUI_INCLUDE_DIR imgui.h HINTS
    "${SOH_GAME_BUILD_DIR}/_deps/imgui-src"
    "${SOH_ROOT}/build-cmake/_deps/imgui-src"
    "${SOH_ROOT}/build-windows/_deps/imgui-src")
if(NOT SOH_JSON_INCLUDE_DIR OR NOT SOH_IMGUI_INCLUDE_DIR)
    message(FATAL_ERROR "Validation requires existing nlohmann-json and game ImGui headers. Set SOH_JSON_INCLUDE_DIR / SOH_IMGUI_INCLUDE_DIR or prepare game dependencies first. Nothing is downloaded by this project.")
endif()

function(validation_test name source)
    add_executable(${name} "${source}")
    target_include_directories(${name} PRIVATE "${SOH_ROOT}" "${SOH_JSON_INCLUDE_DIR}" "${SOH_IMGUI_INCLUDE_DIR}")
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "fast" TIMEOUT 20)
endfunction()
validation_test(calibration map_tracker_calibration_test.cpp)
validation_test(display_modes map_tracker_modes_test.cpp)
validation_test(check_totals map_tracker_totals_test.cpp)
validation_test(viewport_ownership map_tracker_viewport_test.cpp)
validation_test(checks_active checks_active_test.cpp)
validation_test(archipelago_session archipelago_session_test.cpp)
validation_test(pack_schema map_tracker_pack_schema_test.cpp)
set_tests_properties(archipelago_session pack_schema PROPERTIES LABELS "integration")
set_tests_properties(checks_active PROPERTIES WILL_FAIL TRUE)
if(WIN32)
    validation_test(window_shutdown windows_window_shutdown_test.cpp)
    target_link_libraries(window_shutdown PRIVATE user32)
endif()

option(SOH_VALIDATION_NETWORK "Compile the real AP transport integration probe (offline dependencies only)" OFF)
if(SOH_VALIDATION_NETWORK)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    foreach(dependency IN ITEMS apclientpp wswrap asio websocketpp valijson)
        if(NOT IS_DIRECTORY "${SOH_GAME_BUILD_DIR}/_deps/${dependency}-src")
            message(FATAL_ERROR "Missing existing ${dependency} dependency. Prepare game dependencies; validation never downloads them.")
        endif()
    endforeach()
    add_executable(ap_transport_probe ap_transport_probe.cpp)
    target_include_directories(ap_transport_probe PRIVATE "${SOH_ROOT}" "${SOH_JSON_INCLUDE_DIR}"
        "${SOH_GAME_BUILD_DIR}/_deps/apclientpp-src" "${SOH_GAME_BUILD_DIR}/_deps/wswrap-src/include"
        "${SOH_GAME_BUILD_DIR}/_deps/asio-src/asio/include" "${SOH_GAME_BUILD_DIR}/_deps/websocketpp-src"
        "${SOH_GAME_BUILD_DIR}/_deps/valijson-src/include")
    target_compile_definitions(ap_transport_probe PRIVATE ASIO_STANDALONE WSWRAP_NO_SSL WSWRAP_NO_COMPRESSION)
    if(WIN32)
        target_compile_definitions(ap_transport_probe PRIVATE _WIN32_WINNT=0x0601)
        target_link_libraries(ap_transport_probe PRIVATE ws2_32)
    endif()
    if(MSVC)
        target_compile_options(ap_transport_probe PRIVATE /Zc:__cplusplus /bigobj)
    endif()
    add_test(NAME ap_transport COMMAND "${Python3_EXECUTABLE}" "${SOH_ROOT}/scripts/tests/run_ap_transport.py"
        "$<TARGET_FILE:ap_transport_probe>")
    set_tests_properties(ap_transport PROPERTIES LABELS "integration" TIMEOUT 50)
endif()
