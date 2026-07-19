//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Game DLL integration for the client/server Lua plugin managers.
//
//=============================================================================

#include "cbase.h"
#include "igamesystem.h"
#include "lua/lua_plugin_manager.h"
#include "lua_plugin_system.h"
#include "filesystem.h"
#include "tier1/convar.h"
#include "usermessages.h"
#include "gamemovement.h"
#include "basecombatweapon_shared.h"
#include "ammodef.h"

#ifndef CLIENT_DLL
#include "enginecallback.h"
#include "ai_basenpc.h"
#include "recipientfilter.h"
#include "util_shared.h"
#include "takedamageinfo.h"
#include "variant_t.h"
#endif

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
}

#ifdef CLIENT_DLL
#include "cdll_int.h"
#include "c_ai_basenpc.h"
#include "lua_hud.h"
extern IVEngineClient *engine;
#else
#include "eiface.h"
extern IVEngineServer *engine;
#endif

extern IFileSystem *g_pFullFileSystem;

static void InstallLuaGameBindings( lua_State *state, void *context );

#ifdef CLIENT_DLL
static CLuaPluginManager *s_LuaClientManager = NULL;

static void LuaNetworkMessageHook( bf_read &message )
{
	char name[128];
	char payload[4096];
	message.ReadString( name, sizeof( name ) );
	message.ReadString( payload, sizeof( payload ) );
	if ( s_LuaClientManager )
		s_LuaClientManager->NetworkMessage( name, payload );
}
#else
static CLuaPluginManager *s_LuaServerManager = NULL;
#endif

static CLuaPluginManager *LuaGamePluginManager()
{
#ifdef CLIENT_DLL
	return s_LuaClientManager;
#else
	return s_LuaServerManager;
#endif
}

#ifdef CLIENT_DLL
#define CLuaWeaponEntity C_LuaWeaponEntity
#endif
class CLuaWeaponEntity : public CBaseCombatWeapon
{
public:
	DECLARE_CLASS( CLuaWeaponEntity, CBaseCombatWeapon );
	DECLARE_NETWORKCLASS();

	CLuaWeaponEntity()
	{
	}

	virtual int GetMaxClip1() const
	{
		CLuaPluginManager *manager = LuaGamePluginManager();
		return manager ? manager->GetCustomDefinitionInt( "weapon", m_luaDefinition,
			"clip_size", BaseClass::GetMaxClip1() ) : BaseClass::GetMaxClip1();
	}

	virtual int GetDefaultClip1() const
	{
		CLuaPluginManager *manager = LuaGamePluginManager();
		if ( !manager )
			return BaseClass::GetDefaultClip1();
		return manager->GetCustomDefinitionInt( "weapon", m_luaDefinition, "clip1",
			manager->GetCustomDefinitionInt( "weapon", m_luaDefinition, "clip_size",
			BaseClass::GetDefaultClip1() ) );
	}

#ifndef CLIENT_DLL
	void SetLuaDefinition( const char *name )
	{
		Q_strncpy( m_luaDefinition.GetForModify(), name ? name : "", 128 );
	}

	virtual void Precache()
	{
		CLuaPluginManager *manager = LuaGamePluginManager();
		char model[256];
		if ( manager && manager->GetCustomDefinitionString( "weapon", m_luaDefinition,
			"model", model, sizeof( model ) ) && model[0] )
			PrecacheModel( model, true );
		if ( manager && manager->GetCustomDefinitionString( "weapon", m_luaDefinition,
			"world_model", model, sizeof( model ) ) && model[0] )
			PrecacheModel( model, true );
		if ( manager && manager->GetCustomDefinitionString( "weapon", m_luaDefinition,
			"view_model", model, sizeof( model ) ) && model[0] )
			PrecacheModel( model, true );
		BaseClass::Precache();
	}

	virtual void Spawn()
	{
		BaseClass::Spawn();
		CLuaPluginManager *manager = LuaGamePluginManager();
		if ( manager )
		{
			char model[256];
			bool hasWorldModel = manager->GetCustomDefinitionString( "weapon", m_luaDefinition,
				"world_model", model, sizeof( model ) ) && model[0];
			if ( !hasWorldModel )
				hasWorldModel = manager->GetCustomDefinitionString( "weapon", m_luaDefinition,
					"model", model, sizeof( model ) ) && model[0];
			if ( hasWorldModel )
				SetModel( model );
			if ( manager->GetCustomDefinitionString( "weapon", m_luaDefinition,
				"view_model", model, sizeof( model ) ) && model[0] )
				m_iViewModelIndex = PrecacheModel( model, true );
		m_iWorldModelIndex = GetModelIndex();
		char ammo[64];
		if ( manager->GetCustomDefinitionString( "weapon", m_luaDefinition,
			"ammo_type", ammo, sizeof( ammo ) ) && ammo[0] )
			m_iPrimaryAmmoType = GetAmmoDef()->Index( ammo );
			m_iClip1 = manager->GetCustomDefinitionInt( "weapon", m_luaDefinition,
				"clip1", manager->GetCustomDefinitionInt( "weapon", m_luaDefinition,
				"clip_size", -1 ) );
			manager->CallCustomEntityHook( "weapon", m_luaDefinition, "OnSpawn", entindex() );
		}
		SetThink( &CLuaWeaponEntity::LuaThink );
		SetNextThink( gpGlobals->curtime );
	}

	virtual void PrimaryAttack()
	{
		CLuaPluginManager *manager = LuaGamePluginManager();
		if ( manager && manager->CallCustomEntityHook( "weapon", m_luaDefinition,
			"PrimaryAttack", entindex() ) )
			return;
		if ( !manager )
		{
			BaseClass::PrimaryAttack();
			return;
		}
		int damage = manager->GetCustomDefinitionInt( "weapon", m_luaDefinition, "damage", 0 );
		if ( damage <= 0 || !GetOwner() )
		{
			BaseClass::PrimaryAttack();
			return;
		}
		CBasePlayer *owner = ToBasePlayer( GetOwner() );
		if ( !owner || !owner->IsAlive() )
			return;
		int shots = MAX( 1, manager->GetCustomDefinitionInt( "weapon", m_luaDefinition, "shots", 1 ) );
		if ( UsesClipsForAmmo1() )
		{
			if ( m_iClip1 < shots )
			{
				m_flNextPrimaryAttack = gpGlobals->curtime + 0.2f;
				return;
			}
			m_iClip1 -= shots;
		}
		else if ( m_iPrimaryAmmoType >= 0 )
		{
			if ( owner->GetAmmoCount( m_iPrimaryAmmoType ) < shots )
				return;
			owner->RemoveAmmo( shots, m_iPrimaryAmmoType );
		}
		Vector direction;
		AngleVectors( owner->EyeAngles(), &direction );
		float spread = MAX( 0.0f, manager->GetCustomDefinitionFloat( "weapon", m_luaDefinition,
			"spread", 0.0f ) );
		float distance = MAX( 1.0f, manager->GetCustomDefinitionFloat( "weapon", m_luaDefinition,
			"distance", 8192.0f ) );
		owner->FireBullets( shots, owner->Weapon_ShootPosition(), direction,
			Vector( spread, spread, spread ), distance, m_iPrimaryAmmoType, 4,
			entindex(), -1, damage, owner, false, true );
		WeaponSound( SINGLE );
		owner->DoMuzzleFlash();
		m_flNextPrimaryAttack = gpGlobals->curtime + MAX( 0.01f,
			manager->GetCustomDefinitionFloat( "weapon", m_luaDefinition, "fire_rate", 0.2f ) );
	}

	virtual void SecondaryAttack()
	{
		CLuaPluginManager *manager = LuaGamePluginManager();
		if ( !manager || !manager->CallCustomEntityHook( "weapon", m_luaDefinition,
			"SecondaryAttack", entindex() ) )
			BaseClass::SecondaryAttack();
	}

	virtual bool Reload()
	{
		CLuaPluginManager *manager = LuaGamePluginManager();
		bool handled = false;
		if ( manager && manager->CallCustomEntityHook( "weapon", m_luaDefinition,
			"Reload", entindex(), -1.0f, -1, &handled ) && handled )
			return true;
		return BaseClass::Reload();
	}

	void LuaThink()
	{
		SetNextThink( gpGlobals->curtime );
		CLuaPluginManager *manager = LuaGamePluginManager();
		if ( manager )
			manager->CallCustomEntityHook( "weapon", m_luaDefinition, "Think", entindex() );
	}

	virtual void UpdateOnRemove()
	{
		CLuaPluginManager *manager = LuaGamePluginManager();
		if ( manager )
			manager->CallCustomEntityHook( "weapon", m_luaDefinition, "OnRemove", entindex() );
		BaseClass::UpdateOnRemove();
	}
#else
	virtual void Spawn()
	{
		BaseClass::Spawn();
	}
#endif

	private:
	CNetworkString( m_luaDefinition, 128 );
};

IMPLEMENT_NETWORKCLASS_ALIASED( LuaWeaponEntity, DT_LuaWeaponEntity )
BEGIN_NETWORK_TABLE( CLuaWeaponEntity, DT_LuaWeaponEntity )
#ifdef CLIENT_DLL
	RecvPropString( RECVINFO( m_luaDefinition ) ),
#else
	SendPropString( SENDINFO( m_luaDefinition ) ),
#endif
END_NETWORK_TABLE()
LINK_ENTITY_TO_CLASS( lua_weapon, CLuaWeaponEntity );

#ifdef CLIENT_DLL
#define CLuaNPCEntity C_LuaNPCEntity
#endif
class CLuaNPCEntity : public CBaseAnimating
{
public:
	DECLARE_CLASS( CLuaNPCEntity, CBaseAnimating );
	DECLARE_NETWORKCLASS();

	CLuaNPCEntity()
	{
	}

#ifndef CLIENT_DLL
	void SetLuaDefinition( const char *name )
	{
		Q_strncpy( m_luaDefinition.GetForModify(), name ? name : "", 128 );
	}

	virtual void Precache()
	{
		CLuaPluginManager *manager = LuaGamePluginManager();
		char model[256];
		if ( manager && manager->GetCustomDefinitionString( "npc", m_luaDefinition,
			"model", model, sizeof( model ) ) && model[0] )
			PrecacheModel( model, true );
		BaseClass::Precache();
	}

	virtual void Spawn()
	{
		BaseClass::Spawn();
		CLuaPluginManager *manager = LuaGamePluginManager();
		if ( manager )
		{
			char model[256];
			if ( manager->GetCustomDefinitionString( "npc", m_luaDefinition,
				"model", model, sizeof( model ) ) && model[0] )
				SetModel( model );
			int health = manager->GetCustomDefinitionInt( "npc", m_luaDefinition, "health", 100 );
			SetHealth( health > 0 ? health : 1 );
			manager->CallCustomEntityHook( "npc", m_luaDefinition, "OnSpawn", entindex() );
		}
		SetSolid( SOLID_BBOX );
		SetMoveType( MOVETYPE_STEP );
		SetCollisionBounds( Vector( -16, -16, 0 ), Vector( 16, 16, 72 ) );
		SetThink( &CLuaNPCEntity::LuaThink );
		SetNextThink( gpGlobals->curtime );
	}

	void LuaThink()
	{
		SetNextThink( gpGlobals->curtime );
		CLuaPluginManager *manager = LuaGamePluginManager();
		if ( manager )
			manager->CallCustomEntityHook( "npc", m_luaDefinition, "Think", entindex() );
	}

	virtual int OnTakeDamage( const CTakeDamageInfo &info )
	{
		CLuaPluginManager *manager = LuaGamePluginManager();
		bool allowDamage = true;
		int attackerIndex = info.GetAttacker() ? info.GetAttacker()->entindex() : -1;
		if ( manager && manager->CallCustomEntityHook( "npc", m_luaDefinition,
			"OnTakeDamage", entindex(), info.GetDamage(), attackerIndex, &allowDamage ) && !allowDamage )
			return 0;
		return BaseClass::OnTakeDamage( info );
	}

	virtual void Event_Killed( const CTakeDamageInfo &info )
	{
		CLuaPluginManager *manager = LuaGamePluginManager();
		int attackerIndex = info.GetAttacker() ? info.GetAttacker()->entindex() : -1;
		if ( manager )
			manager->CallCustomEntityHook( "npc", m_luaDefinition, "OnDeath", entindex(), -1.0f,
				attackerIndex );
		BaseClass::Event_Killed( info );
	}

	virtual void UpdateOnRemove()
	{
		CLuaPluginManager *manager = LuaGamePluginManager();
		if ( manager )
			manager->CallCustomEntityHook( "npc", m_luaDefinition, "OnRemove", entindex() );
		BaseClass::UpdateOnRemove();
	}
#endif

private:
	CNetworkString( m_luaDefinition, 128 );
};

IMPLEMENT_NETWORKCLASS_ALIASED( LuaNPCEntity, DT_LuaNPCEntity )
BEGIN_NETWORK_TABLE( CLuaNPCEntity, DT_LuaNPCEntity )
#ifdef CLIENT_DLL
	RecvPropString( RECVINFO( m_luaDefinition ) ),
#else
	SendPropString( SENDINFO( m_luaDefinition ) ),
#endif
END_NETWORK_TABLE()
LINK_ENTITY_TO_CLASS( lua_npc, CLuaNPCEntity );

