-- Player chat example. Copy to scripts/plugins/server/ to enable.
plugin.Manifest({
    version = "1.0.0",
    permissions = { "chat.broadcast" }
})

hook.Add("PlayerSay", "chat_commands", function(ply, text)
    if not ply or not text then return end

    if text == "/help" then
        chat.send(ply, "Commands: /help, /where, /announce <text>")
        return false
    end

    if text == "/where" then
        local pos = ply:GetPos()
        chat.send(ply, string.format("Position: %.0f %.0f %.0f", pos.x, pos.y, pos.z))
        return false
    end

    local announcement = string.match(text, "^/announce%s+(.+)$")
    if announcement then
        chat.system("[Announcement] " .. announcement)
        return false
    end
end)

timer.Create("chat_welcome_message", 60, 0, function()
    chat.broadcast("Type /help for Lua chat commands.")
end)
