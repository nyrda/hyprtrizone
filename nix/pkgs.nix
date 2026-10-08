# The nixpkgs package set this branch targets: its Hyprland is the release in HYPRLAND_VERSION, and
# cache.nixos.org serves it prebuilt, so neither CI nor users compile Hyprland. The plugin, the compositor, and
# the test VMs share one compiler and one set of Hyprland libraries.
#
# The flake passes its locked nixpkgs input. Without it (nix-build nix/...), the same revision is read from
# flake.lock.
{
  system ? "x86_64-linux",
  nixpkgs ?
    let
      lock = builtins.fromJSON (builtins.readFile ../flake.lock);
      locked = lock.nodes.${lock.nodes.root.inputs.nixpkgs}.locked;
    in
    builtins.fetchTarball {
      url = "https://github.com/${locked.owner}/${locked.repo}/archive/${locked.rev}.tar.gz";
      sha256 = locked.narHash;
    },
}:

import nixpkgs { localSystem = system; }
