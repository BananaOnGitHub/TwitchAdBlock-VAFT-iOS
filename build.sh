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

build_binary() {
    output_path="$1"
    install_name="$2"
    binary_name="$3"
    ZIG_LOCAL_CACHE_DIR="$LOCAL_CACHE" \
    ZIG_GLOBAL_CACHE_DIR="$GLOBAL_CACHE" \
    "$ZIG" build-lib \
      -target aarch64-ios-none \
      -O ReleaseSmall -dynamic -fPIC -fno-stack-protector \
      -isystem "$ZIG_LIB/libc/include/any-macos-any" \
      -L"$IOS_STUBS" -lSystem -lobjc \
      -install_name "$install_name" \
      -dead_strip -headerpad 4000 \
      --entitlements entitlements.plist \
      --name "$binary_name" \
      -femit-bin="$output_path" \
      -cflags -Wall -Wextra -Werror -fvisibility=hidden -- \
      src/TwitchAdBlock.c src/TASDiagnostics.c
}

# Jailbreak package: keep the clean identity and a large in-place signature
# reservation for injection frameworks that replace the ad-hoc signature.
build_binary build/TwitchAdBlock.dylib @rpath/TwitchAdBlock.dylib TwitchAdBlock
python3 tools/reserve_codesign_space.py build/TwitchAdBlock.dylib --size 65536

# Sideload package: preserve the donor framework identity. Tested iOS signers
# require every file-backed segment to fill its existing 16 KiB-aligned range,
# and handle the compact trailing ad-hoc signature reliably.
mkdir -p build/Tweach.framework
build_binary build/Tweach.framework/Tweach @rpath/Tweach.framework/Tweach Tweach
python3 tools/align_macho_segments.py build/Tweach.framework/Tweach
python3 tools/shrink_adhoc_signature.py build/Tweach.framework/Tweach
cp packaging/TweachFramework-Info.plist build/Tweach.framework/Info.plist
