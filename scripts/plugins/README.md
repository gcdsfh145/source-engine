# Lua 插件 API

插件文件放在 MOD 根目录下：

```text
scripts/plugins/server/
scripts/plugins/client/
```

## 核心

```lua
plugin.log("hello", 123)
local role = plugin.role() -- "server" 或 "client"
plugin.execute("say hello")
local now = plugin.time()
```

## 定时器

```lua
local once = plugin.timer.after(2.0, function()
    plugin.log("two seconds later")
end)

local repeating = plugin.timer.every(1.0, function()
    plugin.log("tick")
end)

plugin.timer.cancel(repeating)
```

## ConVar、文件和数学

```lua
if plugin.convar.exists("sv_gravity") then
    local gravity = plugin.convar.get_int("sv_gravity")
    plugin.convar.set("sv_gravity", gravity)
end

plugin.file.write("scripts/plugins/data/example.txt", "safe MOD data")
local text = plugin.file.read("scripts/plugins/data/example.txt")

local a = plugin.vector.new(1, 2, 3)
local b = plugin.vector.new(4, 5, 6)
local c = plugin.vector.add(a, b)
local d = a + Vector(8, 0, 0)
local e = d * 0.5
```

`Vector` 支持 `+`、`-`、`*`、`/` 和一元负号；实体返回的位置向量也支持这些运算。

Lua 文件写入被限制在 `scripts/plugins/data/`，不允许绝对路径、反斜杠或 `..`。

## 实体和玩家

```lua
local player = plugin.player.get(1)
if player and player:is_valid() then
    plugin.log(player:name(), player:health(), player:origin().x)
    player:set_origin(plugin.vector.new(0, 0, 64))
end

for _, p in ipairs(plugin.player.all()) do
    plugin.log(p:index(), p:name())
end

local entities = plugin.entity.find("npc_zombie")
local ent = plugin.entity.create("prop_physics") -- server only
if ent then ent:set_origin(plugin.vector.new(0, 0, 64)) end
```

服务端才允许创建/删除实体、修改生命、换队和给玩家武器。

实体还提供状态查询：

```lua
local p = plugin.player.get(1)
if p then
    plugin.log(p:userid(), p:flags(), p:move_type())
    plugin.log(p:buttons(), p:grounded(), p:water_level())
    local view = p:eye_angles()
    plugin.log("vertical", p:vertical_velocity(), "fall speed", p:fall_speed())
    plugin.log("movement scale", p:lagged_movement_value())
end
```

移动诊断 API：`p:vertical_velocity()` 返回 Z 轴速度，`p:fall_speed()` 返回当前下落速度，
`p:ground_entity()` 返回脚下实体（没有脚下实体时返回 `nil`）。服务端可以用
`p:SetLaggedMovementValue(scale)` 调整玩家整体移动时间倍率，范围为 `0` 到 `4`；默认值为 `1`。
`p:GetMaxSpeed()` 和服务端的 `p:SetMaxSpeed(speed)` 可控制水平移动上限，不会改变重力下落。

### Mod 实体外观、物理和 KeyValue

动画实体支持模型缩放、皮肤、bodygroup 和动画播放速度；普通实体支持重力和
摩擦力。`SetModelScale` 的第二个参数是渐变秒数，缩放范围为 `0` 到 `100`。

```lua
local ent = ents.Create("prop_physics", {
    model = "models/props_junk/wood_crate001a.mdl",
    spawnflags = 256
})
if ent then
    ent:SetModelScale(1.25, 0.25)
    ent:SetSkin(1)
    ent:SetBodygroup(0, 0)
    ent:SetGravity(0.65)
    ent:SetFriction(0.2)
    ent:SetKeyValue("rendercolor", "80 180 255")
    plugin.log(ent:GetModelScale(), ent:GetSkin(), ent:GetGravity())
end
```

`ents.Create` 的第二个参数可以是出生位置向量，也可以是可选 KeyValue 表；KeyValue
表会在实体生成前应用，表值支持字符串、
数字和布尔值。`GetKeyValue` 读取引擎能提供的 KeyValue，未知字段返回 `nil`。
`SetKeyValue`、`SetModel` 和实体创建属于服务端操作。

实体还支持 Mod 常用的显示和碰撞查询：

```lua
local mins, maxs = ent:GetBounds()
ent:SetNoDraw(true)
if ent:IsNoDraw() then
    ent:SetNoDraw(false)
end
```

