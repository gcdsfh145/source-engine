-- A lightweight Lua-controlled guard NPC. Copy to scripts/plugins/server/.
plugin.Manifest({
    version = "1.0.0",
    permissions = { "entity.create" }
})

local nextAttack = {}

npcs.Register("npc_lua_guard", {
    model = "models/combine_soldier.mdl",
    health = 400,
    native_ai = 1,

    OnSpawn = function(index)
        local guard = ents.GetByIndex(index)
        if guard then guard:SetName("lua_guard") end
    end,

    Think = function(index)
        local guard = ents.GetByIndex(index)
        local target = player.GetByIndex(1)
        if not guard or not target or not target:Alive() then return end

        guard:SetEnemy(target)
        if plugin.vector.distance(guard:GetPos(), target:GetPos()) > 700 then return end

        local now = plugin.time()
        if (nextAttack[index] or 0) > now then return end
        local trace = util.TraceLine(guard:GetPos(), target:EyePos(), guard)
        if trace.Entity and trace.Entity:IsValid() then
            trace.Entity:TakeDamage(12, guard, nil, 0)
            guard:EmitSound("Weapon_SMG1.Single")
            nextAttack[index] = now + 1.0
        end
    end,

    OnRemove = function(index)
        nextAttack[index] = nil
    end
})

plugin.register_command("lua_spawn_guard", function()
    local ply = player.GetByIndex(1)
    if not ply then return end
    local spawnPos = ply:GetPos() + Vector(96, 0, 32)
    local guard = npcs.Create("npc_lua_guard", spawnPos)
    if guard then
        guard:SetEnemy(ply)
    end
end, "Spawn a Lua guard NPC")
