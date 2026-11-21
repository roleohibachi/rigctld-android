#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
read -r revision < ggmorse-client/upstream-version
source_dir=.work/ggmorse
if [[ ! -d "$source_dir/.git" ]]; then
    git init "$source_dir"
    git -C "$source_dir" fetch --depth 1 https://github.com/ggerganov/ggmorse.git "$revision"
    git -C "$source_dir" checkout --detach FETCH_HEAD
fi
if [[ "$(git -C "$source_dir" rev-parse HEAD)" != "$revision" ]]; then
    echo "Expected GGMorse $revision in $source_dir" >&2
    exit 1
fi

notices=.work/ggmorse-notices/licenses
mkdir -p "$notices"
cp "$source_dir/LICENSE" "$notices/GGMorse-LICENSE.txt"
cp app/src/main/assets/licenses/Hamlib-*.txt app/src/main/assets/licenses/GPL-3.0.txt "$notices/"
./gradlew --no-daemon :ggmorse-client:assembleDebug
