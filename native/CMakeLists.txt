cmake_minimum_required(VERSION 3.22)
project(android_rigctld LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/hamlib-version" HAMLIB_REVISION LIMIT_COUNT 1)
set(HAMLIB_ROOT "" CACHE PATH "Override the prepared Hamlib tree")
set(HAMLIB_CACHE "${CMAKE_CURRENT_LIST_DIR}/../.work/hamlib")
if(DEFINED ENV{HAMLIB_WORK_DIR} AND NOT "$ENV{HAMLIB_WORK_DIR}" STREQUAL "")
    set(HAMLIB_ROOT "$ENV{HAMLIB_WORK_DIR}")
elseif(NOT HAMLIB_ROOT OR HAMLIB_ROOT STREQUAL HAMLIB_CACHE)
    set(HAMLIB_ROOT "${HAMLIB_CACHE}/${HAMLIB_REVISION}")
endif()
set(HAMLIB_SOURCE "${HAMLIB_ROOT}/source")
if(ANDROID)
    set(HAMLIB_BUILD "${HAMLIB_ROOT}/build-android-arm64")
else()
    set(HAMLIB_BUILD "${HAMLIB_ROOT}/build-host")
endif()

if(NOT EXISTS "${HAMLIB_BUILD}/src/.libs/libhamlib.a")
    message(FATAL_ERROR "Run scripts/build-hamlib.sh for this target before building the server")
endif()

find_package(Threads REQUIRED)
add_library(rigctld_core STATIC
    server.cpp
    tx_stream.cpp
    embedded_audio.c
    embedded_stream.c
    "${HAMLIB_SOURCE}/tests/rigctl_parse.c"
    "${HAMLIB_SOURCE}/tests/rigctld_stream.c"
    "${HAMLIB_SOURCE}/tests/rigctld_client.c"
    "${HAMLIB_SOURCE}/tests/daemon_client.c"
    "${HAMLIB_SOURCE}/tests/dumpcaps.c"
    "${HAMLIB_SOURCE}/tests/dumpstate.c"
    "${HAMLIB_SOURCE}/tests/rig_tests.c")
set_target_properties(rigctld_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(rigctld_core PRIVATE
    "${HAMLIB_BUILD}/include/hamlib"
    "${HAMLIB_SOURCE}/include"
    "${HAMLIB_SOURCE}/include/hamlib"
    "${HAMLIB_SOURCE}/src"
    "${HAMLIB_SOURCE}/tests"
    "${HAMLIB_SOURCE}/lib"
    "${HAMLIB_SOURCE}/security"
    "${HAMLIB_BUILD}/include")
target_compile_definitions(rigctld_core PRIVATE IN_HAMLIB HAVE_CONFIG_H HAMLIB_EMBEDDED_RIGCTLD
    _FILE_OFFSET_BITS=64 _TIME_BITS=64)
target_link_libraries(rigctld_core PUBLIC
    "${HAMLIB_BUILD}/src/.libs/libhamlib.a"
    "${HAMLIB_BUILD}/lib/.libs/libmisc.a"
    Threads::Threads ${CMAKE_DL_LIBS} m)

if(ANDROID)
    add_library(android_rigctld SHARED service_jni.cpp
        usb_audio_monitor_jni.cpp
        transport/trusdx_rx.cpp
        transport/usb_serial_bridge_jni.cpp
        transport/bluetooth_serial_bridge_jni.cpp)
    target_include_directories(android_rigctld PRIVATE "${HAMLIB_SOURCE}/include" "${CMAKE_CURRENT_LIST_DIR}")
    target_link_libraries(android_rigctld PRIVATE rigctld_core log)
    target_link_options(android_rigctld PRIVATE
        "-Wl,-z,max-page-size=16384" "-Wl,-z,common-page-size=16384")
    # Built explicitly for same-device instrumentation. No APK packages this executable.
    add_executable(rigctld-dummy-test-server EXCLUDE_FROM_ALL
        host_main.cpp
        transport/trusdx_rx.cpp
        transport/usb_serial_bridge_jni.cpp
        transport/bluetooth_serial_bridge_jni.cpp)
    target_include_directories(rigctld-dummy-test-server PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
    target_link_libraries(rigctld-dummy-test-server PRIVATE rigctld_core log)
else()
    add_executable(rigctld-host host_main.cpp)
    target_link_libraries(rigctld-host PRIVATE rigctld_core)
    add_executable(test-dummy-tx-loopback ../tests/test_dummy_tx_loopback.cpp)
    target_include_directories(test-dummy-tx-loopback PRIVATE "${HAMLIB_SOURCE}/include" "${HAMLIB_BUILD}/include")
    target_link_libraries(test-dummy-tx-loopback PRIVATE rigctld_core)
    add_executable(test-trusdx-rx ../tests/test_trusdx_rx.cpp transport/trusdx_rx.cpp)
    target_include_directories(test-trusdx-rx PRIVATE transport)
endif()
