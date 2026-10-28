#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HYPRLAND_ROOT="${HYPRLAND_ROOT:-${ROOT_DIR}/../Hyprland}"
# The legacy entry points load Hyprland with builtins.getFlake too.
NIX_FLAGS=(--extra-experimental-features "nix-command flakes")

usage() {
    cat <<'EOF'
Usage: ./dev.sh <command> [args]

Commands:
  build                  Build from clean state, auto-fixing local Nix pkg-config env.
  make <args...>         Run make with the same auto env handling.
  reload                 Build, then reload the plugin into the current Hyprland session.
  vm-test                Run the headless NixOS integration VM test.
  vm-test-interactive    Build and open the interactive NixOS test driver.
  vm-demo                Boot a throwaway NixOS VM running Hyprland with the plugin and examples/ demo.
  vm-demo-screenshot     Run that demo VM unattended and save its screenshot to docs/demo.png.
  vm-demo-video          Record the 30:9 feature tour in the demo VM and save it to docs/demo.mp4.
  help                   Show this help.

Environment:
  HYPRLAND_ROOT          Matching Hyprland checkout, used for its flake dev shell if its dev output
                         is built. Default: ../Hyprland next to this repo.
  LUA_PKG                Lua pkg-config name. Default: detected by the Makefile.
EOF
}

log() {
    printf '==> %s\n' "$*" >&2
}

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

has_ambient_pkg_config() {
    command -v pkg-config >/dev/null 2>&1 || return 1
    pkg-config --exists pixman-1 libdrm hyprland libeis-1.0
}

nix_pkg_config_path() {
    command -v nix >/dev/null 2>&1 || die "nix is required when pkg-config cannot see Hyprland deps"
    [[ -f "${HYPRLAND_ROOT}/flake.nix" ]] || return 1

    local system hyprland_dev
    system="$(nix "${NIX_FLAGS[@]}" eval --impure --raw --expr builtins.currentSystem)"
    hyprland_dev="$(nix "${NIX_FLAGS[@]}" eval --raw "${HYPRLAND_ROOT}#packages.${system}.hyprland.dev.outPath")" || return 1
    # Evaluating outPath does not build it; after a Hyprland bump the dev output may not exist yet.
    if [[ ! -e "${hyprland_dev}/share/pkgconfig/hyprland.pc" ]]; then
        log "Hyprland flake dev output is not built (to use it: nix build --no-link ${HYPRLAND_ROOT}#hyprland.dev)"
        return 1
    fi
    printf '%s' "${hyprland_dev}/share/pkgconfig"
}

run_make() {
    cd "$ROOT_DIR"

    if has_ambient_pkg_config; then
        log "make $*"
        make "$@"
        return
    fi

    local pc_path had_precommit=0 had_precommit_hook=0
    [[ -e "${ROOT_DIR}/.pre-commit-config.yaml" ]] && had_precommit=1
    [[ -e "${ROOT_DIR}/.git/hooks/pre-commit" ]] && had_precommit_hook=1
    if ! pc_path="$(nix_pkg_config_path)"; then
        # Fall back to Hyprland's package set at the commit pinned in flake.lock (nix/pkgs.nix).
        log "make $* inside the pinned Hyprland's build shell (nix/hyprtrizone-vm-test.nix)"
        nix-shell "${NIX_FLAGS[@]}" "${ROOT_DIR}/nix/hyprtrizone-vm-test.nix" -A plugin --run "make $(printf '%q ' "$@")"
        return
    fi

    log "make $* inside Hyprland Nix dev shell"

    local status=0
    set +e
    nix "${NIX_FLAGS[@]}" develop "$HYPRLAND_ROOT" -c bash -lc '
        root_dir="$1"
        pc_path="$2"
        shift 2
        cd "$root_dir"
        export PKG_CONFIG_PATH="${pc_path}:${PKG_CONFIG_PATH:-}"
        exec make "$@"
    ' bash "$ROOT_DIR" "$pc_path" "$@"
    status=$?
    set -e

    # The Hyprland dev shell installs its git hooks into the current repo; undo that.
    if [[ "$had_precommit" -eq 0 ]]; then
        rm -f "${ROOT_DIR}/.pre-commit-config.yaml"
    fi
    if [[ "$had_precommit_hook" -eq 0 ]]; then
        rm -f "${ROOT_DIR}/.git/hooks/pre-commit"
    fi

    return "$status"
}

