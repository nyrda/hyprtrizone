# Development

Run `./dev.sh` from the repository root. It sets up the build environment as
described under [manual build](installation.md#manual-build).

| Command | Purpose |
| --- | --- |
| `./dev.sh build` | Clean build of `hyprtrizoneLayout.so`. This is also the default command. |
| `./dev.sh make <args>` | Run `make` in the same environment, for example `./dev.sh make all`. |
| `./dev.sh reload` | Build and load the result into the running Hyprland session. |
| `./dev.sh vm-test` | Run the headless layout integration test. |
| `./dev.sh vm-test-interactive` | Open the integration test's interactive driver. |
| `./dev.sh vm-demo` | Boot the graphical demo VM. |
| `./dev.sh vm-demo-screenshot` | Test the demo session and update `docs/demo.png`. |
| `./dev.sh vm-demo-video` | Record the feature tour and update `docs/demo.mp4` and `docs/demo-video.png`. |

The VM commands need Nix and KVM, and pass extra arguments to `nix-build`. Only
`vm-demo` opens a window; the tests and recordings run unattended. Only
`reload` touches your desktop session.

## Reloading into a live session

`./dev.sh reload` builds, then runs `reload-plugin.sh`:

1. Switch every workspace to the `master` layout, so no workspace uses the old
   plugin code.
2. Unload earlier development copies and `hyprtrizoneLayout.so` from the
   repository root.
3. Copy the build to `hyprtrizoneLayout-live-<pid>.so` and load it. The unique
   name keeps `dlopen` from reusing the old library.
4. Switch the layout to `hyprtrizone`.

The script sets `general.layout` globally, whatever it was before. It does not
unload a copy loaded from elsewhere, such as the Nix store path used by the
NixOS module. Remove that from your configuration while developing.

## Tests

**Layout integration** (`nix/hyprtrizone-vm-test.nix`) runs Hyprland headless
with a 1920×1080 output and checks:

- tagged placement and the side header reservations,
- fullscreen entry, exit, and closing,
- pairing on insertion, and pairs splitting when a member leaves,
- keyboard moves between columns and rows,
- every drag-and-drop case, through `dropon`,
- switching layouts and back, and opening windows on another workspace,
- unloading and reloading the plugin with windows open.

**Demo** (`nix/hyprtrizone-vm-demo.nix`, attribute `screenshot`) boots the demo
session on a 2560×1080 virtual monitor. It checks that the Quickshell panels sit
above the side stacks without shortening the center, then captures
`docs/demo.png`. It also checks the full-width panel mode.

CI builds the package and runs both tests. The flake exposes them as
`checks.x86_64-linux.vm-test` and `checks.x86_64-linux.demo`.

## Demo video

`./dev.sh vm-demo-video` builds the `video` attribute of
`nix/hyprtrizone-vm-demo.nix`. It boots the demo session on a 3600×1080 (30:9)
virtual monitor, with animations enabled, and runs a scripted tour:

1. Zone placement from tags and `side_bias`.
2. Overflowing and scrolling side stacks.
3. Pairing on insertion.
4. Changing the right side's row count at runtime.
5. Drag and drop within a side stack, to pair two windows and to swap a pair.
6. `swapwithmaster` and back.
7. Keyboard moves within a pair, across columns, and between rows.
8. Narrowing and restoring the center.
9. Workspace following.
10. Toggling scrolling off and on.

The title cards play in real time. Everything between them is encoded at 1.5×
speed (`TOUR_SPEED` in `tour.py`), so the script can leave comfortable pauses
without slowing the video.

The tour uses real input. Keys go through QEMU, and pointer drags go through
`ydotool`. A Quickshell overlay adds captions and the title cards; the tour
controls it over Quickshell IPC. `wf-recorder` captures the session inside the
VM, and `ffmpeg` on the host encodes the final H.264 video and a poster frame.

| File | Contents |
| --- | --- |
| `nix/demo-video/default.nix` | VM setup, Hyprland additions, and the test derivation |
| `nix/demo-video/tour.py` | The scripted tour, recording, and encoding |
| `nix/demo-video/overlay.qml` | Captions and title cards |
| `nix/demo-video/window.sh` | The labeled terminals shown in the tour |

The recording takes a few minutes and uses 8 VM cores. Nix reuses the previous
result until one of these files changes. The tour asserts the layout after
each step, so a behavior change fails the recording instead of producing a
misleading video.

## Source map

| Path | Responsibility |
| --- | --- |
| `src/main.cpp` | Plugin registration, render hook, workspace following, shutdown |
| `src/pluginConfig.cpp` | Option registration and typed access |
| `src/hyprtrizoneLayout.cpp` | Placement, scrolling, pairing, movement, drag and drop, runtime commands |
| `src/StackIndicatorPassElement.cpp` | Render pass element for the overflow badges |
| `examples/` | Demo Lua config, window launcher, Quickshell panels |
| `nix/package.nix` | Plugin derivation, also usable with `callPackage` |
| `nix/module.nix` | NixOS module and Lua loader |
| `nix/hyprtrizone-vm-test.nix` | Layout integration test |
| `nix/hyprtrizone-vm-demo.nix` | Demo VM, screenshot test, and video entry point |
| `nix/demo-video/` | Demo video tour |

Hyprland creates one layout instance per workspace. Each instance owns its
nodes, scroll offsets, and center width. Caches at file scope, keyed by stable
window ID, keep a window's zone, row, pair, and follow state across layout
switches and workspace moves. A registry of live instances serves rendering
and the global scrolling toggles.

## Updating Hyprland

1. Update the nixpkgs input in `flake.nix` and `flake.lock`.
2. Port the plugin against that Hyprland. A matching checkout at
   `HYPRLAND_ROOT` helps with API reference.
3. Update the expected version in `nix/hyprtrizone-vm-test.nix`.
4. Run the package build and both VM tests, then regenerate the screenshot and
   video.
5. Update the version in the documentation.

Keep behavior changes out of dependency ports. `setPositionGlobal` already
applies Hyprland's gaps, and pairing must stay local to the insertion point.
