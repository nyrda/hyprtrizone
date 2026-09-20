{
  description = "hyprtrizone: three-zone Hyprland layout with scrolling side stacks";

  # Pinned to a nixpkgs revision whose Hyprland the plugin is ported to and tested against.
  # nix/hyprtrizone-vm-test.nix and nix/hyprtrizone-vm-demo.nix read this pin from flake.lock.
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/e554fab72f81915600f3f449b786fd9af40439a5";

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      vmTest = import ./nix/hyprtrizone-vm-test.nix { inherit system nixpkgs; };
      vmDemo = import ./nix/hyprtrizone-vm-demo.nix { inherit system nixpkgs; };
    in
    {
      # Built against the pinned nixpkgs Hyprland. To build against your own compositor package instead:
      #   pkgs.callPackage "${inputs.hyprtrizone}/nix/package.nix" { }
      packages.${system} = {
        hyprtrizone = pkgs.callPackage ./nix/package.nix { };
        default = self.packages.${system}.hyprtrizone;
        demo-vm = vmDemo.vm;

        # The exact Hyprland the plugin above is built and VM-tested against. Running these guarantees a match.
        inherit (pkgs) hyprland xdg-desktop-portal-hyprland;
      };

      # Runs the matching Hyprland and installs /etc/xdg/hypr/hyprtrizone_plugin.lua, which loads the plugin.
      nixosModules.default = import ./nix/module.nix self;

      overlays.default = final: prev: { hyprtrizone = final.callPackage ./nix/package.nix { }; };

      checks.${system} = {
        vm-test = vmTest;
        demo = vmDemo.screenshot;
      };

      devShells.${system}.default = pkgs.mkShell { inputsFrom = [ self.packages.${system}.hyprtrizone ]; };
    };
}
