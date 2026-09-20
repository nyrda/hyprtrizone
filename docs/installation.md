# Installation

Hyprtrizone targets Hyprland 0.56.2 and uses Lua configuration. A plugin must be
compiled against the same compositor build that loads it. Rebuild after changing
Hyprland.

## NixOS

The flake provides the plugin, its matching Hyprland and portal packages, and a
NixOS module. Its outputs target `x86_64-linux`.

```nix
inputs.hyprtrizone.url = "git+ssh://git@github.com/nyrda/hyprtrizone";
```

Keep this input's nixpkgs pin. Adding `inputs.nixpkgs.follows = "nixpkgs"` inside
the hyprtrizone input would replace the tested compositor with your system's
version.

Pass `inputs` through `nixosSystem.specialArgs`, then enable the module:

```nix
{ inputs, ... }: {
  imports = [ inputs.hyprtrizone.nixosModules.default ];
  programs.hyprtrizone.enable = true;
}
```

Load the generated file before plugin settings in `hyprland.lua`:

```lua
dofile("/etc/xdg/hypr/hyprtrizone_plugin.lua")
hl.config({ general = { layout = "hyprtrizone" } })
```

The module sets `programs.hyprland.package` and `portalPackage` with `mkDefault`.
If your configuration already sets those options, use the packages below so the
compositor and plugin match. Updating the hyprtrizone flake input updates the
pinned pair together.

Without the module:

```nix
let
  tz = inputs.hyprtrizone.packages.${pkgs.stdenv.hostPlatform.system};
in {
  programs.hyprland = {
    enable = true;
    package = tz.hyprland;
    portalPackage = tz.xdg-desktop-portal-hyprland;
  };
  # Load "${tz.hyprtrizone}/lib/hyprtrizoneLayout.so" in your generated Lua config.
}
```

To build against your own package set, use
`pkgs.callPackage "${inputs.hyprtrizone}/nix/package.nix" { }`. That compositor
must still provide the API this revision targets. The flake also exports an
overlay that adds `pkgs.hyprtrizone` using the receiving package set.

## Local build

```sh
./dev.sh build
```

The result is `hyprtrizoneLayout.so` in the repository root. The helper uses
installed development packages when available, a matching Hyprland checkout's
Nix shell when its development output is already built, or the pinned nixpkgs
shell as a fallback. Set `HYPRLAND_ROOT` to select a checkout; the default is
`../Hyprland`.

For a system build, install a C++23 compiler, Make, pkg-config, the matching
Hyprland development files and their dependencies, and Lua 5.5 development
files. `LUA_PKG` overrides the detected Lua pkg-config name.

```lua
hl.plugin.load("/absolute/path/to/hyprtrizoneLayout.so")
hl.config({ general = { layout = "hyprtrizone" } })
```

## hyprpm

With the system build dependencies installed and repository access configured:

```sh
hyprpm update
hyprpm add git@github.com:nyrda/hyprtrizone.git
hyprpm enable hyprtrizone
```

Use hyprpm with a system-managed Hyprland. For Nix-managed Hyprland, use the
package or module above.

```lua
hl.on("hyprland.start", function()
    hl.exec_cmd("hyprpm reload -n")
end)
hl.config({ general = { layout = "hyprtrizone" } })
```

Run `hyprpm update` after a Hyprland update. This repository has no compatibility
pins for older compositor releases.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Plugin fails to load | Rebuild against the running Hyprland; inspect `hyprctl plugin list`. |
| Layout is inactive | Set `general.layout = "hyprtrizone"`; inspect `hyprctl -j activeworkspace` and its `tiledLayout` field. |
| Configuration fails | Inspect `hyprctl configerrors`. Load the plugin before its settings. |
| No overflow indicators | Set `indicator_show_zero = true` to display zero counts. |
| Scrolling does nothing | Send `hyprctl dispatch 'hl.dsp.layout("setscrolling on")'`; the side also needs enough rows to overflow. |
| Windows do not pair | Enable pairing on that side and give both windows a matching tag. Title rules stop matching if a terminal changes its title. |
| CLI commands fail under Lua | Use `hyprctl eval '<lua>'` for configuration and `hyprctl dispatch '<lua dispatcher>'` for actions. |