#ifdef CLIENT_DLL
#define CLuaAINPCEntity C_LuaAINPCEntity
#define CAI_BaseNPC C_AI_BaseNPC
#endif
class CLuaAINPCEntity : public CAI_BaseNPC
{
public:
	DECLARE_CLASS( CLuaAINPCEntity, CAI_BaseNPC );
	DECLARE_NETWORKCLASS();

#ifndef CLIENT_DLL
	void SetLuaDefinition( const char *name )
	{
		Q_strncpy( m_luaDefinition.GetForModify(), name ? name : "", 128 );
	}

	virtual void Precache()
	{
		CLuaPluginManager *manager = LuaGamePluginManager();
		char model[256];
		if ( manager && manager->GetCustomDefinitionString( "npc", m_luaDefinition,
			"model", model, sizeof( model ) ) && model[0] )
			PrecacheModel( model, true );
		BaseClass::Precache();
	}

	virtual void Spawn()
	{
		Precache();
		CLuaPluginManager *manager = LuaGamePluginManager();
		char model[256];
		if ( manager && manager->GetCustomDefinitionString( "npc", m_luaDefinition,
			"model", model, sizeof( model ) ) && model[0] )
			SetModel( model );
		SetSolid( SOLID_BBOX );
		SetMoveType( MOVETYPE_STEP );
		SetCollisionBounds( NAI_Hull::Mins( HULL_HUMAN ), NAI_Hull::Maxs( HULL_HUMAN ) );
		CapabilitiesAdd( bits_CAP_MOVE_GROUND | bits_CAP_OPEN_DOORS );
		if ( manager )
			SetHealth( MAX( 1, manager->GetCustomDefinitionInt( "npc", m_luaDefinition, "health", 100 ) ) );
		NPCInit();
		if ( manager )
			manager->CallCustomEntityHook( "npc", m_luaDefinition, "OnSpawn", entindex() );
	}

	virtual void PrescheduleThink()
	{
		BaseClass::PrescheduleThink();
		CLuaPluginManager *manager = LuaGamePluginManager();
		if ( manager )
			manager->CallCustomEntityHook( "npc", m_luaDefinition, "Think", entindex() );
	}

	virtual int OnTakeDamage( const CTakeDamageInfo &info )
	{
		CLuaPluginManager *manager = LuaGamePluginManager();
		bool allowDamage = true;
		int attackerIndex = info.GetAttacker() ? info.GetAttacker()->entindex() : -1;
		if ( manager && manager->CallCustomEntityHook( "npc", m_luaDefinition,
			"OnTakeDamage", entindex(), info.GetDamage(), attackerIndex, &allowDamage ) && !allowDamage )
			return 0;
		return BaseClass::OnTakeDamage( info );
	}

	virtual void Event_Killed( const CTakeDamageInfo &info )
	{
		CLuaPluginManager *manager = LuaGamePluginManager();
		int attackerIndex = info.GetAttacker() ? info.GetAttacker()->entindex() : -1;
		if ( manager )
			manager->CallCustomEntityHook( "npc", m_luaDefinition, "OnDeath", entindex(), -1.0f,
				attackerIndex );
		BaseClass::Event_Killed( info );
	}

	virtual void UpdateOnRemove()
	{
		CLuaPluginManager *manager = LuaGamePluginManager();
		if ( manager )
			manager->CallCustomEntityHook( "npc", m_luaDefinition, "OnRemove", entindex() );
		BaseClass::UpdateOnRemove();
	}
#else
	virtual void Spawn()
	{
		BaseClass::Spawn();
	}
#endif

private:
	CNetworkString( m_luaDefinition, 128 );
};

IMPLEMENT_NETWORKCLASS_ALIASED( LuaAINPCEntity, DT_LuaAINPCEntity )
BEGIN_NETWORK_TABLE( CLuaAINPCEntity, DT_LuaAINPCEntity )
#ifdef CLIENT_DLL
	RecvPropString( RECVINFO( m_luaDefinition ) ),
#else
	SendPropString( SENDINFO( m_luaDefinition ) ),
#endif
END_NETWORK_TABLE()
LINK_ENTITY_TO_CLASS( lua_ai_npc, CLuaAINPCEntity );

static void ExecuteLuaCommand( const char *command, void *context )
{
	(void)context;
	if ( !command || !command[0] || !engine )
		return;

#ifdef CLIENT_DLL
	engine->ClientCmd_Unrestricted( command );
#else
	char commandWithNewline[1024];
	Q_snprintf( commandWithNewline, sizeof( commandWithNewline ), "%s\n", command );
	engine->ServerCommand( commandWithNewline );
	#endif
}

static const char s_LuaEntityMeta[] = "SourceEngine.Entity";

static int LuaEntityIndex( lua_State *state, int stackIndex )
{
	int *index = (int *)luaL_checkudata( state, stackIndex, s_LuaEntityMeta );
	return index ? *index : -1;
}

static CBaseEntity *LuaEntityPointer( lua_State *state, int stackIndex )
{
	int index = LuaEntityIndex( state, stackIndex );
	if ( index < 0 || index >= MAX_EDICTS )
		return NULL;
	return CBaseEntity::Instance( index );
}

static void LuaPushEntity( lua_State *state, int index )
{
	int *value = (int *)lua_newuserdata( state, sizeof( int ) );
	*value = index;
	luaL_getmetatable( state, s_LuaEntityMeta );
	lua_setmetatable( state, -2 );
}

static bool LuaReadVector( lua_State *state, int stackIndex, Vector &value )
{
	if ( !lua_istable( state, stackIndex ) )
		return false;

	lua_rawgeti( state, stackIndex, 1 );
	float x = (float)luaL_optnumber( state, -1, 0.0 );
	lua_pop( state, 1 );
	lua_rawgeti( state, stackIndex, 2 );
	float y = (float)luaL_optnumber( state, -1, 0.0 );
	lua_pop( state, 1 );
	lua_rawgeti( state, stackIndex, 3 );
	float z = (float)luaL_optnumber( state, -1, 0.0 );
	lua_pop( state, 1 );

	lua_getfield( state, stackIndex, "x" );
	if ( lua_isnumber( state, -1 ) ) x = (float)lua_tonumber( state, -1 );
	lua_pop( state, 1 );
	lua_getfield( state, stackIndex, "y" );
	if ( lua_isnumber( state, -1 ) ) y = (float)lua_tonumber( state, -1 );
	lua_pop( state, 1 );
	lua_getfield( state, stackIndex, "z" );
	if ( lua_isnumber( state, -1 ) ) z = (float)lua_tonumber( state, -1 );
	lua_pop( state, 1 );

	value.Init( x, y, z );
	return true;
}

static bool LuaLooksLikeVector( lua_State *state, int stackIndex )
{
	if ( !lua_istable( state, stackIndex ) )
		return false;
	lua_rawgeti( state, stackIndex, 1 );
	bool numeric = lua_isnumber( state, -1 );
	lua_pop( state, 1 );
	if ( numeric ) return true;
	lua_getfield( state, stackIndex, "x" );
	numeric = lua_isnumber( state, -1 );
	lua_pop( state, 1 );
	return numeric;
}

static void LuaSetVectorMetatable( lua_State *state )
{
	luaL_getmetatable( state, "LuaVectorMeta" );
	if ( lua_istable( state, -1 ) )
		lua_setmetatable( state, -2 );
	else
		lua_pop( state, 1 );
}

static void LuaPushVector( lua_State *state, const Vector &value )
{
	lua_createtable( state, 3, 3 );
	lua_pushnumber( state, value.x ); lua_rawseti( state, -2, 1 );
	lua_pushnumber( state, value.y ); lua_rawseti( state, -2, 2 );
	lua_pushnumber( state, value.z ); lua_rawseti( state, -2, 3 );
	lua_pushnumber( state, value.x ); lua_setfield( state, -2, "x" );
	lua_pushnumber( state, value.y ); lua_setfield( state, -2, "y" );
	lua_pushnumber( state, value.z ); lua_setfield( state, -2, "z" );
	LuaSetVectorMetatable( state );
}

static void LuaPushTriple( lua_State *state, const float value[3] )
{
	lua_createtable( state, 3, 3 );
	for ( int i = 0; i < 3; ++i )
	{
		lua_pushnumber( state, value[i] );
		lua_rawseti( state, -2, i + 1 );
	}
	lua_pushnumber( state, value[0] ); lua_setfield( state, -2, "x" );
	lua_pushnumber( state, value[1] ); lua_setfield( state, -2, "y" );
	lua_pushnumber( state, value[2] ); lua_setfield( state, -2, "z" );
	LuaSetVectorMetatable( state );
}

static int LuaEntityGet( lua_State *state )
{
	int index = (int)luaL_checkinteger( state, 1 );
	if ( index < 0 || index >= MAX_EDICTS || !CBaseEntity::Instance( index ) )
		return 0;
	LuaPushEntity( state, index );
	return 1;
}

static int LuaEntityFind( lua_State *state )
{
	const char *classname = luaL_checkstring( state, 1 );
	lua_newtable( state );
	int resultIndex = 1;
	for ( int index = 0; index < MAX_EDICTS; ++index )
	{
		CBaseEntity *entity = CBaseEntity::Instance( index );
		if ( !entity || Q_stricmp( entity->GetClassname(), classname ) )
			continue;
		LuaPushEntity( state, index );
		lua_rawseti( state, -2, resultIndex++ );
	}
	return 1;
}

static int LuaEntityCreate( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "entity.create is server-only" );
#else
	if ( s_LuaServerManager && !s_LuaServerManager->HasPermission( "entity.create" ) )
		return luaL_error( state, "plugin permission denied: entity.create" );
	const char *classname = luaL_checkstring( state, 1 );
	const char *actualClassname = classname;
	Vector spawnOrigin;
	bool hasSpawnOrigin = LuaLooksLikeVector( state, 2 ) && LuaReadVector( state, 2, spawnOrigin );
	int keyValueIndex = hasSpawnOrigin ? 3 : 2;
	bool customWeapon = s_LuaServerManager &&
		s_LuaServerManager->HasCustomDefinition( "weapon", classname );
	bool customNPC = s_LuaServerManager &&
		s_LuaServerManager->HasCustomDefinition( "npc", classname );
	bool nativeNPC = customNPC && s_LuaServerManager->GetCustomDefinitionInt(
		"npc", classname, "native_ai", 0 ) != 0;
	if ( customWeapon || customNPC )
		actualClassname = customWeapon ? "lua_weapon" : ( nativeNPC ? "lua_ai_npc" : "lua_npc" );

	CBaseEntity *entity = CreateEntityByName( actualClassname );
	if ( entity && customWeapon )
	{
		static_cast< CLuaWeaponEntity * >( entity )->SetLuaDefinition( classname );
		entity->SetName( AllocPooledString( classname ) );
	}
	else if ( entity && customNPC )
	{
		if ( nativeNPC )
			static_cast< CLuaAINPCEntity * >( entity )->SetLuaDefinition( classname );
		else
			static_cast< CLuaNPCEntity * >( entity )->SetLuaDefinition( classname );
		entity->SetName( AllocPooledString( classname ) );
	}
	if ( entity && lua_istable( state, keyValueIndex ) )
	{
		lua_pushnil( state );
		while ( lua_next( state, keyValueIndex ) != 0 )
		{
			if ( lua_isstring( state, -2 ) )
			{
				const char *key = lua_tostring( state, -2 );
				char value[256];
				bool validValue = false;
				if ( lua_isstring( state, -1 ) )
				{
					Q_strncpy( value, lua_tostring( state, -1 ), sizeof( value ) );
					validValue = true;
				}
				else if ( lua_isnumber( state, -1 ) )
				{
					Q_snprintf( value, sizeof( value ), "%g", lua_tonumber( state, -1 ) );
					validValue = true;
				}
				else if ( lua_isboolean( state, -1 ) )
				{
					Q_strncpy( value, lua_toboolean( state, -1 ) ? "1" : "0", sizeof( value ) );
					validValue = true;
				}
				if ( validValue )
					entity->KeyValue( key, value );
			}
			lua_pop( state, 1 );
		}
	}
	if ( entity && hasSpawnOrigin )
		entity->SetAbsOrigin( spawnOrigin );
	if ( !entity || DispatchSpawn( entity ) < 0 )
	{
		if ( entity )
			UTIL_Remove( entity );
		return 0;
	}
	if ( s_LuaServerManager )
		s_LuaServerManager->GameEvent( "entity_created", entity->entindex(), 0, 0, classname );
	LuaPushEntity( state, entity->entindex() );
	return 1;
#endif
}

static int LuaEntityRemove( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "entity.remove is server-only" );
#else
	if ( s_LuaServerManager && !s_LuaServerManager->HasPermission( "entity.remove" ) )
		return luaL_error( state, "plugin permission denied: entity.remove" );
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	if ( !entity )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	UTIL_Remove( entity );
	lua_pushboolean( state, 1 );
	return 1;
#endif
}

static int LuaEntityIsValid( lua_State *state )
{
	lua_pushboolean( state, LuaEntityPointer( state, 1 ) ? 1 : 0 );
	return 1;
}

static int LuaEntityGetIndex( lua_State *state )
{
	lua_pushinteger( state, LuaEntityIndex( state, 1 ) );
	return 1;
}

