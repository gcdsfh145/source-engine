-- Zombie Crisis: a cooperative escape mode inspired by horde-survival games.
-- This file is loaded automatically from scripts/plugins/server/.
-- Map authors can place an info_target named zombie_crisis_exit as the goal.
plugin.Manifest({
    version = "1.0.0",
    permissions = {
        "entity.create",
        "entity.remove",
        "entity.damage",
        "world.effect",
        "net.send",
        "chat.broadcast",
        "resource.precache",
        "config.write"
    },
    config = {
        end_x = 0,
        end_y = 0,
        end_z = 64,
        auto_start = true
    }
})

local MODE_WAITING = 0
local MODE_TRAVEL = 1
local MODE_FINALE = 2
local MODE_COMPLETE = 3

local mode = MODE_WAITING
local wave = 0
local waveEndsAt = 0
local finaleEndsAt = 0
local nextCommonSpawn = 0
local nextSpecialSpawn = 0
local nextHudUpdate = 0
local started = false
local zombies = {}
local specialByIndex = {}
local specialNextAttack = {}
local endEntity = nil

local commonTypes = {
    "npc_zombie",
    "npc_zombie",
    "npc_fastzombie",
    "npc_headcrab"
}

local specialTypes = {
    "hunter",
    "smoker",
    "boomer",
    "charger",
    "spitter",
    "jockey",
    "witch",
    "tank"
}

local specialClass = {
    hunter = "npc_fastzombie",
    smoker = "npc_zombie",
    boomer = "npc_poisonzombie",
    charger = "npc_zombine",
    spitter = "npc_poisonzombie",
    jockey = "npc_fastzombie",
    witch = "npc_poisonzombie",
    tank = "npc_zombine"
}

local specialHealth = {
    hunter = 250,
    smoker = 300,
    boomer = 180,
    charger = 500,
    spitter = 220,
    jockey = 220,
    witch = 700,
    tank = 1400
}

