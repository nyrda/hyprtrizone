# hyprtrizone plugin package.
#
# Hyprland plugins must be built against the exact Hyprland they are loaded into, so call this with the
# package set your compositor comes from:
#
#   pkgs.callPackage "${inputs.hyprtrizone}/nix/package.nix" { }
#
# Dependencies include the development files required by hyprland.pc and Lua.
{
  lib,
  stdenv,
  pkg-config,
  hyprland,
  aquamarine,
  cairo,
  glslang,
  hyprcursor,
  hyprgraphics,
  hyprlang,
  hyprutils,
  libGL,
  libdrm,
  libinput,
  libxkbcommon,
  lua5_5,
  pixman,
  wayland,
  libxcb,
  libxcb-errors,
  libxcb-wm,
  pango,
  # Override to build from another source tree.
  hyprtrizoneSrc ? lib.cleanSourceWith {
    src = ../.;
    filter =
      path: type:
      let
        base = baseNameOf path;
      in
      !(lib.hasPrefix ".git" base)
      && !(lib.hasPrefix "result" base)
      && !(lib.elem base [
        "build"
        "nix"
        "examples"
        "docs"
      ])
      && !(lib.hasSuffix ".so" base);
  },
}:

stdenv.mkDerivation {
  pname = "hyprtrizone";
  version = "0-unstable-hyprland-${hyprland.version}";

  src = hyprtrizoneSrc;

  nativeBuildInputs = [ pkg-config ];

  buildInputs = [
    hyprland.dev
    aquamarine
    cairo
    glslang
    hyprcursor
    hyprgraphics
    hyprlang
    hyprutils
    libGL
    libdrm
    libinput
    libxkbcommon
    lua5_5
    pixman
    wayland
    libxcb
    libxcb-errors
    libxcb-wm
    # Required through hyprgraphics' pkg-config file in some Hyprland releases.
    pango
  ];

  enableParallelBuilding = true;

  installPhase = ''
    runHook preInstall
    install -Dm755 hyprtrizoneLayout.so "$out/lib/hyprtrizoneLayout.so"
    runHook postInstall
  '';

  meta = {
    description = "Three-zone Hyprland layout with scrolling side stacks and window pairing";
    license = lib.licenses.mit;
    platforms = lib.platforms.linux;
  };
}
