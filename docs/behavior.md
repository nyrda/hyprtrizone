# Window behavior

Each workspace is split into three columns: a center column for the main
windows and a scrolling stack on either side. A side stack is a list of rows.
A row holds one window, or two when they are paired.

- [Placing new windows](#placing-new-windows)
- [Pairing](#pairing)
- [Scrolling](#scrolling)
- [Drag and drop](#drag-and-drop)
- [Keyboard movement](#keyboard-movement)
- [Resizing](#resizing)
- [Closing windows and focus](#closing-windows-and-focus)
- [Workspaces](#workspaces)
- [Fullscreen and special workspaces](#fullscreen-and-special-workspaces)
- [Known limitations](#known-limitations)

## Placing new windows

A new window is placed by the first rule that applies:

1. **The center is empty.** The window takes the center, whatever its tags.
2. **It has a center tag.** It joins the center. Untagged center windows move
   to their tagged side, or to `side_bias`. Several center-tagged windows share
   the center column, stacked vertically.
3. **It has a side tag.** It goes to that side.
4. **Anything else** goes to the side of the focused window. With the center
   focused, the side under the pointer is used, and otherwise `side_bias`.

On its side, the window opens below the focused row, or below the row under the
pointer. If neither is on that side, it opens at the end of the stack. The
stack scrolls just far enough to show the new window.

Windows that return to a workspace, for example after a layout switch or
through workspace following, keep their previous zone instead. A window
returning to the same workspace also gets back its row and pair.

## Pairing

Two windows can share a side row, each taking half its width. Pairing has to be
enabled for the side with `pairing_left` or `pairing_right`. With
`pairing_tag_only = true`, the default, both windows need a tag from
`pairing_tags`.

A pair only forms where a window is inserted. A new eligible window pairs with
the row it opens below if that row holds a single eligible window. Below a full
pair, it starts a new row. Other rows never move to make room.

When a pair member closes or leaves the side, its partner stays behind as a
single row. A window that moves to the other side joins the end of that stack
and can pair there under the same rule.

Pairs are checked on every layout pass. If pairing is turned off or a tag is
removed, the pair shows as two rows but is still remembered. Turning pairing
back on restores it.

## Scrolling

A side stack shows `side_visible_windows` rows. Further rows overflow and the
stack scrolls. The `scroll` command scrolls the focused side, and the
[demo config](../examples/hyprtrizone-demo.lua) binds it to Super and the mouse
wheel. With the center focused, the `side_bias` stack scrolls.

Moving focus does not scroll. `windowfocus` scrolls the focused window into
view. A window moved into a side, by keyboard, zone command, or drop into an
empty part of the column, scrolls its stack to the end.

Overflow badges count the windows completely scrolled out of view above and
below. With scrolling disabled, the rows of a stack shrink to fit the column.

## Drag and drop

Dragging with the `hl.dsp.window.drag()` mouse bind lifts a tiled window out of
the layout until it is dropped. The window under the pointer decides the
result:

| Dropped onto | Result |
| --- | --- |
| A center window, from a side | The two exchange places. The center window takes the dragged window's old slot, including its place in a pair. |
| A center window, from the center | The dragged window takes that position and the target moves down. |
| A single side window, both eligible to pair | They pair. The half of the target under the pointer decides which side the dragged window takes. |
| A single side window that cannot pair | The dragged window takes that row and the target moves down. If the dragged window came from a pair, the two exchange slots instead so that pair stays whole. |
| A member of a pair | The dragged window and the target exchange slots. The other pair member stays. |
| A side window, dragging the only center window | They exchange places, so the target becomes the center window. |
| An empty center | The window takes the center. |
| An empty part of a side column | The window becomes a single row at the end of that stack. |

Dropping a pair member onto its partner reorders the pair. A window dragged in
from another workspace has no old slot to exchange into, so the target becomes
its own row below instead.

The `dropon` command applies the same rules without a mouse; see
[runtime commands](configuration.md#zones-and-the-center).

## Keyboard movement

Hyprland's directional move, `hl.dsp.window.move`, is handled by the layout.

- **Left and right** move a window between columns. A window entering a side
  joins the end of that stack. Within a pair, moving toward the partner
  exchanges the two; moving away leaves the side.
- **Up and down** exchange the window with the neighboring row in its column.
  Between two pairs, the window keeps its left or right half.

The layout does not move a window past the outer columns or the ends of a
stack.

```lua
for _, direction in ipairs({ "left", "right", "up", "down" }) do
    hl.bind("SUPER + SHIFT + " .. direction, hl.dsp.window.move({ direction = direction }))
end
hl.bind("SUPER + mouse:272", hl.dsp.window.drag(), { mouse = true })
hl.bind("SUPER + mouse:273", hl.dsp.window.resize(), { mouse = true })
```

## Resizing

Resizing horizontally changes the center width. Both side columns change by the
same amount. The center stays between 10 and 90 percent of the work area. The
new width belongs to that workspace until its layout is recreated; see
[`master_factor`](configuration.md#columns).

Resizing vertically changes relative heights:

- **In the center**, with more than one center window. The last window takes
  the remaining height.
- **In a side stack**, where it moves height between rows. Both members of a
  pair resize together, and the total height of the stack stays the same.

Heights belong to positions in the column rather than to windows, so a window
moved elsewhere leaves its height behind.

## Closing windows and focus

When the last center window closes, the top window of the `side_bias` stack
takes its place, or the top window of the other side. Windows tagged for
workspace following are not promoted.

After a window closes, focus goes to its pair partner, then to the next window
in its column, then to the previous one, and finally to any remaining window.

## Workspaces

Hyprland creates a separate layout for each workspace. Rows, scroll offsets,
and center width are per workspace. The runtime scrolling and pairing overrides
apply everywhere.

Windows tagged with `follow_workspace_tags` follow you. When a workspace with a
positive number becomes active, tagged tiled windows from the other hyprtrizone
workspaces on that monitor move to it. Each keeps its zone and goes to the top
of its column. Floating, pinned, hidden, and unmapped windows stay behind, and
named workspaces do not pull windows.

## Fullscreen and special workspaces

While a fullscreen window covers the workspace, Hyprland places it and the
layout leaves it alone. Closing, dragging, floating, or moving the window to
another workspace ends its fullscreen state.

On a special workspace, every window is scaled around its own center by
`special_scale_factor`. Overflow badges are drawn for the monitor's active and
special workspaces.

## Known limitations

- The center can end up empty. Moving the only center window out with a
  keyboard move, `movetozone`, or a drop into an empty side column does not
  promote a replacement. `removemaster` refuses to do this.
- A window that restores its side zone, for example through workspace
  following, keeps that zone even if the destination has no center window.