build() {
    # Clean before invoking make: stale build/*.d files from an older Hyprland break parsing of "clean all".
    rm -rf "${ROOT_DIR}/build" "${ROOT_DIR}/hyprtrizoneLayout.so"
    run_make all
}

reload_plugin() {
    build
    "${ROOT_DIR}/reload-plugin.sh"
}

vm_test() {
    command -v nix-build >/dev/null 2>&1 || die "nix-build is required for the VM test"
    cd "$ROOT_DIR"
    nix-build "${NIX_FLAGS[@]}" nix/hyprtrizone-vm-test.nix "$@"
}

vm_test_interactive() {
    command -v nix-build >/dev/null 2>&1 || die "nix-build is required for the VM test"
    cd "$ROOT_DIR"
    nix-build "${NIX_FLAGS[@]}" nix/hyprtrizone-vm-test.nix -A driverInteractive "$@"
    exec ./result/bin/nixos-test-driver
}

vm_demo() {
    command -v nix-build >/dev/null 2>&1 || die "nix-build is required for the VM demo"
    cd "$ROOT_DIR"
    nix-build "${NIX_FLAGS[@]}" nix/hyprtrizone-vm-demo.nix -A vm -o result-vm-demo "$@"
    log "starting the demo VM (SUPER+M exits Hyprland; its disk image is discarded afterwards)"
    local tmp
    tmp="$(mktemp -d)"
    (cd "$tmp" && "${ROOT_DIR}/result-vm-demo/bin/run-hyprtrizone-demo-vm")
    rm -rf "$tmp"
}

vm_demo_screenshot() {
    command -v nix-build >/dev/null 2>&1 || die "nix-build is required for the VM demo"
    cd "$ROOT_DIR"
    nix-build "${NIX_FLAGS[@]}" nix/hyprtrizone-vm-demo.nix -A screenshot -o result-vm-demo-screenshot "$@"
    install -m644 result-vm-demo-screenshot/hyprtrizone-demo.png docs/demo.png
    log "wrote docs/demo.png"
}

vm_demo_video() {
    command -v nix-build >/dev/null 2>&1 || die "nix-build is required for the VM demo"
    cd "$ROOT_DIR"
    # Nix reuses the previous recording until one of its inputs changes.
    nix-build "${NIX_FLAGS[@]}" nix/hyprtrizone-vm-demo.nix -A video -o result-vm-demo-video "$@"
    install -m644 result-vm-demo-video/hyprtrizone-demo.mp4 docs/demo.mp4
    install -m644 result-vm-demo-video/hyprtrizone-demo-poster.png docs/demo-video.png
    log "wrote docs/demo.mp4 and docs/demo-video.png"
}

cmd="${1:-build}"
case "$cmd" in
    build)
        shift || true
        build "$@"
        ;;
    make)
        shift || true
        [[ "$#" -gt 0 ]] || die "usage: ./dev.sh make <make-args>"
        run_make "$@"
        ;;
    reload)
        shift || true
        reload_plugin "$@"
        ;;
    vm-test)
        shift || true
        vm_test "$@"
        ;;
    vm-test-interactive)
        shift || true
        vm_test_interactive "$@"
        ;;
    vm-demo|demo)
        shift || true
        vm_demo "$@"
        ;;
    vm-demo-screenshot)
        shift || true
        vm_demo_screenshot "$@"
        ;;
    vm-demo-video)
        shift || true
        vm_demo_video "$@"
        ;;
    help|-h|--help)
        usage
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac
