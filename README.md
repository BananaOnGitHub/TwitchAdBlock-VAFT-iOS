# TwitchAdBlock-VAFT-iOS

A native iOS port of the **VAFT** strategy from
[pixeltris/TwitchAdSolutions](https://github.com/pixeltris/TwitchAdSolutions).
It supports both sideloaded decrypted copies of Twitch and jailbroken devices.

Releases contain the open-source jailbreak dylib, sideload framework, IPA
releases, an IPA patcher, rootful/rootless jailbreak packages, and checksums.

## Status

- Port version: **2.2.0**
- Upstream strategy: **VAFT solution 24**
- Tested app version: **Twitch 30.4.2, arm64**
  - Tested on an iPhone 16 Pro on iOS 18.2
- Tested installation paths: ESign and LiveContainer/ZSign

Other Twitch versions may work, but Twitch can change its GraphQL, HLS, or
Amazon IVS behavior without notice.

## What it does

- Intercepts Twitch playback-token and HLS requests without private Twitch
  symbols.
- Tries VAFT's `embed`, `popout`, and `autoplay` player types when an ad-bearing
  playlist is detected.
- Matches alternate renditions by resolution and frame rate.
- Falls back to suppressing stitched ad segments when clean alternates are not
  available.
- Maintains isolated state for simultaneous streams, including Twitch mobile's
  PiP player and muted profile previews.
- Adds an Ad Block settings page with persistent, sanitized diagnostics.

The sideload build uses the physical framework and load path
`Tweach.framework/Tweach` for signer compatibility. The binary in that bundle
contains this project's VAFT implementation. Jailbreak packages use the clean
`TwitchAdBlock.dylib` identity.

## Diagnostics

Open **Profile → Settings (cog) → Ad Block**. The diagnostics page provides:

- A persistent logging toggle.
- A live session summary and sanitized event log.
- One-tap report copying.
- Log clearing.

Diagnostics distinguish intercepted and missed HLS paths, master and variant
playlists, unmapped variants, known ad markers, access-token failures, VAFT
candidate results, clean swaps, and segment suppression. URL query strings and
fragments, request headers, access tokens, and manifest contents are never
stored. The log is capped at 512 KiB and rotates automatically.

For a playback regression, enable logging, reproduce the failure, then copy the
diagnostic report from the same page.

## Install from a release

Requirements:

- A decrypted Twitch IPA
- Python 3.10 or newer
- A sideloading/signing tool

Download the release bundle and run:

```bash
python3 tools/patch_ipa.py Twitch.ipa \
  --framework Tweach.framework \
  --output Twitch-VAFT.ipa
```

The resulting IPA is unsigned. Sign `Twitch-VAFT.ipa` with your normal
sideloading tool before installing it.

The patcher:

- Refuses encrypted executables.
- Removes old Tweach and TwitchAdBlock dylib/framework load commands.
- Adds `@rpath/Tweach.framework/Tweach` using existing Mach-O header padding.
- Replaces any donor `Tweach.framework` with the release framework.
- Preserves every unrelated IPA entry and verifies that `Assets.car` is
  unchanged.

## Install on a jailbroken device

Releases include two standalone packages:

- `iphoneos-arm` for traditional rootful jailbreaks.
- `iphoneos-arm64` for rootless jailbreaks using the `/var/jb` layout.

Install the package matching the jailbreak, then restart Twitch. The filter is
scoped to the `tv.twitch` bundle and does not patch the app executable. It can
therefore be used with the newest Twitch version supported by the device's iOS
version. The package conflicts with level3tjg's TwitchAdBlock because both
projects intercept the same playback stack.

## Build from source

Install [Zig 0.14.0](https://ziglang.org/download/0.14.0/) and run:

```bash
make verify
make test
```

To use a Zig binary outside `PATH`:

```bash
ZIG=/path/to/zig make verify
```

The build produces both identities:

- `build/TwitchAdBlock.dylib` for rootful/rootless jailbreak packages.
- `build/Tweach.framework` for sideload IPA patching.

`make deb` creates the jailbreak packages, while `make release` creates the
complete set used by GitHub Releases under `dist/`.

## Troubleshooting

### The signer says it cannot sign the dylib

Use the `Tweach.framework` compatibility build for sideloaded IPAs. Its Mach-O
layout is normalized to the 16 KiB segment boundaries required by the tested
iOS signing paths. The jailbreak dylib retains a 64 KiB replacement-signature
reservation.

### The app launches logged out but crashes after login

Verify that the input IPA contains the complete `Assets.car`. Twitch force-loads
signed-in navigation assets; a truncated asset catalog can survive the login
screen and then crash during scene creation. The included patcher refuses an IPA
with no asset catalog and verifies its CRC after repackaging.

### PiP and profile previews interfere with one another

Upgrade to 2.0.3 or newer. Older native builds used one global VAFT context and
could substitute one player's clean playlist into another player.

Playback reports should include the Twitch and iOS versions, installation
method, failure type, and the copied diagnostic report. Remove anything you do
not want to share before posting it.

See [Architecture](docs/ARCHITECTURE.md), [Version history](docs/HISTORY.md),
[Contributing](CONTRIBUTING.md), and [Upstream provenance](UPSTREAM.md) for more
detail.

## License and attribution

The native port is Apache-2.0 licensed.

This project is not affiliated with Twitch Interactive, Inc., Amazon.com, Inc.,
pixeltris, level3tjg, or the Tweach project. Twitch is a trademark of its
respective owner.
