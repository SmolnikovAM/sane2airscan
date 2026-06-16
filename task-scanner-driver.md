# Task: C++ AirScan/eSCL Bridge for Xerox WorkCentre 3119

Build a userspace C++ daemon that lets Apple/macOS clients scan from the Xerox
WorkCentre 3119 connected to the NixOS home server.

This is not a kernel driver. The daemon should wrap the scanner that already
works through SANE and expose a network scanner interface that macOS understands.

## Current Environment

- Server OS: NixOS
- Server hardware: Mac mini 7,1
- Scanner/printer: Xerox WorkCentre 3119 Series
- USB ID: `0924:4265`
- Printer queue: `xerox3119`
- Scanner backend detected by SANE: `xerox_mfp`
- Current SANE detection result:

```text
device `net:localhost:xerox_mfp:libusb:001:010' is a SAMSUNG ORION multi-function peripheral
```

Current NixOS config already enables:

- CUPS printer sharing
- Avahi/mDNS
- SANE scanner support
- `saned` on TCP `6566`

The problem is that macOS Image Capture does not natively consume SANE/saned
network scanners. macOS expects direct USB scanner support or driverless network
scanner protocols such as AirScan/eSCL.

## Goal

Create a C++ daemon that:

1. Talks to the local Xerox scanner through the SANE C API.
2. Exposes the scanner as an AirScan/eSCL-compatible network scanner.
3. Publishes the scanner over Bonjour/mDNS so macOS can discover it.
4. Allows macOS Image Capture to scan without installing SANE on the Mac.
5. Runs as a declarative NixOS service.

Target user workflow:

1. Start the service on NixOS.
2. On macOS, open System Settings -> Printers & Scanners or Image Capture.
3. See `Xerox WorkCentre 3119` under shared/network scanners.
4. Scan one page from the flatbed.

## Protocol Direction

Implement the public driverless scan protocol commonly known as:

- AirScan
- AirPrint scanning
- eSCL

Use the Mopria eSCL Scan Technical Specification as the primary protocol
reference. Do not guess the XML schema or Bonjour TXT records from memory.

Expected eSCL surface, to be verified against the spec and macOS behavior:

- Bonjour/mDNS service advertisement, likely `_uscan._tcp` for HTTP.
- Optional `_uscans._tcp` for HTTPS/TLS if macOS requires it.
- HTTP XML endpoints for scanner capabilities, scanner status, job creation,
  retrieving the next document, and cancelling scan jobs.

Common endpoint names seen in eSCL implementations include:

```text
GET    /eSCL/ScannerCapabilities
GET    /eSCL/ScannerStatus
POST   /eSCL/ScanJobs
GET    /eSCL/ScanJobs/{job-id}/NextDocument
DELETE /eSCL/ScanJobs/{job-id}
```

Verify exact endpoint behavior, status codes, XML namespaces, request bodies,
and response content types against the Mopria eSCL spec and real macOS tests.

## Recommended Architecture

Use a single daemon process:

```text
macOS Image Capture
  -> Bonjour discovery
  -> eSCL HTTP request
  -> C++ daemon
  -> SANE API
  -> Xerox WorkCentre 3119 over USB