static int LuaEntityGetClassname( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	if ( !entity ) return 0;
	lua_pushstring( state, entity->GetClassname() );
	return 1;
}

static int LuaEntityGetTargetName( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "entity:GetName is server-only" );
#else
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	if ( !entity ) return 0;
	lua_pushstring( state, STRING( entity->GetEntityName() ) );
	return 1;
#endif
}

static int LuaEntitySetTargetName( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "entity:SetName is server-only" );
#else
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	const char *name = luaL_checkstring( state, 2 );
	if ( !entity )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	entity->SetName( AllocPooledString( name ) );
	lua_pushboolean( state, 1 );
	return 1;
#endif
}

static int LuaEntityGetModel( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "entity:GetModel is server-only" );
#else
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	if ( !entity ) return 0;
	lua_pushstring( state, STRING( entity->GetModelName() ) );
	return 1;
#endif
}

static int LuaEntitySetModel( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "entity:SetModel is server-only" );
#else
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	const char *model = luaL_checkstring( state, 2 );
	if ( !entity )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	entity->SetModel( model );
	lua_pushboolean( state, 1 );
	return 1;
#endif
}

static int LuaEntityGetModelScale( lua_State *state )
{
	CBaseAnimating *entity = dynamic_cast< CBaseAnimating * >( LuaEntityPointer( state, 1 ) );
	if ( !entity ) return 0;
	lua_pushnumber( state, entity->GetModelScale() );
	return 1;
}

static int LuaEntitySetModelScale( lua_State *state )
{
	CBaseAnimating *entity = dynamic_cast< CBaseAnimating * >( LuaEntityPointer( state, 1 ) );
	float scale = (float)luaL_checknumber( state, 2 );
	float duration = (float)luaL_optnumber( state, 3, 0.0 );
	if ( !entity )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	if ( scale <= 0.0f || scale > 100.0f || duration < 0.0f )
		return luaL_error( state, "entity:SetModelScale expects scale in (0, 100] and a non-negative duration" );
	entity->SetModelScale( scale, duration );
	lua_pushboolean( state, 1 );
	return 1;
}

static int LuaEntityGetSkin( lua_State *state )
{
	CBaseAnimating *entity = dynamic_cast< CBaseAnimating * >( LuaEntityPointer( state, 1 ) );
	if ( !entity ) return 0;
	lua_pushinteger( state, entity->m_nSkin );
	return 1;
}

static int LuaEntitySetSkin( lua_State *state )
{
	CBaseAnimating *entity = dynamic_cast< CBaseAnimating * >( LuaEntityPointer( state, 1 ) );
	int skin = (int)luaL_checkinteger( state, 2 );
	if ( !entity )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	if ( skin < 0 )
		return luaL_error( state, "entity:SetSkin expects a non-negative skin index" );
	entity->m_nSkin = skin;
	lua_pushboolean( state, 1 );
	return 1;
}

static int LuaEntityGetBodygroup( lua_State *state )
{
	CBaseAnimating *entity = dynamic_cast< CBaseAnimating * >( LuaEntityPointer( state, 1 ) );
	int group = (int)luaL_checkinteger( state, 2 );
	if ( !entity || group < 0 || group >= entity->GetNumBodyGroups() ) return 0;
	lua_pushinteger( state, entity->GetBodygroup( group ) );
	return 1;
}

static int LuaEntitySetBodygroup( lua_State *state )
{
	CBaseAnimating *entity = dynamic_cast< CBaseAnimating * >( LuaEntityPointer( state, 1 ) );
	int group = (int)luaL_checkinteger( state, 2 );
	int value = (int)luaL_checkinteger( state, 3 );
	if ( !entity || group < 0 || group >= entity->GetNumBodyGroups() || value < 0 )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	entity->SetBodygroup( group, value );
	lua_pushboolean( state, 1 );
	return 1;
}

static int LuaEntityGetGravity( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	if ( !entity ) return 0;
	lua_pushnumber( state, entity->GetGravity() );
	return 1;
}

static int LuaEntitySetGravity( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	float gravity = (float)luaL_checknumber( state, 2 );
	if ( !entity )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	entity->SetGravity( gravity );
	lua_pushboolean( state, 1 );
	return 1;
}

static int LuaEntityGetFriction( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "entity:GetFriction is server-only" );
#else
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	if ( !entity ) return 0;
	lua_pushnumber( state, entity->GetFriction() );
	return 1;
#endif
}

static int LuaEntitySetFriction( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	float friction = (float)luaL_checknumber( state, 2 );
	if ( !entity )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	entity->SetFriction( friction );
	lua_pushboolean( state, 1 );
	return 1;
}

static int LuaEntitySetKeyValue( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "entity:SetKeyValue is server-only" );
#else
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	const char *key = luaL_checkstring( state, 2 );
	const char *value = luaL_checkstring( state, 3 );
	if ( !entity )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	lua_pushboolean( state, entity->KeyValue( key, value ) ? 1 : 0 );
	return 1;
#endif
}

static int LuaEntityGetKeyValue( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	const char *key = luaL_checkstring( state, 2 );
	char value[256];
	if ( !entity || !entity->GetKeyValue( key, value, sizeof( value ) ) )
	{
		lua_pushnil( state );
		return 1;
	}
	lua_pushstring( state, value );
	return 1;
}

static int LuaEntityFireInput( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "entity:Fire is server-only" );
#else
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	const char *input = luaL_checkstring( state, 2 );
	if ( !entity )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}

	variant_t value;
	if ( lua_isboolean( state, 3 ) )
		value.SetBool( lua_toboolean( state, 3 ) != 0 );
	else if ( lua_isnumber( state, 3 ) )
		value.SetFloat( (float)lua_tonumber( state, 3 ) );
	else if ( lua_isstring( state, 3 ) )
		value.SetString( AllocPooledString( lua_tostring( state, 3 ) ) );

	lua_pushboolean( state, entity->AcceptInput( input, entity, entity, value, 0 ) ? 1 : 0 );
	return 1;
#endif
}

static int LuaEntityGetOrigin( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	if ( !entity ) return 0;
	LuaPushVector( state, entity->GetAbsOrigin() );
	return 1;
}

static int LuaEntitySetOrigin( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	Vector value;
	if ( !entity || !LuaReadVector( state, 2, value ) )
		return luaL_error( state, "entity:set_origin expects a vector table" );
	entity->SetAbsOrigin( value );
	return 0;
}

static int LuaEntityGetAngles( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	if ( !entity ) return 0;
	const QAngle &angles = entity->GetAbsAngles();
	Vector value( angles.x, angles.y, angles.z );
	LuaPushVector( state, value );
	return 1;
}

static int LuaEntitySetAngles( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	Vector value;
	if ( !entity || !LuaReadVector( state, 2, value ) )
		return luaL_error( state, "entity:set_angles expects a vector table" );
	entity->SetAbsAngles( QAngle( value.x, value.y, value.z ) );
	return 0;
}

static int LuaEntityGetVelocity( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	if ( !entity ) return 0;
	LuaPushVector( state, entity->GetAbsVelocity() );
	return 1;
}

static int LuaEntitySetVelocity( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	Vector value;
	if ( !entity || !LuaReadVector( state, 2, value ) )
		return luaL_error( state, "entity:set_velocity expects a vector table" );
	entity->SetAbsVelocity( value );
	return 0;
}

static int LuaEntityGetHealth( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	if ( !entity ) return 0;
	lua_pushinteger( state, entity->GetHealth() );
	return 1;
}

static int LuaEntitySetHealth( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "entity:set_health is server-only" );
#else
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	if ( !entity ) return 0;
	entity->SetHealth( (int)luaL_checkinteger( state, 2 ) );
	return 0;
#endif
}

static int LuaEntityTakeDamage( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "entity:TakeDamage is server-only" );
#else
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	float damage = (float)luaL_checknumber( state, 2 );
	int damageType = (int)luaL_optinteger( state, 5, DMG_GENERIC );
	CBaseEntity *attacker = lua_isuserdata( state, 3 ) ? LuaEntityPointer( state, 3 ) : NULL;
	CBaseEntity *weapon = lua_isuserdata( state, 4 ) ? LuaEntityPointer( state, 4 ) : NULL;
	if ( !entity || damage <= 0.0f )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}

	CBaseEntity *inflictor = weapon ? weapon : ( attacker ? attacker : entity );
	CTakeDamageInfo info( inflictor, attacker, weapon, damage, damageType );
	entity->TakeDamage( info );
	lua_pushboolean( state, 1 );
	return 1;
#endif
}

#ifndef CLIENT_DLL
static int LuaUtilTraceLine( lua_State *state )
{
	Vector start;
	Vector end;
	if ( !LuaReadVector( state, 1, start ) || !LuaReadVector( state, 2, end ) )
		return luaL_error( state, "util.TraceLine expects start and end vectors" );

	CBaseEntity *ignore = NULL;
	if ( lua_isuserdata( state, 3 ) )
		ignore = LuaEntityPointer( state, 3 );

	trace_t trace;
	UTIL_TraceLine( start, end, MASK_SHOT, ignore, COLLISION_GROUP_NONE, &trace );
	lua_newtable( state );
	lua_pushboolean( state, trace.DidHit() ? 1 : 0 );
	lua_setfield( state, -2, "Hit" );
	lua_pushnumber( state, trace.fraction );
	lua_setfield( state, -2, "Fraction" );
	lua_pushboolean( state, trace.startsolid ? 1 : 0 );
	lua_setfield( state, -2, "StartSolid" );
	LuaPushVector( state, trace.endpos );
	lua_setfield( state, -2, "HitPos" );
	LuaPushVector( state, trace.plane.normal );
	lua_setfield( state, -2, "HitNormal" );
	lua_pushinteger( state, trace.hitgroup );
	lua_setfield( state, -2, "HitGroup" );
	if ( trace.m_pEnt )
		LuaPushEntity( state, trace.m_pEnt->entindex() );
	else
		lua_pushnil( state );
	lua_setfield( state, -2, "Entity" );
	return 1;
}

static int LuaUtilTraceHull( lua_State *state )
{
	Vector start;
	Vector end;
	Vector mins;
	Vector maxs;
	if ( !LuaReadVector( state, 1, start ) || !LuaReadVector( state, 2, end ) ||
		!LuaReadVector( state, 3, mins ) || !LuaReadVector( state, 4, maxs ) )
		return luaL_error( state, "util.TraceHull expects start, end, mins and maxs vectors" );

	CBaseEntity *ignore = NULL;
	if ( lua_isuserdata( state, 5 ) )
		ignore = LuaEntityPointer( state, 5 );

	trace_t trace;
	UTIL_TraceHull( start, end, mins, maxs, MASK_SHOT_HULL, ignore,
		COLLISION_GROUP_NONE, &trace );
	lua_newtable( state );
	lua_pushboolean( state, trace.DidHit() ? 1 : 0 );
	lua_setfield( state, -2, "Hit" );
	lua_pushnumber( state, trace.fraction );
	lua_setfield( state, -2, "Fraction" );
	lua_pushboolean( state, trace.startsolid ? 1 : 0 );
	lua_setfield( state, -2, "StartSolid" );
	lua_pushboolean( state, trace.allsolid ? 1 : 0 );
	lua_setfield( state, -2, "AllSolid" );
	LuaPushVector( state, trace.endpos );
	lua_setfield( state, -2, "HitPos" );
	LuaPushVector( state, trace.plane.normal );
	lua_setfield( state, -2, "HitNormal" );
	lua_pushinteger( state, trace.hitgroup );
	lua_setfield( state, -2, "HitGroup" );
	if ( trace.m_pEnt )
		LuaPushEntity( state, trace.m_pEnt->entindex() );
	else
		lua_pushnil( state );
	lua_setfield( state, -2, "Entity" );
	return 1;
}

static int LuaUtilPrecacheModel( lua_State *state )
{
	if ( s_LuaServerManager && !s_LuaServerManager->HasPermission( "resource.precache" ) )
		return luaL_error( state, "plugin permission denied: resource.precache" );
	const char *model = luaL_checkstring( state, 1 );
	if ( !model[0] )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	lua_pushinteger( state, CBaseEntity::PrecacheModel( model, true ) );
	return 1;
}

static int LuaUtilPrecacheSound( lua_State *state )
{
	if ( s_LuaServerManager && !s_LuaServerManager->HasPermission( "resource.precache" ) )
		return luaL_error( state, "plugin permission denied: resource.precache" );
	const char *sound = luaL_checkstring( state, 1 );
	if ( !sound[0] )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	lua_pushboolean( state, CBaseEntity::PrecacheSound( sound ) ? 1 : 0 );
	return 1;
}

static CBaseCombatWeapon *LuaWeaponPointer( lua_State *state, int stackIndex )
{
	CBaseEntity *entity = LuaEntityPointer( state, stackIndex );
	return entity && entity->IsBaseCombatWeapon() ? entity->MyCombatWeaponPointer() : NULL;
}

static int LuaWeaponIsWeapon( lua_State *state )
{
	lua_pushboolean( state, LuaWeaponPointer( state, 1 ) ? 1 : 0 );
	return 1;
}

static int LuaWeaponGetOwner( lua_State *state )
{
	CBaseCombatWeapon *weapon = LuaWeaponPointer( state, 1 );
	if ( !weapon || !weapon->GetOwner() ) return 0;
	LuaPushEntity( state, weapon->GetOwner()->entindex() );
	return 1;
}

