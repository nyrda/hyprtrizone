# hyprtrizone

[![CI](https://github.com/nyrda/hyprtrizone/actions/workflows/ci.yml/badge.svg)](https://github.com/nyrda/hyprtrizone/actions/workflows/ci.yml)

A Hyprland window layout plugin for ultrawide monitors with three independent
stacks: the main window in the center, and vertically scrolling stacks on the
left and right.

https://github.com/user-attachments/assets/31f98170-283f-484d-a863-3b48566fd11b

Unlike Hyprland's centered master layout (`master` with `orientation = "center"`),
each side is a stack of its own: windows stay on their side instead of jumping
between left and right, and keep a readable height instead of shrinking.

## Features

- **Three columns.** The center takes half the width by default, and each side
  a quarter. The center can hold several windows, stacked vertically.
- **Scrolling side stacks.** Rows keep a readable height and scroll instead of
  shrinking. Badges show how many windows are scrolled out of view. Each
  workspace keeps its own scroll position.
- **Tag-based placement.** Tag a window with a window rule to decide which
  column it opens in.
- **Pairing.** Two windows can share a side row. Tag the windows that may
  pair; a new one pairs with the single window in the row it opens below.
- **Windows that follow you.** A tiled window tagged to follow, such as a video
  player, moves along when you switch workspaces on its monitor.
  Because each side is a stack of its own, it keeps its column and goes to the
  top of it, and the other windows stay on their sides.
- **Drag and drop.** Drag a window onto another to reorder, pair, or exchange
  places. Keyboard moves travel across columns and rows.
- **Wheel scrolling.** With a modifier held, the mouse wheel scrolls the side
  stack under the pointer.
- **Status bars beside the center.** A full-width bar costs a strip across the
  whole ultrawide screen. hyprtrizone can reserve space above the side stacks
  only, so you can place one or two bar islands there instead, like the
  [Quickshell example](examples/quickshell/README.md). The center keeps the full
  monitor height: on a 49" 32:9 monitor, a game in the center column gets the
  equivalent of a whole 27" 16:9 screen.

## Compatibility

- hyprtrizone is built and tested against **Hyprland 0.57.0** and supports
  only Hyprland's Lua configuration, not a classic `hyprland.conf`.
- A Hyprland plugin only loads into the exact Hyprland build it was compiled
  against, so it has to be rebuilt after every Hyprland update. The NixOS
  module and the Omarchy update hook do that for you.
- `master` follows the newest supported release. The branches
  `hyprland-0.56.1`, `hyprland-0.56.2`, and `hyprland-0.57.0` track those
  releases. Build the branch that matches your installed Hyprland.
- See the [known limitations](docs/behavior.md#known-limitations).

## Install

Pick the method for your system. [Installation](docs/installation.md) has the
details and troubleshooting.

**NixOS.** The flake's module installs the plugin, a Lua loader, and the
Hyprland build the plugin is tested with. It sets `programs.hyprland.package`
and `portalPackage` to that build, with `mkDefault`.

```nix
# flake.nix
inputs.hyprtrizone.url = "github:nyrda/hyprtrizone";

# A NixOS module, with inputs passed through specialArgs.
{ inputs, ... }: {
  imports = [ inputs.hyprtrizone.nixosModules.default ];
  programs.hyprtrizone.enable = true;
}
```

Then load the plugin at the top of `hyprland.lua` with
`dofile("/etc/xdg/hypr/hyprtrizone_plugin.lua")`.

**Arch, CachyOS, and other distributions with hyprpm.** Use the branch that
matches `hyprctl version`:

```sh
hyprpm update
hyprpm add https://github.com/nyrda/hyprtrizone origin/hyprland-0.57.0
hyprpm enable hyprtrizone
```

hyprpm loads plugins after Hyprland has started, so select the layout from a
start hook; see [hyprpm](docs/installation.md#hyprpm).

**Omarchy 4.** Clone, `make`, and install the example config and an update
hook that rebuilds after every `omarchy update`. [Omarchy](docs/omarchy.md)
covers the steps and the changed keybindings.

**Manual build.** `make` builds `hyprtrizoneLayout.so`; load it with
`hl.plugin.load("/absolute/path/to/hyprtrizoneLayout.so")`. It needs a C++23
compiler, pkg-config, and the development files of Hyprland 0.57.0, libei, and
Lua 5.5. See [manual build](docs/installation.md#manual-build).

## Configure

Load the plugin before its settings; with hyprpm, apply them after it has
loaded the plugin, as described under [hyprpm](docs/installation.md#hyprpm).
This example turns on the features that are off by default: pairing,
tag-based placement, and wheel scrolling.

```lua
hl.config({
    general = { layout = "hyprtrizone" },
    plugin = { hyprtrizone = {
        pairing_right = true,
        pairing_tags = "pair",
        zone_tags_left = "browser",
        -- Takes over Super + wheel, which many configs use to switch workspaces.
        wheel_scroll_mods = "SUPER",
    } },
})

hl.window_rule({ name = "pair-terminals", match = { class = [[^(kitty|foot)$]] }, tag = "+pair" })
hl.window_rule({ name = "left-browsers", match = { class = [[^(firefox|chromium)$]] }, tag = "+browser" })

hl.bind("SUPER + SPACE", hl.dsp.layout("swapwithmaster"))
hl.bind("SUPER + SHIFT + Page_Up", hl.dsp.layout("scroll up"))
hl.bind("SUPER + SHIFT + Page_Down", hl.dsp.layout("scroll down"))
```

[Configuration](docs/configuration.md) lists every option and runtime command,
and [`examples/hyprtrizone-demo.lua`](examples/hyprtrizone-demo.lua) is a
complete configuration.

## Try it in a VM

With Nix, KVM, and a graphical session, you can try the layout without
installing anything:

```sh
git clone https://github.com/nyrda/hyprtrizone.git
cd hyprtrizone
./dev.sh vm-demo
```

This boots a disposable NixOS VM with Hyprland, example windows, the demo
configuration, and the Quickshell panels. Its disk is discarded on exit.
[Development](docs/development.md#demo-vm) lists its keys.

![The demo VM: paired rows on both sides, overflow badges, and Quickshell panels above the side stacks](docs/demo.png)

## Documentation

| Document | Contents |
| --- | --- |
| [Installation](docs/installation.md) | NixOS module, Nix packages, manual build, hyprpm, troubleshooting |
| [Omarchy](docs/omarchy.md) | Installing on Omarchy 4, keybindings, the bar, hyprpm |
| [Configuration](docs/configuration.md) | Every option with its default, and every runtime command |
| [Window behavior](docs/behavior.md) | Placement, pairing, scrolling, drag and drop, resizing, workspaces |
| [Quickshell example](examples/quickshell/README.md) | Panels above the side stacks |
| [Development](docs/development.md) | Builds, live reloading, the demo VM, VM tests, the demo video, source layout |

## License

[MIT](LICENSE)
