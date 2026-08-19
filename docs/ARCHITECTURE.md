# Architecture

## Request interception

The dylib uses public Objective-C runtime and Foundation entry points instead of
private Twitch symbols.

- Playback-token GraphQL bodies are normalized at `NSURLSession` task creation.
- HLS requests from Amazon IVS are intercepted through `NSURLProtocol`.
- `AVAssetResourceLoader` remains as a compatibility path for AVFoundation.
- Internal VAFT requests carry `X-TAS-Internal` to avoid recursive interception.

Authenticated startup GraphQL responses are never proxied.

## Per-stream ownership

Each observed `/channel/hls/<channel>.m3u8` master playlist creates or updates a
`TASStreamContext`. Parsing that master registers each exact rendition URL in a
bounded route table.

When a media playlist arrives, its URL selects the owning context before any ad
logic runs. The context owns:

- Channel and master URL
- Master playlist
- Alternate masters for `embed`, `popout`, and `autoplay`
- Active clean variant and its lifetime

Generation counters prevent a delayed network response from writing into a
context slot that has since been evicted and reused. Inactive contexts expire
after 30 minutes; the tables use bounded LRU replacement.

This ownership model is essential on iOS because Twitch can run the main/PiP
player and a muted channel-profile preview simultaneously.

## Ad handling

On an ad-bearing media playlist, the port:

1. Reuses a still-valid clean variant belonging to the same stream.
2. Requests alternate playback tokens in VAFT order.
3. Selects the matching or closest rendition.
4. If every alternate remains stitched, removes marked ad segments and serves a
   small blank media response for their segment URLs.

Unknown media-playlist URLs pass through without borrowing another stream's
state.

## Signing layout

The arm64 dylib contains:

- `@rpath/TwitchAdBlock.dylib` as `LC_ID_DYLIB`
- 16 KiB of Mach-O header padding
- A 64 KiB `LC_CODE_SIGNATURE` reservation ending at EOF

The reservation allows ordinary IPA signers to replace the ad-hoc signature in
place instead of restructuring the Mach-O.

## Installation paths

For sideloading, the patcher removes the donor Tweach load command and dylib,
then injects `TwitchAdBlock.dylib` into the app's `Frameworks` directory. For a
jailbroken install, the same library is loaded into the `tv.twitch` process by
a Substrate-compatible filter. Rootful packages install under `/Library`;
rootless packages install beneath `/var/jb/Library`.
