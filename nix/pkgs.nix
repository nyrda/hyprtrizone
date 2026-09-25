# Hyprland's own package set at the tag this branch targets: Hyprland's pinned nixpkgs with its overlays.
# This is exactly how Hyprland's flake builds the hyprland package it publishes, so the plugin, the
# compositor, and the test VMs share one compiler and one set of Hyprland libraries.
#
# The flake passes its locked hyprland input. Without it (nix-build nix/...), the same revision is read
# from flake.lock.
{
  system ? "x86_64-linux",
  hyprland ?
    let
      lock = builtins.fromJSON (builtins.readFile ../flake.lock);
      locked = lock.nodes.${lock.nodes.root.inputs.hyprland}.locked;
    in
    builtins.getFlake "github:${locked.owner}/${locked.repo}/${locked.rev}?narHash=${locked.narHash}",
}:

let
  # Fixes for this tag's own flake build, applied after Hyprland's overlays. Each branch keeps only what its
  # tag needs; an empty list means the tag builds exactly as Hyprland publishes it.
  fixes = [
    # v0.56.2's CMake requires glaze 7.x, but its pinned nixpkgs has glaze 8.0.0, so the build tries to
    # download glaze and fails in the sandbox. Use glaze 7.9.0, which Hyprland's flake built v0.56.1 with.
    (final: prev: {
      glaze-hyprland = prev.glaze-hyprland.overrideAttrs {
        version = "7.9.0";
        src = final.fetchFromGitHub {
          owner = "stephenberry";
          repo = "glaze";
          tag = "v7.9.0";
          hash = "sha256-vNhxBdGaM70YABfwczvJcAFIYdEGIUGE8Sp2sgkTcaQ=";
        };
      };
    })
  ];
in
import hyprland.inputs.nixpkgs {
  localSystem = system;
  overlays =
    with hyprland.overlays;
    [
      hyprland-packages
      hyprland-extras
    ]
    ++ fixes;
}