static int LuaWeaponGetClip1( lua_State *state )
{
	CBaseCombatWeapon *weapon = LuaWeaponPointer( state, 1 );
	if ( !weapon ) return 0;
	lua_pushinteger( state, weapon->Clip1() );
	return 1;
}

static int LuaWeaponSetClip1( lua_State *state )
{
	CBaseCombatWeapon *weapon = LuaWeaponPointer( state, 1 );
	if ( !weapon ) return 0;
	weapon->m_iClip1 = MAX( -1, (int)luaL_checkinteger( state, 2 ) );
	return 0;
}

static int LuaWeaponGetClip2( lua_State *state )
{
	CBaseCombatWeapon *weapon = LuaWeaponPointer( state, 1 );
	if ( !weapon ) return 0;
	lua_pushinteger( state, weapon->Clip2() );
	return 1;
}

static int LuaWeaponSetClip2( lua_State *state )
{
	CBaseCombatWeapon *weapon = LuaWeaponPointer( state, 1 );
	if ( !weapon ) return 0;
	weapon->m_iClip2 = MAX( -1, (int)luaL_checkinteger( state, 2 ) );
	return 0;
}

static int LuaWeaponGetMaxClip1( lua_State *state )
{
	CBaseCombatWeapon *weapon = LuaWeaponPointer( state, 1 );
	if ( !weapon ) return 0;
	lua_pushinteger( state, weapon->GetMaxClip1() );
	return 1;
}

static int LuaWeaponGetAmmoType( lua_State *state )
{
	CBaseCombatWeapon *weapon = LuaWeaponPointer( state, 1 );
	if ( !weapon ) return 0;
	lua_pushinteger( state, weapon->GetPrimaryAmmoType() );
	return 1;
}

static int LuaWeaponGetNextPrimaryAttack( lua_State *state )
{
	CBaseCombatWeapon *weapon = LuaWeaponPointer( state, 1 );
	if ( !weapon ) return 0;
	lua_pushnumber( state, weapon->m_flNextPrimaryAttack );
	return 1;
}

static int LuaWeaponSetNextPrimaryAttack( lua_State *state )
{
	CBaseCombatWeapon *weapon = LuaWeaponPointer( state, 1 );
	if ( !weapon ) return 0;
	float delay = (float)luaL_checknumber( state, 2 );
	weapon->m_flNextPrimaryAttack = gpGlobals->curtime + MAX( 0.0f, delay );
	return 0;
}

static int LuaWeaponReload( lua_State *state )
{
	CBaseCombatWeapon *weapon = LuaWeaponPointer( state, 1 );
	if ( !weapon )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	lua_pushboolean( state, weapon->Reload() ? 1 : 0 );
	return 1;
}

static int LuaWeaponFireBullets( lua_State *state )
{
	CBaseCombatWeapon *weapon = LuaWeaponPointer( state, 1 );
	CBasePlayer *owner = weapon ? ToBasePlayer( weapon->GetOwner() ) : NULL;
	if ( !weapon || !owner || !owner->IsAlive() )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}

	int damage = (int)luaL_checkinteger( state, 2 );
	float distance = (float)luaL_optnumber( state, 3, 8192.0 );
	int shots = (int)luaL_optinteger( state, 4, 1 );
	float spread = (float)luaL_optnumber( state, 5, 0.0 );
	if ( damage <= 0 || distance <= 0.0f || shots <= 0 || spread < 0.0f )
		return luaL_error( state, "weapon:FireBullets received invalid values" );

	if ( weapon->UsesClipsForAmmo1() )
	{
		if ( weapon->Clip1() < shots )
		{
			lua_pushboolean( state, 0 );
			return 1;
		}
		weapon->m_iClip1 -= shots;
	}
	else if ( weapon->GetPrimaryAmmoType() >= 0 )
	{
		if ( owner->GetAmmoCount( weapon->GetPrimaryAmmoType() ) < shots )
		{
			lua_pushboolean( state, 0 );
			return 1;
		}
		owner->RemoveAmmo( shots, weapon->GetPrimaryAmmoType() );
	}

	Vector direction;
	AngleVectors( owner->EyeAngles(), &direction );
	Vector bulletSpread( spread, spread, spread );
	owner->FireBullets( shots, owner->Weapon_ShootPosition(), direction, bulletSpread,
		distance, weapon->GetPrimaryAmmoType(), 4, weapon->entindex(), -1, damage, owner, false, true );
	lua_pushboolean( state, 1 );
	return 1;
}
#endif

static int LuaEntityGetTeam( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	if ( !entity ) return 0;
	lua_pushinteger( state, entity->GetTeamNumber() );
	return 1;
}

static int LuaPlayerGetName( lua_State *state )
{
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	if ( !player ) return 0;
	lua_pushstring( state, player->GetPlayerName() );
	return 1;
}

static int LuaPlayerIsAlive( lua_State *state )
{
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	lua_pushboolean( state, player && player->IsAlive() );
	return 1;
}

static int LuaPlayerSetTeam( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "player:set_team is server-only" );
#else
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	if ( !player ) return 0;
	player->ChangeTeam( (int)luaL_checkinteger( state, 2 ) );
	return 0;
#endif
}

static int LuaPlayerGive( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "player:give is server-only" );
#else
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	const char *classname = luaL_checkstring( state, 2 );
	if ( !player ) return 0;
	CBaseEntity *item = NULL;
	if ( s_LuaServerManager && s_LuaServerManager->HasCustomDefinition( "weapon", classname ) )
	{
		item = CreateEntityByName( "lua_weapon" );
		if ( item )
		{
			static_cast< CLuaWeaponEntity * >( item )->SetLuaDefinition( classname );
			item->SetLocalOrigin( player->GetLocalOrigin() );
			item->AddSpawnFlags( SF_NORESPAWN );
			DispatchSpawn( item );
			if ( !item->IsMarkedForDeletion() )
				player->Weapon_Equip( static_cast< CBaseCombatWeapon * >( item ) );
		}
	}
	else
		item = player->GiveNamedItem( classname );
	if ( !item ) return 0;
	LuaPushEntity( state, item->entindex() );
	return 1;
#endif
}

static int LuaPlayerGet( lua_State *state )
{
	int index = (int)luaL_checkinteger( state, 1 );
	if ( index < 1 || !gpGlobals || index > gpGlobals->maxClients || !UTIL_PlayerByIndex( index ) )
		return 0;
	LuaPushEntity( state, index );
	return 1;
}

static int LuaPlayerAll( lua_State *state )
{
	lua_newtable( state );
	int resultIndex = 1;
	for ( int index = 1; gpGlobals && index <= gpGlobals->maxClients; ++index )
	{
		if ( !UTIL_PlayerByIndex( index ) )
			continue;
		LuaPushEntity( state, index );
		lua_rawseti( state, -2, resultIndex++ );
	}
	return 1;
}

static int LuaGameMap( lua_State *state )
{
	lua_pushstring( state, IGameSystem::MapName() ? IGameSystem::MapName() : "" );
	return 1;
}

static int LuaGameTime( lua_State *state )
{
	lua_pushnumber( state, gpGlobals ? gpGlobals->curtime : 0.0f );
	return 1;
}

static int LuaGameFrameTime( lua_State *state )
{
	lua_pushnumber( state, gpGlobals ? gpGlobals->frametime : 0.0f );
	return 1;
}

static int LuaGameTick( lua_State *state )
{
	lua_pushinteger( state, gpGlobals ? gpGlobals->tickcount : 0 );
	return 1;
}

static int LuaGameMaxClients( lua_State *state )
{
	lua_pushinteger( state, gpGlobals ? gpGlobals->maxClients : 0 );
	return 1;
}

static int LuaEntityGetFlags( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	if ( !entity ) return 0;
	lua_pushinteger( state, entity->GetFlags() );
	return 1;
}

static int LuaEntityGetMoveType( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	if ( !entity ) return 0;
	lua_pushinteger( state, entity->GetMoveType() );
	return 1;
}

static int LuaEntitySetMoveType( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	int moveType = (int)luaL_checkinteger( state, 2 );
	if ( !entity )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	if ( moveType < MOVETYPE_NONE || moveType > MOVETYPE_LAST )
		return luaL_error( state, "invalid entity move type" );
	entity->SetMoveType( (MoveType_t)moveType );
	lua_pushboolean( state, 1 );
	return 1;
}

static int LuaEntitySetSolid( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	int solid = (int)luaL_checkinteger( state, 2 );
	if ( !entity )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	if ( solid < SOLID_NONE || solid >= SOLID_LAST )
		return luaL_error( state, "invalid entity solid type" );
	entity->SetSolid( (SolidType_t)solid );
	lua_pushboolean( state, 1 );
	return 1;
}

static int LuaEntitySetCollisionBounds( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	Vector mins;
	Vector maxs;
	if ( !entity || !LuaReadVector( state, 2, mins ) || !LuaReadVector( state, 3, maxs ) )
		return luaL_error( state, "entity:SetCollisionBounds expects mins and maxs vectors" );
	entity->SetCollisionBounds( mins, maxs );
	lua_pushboolean( state, 1 );
	return 1;
}

static int LuaEntityGetOwner( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	CBaseEntity *owner = entity ? entity->GetOwnerEntity() : NULL;
	if ( !owner ) return 0;
	LuaPushEntity( state, owner->entindex() );
	return 1;
}

static int LuaEntitySetOwner( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	CBaseEntity *owner = lua_isuserdata( state, 2 ) ? LuaEntityPointer( state, 2 ) : NULL;
	if ( !entity )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	entity->SetOwnerEntity( owner );
	lua_pushboolean( state, 1 );
	return 1;
}

static int LuaEntitySetParent( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	CBaseEntity *parent = lua_isuserdata( state, 2 ) ? LuaEntityPointer( state, 2 ) : NULL;
	int attachment = (int)luaL_optinteger( state, 3, -1 );
	if ( !entity )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	entity->SetParent( parent, attachment );
	lua_pushboolean( state, 1 );
	return 1;
}

static int LuaEntityEmitSound( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	const char *sound = luaL_checkstring( state, 2 );
	if ( !entity )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	entity->EmitSound( sound );
	lua_pushboolean( state, 1 );
	return 1;
}

static int LuaEntitySetColor( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	int red = (int)luaL_checkinteger( state, 2 );
	int green = (int)luaL_checkinteger( state, 3 );
	int blue = (int)luaL_checkinteger( state, 4 );
	int alpha = (int)luaL_optinteger( state, 5, 255 );
	if ( !entity )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	entity->SetRenderColor( (byte)clamp( red, 0, 255 ), (byte)clamp( green, 0, 255 ),
		(byte)clamp( blue, 0, 255 ), (byte)clamp( alpha, 0, 255 ) );
	lua_pushboolean( state, 1 );
	return 1;
}

static int LuaEntityGetColor( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	if ( !entity ) return 0;
	color32 color = entity->GetRenderColor();
	lua_newtable( state );
	lua_pushinteger( state, color.r ); lua_setfield( state, -2, "r" );
	lua_pushinteger( state, color.g ); lua_setfield( state, -2, "g" );
	lua_pushinteger( state, color.b ); lua_setfield( state, -2, "b" );
	lua_pushinteger( state, color.a ); lua_setfield( state, -2, "a" );
	return 1;
}

static int LuaEntitySetRenderMode( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	int mode = (int)luaL_checkinteger( state, 2 );
	if ( !entity )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	entity->SetRenderMode( (RenderMode_t)mode );
	lua_pushboolean( state, 1 );
	return 1;
}

static int LuaEntityGetRenderMode( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	if ( !entity ) return 0;
	lua_pushinteger( state, entity->GetRenderMode() );
	return 1;
}

static int LuaEntityGetElasticity( lua_State *state )
{
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	if ( !entity ) return 0;
	lua_pushnumber( state, entity->GetElasticity() );
	return 1;
}

static int LuaEntitySetElasticity( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "entity:SetElasticity is server-only" );
#else
	CBaseEntity *entity = LuaEntityPointer( state, 1 );
	float elasticity = (float)luaL_checknumber( state, 2 );
	if ( !entity )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	entity->SetElasticity( elasticity );
	lua_pushboolean( state, 1 );
	return 1;
#endif
}

static int LuaEntityGetNumBodyGroups( lua_State *state )
{
	CBaseAnimating *entity = dynamic_cast< CBaseAnimating * >( LuaEntityPointer( state, 1 ) );
	if ( !entity ) return 0;
	lua_pushinteger( state, entity->GetNumBodyGroups() );
	return 1;
}

static int LuaEntityLookupAttachment( lua_State *state )
{
	CBaseAnimating *entity = dynamic_cast< CBaseAnimating * >( LuaEntityPointer( state, 1 ) );
	const char *name = luaL_checkstring( state, 2 );
	if ( !entity ) return 0;
	lua_pushinteger( state, entity->LookupAttachment( name ) );
	return 1;
}

static int LuaEntityGetAttachment( lua_State *state )
{
	CBaseAnimating *entity = dynamic_cast< CBaseAnimating * >( LuaEntityPointer( state, 1 ) );
	int attachment = (int)luaL_checkinteger( state, 2 );
	Vector origin;
	QAngle angles;
	if ( !entity || attachment <= 0 || !entity->GetAttachment( attachment, origin, angles ) )
	{
		lua_pushnil( state );
		return 1;
	}
	LuaPushVector( state, origin );
	Vector angleVector( angles.x, angles.y, angles.z );
	LuaPushVector( state, angleVector );
	return 2;
}

