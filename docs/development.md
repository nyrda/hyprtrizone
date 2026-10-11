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

## Demo VM

`./dev.sh vm-demo` boots a disposable NixOS VM with Hyprland, a set of example
windows, and the [Quickshell panels](../examples/quickshell/README.md). It needs
KVM and a graphical session, and its disk is discarded on exit. The layout
settings come from [`examples/hyprtrizone-demo.lua`](../examples/hyprtrizone-demo.lua),
which also runs in a normal session. The VM adds the general Hyprland binds;
the example file enables wheel scrolling and binds the F-keys.

| Keys | Action |
| --- | --- |
| `Super+Return` | Open a terminal |
| `Super+Q` | Close the focused window |
| `Super+Arrows` | Move focus |
| `Super+Shift+Arrows` | Move the window between columns or rows |
| `Super+Left drag` | Drag a window; drop it onto another to pair or exchange |
| `Super+Right drag` | Resize |
| `Super+Wheel` | Scroll the side stack under the pointer; over the center, the focused side or the right one |
| `Super+1/2/3` | Switch workspace |
| `Super+Shift+F7` | Reopen the demo windows |
| `Super+Shift+F8/F9` | Toggle pairing on the left/right side |
| `Super+Shift+F10` | Toggle scrolling |
| `Super+M` | Exit Hyprland |

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
- grouping and ungrouping a window,
- switching layouts and back with rows and pairs intact, and opening windows
  on another workspace,
- unloading and reloading the plugin with windows open.

**Demo** (`nix/hyprtrizone-vm-demo.nix`, attribute `screenshot`) boots the demo
session on a 2560×1080 virtual monitor. It checks that the Quickshell panels sit
above the side stacks without shortening the center, then captures
`docs/demo.png`. It also checks the full-width panel mode.

The flake exposes both tests as `checks.x86_64-linux.vm-test` and
`checks.x86_64-linux.demo`. GitHub's runners are too small for them, so run
them locally. CI builds the package and only evaluates the tests.

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

The README plays the video from a GitHub attachment, because GitHub does not
play videos stored in the repository inline. After recording a new tour, drag
`docs/demo.mp4` into a GitHub comment box, copy the
`https://github.com/user-attachments/assets/…` URL it inserts without posting
the comment, and replace the URL in the README.

## Testing on Omarchy

[Omarchy](omarchy.md) is not covered by the Nix tests. To test there, install
it in a libvirt VM:

- Use `virtio-vga-gl` with 3D acceleration and SPICE with OpenGL. In the guest,
  Hyprland's log shows a `virgl` renderer instead of `llvmpipe`.
- The Omarchy ISO installs without the wizard when a drive labelled `cidata` is
  attached. The drive carries the wizard's output files:
  `user_configuration.json` (disk, hostname, timezone, keyboard) and
  `user_credentials.json` (user and password hash). An `authorized_keys` file
  enables SSH access. The ISO repository's README lists every file, and
  `test/integration.d/base-test.sh` there generates a complete set.
- Boot the disk first and the ISO second. The empty disk falls through to the
  ISO, and the installed system boots from the disk afterwards. Detach both
  drives once the install is done.
- Copy the working tree into the guest and run `make`. Then install the result
  as described in [Omarchy](omarchy.md).

## Source map

| Path | Responsibility |
| --- | --- |
| `src/main.cpp` | Plugin registration, render hook, workspace following, shutdown |
| `src/pluginConfig.cpp` | Option registration and typed access |
| `src/hyprtrizoneLayout.cpp` | Placement, scrolling, pairing, movement, drag and drop, runtime commands |
| `src/StackIndicatorPassElement.cpp` | Render pass element for the overflow badges |
| `examples/` | Demo Lua config, window launcher, Quickshell panels, Omarchy config |
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

## Hyprland versions and branches

Each branch targets one Hyprland release, named in `HYPRLAND_VERSION`:

| Branch | Targets |
| --- | --- |
| `master` | The newest release the plugin is ported to |
| `hyprland-<version>` | That release, for example `hyprland-0.56.2` |

Every supported release has its own branch; the newest one points at the same
commit as `master` until `master` moves on. The Makefile stops when
`pkg-config` finds a different Hyprland, the Nix test fails to evaluate when
the nixpkgs in `flake.lock` has a different one, and CI runs on `master` and every
`hyprland-*` branch. Distribution users build the branch for their installed
Hyprland; the [Omarchy update hook](omarchy.md#updates) picks it automatically.

### Fixes

Commit fixes on `master`, then cherry-pick them onto the release branches that
still get updates:

```sh
git switch hyprland-0.56.1
git cherry-pick <commit>
./dev.sh vm-test
```

The branch of the newest release fast-forwards instead, as long as `master`
has not moved on to another release:

```sh
git switch hyprland-0.56.2
git merge --ff-only master
```

Never merge `master` into a release branch otherwise. Once `master` is ported to a newer
Hyprland, the merge brings that port along and the branch no longer builds.
Keep a release branch updated while the distributions you care about still
ship its release. Omarchy's stable channel runs about a month behind Arch.

### Tags

A tag marks a tested state of a release branch: `v<Hyprland version>-<n>`, for
example `v0.57.0-1`, where `n` counts the plugin's own releases for that
Hyprland release. Tag a release branch after fixes that users should pick up,
with the next `n`:

```sh
git tag -a v0.57.0-2 hyprland-0.57.0 -m "hyprtrizone for Hyprland 0.57.0, revision 2"
git push origin v0.57.0-2
```

Tags never use the branch names: a tag `hyprland-0.57.0` next to the branch of
the same name makes the plain name ambiguous for Git and Nix flake references.
Nix users can pin a tag with `github:nyrda/hyprtrizone/v0.57.0-1`.

### Porting to a new release

1. Branch the current state: `git branch hyprland-<current version> master`,
   unless that branch already points there.
2. On `master`, point the `nixpkgs` input in `flake.nix` at a nixpkgs
   revision whose `hyprland` is the new release
   (`github:NixOS/nixpkgs/<commit>`), run `nix flake lock`, and write the new
   version to `HYPRLAND_VERSION`.
3. Port the plugin. A matching checkout at `HYPRLAND_ROOT` helps with API
   reference. `make IGNORE_HYPRLAND_VERSION=1` builds against other headers
   while porting.
4. Run the package build and both VM tests, then regenerate the screenshot and
   video, and replace the video attachment in the README.
5. Update the version in the documentation.
6. Create `hyprland-<new version>` at the new `master`, and tag that commit
   `v<new version>-1`.

A release that needs no code changes, typically a patch release, still gets its
own branch with its own pin and version file, so its tests run against it.

Until nixpkgs has a release that needs this port, the Nix builds and tests use
Hyprland's own flake at a `main` commit, built with its pinned nixpkgs and
compiler (`nix/pkgs.nix`). Pick a commit Hyprland's CI has published to
hyprland.cachix.org. CI never compiles Hyprland: its first step fetches it
with local builds disabled and fails when the cache does not have it.

Keep behavior changes out of dependency ports. `setPositionGlobal` already
applies Hyprland's gaps, and pairing must stay local to the insertion point.
