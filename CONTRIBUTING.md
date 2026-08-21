# Contributing

Bug reports and focused pull requests are welcome.

## Before filing a playback issue

Include:

- Twitch app version
- iOS version
- Sideloading/container tool, or jailbreak and injection framework
- Whether the account was signed in
- Whether the failure involved a preroll, midroll, PiP, or profile preview
- The copied in-app diagnostic report after reproducing the failure

Do not include Twitch credentials, OAuth tokens, or client-integrity headers.

## Development checks

```bash
make verify
make test
make deb
```

`-Werror` is enabled for the native build. Changes to stream routing should keep
all master, variant, and backup state scoped to a `TASStreamContext`.

When updating upstream VAFT, update `upstream/vaft-ublock-origin.js`,
`UPSTREAM.md`, its commit, retrieval date, internal solution version, and
SHA-256 together.
