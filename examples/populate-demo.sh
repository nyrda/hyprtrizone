#!/usr/bin/env bash
# Opens a set of titled terminals that exercise the demo config: zone tags, pairing,
# side-stack overflow and a workspace-following window. Run it inside the Hyprland session.
set -euo pipefail

if [[ -z "${HYPRLAND_INSTANCE_SIGNATURE:-}" ]]; then
    inst_dir="$(find "${XDG_RUNTIME_DIR:-/run/user/${UID}}/hypr" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | head -n1)"
    [[ -n "${inst_dir}" ]] || { echo "No running Hyprland instance found" >&2; exit 1; }
    export HYPRLAND_INSTANCE_SIGNATURE="${inst_dir##*/}"
fi

# TRIZONE_TERMINAL must accept "--title <title> <command...>" and keep that title: the demo's zone and
# pairing rules match on it, and an interactive shell would otherwise rename the window (kitty locks it by itself).
terminal="${TRIZONE_TERMINAL:-foot -o locked-title=yes}"

close_existing_demo() {
    hyprctl -j clients | jq -r '.[] | select(.title | startswith("TRIZONE_")) | .address' | while read -r address; do
        hyprctl dispatch "hl.dsp.window.close({ window = \"address:${address}\" })" >/dev/null || true
    done
}

spawn_term() {
    local workspace="$1"
    local title="$2"
    local body="$3"
    # Under a Lua config, hyprctl dispatch takes a Lua dispatcher expression.
    local cmd="${terminal} --title ${title} sh -c 'printf \"%s\\n\\n\" \"${body}\"; exec \${SHELL:-sh}'"
    hyprctl dispatch "hl.dsp.exec_cmd([==[${cmd}]==], { workspace = \"${workspace} silent\" })" >/dev/null
    sleep 0.3
}

close_existing_demo
sleep 0.7

spawn_term 1 "TRIZONE_CENTER_MAIN" "Center-tagged window. This should become the master column."
spawn_term 1 "TRIZONE_LEFT_PAIR_1" "Left zone pair candidate 1."
spawn_term 1 "TRIZONE_LEFT_PAIR_2" "Left zone pair candidate 2. These two should share a row."
spawn_term 1 "TRIZONE_LEFT_3" "Additional left-side stack window for scrolling."
spawn_term 1 "TRIZONE_LEFT_4" "More left-side overflow."
spawn_term 1 "TRIZONE_RIGHT_PAIR_1" "Right zone pair candidate 1."
spawn_term 1 "TRIZONE_RIGHT_PAIR_2" "Right zone pair candidate 2. These two should share a row."
spawn_term 1 "TRIZONE_RIGHT_3" "Additional right-side stack window."
spawn_term 1 "TRIZONE_RIGHT_4" "More right-side overflow."
spawn_term 1 "TRIZONE_FOLLOW" "Tagged with followdemo. Switching workspaces should move this window with you."
spawn_term 2 "TRIZONE_WS2" "Workspace 2 window to exercise the workspace switcher."

hyprctl dispatch 'hl.dsp.focus({ workspace = 1 })' >/dev/null

# New side windows snap their stack to the end; go back to the top so the paired rows are in view.
sleep 0.5
hyprctl dispatch 'hl.dsp.layout("scroll left up 10")' >/dev/null
hyprctl dispatch 'hl.dsp.layout("scroll right up 10")' >/dev/null
