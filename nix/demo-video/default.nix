# Records the feature tour: boots the demo session on a 30:9 monitor, drives it with real keyboard and pointer
# input, and encodes the recording on the host.
#
#   nix-build nix/hyprtrizone-vm-demo.nix -A video   (./dev.sh vm-demo-video writes docs/hyprtrizone-demo.mp4)
{
  pkgs,
  lib,
  demoDir,
  mkDemoModule,
  mkHyprlandConfig,
  width ? 3600,
  height ? 1080,
}:
let
  overlay = ./overlay.qml;

  demoWindow = pkgs.writeShellApplication {
    name = "demo-window";
    runtimeInputs = [
      pkgs.toilet
      pkgs.ncurses
    ];
    text = builtins.readFile ./window.sh;
  };

  hyprlandConfig = mkHyprlandConfig {
    inherit width height;
    populate = false;
    extraConfig = ''
      hl.config({
        animations = { enabled = true },
        cursor = { inactive_timeout = 1 },
        -- ydotool moves the pointer in relative steps; keep them 1:1.
        input = { accel_profile = "flat", sensitivity = 0 },
        -- Badges readable in a scaled-down video.
        plugin = { hyprtrizone = { indicator_width = 84, indicator_height = 40, indicator_font_size = 22 } },
      })

      hl.curve("easeOutQuint", { type = "bezier", points = { { 0.23, 1 }, { 0.32, 1 } } })
      hl.curve("easy", { type = "spring", mass = 1, stiffness = 238.1191, dampening = 24.21279333 })
      hl.animation({ leaf = "global", enabled = true, speed = 6, bezier = "default" })
      hl.animation({ leaf = "windows", enabled = true, speed = 4.5, spring = "easy" })
      hl.animation({ leaf = "windowsIn", enabled = true, speed = 4, spring = "easy", style = "popin 87%" })
      hl.animation({ leaf = "windowsOut", enabled = true, speed = 1.5, bezier = "easeOutQuint", style = "popin 87%" })
      hl.animation({ leaf = "border", enabled = true, speed = 5, bezier = "easeOutQuint" })
      hl.animation({ leaf = "workspaces", enabled = true, speed = 3, bezier = "easeOutQuint", style = "slidefade 12%" })

      hl.bind("SUPER + SPACE", hl.dsp.layout("swapwithmaster"))

      -- The tour's terminals carry their tags in their app id, so their titles can stay readable.
      local tour_tags = {
        ["tour-center"] = { "centerdemo" },
        ["tour-left"] = { "leftdemo" },
        ["tour-left-follow"] = { "leftdemo", "followdemo" },
        ["tour-right"] = { "rightdemo" },
        ["tour-right-pair"] = { "rightdemo", "pairdemo" },
      }
      for class, tags in pairs(tour_tags) do
        for _, tag in ipairs(tags) do
          hl.window_rule({ name = class .. "-" .. tag, match = { class = "^" .. class .. "$" }, tag = "+" .. tag })
        end
      end

      hl.on("hyprland.start", function()
        hl.exec_cmd("quickshell -p ${overlay} > /tmp/tour-overlay.log 2>&1")
      end)
    '';
  };

  demoModule = mkDemoModule {
    inherit width height hyprlandConfig;
    cores = 8;
  };
in
pkgs.testers.runNixOSTest {
  name = "hyprtrizone-demo-video";

  nodes.machine = {
    imports = [ demoModule ];
    programs.ydotool.enable = true;
    environment.systemPackages = [
      demoWindow
      pkgs.wf-recorder
    ];
    # Larger terminal text: the video is usually watched scaled down.
    environment.etc."xdg/foot/foot.ini".text = lib.mkForce ''
      font=monospace:size=13
      pad=18x14

      [colors-dark]
      background=1e1e2e
      foreground=cdd6f4
    '';
  };

  testScript = ''
    WIDTH = ${toString width}
    HEIGHT = ${toString height}
    OVERLAY = "${overlay}"
    FFMPEG = "${lib.getExe pkgs.ffmpeg}"
  ''
  + builtins.readFile ./tour.py;
}