动画模型还可以通过挂点制作炮口、特效和装备位置：

```lua
local attachment = ent:LookupAttachment("muzzle")
local muzzlePos, muzzleAngles = ent:GetAttachment(attachment)
if muzzlePos then
    plugin.log(muzzlePos.x, muzzleAngles.y)
end

local color = ent:GetColor()
ent:SetColor(color.r, color.g, color.b, 180)
plugin.log(ent:GetNumBodyGroups(), ent:GetElasticity(), ent:GetRenderMode())
```

服务端可以在地图/插件初始化时预加载模型和声音。清单需要声明
`resource.precache` 权限：

```lua
plugin.Manifest({
    version = "1.0.0",
    permissions = { "entity.create", "entity.remove", "resource.precache" }
})

util.PrecacheModel("models/props_junk/wood_crate001a.mdl")
util.PrecacheSound("buttons/button14.wav")
```

完整可运行的 Mod 小样例见 `scripts/plugins/server/mod_showcase.lua`。

服务端可以让一个在地面的玩家跳跃。空中跳跃被引擎禁止，`player:jump()`
只能在玩家站在地面时生效：

```lua
if p and p:alive() and p:grounded() then
    p:jump()
end

-- 可指定跳跃高度；不传时使用游戏默认高度。
p:Jump(45)
```

### 玩家身份、视线和范围效果

服务端玩家支持稳定的网络身份、最大生命值和视线 Trace：

```lua
local id = ply:network_id()
ply:SetMaxHealth(150)
local trace = ply:EyeTrace(2048)
if trace.Hit and trace.Entity then
    trace.Entity:TakeDamage(25, ply, nil, 0)
end
```

服务端 `util` 还提供范围伤害和屏幕震动。它们分别需要
`entity.damage` 和 `world.effect` 权限：

```lua
plugin.Manifest({
    permissions = { "entity.damage", "world.effect" }
})

util.RadiusDamage(Vector(0, 0, 64), 80, 256, ply, 64, ply)
util.ScreenShake(Vector(0, 0, 64), 12, 150, 0.8, 600, true)
```

完整的成长、技能、自定义武器和 NPC 组合示例见
`scripts/plugins/examples/server/rpg_mod_foundation.lua`。

## Zombie Crisis 示例模式

`scripts/plugins/examples/server/zombie_crisis.lua` 是完整的合作逃生模式，
客户端 HUD 在 `scripts/plugins/examples/client/zombie_crisis_hud.lua`。将两个文件分别
复制到 `server/` 和 `client/` 后加载：

```text
lua_server_load zombie_crisis.lua
lua_client_load zombie_crisis_hud.lua
```

模式包含普通感染者、Hunter、Smoker、Boomer、Charger、Spitter、Jockey、Witch、Tank，
动态尸潮、特殊感染者计时、队友血量 HUD、终点判定和最终坚持阶段。不包含安全屋、
语音、友军伤害或倒地救援。

地图可以放置名为 `zombie_crisis_exit` 的 `info_target` 作为终点；没有实体时，
服务器控制台可以设置坐标：

```text
zcrisis_set_end <x> <y> <z>
```

运行控制：`/zstart`、`/zstop`、`/zstatus`，或服务器控制台使用
`zcrisis_start` 和 `zcrisis_stop`。`auto_start` 默认会在首批玩家进入后自动开始。

## 游戏状态

```lua
local map = plugin.game.map()
local now = plugin.game.time()
local frameTime = plugin.game.frame_time()
local tick = plugin.game.tick()
local maxClients = plugin.game.max_clients()
local gravity = plugin.game.gravity() -- 当前 sv_gravity
```

## 事件

```lua
plugin.on("level_init", function(mapName) end)
plugin.on("game_frame", function(simulating) end)
plugin.on("client_put_in_server", function(entIndex, playerName) end)
plugin.on("client_disconnect", function(entIndex) end)

plugin.on("player_death", function(userid, attacker, weapon) end)
plugin.on("player_hurt", function(userid, attacker, damage) end)
plugin.on("weapon_fire", function(userid, weapon) end)
plugin.on("player_spawn", function(userid) end)
plugin.on("player_team", function(userid, team) end)
plugin.on("player_say", function(userid, text) end)
plugin.on("item_pickup", function(userid, item) end)
plugin.on("round_start", function() end)
plugin.on("round_end", function(winner, reason) end)
```

## GMod 风格服务端 hook

