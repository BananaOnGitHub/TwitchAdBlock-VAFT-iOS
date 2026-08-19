#!/bin/sh
set -eu

cd "$(dirname "$0")"

ZIG="${ZIG:-$(command -v zig || true)}"
if [ -z "$ZIG" ]; then
    echo "error: Zig 0.14.0 is required (set ZIG=/path/to/zig)" >&2
    exit 1
fi

ZIG_REAL="$(python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "$ZIG")"
ZIG_LIB="${ZIG_LIB:-$(dirname "$ZIG_REAL")/lib}"
IOS_STUBS="${IOS_STUBS:-$PWD/stubs}"
LOCAL_CACHE="${ZIG_LOCAL_CACHE_DIR:-$PWD/build/cache}"
GLOBAL_CACHE="${ZIG_GLOBAL_CACHE_DIR:-$PWD/build/global-cache}"

mkdir -p build "$LOCAL_CACHE" "$GLOBAL_CACHE"
ZIG_LOCAL_CACHE_DIR="$LOCAL_CACHE" \
ZIG_GLOBAL_CACHE_DIR="$GLOBAL_CACHE" \
"$ZIG" build-lib \
  -target aarch64-ios-none \
  -O ReleaseSmall -dynamic -fPIC -fno-stack-protector \
  -isystem "$ZIG_LIB/libc/include/any-macos-any" \
  -L"$IOS_STUBS" -lSystem -lobjc \
  -install_name @rpath/TwitchAdBlock.dylib \
  -dead_strip -headerpad 4000 \
  --entitlements entitlements.plist \
  --name TwitchAdBlock \
  -femit-bin=build/TwitchAdBlock.dylib \
  -cflags -Wall -Wextra -Werror -fvisibility=hidden -- \
  src/TwitchAdBlock.c

python3 tools/reserve_codesign_space.py build/TwitchAdBlock.dylib --size 65536
