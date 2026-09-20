# Quickshell example

The demo runs two panels above the side stacks. The left panel contains
clickable workspaces 1 through 4 and the focused window title. The right panel
shows the local date and time. The center column keeps its full height.

## Run

The demo VM includes Quickshell and starts it automatically:

```sh
./dev.sh vm-demo
```

For a local session, install Quickshell with Hyprland support. The pinned demo
uses Quickshell 0.3.1. Run from the repository root:

```sh
quickshell -p examples/quickshell
```

The [Lua example](../hyprtrizone-demo.lua) starts Quickshell on Hyprland startup.
Set `HYPRTRIZONE_DEMO_DIR` to the absolute path of this repository's `examples`
directory before starting Hyprland. Alternatively, copy the contents of
`examples/` to `~/.config/hypr/hyprtrizone-demo`, the default location.

## Geometry

`masterFactor: 0.5` and `headerHeight: 34` in `shell.qml` match the Lua demo's
`master_factor`, `left_header_height`, and `right_header_height`. Change both
configurations together. Each side is `screen.width * (1 - masterFactor) / 2`
logical pixels wide.

Each split panel uses `exclusiveZone: 0`. Hyprtrizone reserves space within its
side stacks. A positive layer-shell reservation would shorten the center too.

The panels use fixed geometry and assume no other panels reserve monitor space.
They appear on every monitor; the workspace highlight and title follow global
focus. They do not track runtime master resizing, layout changes, or special
workspaces. Fullscreen placement follows layer-shell behavior.

## Full-width mode

The shared `BarContents.qml` component also works with an ordinary Hyprland
layout:

```sh
QS_BAR_MODE=top quickshell -p examples/quickshell
```

This creates one full-width panel and reserves 34 pixels at the top. It works
independently of hyprtrizone. Stop the previous Quickshell instance before
switching modes.

## Validation

`./dev.sh vm-demo-screenshot` checks both modes in the demo VM. The split mode
must create two 640 by 34 panels on a 2560 by 1080 monitor, with no monitor-wide
reservation. The full-width mode must reserve 34 pixels at the top.
