# Quickshell example

Two small panels that sit above the side stacks. The left panel shows
workspaces 1 to 4, which can be clicked, and the title of the focused window.
The right panel shows the date and time. The center column keeps the full
monitor height.

![Panels above both side stacks, with the center window reaching the top edge](../../docs/demo.png)

## Run

The demo VM starts Quickshell by itself:

```sh
./dev.sh vm-demo
```

In your own session, install Quickshell with Hyprland support (the demo pins
0.3.1) and run it from the repository root:

```sh
quickshell -p examples/quickshell
```

The [example Lua config](../hyprtrizone-demo.lua) starts Quickshell when
Hyprland starts. It looks for the examples in `HYPRTRIZONE_DEMO_DIR`, or in
`~/.config/hypr/hyprtrizone-demo` if that variable is unset. Either set the
variable to this repository's `examples` directory before starting Hyprland,
or copy the contents of `examples/` there.

The example config binds `Super+Shift+F7` to
[`populate-demo.sh`](../populate-demo.sh), which opens the demo windows. It
needs `jq` and a terminal that accepts `--title <title> <command>` and keeps
that title. The default is `foot -o locked-title=yes`; set `TRIZONE_TERMINAL`
to use another.

## Geometry

The panels do not read the layout. Two values in `shell.qml` must match the Lua
config:

| `shell.qml` | Lua config | Demo value |
| --- | --- | --- |
| `masterFactor` | `master_factor` | `0.5` |
| `headerHeight` | `left_header_height`, `right_header_height` | `34` |

Each panel is `screen.width * (1 - masterFactor) / 2` logical pixels wide.

The panels use `exclusiveZone: 0`, so they reserve no monitor space. The header
heights reserve the space inside the side columns instead. A positive exclusive
zone would shorten the center column too.

The panels assume no other panel reserves monitor space. They appear on every
monitor, and the workspace highlight and window title follow global focus. They
do not follow runtime resizing of the center, layout switches, or special
workspaces.

## Full-width mode

The shared `BarContents.qml` also works as an ordinary top bar with any layout:

```sh
QS_BAR_MODE=top quickshell -p examples/quickshell
```

This mode draws one full-width panel and reserves 34 pixels at the top of the
monitor. Stop the other Quickshell instance before switching modes.

## Validation

`./dev.sh vm-demo-screenshot` checks both modes in the demo VM. In split mode,
the panels must be 640×34 on a 2560×1080 monitor and reserve no space. In
full-width mode, the panel must reserve 34 pixels at the top.
