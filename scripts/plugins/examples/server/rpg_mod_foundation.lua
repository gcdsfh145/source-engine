-- Copy this file to scripts/plugins/server/ to enable the example RPG foundation.
plugin.Manifest({
    version = "1.0.0",
    permissions = {
        "entity.create",
        "entity.damage",
        "world.effect",
        "chat.broadcast",
        "resource.precache",
        "config.write"
    },
    config = { announce = true }
})

local players = {}
local BLAST_SOUND = "ambient/explosions/explode_4.wav"

util.PrecacheSound(BLAST_SOUND)

local function player_key(ply)
    local id = ply:network_id()
    if id and id ~= "" then return id end
    return tostring(ply:userid())
end

local function get_state(ply)
    local key = player_key(ply)
    players[key] = players[key] or { xp = 0, level = 1 }
    return players[key]
end

local function add_xp(ply, amount)
    local state = get_state(ply)
    state.xp = state.xp + amount
    local required = state.level * 100
    while state.xp >= required do
        state.xp = state.xp - required
        state.level = state.level + 1
        ply:SetMaxHealth(100 + state.level * 10)
        ply:SetHealth(ply:GetMaxHealth())
        ply:ChatPrint("Level up: " .. tostring(state.level))
        required = state.level * 100
    end
end

hook.Add("PlayerInitialSpawn", "rpg_foundation_state", function(ply)
    local state = get_state(ply)
    ply:SetMaxHealth(100 + state.level * 10)
    ply:SetHealth(ply:GetMaxHealth())
end)

hook.Add("PlayerDeath", "rpg_foundation_reward", function(victim, attacker)
    if attacker and attacker:IsValid() and attacker ~= victim and attacker:IsPlayer() then
        add_xp(attacker, 50)
    end
end)

hook.Add("PlayerSay", "rpg_foundation_commands", function(ply, text)
    if not ply or not text then return end

    if text == "/stats" then
        local state = get_state(ply)
        ply:ChatPrint(string.format("Level %d | XP %d | Max HP %d",
            state.level, state.xp, ply:GetMaxHealth()))
        return false
    end

    if text == "/blast" then
        local trace = ply:EyeTrace(2048)
        if trace and trace.Hit then
            util.RadiusDamage(trace.HitPos, 60 + get_state(ply).level * 5,
                220, ply, 64, ply)
            util.ScreenShake(trace.HitPos, 12, 150, 0.6, 500, true)
            ply:EmitSound(BLAST_SOUND)
        end
        return false
    end
end)

weapons.Register("weapon_lua_pulse", {
    model = "models/weapons/w_smg1.mdl",
    view_model = "models/weapons/v_smg1.mdl",
    world_model = "models/weapons/w_smg1.mdl",
    ammo_type = "SMG1",
    clip_size = 30,
    damage = 22,
    shots = 1,
    spread = 0.01,
    distance = 4096,
    fire_rate = 0.12,
    OnSpawn = function(index)
        local weapon = ents.GetByIndex(index)
        if weapon then weapon:SetName("lua_pulse_weapon") end
    end
})

npcs.Register("npc_lua_training_guard", {
    native_ai = 1,
    model = "models/combine_soldier.mdl",
    health = 250,
    Think = function(index)
        local guard = ents.GetByIndex(index)
        local target = player.GetByIndex(1)
        if guard and target and target:Alive() then
            guard:SetEnemy(target)
        end
    end,
    OnDeath = function(index, damage, attacker)
        local killer = ents.GetByIndex(attacker)
        if killer and killer:IsValid() and killer:IsPlayer() then
            add_xp(killer, 25)
        end
    end
})

hook.Add("PlayerSpawn", "rpg_foundation_loadout", function(ply)
    if ply and ply:IsValid() then
        ply:Give("weapon_lua_pulse")
    end
end)

plugin.register_command("lua_rpg_spawn_guard", function(command, args)
    local ply = player.GetByIndex(1)
    if not ply then return end
    local guard = npcs.Create("npc_lua_training_guard", ply:GetPos() + Vector(96, 0, 0))
    if guard then ply:ChatPrint("Training guard spawned") end
end, "Spawn the example Lua training guard")

if plugin.config_get("announce", true) then
    timer.Create("rpg_foundation_announce", 60, 0, function()
        chat.broadcast("RPG foundation active: use /stats or /blast")
    end)
end
