#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
case "${1:-apk}" in
    apk)
        target=android
        command='bash scripts/build-hamlib.sh android-arm64 && ./gradlew --no-daemon :app:assembleDebug'
        ;;
    test)
        target=native
        command='bash scripts/build-hamlib.sh host &&
            cmake -S native -B .work/native -DCMAKE_BUILD_TYPE=Debug &&
            cmake --build .work/native -j4 &&
            python3 tests/test_rigctld.py .work/native/rigctld-host .work/hamlib/build-host/tests/rigctl -v'
        ;;
    *) echo "Usage: $0 [apk|test]" >&2; exit 2 ;;
esac

image="rigctld-$target-build"
docker build --target "$target" -t "$image" .
docker run --rm --user "$(id -u):$(id -g)" \
    -e GRADLE_USER_HOME=/workspace/.work/gradle \
    -e JAVA_TOOL_OPTIONS=-Duser.home=/workspace/.work/java \
    -v "$PWD:/workspace" "$image" bash -ec "$command"
