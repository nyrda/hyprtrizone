# Development

Use `./dev.sh` from the repository root. The helper handles local pkg-config
paths and the pinned Nix build environment.

| Command | Purpose |
| --- | --- |
| `./dev.sh build` | Clean build of `hyprtrizoneLayout.so` |
| `./dev.sh make all` | Incremental build with the same environment handling |
| `./dev.sh reload` | Build, switch to `master`, unload/reload the plugin, then select `hyprtrizone` |
| `./dev.sh vm-test` | Run the headless NixOS integration test |
| `./dev.sh vm-test-interactive` | Open the NixOS test driver |
| `./dev.sh vm-demo` | Boot the disposable graphical demo |
| `./dev.sh vm-demo-screenshot` | Test the demo and regenerate `docs/demo.png` |

The VM commands require Nix and KVM. The graphical demo also needs a display;
the integration and screenshot tests run unattended. Build and test commands do
not reload the plugin in your desktop session.

## Validation

The layout test checks tagged placement, side reservations, fullscreen entry and
exit, window teardown, pairing, row movement, drag-and-drop placement through
`dropon`, layout switching, launching on another workspace, and plugin unload/reload
with windows open.

The demo test boots the example config and Quickshell. It checks split-panel
geometry, the center work area, and the full-width panel mode, then captures
screenshots. Its primary screenshot is used in the README.

CI runs a package build followed by both VM tests. The equivalent flake checks
are `checks.x86_64-linux.vm-test` and `checks.x86_64-linux.demo`.

## Source map

| Path | Responsibility |
| --- | --- |
| `src/main.cpp` | Registration, render listener, workspace following, shutdown |
| `src/pluginConfig.cpp` | Typed configuration registration and access |
| `src/hyprtrizoneLayout.cpp` | Placement, scrolling, pairing, movement, runtime commands |
| `src/StackIndicatorPassElement.cpp` | Render pass for overflow indicators |
| `examples/` | Lua config, window launcher, and Quickshell panels |
| `nix/package.nix` | Plugin derivation, also usable with `callPackage` |
| `nix/module.nix` | NixOS module and Lua loader |
| `nix/hyprtrizone-vm-test.nix` | Layout integration test |
| `nix/hyprtrizone-vm-demo.nix` | Demo VM and screenshot test |

Hyprland creates one layout algorithm per workspace. Each instance owns its
nodes and scroll offsets. File-scope caches, keyed by stable window ID, preserve
zone, order, and pairing across lifecycle changes. The global instance registry
supports rendering and recalculation.

## Updating Hyprland

Use a matching Hyprland checkout for API reference. `HYPRLAND_ROOT` defaults to
`../Hyprland`; its Nix shell is used only when the `hyprland.dev` output already
exists. Otherwise the helper uses the package set pinned in `flake.lock`.

Update the nixpkgs input in `flake.nix` and its lock, port against that
compositor, and update the expected version in the VM test. Run the package
build and both VM tests before updating the documented version.

Keep placement changes separate from dependency ports. `setPositionGlobal`
already applies Hyprland's gaps. Pairing is local to insertion and must not
rearrange unrelated rows.
