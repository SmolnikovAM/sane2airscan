# Xerox AirScan Bridge

`xerox-airscan-bridge` is a userspace C++ daemon that exposes a local SANE
scanner as an eSCL/AirScan network scanner. The MVP target is a Xerox
WorkCentre 3119 attached by USB to a NixOS x86_64 host, so macOS Image Capture
can discover and scan from it without installing SANE on the Mac.

This is not a kernel driver. The daemon sits between macOS and SANE:

```text
macOS Image Capture
  -> Bonjour _uscan._tcp discovery
  -> eSCL HTTP requests
  -> xerox-airscan-bridge
  -> SANE xerox_mfp backend
  -> Xerox WorkCentre 3119 over USB
```

## Status

Current MVP:

- Uses the SANE C API through `libsane`.
- Publishes `_uscan._tcp` through Avahi.
- Serves the core eSCL endpoints:
  - `GET /eSCL/ScannerCapabilities`
  - `GET /eSCL/ScannerStatus`
  - `POST /eSCL/ScanJobs`
  - `GET /eSCL/ScanJobs/{job-id}/NextDocument`
  - `DELETE /eSCL/ScanJobs/{job-id}`
  - `GET /eSCL/ScannerIcon`
- Serializes scanner access for single-user flatbed hardware.
- Produces single-page JPEG scans.
- Parses basic eSCL scan settings, including resolution, color mode, input
  source, and scan region.

The code is structured so future SANE-backed scanners can reuse the HTTP/eSCL
and Bonjour layers while extending device-specific option mapping.

## Build

Required libraries:

- C++20 compiler
- CMake
- pkg-config
- `sane-backends`
- Avahi client/common libraries
- libjpeg

On a system with those dependencies available:

```bash
cmake -S . -B build
cmake --build build
```

On NixOS:

```bash
nix-build -E 'with import <nixpkgs> {}; callPackage ./packages/xerox-airscan-bridge {}'
./result/bin/xerox-airscan-bridge --help
./result/bin/xerox-airscan-bridge --version
```

## Run Manually

```bash
./build/xerox-airscan-bridge \
  --device-name "Xerox WorkCentre 3119" \
  --manufacturer Xerox \
  --model "WorkCentre 3119" \
  --serial-number XEROX3119 \
  --sane-device "xerox_mfp:libusb:001:010" \
  --listen-address 0.0.0.0 \
  --port 8081 \
  --uuid 2f07f7a4-3119-44a7-9a11-xerox31190001
```

Leave `--sane-device` unset to prefer a local non-`net:` `xerox_mfp` device.

## Release Artifacts

Release builds are tag-driven. Pushing a tag such as `v1.0.0` runs the GitHub
Actions Nix build and passes that exact tag into the binary. The release binary
must report the same value:

```bash
xerox-airscan-bridge --version
# xerox-airscan-bridge v1.0.0
```

## NixOS Module

Import `modules/xerox-airscan-bridge.nix` and enable the service:

```nix
{
  imports = [
    ./modules/xerox-airscan-bridge.nix
  ];

  services.xeroxAirscanBridge = {
    enable = true;
    deviceName = "Xerox WorkCentre 3119";
    manufacturer = "Xerox";
    model = "WorkCentre 3119";
    serialNumber = "XEROX3119";
    saneDevice = "xerox_mfp:libusb:001:010";
    listenAddress = "0.0.0.0";
    port = 8081;
    uuid = "2f07f7a4-3119-44a7-9a11-xerox31190001";
    openFirewall = true;
  };
}
```

The module enables SANE and Avahi, creates a dedicated service user, adds it to
`scanner` and `lp`, and opens the configured TCP port when `openFirewall` is
true.

## Verification

On the NixOS host:

```bash
scanimage -L
systemctl status xerox-airscan-bridge.service
avahi-browse -rt _uscan._tcp
curl http://scanner-host.local:8081/eSCL/ScannerCapabilities
curl http://scanner-host.local:8081/eSCL/ScannerStatus
```

On macOS:

```bash
dns-sd -B _uscan._tcp local
dns-sd -L "Xerox WorkCentre 3119" _uscan._tcp local
```

Then open Image Capture and test a grayscale A4 scan at 300 DPI.

## Repository Layout

- `src/` and `include/`: C++ daemon implementation.
- `packages/xerox-airscan-bridge/default.nix`: Nix package.
- `modules/xerox-airscan-bridge.nix`: NixOS service module.
- `docs/xerox-airscan-bridge.md`: operational notes and test checklist.
- `docs/nixos-server-usage.md`: NixOS build, deployment, and artifact usage.

## License

MIT. See `LICENSE`.
