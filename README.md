# TwitchAdBlock for iOS

A native iOS adaptation of `video-swap-new.user.js` from
[pixeltris/TwitchAdSolutions](https://github.com/pixeltris/TwitchAdSolutions),
pinned to userscript version 1.55 / internal solution version 23.

The exact upstream userscript used for this port is included under
`upstream/`; its provenance and checksum are recorded in `UPSTREAM.md`.

## Strategy

- Normalizes Twitch live playback-token requests to the `popout` player type.
- Intercepts Twitch HLS through `AVAssetResourceLoader` without depending on
  Twitch's private Swift class names.
- Detects `stitched-ad` playlists and retries `autoplay`,
  `picture-by-picture`, and `embed` playback-token variants.
- Selects the alternate HLS rendition matching the current resolution and
  frame rate.
- If every alternate still contains ads, removes stitched ad segments and
  waits for live segments rather than playing the ad media.

The dylib is intentionally installed as `Tweach.dylib` so it can replace the
existing injected dylib without modifying the Twitch executable's Mach-O load
commands. It contains no Tweach code, DRM, account logic, FFmpeg, or calls to
the Tweach developer's servers.

## Compatibility

Built against the decrypted Twitch 30.4.2 arm64 IPA supplied for this port.
The packaged IPA is not distribution-signed and must be signed by the
sideloading tool or signing service used to install it.

The module is emitted with a replaceable ad-hoc signature, 16 KiB of Mach-O
header padding, and a 64 KiB in-place signature reservation in `__LINKEDIT`.
This lets normal IPA signing tools replace its signature without having to
restructure or enlarge the dylib.

The injected filename remains `Tweach.dylib` only because the supplied Twitch
executable already has that load command. The file itself is this project's
small native module; the original Tweach bundle, binary, dependencies,
authentication, and DRM are not present in the packaged IPA.

Because Twitch controls the GraphQL and HLS responses, ad-delivery changes can
require future updates even when the native interception points remain stable.

## Build

Set `ZIG` to a Zig compiler, `ZIG_LIB` to its `lib` directory, and
`IOS_STUBS` to this repository's `build-stubs` directory, then run
`./build.sh`. The output is `build/Tweach.dylib` for arm64 iOS.
