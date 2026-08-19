# Changelog

## 2.1.0

- Renamed the native library and load command to `TwitchAdBlock.dylib`.
- The IPA patcher now removes Tweach's dylib and load command instead of
  replacing Tweach in place.
- Added standalone rootful and rootless jailbreak DEB packages scoped to the
  Twitch bundle.
- Added DEBs to automated builds, release archives, and checksums.

## 2.0.3

- Replaced the single global stream slot with per-channel VAFT contexts.
- Mapped each media-playlist URL to the master playlist that produced it.
- Scoped clean variants and alternate-player caches to their owning stream.
- Fixed profile previews that could clone the active PiP stream or turn black.
- Added a local IPA patcher, Mach-O verification, tests, and
  reproducible GitHub Actions builds.
