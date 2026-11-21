#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
case "${1:-apk}" in
    apk)
        target=android
        command='bash scripts/build-hamlib.sh android-arm64 && ./gradlew --no-daemon :app:assembleDebug'
        ;;
    ggmorse)
        target=android
        command='bash scripts/build-hamlib.sh android-arm64 && bash scripts/build-ggmorse.sh'
        ;;
    dummy-server)
        target=android
        command='bash scripts/build-hamlib.sh android-arm64 &&
            sdk_cmake="$ANDROID_HOME/cmake/3.22.1/bin/cmake" &&
            ndk="${ANDROID_NDK_ROOT:-$ANDROID_HOME/ndk/27.2.12479018}" &&
            "$sdk_cmake" -S native -B .work/native-dummy-android \
                -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
                -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-31 -DCMAKE_BUILD_TYPE=Release &&
            "$sdk_cmake" --build .work/native-dummy-android --target rigctld-dummy-test-server --parallel 4'
        ;;
    test)
        target=native
        command='bash scripts/build-hamlib.sh host &&
            cmake -S native -B .work/native -DCMAKE_BUILD_TYPE=Debug &&
            cmake --build .work/native -j4 &&
            .work/native/test-trusdx-rx &&
            read -r revision < native/hamlib-version &&
            python3 tests/test_rigctld.py .work/native/rigctld-host "${HAMLIB_WORK_DIR:-.work/hamlib/$revision}/build-host/tests/rigctl" -v'
        ;;
    *) echo "Usage: $0 [apk|ggmorse|dummy-server|test]" >&2; exit 2 ;;
esac

image="rigctld-$target-build"
docker build --target "$target" -t "$image" .
docker run --rm --user "$(id -u):$(id -g)" \
    -e GRADLE_USER_HOME=/workspace/.work/gradle \
    -e JAVA_TOOL_OPTIONS=-Duser.home=/workspace/.work/java \
    -v "$PWD:/workspace" "$image" bash -ec "$command"
