# Omarchy

Omarchy 4 runs Hyprland 0.56.2 with a Lua configuration, the same compositor
this plugin targets. It already ships the Hyprland headers, Lua 5.5, and
`base-devel`, so the plugin builds without extra packages. Omarchy itself loads
no Hyprland plugins, and its Quickshell shell does not depend on the layout.

This setup was tested on Omarchy 4.0.4 in a VM.

## Requirements

Check the compositor version first:

```sh
pacman -Q hyprland   # hyprland 0.56.2-…
```

The plugin only loads into the Hyprland build whose headers it was compiled
against. A build for another version refuses to load and shows a notification.

## Try it without changing the configuration

Build the plugin, load it into the running session, and switch the layout:

```sh
git clone https://github.com/nyrda/hyprtrizone.git
cd hyprtrizone
make
hyprctl plugin load "$PWD/hyprtrizoneLayout.so"
hyprctl eval 'hl.config({ general = { layout = "hyprtrizone" } })'
```

Log out and back in to undo it. Omarchy's keybindings still assume dwindle
until you apply the changes below.

## Install

1. Build the plugin as above, then install it and the layout toggle script:

   ```sh
   install -Dm755 hyprtrizoneLayout.so ~/.local/lib/hyprtrizone/hyprtrizoneLayout.so
   install -Dm755 examples/omarchy/hyprtrizone-layout-toggle ~/.local/bin/hyprtrizone-layout-toggle
   cp examples/omarchy/hyprtrizone.lua ~/.config/hypr/hyprtrizone.lua
   ```

2. Add this line at the end of `~/.config/hypr/hyprland.lua`, after Omarchy's
   defaults and your override files:

   ```lua
   require("hypr.hyprtrizone")
   ```

3. Delete any saved workspace layouts, then log out and back in:

   ```sh
   rm -f ~/.local/state/omarchy/workspace-layouts/*.lua
   ```

[`examples/omarchy/hyprtrizone.lua`](../examples/omarchy/hyprtrizone.lua)
loads the plugin, selects the layout, and replaces the keybindings listed
below. It reuses Omarchy's `terminal` tag for pairing on the right side. See
[Configuration](configuration.md) for the other options.

Check the result with `hyprctl plugin list` and `hyprctl configerrors`.
`hyprctl -j activeworkspace` shows `"tiledLayout": "hyprtrizone"`.

## Keybindings

| Keys | Omarchy default | With the example config |
| --- | --- | --- |
| `SUPER+J` | `togglesplit`, which does nothing here | `swapwithmaster` |
| `SUPER+L` | Toggle dwindle and scrolling, saved per workspace | Toggle hyprtrizone and dwindle, saved in the same files |
| `SUPER+Shift+Wheel`, `SUPER+Shift+PgUp/PgDn` | Unbound | Scroll the focused side stack |

`SUPER+Wheel` keeps switching workspaces. Omarchy's own `SUPER+L` script has no
way back to hyprtrizone. If a workspace stays on another layout, delete its
file in `~/.local/state/omarchy/workspace-layouts/` and log out and back in.

These Omarchy bindings work unchanged: focus, `SUPER+Shift+Arrows` (exchange
with the neighbouring window or column), resizing, fullscreen, full width,
groups, and window width save and restore.

## The bar and Quickshell

The Omarchy bar reserves its height across the whole monitor, and every column
starts below it. Keep `left_header_height` and `right_header_height` at `0`.

The [Quickshell panels](../examples/quickshell/README.md) also run next to
Omarchy's shell. They sit below the bar, so set the header heights to the panel
height (34 in the example). For side panels without the bar above them, hide
the Omarchy bar with `omarchy-toggle-bar`, or move it to a side edge with
`bar.position` in `~/.config/omarchy/shell.json`.

Omarchy shows notifications at the top right, over the top of the right column.

## Updates

Rebuild and reinstall the plugin after every `hyprland` package update, then
log out and back in. Until then, the old build refuses to load: the layout
falls back to dwindle and `hyprctl configerrors` lists the
`plugin.hyprtrizone` options.

## hyprpm

Omarchy includes `hyprpm` in its `hyprland` package, but the manual install
above is simpler. hyprpm needs extra packages and `sudo`, downloads the
Hyprland sources although Omarchy already ships the headers, and loads the
plugin only after Hyprland has started.

To use it anyway, install its build tools and add the plugin:

```sh
sudo pacman -S --needed cmake meson
hyprpm update
hyprpm add https://github.com/nyrda/hyprtrizone
hyprpm enable hyprtrizone
```

Remove the `hl.plugin.load` line from `hyprtrizone.lua`, and add a start hook
that loads the plugin and then reloads the configuration:

```lua
hl.on("hyprland.start", function()
    hl.exec_cmd("hyprpm reload -n && hyprctl reload")
end)
```

Hyprland does not load hyprpm plugins on its own. Each session starts in dwindle
and with configuration errors for the plugin options until the hook has run.
Run `hyprpm update` after every Hyprland update.

## Uninstall

Remove the `require("hypr.hyprtrizone")` line, delete
`~/.config/hypr/hyprtrizone.lua`, `~/.local/bin/hyprtrizone-layout-toggle`, and
`~/.local/lib/hyprtrizone/`, and delete
`~/.local/state/omarchy/workspace-layouts/*.lua`. Then log out and back in.
