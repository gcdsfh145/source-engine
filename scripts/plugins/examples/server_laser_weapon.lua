-- Custom weapon example. Copy to scripts/plugins/server/ to enable.
plugin.Manifest({
    version = "1.0.0",
    permissions = { "entity.create" }
})

weapons.Register("weapon_lua_laser", {
    model = "models/weapons/w_pistol.mdl",
    view_model = "models/weapons/v_pistol.mdl",
    world_model = "models/weapons/w_pistol.mdl",
    ammo_type = "SMG1",
    clip_size = 20,
    damage = 35,
    shots = 1,
    spread = 0.002,
    distance = 8192,
    fire_rate = 0.18,

    OnSpawn = function(index)
        local weapon = ents.GetByIndex(index)
        if weapon then
            weapon:SetName("lua_laser")
        end
    end,

    PrimaryAttack = function(index)
        local weapon = ents.GetByIndex(index)
        if weapon and weapon:FireBullets(35, 8192, 1, 0.002) then
            weapon:EmitSound("Weapon_Pistol.Single")
            weapon:SetNextPrimaryFire(0.18)
        end
    end
})

hook.Add("PlayerSpawn", "example_give_laser", function(ply)
    if ply and ply:IsValid() then
        ply:Give("weapon_lua_laser")
    end
end)
