-- Read-only map and MOD file browser.
-- Put a screenshot at maps/<map>.jpg, maps/<map>.png or
-- materials/maps/<map>/preview.(jpg|png) to show a map preview.

local function find_map(name)
    for _, info in ipairs(plugin.map.list()) do
        if info.name == name then
            return info
        end
    end
    return nil
end

local function draw_browser(mapInfo)
    plugin.hud.clear()
    plugin.hud.create_rect("browser_panel", 24, 24, 620, 520, {r=10, g=14, b=22, a=235})
    plugin.hud.create_text("browser_title", 44, 42, "Lua Map / File Browser", 26, {r=255, g=190, b=80, a=255})

    local current = mapInfo or plugin.map.current()
    plugin.hud.create_text("browser_current", 44, 82,
        "Current map: " .. tostring(current.name), 20, {r=230, g=235, b=245, a=255})

    if current.preview then
        plugin.hud.create_image("browser_preview", 44, 116, 300, 170, current.preview,
            {r=255, g=255, b=255, a=255})
    else
        plugin.hud.create_text("browser_no_preview", 44, 132, "No preview image found", 18,
            {r=180, g=185, b=195, a=255})
    end

    local mapNames = {}
    for _, info in ipairs(plugin.map.list()) do
        mapNames[#mapNames + 1] = info.name
        if #mapNames >= 12 then break end
    end
    plugin.hud.create_text("browser_maps", 370, 116,
        "Maps:\n" .. table.concat(mapNames, "\n"), 17, {r=180, g=220, b=255, a=255})

    local files = {}
    for _, item in ipairs(plugin.file.list("scripts/plugins")) do
        files[#files + 1] = (item.directory and "[DIR] " or "      ") .. item.name
        if #files >= 12 then break end
    end
    plugin.hud.create_text("browser_files", 44, 320,
        "MOD files (read-only):\n" .. table.concat(files, "\n"), 17, {r=210, g=220, b=210, a=255})
    plugin.hud.create_text("browser_help", 44, 498,
        "Commands: lua_map_preview <map> | lua_file_view <relative path>", 15,
        {r=190, g=190, b=190, a=255})
end

plugin.register_command("lua_map_browser", function()
    draw_browser()
end, "Show the Lua map and file browser")

plugin.register_command("lua_map_preview", function(_, args)
    local info = args[1] and find_map(args[1]) or plugin.map.current()
    if info then
        draw_browser(info)
    else
        plugin.hud.message("Map not found")
    end
end, "Preview a map screenshot")

plugin.register_command("lua_file_view", function(_, args)
    local path = args[1]
    if not path then
        plugin.hud.message("Usage: lua_file_view <relative path>")
        return
    end
    local contents = plugin.file.read(path)
    if not contents then
        plugin.hud.message("File unavailable or outside the safe path")
        return
    end
    plugin.hud.clear()
    plugin.hud.create_rect("file_panel", 24, 24, 900, 620, {r=8, g=10, b=16, a=245})
    plugin.hud.create_text("file_title", 44, 42, "File: " .. path, 22, {r=255, g=190, b=80, a=255})
    plugin.hud.create_text("file_contents", 44, 82, string.sub(contents, 1, 5000), 16,
        {r=230, g=235, b=240, a=255})
end, "Read a relative game file")

plugin.on("level_init", function()
    timer.Simple(0.5, draw_browser)
end)

timer.Simple(1.0, draw_browser)
