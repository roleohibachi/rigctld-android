cmake_minimum_required(VERSION 3.22)
project(ggmorse_rigctld LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
file(STRINGS "${ROOT}/native/hamlib-version" HAMLIB_REVISION LIMIT_COUNT 1)
set(HAMLIB "${ROOT}/.work/hamlib/${HAMLIB_REVISION}")
set(GGMORSE "${ROOT}/.work/ggmorse")
set(HAMLIB_BUILD "${HAMLIB}/build-android-arm64")

if(NOT EXISTS "${HAMLIB_BUILD}/src/.libs/libhamlib.a" OR NOT EXISTS "${GGMORSE}/src/ggmorse.cpp")
    message(FATAL_ERROR "Prepare the dependencies with scripts/docker-build.sh ggmorse")
endif()

add_library(ggmorse_rigctld SHARED receiver.cpp network_serial_stubs.c
    "${GGMORSE}/src/ggmorse.cpp" "${GGMORSE}/src/resampler.cpp")
target_include_directories(ggmorse_rigctld PRIVATE
    "${GGMORSE}/include" "${HAMLIB}/source/include" "${HAMLIB_BUILD}/include")
target_link_libraries(ggmorse_rigctld PRIVATE
    "${HAMLIB_BUILD}/src/.libs/libhamlib.a" "${HAMLIB_BUILD}/lib/.libs/libmisc.a" m)
target_link_options(ggmorse_rigctld PRIVATE "-Wl,-z,max-page-size=16384" "-Wl,-z,common-page-size=16384")