static int LuaEntityGetSequence( lua_State *state )
{
	CBaseAnimating *entity = dynamic_cast< CBaseAnimating * >( LuaEntityPointer( state, 1 ) );
	if ( !entity ) return 0;
	lua_pushinteger( state, entity->GetSequence() );
	return 1;
}

static int LuaEntitySetSequence( lua_State *state )
{
	CBaseAnimating *entity = dynamic_cast< CBaseAnimating * >( LuaEntityPointer( state, 1 ) );
	int sequence = (int)luaL_checkinteger( state, 2 );
	if ( !entity )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	entity->ResetSequence( sequence );
	lua_pushboolean( state, 1 );
	return 1;
}

static int LuaEntityLookupSequence( lua_State *state )
{
	CBaseAnimating *entity = dynamic_cast< CBaseAnimating * >( LuaEntityPointer( state, 1 ) );
	const char *name = luaL_checkstring( state, 2 );
	if ( !entity ) return 0;
	lua_pushinteger( state, entity->LookupSequence( name ) );
	return 1;
}

static int LuaEntitySetPlaybackRate( lua_State *state )
{
	CBaseAnimating *entity = dynamic_cast< CBaseAnimating * >( LuaEntityPointer( state, 1 ) );
	float rate = (float)luaL_checknumber( state, 2 );
	if ( !entity )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	entity->SetPlaybackRate( rate );
	lua_pushboolean( state, 1 );
	return 1;
}

#ifndef CLIENT_DLL
static CAI_BaseNPC *LuaNPCPointer( lua_State *state, int stackIndex )
{
	CBaseEntity *entity = LuaEntityPointer( state, stackIndex );
	return entity ? entity->MyNPCPointer() : NULL;
}

static int LuaNPCGetEnemy( lua_State *state )
{
	CAI_BaseNPC *npc = LuaNPCPointer( state, 1 );
	if ( !npc || !npc->GetEnemy() ) return 0;
	LuaPushEntity( state, npc->GetEnemy()->entindex() );
	return 1;
}

static int LuaNPCSetEnemy( lua_State *state )
{
	CAI_BaseNPC *npc = LuaNPCPointer( state, 1 );
	CBaseEntity *enemy = lua_isuserdata( state, 2 ) ? LuaEntityPointer( state, 2 ) : NULL;
	if ( !npc )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	npc->SetEnemy( enemy );
	lua_pushboolean( state, 1 );
	return 1;
}

static int LuaNPCSetSchedule( lua_State *state )
{
	CAI_BaseNPC *npc = LuaNPCPointer( state, 1 );
	int schedule = (int)luaL_checkinteger( state, 2 );
	if ( !npc )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	lua_pushboolean( state, npc->SetSchedule( schedule ) ? 1 : 0 );
	return 1;
}

static int LuaNPCClearSchedule( lua_State *state )
{
	CAI_BaseNPC *npc = LuaNPCPointer( state, 1 );
	if ( !npc )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	npc->ClearSchedule( "lua" );
	lua_pushboolean( state, 1 );
	return 1;
}

static int LuaNPCSetCondition( lua_State *state )
{
	CAI_BaseNPC *npc = LuaNPCPointer( state, 1 );
	int condition = (int)luaL_checkinteger( state, 2 );
	if ( !npc )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	npc->SetCondition( condition );
	lua_pushboolean( state, 1 );
	return 1;
}
#endif

static int LuaPlayerGetButtons( lua_State *state )
{
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	if ( !player ) return 0;
	lua_pushinteger( state, player->m_nButtons );
	return 1;
}

static int LuaPlayerIsGrounded( lua_State *state )
{
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	lua_pushboolean( state, player && player->GetGroundEntity() != NULL );
	return 1;
}

static int LuaPlayerGetWaterLevel( lua_State *state )
{
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	if ( !player ) return 0;
	lua_pushinteger( state, player->GetWaterLevel() );
	return 1;
}

static int LuaPlayerGetEyeAngles( lua_State *state )
{
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	if ( !player ) return 0;
	const QAngle &angles = player->EyeAngles();
	float value[3] = { angles.x, angles.y, angles.z };
	LuaPushTriple( state, value );
	return 1;
}

static int LuaPlayerGetEyePos( lua_State *state )
{
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	if ( !player ) return 0;
	LuaPushVector( state, player->EyePosition() );
	return 1;
}

static int LuaPlayerGetUserID( lua_State *state )
{
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	if ( !player ) return 0;
#ifdef CLIENT_DLL
	// The client does not own the authoritative userid table.
	lua_pushinteger( state, player->entindex() );
#else
	lua_pushinteger( state, player->GetUserID() );
#endif
	return 1;
}

static int LuaPlayerJump( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "player:jump is server-only" );
#else
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	if ( !player || !player->IsAlive() || player->GetGroundEntity() == NULL )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}

	ConVarRef gravity( "sv_gravity", true );
	bool hasCustomHeight = !lua_isnoneornil( state, 2 );
	float jumpHeight = (float)luaL_optnumber( state, 2, GAMEMOVEMENT_JUMP_HEIGHT );
	if ( jumpHeight <= 0.0f )
		return luaL_error( state, "player:jump height must be greater than zero" );
	float jumpSpeed = hasCustomHeight ?
		sqrtf( 2.0f * ( gravity.IsValid() ? gravity.GetFloat() : 600.0f ) * jumpHeight ) :
		GetGameMovementJumpImpulse();
	Vector velocity = player->GetAbsVelocity();
	velocity.z = jumpSpeed;
	player->SetGroundEntity( NULL );
	player->SetAbsVelocity( velocity );
	lua_pushboolean( state, 1 );
	return 1;
#endif
}

static int LuaPlayerSpawn( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "Player:Spawn is server-only" );
#else
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	if ( !player )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	player->Spawn();
	lua_pushboolean( state, 1 );
	return 1;
#endif
}

static int LuaPlayerKill( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "Player:Kill is server-only" );
#else
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	if ( !player )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	player->CommitSuicide( false, true );
	lua_pushboolean( state, 1 );
	return 1;
#endif
}

static int LuaPlayerChatPrint( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "Player:ChatPrint is server-only" );
#else
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	const char *message = luaL_checkstring( state, 2 );
	if ( !player )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	ClientPrint( player, HUD_PRINTTALK, message );
	lua_pushboolean( state, 1 );
	return 1;
#endif
}

static int LuaPlayerGetMaxHealth( lua_State *state )
{
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	if ( !player ) return 0;
	lua_pushinteger( state, player->GetMaxHealth() );
	return 1;
}

static int LuaPlayerGetArmor( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "Player:Armor is server-only" );
#else
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	if ( !player ) return 0;
	lua_pushinteger( state, player->ArmorValue() );
	return 1;
#endif
}

static int LuaPlayerSetArmor( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "Player:SetArmor is server-only" );
#else
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	if ( !player ) return 0;
	player->SetArmorValue( (int)luaL_checkinteger( state, 2 ) );
	return 0;
#endif
}

static int LuaPlayerStripWeapons( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "Player:StripWeapons is server-only" );
#else
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	if ( !player ) return 0;
	player->RemoveAllItems( true );
	return 0;
#endif
}

static int LuaPlayerGetActiveWeapon( lua_State *state )
{
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	CBaseCombatWeapon *weapon = player ? player->GetActiveWeapon() : NULL;
	if ( !weapon ) return 0;
	LuaPushEntity( state, weapon->entindex() );
	return 1;
}

#ifndef CLIENT_DLL
static int LuaPlayerGetWeapons( lua_State *state )
{
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	if ( !player ) return 0;
	lua_newtable( state );
	int resultIndex = 1;
	for ( int i = 0; i < player->WeaponCount(); ++i )
	{
		CBaseCombatWeapon *weapon = player->GetWeapon( i );
		if ( !weapon )
			continue;
		LuaPushEntity( state, weapon->entindex() );
		lua_rawseti( state, -2, resultIndex++ );
	}
	return 1;
}

static int LuaPlayerSelectWeapon( lua_State *state )
{
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	const char *classname = luaL_checkstring( state, 2 );
	if ( !player )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	CBaseCombatWeapon *weapon = player->Weapon_OwnsThisType( classname );
	lua_pushboolean( state, weapon && player->Weapon_Switch( weapon ) );
	return 1;
}

static int LuaPlayerGetAmmo( lua_State *state )
{
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	int ammoType = (int)luaL_checkinteger( state, 2 );
	if ( !player ) return 0;
	lua_pushinteger( state, player->GetAmmoCount( ammoType ) );
	return 1;
}

static int LuaPlayerGiveAmmo( lua_State *state )
{
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	int amount = (int)luaL_checkinteger( state, 2 );
	int ammoType = (int)luaL_checkinteger( state, 3 );
	if ( !player ) return 0;
	lua_pushinteger( state, player->GiveAmmo( MAX( 0, amount ), ammoType ) );
	return 1;
}

static int LuaPlayerGetShootPos( lua_State *state )
{
	CBasePlayer *player = ToBasePlayer( LuaEntityPointer( state, 1 ) );
	if ( !player ) return 0;
	LuaPushVector( state, player->Weapon_ShootPosition() );
	return 1;
}
#endif

static void LuaMapPushInfo( lua_State *state, const char *mapName )
{
	char bspPath[160];
	Q_snprintf( bspPath, sizeof( bspPath ), "maps/%s.bsp", mapName ? mapName : "" );
	lua_newtable( state );
	lua_pushstring( state, mapName ? mapName : "" );
	lua_setfield( state, -2, "name" );
	lua_pushstring( state, bspPath );
	lua_setfield( state, -2, "file" );

	const char *preview = NULL;
	char previewPath[192];
	const char *candidates[] =
	{
		"maps/%s.jpg",
		"maps/%s.png",
		"materials/maps/%s/preview.jpg",
		"materials/maps/%s/preview.png"
	};
	for ( int i = 0; i < ARRAYSIZE( candidates ); ++i )
	{
		Q_snprintf( previewPath, sizeof( previewPath ), candidates[i], mapName ? mapName : "" );
		if ( g_pFullFileSystem && g_pFullFileSystem->FileExists( previewPath, "MOD" ) )
		{
			preview = previewPath;
			break;
		}
	}
	if ( preview )
		lua_pushstring( state, preview );
	else
		lua_pushnil( state );
	lua_setfield( state, -2, "preview" );
}

static int LuaMapCurrent( lua_State *state )
{
	const char *levelName = "";
#ifdef CLIENT_DLL
	if ( engine )
		levelName = engine->GetLevelName();
#else
	if ( gpGlobals )
		levelName = STRING( gpGlobals->mapname );
#endif
	char mapName[128];
	Q_FileBase( levelName ? levelName : "", mapName, sizeof( mapName ) );
	LuaMapPushInfo( state, mapName );
	return 1;
}

static int LuaMapList( lua_State *state )
{
	lua_newtable( state );
	if ( !g_pFullFileSystem )
		return 1;

	FileFindHandle_t handle = FILESYSTEM_INVALID_FIND_HANDLE;
	const char *fileName = g_pFullFileSystem->FindFirstEx( "maps/*.bsp", "MOD", &handle );
	int resultIndex = 1;
	while ( fileName && resultIndex <= 256 )
	{
		if ( !g_pFullFileSystem->FindIsDirectory( handle ) )
		{
			char mapName[128];
			Q_FileBase( fileName, mapName, sizeof( mapName ) );
			LuaMapPushInfo( state, mapName );
			lua_rawseti( state, -2, resultIndex++ );
		}
		fileName = g_pFullFileSystem->FindNext( handle );
	}
	if ( handle != FILESYSTEM_INVALID_FIND_HANDLE )
		g_pFullFileSystem->FindClose( handle );
	return 1;
}

static int LuaEntityAll( lua_State *state )
{
	lua_newtable( state );
	int resultIndex = 1;
	for ( int index = 0; index < MAX_EDICTS; ++index )
	{
		if ( !CBaseEntity::Instance( index ) )
			continue;
		LuaPushEntity( state, index );
		lua_rawseti( state, -2, resultIndex++ );
	}
	return 1;
}

static int LuaEntityFindInSphere( lua_State *state )
{
	Vector center;
	if ( !LuaReadVector( state, 1, center ) )
		return luaL_error( state, "ents.FindInSphere expects a vector table" );
	float radius = (float)luaL_checknumber( state, 2 );
	if ( radius < 0.0f )
		return luaL_error( state, "ents.FindInSphere radius cannot be negative" );
	float radiusSqr = radius * radius;
	lua_newtable( state );
	int resultIndex = 1;
	for ( int index = 0; index < MAX_EDICTS; ++index )
	{
		CBaseEntity *entity = CBaseEntity::Instance( index );
		if ( !entity || ( entity->GetAbsOrigin() - center ).LengthSqr() > radiusSqr )
			continue;
		LuaPushEntity( state, index );
		lua_rawseti( state, -2, resultIndex++ );
	}
	return 1;
}

