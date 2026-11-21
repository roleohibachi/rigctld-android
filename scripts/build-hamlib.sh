#!/usr/bin/env bash
set -euo pipefail

root_dir=$(cd "$(dirname "$0")/.." && pwd)
read -r revision < "$root_dir/native/hamlib-version"
work_dir="${HAMLIB_WORK_DIR:-$root_dir/.work/hamlib/$revision}"
source_dir="$work_dir/source"
build_dir="$work_dir/build-${1:-host}"
jobs="${BUILD_JOBS:-4}"
configure_options=()

case "${1:-host}" in
    host) ;;
    android-arm64)
        ndk="${ANDROID_NDK_ROOT:-${ANDROID_HOME:?Set ANDROID_HOME or ANDROID_NDK_ROOT}/ndk/27.2.12479018}"
        toolchain="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
        export CC="$toolchain/aarch64-linux-android31-clang"
        export CXX="$toolchain/aarch64-linux-android31-clang++"
        export AR="$toolchain/llvm-ar"
        export RANLIB="$toolchain/llvm-ranlib"
        export STRIP="$toolchain/llvm-strip"
        configure_options+=(--host=aarch64-linux-android)
        ;;
    *) echo "Usage: $0 [host|android-arm64]" >&2; exit 2 ;;
esac

mkdir -p "$work_dir" "$build_dir"
exec 9>"$work_dir/.build-lock"
flock 9
if [[ ! -d "$source_dir/.git" ]]; then
    git clone --no-checkout "${HAMLIB_GIT_URL:-https://github.com/Hamlib/Hamlib.git}" "$source_dir"
    git -C "$source_dir" checkout --detach "$revision"
fi
if [[ "$(git -C "$source_dir" rev-parse HEAD)" != "$revision" ]]; then
    echo "Expected Hamlib $revision in $source_dir" >&2
    exit 1
fi

for patch in "$root_dir"/native/hamlib-patches/*.patch; do
    if git -C "$source_dir" apply --reverse --check --ignore-space-change --unidiff-zero --recount "$patch" 2>/dev/null; then
        continue
    fi
    git -C "$source_dir" apply --ignore-space-change --unidiff-zero --recount "$patch"
done

if [[ ! -x "$source_dir/configure" ]]; then
    (cd "$source_dir" && ./bootstrap)
fi

if [[ ! -f "$build_dir/Makefile" ]]; then
    cd "$build_dir"
    CFLAGS="-O2 -fPIC" CXXFLAGS="-O2 -fPIC" "$source_dir/configure" \
        "${configure_options[@]}" --enable-static --disable-shared \
        --without-cxx-binding --without-readline --without-libusb --without-xml-support \
        --disable-winradio
fi

make -C "$build_dir/lib" -j"$jobs"
make -C "$build_dir/security" -j"$jobs"
make -C "$build_dir/src" -j"$jobs"
if [[ "${1:-host}" == host ]]; then
    make -C "$build_dir/tests" -j"$jobs" rigctl
fi
