# Configuration

Load the plugin before setting `plugin.hyprtrizone` in `hl.config`. The
[demo config](../examples/hyprtrizone-demo.lua) provides a working example.

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

All defaults below come from `src/pluginConfig.cpp`. Option names use the
`plugin:hyprtrizone:` prefix when queried through Hyprland.

## Columns and scrolling

| Option | Type | Default | Effect |
| --- | --- | --- | --- |
| `master_factor` | float | `0.5` | Center width as a fraction of the work area, clamped to `0.1` through `0.9`. The side columns share the remaining width equally. |
| `special_scale_factor` | float | `0.8` | Window scale on special workspaces. |
| `side_bias` | string | `"right"` | Default side for placement and scrolling with center focus. Accepts `left`, `l`, `0`, `right`, `r`, or `1`. |
| `scrolling_enabled` | bool | `true` | Allow side stacks to overflow and scroll. When disabled, rows fit within the column. |
| `side_visible_windows` | int | `3` | Fallback visible row count before overflow. A pair counts as one row. |
| `left_side_visible_windows` | int | `0` | Left-side override; `0` uses the fallback. |
| `right_side_visible_windows` | int | `0` | Right-side override; `0` uses the fallback. |
| `scroll_step` | float | `0.35` | Distance per scroll command as a fraction of a base row height. |
| `left_header_height` | int | `0` | Reserved space at the top of the left column, in logical pixels. |
| `right_header_height` | int | `0` | Reserved space at the top of the right column, in logical pixels. |

Side reservations leave the center at full height. The
[Quickshell example](../examples/quickshell/README.md) uses them to place panels
above the side windows without reserving space across the monitor.

Rows expand when fewer than the visible count exist. Once a stack overflows,
the base row height is its available content height divided by the configured
visible count. Vertical resizing adjusts a row's relative height.

## Tags

| Option | Type | Default | Effect |
| --- | --- | --- | --- |
| `zone_tags_left` | string | `""` | Place matching windows in the left column. |
| `zone_tags_center` | string | `""` | Place matching windows in the center column. |
| `zone_tags_right` | string | `""` | Place matching windows in the right column. |
| `follow_workspace_tags` | string | `""` | Move matching tiled windows to newly activated numeric workspaces on the same monitor. |
| `pairing_left` | bool | `false` | Allow two eligible windows to share a left-side row. |
| `pairing_right` | bool | `false` | Allow two eligible windows to share a right-side row. |
| `pairing_tag_only` | bool | `true` | Require matching tags for pairing. |
| `pairing_tags` | string | `"pair"` | Tags that make windows eligible for pairing. |

Tag lists are comma-separated. Zone priority is center, then left, then right.
The first ordinary window occupies center even when tagged for a side. Cached
placement and workspace-follow moves have their own restoration rules; see
[window behavior](behavior.md).

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
```

Following moves a window between workspaces. Named or special destination
workspaces do not trigger it. Floating, pinned, hidden, and unmapped windows are
excluded. A followed window retains its zone and goes to the front of that zone
on the destination workspace.

To reassign the focused window after changing its tags:

```lua
hl.bind("SUPER + g", hl.dsp.window.tag({ tag = "+game" }))
hl.bind("SUPER + g", hl.dsp.layout("rezone"))
```

## Overflow indicators

Indicators count windows outside the visible portion of a side stack.

| Option | Type | Default | Effect |
| --- | --- | --- | --- |
| `indicator_width` | int | `112` | Width in logical pixels. |
| `indicator_height` | int | `40` | Height in logical pixels. |
| `indicator_font_size` | int | `24` | Count font size. |
| `indicator_padding` | int | `8` | Inset from the top or bottom of the content area. |
| `indicator_opacity` | float | `0.78` | Background opacity, clamped to `0.05` through `1.0`. |
| `indicator_text_opacity` | float | `1.0` | Text opacity, clamped to `0.0` through `1.0`. |
| `indicator_text_color` | color | `"rgba(ffffffff)"` | Count text color. |
| `indicator_show_zero` | bool | `false` | Display indicators even when their count is zero. |

## Runtime commands

Bind commands with `hl.dsp.layout`:

```lua
hl.bind("SUPER + mouse_up", hl.dsp.layout("scroll up"))
hl.bind("SUPER + mouse_down", hl.dsp.layout("scroll down"))
hl.bind("SUPER + SPACE", hl.dsp.layout("swapwithmaster"))
```

Or invoke them from a shell:

```sh
hyprctl dispatch 'hl.dsp.layout("scroll right down 2")'
```

### Scrolling

| Command | Effect |
| --- | --- |
| `scroll up` / `scroll down` | Scroll the focused side, or `side_bias` with center focus. |
| `scroll left up` / `scroll left down` | Scroll the left stack. |
| `scroll right up` / `scroll right down` | Scroll the right stack. |
| `scrollup` / `scrolldown` | Aliases for scrolling the focused side. |
| `windowfocus` | Bring the focused side window fully into view. |
| `togglescrolling` | Toggle scrolling globally. Turning it off resets scroll offsets. |
| `setscrolling on\|off\|default` | Override scrolling globally, or return to the configured value. |

Scroll commands accept a final multiplier, such as `scroll right down 2.0`.
The default multiplier is `1.0`.

### Zones and center windows

| Command | Effect |
| --- | --- |
| `movetozone left\|right\|center` | Move the focused window to a zone. |
| `swapzone` | Move the focused side window to the opposite side. |
| `swapwithmaster` | Exchange the focused side window with the first center window. With center focus, focus the first side window. |
| `promote` | Move the focused window to center and move the other center windows to `side_bias`. |
| `addmaster` | Add the focused side window to the center column. |
| `removemaster` | Move the focused center window to `side_bias` when more than one center window exists. |
| `focusmaster` | Focus the first center window. |
| `rezone` | Apply the focused window's zone tags again. A center-tagged window displaces untagged center windows. |
| `dropon <source> <target> [left\|right]` | Apply mouse-drop placement to two window selectors. The half defaults to `right`. |

`dropon` accepts selectors such as `title:^terminal$` or `address:0x1234`.
It follows the [drag-and-drop rules](behavior.md#drag-and-drop) and is also used
by the integration test.

### Pairing

| Command | Effect |
| --- | --- |
| `pairingleft on\|off\|toggle\|default` | Override pairing on the left side for all workspaces. |
| `pairingright on\|off\|toggle\|default` | Override pairing on the right side for all workspaces. |

`default` returns to the configuration value.
