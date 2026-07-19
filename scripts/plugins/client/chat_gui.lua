-- Client side Lua GUI chat.
-- The normal chat key opens the input box; this HUD displays the shared history.
local panelId = "lua_gui_chat_panel"
local titleId = "lua_gui_chat_title"
local hintId = "lua_gui_chat_hint"
local lineIds = {}
local messages = {}
local visible = true
local maxMessages = 8

local function removeLine(id)
    plugin.hud.remove(id)
end

local function redraw()
    if not visible then
        plugin.hud.set_visible(panelId, false)
        plugin.hud.set_visible(titleId, false)
        plugin.hud.set_visible(hintId, false)
        for _, id in ipairs(lineIds) do
            plugin.hud.set_visible(id, false)
        end
        return
    end

    plugin.hud.create_rect(panelId, 24, 600, 760, 360,
        {r = 8, g = 12, b = 20, a = 225})
    plugin.hud.create_text(titleId, 44, 618, "Lua GUI Chat", 24,
        {r = 255, g = 210, b = 90, a = 255})
    plugin.hud.create_text(hintId, 44, 930, "Use the normal chat key | lua_chat_toggle toggles this panel", 14,
        {r = 180, g = 190, b = 205, a = 255})

    for _, id in ipairs(lineIds) do
        removeLine(id)
    end
    lineIds = {}

    local first = math.max(1, #messages - maxMessages + 1)
    local row = 0
    for i = first, #messages do
        row = row + 1
        local id = "lua_gui_chat_line_" .. tostring(row)
        lineIds[#lineIds + 1] = id
        plugin.hud.create_text(id, 44, 650 + (row - 1) * 32,
            messages[i], 18, {r = 235, g = 240, b = 250, a = 255})
    end
end

local function appendMessage(message)
    messages[#messages + 1] = message
    while #messages > 32 do
        table.remove(messages, 1)
    end
    redraw()
end

net.Receive("lua_gui_chat_message", function()
    local sender = net.ReadEntity()
    local text = net.ReadString()
    local name = sender and sender:IsValid() and sender:Nick() or "Unknown"
    appendMessage("<" .. name .. "> " .. text)
end)

net.Receive("lua_gui_chat_system", function()
    appendMessage("[System] " .. net.ReadString())
end)

plugin.register_command("lua_chat_toggle", function()
    visible = not visible
    redraw()
end, "Toggle the Lua GUI chat panel")

plugin.on("level_init", function()
    messages = {}
    visible = true
    redraw()
end)

redraw()
