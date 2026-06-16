{
  lib,
  stdenv,
  cmake,
  pkg-config,
  sane-backends,
  avahi,
  libjpeg,
}:

stdenv.mkDerivation {
  pname = "xerox-airscan-bridge";
  version = "0.1.0";

  src = ../..;

  nativeBuildInputs = [
    cmake
    pkg-config
  ];

  buildInputs = [
    sane-backends
    avahi
    libjpeg
  ];

  meta = {
    description = "Userspace SANE to eSCL/AirScan bridge for Xerox WorkCentre 3119";
    license = lib.licenses.mit;
    platforms = lib.platforms.linux;
    mainProgram = "xerox-airscan-bridge";
  };
}
