# Xerox AirScan Bridge

`xerox-airscan-bridge` is a userspace C++ daemon that exposes a SANE scanner as
an eSCL/AirScan-compatible network scanner. The first target is a Xerox
WorkCentre 3119 connected to a NixOS Intel host.

## Current MVP

- Uses SANE directly through `libsane`.
- Auto-detects a `xerox_mfp` scanner when no explicit SANE device is configured.
- Publishes `_uscan._tcp` with Avahi.
- Serves:
  - `GET /eSCL/ScannerCapabilities`
  - `GET /eSCL/ScannerStatus`
  - `POST /eSCL/ScanJobs`
  - `GET /eSCL/ScanJobs/{job-id}/NextDocument`
  - `DELETE /eSCL/ScanJobs/{job-id}`
- Serializes scanner access so one physical scan runs at a time.
- Produces single-page JPEG output.

## NixOS usage

Import the module:

```nix
{
  imports = [
    ./modules/xerox-airscan-bridge.nix
  ];

  services.xeroxAirscanBridge = {
    enable = true;
    deviceName = "Xerox WorkCentre 3119";
    serialNumber = "XEROX3119";
    saneDevice = "xerox_mfp:libusb:001:010";
    listenAddress = "0.0.0.0";
    port = 8081;
    uuid = "2f07f7a4-3119-44a7-9a11-xerox31190001";
    openFirewall = true;
  };
}
```

Leave `saneDevice = ""` to auto-detect the first `xerox_mfp` device.

## Server-side checks

```bash
scanimage -L
systemctl status xerox-airscan-bridge.service
avahi-browse -rt _uscan._tcp
curl http://macmini.local:8081/eSCL/ScannerCapabilities
curl http://macmini.local:8081/eSCL/ScannerStatus
```

## macOS checks

```bash
dns-sd -B _uscan._tcp local
dns-sd -L "Xerox WorkCentre 3119" _uscan._tcp local
```

Then open Image Capture and test a grayscale A4 scan at 300 DPI.

## Extension points

The daemon is named after the current scanner, but the scanner integration is
split behind a `Scanner` interface and the SANE option setting is centralized in
`SaneDevice`. Future printer/scanner profiles should extend configuration and
option mapping rather than duplicating HTTP or Bonjour code.

Compatibility with macOS may still require adjusting the eSCL XML and Bonjour
TXT records after testing against Image Capture.
