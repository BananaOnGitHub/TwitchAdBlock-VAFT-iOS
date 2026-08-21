# Changelog

## 2.2.0

- Added an Ad Block entry to Twitch's profile settings screen.
- Added persistent, opt-in diagnostic logging with a 512 KiB cap.
- Added an in-app diagnostic report with session counters, viewing, copying,
  and clearing controls.
- Sanitized all logged URLs and excluded query strings, fragments, headers,
  access tokens, and manifest contents.
- Logged HLS interception, manifest classification, VAFT candidate selection,
  access-token failures, clean swaps, unmapped variants, and suppressed
  segments.
- Restored the proven `Tweach.framework/Tweach` physical path for sideload
  compatibility while retaining `TwitchAdBlock.dylib` for jailbreak packages.
- Normalized the sideload framework's Mach-O segments to 16 KiB boundaries so
  ESign and LiveContainer/ZSign can replace its signature successfully.
- Updated the build, verification, patching, tests, and release tooling for the
  framework-based sideload layout.

## 2.1.0

- Renamed the native library and load command to `TwitchAdBlock.dylib`.
- Added standalone rootful and rootless jailbreak DEB packages scoped to the
  Twitch bundle.
- Added DEBs to automated builds, release archives, and checksums.
- The clean dylib identity remains the jailbreak packaging path; sideload
  builds subsequently returned to the compatibility path in 2.2.0.

## 2.0.3

- Replaced the single global stream slot with per-channel VAFT contexts.
- Mapped each media-playlist URL to the master playlist that produced it.
- Scoped clean variants and alternate-player caches to their owning stream.
- Fixed profile previews that could clone the active PiP stream or turn black.
- Added a local IPA patcher, Mach-O verification, tests, and reproducible
  GitHub Actions builds.