#ifndef CLIENT_DLL
static int LuaEntityFindByName( lua_State *state )
{
	const char *targetName = luaL_checkstring( state, 1 );
	lua_newtable( state );
	int resultIndex = 1;
	for ( int index = 0; index < MAX_EDICTS; ++index )
	{
		CBaseEntity *entity = CBaseEntity::Instance( index );
		if ( !entity || Q_stricmp( STRING( entity->GetEntityName() ), targetName ) )
			continue;
		LuaPushEntity( state, index );
		lua_rawseti( state, -2, resultIndex++ );
	}
	return 1;
}
#endif

static int LuaPlayerGetByID( lua_State *state )
{
	int userID = (int)luaL_checkinteger( state, 1 );
	for ( int index = 1; gpGlobals && index <= gpGlobals->maxClients; ++index )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( index );
		if ( !player )
			continue;
#ifdef CLIENT_DLL
		if ( index == userID )
#else
		if ( player->GetUserID() == userID )
#endif
		{
			LuaPushEntity( state, index );
			return 1;
		}
	}
	return 0;
}

static int LuaHudMessage( lua_State *state )
{
	const char *message = luaL_checkstring( state, 1 );
#ifdef CLIENT_DLL
	char command[1024];
	Q_snprintf( command, sizeof( command ), "echo %s", message );
	if ( engine )
		engine->ClientCmd_Unrestricted( command );
#else
	int destination = (int)luaL_optinteger( state, 2, HUD_PRINTCENTER );
	if ( destination != HUD_PRINTCONSOLE && destination != HUD_PRINTNOTIFY &&
		destination != HUD_PRINTTALK && destination != HUD_PRINTCENTER )
		return luaL_error( state, "invalid HUD destination" );
	UTIL_ClientPrintAll( destination, message );
#endif
	return 0;
}

static int LuaHudShowFps( lua_State *state )
{
#ifdef CLIENT_DLL
	bool enabled = lua_toboolean( state, 1 ) != 0;
	if ( engine )
		engine->ClientCmd_Unrestricted( enabled ? "cl_showfps 1" : "cl_showfps 0" );
	return 0;
#else
	(void)state;
	return luaL_error( state, "hud.show_fps is client-only" );
#endif
}

static int LuaHudColorComponent( lua_State *state, int tableIndex, const char *field, int fallback )
{
	if ( !lua_istable( state, tableIndex ) )
		return fallback;
	lua_getfield( state, tableIndex, field );
	int value = lua_isnumber( state, -1 ) ? (int)lua_tointeger( state, -1 ) : fallback;
	lua_pop( state, 1 );
	return value;
}

static void LuaHudReadColor( lua_State *state, int index, int &r, int &g, int &b, int &a )
{
	r = LuaHudColorComponent( state, index, "r", 255 );
	g = LuaHudColorComponent( state, index, "g", 255 );
	b = LuaHudColorComponent( state, index, "b", 255 );
	a = LuaHudColorComponent( state, index, "a", 255 );
}

static int LuaHudCreateText( lua_State *state )
{
#ifdef CLIENT_DLL
	const char *id = luaL_checkstring( state, 1 );
	int x = (int)luaL_checkinteger( state, 2 );
	int y = (int)luaL_checkinteger( state, 3 );
	const char *text = luaL_optstring( state, 4, "" );
	int size = (int)luaL_optinteger( state, 5, 20 );
	int r, g, b, a;
	LuaHudReadColor( state, 6, r, g, b, a );
	LuaHudCreateText( id, x, y, text, size, r, g, b, a );
	return 0;
#else
	(void)state;
	return luaL_error( state, "hud.create_text is client-only" );
#endif
}

static int LuaHudCreateRect( lua_State *state )
{
#ifdef CLIENT_DLL
	const char *id = luaL_checkstring( state, 1 );
	int x = (int)luaL_checkinteger( state, 2 );
	int y = (int)luaL_checkinteger( state, 3 );
	int wide = (int)luaL_checkinteger( state, 4 );
	int tall = (int)luaL_checkinteger( state, 5 );
	int r, g, b, a;
	LuaHudReadColor( state, 6, r, g, b, a );
	LuaHudCreateRect( id, x, y, wide, tall, r, g, b, a );
	return 0;
#else
	(void)state;
	return luaL_error( state, "hud.create_rect is client-only" );
#endif
}

static int LuaHudCreateImage( lua_State *state )
{
#ifdef CLIENT_DLL
	const char *id = luaL_checkstring( state, 1 );
	int x = (int)luaL_checkinteger( state, 2 );
	int y = (int)luaL_checkinteger( state, 3 );
	int wide = (int)luaL_checkinteger( state, 4 );
	int tall = (int)luaL_checkinteger( state, 5 );
	const char *file = luaL_checkstring( state, 6 );
	int r, g, b, a;
	LuaHudReadColor( state, 7, r, g, b, a );
	LuaHudCreateImage( id, x, y, wide, tall, file, r, g, b, a );
	return 0;
#else
	(void)state;
	return luaL_error( state, "hud.create_image is client-only" );
#endif
}

static int LuaHudSetText( lua_State *state )
{
#ifdef CLIENT_DLL
	LuaHudSetText( luaL_checkstring( state, 1 ), luaL_checkstring( state, 2 ) );
	return 0;
#else
	(void)state;
	return luaL_error( state, "hud.set_text is client-only" );
#endif
}

static int LuaHudSetPosition( lua_State *state )
{
#ifdef CLIENT_DLL
	LuaHudSetPosition( luaL_checkstring( state, 1 ), (int)luaL_checkinteger( state, 2 ),
		(int)luaL_checkinteger( state, 3 ) );
	return 0;
#else
	(void)state;
	return luaL_error( state, "hud.set_pos is client-only" );
#endif
}

static int LuaHudSetColor( lua_State *state )
{
#ifdef CLIENT_DLL
	int r, g, b, a;
	LuaHudReadColor( state, 2, r, g, b, a );
	LuaHudSetColor( luaL_checkstring( state, 1 ), r, g, b, a );
	return 0;
#else
	(void)state;
	return luaL_error( state, "hud.set_color is client-only" );
#endif
}

static int LuaHudSetVisible( lua_State *state )
{
#ifdef CLIENT_DLL
	LuaHudSetVisible( luaL_checkstring( state, 1 ), lua_toboolean( state, 2 ) != 0 );
	return 0;
#else
	(void)state;
	return luaL_error( state, "hud.set_visible is client-only" );
#endif
}

static int LuaHudRemove( lua_State *state )
{
#ifdef CLIENT_DLL
	LuaHudRemove( luaL_checkstring( state, 1 ) );
	return 0;
#else
	(void)state;
	return luaL_error( state, "hud.remove is client-only" );
#endif
}

static int LuaHudClear( lua_State *state )
{
#ifdef CLIENT_DLL
	LuaHudClear();
	return 0;
#else
	(void)state;
	return luaL_error( state, "hud.clear is client-only" );
#endif
}

static int LuaNetSend( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "net.send is server-only" );
#else
	if ( s_LuaServerManager && !s_LuaServerManager->HasPermission( "net.send" ) )
		return luaL_error( state, "plugin permission denied: net.send" );
	const char *name = luaL_checkstring( state, 1 );
	const char *payload = luaL_checkstring( state, 2 );
	int targetIndex = (int)luaL_optinteger( state, 3, 0 );
	if ( !name[0] || Q_strlen( name ) >= 120 || Q_strlen( payload ) >= 4000 )
		return luaL_error( state, "network message is too long or empty" );

	if ( targetIndex > 0 )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( targetIndex );
		if ( !player )
		{
			lua_pushboolean( state, 0 );
			return 1;
		}
		CSingleUserRecipientFilter filter( player );
		UserMessageBegin( filter, "LuaNet" );
		WRITE_STRING( name );
		WRITE_STRING( payload );
		MessageEnd();
	}
	else
	{
		CReliableBroadcastRecipientFilter filter;
		UserMessageBegin( filter, "LuaNet" );
		WRITE_STRING( name );
		WRITE_STRING( payload );
		MessageEnd();
	}
	lua_pushboolean( state, 1 );
	return 1;
#endif
}

static int LuaNetSendInt( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "net.send_int is server-only" );
#else
	char payload[64];
	Q_snprintf( payload, sizeof( payload ), "%d", (int)luaL_checkinteger( state, 2 ) );
	lua_pushstring( state, payload );
	lua_replace( state, 2 );
	return LuaNetSend( state );
#endif
}

static int LuaNetSendFloat( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "net.send_float is server-only" );
#else
	char payload[64];
	Q_snprintf( payload, sizeof( payload ), "%.9g", luaL_checknumber( state, 2 ) );
	lua_pushstring( state, payload );
	lua_replace( state, 2 );
	return LuaNetSend( state );
#endif
}

static int LuaNetSendVector( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "net.send_vector is server-only" );
#else
	Vector value;
	if ( !LuaReadVector( state, 2, value ) )
		return luaL_error( state, "net.send_vector expects a vector table" );
	char payload[128];
	Q_snprintf( payload, sizeof( payload ), "%.9g %.9g %.9g", value.x, value.y, value.z );
	lua_pushstring( state, payload );
	lua_replace( state, 2 );
	return LuaNetSend( state );
#endif
}

static int LuaNetSendEntity( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "net.send_entity is server-only" );
#else
	CBaseEntity *entity = LuaEntityPointer( state, 2 );
	if ( !entity )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	char payload[32];
	Q_snprintf( payload, sizeof( payload ), "%d", entity->entindex() );
	lua_pushstring( state, payload );
	lua_replace( state, 2 );
	return LuaNetSend( state );
#endif
}

static int LuaNetStart( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state;
	return luaL_error( state, "net.Start is server-only" );
#else
	if ( !s_LuaServerManager || !s_LuaServerManager->HasPermission( "net.send" ) )
		return luaL_error( state, "plugin permission denied: net.send" );
	s_LuaServerManager->NetworkStart( luaL_checkstring( state, 1 ) );
	return 0;
#endif
}

static int LuaNetWriteString( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state; return luaL_error( state, "net.WriteString is server-only" );
#else
	if ( !s_LuaServerManager->NetworkWriteString( luaL_checkstring( state, 1 ) ) ) return luaL_error( state, "network buffer is full" );
	return 0;
#endif
}

static int LuaNetWriteInt( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state; return luaL_error( state, "net.WriteInt is server-only" );
#else
	if ( !s_LuaServerManager->NetworkWriteInt( (int)luaL_checkinteger( state, 1 ) ) ) return luaL_error( state, "network buffer is full" );
	return 0;
#endif
}

static int LuaNetWriteFloat( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state; return luaL_error( state, "net.WriteFloat is server-only" );
#else
	if ( !s_LuaServerManager->NetworkWriteFloat( (float)luaL_checknumber( state, 1 ) ) ) return luaL_error( state, "network buffer is full" );
	return 0;
#endif
}

static int LuaNetWriteBool( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state; return luaL_error( state, "net.WriteBool is server-only" );
#else
	if ( !s_LuaServerManager->NetworkWriteBool( lua_toboolean( state, 1 ) != 0 ) ) return luaL_error( state, "network buffer is full" );
	return 0;
#endif
}

static int LuaNetWriteVector( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state; return luaL_error( state, "net.WriteVector is server-only" );
#else
	Vector value;
	if ( !LuaReadVector( state, 1, value ) ) return luaL_error( state, "net.WriteVector expects a vector table" );
	if ( !s_LuaServerManager->NetworkWriteVector( value.x, value.y, value.z ) ) return luaL_error( state, "network buffer is full" );
	return 0;
#endif
}

static int LuaNetWriteEntity( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state; return luaL_error( state, "net.WriteEntity is server-only" );
#else
	if ( !s_LuaServerManager->NetworkWriteEntity( LuaEntityIndex( state, 1 ) ) ) return luaL_error( state, "network buffer is full" );
	return 0;
#endif
}

static int LuaNetSendMessage( lua_State *state )
{
#ifdef CLIENT_DLL
	(void)state; return luaL_error( state, "net.Send is server-only" );
#else
	if ( !s_LuaServerManager || !s_LuaServerManager->NetworkWriteName()[0] )
		return luaL_error( state, "net.Send called without net.Start" );
	int target = 0;
	if ( lua_gettop( state ) >= 1 && !lua_isnil( state, 1 ) )
		target = lua_isnumber( state, 1 ) ? (int)lua_tointeger( state, 1 ) : LuaEntityIndex( state, 1 );
	lua_settop( state, 0 );
	lua_pushstring( state, s_LuaServerManager->NetworkWriteName() );
	lua_pushstring( state, s_LuaServerManager->NetworkWritePayload() );
	lua_pushinteger( state, target );
	return LuaNetSend( state );
#endif
}

static int LuaNetReadString( lua_State *state )
{
#ifdef CLIENT_DLL
	char value[1024];
	if ( !s_LuaClientManager || !s_LuaClientManager->NetworkReadString( value, sizeof( value ) ) ) return 0;
	lua_pushstring( state, value ); return 1;
#else
	(void)state; return luaL_error( state, "net.ReadString is client-only" );
#endif
}

static int LuaNetReadInt( lua_State *state )
{
#ifdef CLIENT_DLL
	int value; if ( !s_LuaClientManager || !s_LuaClientManager->NetworkReadInt( value ) ) return 0; lua_pushinteger( state, value ); return 1;
#else
	(void)state; return luaL_error( state, "net.ReadInt is client-only" );
#endif
}

