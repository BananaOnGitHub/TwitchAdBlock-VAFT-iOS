# Upstream provenance

- Project: `pixeltris/TwitchAdSolutions`
- Repository commit: `c51ef2fe8f667f9dc9216eb550924cf0d732ce27`
- Strategy: `vaft`
- Distribution: uBlock Origin script
- Internal solution version: 24
- Source URL: <https://raw.githubusercontent.com/pixeltris/TwitchAdSolutions/master/vaft/vaft-ublock-origin.js>
- Retrieved: 2026-08-12
- SHA-256: `8ba15a99627c3d2a8fab3c3011b43d68ecb89eb40af549b0052d98449f02f591`

The native port preserves the upstream access-token player types, persisted
query hash, broad ad marker, resolution matching, alternate-master cache,
blank-segment response, and alternate-player ordering. Browser Worker/fetch
hooks and DOM/player UI actions are replaced by native Foundation interception;
the app's Amazon IVS player does not expose a browser Worker or DOM.