```

Main components:

- `SaneDevice`
  - Opens the scanner with `sane_open`.
  - Lists and maps SANE options.
  - Applies scan settings.
  - Starts scans with `sane_start`.
  - Reads image data with `sane_read`.
  - Cancels scans with `sane_cancel`.

- `EsclServer`
  - HTTP server.
  - XML request parsing and response generation.
  - Scan job lifecycle.
  - Error mapping from SANE errors to eSCL responses.

- `BonjourPublisher`
  - Publishes the service through Avahi or DNS-SD compatibility APIs.
  - Uses stable service name, model, UUID, manufacturer, and supported formats.

- `ScanJobQueue`
  - Serializes scanner access because the hardware is single-user.
  - Tracks job state: pending, scanning, complete, cancelled, failed.
  - Holds temporary scan output until the client fetches it.

## Suggested C++ Stack

Prefer libraries available in Nixpkgs:

- Scanner access: `sane-backends` / `libsane`
- HTTP server: Boost.Beast, libmicrohttpd, or cpp-httplib
- XML: pugixml or tinyxml2
- mDNS/Bonjour: Avahi client library or Avahi DNS-SD compatibility layer
- Image encoding: libjpeg-turbo and/or libpng
- Logging: spdlog or simple structured stderr logs
- Tests: Catch2 or GoogleTest

Use C++20 or newer.

## Scanner Constraints

Assume the Xerox 3119 scanner is simple flatbed hardware:

- No automatic document feeder unless proven otherwise.
- Start with platen/flatbed scans only.
- Support A4 defaults.
- Support grayscale first.
- Add color if the SANE backend reports it reliably.
- Limit initial resolutions to conservative values: `100`, `150`, `200`,
  `300` DPI.

The implementation must discover actual SANE options at runtime instead of
hardcoding unsupported options.

## Output Formats

Initial target:

- JPEG or PNG for single-page scans.

Optional later target:

- Single-page PDF.
- Multi-page PDF if ADF support ever appears.

Check what macOS Image Capture accepts from eSCL. If macOS requires a specific
format, prioritize that format.

## NixOS Integration

Add a Nix package and module.

Possible files:

```text
packages/xerox-airscan-bridge/default.nix
modules/xerox-airscan-bridge.nix
docs/xerox-airscan-bridge.md
```

The NixOS module should provide options such as:

```nix
services.xeroxAirscanBridge = {
  enable = true;
  deviceName = "Xerox WorkCentre 3119";
  saneDevice = "xerox_mfp:libusb:001:010";
  listenAddress = "0.0.0.0";
  port = 8081;
  uuid = "stable-generated-or-configured-uuid";
  openFirewall = true;
};
```

Systemd service requirements:

- Run after network and Avahi.
- Run under a dedicated user if possible.
- Ensure the service user can access the scanner device.
- Add the service user to `scanner` and possibly `lp`.
- Restart on failure.

Firewall:

- Open the selected TCP port for eSCL.
- Avahi/mDNS must remain enabled for discovery.

## Security Requirements

- Bind only to the LAN-facing interface or all interfaces behind the existing
  NixOS firewall.
- Do not expose outside the home network.
- No remote command execution.
- No arbitrary file path reads/writes from HTTP requests.
- Put temporary scan files under a private runtime directory.
- Clean up stale jobs and temporary files.
- Keep logs useful but do not log raw image data.

## Acceptance Criteria

Minimum success:

1. Service builds on NixOS.
2. Service starts with systemd.
3. `avahi-browse -rt _uscan._tcp` on the LAN sees the scanner.
4. macOS sees the scanner in Image Capture or Printers & Scanners.
5. macOS can scan one A4 page from the flatbed.
6. `systemctl --failed` stays clean.
7. Existing CUPS printing still works.
8. Existing SANE local detection still works with `scanimage -L`.

Nice-to-have success:

1. macOS can preview before final scan.
2. Color and grayscale both work.
3. Cancellation works from Image Capture.
4. Multiple concurrent client requests fail gracefully instead of corrupting a
   scan.

## Test Plan

Server-side tests:

```bash
scanimage -L
systemctl status xerox-airscan-bridge.service
avahi-browse -rt _uscan._tcp
curl http://macmini.local:8081/eSCL/ScannerCapabilities
curl http://macmini.local:8081/eSCL/ScannerStatus
```

macOS tests:

```bash
dns-sd -B _uscan._tcp local
dns-sd -L "Xerox WorkCentre 3119" _uscan._tcp local
```

Then test with GUI:

1. Open Image Capture.
2. Check whether the scanner appears under Shared.
3. Run preview scan.
4. Run final scan at 300 DPI grayscale.
5. Save output and inspect orientation, page size, and image quality.

## Implementation Milestones

1. Build a tiny C++ SANE probe tool.
   - List SANE devices.
   - Open the Xerox scanner.
   - Dump supported options.

2. Build basic scan CLI.
   - Scan one page to PNM.
   - Convert to PNG or JPEG.

3. Build HTTP eSCL skeleton.
   - Return static ScannerCapabilities XML.
   - Return static ScannerStatus XML.

4. Add Bonjour/mDNS.
   - Publish `_uscan._tcp`.
   - Verify discovery from macOS with `dns-sd`.

5. Implement real scan jobs.
   - Parse eSCL scan settings.
   - Map them to SANE options.
   - Return scanned image from `NextDocument`.

6. Package for NixOS.
   - Add derivation.
   - Add module.
   - Add systemd service.

7. Compatibility pass with macOS.
   - Fix XML, TXT records, content types, and job state behavior based on
     Image Capture behavior.

## Open Questions

- Does current macOS accept plain HTTP `_uscan._tcp`, or does it require HTTPS
  `_uscans._tcp` for this type of device?
- Which image formats does Image Capture request and accept for eSCL?
- What exact Bonjour TXT records are required for reliable macOS discovery?
- Does the Xerox/SANE backend support color mode reliably?
- Does the scanner produce raw data in a form that can be streamed directly, or
  should the daemon always buffer and encode before returning `NextDocument`?

## References

- Apple AirPrint overview: https://support.apple.com/en-us/102895
- Apple Image Capture guide: https://support.apple.com/guide/image-capture/welcome/mac
- Apple scanner setup guide: https://support.apple.com/guide/mac-help/set-up-a-scanner-to-use-with-mac-mh28039/mac
- Mopria eSCL specification download: https://mopria.org/spec-download
- Mopria overview: https://mopria.org/what-is-mopria
- `sane-airscan` reference implementation for eSCL/WSD clients: https://github.com/alexpevzner/sane-airscan
- SANE project: http://www.sane-project.org/
