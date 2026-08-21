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

## Diagnostics

`TASDiagnostics` registers two runtime-created UIKit controllers and adds an
`Ad Block` navigation item to Twitch's `AppSettingsViewController`. No Twitch
settings data source is replaced or modified.

Logging is disabled by default and stored under the app's Application Support
directory when enabled. The log records only classified events and summaries:

- Interception transport and sanitized request path
- HTTP status and response size
- Master/variant ownership
- Segment and marker counts
- Alternate player type outcome
- VAFT fallback and suppression decisions

Query strings, fragments, headers, access tokens, GraphQL bodies, and manifest
contents are excluded. The file rotates at 512 KiB. Session counters remain in
memory and are included in the in-app report.

## Signing layout

Both arm64 outputs contain 16 KiB of Mach-O header padding.

- `TwitchAdBlock.dylib` uses `@rpath/TwitchAdBlock.dylib` and is packaged for
  jailbreak injection.
- `Tweach.framework/Tweach` uses `@rpath/Tweach.framework/Tweach` and is
  packaged for sideloading. Its file-backed segments fill their existing 16 KiB
  ranges, and its compact ad-hoc signature ends at EOF so iOS signers can
  replace it cleanly.

The sideload compatibility identity is required because the tested resigning
path correctly rescans the donor filename while rejecting an otherwise
byte-equivalent dylib at a new physical path.

## Installation paths

For sideloading, the patcher removes obsolete dylib/framework commands, places
the release `Tweach.framework` in the app's Frameworks directory, and injects
its compatibility load command. For a jailbroken install,
`TwitchAdBlock.dylib` is loaded into the `tv.twitch` process by a
Substrate-compatible filter. Rootful packages install under `/Library`;
rootless packages install beneath `/var/jb/Library`.
