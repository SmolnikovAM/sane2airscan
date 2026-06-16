# NixOS Server Usage

This guide covers building, running, and testing `xerox-airscan-bridge` on a
NixOS x86_64 host with a USB Xerox WorkCentre 3119 scanner.

## Prerequisites

- The scanner is visible locally through SANE:

```bash
scanimage -L
```

- Avahi is enabled for mDNS/Bonjour discovery.
- The eSCL HTTP port, `8081` by default, is reachable from macOS on the LAN.
- The service user can access the scanner device. On NixOS that usually means
  membership in the `scanner` and `lp` groups.

## Build on NixOS

From a checkout on the NixOS host:

```bash
nix-build -E 'with import <nixpkgs> {}; callPackage ./packages/xerox-airscan-bridge {}'
./result/bin/xerox-airscan-bridge --help
./result/bin/xerox-airscan-bridge --version
```

## Declarative Service

Import the module from this repository and enable the service:

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
    saneDevice = "";
    listenAddress = "0.0.0.0";
    port = 8081;
    uuid = "2f07f7a4-3119-44a7-9a11-xerox31190001";
    openFirewall = true;
  };
}
```

Leaving `saneDevice = ""` lets the bridge auto-detect the first local
`xerox_mfp` device. Set it to an exact `scanimage -L` value if auto-detection
finds the wrong scanner.

Apply the configuration:

```bash
sudo nixos-rebuild switch
systemctl status xerox-airscan-bridge.service
```

## Temporary Manual Run

For one-off testing without installing the module:

```bash
bin="$(readlink -f result/bin/xerox-airscan-bridge)"

sudo systemd-run \
  --unit=xerox-airscan-bridge-test \
  --description="Temporary Xerox AirScan bridge test" \
  --property=Restart=no \
  --property=SupplementaryGroups=scanner \
  --property=SupplementaryGroups=lp \
  "$bin" \
  --device-name "Xerox WorkCentre 3119" \
  --manufacturer Xerox \
  --model "WorkCentre 3119" \
  --serial-number XEROX3119 \
  --listen-address 0.0.0.0 \
  --port 8081 \
  --uuid 2f07f7a4-3119-44a7-9a11-xerox31190001
```

Stop it with:

```bash
sudo systemctl stop xerox-airscan-bridge-test.service
```

## Safe Checks

These checks do not start a scan:

```bash
systemctl is-active xerox-airscan-bridge.service
curl -fsS http://127.0.0.1:8081/eSCL/ScannerStatus
curl -fsS http://127.0.0.1:8081/eSCL/ScannerCapabilities
avahi-browse -rt _uscan._tcp
```

From macOS on the same LAN:

```bash
dns-sd -B _uscan._tcp local
dns-sd -L "Xerox WorkCentre 3119" _uscan._tcp local
curl -fsS http://scanner-host.local:8081/eSCL/ScannerStatus
```

Then open Image Capture and scan from the shared Xerox device.

## GitHub Nix Artifact

The `Build Nix artifact` workflow runs only for pushed tags named `v*`, for
example `v1.0.0`. It builds the package on `x86_64-linux`, passes the tag into
the binary version, and uploads a Nix store closure artifact. After downloading
and unpacking the workflow artifact on a NixOS host:

```bash
xz -dc xerox-airscan-bridge-x86_64-linux.nix-store-closure.nar.xz | sudo nix-store --import
binary_path="$(cat binary-path.txt)"
"$binary_path" --help
"$binary_path" --version
```

The artifact is intended for quick testing. For a permanent installation,
prefer the NixOS module so the service, firewall, SANE, and Avahi settings are
declared together.

## Logs

Focused service logs:

```bash
journalctl -u xerox-airscan-bridge.service --no-pager -o short-iso
```

For the temporary unit:

```bash
journalctl -u xerox-airscan-bridge-test.service --no-pager -o short-iso
```

Useful request patterns:

```bash
journalctl -u xerox-airscan-bridge.service --no-pager -o short-iso \
  | grep -E 'HTTP (GET|POST|DELETE)|scan job|failed|published|SANE option'
```

## Scan Operations

These operations start or affect the physical scanner:

- `POST /eSCL/ScanJobs`
- `GET /eSCL/ScanJobs/{job-id}/NextDocument`
- `scanimage` commands that produce image output
- Image Capture preview or scan attempts

Use them only when the scanner is ready and nobody else is using the device.
