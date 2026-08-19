# TwitchAdBlock for iOS

A native iOS adaptation of `vaft-ublock-origin.js` from
[pixeltris/TwitchAdSolutions](https://github.com/pixeltris/TwitchAdSolutions),
pinned to internal VAFT solution version 24.

The exact upstream uBlock Origin script used for this port is included under
`upstream/`; its provenance and checksum are recorded in `UPSTREAM.md`.

## Strategy

- Normalizes Twitch live playback-token requests to the `popout` player type.
- Mutates only playback-token GraphQL requests at `NSURLSession` task creation,
  while HLS is intercepted through `NSURLProtocol`, including the bundled
  Amazon IVS player's session path. Authenticated startup GraphQL responses are
  never proxied. `AVAssetResourceLoader` remains as a compatibility path for
  AVFoundation playback.
- Detects VAFT's broad `stitched` ad marker and tries `embed`, `popout`, then
  `autoplay`, matching upstream's priority and fallback behavior.
- Selects an exact resolution/frame-rate rendition when available, otherwise
  the closest resolution by pixel area.
- Caches alternate master playlists and forwards the current Twitch client
  version, session, integrity, authorization, and device headers when asking
  for alternate playback tokens.
- If all alternates still contain ads, identifies the stitched media segments,
  removes low-latency prefetches, and substitutes VAFT's blank MP4 response for
  those segment requests.

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

The final module was exercised against LiveContainer's current ZSign source
(`b8f2401f95d445fc5ab3698677828feb5d24e038`): both its ad-hoc and
certificate-backed paths completed in place, with no signature-space
reallocation.

The IPA must retain the base app's complete `Assets.car`. Twitch force-unwraps
several signed-in navigation images during scene creation, so a truncated asset
catalog can launch while logged out and then trap immediately after login even
though the injected module and its signature are valid.

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