服务端插件可以使用 `hook.Add`、`hook.Remove`、`timer.Create`、`timer.Remove` 和
`timer.Simple`。命名 timer 属于当前插件，插件卸载时会自动清理；重复次数为 `0`
表示无限重复。

```lua
hook.Add("PlayerSpawn", "give_rpg", function(ply)
    if ply and ply:IsValid() then
        ply:Give("weapon_rpg")
    end
end)

hook.Add("PlayerSay", "where_command", function(ply, text)
    if ply and text == "!where" then
        local pos = ply:GetPos()
        ply:ChatPrint(string.format("%.0f %.0f %.0f", pos.x, pos.y, pos.z))
        return false -- 阻止原消息广播
    end
end)

-- hook 工具还提供查询和手动分发能力。第二个参数是兼容 GMod 的
-- gamemode 返回值；回调返回第一个非 nil 值后停止继续分发。
local installed = hook.List()
local callbacks = hook.GetTable()
local result = hook.Call("PlayerSpawn", nil, player.GetByIndex(1))
hook.Remove("PlayerSay", "where_command")

timer.Create("periodic_message", 30, 0, function()
    plugin.hud.message("服务器公告")
end)
```

GMod 风格 hook 的玩家参数是实体 userdata，可以直接调用 `Nick`、`Health`、
`SetHealth`、`Give`、`ChatPrint`、`GetPos` 等方法。`plugin.on` 保持旧接口，玩家
参数仍然是 userid 或实体索引，便于兼容旧脚本。

`PlayerSay` 是可拦截 Hook：返回 `false` 会阻止聊天消息广播，返回 `nil` 或 `true` 则继续。

聊天 API：服务端可以使用 `chat.send(player, text)` 私聊、`chat.broadcast(text, sender)`
广播玩家聊天格式消息，或使用 `chat.system(text)` 发送系统消息。广播和系统消息需要
Manifest 权限 `chat.broadcast`。完整示例见 `scripts/plugins/examples/server_chat.lua`。

`SetupMove` 可以修改每次移动命令。回调返回的表只应用其中存在的字段：

```lua
hook.Add("SetupMove", "sprint_control", function(ply, buttons, forward, side, up)
    if not ply or not ply:Alive() then return end
    return {
        buttons = buttons,
        forward = math.min(forward * 1.25, 450),
        side = side,
        up = up
    }
end)
```

服务端常用实体操作：

```lua
for _, ent in ipairs(ents.FindByClass("npc_zombie")) do
    if ent:IsValid() then
        ent:SetHealth(100)
    end
end

local prop = ents.Create("prop_physics")
if prop then
    prop:SetPos(Vector(0, 0, 64))
end
```

`ents.Create`、`ents.Remove`、`player:Spawn`、`player:Kill`、`player:Give`、
`player:SetTeam`、`player:SetArmor` 和 `player:StripWeapons` 只能在服务端插件中使用。

## 服务端武器 API

武器 userdata 可以读取和修改弹匣、弹药、换弹冷却，并使用引擎的子弹碰撞逻辑：

```lua
hook.Add("WeaponFire", "custom_pistol_damage", function(ply, weaponName)
    local weapon = ply and ply:GetActiveWeapon()
    if not weapon or not weapon:IsWeapon() then
        return
    end

    if weapon:Clip1() > 0 then
        weapon:SetNextPrimaryFire(0.15)
    end
end)

local weapon = ply:GetActiveWeapon()
if weapon and weapon:IsWeapon() then
    weapon:SetClip1(30)
    weapon:FireBullets(80, 8192, 1, 0.015)
end
```

可用方法包括 `Clip1`、`SetClip1`、`Clip2`、`SetClip2`、`GetMaxClip1`、
`GetPrimaryAmmoType`、`GetOwner`、`Reload`、`GetNextPrimaryFire`、
`SetNextPrimaryFire` 和 `FireBullets`。`FireBullets` 参数依次为伤害、距离、
子弹数量和散布。

## 自定义武器

`weapons.Register` 会注册真正的网络实体。`weapons.Create`、`ents.Create` 和
`player:Give` 都会创建这个实体，而不是创建一个同名的普通 point entity。
服务端回调中的第一个参数是实体 userdata 的 `EntIndex`，可以用
`ents.GetByIndex(entIndex)` 取回实体。

