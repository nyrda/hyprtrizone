# Window behavior

## New windows

A returning window first restores its cached placement. A restore on the same
workspace also restores its order and pairing state. Workspace-follow moves keep
their zone and insert at the front of that zone on the destination workspace.

Without restored state, the first window occupies center. A new window also
fills center if there is no master. Otherwise its zone tags take precedence over
the default placement rules. When a center-tagged window arrives, untagged center
windows move to their preferred side or the configured `side_bias`. Multiple
center-tagged windows share the center column vertically.

For a side window, insertion follows the focused row in that side, or the row
under the pointer. Without a side tag, the focused side window takes priority
over the pointer. If neither supplies a side, placement uses `side_bias`.

A window inserted after a complete pair starts a new row. An eligible window
inserted after an eligible single window can join its row. New side windows are
brought into view.

## Pairing

Two eligible windows can share a side row, each taking half its width. Pairing
must be enabled for that side. With `pairing_tag_only = true`, both windows must
carry a tag listed in `pairing_tags`.

Pairing only happens at the insertion point. Existing rows keep their members
and order. A closing or departing member leaves its partner in its own row.
Hyprland applies the normal window gaps to both members.

A window moving to another zone leaves its pair, joins the end of the destination
stack, and can pair there under the same insertion rules. `swapwithmaster`
exchanges windows in place, including the side window's position within a pair.

## Drag and drop

Hyprland temporarily floats a tiled window during a drag. On release, placement
depends on the target under the pointer.

| Drop target | Result |
| --- | --- |
| Empty zone | Place in that column. An occupied center falls back to `side_bias`. |
| Center window | Exchange the dragged window with that window. The former center takes the dragged window's side slot when available. |
| Member of a full pair | Exchange the source and target, keeping the other pair members in place. |
| Eligible single side window | Pair them. The pointer's half of the target determines the dragged window's position. |
| Single side window that cannot pair | Insert at that position and move the target down. If the dragged window came from a pair, exchange slots to preserve that row. |

Dropping a pair member onto its partner reorders that pair. A window arriving
from another workspace has no source slot in the destination. Where an exchange
needs that slot, the target becomes a separate row below instead.

## Keyboard movement

Hyprland's directional window movement moves left and right through the columns.
Within a pair, moving toward the partner exchanges the members first. Moving
away from the partner leaves the zone.

Up and down exchange the focused window with the neighboring row in the same
zone. When both rows are pairs, the window keeps its column.

```lua
for _, direction in ipairs({ "left", "right", "up", "down" }) do
    hl.bind("SUPER + SHIFT + " .. direction, hl.dsp.window.move({ direction = direction }))
end
hl.bind("SUPER + mouse:272", hl.dsp.window.drag(), { mouse = true })
hl.bind("SUPER + mouse:273", hl.dsp.window.resize(), { mouse = true })
```

## Resizing

Horizontal resizing changes the center width; both side columns resize
symmetrically. The center width stays between 10 and 90 percent of the monitor
width used by the resize calculation.

Vertical resizing changes a center window's relative height when several center
windows exist. In a side stack it changes the row height, including both members
of a pair. These heights affect the scrollable content size.

## Closing windows and focus

If removing a center window leaves center empty, a side window is promoted.
The configured `side_bias` is preferred, then the opposite side.

After closing a window, the layout suggests its pair partner first, then the next
window in the same zone, then the previous one. It falls back to any remaining
window if those candidates are gone.

## Workspace state

Each workspace owns its scroll offsets and layout nodes. Window caches preserve
placement during layout changes. Runtime scrolling and pairing overrides apply
to all instances.

A window tagged for workspace following moves to a newly active numeric workspace
on its current monitor. It retains its zone and goes to the front of that zone.
Named and special destination workspaces, floating windows, and pinned windows
do not trigger this behavior.
