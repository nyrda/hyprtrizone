-- hyprtrizone on Omarchy 4. See docs/omarchy.md.
--
-- Copy to ~/.config/hypr/hyprtrizone.lua and add this line at the end of ~/.config/hypr/hyprland.lua,
-- after Omarchy's defaults and your override files:
--   require("hypr.hyprtrizone")

hl.plugin.load(os.getenv("HOME") .. "/.local/lib/hyprtrizone/hyprtrizoneLayout.so")

hl.config({
    general = {
        layout = "hyprtrizone",
    },
    plugin = {
        hyprtrizone = {
            master_factor = 0.5,
            side_bias = "right",
            side_visible_windows = 3,

            -- Omarchy tags terminals with "terminal" (default/hypr/apps/terminals.lua).
            pairing_right = true,
            pairing_tags = "terminal",

            -- The Omarchy bar already reserves its height across the whole monitor.
            left_header_height = 0,
            right_header_height = 0,
        },
    },
})

-- Replace Omarchy binds that assume dwindle or scrolling.
local function rebind(keys, description, dispatcher)
    hl.unbind(keys)
    o.bind(keys, description, dispatcher)
end

rebind("SUPER + J", "Swap window with center", hl.dsp.layout("swapwithmaster"))
rebind("SUPER + L", "Toggle workspace layout", os.getenv("HOME") .. "/.local/bin/hyprtrizone-layout-toggle")

-- SUPER + wheel stays Omarchy's workspace scrolling.
o.bind("SUPER + SHIFT + mouse_up", "Scroll side stack up", hl.dsp.layout("scroll up"))
o.bind("SUPER + SHIFT + mouse_down", "Scroll side stack down", hl.dsp.layout("scroll down"))
o.bind("SUPER + SHIFT + Page_Up", "Scroll side stack up", hl.dsp.layout("scroll up"))
o.bind("SUPER + SHIFT + Page_Down", "Scroll side stack down", hl.dsp.layout("scroll down"))
