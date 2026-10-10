# NixOS module: run the Hyprland this plugin revision is built and tested against, and ship a Lua loader for it.
self:
{
  config,
  lib,
  pkgs,
  ...
}:
let
  cfg = config.programs.hyprtrizone;
  packages = self.packages.${pkgs.stdenv.hostPlatform.system};
in
{
  options.programs.hyprtrizone = {
    enable = lib.mkEnableOption "the hyprtrizone Hyprland layout plugin together with its matching Hyprland";

    package = lib.mkOption {
      type = lib.types.package;
      default = packages.hyprtrizone;
      defaultText = lib.literalExpression "hyprtrizone.packages.\${system}.hyprtrizone";
      description = "The plugin package. It has to be built against the Hyprland in programs.hyprland.package.";
    };
  };

  config = lib.mkIf cfg.enable {
    programs.hyprland = {
      enable = lib.mkDefault true;
      # Both come from the Hyprland flake revision locked by the hyprtrizone flake, the Hyprland the plugin is tested with.
      package = lib.mkDefault packages.hyprland;
      portalPackage = lib.mkDefault packages.xdg-desktop-portal-hyprland;
    };

    # Load it from hyprland.lua, before any plugin config: dofile("/etc/xdg/hypr/hyprtrizone_plugin.lua")
    environment.etc."xdg/hypr/hyprtrizone_plugin.lua".text = ''
      hl.plugin.load("${cfg.package}/lib/hyprtrizoneLayout.so")
    '';
  };
}
