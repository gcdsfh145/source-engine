-- Client HUD example. Copy to scripts/plugins/client/ to enable.
local hudId = "example_status_hud"

local function refresh()
    local map = plugin.map.current()
    local frameTime = plugin.game.frame_time()
    local fps = frameTime > 0 and math.floor(1 / frameTime) or 0
    plugin.hud.create_text(hudId, 24, 24,
        string.format("Map: %s  |  FPS: %d", map.name, fps), 20,
        {r=255, g=220, b=120, a=255})
end

timer.Create("example_status_hud", 0.5, 0, refresh)
plugin.on("level_init", function()
    timer.Simple(0.2, refresh)
end)
