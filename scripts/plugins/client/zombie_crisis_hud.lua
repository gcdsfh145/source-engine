-- Client HUD for Zombie Crisis. Loaded automatically from scripts/plugins/client/.
local panelId = "zombie_crisis_panel"
local titleId = "zombie_crisis_title"
local waveId = "zombie_crisis_wave"
local goalId = "zombie_crisis_goal"
local teamIds = {}

local function hideTeamRows()
    for _, id in ipairs(teamIds) do
        plugin.hud.set_visible(id, false)
    end
end

local function draw(mode, wave, infected, players)
    plugin.hud.create_rect(panelId, 24, 24, 420, 80 + #players * 28,
        {r = 8, g = 12, b = 18, a = 220})
    plugin.hud.create_text(titleId, 42, 40, "ZOMBIE CRISIS", 22,
        {r = 255, g = 190, b = 70, a = 255})
    plugin.hud.create_text(waveId, 42, 68,
        string.format("Wave %d  |  Infected %d", wave, infected), 17,
        {r = 230, g = 235, b = 245, a = 255})
    local objective = mode == 2 and "Hold the extraction point" or
        (mode == 3 and "Extraction complete" or "Reach the endpoint")
    plugin.hud.create_text(goalId, 42, 94, objective, 16,
        {r = 180, g = 220, b = 255, a = 255})

    hideTeamRows()
    teamIds = {}
    for i, info in ipairs(players) do
        local id = "zombie_crisis_team_" .. tostring(i)
        teamIds[#teamIds + 1] = id
        plugin.hud.create_text(id, 42, 120 + (i - 1) * 28,
            string.format("%s  %d/%d", info.name, info.health, info.max_health), 16,
            info.health > 0 and {r = 220, g = 245, b = 220, a = 255} or
                {r = 255, g = 100, b = 100, a = 255})
    end
end

net.Receive("zombie_crisis_hud", function()
    local mode = net.ReadInt()
    local wave = net.ReadInt()
    local infected = net.ReadInt()
    local count = net.ReadInt()
    local players = {}
    for i = 1, count do
        players[i] = {
            name = net.ReadString(),
            health = net.ReadInt(),
            max_health = net.ReadInt()
        }
    end
    draw(mode, wave, infected, players)
end)

-- Draw a visible waiting state even before the server sends its first update.
draw(0, 0, 0, {})

plugin.on("level_init", function()
    plugin.hud.remove(panelId)
    plugin.hud.remove(titleId)
    plugin.hud.remove(waveId)
    plugin.hud.remove(goalId)
    hideTeamRows()
    teamIds = {}
end)
