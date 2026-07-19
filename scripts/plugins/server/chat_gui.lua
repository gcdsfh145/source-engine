-- Server side of the Lua GUI chat.
-- Players use the normal game chat input; messages are rendered by the client GUI.
plugin.Manifest({
    version = "1.0.0",
    permissions = { "net.send" }
})

local function sendChatMessage(ply, text)
    if not ply or not text or text == "" then return end

    net.Start("lua_gui_chat_message")
    net.WriteEntity(ply)
    net.WriteString(text)
    net.Send()
end

hook.Add("PlayerSay", "lua_gui_chat_broadcast", function(ply, text)
    if not ply or not text or text == "" then
        return false
    end

    -- Keep slash commands available for other plugins and the game.
    if string.sub(text, 1, 1) == "/" then
        return
    end

    sendChatMessage(ply, text)
    return false -- Hide the old chat line; the GUI renders it instead.
end)

hook.Add("PlayerInitialSpawn", "lua_gui_chat_welcome", function(ply)
    timer.Simple(1.0, function()
        if ply and ply:IsValid() then
            net.Start("lua_gui_chat_system")
            net.WriteString("Welcome to the server. Press the chat key to talk.")
            net.Send(ply)
        end
    end)
end)