```lua
weapons.Register("weapon_laser", {
    model = "models/weapons/w_pistol.mdl",
    clip_size = 20,
    OnSpawn = function(index)
        local weapon = ents.GetByIndex(index)
        if weapon then weapon:SetName("laser_weapon") end
    end,
    PrimaryAttack = function(index)
        local weapon = ents.GetByIndex(index)
        if weapon and weapon:FireBullets(35, 8192, 1, 0.005) then
            weapon:SetNextPrimaryFire(0.15)
        end
    end,
    SecondaryAttack = function(index)
        local weapon = ents.GetByIndex(index)
        if weapon then weapon:SetNextPrimaryFire(0.5) end
    end,
    Reload = function(index)
        -- 返回 true 表示 Lua 已处理换弹；返回 false/nil 使用原生换弹。
        return false
    end,
    Think = function(index)
        local weapon = ents.GetByIndex(index)
        if weapon and weapon:IsValid() then
            -- 每个服务端 tick 执行，可在这里做热量、充能等逻辑。
        end
    end,
    OnRemove = function(index) end
})

hook.Add("PlayerSpawn", "give_laser", function(ply)
    if ply and ply:IsValid() then
        ply:Give("weapon_laser")
    end
end)
```

武器继承引擎的 `CBaseCombatWeapon`，拥有原生装备、攻击输入、网络同步和
弹匣字段。定义表目前支持 `model`、`clip_size`/`clip1` 和上述生命周期回调；
伤害、散布、冷却和特效可以在 `PrimaryAttack` 中通过现有武器 API 控制。

## 自定义 NPC

`npcs.Register` 创建的是可网络同步、可碰撞、可受伤的动画实体。它不强行套用
Source AI schedule，NPC 的目标选择、移动和攻击逻辑由 Lua 的 `Think` 自己决定，
这样可以制作 Boss、宠物、炮塔和特殊生物。

```lua
npcs.Register("npc_laser_boss", {
    model = "models/zombie/classic.mdl",
    health = 2000,
    OnSpawn = function(index)
        local boss = ents.GetByIndex(index)
        if boss then boss:SetName("laser_boss") end
    end,
    Think = function(index)
        local boss = ents.GetByIndex(index)
        local target = player.GetByIndex(1)
        if boss and target and target:Alive() then
            -- 这里可以用 SetVelocity、util.TraceLine 和 TakeDamage 实现 AI。
        end
    end,
    OnTakeDamage = function(index, damage, attackerIndex)
        -- 返回 false 阻止本次伤害，返回 true 或不返回则允许伤害。
        return true
    end,
    OnDeath = function(index, attackerIndex) end,
    OnRemove = function(index) end
})

local boss = npcs.Create("npc_laser_boss")
if boss then
    boss:SetPos(Vector(0, 0, 64))
end
```

自定义 NPC 的 `model`、`health` 和 `OnSpawn`、`Think`、`OnTakeDamage`、
`OnDeath`、`OnRemove` 均由服务端管理。需要原生 nav、schedule 或复杂感知系统
时，仍可在 Lua 中调用现有实体接口；下一层可以再接入专门的 `CAI_BaseNPC` 适配器。

引擎已有 NPC 仍然可以直接使用：

```lua
local boss = ents.Create("npc_zombie")
if boss then
    boss:SetName("lua_boss")
    boss:SetModel("models/zombie/classic.mdl")
    boss:SetPos(Vector(0, 0, 64))
    boss:SetHealth(2000)
    boss:Fire("Wake")
end

hook.Add("Think", "lua_boss_ai", function()
    for _, ent in ipairs(ents.FindByClass("npc_zombie")) do
        if ent:IsValid() and ent:GetName() == "lua_boss" then
            -- 在这里加入目标搜索、移动和攻击逻辑。
        end
    end
end)
```

实体还支持 `GetName`、`SetName`、`GetModel`、`SetModel`、`Fire`、`Remove` 和
`TakeDamage`。例如：

```lua
boss:TakeDamage(50, 0, ply, ply:GetActiveWeapon())
local trace = util.TraceLine(ply:GetShootPos(), boss:GetPos(), ply)
if trace.Hit and trace.Entity then
    trace.Entity:TakeDamage(100, 0, ply, ply:GetActiveWeapon())
end
```

这些接口可以制作武器变体、Boss、刷怪器、触发器和服务器小游戏。自定义武器和
NPC 的网络实体注册层已经可用；专属网络字段仍应先在 C++ 增加，再通过 Lua 回调
暴露，避免让脚本直接读写不安全的内存。

## HUD 和网络

