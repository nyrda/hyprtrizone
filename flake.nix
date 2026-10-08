{
  description = "hyprtrizone: three-zone Hyprland layout with scrolling side stacks";

  # The nixpkgs revision whose Hyprland is the release this branch targets (HYPRLAND_VERSION), prebuilt on
  # cache.nixos.org. The plugin, Hyprland, and the test VMs all come from it (nix/pkgs.nix). Do not make it
  # follow another nixpkgs: that changes the Hyprland the plugin is built against.
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/39ad350a0602fa0a58a544344e3e9187526ea45c";

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import ./nix/pkgs.nix { inherit system nixpkgs; };
      vmTest = import ./nix/hyprtrizone-vm-test.nix { inherit system pkgs; };
      vmDemo = import ./nix/hyprtrizone-vm-demo.nix { inherit system pkgs; };
    in
    {
      # Built against the pinned Hyprland. To build against your own compositor package instead:
      #   pkgs.callPackage "${inputs.hyprtrizone}/nix/package.nix" { }
      packages.${system} = {
        hyprtrizone = vmTest.plugin;
        default = self.packages.${system}.hyprtrizone;
        demo-vm = vmDemo.vm;
        # The 30:9 feature tour (hyprtrizone-demo.mp4). Not a check: it needs 8 cores and a few minutes.
        demo-video = vmDemo.video;

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

      devShells.${system}.default = pkgs.mkShell.override { inherit (pkgs.hyprland) stdenv; } {
        inputsFrom = [ self.packages.${system}.hyprtrizone ];
      };
    };
}
