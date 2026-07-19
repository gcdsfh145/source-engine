plugin.log("server example loaded")

-- GMod-style hooks receive player userdata. The old plugin.on API below
-- remains available when a script needs raw userid/entity indexes.
hook.Add("PlayerInitialSpawn", "server_example_join", function(ply, playerName)
    if ply then
        ply:ChatPrint("Welcome, " .. playerName .. "!")
    end
end)

hook.Add("PlayerSpawn", "server_example_loadout", function(ply)
    if ply and ply:IsValid() then
        ply:Give("weapon_rpg")
        ply:SetArmor(100)
    end
end)

hook.Add("PlayerDeath", "server_example_death", function(victim, attacker, weapon)
    if victim then
        plugin.log(victim:Nick() .. " died to " .. tostring(weapon))
    end
end)

hook.Add("PlayerSay", "server_example_chat", function(ply, text)
    if ply and text == "!where" then
        local pos = ply:GetPos()
        ply:ChatPrint(string.format("position: %.0f %.0f %.0f", pos.x, pos.y, pos.z))
    end
end)

timer.Create("server_example_announce", 60, 0, function()
    plugin.hud.message("Server Lua is running")
end)

plugin.on("level_init", function(mapName)
    plugin.log("server level: " .. mapName)
end)

plugin.on("client_put_in_server", function(entIndex, playerName)
    plugin.log(playerName .. " joined as entity " .. tostring(entIndex))
end)

plugin.on("round_start", function()
    for _, ply in ipairs(player.GetAll()) do
        if ply:IsValid() then
            ply:SetHealth(ply:GetMaxHealth())
        end
    end
end)

plugin.on("game_frame", function(simulating)
    if not simulating then
        return
    end
    -- Server-authoritative gameplay logic can run here.
end)

plugin.register_command("lua_server_hello", function(command, args)
    plugin.log("hello from server Lua, command=" .. command)
end, "Example server Lua command")