local function alivePlayers()
    local result = {}
    for _, ply in ipairs(player.GetAll()) do
        if ply and ply:IsValid() and ply:IsPlayer() then
            result[#result + 1] = ply
        end
    end
    return result
end

local function livingPlayers()
    local result = {}
    for _, ply in ipairs(alivePlayers()) do
        if ply:Alive() then result[#result + 1] = ply end
    end
    return result
end

local function countLivingZombies()
    local count = 0
    for index, ent in pairs(zombies) do
        if ent and ent:IsValid() and ent:Health() > 0 then
            count = count + 1
        else
            zombies[index] = nil
            specialByIndex[index] = nil
        end
    end
    return count
end

local function findGoal()
    local goals = ents.FindByName("zombie_crisis_exit")
    if goals and goals[1] and goals[1]:IsValid() then
        endEntity = goals[1]
        return endEntity:GetPos()
    end
    endEntity = nil
    return Vector(
        plugin.config_get("end_x", 0),
        plugin.config_get("end_y", 0),
        plugin.config_get("end_z", 64))
end

local function goalPosition()
    if endEntity and endEntity:IsValid() then return endEntity:GetPos() end
    return findGoal()
end

local function sendStatus()
    local now = plugin.game.time()
    if now < nextHudUpdate then return end
    nextHudUpdate = now + 0.5

    local players = alivePlayers()
    local living = countLivingZombies()
    net.Start("zombie_crisis_hud")
    net.WriteInt(mode, 8)
    net.WriteInt(wave, 16)
    net.WriteInt(living, 16)
    net.WriteInt(#players, 8)
    for _, ply in ipairs(players) do
        net.WriteString(ply:Nick())
        net.WriteInt(ply:Health(), 16)
        net.WriteInt(ply:GetMaxHealth(), 16)
    end
    net.Send()
end

local function announce(text)
    chat.broadcast("[Zombie Crisis] " .. text)
end

local function cleanupZombies()
    for index, ent in pairs(zombies) do
        if ent and ent:IsValid() then ent:Remove() end
        zombies[index] = nil
        specialByIndex[index] = nil
        specialNextAttack[index] = nil
    end
end

local function getSpawnPosition()
    local players = livingPlayers()
    if #players == 0 then return nil end

    for attempt = 1, 12 do
        local target = players[math.random(1, #players)]
        local angle = math.random() * math.pi * 2
        local distance = math.random(700, 1100)
        local start = target:GetPos() + Vector(math.cos(angle) * distance,
            math.sin(angle) * distance, 256)
        local finish = start - Vector(0, 0, 768)
        local trace = util.TraceHull(start, finish, Vector(-16, -16, 0),
            Vector(16, 16, 72), target)
        if trace and trace.Hit and not trace.StartSolid and not trace.AllSolid then
            return trace.HitPos + Vector(0, 0, 4)
        end
    end
    return nil
end

local function pickTarget(ent)
    local best = nil
    local bestDistance = 100000000
    for _, ply in ipairs(livingPlayers()) do
        local distance = plugin.vector.distance(ent:GetPos(), ply:GetPos())
        if distance < bestDistance then
            best = ply
            bestDistance = distance
        end
    end
    return best
end

local function spawnZombie(classname, kind)
    local position = getSpawnPosition()
    if not position then return nil end

    local ent = ents.Create(classname, position, {
        targetname = "zombie_crisis_infected",
        spawnflags = 256
    })
    if not ent then return nil end

    zombies[ent:EntIndex()] = ent
    if kind then
        specialByIndex[ent:EntIndex()] = kind
        specialNextAttack[ent:EntIndex()] = 0
        ent:SetHealth(specialHealth[kind] or 250)
        ent:SetMaxHealth(specialHealth[kind] or 250)
        ent:SetName("zombie_crisis_" .. kind)
        if kind == "tank" then
            ent:SetModelScale(1.45, 0.2)
        elseif kind == "witch" then
            ent:SetModelScale(1.15, 0.2)
        end
    end

    local target = pickTarget(ent)
    if target then ent:SetEnemy(target) end
    return ent
end

local function spawnCommon(amount)
    for i = 1, amount do
        local class = commonTypes[math.random(1, #commonTypes)]
        spawnZombie(class)
    end
end

local function spawnSpecial()
    local kind = specialTypes[math.random(1, #specialTypes)]
    local ent = spawnZombie(specialClass[kind], kind)
    if ent then
        announce(string.upper(kind) .. " incoming!")
        ent:EmitSound("npc/zombie/zombie_alert1.wav")
    end
end

local function startGame()
    if started then return end
    local players = alivePlayers()
    if #players == 0 then return end
    started = true
    mode = MODE_TRAVEL
    wave = 1
    waveEndsAt = plugin.game.time() + 45
    nextCommonSpawn = plugin.game.time() + 2
    nextSpecialSpawn = plugin.game.time() + 30
    findGoal()
    for _, ply in ipairs(players) do
        ply:Give("weapon_smg1")
        ply:Give("weapon_shotgun")
        ply:GiveAmmo(120, 0)
        ply:GiveAmmo(32, 1)
        ply:ChatPrint("Reach the extraction point. Stay together.")
    end
    announce("Escape operation started. Reach the marked endpoint.")
end

local function stopGame(message)
    cleanupZombies()
    mode = MODE_WAITING
    wave = 0
    started = false
    finaleEndsAt = 0
    if message then announce(message) end
end

local function completeGame()
    cleanupZombies()
    mode = MODE_COMPLETE
    started = false
    announce("Extraction complete. All surviving players reached the endpoint.")
    timer.Simple(8, function()
        if mode == MODE_COMPLETE then
            mode = MODE_WAITING
            wave = 0
        end
    end)
end

local function updateSpecial(ent, kind, now)
    local target = pickTarget(ent)
    if target then ent:SetEnemy(target) end

    if kind == "boomer" or kind == "spitter" then
        local readyAt = specialNextAttack[ent:EntIndex()] or 0
        if now >= readyAt and target and plugin.vector.distance(ent:GetPos(), target:GetPos()) < 260 then
            util.RadiusDamage(ent:GetPos(), kind == "boomer" and 8 or 14,
                kind == "boomer" and 180 or 240, ent, 64, ent)
            ent:EmitSound("npc/zombie/zombie_pain" .. tostring(math.random(1, 3)) .. ".wav")
            specialNextAttack[ent:EntIndex()] = now + 3
        end
    elseif kind == "charger" and target then
        if plugin.vector.distance(ent:GetPos(), target:GetPos()) < 100 then
            target:SetVelocity(Vector(0, 0, 260))
            target:TakeDamage(30, ent, nil, 128)
        end
    elseif kind == "tank" and target then
        if plugin.vector.distance(ent:GetPos(), target:GetPos()) < 120 then
            target:TakeDamage(35, ent, nil, 128)
        end
    end
end

hook.Add("PlayerInitialSpawn", "zombie_crisis_join", function(ply)
    timer.Simple(2, function()
        if ply and ply:IsValid() then
            ply:ChatPrint("Zombie Crisis: reach the endpoint. No safe rooms, no voice system.")
        end
    end)
end)

hook.Add("PlayerSpawn", "zombie_crisis_loadout", function(ply)
    if ply and ply:IsValid() and started then
        ply:Give("weapon_smg1")
        ply:Give("weapon_shotgun")
    end
end)

hook.Add("PlayerSay", "zombie_crisis_commands", function(ply, text)
    if not text then return end
    if text == "/zstart" then
        startGame()
        return false
    elseif text == "/zstop" then
        stopGame("Operation aborted.")
        return false
    elseif text == "/zstatus" then
        ply:ChatPrint(string.format("Mode %d | Wave %d | Infected %d",
            mode, wave, countLivingZombies()))
        return false
    end
end)

hook.Add("PlayerDeath", "zombie_crisis_team_wipe", function(victim, attacker)
    if started and #livingPlayers() == 0 then
        stopGame("All survivors were lost. Use /zstart to retry.")
    end
end)

hook.Add("Think", "zombie_crisis_director", function()
    local now = plugin.game.time()
    local players = livingPlayers()
    if not started then
        sendStatus()
        return
    end
    if #players == 0 then
        stopGame("All survivors were lost. Use /zstart to retry.")
        return
    end

    local goal = goalPosition()
    local reached = 0
    for _, ply in ipairs(players) do
        if plugin.vector.distance(ply:GetPos(), goal) <= 180 then
            reached = reached + 1
        end
    end

    if mode == MODE_TRAVEL and reached == #players then
        mode = MODE_FINALE
        finaleEndsAt = now + 45
        announce("Extraction point reached. Hold for 45 seconds!")
    elseif mode == MODE_FINALE and now >= finaleEndsAt then
        completeGame()
        sendStatus()
        return
    end

    local maxAlive = 8 + #players * 5 + wave * 2
    local living = countLivingZombies()
    if now >= nextCommonSpawn and living < maxAlive then
        local amount = math.min(3 + wave, maxAlive - living)
        spawnCommon(amount)
        nextCommonSpawn = now + (mode == MODE_FINALE and 1.0 or 2.5)
    end

    if now >= waveEndsAt then
        wave = wave + 1
        waveEndsAt = now + math.max(30, 75 - wave * 2)
        announce("Horde wave " .. tostring(wave) .. " started.")
    end

    if now >= nextSpecialSpawn and living < maxAlive then
        spawnSpecial()
        nextSpecialSpawn = now + math.max(25, 75 - wave * 2)
    end

    for index, ent in pairs(zombies) do
        if not ent or not ent:IsValid() or ent:Health() <= 0 then
            zombies[index] = nil
            specialByIndex[index] = nil
            specialNextAttack[index] = nil
        else
            local kind = specialByIndex[index]
            if kind then updateSpecial(ent, kind, now) end
        end
    end
    sendStatus()
end)

hook.Add("LevelInit", "zombie_crisis_reset", function()
    stopGame(nil)
    findGoal()
end)

hook.Add("ShutDown", "zombie_crisis_cleanup", function()
    cleanupZombies()
end)

plugin.register_command("zcrisis_start", function()
    startGame()
end, "Start Zombie Crisis")

plugin.register_command("zcrisis_stop", function()
    stopGame("Operation aborted.")
end, "Stop Zombie Crisis")

plugin.register_command("zcrisis_set_end", function(command, args)
    local x = tonumber(args[1])
    local y = tonumber(args[2])
    local z = tonumber(args[3])
    if not x or not y or not z then
        plugin.log("usage: zcrisis_set_end <x> <y> <z>")
        return
    end
    plugin.config_set("end_x", x)
    plugin.config_set("end_y", y)
    plugin.config_set("end_z", z)
    endEntity = nil
    plugin.log("Zombie Crisis endpoint saved")
end, "Set the Zombie Crisis endpoint coordinates")

if plugin.config_get("auto_start", true) then
    timer.Simple(5, startGame)
end