```lua
plugin.hud.show_fps(true) -- client only
plugin.hud.message("Lua message")
```

## 地图预览和游戏文件浏览

客户端 Lua 可以读取 MOD 搜索路径中的文件名和文本内容，但路径必须是相对路径，
不能使用绝对路径、反斜杠或 `..`。目录枚举最多返回 256 项，适合制作 Mod 浏览器、
配置查看器和资源检查工具。

地图 API 会扫描 `maps/*.bsp`，并自动寻找以下预览图之一：
`maps/<map>.jpg`、`maps/<map>.png`、`materials/maps/<map>/preview.jpg` 或
`materials/maps/<map>/preview.png`。

```lua
local current = plugin.map.current()
plugin.log(current.name, current.file, current.preview or "no preview")

for _, map in ipairs(plugin.map.list()) do
    plugin.log(map.name, map.preview or "no preview")
end

for _, item in ipairs(plugin.file.list("scripts/plugins")) do
    plugin.log(item.directory and "directory" or "file", item.path)
end

local text = plugin.file.read("scripts/plugins/README.md")
if text then
    plugin.log(string.sub(text, 1, 200))
end
```

客户端还支持将地图预览图显示到 HUD：

```lua
local map = plugin.map.current()
if map.preview then
    plugin.hud.create_image("map_preview", 32, 32, 320, 180, map.preview,
        {r=255, g=255, b=255, a=255})
end
```

示例插件：`scripts/plugins/client/map_file_browser.lua`。加载后可执行
`lua_map_browser`，或者使用 `lua_map_preview <map>` 与
`lua_file_view <relative path>`。

服务端向客户端发送自定义消息：

```lua
plugin.net.send("welcome", "hello client") -- 发给所有客户端
plugin.net.send("private", "payload", 1) -- 发给实体 1
```

客户端接收：

```lua
plugin.on("net_message", function(name, payload)
    plugin.log(name, payload)
end)
```

也可以按消息名注册接收回调，回调中直接读取 Typed Net 字段：

```lua
net.Receive("score", function()
    local score = net.ReadInt()
    local pos = net.ReadVector()
    plugin.log("score", score, pos)
end)
```

服务端接收客户端消息时，回调第一个参数是发送玩家：

```lua
plugin.Manifest({ permissions = { "net.receive" } })
net.Receive("client_ping", function(ply)
    if ply then chat.send(ply, "Pong") end
end)
```

客户端可以反向发送 Typed Net：

```lua
net.Start("client_ping")
net.WriteString("hello")
net.SendToServer()
```

客户端发送消息名和字符串字段不能包含引号、反斜杠或换行；普通整数、浮点数、布尔值、
向量和实体字段不受影响。

标准库只开放 `base`、`table`、`string` 和 `math`；`io`、`os`、`debug`、`package` 不开放。

## C++ 主菜单管理

主菜单中的 `Lua Plugins` 可以分别管理 client/server 插件：

- `Enable This Session`：只在当前运行中加载。
- `Disable This Session`：只卸载当前运行实例。
- `Permanent Enable`：移除永久禁用状态并立即加载。
- `Permanent Disable`：写入永久禁用状态并卸载。

永久禁用列表分别保存在：

```text
scripts/plugins/client/disabled.cfg
scripts/plugins/server/disabled.cfg
```

状态写入引擎的 `DEFAULT_WRITE_PATH`，启动时优先读取可写文件；因此 Android 上不会被 APK 内的旧配置覆盖。

## Manifest、配置和热重载

插件可以声明版本、依赖、权限和默认配置。没有 Manifest 的旧插件保持兼容；声明
permissions 后，危险 API 只允许清单中的权限：

```lua
plugin.Manifest({
    version = "1.2.0",
    dependencies = { "shared_base" },
    permissions = {
        "engine.execute", "convar.write", "file.write",
        "entity.create", "entity.remove", "net.send", "config.write"
    },
    config = { welcome = true, max_npcs = 8 }
})

local enabled = plugin.config_get("welcome", false)
local saved = plugin.config_set("max_npcs", 12) -- 自动保存到插件 cfg
plugin.config_save() -- 也可以手动保存
local info = plugin.info()
plugin.log(info.name, info.version, plugin.has_permission("net.send"))
```

依赖插件会在加载扫描中优先完成；缺失依赖的插件不会执行。`plugin.reload()` 会在
当前帧结束后安全热重载当前插件。一个插件连续出现 5 次运行时错误后会自动停用并
写入对应角色的 `disabled.cfg`。