static int LuaNetReadFloat( lua_State *state )
{
#ifdef CLIENT_DLL
	float value; if ( !s_LuaClientManager || !s_LuaClientManager->NetworkReadFloat( value ) ) return 0; lua_pushnumber( state, value ); return 1;
#else
	(void)state; return luaL_error( state, "net.ReadFloat is client-only" );
#endif
}

static int LuaNetReadBool( lua_State *state )
{
#ifdef CLIENT_DLL
	bool value; if ( !s_LuaClientManager || !s_LuaClientManager->NetworkReadBool( value ) ) return 0; lua_pushboolean( state, value ); return 1;
#else
	(void)state; return luaL_error( state, "net.ReadBool is client-only" );
#endif
}

static int LuaNetReadVector( lua_State *state )
{
#ifdef CLIENT_DLL
	float x, y, z; if ( !s_LuaClientManager || !s_LuaClientManager->NetworkReadVector( x, y, z ) ) return 0; Vector value( x, y, z ); LuaPushVector( state, value ); return 1;
#else
	(void)state; return luaL_error( state, "net.ReadVector is client-only" );
#endif
}

static int LuaNetReadEntity( lua_State *state )
{
#ifdef CLIENT_DLL
	int index; if ( !s_LuaClientManager || !s_LuaClientManager->NetworkReadEntity( index ) ) return 0; if ( !CBaseEntity::Instance( index ) ) return 0; LuaPushEntity( state, index ); return 1;
#else
	(void)state; return luaL_error( state, "net.ReadEntity is client-only" );
#endif
}

static void InstallLuaGameBindings( lua_State *state, void *context )
{
	(void)context;
	static const luaL_Reg entityMethods[] =
	{
		{ "is_valid",     &LuaEntityIsValid },
		{ "index",        &LuaEntityGetIndex },
		{ "classname",    &LuaEntityGetClassname },
		{ "IsValid",      &LuaEntityIsValid },
		{ "EntIndex",     &LuaEntityGetIndex },
		{ "GetClass",     &LuaEntityGetClassname },
		{ "flags",        &LuaEntityGetFlags },
		{ "move_type",    &LuaEntityGetMoveType },
		{ "origin",       &LuaEntityGetOrigin },
		{ "set_origin",   &LuaEntitySetOrigin },
		{ "angles",       &LuaEntityGetAngles },
		{ "set_angles",   &LuaEntitySetAngles },
		{ "velocity",     &LuaEntityGetVelocity },
		{ "set_velocity", &LuaEntitySetVelocity },
		{ "SetMoveType",  &LuaEntitySetMoveType },
		{ "SetSolid",     &LuaEntitySetSolid },
		{ "SetCollisionBounds", &LuaEntitySetCollisionBounds },
		{ "GetOwnerEntity", &LuaEntityGetOwner },
		{ "SetOwnerEntity", &LuaEntitySetOwner },
		{ "SetParent",    &LuaEntitySetParent },
		{ "EmitSound",    &LuaEntityEmitSound },
		{ "SetColor",     &LuaEntitySetColor },
		{ "GetColor",     &LuaEntityGetColor },
		{ "SetRenderMode", &LuaEntitySetRenderMode },
		{ "GetRenderMode", &LuaEntityGetRenderMode },
		{ "GetElasticity", &LuaEntityGetElasticity },
		{ "SetElasticity", &LuaEntitySetElasticity },
		{ "GetSequence",  &LuaEntityGetSequence },
		{ "SetSequence",  &LuaEntitySetSequence },
		{ "LookupSequence", &LuaEntityLookupSequence },
		{ "SetPlaybackRate", &LuaEntitySetPlaybackRate },
		{ "GetNumBodyGroups", &LuaEntityGetNumBodyGroups },
		{ "LookupAttachment", &LuaEntityLookupAttachment },
		{ "GetAttachment", &LuaEntityGetAttachment },
		{ "health",       &LuaEntityGetHealth },
		{ "set_health",   &LuaEntitySetHealth },
		{ "team",         &LuaEntityGetTeam },
		{ "GetName",      &LuaEntityGetTargetName },
		{ "SetName",      &LuaEntitySetTargetName },
		{ "GetModel",     &LuaEntityGetModel },
		{ "SetModel",     &LuaEntitySetModel },
		{ "GetModelScale", &LuaEntityGetModelScale },
		{ "SetModelScale", &LuaEntitySetModelScale },
		{ "GetSkin",      &LuaEntityGetSkin },
		{ "SetSkin",      &LuaEntitySetSkin },
		{ "GetBodygroup", &LuaEntityGetBodygroup },
		{ "SetBodygroup", &LuaEntitySetBodygroup },
		{ "GetGravity",   &LuaEntityGetGravity },
		{ "SetGravity",   &LuaEntitySetGravity },
		{ "GetFriction",  &LuaEntityGetFriction },
		{ "SetFriction",  &LuaEntitySetFriction },
		{ "GetKeyValue",  &LuaEntityGetKeyValue },
		{ "SetKeyValue",  &LuaEntitySetKeyValue },
		{ "Fire",         &LuaEntityFireInput },
		{ "TakeDamage",   &LuaEntityTakeDamage },
		{ "Remove",       &LuaEntityRemove },
		{ "name",         &LuaPlayerGetName },
		{ "alive",        &LuaPlayerIsAlive },
		{ "Nick",         &LuaPlayerGetName },
		{ "Alive",        &LuaPlayerIsAlive },
		{ "userid",       &LuaPlayerGetUserID },
		{ "buttons",      &LuaPlayerGetButtons },
		{ "grounded",     &LuaPlayerIsGrounded },
		{ "water_level",  &LuaPlayerGetWaterLevel },
		{ "eye_angles",   &LuaPlayerGetEyeAngles },
		{ "eye_pos",      &LuaPlayerGetEyePos },
		{ "jump",         &LuaPlayerJump },
		{ "GetPos",       &LuaEntityGetOrigin },
		{ "SetPos",       &LuaEntitySetOrigin },
		{ "GetAngles",    &LuaEntityGetAngles },
		{ "SetAngles",    &LuaEntitySetAngles },
		{ "GetVelocity",  &LuaEntityGetVelocity },
		{ "SetVelocity",  &LuaEntitySetVelocity },
		{ "Health",       &LuaEntityGetHealth },
		{ "SetHealth",    &LuaEntitySetHealth },
		{ "Team",         &LuaEntityGetTeam },
		{ "SetTeam",      &LuaPlayerSetTeam },
		{ "Give",         &LuaPlayerGive },
		{ "IsOnGround",   &LuaPlayerIsGrounded },
		{ "WaterLevel",   &LuaPlayerGetWaterLevel },
		{ "EyeAngles",    &LuaPlayerGetEyeAngles },
		{ "EyePos",       &LuaPlayerGetEyePos },
		{ "Jump",         &LuaPlayerJump },
		{ "Spawn",        &LuaPlayerSpawn },
		{ "Kill",         &LuaPlayerKill },
		{ "ChatPrint",    &LuaPlayerChatPrint },
		{ "GetMaxHealth", &LuaPlayerGetMaxHealth },
		{ "Armor",        &LuaPlayerGetArmor },
		{ "SetArmor",     &LuaPlayerSetArmor },
		{ "StripWeapons", &LuaPlayerStripWeapons },
		{ "GetActiveWeapon", &LuaPlayerGetActiveWeapon },
		{ "set_team",     &LuaPlayerSetTeam },
		{ "give",         &LuaPlayerGive },
#ifndef CLIENT_DLL
		{ "IsWeapon",     &LuaWeaponIsWeapon },
		{ "GetOwner",     &LuaWeaponGetOwner },
		{ "Clip1",        &LuaWeaponGetClip1 },
		{ "SetClip1",     &LuaWeaponSetClip1 },
		{ "Clip2",        &LuaWeaponGetClip2 },
		{ "SetClip2",     &LuaWeaponSetClip2 },
		{ "GetMaxClip1",  &LuaWeaponGetMaxClip1 },
		{ "GetPrimaryAmmoType", &LuaWeaponGetAmmoType },
		{ "GetNextPrimaryFire", &LuaWeaponGetNextPrimaryAttack },
		{ "SetNextPrimaryFire", &LuaWeaponSetNextPrimaryAttack },
		{ "Reload",       &LuaWeaponReload },
		{ "FireBullets",  &LuaWeaponFireBullets },
		{ "GetWeapons",   &LuaPlayerGetWeapons },
		{ "SelectWeapon", &LuaPlayerSelectWeapon },
		{ "GetAmmoCount", &LuaPlayerGetAmmo },
		{ "GiveAmmo",     &LuaPlayerGiveAmmo },
		{ "GetShootPos",  &LuaPlayerGetShootPos },
		{ "GetEnemy",     &LuaNPCGetEnemy },
		{ "SetEnemy",     &LuaNPCSetEnemy },
		{ "SetSchedule",  &LuaNPCSetSchedule },
		{ "ClearSchedule", &LuaNPCClearSchedule },
		{ "SetCondition", &LuaNPCSetCondition },
#endif
		{ NULL, NULL }
	};
	luaL_newmetatable( state, s_LuaEntityMeta );
	lua_pushvalue( state, -1 );
	lua_setfield( state, -2, "__index" );
	luaL_register( state, NULL, entityMethods );
	lua_pop( state, 1 );

	static const luaL_Reg entityFunctions[] =
	{
		{ "get",    &LuaEntityGet },
		{ "find",   &LuaEntityFind },
		{ "create", &LuaEntityCreate },
		{ "remove", &LuaEntityRemove },
		{ "GetByIndex", &LuaEntityGet },
		{ "FindByClass", &LuaEntityFind },
		{ "Create", &LuaEntityCreate },
		{ "Remove", &LuaEntityRemove },
		{ "GetAll", &LuaEntityAll },
		{ "FindInSphere", &LuaEntityFindInSphere },
#ifndef CLIENT_DLL
		{ "FindByName", &LuaEntityFindByName },
#endif
		{ NULL, NULL }
	};
	static const luaL_Reg playerFunctions[] =
	{
		{ "get", &LuaPlayerGet },
		{ "all", &LuaPlayerAll },
		{ NULL, NULL }
	};

	lua_getglobal( state, "plugin" );
	lua_newtable( state );
	luaL_register( state, NULL, entityFunctions );
	lua_setfield( state, -2, "entity" );
	lua_newtable( state );
	luaL_register( state, NULL, playerFunctions );
	lua_setfield( state, -2, "player" );
	static const luaL_Reg gmodEntityFunctions[] =
	{
		{ "GetByIndex", &LuaEntityGet },
		{ "FindByClass", &LuaEntityFind },
		{ "Create", &LuaEntityCreate },
		{ "Remove", &LuaEntityRemove },
		{ "GetAll", &LuaEntityAll },
		{ "FindInSphere", &LuaEntityFindInSphere },
#ifndef CLIENT_DLL
		{ "FindByName", &LuaEntityFindByName },
#endif
		{ NULL, NULL }
	};
	lua_newtable( state );
	luaL_register( state, NULL, gmodEntityFunctions );
	lua_setglobal( state, "ents" );
	lua_getglobal( state, "weapons" );
	if ( lua_istable( state, -1 ) )
	{
		lua_pushcfunction( state, &LuaEntityCreate );
		lua_setfield( state, -2, "Create" );
	}
	lua_pop( state, 1 );
	lua_getglobal( state, "npcs" );
	if ( lua_istable( state, -1 ) )
	{
		lua_pushcfunction( state, &LuaEntityCreate );
		lua_setfield( state, -2, "Create" );
	}
	lua_pop( state, 1 );
	static const luaL_Reg gmodPlayerFunctions[] =
	{
		{ "GetAll", &LuaPlayerAll },
		{ "GetByID", &LuaPlayerGetByID },
		{ "GetByIndex", &LuaPlayerGet },
		{ NULL, NULL }
	};
	lua_newtable( state );
	luaL_register( state, NULL, gmodPlayerFunctions );
	lua_setglobal( state, "player" );
	static const luaL_Reg gameFunctions[] =
	{
		{ "map",         &LuaGameMap },
		{ "time",        &LuaGameTime },
		{ "frame_time",  &LuaGameFrameTime },
		{ "tick",        &LuaGameTick },
		{ "max_clients", &LuaGameMaxClients },
		{ "GetMap",      &LuaGameMap },
		{ "GetTime",     &LuaGameTime },
		{ "GetFrameTime", &LuaGameFrameTime },
		{ "GetTick",     &LuaGameTick },
		{ NULL, NULL }
	};
	lua_newtable( state );
	luaL_register( state, NULL, gameFunctions );
	lua_setfield( state, -2, "game" );
	static const luaL_Reg mapFunctions[] =
	{
		{ "current", &LuaMapCurrent },
		{ "list",    &LuaMapList },
		{ NULL, NULL }
	};
	lua_newtable( state );
	luaL_register( state, NULL, mapFunctions );
	lua_setfield( state, -2, "map" );
	static const luaL_Reg hudFunctions[] =
	{
		{ "message",      &LuaHudMessage },
		{ "show_fps",     &LuaHudShowFps },
		{ "create_text",  &LuaHudCreateText },
		{ "create_rect",  &LuaHudCreateRect },
		{ "create_image", &LuaHudCreateImage },
		{ "set_text",     &LuaHudSetText },
		{ "set_pos",      &LuaHudSetPosition },
		{ "set_color",    &LuaHudSetColor },
		{ "set_visible",  &LuaHudSetVisible },
		{ "remove",       &LuaHudRemove },
		{ "clear",        &LuaHudClear },
		{ NULL, NULL }
	};
	lua_newtable( state );
	luaL_register( state, NULL, hudFunctions );
	lua_setfield( state, -2, "hud" );
	static const luaL_Reg netFunctions[] =
	{
		{ "send", &LuaNetSend },
		{ "send_int", &LuaNetSendInt },
		{ "send_float", &LuaNetSendFloat },
		{ "send_vector", &LuaNetSendVector },
		{ "send_entity", &LuaNetSendEntity },
		{ "Start", &LuaNetStart },
		{ "WriteString", &LuaNetWriteString },
		{ "WriteInt", &LuaNetWriteInt },
		{ "WriteFloat", &LuaNetWriteFloat },
		{ "WriteBool", &LuaNetWriteBool },
		{ "WriteVector", &LuaNetWriteVector },
		{ "WriteEntity", &LuaNetWriteEntity },
		{ "Send", &LuaNetSendMessage },
		{ "ReadString", &LuaNetReadString },
		{ "ReadInt", &LuaNetReadInt },
		{ "ReadFloat", &LuaNetReadFloat },
		{ "ReadBool", &LuaNetReadBool },
		{ "ReadVector", &LuaNetReadVector },
		{ "ReadEntity", &LuaNetReadEntity },
		{ NULL, NULL }
	};
	lua_newtable( state );
	luaL_register( state, NULL, netFunctions );
	lua_setfield( state, -2, "net" );
	lua_getfield( state, -1, "net" );
	lua_setglobal( state, "net" );
	lua_pop( state, 1 );
#ifndef CLIENT_DLL
	static const luaL_Reg utilFunctions[] =
	{
		{ "TraceLine", &LuaUtilTraceLine },
		{ "TraceHull", &LuaUtilTraceHull },
		{ "PrecacheModel", &LuaUtilPrecacheModel },
		{ "PrecacheSound", &LuaUtilPrecacheSound },
		{ NULL, NULL }
	};
	lua_newtable( state );
	luaL_register( state, NULL, utilFunctions );
	lua_setglobal( state, "util" );
#endif
}

