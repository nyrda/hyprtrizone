{
  system ? "x86_64-linux",
  # Same pin as the flake: the nixpkgs revision whose Hyprland the plugin is ported to.
  nixpkgs ?
    let
      locked = (builtins.fromJSON (builtins.readFile ../flake.lock)).nodes.nixpkgs.locked;
    in
    builtins.fetchTarball {
      url = "https://github.com/NixOS/nixpkgs/archive/${locked.rev}.tar.gz";
      sha256 = locked.narHash;
    },
}:

let
  pkgs = import nixpkgs { inherit system; };

  # Bumping the pin is a port: the pinned Hyprland has to be the release this branch targets.
  targetVersion = pkgs.lib.fileContents ../HYPRLAND_VERSION;
  hyprland =
    if pkgs.hyprland.version == targetVersion then
      pkgs.hyprland
    else
      throw "hyprtrizone targets Hyprland ${targetVersion} (HYPRLAND_VERSION), the pinned nixpkgs has ${pkgs.hyprland.version}";

  plugin = pkgs.callPackage ./package.nix { inherit hyprland; };

  testConfig = pkgs.writeText "hyprtrizone-test.lua" ''
    hl.plugin.load("${plugin}/lib/hyprtrizoneLayout.so")

    hl.monitor({ output = "HYPRTRIZONE-TEST", mode = "1920x1080@60", position = "0x0", scale = "1" })
    hl.monitor({ output = "", disabled = true })

    hl.config({
      general = {
        gaps_in = 0,
        gaps_out = 0,
        border_size = 1,
        layout = "hyprtrizone",
      },
      decoration = {
        rounding = 0,
        shadow = { enabled = false },
        blur = { enabled = false },
      },
      animations = {
        enabled = false,
      },
      misc = {
        force_default_wallpaper = -1,
        disable_hyprland_logo = true,
      },
      input = {
        kb_layout = "us",
        follow_mouse = 1,
      },
      debug = {
        disable_logs = false,
      },
      plugin = {
        hyprtrizone = {
          zone_tags_left = "tzleft",
          zone_tags_center = "tzcenter",
          zone_tags_right = "tzright",
          left_header_height = 32,
          right_header_height = 32,
          right_side_visible_windows = 2,
          pairing_right = true,
          pairing_tags = "tzpair",
        },
      },
    })

    hl.window_rule({ name = "tz-left", match = { title = "^tz-left$" }, tag = "+tzleft" })
    hl.window_rule({ name = "tz-center", match = { title = "^tz-center$" }, tag = "+tzcenter" })
    hl.window_rule({ name = "tz-right", match = { title = "^tz-right$" }, tag = "+tzright" })
    hl.window_rule({ name = "tz-pair", match = { title = "^tz-pair.*$" }, tag = "+tzpair" })
  '';

