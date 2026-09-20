-- hyprtrizone example config: zones, pairing, workspace following and Quickshell side headers.
--
-- Load the plugin first (hl.plugin.load("/path/to/hyprtrizoneLayout.so")), then dofile() or require() this file.
-- HYPRTRIZONE_DEMO_DIR locates these examples (default ~/.config/hypr/hyprtrizone-demo).
-- ./dev.sh vm-demo boots a throwaway NixOS VM with exactly this config.

local demo_dir = os.getenv("HYPRTRIZONE_DEMO_DIR") or (os.getenv("HOME") .. "/.config/hypr/hyprtrizone-demo")

hl.config({
    debug = {
        disable_logs = false,
        enable_stdout_logs = true,
    },
    general = {
        layout = "hyprtrizone",
    },
    plugin = {
        hyprtrizone = {
            master_factor = 0.5,
            side_bias = "right",

            scrolling_enabled = true,
            side_visible_windows = 2,
            left_side_visible_windows = 2,
            right_side_visible_windows = 2,
            scroll_step = 0.65,

            pairing_left = true,
            pairing_right = true,
            pairing_tag_only = true,
            pairing_tags = "pairdemo",

            zone_tags_left = "leftdemo",
            zone_tags_center = "centerdemo",
            zone_tags_right = "rightdemo",
            follow_workspace_tags = "followdemo",

            left_header_height = 34,
            right_header_height = 34,

            indicator_width = 60,
            indicator_height = 30,
            indicator_font_size = 14,
            indicator_padding = 8,
            indicator_opacity = 0.48,
            indicator_text_opacity = 0.5,
            indicator_text_color = "rgba(ffffffff)",
            indicator_show_zero = false,
        },
    },
})

local rule = hl.window_rule

rule({ name = "hyprtrizone-demo-center", match = { title = [[^TRIZONE_CENTER_.*]] }, tag = "+centerdemo" })
rule({ name = "hyprtrizone-demo-left", match = { title = [[^TRIZONE_LEFT_.*]] }, tag = "+leftdemo" })
rule({ name = "hyprtrizone-demo-right", match = { title = [[^TRIZONE_RIGHT_.*]] }, tag = "+rightdemo" })
rule({ name = "hyprtrizone-demo-follow", match = { title = [[^TRIZONE_FOLLOW.*]] }, tag = "+followdemo" })
rule({ name = "hyprtrizone-demo-pair-left", match = { title = [[^TRIZONE_LEFT_PAIR_.*]] }, tag = "+leftdemo" })
rule({ name = "hyprtrizone-demo-pair-left-mark", match = { title = [[^TRIZONE_LEFT_PAIR_.*]] }, tag = "+pairdemo" })
rule({ name = "hyprtrizone-demo-pair-right", match = { title = [[^TRIZONE_RIGHT_PAIR_.*]] }, tag = "+rightdemo" })
rule({ name = "hyprtrizone-demo-pair-right-mark", match = { title = [[^TRIZONE_RIGHT_PAIR_.*]] }, tag = "+pairdemo" })

local shift = "SUPER + SHIFT"

hl.bind("SUPER + mouse_up", hl.dsp.layout("scroll up"))
hl.bind("SUPER + mouse_down", hl.dsp.layout("scroll down"))

hl.bind(shift .. " + F7", hl.dsp.exec_cmd(demo_dir .. "/populate-demo.sh"))
hl.bind(shift .. " + F8", hl.dsp.layout("pairingleft toggle"))
hl.bind(shift .. " + F9", hl.dsp.layout("pairingright toggle"))
hl.bind(shift .. " + F10", hl.dsp.layout("togglescrolling"))

hl.on("hyprland.start", function()
    hl.exec_cmd("quickshell -p '" .. demo_dir .. "/quickshell'")
end)
