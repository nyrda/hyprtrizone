# AGENTS.md

This is hyprtrizone, a personal Hyprland layout plugin targeting Hyprland 0.56.2.

## Build and validate

Use `./dev.sh` for routine work:

```sh
./dev.sh build
./dev.sh make all
./dev.sh vm-test
./dev.sh vm-demo-screenshot
./dev.sh vm-demo-video
```

`./dev.sh reload` changes the running desktop session. The VM commands provide
isolated validation. `vm-demo` opens the graphical demo and `vm-test-interactive`
opens the test driver. `vm-demo-video` records the 30:9 feature tour into
`docs/demo.mp4`; rerun it when a change affects what the tour shows. The README plays a GitHub
attachment copy of the video; the user re-uploads it after a new recording.

Each branch targets the Hyprland release in `HYPRLAND_VERSION`: `master` the newest,
`hyprland-<version>` one release each. Commit fixes on `master` and cherry-pick them onto
maintained release branches; never merge `master` into them. See "Hyprland versions and
branches" in `docs/development.md`.

`HYPRLAND_ROOT` defaults to `../Hyprland`. Use a matching checkout for API
reference. The helper uses its Nix shell only if `hyprland.dev` is already built;
otherwise it falls back to the nixpkgs revision pinned in `flake.lock` (`nix/pkgs.nix`).
Nix builds use that nixpkgs' Hyprland, prebuilt on cache.nixos.org. Never compile Hyprland in CI.
CI does not run the VM tests: GitHub's runners are too small for them. Run them locally.

## Files

- `src/main.cpp`: plugin registration, event listeners, workspace following.
- `src/pluginConfig.cpp/.hpp`: typed configuration and access helpers.
- `src/hyprtrizoneLayout.cpp/.hpp`: placement, scrolling, pairing, drag and drop, runtime commands.
- `src/StackIndicatorPassElement.cpp/.hpp`: overflow indicator render pass.
- `examples/`: Lua config, window launcher, Quickshell example, Omarchy config.
- `nix/package.nix`: plugin derivation for the flake and downstream `callPackage`.
- `nix/module.nix`: matching Hyprland package and generated Lua loader.
- `nix/hyprtrizone-vm-test.nix`: headless layout integration test.
- `nix/hyprtrizone-vm-demo.nix`: demo VM, Quickshell screenshot test, and video entry point.
- `nix/demo-video/`: scripted video tour, caption overlay, and recording.
- `docs/`: configuration, behavior, installation, and development references.

## Layout invariants

- Hyprland creates one algorithm per workspace. Nodes and scroll offsets belong to that instance.
- Stable-window-ID caches preserve zone, pair, order, and workspace-follow state across lifecycle changes.
- `SNodeData::zone` initially defaults to center. A new node is not a live master candidate before placement.
- `setPositionGlobal(CBox)` handles Hyprland gaps. Do not add manual gap math to the work area.
- Config access goes through `HyprtrizoneConfig` helpers.
- Tag checks use `m_ruleApplicator->m_tagKeeper.isTagged(...)`.
- Pairing is side-local and insertion-local. Do not reshuffle unrelated rows.
- Runtime scrolling and pairing overrides are global; stored scroll offsets are per workspace.
- `left_header_height` and `right_header_height` reserve top space only in the side columns. Keep these public names.
- Keep Quickshell in the demo VM and as a standalone example. Its fixed geometry matches the Lua demo config.
- The video tour asserts the layout after each step. Keep captions in `nix/demo-video/tour.py` true to the behavior shown.

Public identifiers are `hyprtrizone` for the plugin and layout,
`plugin:hyprtrizone:*` for options, and `hyprtrizoneLayout.so` for the artifact.