in
(pkgs.testers.runNixOSTest {
  name = "hyprtrizone-hyprland-nixpkgs-vm";

  nodes.machine =
    { pkgs, ... }:
    {
      users.users.alice = {
        isNormalUser = true;
        extraGroups = [ "video" ];
      };

      environment.systemPackages = [
        hyprland
        pkgs.foot
        pkgs.jq
        pkgs.procps
      ];

      environment.variables = {
        AQ_TRACE = "1";
        HYPRLAND_TRACE = "1";
        XDG_CACHE_HOME = "/tmp";
        TERM = "xterm-256color";
      };

      programs.hyprland = {
        enable = true;
        package = hyprland;
      };

      xdg.portal.enable = pkgs.lib.mkForce false;
      services.speechd.enable = false;
      services.getty.autologinUser = "alice";

      virtualisation = {
        cores = 4;
        memorySize = 8192;
        resolution = {
          x = 1920;
          y = 1080;
        };
        qemu.options = [ "-vga none -device virtio-gpu-pci" ];
      };

      system.stateVersion = "24.11";
    };

  testScript = ''
    import json
    import shlex
    import time

    machine.wait_for_unit("multi-user.target")

    hyprctl = "${hyprland}/bin/hyprctl"
    hyprland_bin = "${hyprland}/bin/Hyprland"
    runtime = "/tmp"

    machine.succeed("rm -rf /tmp/hypr /tmp/hyprtrizone-*")
    machine.succeed(
        "su - alice -c "
        + shlex.quote(
            f"XDG_RUNTIME_DIR={runtime} HYPRLAND_HEADLESS_ONLY=1 "
            f"AQ_TRACE=1 HYPRLAND_TRACE=1 nohup {hyprland_bin} --config ${testConfig} "
            "> /tmp/hyprtrizone-hyprland.log 2>&1 &"
        )
    )

    try:
        machine.wait_until_succeeds(f"test -f {runtime}/hypr/*/hyprland.lock", timeout=60)
    except Exception:
        machine.succeed("cat /tmp/hyprtrizone-hyprland.log >&2 || true")
        machine.succeed("cat /tmp/hyprland/hyprlandCrashReport*.txt >&2 || true")
        raise

    instance = machine.succeed(f"basename $(dirname $(ls {runtime}/hypr/*/hyprland.lock | head -1))").strip()
    wayland_display = machine.succeed(f"sed -n '2p' {runtime}/hypr/{instance}/hyprland.lock").strip()
    ipc_env = f"XDG_RUNTIME_DIR={runtime} HYPRLAND_INSTANCE_SIGNATURE={instance}"
    client_env = f"XDG_RUNTIME_DIR={runtime} WAYLAND_DISPLAY={wayland_display}"

    def hypr(args):
        return machine.succeed(f"{ipc_env} {hyprctl} {args}")

    def dump_logs():
        machine.succeed(f"{ipc_env} {hyprctl} monitors >&2 || true")
        machine.succeed(f"{ipc_env} {hyprctl} -j monitors >&2 || true")
        machine.succeed(f"{ipc_env} {hyprctl} -j workspaces >&2 || true")
        machine.succeed("cat /tmp/hyprtrizone-hyprland.log >&2 || true")
        machine.succeed("cat /tmp/hyprland/hyprlandCrashReport*.txt >&2 || true")
        machine.succeed("for f in /tmp/hyprtrizone-*.log; do echo ==== $f ==== >&2; cat $f >&2 || true; done")

    def wait_for_test_monitor():
        deadline = time.time() + 120
        while time.time() < deadline:
            exit_code, _ = machine.execute(
                f"{ipc_env} {hyprctl} -j monitors "
                "| jq -e '.[] | select(.name == \"HYPRTRIZONE-TEST\" and .activeWorkspace.id != null)'"
            )
            if exit_code == 0:
                return
            time.sleep(1)

        dump_logs()
        raise Exception("Timed out waiting for HYPRTRIZONE-TEST to become active")

    def wait_for_clients(count):
        deadline = time.time() + 180
        while time.time() < deadline:
            exit_code, out = machine.execute(
                f"{ipc_env} {hyprctl} -j clients "
                "| jq -e '[.[] | select(.title | startswith(\"tz-\"))] | length' 2>/tmp/hyprtrizone-jq.err"
            )
            if exit_code == 0:
                try:
                    if int(out.strip().splitlines()[-1]) >= count:
                        return
                except Exception:
                    pass
            time.sleep(1)

        machine.succeed(f"{ipc_env} {hyprctl} -j clients >&2 || true")
        machine.succeed("cat /tmp/hyprtrizone-jq.err >&2 || true")
        dump_logs()
        raise Exception(f"Timed out waiting for {count} hyprtrizone test clients")

    # The lock file appears before the IPC socket answers; slow hosts (CI) need a moment.
    machine.wait_until_succeeds(f"{ipc_env} {hyprctl} version", timeout=120)
    # Creating the output can outlast hyprctl's reply timeout on slow hosts although it succeeds;
    # wait_for_test_monitor() below is the actual check.
    machine.execute(f"{ipc_env} {hyprctl} output create headless HYPRTRIZONE-TEST")
    wait_for_test_monitor()
    hypr("monitors")
    machine.wait_until_succeeds(
        f"{ipc_env} {hyprctl} -j workspaces "
        "| jq -e '.[] | select(.name == \"1\" and .tiledLayout == \"hyprtrizone\")'",
        timeout=30,
    )
    machine.succeed(f"{ipc_env} {hyprctl} plugin list > /tmp/hyprtrizone-plugin-list")
    machine.succeed("grep -q hyprtrizone /tmp/hyprtrizone-plugin-list")

    assert not hypr("configerrors").strip(), "Hyprland reported configuration errors"

    def spawn(title):
        machine.succeed(
            "su - alice -c "
            + shlex.quote(
                f"{client_env} foot -T {title} sh -c 'sleep 900' "
                f"> /tmp/hyprtrizone-{title}.log 2>&1 &"
            )
        )

    for index, title in enumerate(["tz-center", "tz-left", "tz-right", "tz-pair-a", "tz-pair-b"], start=1):
        spawn(title)
        wait_for_clients(index)

    machine.succeed(f"{ipc_env} {hyprctl} -j clients > /tmp/hyprtrizone-clients.json")
    geometry_check = machine.execute(
        "jq -e '"
        "def x(t): (.[] | select(.title == t) | .at[0]); "
        "(x(\"tz-left\") < x(\"tz-center\")) and (x(\"tz-center\") < x(\"tz-right\"))"
        "' /tmp/hyprtrizone-clients.json"
    )
    if geometry_check[0] != 0:
        machine.succeed("cat /tmp/hyprtrizone-clients.json >&2")
        dump_logs()
        raise Exception("hyprtrizone did not place tagged left/center/right windows in zone order")

    # hyprctl dispatch evaluates its argument as hl.dispatch(<lua>) under a Lua config.
    def dispatch_lua(expr):
        hypr("dispatch " + shlex.quote(expr))

    dispatch_lua('hl.dsp.layout("setscrolling off")')
    dispatch_lua('hl.dsp.layout("setscrolling default")')
    dispatch_lua('hl.dsp.layout("scroll right down 1")')

    # Fullscreen handling (Fullscreen::controller since 0.56) and window teardown.
    def client(title):
        matches = [c for c in json.loads(hypr("-j clients")) if c["title"] == title]
        return matches[0] if matches else None

    def wait_for(description, predicate):
        deadline = time.time() + 90
        while time.time() < deadline:
            if predicate():
                return
            time.sleep(1)

        machine.succeed(f"{ipc_env} {hyprctl} -j clients >&2 || true")
        dump_logs()
        raise Exception(f"Timed out waiting for: {description}")

    def covers_monitor(title):
        c = client(title)
        return c is not None and c["fullscreen"] == 2 and c["at"] == [0, 0] and c["size"] == [1920, 1080]

    # Client geometry excludes the one-pixel border on each edge.
    wait_for(
        "side reservations to preserve the center geometry",
        lambda: client("tz-left")["at"][1] == 33
        and client("tz-left")["size"][1] == 1046
        and client("tz-center")["at"][1] == 1
        and client("tz-center")["size"][1] == 1078,
    )

    dispatch_lua('hl.dsp.window.fullscreen({ mode = "fullscreen", action = "set", window = "title:^tz-left$" })')
    wait_for("tz-left to cover the monitor", lambda: covers_monitor("tz-left"))

    # A layout recalculation must not pull the fullscreen window back into its zone.
    dispatch_lua('hl.dsp.layout("scroll right down 1")')
    time.sleep(1)
    if not covers_monitor("tz-left"):
        dump_logs()
        raise Exception("fullscreen window lost its geometry after a layout recalculation")

    dispatch_lua('hl.dsp.window.fullscreen({ mode = "fullscreen", action = "unset", window = "title:^tz-left$" })')
    wait_for(
        "tz-left to return to the left zone",
        lambda: client("tz-left")["fullscreen"] == 0
        and client("tz-left")["size"][0] < 1920
        and client("tz-left")["at"][0] < client("tz-center")["at"][0],
    )

    # Closing a fullscreen window goes through removeTarget's fullscreen reset and the destroy listener.
    dispatch_lua('hl.dsp.window.fullscreen({ mode = "fullscreen", action = "set", window = "title:^tz-right$" })')
    wait_for("tz-right to cover the monitor", lambda: covers_monitor("tz-right"))
    dispatch_lua('hl.dsp.window.close({ window = "title:^tz-right$" })')
    wait_for("tz-right to close", lambda: client("tz-right") is None)
    wait_for(
        "remaining windows to be tiled without fullscreen",
        lambda: all(c["fullscreen"] == 0 for c in json.loads(hypr("-j clients")))
        and client("tz-left")["at"][0] < client("tz-center")["at"][0]
        and client("tz-center")["size"][0] < 1920,
    )
    machine.succeed(f"{ipc_env} {hyprctl} plugin list | grep -q hyprtrizone")

    # Pairing. Pair structure has to match what is on screen, also when the two members of a
    # pair are not adjacent in the plugin's node list (another zone's window opened in between).
    def titled(title):
        return [c for c in json.loads(hypr("-j clients")) if c["title"] == title]

    def in_right_zone(c):
        return c["at"][0] > client("tz-center")["at"][0]

    def paired(left, right):
        l, r = client(left), client(right)
        return bool(l and r) and in_right_zone(l) and l["at"][1] == r["at"][1] and l["at"][0] < r["at"][0]

    def stacked(top, bottom):
        t, b = client(top), client(bottom)
        return bool(t and b) and in_right_zone(t) and t["at"][0] == b["at"][0] and t["size"][0] == b["size"][0] and t["at"][1] < b["at"][1]

    def in_center(title):
        c = client(title)
        return c is not None and c["at"][0] == client("tz-center")["at"][0]

    def focus(title):
        dispatch_lua('hl.dsp.focus({ window = "title:^' + title + '$" })')
        wait_for(f"{title} to be focused", lambda: json.loads(hypr("-j activewindow")).get("title") == title)

    def move(title, direction):
        dispatch_lua('hl.dsp.window.move({ direction = "' + direction + '", window = "title:^' + title + '$" })')

    def close(title):
        dispatch_lua('hl.dsp.window.close({ window = "title:^' + title + '$" })')
        wait_for(f"{title} to close", lambda: client(title) is None)

    def hold(description, predicate):
        wait_for(description, predicate)
        time.sleep(2)
        if not predicate():
            machine.succeed(f"{ipc_env} {hyprctl} -j clients >&2 || true")
            raise Exception(f"Did not hold: {description}")

    wait_for("tz-pair-a and tz-pair-b to share a row", lambda: paired("tz-pair-a", "tz-pair-b"))

    # Right stack: a, then the pair {k, c} with a left-zone window between k and c in the node list.
    focus("tz-pair-b")
    spawn("tz-pair-k")
    wait_for("tz-pair-k to open below the pair", lambda: client("tz-pair-k") is not None and client("tz-pair-k")["at"][1] > client("tz-pair-a")["at"][1])
    close("tz-pair-b")
    wait_for("tz-pair-a above tz-pair-k", lambda: stacked("tz-pair-a", "tz-pair-k"))
    spawn("tz-left")
    wait_for("second tz-left window", lambda: len(titled("tz-left")) == 2)
    focus("tz-pair-k")
    spawn("tz-pair-c")
    wait_for("tz-pair-c to pair with tz-pair-k", lambda: paired("tz-pair-k", "tz-pair-c") and client("tz-pair-a")["at"][1] < client("tz-pair-k")["at"][1])

    # Closing the pair leader must leave the second member as its own row, not pair it with the row above.
    close("tz-pair-k")
    hold("tz-pair-c to stay in its own row below tz-pair-a", lambda: stacked("tz-pair-a", "tz-pair-c"))

    # Leaving and re-entering a side stack appends at the end and pairs like a newly opened window.
    move("tz-pair-c", "left")
    wait_for("tz-pair-c to move to the center", lambda: in_center("tz-pair-c"))
    move("tz-pair-c", "right")
    hold("tz-pair-c to pair with tz-pair-a", lambda: paired("tz-pair-a", "tz-pair-c"))

    # Moving towards the pair partner exchanges the two members instead of leaving the zone.
    move("tz-pair-c", "left")
    hold("pair members to exchange places", lambda: paired("tz-pair-c", "tz-pair-a"))

    # Up/down moves by row: the left pair member trades places with the row below, not with its partner.
    focus("tz-pair-a")
    spawn("tz-pair-d")
    wait_for("tz-pair-d to open below the pair", lambda: client("tz-pair-d") is not None and client("tz-pair-d")["at"][1] > client("tz-pair-c")["at"][1])
    move("tz-pair-c", "down")
    hold("tz-pair-c to trade rows with tz-pair-d", lambda: paired("tz-pair-d", "tz-pair-a") and client("tz-pair-c")["at"][1] > client("tz-pair-d")["at"][1])

    # A pair leader leaving the zone must not leave a dangling pair flag on its partner.
    move("tz-pair-d", "left")
    hold("tz-pair-a to become a single row above tz-pair-c", lambda: in_center("tz-pair-d") and stacked("tz-pair-a", "tz-pair-c"))
    machine.succeed(f"{ipc_env} {hyprctl} plugin list | grep -q hyprtrizone")

    # Drag and drop placement through the dropon layoutmsg (same code path as a mouse drop):
    # a full row exchanges, a free slot pairs.
    def drop(source, target, half="right"):
        dispatch_lua('hl.dsp.layout("dropon title:^' + source + '$ title:^' + target + '$ ' + half + '")')

    def above(top, bottom):
        return client(top)["at"][1] < client(bottom)["at"][1]

    def single_row(title):
        others = [c for c in json.loads(hypr("-j clients")) if c["title"] != title and in_right_zone(c)]
        return in_right_zone(client(title)) and all(c["at"][1] != client(title)["at"][1] for c in others)

    move("tz-pair-d", "right")
    hold("right stack to be a | {c, d}", lambda: single_row("tz-pair-a") and paired("tz-pair-c", "tz-pair-d") and above("tz-pair-a", "tz-pair-c"))

    # Single window dropped onto the left member of a full row: the two exchange, the partner stays.
    drop("tz-pair-a", "tz-pair-c")
    hold("a and c to exchange: c | {a, d}", lambda: single_row("tz-pair-c") and paired("tz-pair-a", "tz-pair-d") and above("tz-pair-c", "tz-pair-a"))

    # ... and onto the right member.
    drop("tz-pair-c", "tz-pair-d", "left")
    hold("c and d to exchange: d | {a, c}", lambda: single_row("tz-pair-d") and paired("tz-pair-a", "tz-pair-c") and above("tz-pair-d", "tz-pair-a"))

    # A pair member dropped onto a single eligible window pairs with it on the cursor's side.
    drop("tz-pair-a", "tz-pair-d", "left")
    hold("a to pair left of d: {a, d} | c", lambda: paired("tz-pair-a", "tz-pair-d") and single_row("tz-pair-c") and above("tz-pair-a", "tz-pair-c"))

    # Dropping a pair member onto its own partner reorders the pair.
    drop("tz-pair-a", "tz-pair-d", "right")
    hold("pair to reorder: {d, a} | c", lambda: paired("tz-pair-d", "tz-pair-a") and single_row("tz-pair-c") and above("tz-pair-d", "tz-pair-c"))

    # Between two full rows, only the two windows involved trade places.
    focus("tz-pair-c")
    spawn("tz-pair-e")
    wait_for("tz-pair-e to pair with tz-pair-c", lambda: paired("tz-pair-c", "tz-pair-e"))
    drop("tz-pair-d", "tz-pair-e")
    hold("d and e to exchange: {e, a} | {c, d}", lambda: paired("tz-pair-e", "tz-pair-a") and paired("tz-pair-c", "tz-pair-d") and above("tz-pair-e", "tz-pair-c"))
    drop("tz-pair-a", "tz-pair-c", "left")
    hold("a and c to exchange: {e, c} | {a, d}", lambda: paired("tz-pair-e", "tz-pair-c") and paired("tz-pair-a", "tz-pair-d") and above("tz-pair-e", "tz-pair-a"))

    # Grouping swaps the window's target for a group target outside the layout; the node has to adopt it.
    center_box = (client("tz-center")["at"], client("tz-center")["size"])
    focus("tz-center")
    dispatch_lua("hl.dsp.group.toggle()")
    hold("grouped tz-center to keep its center box", lambda: len(client("tz-center")["grouped"]) == 1 and client("tz-center")["size"][0] > 0 and client("tz-center")["at"][0] == center_box[0][0])
    dispatch_lua("hl.dsp.group.toggle()")
    hold("ungrouped tz-center to return to its box", lambda: not client("tz-center")["grouped"] and (client("tz-center")["at"], client("tz-center")["size"]) == center_box)

    # Runtime forms used by reload-plugin.sh, vm-demo and user binds under a Lua config
    # (hyprctl keyword and string dispatchers are legacy-parser only).
    def tiled_layout():
        return json.loads(hypr("-j activeworkspace"))["tiledLayout"]

    hypr("eval " + shlex.quote('hl.config({ general = { layout = "master" } })'))
    wait_for("workspace to switch to the master layout", lambda: tiled_layout() == "master")
    hypr("eval " + shlex.quote('hl.config({ general = { layout = "hyprtrizone" } })'))
    wait_for("workspace to switch back to hyprtrizone", lambda: tiled_layout() == "hyprtrizone")
    hold("windows to be zoned again after the layout switch", lambda: client("tz-left")["at"][0] < client("tz-center")["at"][0] and in_right_zone(client("tz-pair-a")))
    hold("rows and pairs to survive the layout switch: {e, c} | {a, d}", lambda: paired("tz-pair-e", "tz-pair-c") and paired("tz-pair-a", "tz-pair-d") and above("tz-pair-e", "tz-pair-a"))

    dispatch_lua('hl.dsp.exec_cmd("foot -T tz-exec sh -c \'sleep 900\'", { workspace = "2 silent" })')
    wait_for("exec_cmd window to open on workspace 2", lambda: client("tz-exec") is not None and client("tz-exec")["workspace"]["id"] == 2)
    machine.succeed(f"{ipc_env} {hyprctl} plugin list | grep -q hyprtrizone")

    # Exercise listener and render-pass teardown with windows still open.
    hypr("eval " + shlex.quote('hl.config({ general = { layout = "master" } })'))
    wait_for("master layout before unloading", lambda: tiled_layout() == "master")
    hypr("plugin unload ${plugin}/lib/hyprtrizoneLayout.so")
    assert "hyprtrizone" not in hypr("plugin list")
    hypr("plugin load ${plugin}/lib/hyprtrizoneLayout.so")
    assert "hyprtrizone" in hypr("plugin list")
    hypr("eval " + shlex.quote('hl.config({ general = { layout = "hyprtrizone" } })'))
    wait_for("hyprtrizone layout after reloading", lambda: tiled_layout() == "hyprtrizone")
    assert client("tz-center") is not None
    assert not hypr("configerrors").strip()

    machine.succeed("cp /tmp/hyprtrizone-hyprland.log /tmp/hyprtrizone-hyprland.log.final")
    machine.copy_from_machine("/tmp/hyprtrizone-plugin-list")
    machine.copy_from_machine("/tmp/hyprtrizone-clients.json")
    machine.copy_from_machine("/tmp/hyprtrizone-hyprland.log.final")

    machine.succeed(f"{ipc_env} {hyprctl} dispatch 'hl.dsp.exit()'")
    machine.wait_until_fails(
        f"test -f {runtime}/hypr/{instance}/hyprland.lock "
        f"&& test -e /proc/$(sed -n '1p' {runtime}/hypr/{instance}/hyprland.lock)",
        timeout=60,
    )
  '';
})
// {
  # Plugin-only build, also used by ./dev.sh as a build shell: nix-shell <this file> -A plugin
  inherit plugin;
  # Shared with nix/hyprtrizone-vm-demo.nix so both run on the same package set.
  inherit pkgs hyprland;
}
