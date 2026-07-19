-- Copy to scripts/plugins/server/ to enable.
plugin.Manifest({
    version = "1.0.0",
    permissions = { "config.write" }
})

hook.Add("PlayerInitialSpawn", "example_welcome", function(ply, playerName)
    if ply then
        ply:ChatPrint("Welcome, " .. tostring(playerName) .. "!")
    end
end)

hook.Add("PlayerSay", "example_commands", function(ply, text)
    if not ply then return end
    if text == "!rules" then
        ply:ChatPrint("Rules: be respectful and enjoy the MOD.")
        return ""
    end
    if text == "!map" then
        ply:ChatPrint("Current map: " .. plugin.game.map())
        return ""
    end
end)

plugin.register_command("lua_welcome_reload", function()
    plugin.log("welcome plugin is active")
end, "Check the welcome plugin")
