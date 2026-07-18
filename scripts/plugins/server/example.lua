plugin.log("server example loaded")

plugin.on("level_init", function(mapName)
    plugin.log("server level: " .. mapName)
end)

plugin.on("game_frame", function(simulating)
    -- Server-authoritative gameplay logic belongs here.
end)

plugin.on("client_put_in_server", function(entIndex, playerName)
    plugin.log(playerName .. " joined as entity " .. tostring(entIndex))
end)

plugin.register_command("lua_server_hello", function(command, args)
    plugin.log("hello from server Lua, command=" .. command)
end, "Example server Lua command")
