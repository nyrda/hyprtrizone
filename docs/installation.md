# Installation

Hyprtrizone targets **Hyprland 0.56.2** with a Lua configuration. A Hyprland
plugin only loads into the exact compositor build it was compiled against, so
rebuild the plugin whenever Hyprland changes.

`master` targets the newest supported Hyprland release. Every supported release
also has a `hyprland-<version>` branch, for example `hyprland-0.56.2`. On a
distribution, build the branch for the Hyprland you have installed.

| Method | Use it when |
| --- | --- |
| [NixOS module](#nixos-module) | You run NixOS. The flake pins Hyprland and the plugin together. |
| [Nix packages](#nix-packages-without-the-module) | You manage the Hyprland package yourself on Nix. |
| [Manual build](#manual-build) | You want a local `.so`, for development or another distribution. |
| [hyprpm](#hyprpm) | Your distribution provides Hyprland 0.56.2 and hyprpm. |
| [Omarchy](omarchy.md) | You run Omarchy 4. Covers the build, the configuration, and Omarchy's keybindings. |

## NixOS module

The flake provides the plugin, the matching Hyprland and portal packages, and a
NixOS module for `x86_64-linux`. Hyprland and its portal come from a pinned
nixpkgs revision whose Hyprland is the release the branch targets, so
cache.nixos.org serves them prebuilt.

```nix
# flake.nix
inputs.hyprtrizone.url = "github:nyrda/hyprtrizone";
```

Do not make its `nixpkgs` input follow your own nixpkgs. It would build the
plugin against a Hyprland it was not tested with.

Pass `inputs` through `specialArgs` of `nixosSystem`, then enable the module:

```nix
{ inputs, ... }: {
  imports = [ inputs.hyprtrizone.nixosModules.default ];
  programs.hyprtrizone.enable = true;
}
```

The module enables `programs.hyprland` and sets its `package` and
`portalPackage` to the pinned builds, all with `mkDefault`. It also writes
`/etc/xdg/hypr/hyprtrizone_plugin.lua`, which loads the plugin. Load it at the
top of `hyprland.lua`, before any plugin settings:

```lua
dofile("/etc/xdg/hypr/hyprtrizone_plugin.lua")
hl.config({ general = { layout = "hyprtrizone" } })
```

If your configuration sets `programs.hyprland.package` itself, use the flake's
`hyprland` and `xdg-desktop-portal-hyprland` packages there. Updating the
hyprtrizone input updates the compositor and the plugin together.

| Module option | Default | Description |
| --- | --- | --- |
| `programs.hyprtrizone.enable` | `false` | Install the plugin, its loader, and the matching Hyprland. |
| `programs.hyprtrizone.package` | the flake's plugin | Plugin package. It must be built against `programs.hyprland.package`. |

## Nix packages without the module

```nix
let
  tz = inputs.hyprtrizone.packages.${pkgs.stdenv.hostPlatform.system};
in {
  programs.hyprland = {
    enable = true;
    package = tz.hyprland;
    portalPackage = tz.xdg-desktop-portal-hyprland;
  };
  # In your Lua config: hl.plugin.load("${tz.hyprtrizone}/lib/hyprtrizoneLayout.so")
}
```

To build the plugin against your own package set instead, use
`pkgs.callPackage "${inputs.hyprtrizone}/nix/package.nix" { }` or the flake's
`overlays.default`, which adds `pkgs.hyprtrizone`. Your Hyprland must then
provide the same plugin API as 0.56.2.

## Manual build

```sh
./dev.sh build
```

This produces `hyprtrizoneLayout.so` in the repository root. `dev.sh` finds the
build dependencies in this order:

1. Development packages installed on the system, through `pkg-config`.
2. The Nix dev shell of a Hyprland checkout at `HYPRLAND_ROOT` (default
   `../Hyprland`), if its `hyprland.dev` output is already built.
3. The nixpkgs package set pinned in `flake.lock`.

Without Nix, install a C++23 compiler, Make, pkg-config, the Hyprland 0.56.2
development files with their dependencies, and the libei and Lua 5.5
development files, then run `make`. Set `LUA_PKG` if your distribution uses an
unusual pkg-config name for Lua.

The build stops when `pkg-config` finds a different Hyprland than the branch
targets (`HYPRLAND_VERSION`). Switch to the matching `hyprland-<version>` branch.
`make IGNORE_HYPRLAND_VERSION=1` skips the check for porting.

Load the result by absolute path:

```lua
hl.plugin.load("/absolute/path/to/hyprtrizoneLayout.so")
hl.config({ general = { layout = "hyprtrizone" } })
```

## hyprpm

hyprpm builds plugins against a system-managed Hyprland. For a Nix-managed
Hyprland, use the module or packages above. hyprpm needs `git`, a C++23
compiler, `pkg-config`, `cmake`, `cpio`, and `meson`; the plugin build also
needs the libei and Lua 5.5 development files. Some distributions ship hyprpm
as a separate package. On Arch Linux and CachyOS:

```sh
sudo pacman -S --needed hyprland hyprpm base-devel git
```

Then, as your user inside a running Hyprland session:

```sh
hyprpm update
hyprpm add https://github.com/nyrda/hyprtrizone origin/hyprland-0.56.2
hyprpm enable hyprtrizone
```

`hyprpm update` downloads the Hyprland sources that match the running
compositor and installs their headers. It asks for your password through
`sudo`.

hyprpm loads plugins after Hyprland starts. Select the layout once the plugin
has loaded:

```lua
hl.on("hyprland.start", function()
    hl.exec_cmd([[hyprpm reload -n && hyprctl eval 'hl.config({ general = { layout = "hyprtrizone" } })']])
end)
```

Put plugin settings in the same `hyprctl eval` call, or set them from a
separate file loaded after the plugin. Run `hyprpm update` after every Hyprland
update. hyprpm needs the branch with its `origin/` prefix. After an update to
another Hyprland release, remove the repository with
`hyprpm remove hyprtrizone` and add the branch for the new release.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| The plugin fails to load | Rebuild against the running Hyprland. A build for another Hyprland refuses to load and shows a notification. `hyprctl plugin list` shows loaded plugins. |
| The layout is not active | Set `general.layout = "hyprtrizone"`. `hyprctl -j activeworkspace` shows the active layout in `tiledLayout`. |
| Configuration errors | Run `hyprctl configerrors`. Load the plugin before its settings. |
| No overflow badges | Badges only appear for hidden windows. Set `indicator_show_zero = true` to always show them. |
| Scrolling does nothing | A side window or the center needs focus, and the stack needs more rows than `side_visible_windows`. Check `hyprctl dispatch 'hl.dsp.layout("setscrolling on")'`. |
| Windows do not pair | Enable pairing on that side and give both windows a tag from `pairing_tags`. Title-based window rules stop matching when a program changes its title. |
| Old `hyprctl` forms fail | Under a Lua config, use `hyprctl eval '<lua>'` for settings and `hyprctl dispatch '<lua dispatcher>'` for actions. |
