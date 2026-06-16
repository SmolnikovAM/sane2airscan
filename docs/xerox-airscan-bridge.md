# Technical Notes

`xerox-airscan-bridge` exposes a local SANE scanner as an eSCL/AirScan network
scanner. The current tested target is a Xerox WorkCentre 3119 connected to a
NixOS x86_64 host.

## Current MVP

- Uses SANE directly through `libsane`.
- Auto-detects a local `xerox_mfp` scanner when no explicit SANE device is
  configured.
- Publishes `_uscan._tcp` with Avahi.
- Serves the core eSCL scan endpoints plus `GET /eSCL/ScannerIcon`.
- Serializes scanner access so one physical scan runs at a time.
- Produces single-page JPEG output.
- Forces unsupported client input sources back to platen for the Xerox 3119
  flatbed path.
- Reports eSCL job state in `ScannerStatus` for macOS Image Capture
  compatibility.

## Extension Points

The daemon is named after the current scanner, but the scanner integration is
split behind a `Scanner` interface and the SANE option setting is centralized in
`SaneDevice`. Future scanner profiles should extend configuration and option
mapping rather than duplicating HTTP or Bonjour code.

Useful future work:

- Add explicit device profiles for scanner-specific source and option mapping.
- Broaden output format support if clients require PNG or PDF.
- Add automated tests for eSCL XML parsing and job-state transitions.
- Keep macOS Image Capture compatibility checks around `ScannerStatus`,
  Bonjour TXT records, and cancellation behavior.

## Versioning

Release artifacts are built from `v*` tags. The tag is passed into the build and
is returned by:

```bash
xerox-airscan-bridge --version
```
