{
  lib,
  stdenv,
  cmake,
  pkg-config,
  sane-backends,
  avahi,
  libjpeg,
  version ? "v1.0.0",
}:

stdenv.mkDerivation {
  pname = "xerox-airscan-bridge";
  inherit version;

  src = lib.cleanSource ../..;

  nativeBuildInputs = [
    cmake
    pkg-config
  ];

  buildInputs = [
    sane-backends
    avahi
    libjpeg
  ];

  cmakeFlags = [
    "-DXAB_VERSION=${version}"
  ];

  meta = {
    description = "Userspace SANE to eSCL/AirScan bridge for Xerox WorkCentre 3119";
    license = lib.licenses.mit;
    platforms = lib.platforms.linux;
    mainProgram = "xerox-airscan-bridge";
  };
}
