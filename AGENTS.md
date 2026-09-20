# AGENTS.md

This is hyprtrizone, a personal Hyprland layout plugin targeting Hyprland 0.56.2.

## Build and validate

Use `./dev.sh` for routine work:

```sh
./dev.sh build
./dev.sh make all
./dev.sh vm-test
./dev.sh vm-demo-screenshot
```

`./dev.sh reload` changes the running desktop session. The VM commands provide
isolated validation. `vm-demo` opens the graphical demo and `vm-test-interactive`
opens the test driver.

`HYPRLAND_ROOT` defaults to `../Hyprland`. Use a matching checkout for API
reference. The helper uses its Nix shell only if `hyprland.dev` is already built;
otherwise it falls back to the nixpkgs pin in `flake.lock`.

## Files

- `src/main.cpp`: plugin registration, event listeners, workspace following.
- `src/pluginConfig.cpp/.hpp`: typed configuration and access helpers.
- `src/hyprtrizoneLayout.cpp/.hpp`: placement, scrolling, pairing, drag and drop, runtime commands.
- `src/StackIndicatorPassElement.cpp/.hpp`: overflow indicator render pass.
- `examples/`: Lua config, window launcher, Quickshell example.
- `nix/package.nix`: plugin derivation for the flake and downstream `callPackage`.
- `nix/module.nix`: matching Hyprland package and generated Lua loader.
- `nix/hyprtrizone-vm-test.nix`: headless layout integration test.
- `nix/hyprtrizone-vm-demo.nix`: demo VM and Quickshell screenshot test.
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

Public identifiers are `hyprtrizone` for the plugin and layout,
`plugin:hyprtrizone:*` for options, and `hyprtrizoneLayout.so` for the artifact.