## 原生 AI NPC

设置 `native_ai = 1` 可以使用真正的 `CAI_BaseNPC` 生命周期、Nav 和 Schedule：

```lua
npcs.Register("npc_guard", {
    native_ai = 1,
    model = "models/combine_soldier.mdl",
    health = 300,
    Think = function(index)
        local npc = ents.GetByIndex(index)
        local ply = player.GetByIndex(1)
        if npc and ply then npc:SetEnemy(ply) end
    end,
    OnTakeDamage = function(index, damage, attacker)
        return damage < 100
    end
})
local guard = npcs.Create("npc_guard")
```

Native AI 实体支持 `GetEnemy`、`SetEnemy`、`SetSchedule`、`ClearSchedule` 和
`SetCondition`。普通 `lua_npc` 仍适合完全由 Lua 驱动的 Boss、宠物和炮塔。
自定义 NPC 还支持 `Touch(index, otherIndex)`、`Use(index, activatorIndex)` 和
`OnRemove(index)` 回调；武器支持 `OnDeploy(index, ...)`、`OnHolster(index)`。
插件卸载时会自动移除自己的定义，也可以主动调用 `weapons.Unregister(name)` 或
`npcs.Unregister(name)`。

## HUD、Typed Net 和伤害 Hook

### 双端 GUI 聊天

直接启用 `scripts/plugins/server/chat_gui.lua` 和
`scripts/plugins/client/chat_gui.lua` 即可使用。玩家使用原生聊天输入框发送消息，
服务端广播后由客户端 Lua HUD 显示；`lua_chat_toggle` 可以隐藏或显示聊天面板。
Android 版本使用原生聊天输入框，已有的 `Y` 键和控制台方式仍然可用。

客户端 HUD 支持文本、矩形、颜色、位置、显示状态和删除：

```lua
plugin.hud.create_rect("panel", 20, 20, 260, 48, {r=0, g=0, b=0, a=180})
plugin.hud.create_text("label", 32, 32, "Lua HUD", 22, {r=255, g=220, b=80, a=255})
plugin.hud.set_text("label", "updated")
plugin.hud.set_visible("panel", true)
plugin.hud.remove("label")
```

网络 API 支持 `net.Start`、`WriteString`、`WriteInt`、`WriteFloat`、`WriteBool`、
`WriteVector`、`WriteEntity`、`Send`，以及客户端对应的 `Read*` 方法：

```lua
-- server
net.Start("score")
net.WriteInt(10)
net.WriteVector(Vector(0, 0, 64))
net.Send()

-- client
plugin.on("net_message", function(name)
    if name == "score" then
        local score = net.ReadInt()
        local pos = net.ReadVector()
    end
end)
```

`hook.Add("EntityTakeDamage", id, callback)` 是可拦截 Hook。回调参数是实体索引、
伤害值和攻击者索引；返回 `false` 会阻止本次伤害：

```lua
hook.Add("EntityTakeDamage", "friendly_fire_guard", function(ent, damage, attacker)
    if ent == attacker then return false end
end)
```

`plugin.debug_stats()` 返回当前 Lua 状态的插件数、回调数、定时器数、本帧回调数和
累计指令数，可用于定位插件卡顿：

```lua
local stats = plugin.debug_stats()
plugin.log(stats.plugins, stats.callbacks, stats.timers, stats.instructions)
```

## 限制和诊断

`hook.List()`、`hook.GetTable()`、`hook.Call()` 和 `hook.Remove()` 可用于诊断及组合
插件。Lua 默认关闭 `io`、`os`、`debug`、`package`，文件写入只能进入
`scripts/plugins/data/`。每帧回调预算由 `lua_max_callbacks_per_frame` 控制，单次
回调指令预算由 `lua_max_instructions_per_callback` 控制，设置为 `0` 可关闭对应限制。

## 可直接复制的插件

`scripts/plugins/examples/` 中提供了几个不会自动加载的示例：

- `server_welcome.lua`：欢迎消息、聊天命令
- `server_healing_zone.lua`：治疗区域
- `server_laser_weapon.lua`：自定义激光武器
- `server_guard_npc.lua`：Lua 控制的 NPC 守卫
- `client_status_hud.lua`：地图名和 FPS HUD

确认脚本后，将服务端插件复制到 `scripts/plugins/server/`，客户端插件复制到
`scripts/plugins/client/`，重启或执行插件热重载即可。
