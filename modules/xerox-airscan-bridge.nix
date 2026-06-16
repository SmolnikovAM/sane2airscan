{
  config,
  lib,
  pkgs,
  ...
}:

let
  cfg = config.services.xeroxAirscanBridge;
  package = cfg.package;
in
{
  options.services.xeroxAirscanBridge = {
    enable = lib.mkEnableOption "SANE to eSCL/AirScan scanner bridge";

    package = lib.mkOption {
      type = lib.types.package;
      default = pkgs.callPackage ../packages/xerox-airscan-bridge { };
      description = "xerox-airscan-bridge package to run.";
    };

    deviceName = lib.mkOption {
      type = lib.types.str;
      default = "Xerox WorkCentre 3119";
      description = "Scanner name advertised over Bonjour and eSCL.";
    };

    manufacturer = lib.mkOption {
      type = lib.types.str;
      default = "Xerox";
      description = "Manufacturer name exposed by eSCL.";
    };

    model = lib.mkOption {
      type = lib.types.str;
      default = "WorkCentre 3119";
      description = "Model name exposed by eSCL.";
    };

    saneDevice = lib.mkOption {
      type = lib.types.str;
      default = "";
      example = "xerox_mfp:libusb:001:010";
      description = "Exact SANE device name. Empty means auto-detect a xerox_mfp device.";
    };

    listenAddress = lib.mkOption {
      type = lib.types.str;
      default = "0.0.0.0";
      description = "Address for the eSCL HTTP server to bind.";
    };

    port = lib.mkOption {
      type = lib.types.port;
      default = 8081;
      description = "TCP port for the eSCL HTTP server.";
    };

    uuid = lib.mkOption {
      type = lib.types.str;
      default = "2f07f7a4-3119-44a7-9a11-xerox31190001";
      description = "Stable scanner UUID advertised over Bonjour and eSCL.";
    };

    defaultResolution = lib.mkOption {
      type = lib.types.enum [ 100 150 200 300 ];
      default = 300;
      description = "Default scan resolution in DPI.";
    };

    defaultColorMode = lib.mkOption {
      type = lib.types.enum [ "Grayscale8" "RGB24" ];
      default = "Grayscale8";
      description = "Default color mode for scan jobs that omit ColorMode.";
    };

    publishMdns = lib.mkOption {
      type = lib.types.bool;
      default = true;
      description = "Publish the scanner as an _uscan._tcp service through Avahi.";
    };

    openFirewall = lib.mkOption {
      type = lib.types.bool;
      default = true;
      description = "Open the eSCL TCP port in the NixOS firewall.";
    };
  };

  config = lib.mkIf cfg.enable {
    services.avahi.enable = true;
    services.avahi.publish.enable = true;
    hardware.sane.enable = true;

    users.users.xerox-airscan-bridge = {
      isSystemUser = true;
      group = "xerox-airscan-bridge";
      extraGroups = [ "scanner" "lp" ];
    };
    users.groups.xerox-airscan-bridge = { };

    networking.firewall.allowedTCPPorts = lib.mkIf cfg.openFirewall [ cfg.port ];

    systemd.services.xerox-airscan-bridge = {
      description = "Xerox WorkCentre SANE to eSCL/AirScan bridge";
      wantedBy = [ "multi-user.target" ];
      after = [ "network-online.target" "avahi-daemon.service" ];
      wants = [ "network-online.target" "avahi-daemon.service" ];

      serviceConfig = {
        ExecStart = lib.escapeShellArgs ([
          "${lib.getExe package}"
          "--device-name" cfg.deviceName
          "--manufacturer" cfg.manufacturer
          "--model" cfg.model
          "--listen-address" cfg.listenAddress
          "--port" (toString cfg.port)
          "--uuid" cfg.uuid
          "--default-resolution" (toString cfg.defaultResolution)
          "--default-color-mode" cfg.defaultColorMode
        ] ++ lib.optional (cfg.saneDevice != "") "--sane-device"
          ++ lib.optional (cfg.saneDevice != "") cfg.saneDevice
          ++ lib.optional (!cfg.publishMdns) "--no-mdns");
        DynamicUser = false;
        User = "xerox-airscan-bridge";
        Group = "xerox-airscan-bridge";
        Restart = "on-failure";
        RestartSec = "5s";
        PrivateTmp = true;
        ProtectHome = true;
        ProtectSystem = "strict";
        NoNewPrivileges = true;
        SupplementaryGroups = [ "scanner" "lp" ];
      };
    };
  };
}
