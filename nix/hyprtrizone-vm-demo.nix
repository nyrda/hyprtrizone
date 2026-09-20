# Throwaway NixOS VM that boots straight into Hyprland with the plugin and examples/hyprtrizone-demo.lua.
#
#   nix-build nix/hyprtrizone-vm-demo.nix -A vm && ./result/bin/run-hyprtrizone-demo-vm   (./dev.sh vm-demo)
#   nix-build nix/hyprtrizone-vm-demo.nix -A screenshot                                   (./dev.sh vm-demo-screenshot)
#
# In the VM: SUPER+Return terminal, SUPER+Q close, SUPER+arrows focus, SUPER+SHIFT+arrows move,
# SUPER+left mouse drag (drop onto windows to pair/exchange), SUPER+SHIFT+F7 re-populate, SUPER+M exit.
# SUPER+mouse wheel scrolls the focused side stack (right stack when the center is focused).
{
  system ? "x86_64-linux",
  nixpkgs ? null,
}:
let
  vmTest = import ./hyprtrizone-vm-test.nix ({ inherit system; } // (if nixpkgs == null then { } else { inherit nixpkgs; }));
  inherit (vmTest) pkgs plugin hyprland;
  lib = pkgs.lib;

  demoDir = pkgs.runCommand "hyprtrizone-demo" { } ''
    mkdir -p $out
    cp -r ${../examples}/. $out/
    chmod -R u+w $out
    chmod +x $out/*.sh
  '';

  hyprlandConfig = pkgs.writeText "hyprland.lua" ''
    hl.plugin.load("${plugin}/lib/hyprtrizoneLayout.so")

    hl.monitor({ output = "", mode = "2560x1080@60", position = "0x0", scale = "1" })

    hl.config({
      general = {
        gaps_in = 4,
        gaps_out = 0,
        border_size = 3,
        col = {
          active_border = { colors = { "rgba(aaaaeeaa)", "rgba(eeeeaaff)", "rgba(aaaaeeaa)" }, angle = 45 },
          inactive_border = "rgba(595959aa)",
        },
      },
      decoration = { rounding = 10, shadow = { enabled = false }, blur = { enabled = false } },
      -- Software rendering: keep it snappy.
      animations = { enabled = false },
      misc = {
        disable_hyprland_logo = true,
        -- This disposable VM starts Hyprland directly for test-driver control.
        disable_watchdog_warning = true,
        force_default_wallpaper = 0,
        background_color = "rgba(1b1e28ff)",
      },
      input = { kb_layout = "us", follow_mouse = 1 },
    })

    dofile("${demoDir}/hyprtrizone-demo.lua")

    local main = "SUPER"
    local shift = "SUPER + SHIFT"
    hl.bind(main .. " + RETURN", hl.dsp.exec_cmd("foot"))
    hl.bind(main .. " + Q", hl.dsp.window.close())
    hl.bind(main .. " + M", hl.dsp.exit())
    for _, dir in ipairs({ "left", "right", "up", "down" }) do
      hl.bind(main .. " + " .. dir, hl.dsp.focus({ direction = dir }))
      hl.bind(shift .. " + " .. dir, hl.dsp.window.move({ direction = dir }))
    end
    for i = 1, 3 do
      hl.bind(main .. " + " .. i, hl.dsp.focus({ workspace = i }))
    end
    hl.bind(main .. " + mouse:272", hl.dsp.window.drag(), { mouse = true })
    hl.bind(main .. " + mouse:273", hl.dsp.window.resize(), { mouse = true })

    hl.on("hyprland.start", function()
      hl.exec_cmd("${demoDir}/populate-demo.sh")
    end)
  '';

  demoModule =
    { pkgs, ... }:
    {
      networking.hostName = "hyprtrizone-demo";

      users.users.demo = {
        isNormalUser = true;
        password = "";
        extraGroups = [ "video" ];
      };
      services.getty.autologinUser = "demo";

      programs.hyprland = {
        enable = true;
        package = hyprland;
      };
      xdg.portal.enable = lib.mkForce false;
      services.speechd.enable = false;

      environment.systemPackages = [
        pkgs.foot
        pkgs.quickshell
        pkgs.jq
        pkgs.procps
      ];
      fonts.packages = [
        pkgs.noto-fonts
        pkgs.noto-fonts-color-emoji
      ];

      # Calm terminals: padded, themed, and a short prompt instead of user@host.
      environment.etc."xdg/foot/foot.ini".text = ''
        font=monospace:size=10
        pad=14x12

        [colors-dark]
        background=1e1e2e
        foreground=cdd6f4
      '';
      programs.bash.promptInit = ''
        PS1='\[\e[38;5;110m\]❯\[\e[0m\] '
      '';

      # tty1 goes straight into the demo session.
      environment.loginShellInit = ''
        if [ "$(tty)" = /dev/tty1 ] && [ -z "$WAYLAND_DISPLAY" ]; then
          export HYPRTRIZONE_DEMO_DIR=${demoDir}
          exec Hyprland --config ${hyprlandConfig} > /tmp/hyprland.log 2>&1
        fi
      '';

      system.stateVersion = "24.11";
    };

  # Software rendering on a plain virtio-gpu is enough; 2560x1080 leaves room for all three zones.
  vmSettings = {
    virtualisation = {
      cores = 4;
      memorySize = 4096;
      resolution = {
        x = 2560;
        y = 1080;
      };
      qemu.options = [ "-vga none -device virtio-gpu-pci" ];
    };
  };
in
{
  vm =
    (import (pkgs.path + "/nixos/lib/eval-config.nix") {
      system = pkgs.stdenv.hostPlatform.system;
      modules = [
        demoModule
        vmSettings
        (pkgs.path + "/nixos/modules/virtualisation/qemu-vm.nix")
      ];
    }).config.system.build.vm;

  # Boots the same session unattended and captures the populated desktop.
  screenshot = pkgs.testers.runNixOSTest {
    name = "hyprtrizone-demo-screenshot";
    nodes.machine = {
      imports = [
        demoModule
        vmSettings
      ];
    };
    testScript = ''
      import json

      machine.wait_for_unit("multi-user.target")
      hyprctl = "su - demo -c 'XDG_RUNTIME_DIR=/run/user/1000 hyprctl -i 0 -j clients'"
      try:
          machine.wait_until_succeeds(f"{hyprctl} | jq -e '[.[] | select(.title | startswith(\"TRIZONE_\"))] | length >= 11'", timeout=120)
      except Exception:
          machine.screenshot("hyprtrizone-demo-failed")
          machine.succeed("tail -n 40 /tmp/hyprland.log >&2 || true")
          machine.succeed(f"{hyprctl} | jq -c '.[] | {{title, workspace: .workspace.id}}' >&2 || true")
          machine.succeed("su - demo -c 'XDG_RUNTIME_DIR=/run/user/1000 hyprctl -i 0 configerrors' >&2 || true")
          raise
      ctl = "su - demo -c 'XDG_RUNTIME_DIR=/run/user/1000 hyprctl -i 0 "
      def query(command):
          return json.loads(machine.succeed(ctl + "-j " + command + "'"))

      def panels():
          return [
              layer
              for monitor in query("layers").values()
              for group in monitor["levels"].values()
              for layer in group
              if layer["namespace"].startswith("hyprtrizone-demo-")
          ]

      with subtest("Quickshell split panels preserve the master work area"):
          machine.wait_until_succeeds("pgrep -u demo quickshell")
          retry(lambda _: len(panels()) == 2)
          bars = sorted(panels(), key=lambda panel: panel["x"])
          assert [(b["x"], b["y"], b["w"], b["h"]) for b in bars] == [(0, 0, 640, 34), (1920, 0, 640, 34)], bars
          assert query("monitors")[0]["reserved"] == [0, 0, 0, 0]
          clients = query("clients")
          center = next(c for c in clients if c["title"].startswith("TRIZONE_CENTER_"))
          assert center["at"][1] < 34, center

      errors = machine.succeed(ctl + "configerrors'").strip()
      assert not errors, errors
      machine.sleep(2)
      machine.succeed("cat /tmp/hyprland.log | tail -n 5 >&2 || true")
      machine.screenshot("hyprtrizone-demo")

      with subtest("shared widgets work as a conventional bar"):
          machine.succeed("pkill -u demo quickshell")
          machine.succeed(ctl + "eval \"hl.config({ general = { layout = \\\"dwindle\\\" } })\"'")
          machine.succeed("su - demo -c 'XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1 HYPRLAND_INSTANCE_SIGNATURE=$(ls /run/user/1000/hypr | head -n1) QS_BAR_MODE=top quickshell -p ${demoDir}/quickshell > /tmp/quickshell-top.log 2>&1 &'")
          retry(lambda _: len(panels()) == 1)
          bar = panels()[0]
          assert (bar["x"], bar["y"], bar["w"], bar["h"]) == (0, 0, 2560, 34), bar
          assert query("monitors")[0]["reserved"][1] == 34
          machine.screenshot("quickshell-top")
    '';
  };
}