class CLuaGameEventBridge : public IGameEventListener2
{
public:
	CLuaGameEventBridge() : m_manager( NULL ), m_registered( false )
	{
	}

	void Init( CLuaPluginManager *manager )
	{
		m_manager = manager;
		if ( !gameeventmanager )
			return;

		static const char *eventNames[] =
		{
			"player_spawn", "player_death", "player_hurt", "weapon_fire",
			"player_team", "player_say", "item_pickup", "round_start", "round_end",
			"player_connect", "player_use", "player_footstep", "player_jump", "weapon_reload"
		};
		const int eventCount = sizeof( eventNames ) / sizeof( eventNames[0] );
		for ( int i = 0; i < eventCount; ++i )
		{
#ifdef CLIENT_DLL
			gameeventmanager->AddListener( this, eventNames[i], false );
#else
			gameeventmanager->AddListener( this, eventNames[i], true );
#endif
		}
		m_registered = true;
	}

	void Shutdown()
	{
		if ( m_registered && gameeventmanager )
			gameeventmanager->RemoveListener( this );
		m_registered = false;
		m_manager = NULL;
	}

	virtual void FireGameEvent( IGameEvent *event )
	{
		if ( !m_manager || !event )
			return;

		const char *name = event->GetName();
		if ( !Q_stricmp( name, "player_death" ) )
		{
			m_manager->GameEvent( name, event->GetInt( "userid" ), event->GetInt( "attacker" ),
				0, event->GetString( "weapon" ) );
		}
		else if ( !Q_stricmp( name, "player_hurt" ) )
		{
			m_manager->GameEvent( name, event->GetInt( "userid" ), event->GetInt( "attacker" ),
				event->GetInt( "damageamount", event->GetInt( "dmg_health" ) ) );
		}
		else if ( !Q_stricmp( name, "weapon_fire" ) )
		{
			m_manager->GameEvent( name, event->GetInt( "userid" ), 0, 0, event->GetString( "weapon" ) );
		}
		else if ( !Q_stricmp( name, "item_pickup" ) )
		{
			m_manager->GameEvent( name, event->GetInt( "userid" ), 0, 0, event->GetString( "item" ) );
		}
		else if ( !Q_stricmp( name, "player_say" ) )
		{
			m_manager->GameEvent( name, event->GetInt( "userid" ), 0, 0, event->GetString( "text" ) );
		}
		else if ( !Q_stricmp( name, "player_connect" ) )
		{
			m_manager->GameEvent( name, event->GetInt( "userid" ), 0, 0,
				event->GetString( "name" ), event->GetString( "networkid" ) );
		}
		else if ( !Q_stricmp( name, "player_use" ) )
		{
			m_manager->GameEvent( name, event->GetInt( "userid" ), event->GetInt( "entityid" ) );
		}
		else if ( !Q_stricmp( name, "player_footstep" ) )
		{
			m_manager->GameEvent( name, event->GetInt( "userid" ) );
		}
		else if ( !Q_stricmp( name, "player_jump" ) )
		{
			m_manager->GameEvent( name, event->GetInt( "userid" ) );
		}
		else if ( !Q_stricmp( name, "weapon_reload" ) )
		{
			m_manager->GameEvent( name, event->GetInt( "userid" ), 0, 0,
				event->GetString( "weapon" ) );
		}
		else if ( !Q_stricmp( name, "player_team" ) )
		{
			m_manager->GameEvent( name, event->GetInt( "userid" ), event->GetInt( "team" ) );
		}
		else if ( !Q_stricmp( name, "round_end" ) )
		{
			m_manager->GameEvent( name, event->GetInt( "winner" ), event->GetInt( "reason" ) );
		}
		else if ( !Q_stricmp( name, "round_start" ) )
		{
			m_manager->GameEvent( name );
		}
		else if ( !Q_stricmp( name, "player_spawn" ) )
		{
			m_manager->GameEvent( name, event->GetInt( "userid" ) );
		}
	}

private:
	CLuaPluginManager *m_manager;
	bool m_registered;
};

class CLuaGamePluginSystem : public CAutoGameSystemPerFrame
{
public:
	CLuaGamePluginSystem() : CAutoGameSystemPerFrame( "CLuaGamePluginSystem" )
	{
	}

	virtual bool Init()
	{
#ifdef CLIENT_DLL
		const char *root = "scripts/plugins/client";
		const char *role = "client";
#else
		const char *root = "scripts/plugins/server";
		const char *role = "server";
#endif
		m_manager.SetBindingInstaller( InstallLuaGameBindings, NULL );
		if ( usermessages && usermessages->LookupUserMessage( "LuaNet" ) < 0 )
			usermessages->Register( "LuaNet", -1 );
#ifdef CLIENT_DLL
		usermessages->HookMessage( "LuaNet", LuaNetworkMessageHook );
#endif
		if ( !m_manager.Init( g_pFullFileSystem, "MOD", root, role ) )
			return false;
	#ifdef CLIENT_DLL
		s_LuaClientManager = &m_manager;
	#else
		s_LuaServerManager = &m_manager;
	#endif

		m_manager.SetCommandExecutor( ExecuteLuaCommand, NULL );
		m_eventBridge.Init( &m_manager );
		m_manager.LoadAll();
		return true;
	}

	virtual void Shutdown()
	{
	#ifdef CLIENT_DLL
		s_LuaClientManager = NULL;
	#else
		s_LuaServerManager = NULL;
	#endif
		m_eventBridge.Shutdown();
		m_manager.Shutdown();
	}

	virtual void LevelInitPreEntity()
	{
		m_manager.LevelInit( IGameSystem::MapName() );
	}

	virtual void LevelShutdownPreEntity()
	{
		m_manager.LevelShutdown();
	}

#ifdef CLIENT_DLL
	virtual void Update( float frametime )
	{
		(void)frametime;
		m_manager.Frame( true, gpGlobals ? gpGlobals->curtime : 0.0f );
	}
#else
	virtual void FrameUpdatePostEntityThink()
	{
		m_manager.Frame( true, gpGlobals ? gpGlobals->curtime : 0.0f );
	}
#endif

	void ReloadAll() { m_manager.ReloadAll(); }
	bool Load( const char *name ) { return m_manager.LoadPlugin( name ); }
	bool Unload( const char *name ) { return m_manager.UnloadPlugin( name ); }
	bool Reload( const char *name ) { return m_manager.ReloadPlugin( name ); }
	bool SetPermanent( const char *name, bool enabled )
	{
		if ( !m_manager.SetPersistentPluginEnabled( name, enabled ) )
			return false;
		return enabled ? ( m_manager.IsPluginLoaded( name ) || m_manager.LoadPlugin( name ) ) : true;
	}
	void Print() const { m_manager.PrintPlugins(); }
	void ClientPutInServer( edict_t *entity, const char *playerName )
	{
	#ifndef CLIENT_DLL
		if ( entity )
			m_manager.ClientPutInServer( engine->IndexOfEdict( entity ), playerName );
#else
		(void)entity;
		(void)playerName;
#endif
	}
	void ClientDisconnect( edict_t *entity )
	{
	#ifndef CLIENT_DLL
		if ( entity )
			m_manager.ClientDisconnect( engine->IndexOfEdict( entity ) );
#else
		(void)entity;
#endif
	}

private:
	CLuaPluginManager m_manager;
	CLuaGameEventBridge m_eventBridge;
};

static CLuaGamePluginSystem g_LuaGamePluginSystem;

#ifdef CLIENT_DLL
CON_COMMAND( lua_client_plugins, "List loaded client Lua plugins" )
{
	g_LuaGamePluginSystem.Print();
}

CON_COMMAND( lua_client_reload, "Reload all client Lua plugins" )
{
	g_LuaGamePluginSystem.ReloadAll();
}

CON_COMMAND( lua_client_load, "lua_client_load <plugin.lua>" )
{
	if ( args.ArgC() < 2 )
		Warning( "lua_client_load <plugin.lua>\n" );
	else if ( !g_LuaGamePluginSystem.Load( args[1] ) )
		Warning( "Unable to load client Lua plugin '%s'\n", args[1] );
}

CON_COMMAND( lua_client_unload, "lua_client_unload <plugin>" )
{
	if ( args.ArgC() < 2 )
		Warning( "lua_client_unload <plugin>\n" );
	else if ( !g_LuaGamePluginSystem.Unload( args[1] ) )
		Warning( "Unable to unload client Lua plugin '%s'\n", args[1] );
}

CON_COMMAND( lua_client_permanent_enable, "lua_client_permanent_enable <plugin.lua>" )
{
	if ( args.ArgC() < 2 || !g_LuaGamePluginSystem.SetPermanent( args[1], true ) )
		Warning( "Unable to permanently enable client Lua plugin\n" );
}

CON_COMMAND( lua_client_permanent_disable, "lua_client_permanent_disable <plugin>" )
{
	if ( args.ArgC() < 2 || !g_LuaGamePluginSystem.SetPermanent( args[1], false ) )
		Warning( "Unable to permanently disable client Lua plugin\n" );
}
#else
CON_COMMAND( lua_server_plugins, "List loaded server Lua plugins" )
{
	g_LuaGamePluginSystem.Print();
}

CON_COMMAND( lua_server_reload, "Reload all server Lua plugins" )
{
	g_LuaGamePluginSystem.ReloadAll();
}

CON_COMMAND( lua_server_load, "lua_server_load <plugin.lua>" )
{
	if ( args.ArgC() < 2 )
		Warning( "lua_server_load <plugin.lua>\n" );
	else if ( !g_LuaGamePluginSystem.Load( args[1] ) )
		Warning( "Unable to load server Lua plugin '%s'\n", args[1] );
}

CON_COMMAND( lua_server_unload, "lua_server_unload <plugin>" )
{
	if ( args.ArgC() < 2 )
		Warning( "lua_server_unload <plugin>\n" );
	else if ( !g_LuaGamePluginSystem.Unload( args[1] ) )
		Warning( "Unable to unload server Lua plugin '%s'\n", args[1] );
}

CON_COMMAND( lua_server_permanent_enable, "lua_server_permanent_enable <plugin.lua>" )
{
	if ( args.ArgC() < 2 || !g_LuaGamePluginSystem.SetPermanent( args[1], true ) )
		Warning( "Unable to permanently enable server Lua plugin\n" );
}

CON_COMMAND( lua_server_permanent_disable, "lua_server_permanent_disable <plugin>" )
{
	if ( args.ArgC() < 2 || !g_LuaGamePluginSystem.SetPermanent( args[1], false ) )
		Warning( "Unable to permanently disable server Lua plugin\n" );
}

void LuaServerPluginClientPutInServer( edict_t *entity, const char *playerName )
{
	g_LuaGamePluginSystem.ClientPutInServer( entity, playerName );
}

void LuaServerPluginClientDisconnect( edict_t *entity )
{
	g_LuaGamePluginSystem.ClientDisconnect( entity );
}

bool LuaServerPluginEntityTakeDamage( CBaseEntity *entity, const CTakeDamageInfo &info )
{
	if ( !s_LuaServerManager || !entity )
		return true;
	return s_LuaServerManager->EntityTakeDamage( entity, info );
}
#endif
