#!/usr/bin/env bash
# Hot-reloads the freshly built plugin into the running Hyprland session (./dev.sh reload).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_SO="${SCRIPT_DIR}/hyprtrizoneLayout.so"
LIVE_SO="${SCRIPT_DIR}/hyprtrizoneLayout-live-$$.so"

# "hyprctl keyword" only works with the legacy config parser; eval works under Lua configs.
set_layout() {
  hyprctl eval "hl.config({ general = { layout = \"$1\" } })"
}

# Get every workspace off the layout before its code is unloaded.
set_layout master

# Unload any previous live copies and the plain build.
for f in "${SCRIPT_DIR}"/hyprtrizoneLayout-live-*.so "${PLUGIN_SO}"; do
  [[ -f "$f" ]] && hyprctl plugin unload "$f" >/dev/null 2>&1 || true
done

# Load from a PID-unique filename so dlopen cannot hand back its cached handle of the old build.
rm -f "${SCRIPT_DIR}"/hyprtrizoneLayout-live-*.so
cp -f "${PLUGIN_SO}" "${LIVE_SO}"
hyprctl plugin load "${LIVE_SO}"
echo "Plugin path: ${LIVE_SO}"

set_layout hyprtrizone
