# Configuration

Hyprtrizone is configured through `plugin.hyprtrizone` in `hl.config`. Load the
plugin before these settings; otherwise Hyprland rejects the unknown options.
The [demo config](../examples/hyprtrizone-demo.lua) uses most of them.

```lua
hl.config({
    general = { layout = "hyprtrizone" },
    plugin = { hyprtrizone = {
        master_factor = 0.5,
        side_bias = "right",
        side_visible_windows = 3,
        pairing_right = true,
        pairing_tags = "pair",
    } },
})
```

Defaults are registered in `src/pluginConfig.cpp`. Through `hyprctl`, options
are named `plugin:hyprtrizone:<option>`.

- [Columns](#columns)
- [Scrolling](#scrolling)
- [Tags](#tags)
- [Overflow indicators](#overflow-indicators)
- [Runtime commands](#runtime-commands)

## Columns

| Option | Type | Default | Effect |
| --- | --- | --- | --- |
| `master_factor` | float | `0.5` | Center width as a fraction of the work area. The side columns share the rest equally. |
| `side_bias` | string | `"right"` | Default side for new windows, displaced center windows, and scrolling while the center has focus. |
| `left_header_height` | int | `0` | Space reserved at the top of the left column, in logical pixels. |
| `right_header_height` | int | `0` | Space reserved at the top of the right column, in logical pixels. |
| `special_scale_factor` | float | `0.8` | Scale of every window on a special workspace, around its own center. |

`master_factor` is read when Hyprland creates a workspace's layout. Horizontal
resizing then changes that workspace's value, clamped to `0.1` through `0.9`.
Configuration reloads, layout switches, and plugin reloads return to the
configured value.

`side_bias` is `left` for `left`, `l`, or `0`. Every other value selects the
right side, including misspellings and different capitalization.

Header heights shorten only their side column; the center keeps the full height.
This lets a panel sit above a side stack without a monitor-wide reservation. The
[Quickshell example](../examples/quickshell/README.md) shows this setup. Header
heights are clamped between zero and the column height.

## Scrolling

| Option | Type | Default | Effect |
| --- | --- | --- | --- |
| `scrolling_enabled` | bool | `true` | Let side stacks overflow and scroll. When off, every row fits into its column. |
| `side_visible_windows` | int | `3` | Rows visible before a stack overflows. A pair counts as one row. Values below `1` become `1`. |
| `left_side_visible_windows` | int | `0` | Left-side override. `0` or less uses `side_visible_windows`. |
| `right_side_visible_windows` | int | `0` | Right-side override. `0` or less uses `side_visible_windows`. |
| `scroll_step` | float | `0.35` | Distance per scroll command as a fraction of one row. At least `0.05`. |
| `wheel_scroll_mods` | string | `""` | Modifiers that make the mouse wheel scroll a side stack, written like in a bind, for example `"SUPER"`. Empty turns wheel scrolling off. |
| `wheel_scroll_step` | float | `0.5` | Distance per wheel notch as a fraction of one row. At least `0.05`. |
| `wheel_scroll_acceleration` | float | `0.5` | Step growth per notch while the stack is still moving. `0` keeps every notch the same. |

With fewer rows than the visible count, rows grow to fill the column. Once a
stack overflows, a row is the column height divided by the visible count, and
the stack scrolls. Vertical resizing redistributes height between rows. It does
not change the total height of the stack.

Each workspace keeps its own scroll offsets. The `togglescrolling` and
`setscrolling` commands override `scrolling_enabled` for every workspace.

### Wheel scrolling

While exactly the `wheel_scroll_mods` modifiers are held, the mouse wheel
scrolls the side stack under the pointer. Over the center it scrolls the
focused side, or `side_bias` while the center has focus. The plugin handles
every notch itself and none reach the window under the pointer, however fast
the wheel turns. High-resolution wheels scroll in proportion to each fraction
of a notch.

A notch that arrives while the stack is still moving from the previous one
continues from that target, and its step grows by `wheel_scroll_acceleration`
per notch, up to four times `wheel_scroll_step`. Changing direction, a pause of
a second, reaching either end, or any other scroll of the stack starts over at
one step. Without window animations every notch moves one step.

Wheel scrolling follows the conditions of a bind: it is inactive inside a
submap, on a locked session, during input capture, and while an application
inhibits shortcuts. Prefer it over `mouse_up` and `mouse_down` binds to the
`scroll` command: Hyprland runs one wheel bind per `binds:scroll_event_delay`
(300 ms by default) and passes the notches in between to the window under the
pointer.

## Tags

| Option | Type | Default | Effect |
| --- | --- | --- | --- |
| `zone_tags_left` | string | `""` | Place matching windows in the left column. |
| `zone_tags_center` | string | `""` | Place matching windows in the center column. |
| `zone_tags_right` | string | `""` | Place matching windows in the right column. |
| `pairing_left` | bool | `false` | Allow two eligible windows to share a left-side row. |
| `pairing_right` | bool | `false` | Allow two eligible windows to share a right-side row. |
| `pairing_tag_only` | bool | `true` | Only windows with a tag from `pairing_tags` are eligible. When `false`, every side window is eligible. |
| `pairing_tags` | string | `"pair"` | Tags that make a window eligible for pairing. |
| `follow_workspace_tags` | string | `""` | Tiled windows with these tags move to each numbered workspace you switch to on their monitor. |

Tag lists are comma-separated, and spaces around entries are ignored. A window
needs one tag from the list. Two paired windows can carry different tags from
`pairing_tags`.

When a window has several zone tags, center wins over left, and left wins over
right. The first window on a workspace always takes the center, whatever its
tags. [Window behavior](behavior.md) describes the placement order in full.

Tags come from Hyprland window rules:

```lua
hl.config({ plugin = { hyprtrizone = {
    zone_tags_center = "game",
    zone_tags_left = "browser",
    pairing_right = true,
    pairing_tags = "pair",
    follow_workspace_tags = "follow",
} } })

hl.window_rule({ name = "zone-games", match = { class = [[^(steam_app_.*)$]] }, tag = "+game" })
hl.window_rule({ name = "zone-browsers", match = { class = [[^(firefox|chromium)$]] }, tag = "+browser" })
hl.window_rule({ name = "pair-terminals", match = { class = [[^(kitty|alacritty|foot|wezterm)$]] }, tag = "+pair" })
hl.window_rule({ name = "follow-music", match = { class = [[^(spotify)$]] }, tag = "+follow" })
```

Tags only apply when a window is placed. After changing the focused window's
tags, `rezone` places it again:

```lua
hl.bind("SUPER + G", hl.dsp.window.tag({ tag = "+game" }))
hl.bind("SUPER + G", hl.dsp.layout("rezone"))
```

## Overflow indicators

A badge at the top or bottom of a side stack counts the windows scrolled
completely out of view in that direction. A pair counts as two windows;
partially visible rows are not counted.

| Option | Type | Default | Effect |
| --- | --- | --- | --- |
| `indicator_width` | int | `112` | Width in logical pixels, from `20` up to the column width. |
| `indicator_height` | int | `40` | Height in logical pixels, from `14` up to a third of the column height. |
| `indicator_font_size` | int | `24` | Count font size, `8` through `72`. |
| `indicator_padding` | int | `8` | Distance from the top or bottom of the column. |
| `indicator_opacity` | float | `0.78` | Background opacity, `0.05` through `1.0`. |
| `indicator_text_opacity` | float | `1.0` | Text opacity, `0.0` through `1.0`. |
| `indicator_text_color` | color | `"rgba(ffffffff)"` | Count text color. |
| `indicator_show_zero` | bool | `false` | Show badges with a count of zero. |

Badges have a dark, rounded background and are centered horizontally in their
column. With scrolling disabled, every row is visible and counts are zero.

## Runtime commands

Commands go through Hyprland's layout dispatcher. Bind them with
`hl.dsp.layout`:

```lua
hl.bind("SUPER + SHIFT + Page_Up", hl.dsp.layout("scroll up"))
hl.bind("SUPER + SHIFT + Page_Down", hl.dsp.layout("scroll down"))
hl.bind("SUPER + SPACE", hl.dsp.layout("swapwithmaster"))
```

Or send them from a shell:

```sh
hyprctl dispatch 'hl.dsp.layout("scroll right down 2")'
```

Arguments are separated by single spaces and are case-sensitive. Unknown
commands and arguments are ignored without an error. Commands that act on a
window use the focused tiled window of the active workspace and do nothing
without one.

### Scrolling

| Command | Effect |
| --- | --- |
| `scroll [left\|right] up\|down [amount]` | Scroll a side stack. Without a side, scroll the focused side, or `side_bias` while the center has focus. |
| `scrollup [left\|right\|amount]` | Scroll up. Takes either a side or an amount. |
| `scrolldown [left\|right\|amount]` | Scroll down. Takes either a side or an amount. |
| `windowfocus` | Scroll until the focused side window is fully visible. |
| `togglescrolling` | Toggle scrolling on every workspace. |
| `setscrolling on\|off\|default` | Override scrolling on every workspace, or return to `scrolling_enabled`. |

The arguments of `scroll` must be in the order shown. `amount` multiplies
`scroll_step` and defaults to `1.0`. Turning scrolling off resets all scroll
offsets. `setscrolling` also accepts `1`/`true`, `0`/`false`, and `config` as
an alias for `default`.

### Zones and the center

| Command | Effect |
| --- | --- |
| `movetozone left\|right\|center` | Move the focused window to a zone. |
| `swapzone` | Move the focused side window to the other side. |
| `swapwithmaster` | Exchange the focused side window with the first center window. With the center focused, focus a side window instead. |
| `focusmaster` | Focus the first center window. |
| `promote` | Make the focused side window the only center window. The previous center windows go to `side_bias`. |
| `addmaster` | Add the focused side window to the center column. |
| `removemaster` | Move the focused center window to `side_bias` if the center has other windows. |
| `rezone` | Place the focused window again by its zone tags. Does nothing without a zone tag. |
| `dropon <source> <target> [left\|right]` | Drop one window onto another, as with the mouse. See below. |

`focusmaster` and `swapwithmaster` also move the pointer to the focused window.

`dropon` takes two window selectors, such as `title:^editor$` or
`address:0x1234`. Selectors cannot contain spaces. The last argument names the
half of the target the drop lands on; anything but `left` means `right`. The
result follows the [drag-and-drop rules](behavior.md#drag-and-drop). The
integration test uses this command to check drag-and-drop placement.

### Pairing

| Command | Effect |
| --- | --- |
| `pairingleft on\|off\|toggle\|default` | Override `pairing_left` on every workspace. |
| `pairingright on\|off\|toggle\|default` | Override `pairing_right` on every workspace. |

These also accept `1`/`true`, `0`/`false`, and `config` as an alias for
`default`. Turning pairing off splits pairs into rows on screen but remembers
them. Turning it on again restores those pairs; it does not form new ones.
