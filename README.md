# hyprtrizone

[![CI](https://github.com/nyrda/hyprtrizone/actions/workflows/ci.yml/badge.svg)](https://github.com/nyrda/hyprtrizone/actions/workflows/ci.yml)

A Hyprland layout for an ultrawide desktop. The center column holds the main
window, with independently scrolling stacks on either side. Two windows can
share a side row, and tags control placement, pairing, and workspace following.

This is a personal plugin, built and tested against **Hyprland 0.56.2** with Lua
configuration. The Nix flake pins the compositor and plugin together.

![Hyprtrizone demo with paired side rows and overflow indicators](docs/demo.png)

## Try the demo

```sh
git clone git@github.com:nyrda/hyprtrizone.git
cd hyprtrizone
./dev.sh vm-demo
```

Nix boots a disposable NixOS VM with Hyprland, example windows, and
[Quickshell panels](examples/quickshell/README.md). The VM needs KVM and a
graphical session. Its disk is temporary.

| Binding | Action |
| --- | --- |
| `Super+Return` | Open a terminal |
| `Super+Q` | Close the focused window |
| `Super+arrows` | Move focus |
| `Super+Shift+arrows` | Move a window between zones or rows |
| `Super+left mouse` | Drag a window; drop onto another to pair or exchange |
| `Super+right mouse` | Resize |
| `Super+mouse wheel` | Scroll the focused side stack; center focus targets the right |
| `Super+1/2/3` | Switch workspace |
| `Super+Shift+F7` | Repopulate the demo |
| `Super+Shift+F8/F9` | Toggle left/right pairing |
| `Super+Shift+F10` | Toggle scrolling |
| `Super+M` | Exit Hyprland |

The screenshot comes from `./dev.sh vm-demo-screenshot`. The same
[example configuration](examples/hyprtrizone-demo.lua) can run in a local session.

## Install

The plugin must be built against the exact Hyprland build that loads it.
See [installation](docs/installation.md) for NixOS, a manual build, and hyprpm.
The NixOS module selects the matching compositor and installs a Lua loader:

```nix
# In your flake inputs. SSH access is required for this private repository.
inputs.hyprtrizone.url = "git+ssh://git@github.com/nyrda/hyprtrizone";
```

```nix
# In a NixOS module with inputs passed through specialArgs.
{ inputs, ... }: {
  imports = [ inputs.hyprtrizone.nixosModules.default ];
  programs.hyprtrizone.enable = true;
}
```

```lua
-- hyprland.lua
-- Load before configuring the plugin.
dofile("/etc/xdg/hypr/hyprtrizone_plugin.lua")

hl.config({
    general = { layout = "hyprtrizone" },
    plugin = { hyprtrizone = {
        master_factor = 0.5,
        side_bias = "right",
        side_visible_windows = 3,
    } },
})

hl.bind("SUPER + mouse_up", hl.dsp.layout("scroll up"))
hl.bind("SUPER + mouse_down", hl.dsp.layout("scroll down"))
hl.bind("SUPER + SPACE", hl.dsp.layout("swapwithmaster"))
```

## Layout

The center takes half the workspace width by default. Each side takes a quarter.
Side rows expand to fill their column until the configured visible count is
reached, then overflow vertically. Indicators count windows above and below the
visible area. Disabling scrolling fits all rows into the available height.

New windows follow the focused side row, a zone tag, or the configured side bias.
Pairing happens at the insertion point and leaves existing rows in place. Scroll
offsets belong to each workspace; runtime scrolling and pairing toggles apply
globally.

- [Configuration](docs/configuration.md) lists every setting and runtime command.
- [Window behavior](docs/behavior.md) covers placement, movement, focus, and drag and drop.
- [Quickshell example](examples/quickshell/README.md) explains the demo's panels and geometry.
- [Development](docs/development.md) covers builds, VM tests, and the source structure.

## Development

```sh
./dev.sh build                # Clean local build
./dev.sh vm-test              # Headless layout integration test
./dev.sh vm-demo-screenshot   # Quickshell integration test and screenshot
```

CI builds the plugin and runs both VM tests on the pinned Nix package set.

## License

[MIT](LICENSE)
