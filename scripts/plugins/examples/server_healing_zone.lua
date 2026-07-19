-- A simple healing zone. Copy to scripts/plugins/server/ to enable.
local center = Vector(0, 0, 64)
local radius = 256

timer.Create("example_healing_zone", 1.0, 0, function()
    for _, ply in ipairs(player.GetAll()) do
        if ply:IsValid() and ply:Alive() then
            local distance = plugin.vector.distance(ply:GetPos(), center)
            if distance <= radius then
                ply:SetHealth(math.min(ply:Health() + 10, ply:GetMaxHealth()))
                ply:ChatPrint("Healing zone: +10 HP")
            end
        end
    end
end)

hook.Add("PlayerSay", "example_healing_zone_position", function(ply, text)
    if ply and text == "!healzone" then
        ply:ChatPrint("Healing zone is at 0 0 64, radius " .. tostring(radius))
        return ""
    end
end)
