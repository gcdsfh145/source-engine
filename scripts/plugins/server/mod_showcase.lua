plugin.Manifest({
    version = "1.0.0",
    permissions = {
        "entity.create",
        "entity.remove",
        "resource.precache"
    }
})

local CRATE_MODEL = "models/props_junk/wood_crate001a.mdl"

util.PrecacheModel(CRATE_MODEL)
util.PrecacheSound("buttons/button14.wav")

local function spawn_crate(pos, scale)
    local crate = ents.Create("prop_physics", {
        model = CRATE_MODEL,
        spawnflags = 256
    })
    if not crate then
        return nil
    end

    crate:SetPos(pos)
    crate:SetModelScale(scale or 1.0, 0.15)
    crate:SetGravity(0.75)
    crate:SetFriction(0.35)
    crate:SetColor(80, 180, 255, 255)
    crate:EmitSound("buttons/button14.wav")
    return crate
end

hook.Add("PlayerSay", "mod_showcase_crate", function(ply, text)
    if not ply or text ~= "!crate" then
        return
    end

    local crate = spawn_crate(ply:EyePos() + Vector(0, 0, 12), 1.25)
    if crate then
        ply:ChatPrint("Lua Mod crate spawned: " .. tostring(crate:EntIndex()))
    end
    return ""
end)

hook.Add("PlayerUse", "mod_showcase_crate_use", function(ply, entIndex)
    local ent = ents.GetByIndex(entIndex)
    if not ent or ent:GetClass() ~= "prop_physics" then
        return
    end
    if ent:GetModel() == CRATE_MODEL then
        ent:SetModelScale(0.75, 0.2)
        ent:SetGravity(0.0)
        ply:ChatPrint("The crate is now floating.")
    end
end)

plugin.on("level_init", function(mapName)
    plugin.log("mod_showcase loaded on " .. mapName)
end)
