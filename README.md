# TwitchAdBlock-VAFT-iOS

A native iOS port of the **VAFT** strategy from
[pixeltris/TwitchAdSolutions](https://github.com/pixeltris/TwitchAdSolutions).
It supports both sideloaded decrypted copies of Twitch and jailbroken devices.

Releases contain the open-source dylib, IPA releases, an IPA patcher,
rootful/rootless jailbreak packages, and checksums.

## Status

- Port version: **2.1.0**
- Upstream strategy: **VAFT solution 24**
- Tested app version: **Twitch 30.4.2, arm64**
  - Tested on an iPhone 16 Pro on 18.2
- Tested installation path: LiveContainer/ZSign and ordinary IPA resigning

Other Twitch versions may work, but Twitch can change its GraphQL, HLS, or
Amazon IVS behavior without notice. I haven't tested this on any other devices,
iOS, or Twitch versions.

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

The project is completely independent of Tweach. When a Tweach-modified IPA is
used as a donor solely because it contains a newer decrypted Twitch build, the
patcher removes `Tweach.dylib` and its Mach-O load command before injecting
`TwitchAdBlock.dylib`. No Tweach authentication, DRM, account system, or
developer-server calls remain.

## Install from a release

Requirements:

- A decrypted Twitch IPA
- Python 3.10 or newer
- A sideloading/signing tool

Download the release bundle and run:

```bash
python3 tools/patch_ipa.py (TWITCH IPA NAME HERE).ipa \
  --dylib TwitchAdBlock.dylib \
  --output Twitch-VAFT.ipa
```

The resulting IPA is unsigned. Sign `Twitch-VAFT.ipa` with your
normal sideloading tool before installing it.

The patcher:

- Refuses encrypted executables.
- Removes the donor's `@rpath/Tweach.dylib` load command and dylib file.
- Adds `@rpath/TwitchAdBlock.dylib` using existing Mach-O header padding.
- Updates an existing `TwitchAdBlock.dylib` without duplicate commands.
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

The output is `build/TwitchAdBlock.dylib`. `make deb` creates rootful and
rootless packages, while `make release` creates the complete set used by
GitHub Releases under `dist/`.

## Troubleshooting

### The signer says it cannot sign the dylib

Use the release dylib rather than rebuilding with arbitrary linker settings.
The build reserves 64 KiB for a replacement code signature and has been tested
against LiveContainer's ZSign implementation.

### The app launches logged out but crashes after login

Verify that the input IPA contains the complete `Assets.car`. Twitch force-loads
signed-in navigation assets; a truncated asset catalog can survive the login
screen and then crash during scene creation. The included patcher refuses an IPA
with no asset catalog and verifies its CRC after repackaging.

### PiP and profile previews interfere with one another

Upgrade to 2.0.3 or newer. Older native builds used one global VAFT context and
could substitute one player's clean playlist into another player.

## Repository policy

- Reproduction reports should include the Twitch version, install method, and a
  description of playback behavior—not account tokens or credentials.

See [Architecture](docs/ARCHITECTURE.md), [Version history](docs/HISTORY.md),
[Contributing](CONTRIBUTING.md), and [Upstream provenance](UPSTREAM.md) for more
detail.

## License and attribution

The native port is Apache-2.0 licensed.

This project is not affiliated with Twitch Interactive, Inc., Amazon.com, Inc.,
pixeltris, level3tjg, or the Tweach project. Twitch is a trademark of its
respective owner.
