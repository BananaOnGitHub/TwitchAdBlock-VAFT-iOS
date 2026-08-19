# Version history and reconstruction notes

This project began as a practical patch for a decrypted Twitch 30.4.2 arm64
app and gained dedicated packaging and patching tooling in 2.0.3.

The early source downloads were not all retained. Their history was recovered
from surviving source archives and the injected Mach-O dylibs in the original
test IPAs. The table distinguishes exact source from reconstructed source so
the Git history does not pretend to have evidence that no longer exists.

| Version | Date | Architecture | Source confidence | What changed |
| --- | --- | --- | --- | --- |
| 1.0.0 | 2026-08-12 | video-swap-new 1.55 / solution 23 | Exact surviving archive | Initial native iOS port. |
| 1.0.0 signing correction | 2026-08-12 | video-swap-new | Exact surviving artifact | Replaced the downloadable dylib with an ad-hoc-signed build; no source delta. |
| 1.0.1 | 2026-08-12 | video-swap-new | Exact reconstruction | Added a 64 KiB signature reservation. Applying the recovered reservation tool to the 1.0.0 dylib produces the IPA dylib byte-for-byte. |
| 2.0.0 | 2026-08-12 | VAFT solution 24 | Source-level reconstruction | First VAFT port. Recovered by reversing the small 2.0.0-to-2.0.1 native delta from symbols and arm64 code. Formatting and comments may differ from the lost original source. |
| 2.0.1 | 2026-08-12 | VAFT solution 24 | Exact code state | Added direct playback-token request normalization. Its dylib is byte-identical to the one built from the surviving 2.0.2 source archive. |
| 2.0.2 | 2026-08-12 | VAFT solution 24 | Exact surviving archive | Packaging-only fix: restored the complete Twitch `Assets.car`; native code is byte-identical to 2.0.1. |
| 2.0.3 | 2026-08-18 | VAFT solution 24 | Exact surviving archive | Isolated VAFT state per stream to fix PiP/profile-preview cross-talk. Added the local patcher and tests. |
| 2.1.0 | 2026-08-18 | VAFT solution 24 | Current source | Renamed the dylib/load command and added standalone rootful/rootless DEBs. |

## Binary evidence

All historical test IPAs used the same Twitch 30.4.2 main executable. Only the
injected dylib changed between native revisions; 2.0.0 through 2.0.2 also
exposed the asset-catalog packaging regression described below.

| Version | Injected dylib SHA-256 | `LC_CODE_SIGNATURE` reservation |
| --- | --- | --- |
| 1.0.1 | `f561350754e84062ecdf90cf1d47bf14b21f05852d7a4aa0f4d5f2c1a04c425f` | 65,536 bytes |
| 2.0.0 | `f329fea631d6a74e866ab8de4a2bef1af3e2ee948a2f292789a77aba55fe3332` | 65,536 bytes |
| 2.0.1 | `61a50c69c86a020c0855411db85aaaebae3cb5a0bc8237f71ed454201d6abf86` | 65,536 bytes |
| 2.0.2 | `61a50c69c86a020c0855411db85aaaebae3cb5a0bc8237f71ed454201d6abf86` | 65,536 bytes |
| 2.0.3 | `7c55a15000fb1b8dcab436aba05c1a952618bf67af1e165a83914577c61048ac` | 65,536 bytes |

The only native symbols added by 2.0.1 were the GraphQL request-normalization
helpers, the two `NSURLSession` data-task replacements, and their stored
original implementations. The 2.0.1 and 2.0.2 dylibs are identical. The only
IPA entry changed by the 2.0.2 repack was `Assets.car`, restoring the same CRC
present in the working 1.0.1 and 2.0.3 packages.

## Architecture transition

Versions 1.x followed `video-swap-new`: detect ad-bearing playlists, request
alternate player types, and swap to a clean rendition. Version 2.0.0 moved to
VAFT's broader stitched marker, ordered alternate-master cache, closest
rendition fallback, and blank-segment response. Version 2.0.3 retained VAFT but
replaced its single global stream state with per-channel contexts required by
Twitch mobile's simultaneous PiP and muted profile-preview players.

The early `Tweach.dylib` name was only a donor compatibility shim: the Twitch
executable already had that load command. Version 2.1.0 made the project fully
standalone by removing the donor command and installing
`@rpath/TwitchAdBlock.dylib` instead.
