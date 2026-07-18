plugin.log("client example loaded")

plugin.on("level_init", function(mapName)
    plugin.log("client level: " .. mapName)
end)

plugin.on("game_frame", function(simulating)
    -- Client-only HUD, input and presentation logic belongs here.
end)

plugin.register_command("lua_client_hello", function(command, args)
    plugin.log("hello from client Lua, command=" .. command)
end, "Example client Lua command")
