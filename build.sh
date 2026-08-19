#!/bin/sh
set -eu

cd "$(dirname "$0")"

: "${ZIG:?Set ZIG to a Zig compiler executable}"
: "${ZIG_LIB:?Set ZIG_LIB to Zig's lib directory}"
: "${IOS_STUBS:?Set IOS_STUBS to the directory containing libSystem and libobjc TBD stubs}"

mkdir -p build/cache build/global-cache
"$ZIG" build-lib \
  -target aarch64-ios-none \
  -O ReleaseSmall -dynamic -fPIC -fno-stack-protector \
  -isystem "$ZIG_LIB/libc/include/any-macos-any" \
  -L"$IOS_STUBS" -lSystem -lobjc \
  -install_name @rpath/Tweach.dylib \
  -dead_strip -headerpad 4000 \
  --entitlements entitlements.plist \
  --name Tweach \
  -femit-bin=build/Tweach.dylib \
  -cflags -Wall -Wextra -Werror -fvisibility=hidden -- \
  src/TwitchAdBlock.c
