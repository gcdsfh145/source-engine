//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Embedded Lua plugin runtime shared by the client and server DLLs.
//
//=============================================================================

#include "cbase.h"
#include "takedamageinfo.h"
#include "lua/lua_plugin_manager.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include "filesystem.h"
#include "tier1/convar.h"
#include "tier1/tier1.h"
#include "tier1/strtools.h"
#include "tier1/utlbuffer.h"
#include "tier0/dbg.h"

// memdbgon must be the last include file in a .cpp file.
#include "tier0/memdbgon.h"

static const char s_LuaManagerRegistryKey[] = "SourceEngine.LuaPluginManager";
static ConVar lua_max_callbacks_per_frame( "lua_max_callbacks_per_frame", "2000", FCVAR_NONE,
	"Maximum Lua callbacks executed per game frame; 0 disables the limit." );
static ConVar lua_max_instructions_per_callback( "lua_max_instructions_per_callback", "100000", FCVAR_NONE,
	"Maximum Lua VM instructions executed by one callback; 0 disables the limit." );

struct CLuaPluginManager::LuaPlugin
{
	char name[128];
	char version[32];
	char dependencies[16][128];
	char permissions[32][64];
	int dependencyCount;
	int permissionCount;
	int configRef;
	int errorCount;
	bool faulted;
	int environmentRef;
};

struct CLuaPluginManager::LuaCommand
{
	ConCommand *command;
	LuaCommandCallback *callback;
	LuaPlugin *plugin;
	int functionRef;
	char name[128];
	char help[256];
};

struct CLuaPluginManager::LuaEvent
{
	char name[64];
	char identifier[64];
	LuaPlugin *plugin;
	int functionRef;
};

struct CLuaPluginManager::LuaTimer
{
	int id;
	float nextTime;
	float interval;
	bool repeat;
	bool cancelled;
	bool executing;
	char name[64];
	int repetitions;
	LuaPlugin *plugin;
	int functionRef;
};

struct CLuaPluginManager::LuaDisabledPlugin
{
	char name[128];
};

struct CLuaPluginManager::LuaCustomDefinition
{
	char kind[16];
	char name[128];
	LuaPlugin *plugin;
	int tableRef;
};

static void NormalizeLuaPluginName( const char *pluginName, char *name, int nameSize )
{
	Q_strncpy( name, pluginName ? pluginName : "", nameSize );
	char *extension = V_stristr( name, ".lua" );
	if ( extension && extension[4] == '\0' )
		*extension = '\0';
}

static void NormalizeLuaHookName( const char *hookName, char *name, int nameSize )
{
	Q_strncpy( name, hookName ? hookName : "", nameSize );
	struct HookAlias_t { const char *gmodName; const char *eventName; };
	static const HookAlias_t aliases[] =
	{
		{ "PlayerInitialSpawn", "client_put_in_server" },
		{ "PlayerDisconnected", "client_disconnect" },
		{ "PlayerSpawn", "player_spawn" },
		{ "PlayerDeath", "player_death" },
		{ "PlayerHurt", "player_hurt" },
		{ "WeaponFire", "weapon_fire" },
		{ "PlayerTeam", "player_team" },
		{ "PlayerSay", "player_say" },
		{ "PlayerUse", "player_use" },
		{ "EntityTakeDamage", "entity_take_damage" },
		{ "EntityCreated", "entity_created" },
		{ "PlayerConnect", "player_connect" },
		{ "PlayerFootstep", "player_footstep" },
		{ "PlayerJump", "player_jump" },
		{ "PlayerReload", "weapon_reload" },
		{ "ItemPickup", "item_pickup" },
		{ "RoundStart", "round_start" },
		{ "RoundEnd", "round_end" },
		{ "InitPostEntity", "level_init" },
		{ "ShutDown", "level_shutdown" },
		{ "Think", "game_frame" }
	};
	for ( int i = 0; i < ARRAYSIZE( aliases ); ++i )
	{
		if ( !Q_stricmp( name, aliases[i].gmodName ) )
		{
			Q_strncpy( name, aliases[i].eventName, nameSize );
			break;
		}
	}
}

static void LuaPushHookPlayer( lua_State *state, int value, bool valueIsUserID )
{
	const char *lookup = valueIsUserID ? "GetByID" : "GetByIndex";
	lua_getglobal( state, "player" );
	if ( lua_istable( state, -1 ) )
	{
		lua_getfield( state, -1, lookup );
		if ( lua_isfunction( state, -1 ) )
		{
			lua_pushinteger( state, value );
			if ( lua_pcall( state, 1, 1, 0 ) == 0 )
			{
				lua_remove( state, -2 );
				return;
			}
			lua_pop( state, 1 );
		}
		else
		{
			lua_pop( state, 1 );
		}
	}
	lua_pop( state, 1 );
	lua_pushinteger( state, value );
}

class CLuaPluginManager::LuaCommandCallback : public ICommandCallback
{
public:
	LuaCommandCallback( CLuaPluginManager *manager, LuaPlugin *plugin, int functionRef ) :
		m_manager( manager ), m_plugin( plugin ), m_functionRef( functionRef )
	{
	}

	virtual void CommandCallback( const CCommand &command )
	{
		m_manager->InvokeCommand( m_plugin, m_functionRef, command );
	}

	CLuaPluginManager *m_manager;
	LuaPlugin *m_plugin;
	int m_functionRef;
};

static void LuaReportError( lua_State *state, const char *where )
{
	const char *error = lua_tostring( state, -1 );
	Warning( "[Lua] %s: %s\n", where, error ? error : "unknown error" );
	lua_pop( state, 1 );
}

static void LuaInstructionHook( lua_State *state, lua_Debug *debug )
{
	(void)debug;
	lua_getfield( state, LUA_REGISTRYINDEX, s_LuaManagerRegistryKey );
	CLuaPluginManager *manager = (CLuaPluginManager *)lua_touserdata( state, -1 );
	lua_pop( state, 1 );
	if ( !manager )
		return;
	manager->OnLuaInstruction();
	int limit = lua_max_instructions_per_callback.GetInt();
	if ( limit > 0 && manager->LuaInstructionLimitExceeded() )
		luaL_error( state, "Lua instruction limit exceeded (%d)", limit );
}

CLuaPluginManager::CLuaPluginManager() :
	m_state( NULL ),
	m_fileSystem( NULL ),
	m_commandExecutor( NULL ),
	m_commandContext( NULL ),
	m_currentPlugin( NULL ),
	m_executingTimer( NULL ),
	m_bindingInstaller( NULL ),
	m_bindingContext( NULL ),
	m_currentTime( 0.0f ),
	m_frameCallbackCount( 0 ),
	m_callbackLimitWarning( false ),
	m_instructionCount( 0 ),
	m_nextTimerId( 1 )
{
	m_pathID[0] = '\0';
	m_pluginRoot[0] = '\0';
	m_disabledFile[0] = '\0';
	m_reloadRequested[0] = '\0';
	m_networkWriteName[0] = '\0';
	m_networkWritePayload[0] = '\0';
	m_networkReadPayload[0] = '\0';
	m_networkReadOffset = 0;
	m_role[0] = '\0';
}

CLuaPluginManager::~CLuaPluginManager()
{
	Shutdown();
}

bool CLuaPluginManager::Init( IFileSystem *fileSystem, const char *pathID, const char *pluginRoot, const char *role )
{
	Shutdown();

	if ( !fileSystem || !pluginRoot || !pluginRoot[0] )
		return false;

	lua_State *state = luaL_newstate();
	if ( !state )
		return false;

	// The game runtime intentionally exposes only safe standard libraries.
	// io, os, debug and package are not opened for plugin scripts.
	luaopen_base( state );
	luaopen_table( state );
	luaopen_string( state );
	luaopen_math( state );

	lua_pushlightuserdata( state, this );
	lua_setfield( state, LUA_REGISTRYINDEX, s_LuaManagerRegistryKey );
	lua_sethook( state, LuaInstructionHook, LUA_MASKCOUNT, 1000 );

	static const luaL_Reg pluginFunctions[] =
	{
		{ "log",              &CLuaPluginManager::LuaLog },
		{ "role",             &CLuaPluginManager::LuaRole },
		{ "Manifest",         &CLuaPluginManager::LuaManifest },
		{ "has_permission",   &CLuaPluginManager::LuaHasPermission },
		{ "require_permission", &CLuaPluginManager::LuaRequirePermission },
		{ "config_get",       &CLuaPluginManager::LuaConfigGet },
		{ "config_set",       &CLuaPluginManager::LuaConfigSet },
		{ "info",             &CLuaPluginManager::LuaPluginInfo },
		{ "reload",           &CLuaPluginManager::LuaReloadSelf },
		{ "execute",          &CLuaPluginManager::LuaExecute },
		{ "on",               &CLuaPluginManager::LuaOn },
		{ "register_command", &CLuaPluginManager::LuaRegisterCommand },
		{ NULL, NULL }
	};

	lua_newtable( state );
	luaL_register( state, NULL, pluginFunctions );

	static const luaL_Reg timerFunctions[] =
	{
		{ "after",  &CLuaPluginManager::LuaTimerAfter },
		{ "every",  &CLuaPluginManager::LuaTimerEvery },
		{ "cancel", &CLuaPluginManager::LuaTimerCancel },
		{ NULL, NULL }
	};
	lua_newtable( state );
	luaL_register( state, NULL, timerFunctions );
	lua_setfield( state, -2, "timer" );

	static const luaL_Reg convarFunctions[] =
	{
		{ "exists",    &CLuaPluginManager::LuaConVarExists },
		{ "get",       &CLuaPluginManager::LuaConVarGet },
		{ "get_int",   &CLuaPluginManager::LuaConVarGetInt },
		{ "get_float", &CLuaPluginManager::LuaConVarGetFloat },
		{ "set",       &CLuaPluginManager::LuaConVarSet },
		{ NULL, NULL }
	};
	lua_newtable( state );
	luaL_register( state, NULL, convarFunctions );
	lua_setfield( state, -2, "convar" );

	static const luaL_Reg fileFunctions[] =
	{
		{ "exists", &CLuaPluginManager::LuaFileExists },
		{ "read",   &CLuaPluginManager::LuaFileRead },
		{ "write",  &CLuaPluginManager::LuaFileWrite },
		{ NULL, NULL }
	};
	lua_newtable( state );
	luaL_register( state, NULL, fileFunctions );
	lua_setfield( state, -2, "file" );

	static const luaL_Reg vectorFunctions[] =
	{
		{ "new",       &CLuaPluginManager::LuaVectorNew },
		{ "add",       &CLuaPluginManager::LuaVectorAdd },
		{ "sub",       &CLuaPluginManager::LuaVectorSub },
		{ "scale",     &CLuaPluginManager::LuaVectorScale },
		{ "dot",       &CLuaPluginManager::LuaVectorDot },
		{ "length",    &CLuaPluginManager::LuaVectorLength },
		{ "distance",  &CLuaPluginManager::LuaVectorDistance },
		{ "normalize", &CLuaPluginManager::LuaVectorNormalize },
		{ NULL, NULL }
	};
	lua_newtable( state );
	luaL_register( state, NULL, vectorFunctions );
	lua_setfield( state, -2, "vector" );
	lua_newtable( state );
	luaL_register( state, NULL, vectorFunctions );
	lua_setfield( state, -2, "angle" );

	lua_setglobal( state, "plugin" );
	// Keep the constructor names used by common GMod server scripts while
	// retaining plugin.vector.new/plugin.angle.new for explicit code.
	lua_pushcfunction( state, &CLuaPluginManager::LuaVectorNew );
	lua_setglobal( state, "Vector" );
	lua_pushcfunction( state, &CLuaPluginManager::LuaVectorNew );
	lua_setglobal( state, "Angle" );

	static const luaL_Reg hookFunctions[] =
	{
		{ "Add",       &CLuaPluginManager::LuaHookAdd },
		{ "Remove",    &CLuaPluginManager::LuaHookRemove },
		{ "GetTable",  &CLuaPluginManager::LuaHookGetTable },
		{ "List",      &CLuaPluginManager::LuaHookList },
		{ "Call",      &CLuaPluginManager::LuaHookCall },
		{ NULL, NULL }
	};
	lua_newtable( state );
	luaL_register( state, NULL, hookFunctions );
	lua_setglobal( state, "hook" );

	static const luaL_Reg gmodTimerFunctions[] =
	{
		{ "Create", &CLuaPluginManager::LuaTimerCreate },
		{ "Remove", &CLuaPluginManager::LuaTimerRemove },
		{ "Simple", &CLuaPluginManager::LuaTimerSimple },
		{ NULL, NULL }
	};
	lua_newtable( state );
	luaL_register( state, NULL, gmodTimerFunctions );
	lua_setglobal( state, "timer" );

	static const luaL_Reg customWeaponFunctions[] =
	{
		{ "Register", &CLuaPluginManager::LuaRegisterCustomWeapon },
		{ NULL, NULL }
	};
	lua_newtable( state );
	luaL_register( state, NULL, customWeaponFunctions );
	lua_setglobal( state, "weapons" );

	static const luaL_Reg customNPCFunctions[] =
	{
		{ "Register", &CLuaPluginManager::LuaRegisterCustomNPC },
		{ NULL, NULL }
	};
	lua_newtable( state );
	luaL_register( state, NULL, customNPCFunctions );
	lua_setglobal( state, "npcs" );

	m_state = state;
	m_fileSystem = fileSystem;
	Q_strncpy( m_pathID, pathID && pathID[0] ? pathID : "MOD", sizeof( m_pathID ) );
	Q_strncpy( m_pluginRoot, pluginRoot, sizeof( m_pluginRoot ) );
	Q_snprintf( m_disabledFile, sizeof( m_disabledFile ), "%s/disabled.cfg", m_pluginRoot );
	Q_strncpy( m_role, role && role[0] ? role : "unknown", sizeof( m_role ) );
	LoadDisabledPlugins();
	if ( m_bindingInstaller )
		m_bindingInstaller( state, m_bindingContext );
	return true;
}

void CLuaPluginManager::Shutdown()
{
	lua_State *state = (lua_State *)m_state;
	if ( !state )
		return;

	for ( int i = m_plugins.Count() - 1; i >= 0; --i )
	{
		LuaPlugin *plugin = m_plugins[i];
		CallPluginFunction( plugin, "OnPluginUnload" );
		RemovePluginBindings( plugin );
		luaL_unref( state, LUA_REGISTRYINDEX, plugin->environmentRef );
		delete plugin;
	}
	m_timers.RemoveAll();
	m_plugins.RemoveAll();
	lua_close( state );

	m_state = NULL;
	m_fileSystem = NULL;
	m_currentPlugin = NULL;
}

CLuaPluginManager::LuaPlugin *CLuaPluginManager::FindPlugin( const char *pluginName ) const
{
	if ( !pluginName )
		return NULL;

	char name[128];
	NormalizeLuaPluginName( pluginName, name, sizeof( name ) );

	for ( int i = 0; i < m_plugins.Count(); ++i )
	{
		if ( !Q_stricmp( m_plugins[i]->name, name ) )
			return m_plugins[i];
	}
	return NULL;
}

bool CLuaPluginManager::HasPermission( const char *permission ) const
{
	if ( !m_currentPlugin || !permission || !permission[0] )
		return true;
	// Plugins without a manifest retain compatibility with existing scripts.
	if ( m_currentPlugin->permissionCount <= 0 )
		return true;
	for ( int i = 0; i < m_currentPlugin->permissionCount; ++i )
	{
		if ( !Q_stricmp( m_currentPlugin->permissions[i], permission ) ||
			!Q_stricmp( m_currentPlugin->permissions[i], "*" ) )
			return true;
	}
	return false;
}

void CLuaPluginManager::OnLuaInstruction()
{
	++m_instructionCount;
}

bool CLuaPluginManager::LuaInstructionLimitExceeded() const
{
	int limit = lua_max_instructions_per_callback.GetInt();
	return limit > 0 && m_instructionCount > limit;
}

bool CLuaPluginManager::PluginDependenciesLoaded( LuaPlugin *plugin ) const
{
	if ( !plugin )
		return false;
	for ( int i = 0; i < plugin->dependencyCount; ++i )
		if ( !FindPlugin( plugin->dependencies[i] ) )
			return false;
	return true;
}

void CLuaPluginManager::RecordPluginError( LuaPlugin *plugin, const char *where )
{
	if ( !plugin )
		return;
	++plugin->errorCount;
	Warning( "[Lua] plugin '%s' error #%d in %s\n", plugin->name,
		plugin->errorCount, where ? where : "callback" );
	if ( plugin->errorCount >= 5 && !plugin->faulted )
	{
		plugin->faulted = true;
		if ( !IsPluginDisabled( plugin->name ) )
		{
			LuaDisabledPlugin disabled;
			Q_strncpy( disabled.name, plugin->name, sizeof( disabled.name ) );
			m_disabledPlugins.AddToTail( disabled );
			SaveDisabledPlugins();
		}
		Warning( "[Lua] plugin '%s' disabled after repeated errors\n", plugin->name );
	}
}

CLuaPluginManager::LuaCustomDefinition *CLuaPluginManager::FindCustomDefinition( const char *kind, const char *name ) const
{
	if ( !kind || !name )
		return NULL;
	for ( int i = 0; i < m_customDefinitions.Count(); ++i )
	{
		LuaCustomDefinition *definition = m_customDefinitions[i];
		if ( !Q_stricmp( definition->kind, kind ) && !Q_stricmp( definition->name, name ) )
			return definition;
	}
	return NULL;
}

bool CLuaPluginManager::HasCustomDefinition( const char *kind, const char *name ) const
{
	return FindCustomDefinition( kind, name ) != NULL;
}

bool CLuaPluginManager::GetCustomDefinitionString( const char *kind, const char *name,
	const char *field, char *value, int valueSize ) const
{
	if ( value && valueSize > 0 )
		value[0] = '\0';
	if ( !m_state || !field || !value || valueSize <= 0 )
		return false;
	LuaCustomDefinition *definition = FindCustomDefinition( kind, name );
	if ( !definition )
		return false;
	lua_State *state = (lua_State *)m_state;
	lua_rawgeti( state, LUA_REGISTRYINDEX, definition->tableRef );
	lua_getfield( state, -1, field );
	bool found = lua_isstring( state, -1 ) != 0;
	if ( found )
		Q_strncpy( value, lua_tostring( state, -1 ), valueSize );
	lua_pop( state, 2 );
	return found;
}

int CLuaPluginManager::GetCustomDefinitionInt( const char *kind, const char *name,
	const char *field, int defaultValue ) const
{
	if ( !m_state || !field )
		return defaultValue;
	LuaCustomDefinition *definition = FindCustomDefinition( kind, name );
	if ( !definition )
		return defaultValue;

	lua_State *state = (lua_State *)m_state;
	lua_rawgeti( state, LUA_REGISTRYINDEX, definition->tableRef );
	lua_getfield( state, -1, field );
	int value = lua_isnumber( state, -1 ) ? (int)lua_tointeger( state, -1 ) : defaultValue;
	lua_pop( state, 2 );
	return value;
}

float CLuaPluginManager::GetCustomDefinitionFloat( const char *kind, const char *name,
	const char *field, float defaultValue ) const
{
	if ( !m_state || !field )
		return defaultValue;
	LuaCustomDefinition *definition = FindCustomDefinition( kind, name );
	if ( !definition )
		return defaultValue;

	lua_State *state = (lua_State *)m_state;
	lua_rawgeti( state, LUA_REGISTRYINDEX, definition->tableRef );
	lua_getfield( state, -1, field );
	float value = lua_isnumber( state, -1 ) ? (float)lua_tonumber( state, -1 ) : defaultValue;
	lua_pop( state, 2 );
	return value;
}

bool CLuaPluginManager::RegisterCustomDefinition( const char *kind, const char *name, lua_State *state, int tableIndex )
{
	if ( !m_currentPlugin || !kind || !name || !name[0] || !lua_istable( state, tableIndex ) )
		return false;
	LuaCustomDefinition *existing = FindCustomDefinition( kind, name );
	if ( existing && existing->plugin != m_currentPlugin )
		return false;
	if ( existing )
	{
		luaL_unref( state, LUA_REGISTRYINDEX, existing->tableRef );
		m_customDefinitions.FindAndRemove( existing );
		delete existing;
	}

	lua_pushvalue( state, tableIndex );
	LuaCustomDefinition *definition = new LuaCustomDefinition;
	Q_strncpy( definition->kind, kind, sizeof( definition->kind ) );
	Q_strncpy( definition->name, name, sizeof( definition->name ) );
	definition->plugin = m_currentPlugin;
	definition->tableRef = luaL_ref( state, LUA_REGISTRYINDEX );
	m_customDefinitions.AddToTail( definition );
	return true;
}

bool CLuaPluginManager::CallCustomEntityHook( const char *kind, const char *name, const char *callback,
	int entityIndex, float damage, int attackerIndex, bool *boolResult )
{
	if ( !m_state || !kind || !name || !callback )
		return false;
	LuaCustomDefinition *definition = FindCustomDefinition( kind, name );
	if ( !definition || definition->plugin->faulted )
		return false;
	if ( !BeginScriptCallback() )
		return false;

	lua_State *state = (lua_State *)m_state;
	lua_rawgeti( state, LUA_REGISTRYINDEX, definition->tableRef );
	lua_getfield( state, -1, callback );
	if ( !lua_isfunction( state, -1 ) )
	{
		lua_pop( state, 2 );
		return false;
	}
	lua_remove( state, -2 );
	lua_pushinteger( state, entityIndex );
	int argumentCount = 1;
	if ( damage >= 0.0f )
	{
		lua_pushnumber( state, damage );
		++argumentCount;
	}
	if ( attackerIndex >= 0 )
	{
		lua_pushinteger( state, attackerIndex );
		++argumentCount;
	}

	LuaPlugin *previousPlugin = m_currentPlugin;
	m_currentPlugin = definition->plugin;
	int result = lua_pcall( state, argumentCount, 1, 0 );
	m_currentPlugin = previousPlugin;
	if ( result != 0 )
	{
		RecordPluginError( definition->plugin, callback );
		LuaReportError( state, callback );
		return true;
	}
	if ( boolResult && lua_isboolean( state, -1 ) )
		*boolResult = lua_toboolean( state, -1 ) != 0;
	lua_pop( state, 1 );
	return true;
}

bool CLuaPluginManager::IsPluginDisabled( const char *pluginName ) const
{
	if ( !pluginName )
		return false;

	char name[128];
	NormalizeLuaPluginName( pluginName, name, sizeof( name ) );

	for ( int i = 0; i < m_disabledPlugins.Count(); ++i )
	{
		if ( !Q_stricmp( m_disabledPlugins[i].name, name ) )
			return true;
	}
	return false;
}

void CLuaPluginManager::LoadDisabledPlugins()
{
	m_disabledPlugins.RemoveAll();
	if ( !m_fileSystem || !m_disabledFile[0] )
		return;

	// Prefer the writable search path so a new empty file can clear a legacy
	// disabled list that may still exist in read-only MOD content.
	const char *readPathID = "DEFAULT_WRITE_PATH";
	if ( !m_fileSystem->FileExists( m_disabledFile, readPathID ) )
	{
		// Older builds wrote this file through MOD.
		readPathID = m_pathID;
		if ( !m_fileSystem->FileExists( m_disabledFile, readPathID ) )
			return;
	}

	CUtlBuffer buffer;
	if ( !m_fileSystem->ReadFile( m_disabledFile, readPathID, buffer ) )
		return;

	char contents[8192];
	int length = buffer.TellPut();
	if ( length >= (int)sizeof( contents ) )
		length = sizeof( contents ) - 1;
	memcpy( contents, buffer.Base(), length );
	contents[length] = '\0';

	char *line = contents;
	while ( line && *line )
	{
		char *next = strchr( line, '\n' );
		if ( next )
		{
			*next = '\0';
			++next;
		}
		while ( *line == ' ' || *line == '\t' || *line == '\r' ) ++line;
		char *comment = strchr( line, '#' );
		if ( comment ) *comment = '\0';
		char *end = line + Q_strlen( line );
		while ( end > line && ( end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' ) )
			*--end = '\0';

		if ( line[0] && !strchr( line, '/' ) && !strchr( line, '\\' ) && !strstr( line, ".." ) )
		{
			char *extension = V_stristr( line, ".lua" );
			if ( extension && extension[4] == '\0' ) *extension = '\0';
			if ( line[0] )
			{
				LuaDisabledPlugin disabled;
				Q_strncpy( disabled.name, line, sizeof( disabled.name ) );
				m_disabledPlugins.AddToTail( disabled );
			}
		}
		line = next;
	}
}

bool CLuaPluginManager::SaveDisabledPlugins() const
{
	if ( !m_fileSystem || !m_disabledFile[0] )
		return false;

	// MOD can resolve to read-only APK content on Android. Always create and
	// write persistent state through the engine's writable search path.
	m_fileSystem->CreateDirHierarchy( m_pluginRoot, "DEFAULT_WRITE_PATH" );
	CUtlBuffer buffer( 0, 0, CUtlBuffer::TEXT_BUFFER );
	for ( int i = 0; i < m_disabledPlugins.Count(); ++i )
	{
		buffer.PutString( m_disabledPlugins[i].name );
		buffer.PutChar( '\n' );
	}
	bool saved = m_fileSystem->WriteFile( m_disabledFile, "DEFAULT_WRITE_PATH", buffer );
	if ( !saved )
		Warning( "[Lua] Unable to save persistent plugin state '%s'\n", m_disabledFile );
	return saved;
}

bool CLuaPluginManager::SetPersistentPluginEnabled( const char *pluginName, bool enabled )
{
	if ( !pluginName || !pluginName[0] || strstr( pluginName, ".." ) ||
		strchr( pluginName, '/' ) || strchr( pluginName, '\\' ) )
		return false;

	char name[128];
	NormalizeLuaPluginName( pluginName, name, sizeof( name ) );

	bool found = false;
	for ( int i = m_disabledPlugins.Count() - 1; i >= 0; --i )
	{
		if ( Q_stricmp( m_disabledPlugins[i].name, name ) )
			continue;
		found = true;
		m_disabledPlugins.Remove( i );
	}

	if ( !enabled && !found )
	{
		LuaDisabledPlugin disabled;
		Q_strncpy( disabled.name, name, sizeof( disabled.name ) );
		m_disabledPlugins.AddToTail( disabled );
	}

	bool saved = SaveDisabledPlugins();
	if ( !saved )
		return false;
	if ( !enabled )
		UnloadPlugin( name );
	return saved;
}

bool CLuaPluginManager::IsPluginLoaded( const char *pluginName ) const
{
	return FindPlugin( pluginName ) != NULL;
}

bool CLuaPluginManager::LoadAll()
{
	if ( !m_state || !m_fileSystem )
		return false;

	// A dependency may sort after its consumer in the filesystem. Retry passes
	// so a normal directory scan still produces dependency order.
	bool loadedInPass;
	do
	{
		loadedInPass = false;
		char wildcard[sizeof( m_pluginRoot ) + 8];
		Q_snprintf( wildcard, sizeof( wildcard ), "%s/*.lua", m_pluginRoot );
		FileFindHandle_t handle;
		const char *fileName = m_fileSystem->FindFirstEx( wildcard, m_pathID, &handle );
		while ( fileName )
		{
			char currentFile[128];
			Q_strncpy( currentFile, fileName, sizeof( currentFile ) );
			if ( !m_fileSystem->FindIsDirectory( handle ) && !IsPluginDisabled( currentFile ) &&
				!FindPlugin( currentFile ) )
			{
				if ( LoadPluginFile( currentFile ) )
					loadedInPass = true;
			}
			fileName = m_fileSystem->FindNext( handle );
		}
		if ( handle != FILESYSTEM_INVALID_FIND_HANDLE )
			m_fileSystem->FindClose( handle );
	}
	while ( loadedInPass );

	return true;
}

bool CLuaPluginManager::LoadPlugin( const char *fileName )
{
	if ( !fileName || !fileName[0] )
		return false;

	// Plugins are loaded by filename, never by an arbitrary filesystem path.
	if ( strstr( fileName, ".." ) || strchr( fileName, '/' ) || strchr( fileName, '\\' ) )
		return false;

	return LoadPluginFile( fileName );
}

bool CLuaPluginManager::LoadPluginFile( const char *fileName )
{
	if ( FindPlugin( fileName ) )
		return false;

	char scriptName[128];
	Q_strncpy( scriptName, fileName, sizeof( scriptName ) );
	if ( !V_stristr( scriptName, ".lua" ) )
		Q_strncat( scriptName, ".lua", sizeof( scriptName ), COPY_ALL_CHARACTERS );

	char path[sizeof( m_pluginRoot ) + sizeof( scriptName ) + 2];
	Q_snprintf( path, sizeof( path ), "%s/%s", m_pluginRoot, scriptName );

	CUtlBuffer buffer;
	if ( !m_fileSystem->ReadFile( path, m_pathID, buffer ) )
	{
		Warning( "[Lua] Unable to read plugin '%s'\n", path );
		return false;
	}

	lua_State *state = (lua_State *)m_state;
	if ( luaL_loadbuffer( state, (const char *)buffer.Base(), buffer.TellPut(), path ) != 0 )
	{
		LuaReportError( state, path );
		return false;
	}

	// Each plugin gets its own environment. It inherits the safe global table,
	// but plugin globals cannot overwrite another plugin's globals.
	lua_newtable( state );
	lua_newtable( state );
	lua_pushvalue( state, LUA_GLOBALSINDEX );
	lua_setfield( state, -2, "__index" );
	lua_setmetatable( state, -2 );
	lua_pushvalue( state, -1 );
	lua_setfenv( state, -3 );
	int environmentRef = luaL_ref( state, LUA_REGISTRYINDEX );

	LuaPlugin *plugin = new LuaPlugin;
	memset( plugin, 0, sizeof( *plugin ) );
	Q_strncpy( plugin->name, scriptName, sizeof( plugin->name ) );
	char *extension = V_stristr( plugin->name, ".lua" );
	if ( extension )
		*extension = '\0';
	plugin->environmentRef = environmentRef;
	plugin->configRef = LUA_NOREF;
	m_plugins.AddToTail( plugin );

	m_currentPlugin = plugin;
	if ( lua_pcall( state, 0, 0, 0 ) != 0 )
	{
		LuaReportError( state, path );
		m_currentPlugin = NULL;
		RemovePluginBindings( plugin );
		luaL_unref( state, LUA_REGISTRYINDEX, plugin->environmentRef );
		m_plugins.FindAndRemove( plugin );
		delete plugin;
		return false;
	}
	if ( !PluginDependenciesLoaded( plugin ) )
	{
		Warning( "[Lua] plugin '%s' has missing dependencies; load it after its dependencies\n", plugin->name );
		m_currentPlugin = NULL;
		RemovePluginBindings( plugin );
		luaL_unref( state, LUA_REGISTRYINDEX, plugin->environmentRef );
		m_plugins.FindAndRemove( plugin );
		delete plugin;
		return false;
	}
	CallPluginFunction( plugin, "OnPluginLoad" );
	m_currentPlugin = NULL;

	Msg( "[Lua] Loaded %s plugin '%s'\n", m_role, plugin->name );
	return true;
}

void CLuaPluginManager::RemovePluginBindings( LuaPlugin *plugin )
{
	lua_State *state = (lua_State *)m_state;
	RemovePluginTimers( plugin );
	for ( int i = m_commands.Count() - 1; i >= 0; --i )
	{
		if ( m_commands[i]->plugin != plugin )
			continue;
		if ( g_pCVar && m_commands[i]->command )
			g_pCVar->UnregisterConCommand( m_commands[i]->command );
		delete m_commands[i]->command;
		delete m_commands[i]->callback;
		luaL_unref( state, LUA_REGISTRYINDEX, m_commands[i]->functionRef );
		delete m_commands[i];
		m_commands.Remove( i );
	}

	for ( int i = m_events.Count() - 1; i >= 0; --i )
	{
		if ( m_events[i]->plugin != plugin )
			continue;
		luaL_unref( state, LUA_REGISTRYINDEX, m_events[i]->functionRef );
		delete m_events[i];
		m_events.Remove( i );
	}

	for ( int i = m_customDefinitions.Count() - 1; i >= 0; --i )
	{
		LuaCustomDefinition *definition = m_customDefinitions[i];
		if ( definition->plugin != plugin )
			continue;
		luaL_unref( state, LUA_REGISTRYINDEX, definition->tableRef );
		delete definition;
		m_customDefinitions.Remove( i );
	}
	if ( plugin->configRef != LUA_NOREF )
		luaL_unref( state, LUA_REGISTRYINDEX, plugin->configRef );
}

void CLuaPluginManager::RemovePluginTimers( LuaPlugin *plugin )
{
	lua_State *state = (lua_State *)m_state;
	for ( int i = m_timers.Count() - 1; i >= 0; --i )
	{
		LuaTimer *timer = m_timers[i];
		if ( timer->plugin != plugin )
			continue;
		luaL_unref( state, LUA_REGISTRYINDEX, timer->functionRef );
		delete timer;
		m_timers.Remove( i );
	}
}

bool CLuaPluginManager::UnloadPlugin( const char *pluginName )
{
	LuaPlugin *plugin = FindPlugin( pluginName );
	if ( !plugin )
		return false;

	m_currentPlugin = plugin;
	CallPluginFunction( plugin, "OnPluginUnload" );
	m_currentPlugin = NULL;
	RemovePluginBindings( plugin );
	luaL_unref( (lua_State *)m_state, LUA_REGISTRYINDEX, plugin->environmentRef );
	m_plugins.FindAndRemove( plugin );
	delete plugin;
	return true;
}

bool CLuaPluginManager::ReloadPlugin( const char *pluginName )
{
	if ( !UnloadPlugin( pluginName ) )
		return false;
	return LoadPlugin( pluginName );
}

void CLuaPluginManager::ReloadAll()
{
	for ( int i = m_plugins.Count() - 1; i >= 0; --i )
	{
		char name[128];
		Q_strncpy( name, m_plugins[i]->name, sizeof( name ) );
		UnloadPlugin( name );
	}
	LoadAll();
}

bool CLuaPluginManager::BeginScriptCallback()
{
	int limit = lua_max_callbacks_per_frame.GetInt();
	if ( limit > 0 && m_frameCallbackCount >= limit )
	{
		if ( !m_callbackLimitWarning )
		{
			Warning( "[Lua] %s callback budget exceeded (%d); deferring callbacks until next frame\n",
				m_role, limit );
			m_callbackLimitWarning = true;
		}
		return false;
	}
	++m_frameCallbackCount;
	return true;
}

void CLuaPluginManager::PrintPlugins() const
{
	Msg( "Lua %s plugins:\n", m_role );
	for ( int i = 0; i < m_plugins.Count(); ++i )
		Msg( "  %d: %s\n", i, m_plugins[i]->name );
}

void CLuaPluginManager::CallPluginFunction( LuaPlugin *plugin, const char *functionName, int argumentCount )
{
	if ( !m_state || !plugin || plugin->faulted )
		return;
	if ( !BeginScriptCallback() )
		return;

	lua_State *state = (lua_State *)m_state;
	lua_rawgeti( state, LUA_REGISTRYINDEX, plugin->environmentRef );
	lua_getfield( state, -1, functionName );
	if ( !lua_isfunction( state, -1 ) )
	{
		lua_pop( state, 2 );
		return;
	}
	lua_remove( state, -2 );
	LuaPlugin *previousPlugin = m_currentPlugin;
	m_currentPlugin = plugin;
	int result = lua_pcall( state, argumentCount, 0, 0 );
	m_currentPlugin = previousPlugin;
	if ( result != 0 )
	{
		RecordPluginError( plugin, functionName );
		LuaReportError( state, functionName );
	}
}

void CLuaPluginManager::EmitEvent( const char *eventName, int intArg, const char *stringArg, bool boolArg )
{
	if ( !m_state )
		return;

	lua_State *state = (lua_State *)m_state;
	for ( int i = 0; i < m_events.Count(); ++i )
	{
		LuaEvent *event = m_events[i];
		if ( Q_stricmp( event->name, eventName ) || event->plugin->faulted )
			continue;
	if ( !BeginScriptCallback() )
			break;
		// A hook callback may remove itself while it runs. Keep only the
		// values needed for this invocation so the callback cannot leave us
		// dereferencing a deleted LuaEvent.
		LuaPlugin *plugin = event->plugin;
		int functionRef = event->functionRef;
		bool isHook = event->identifier[0] != '\0';

		lua_rawgeti( state, LUA_REGISTRYINDEX, functionRef );
		int argumentCount = 0;
		if ( !Q_stricmp( eventName, "game_frame" ) )
		{
			lua_pushboolean( state, boolArg ? 1 : 0 );
			argumentCount = 1;
		}
		else if ( !Q_stricmp( eventName, "level_init" ) )
		{
			lua_pushstring( state, stringArg ? stringArg : "" );
			argumentCount = 1;
		}
		else if ( !Q_stricmp( eventName, "client_put_in_server" ) )
		{
			if ( isHook )
				LuaPushHookPlayer( state, intArg, false );
			else
				lua_pushinteger( state, intArg );
			lua_pushstring( state, stringArg ? stringArg : "" );
			argumentCount = 2;
		}
		else if ( !Q_stricmp( eventName, "client_disconnect" ) )
		{
			if ( isHook )
				LuaPushHookPlayer( state, intArg, false );
			else
				lua_pushinteger( state, intArg );
			argumentCount = 1;
		}

		LuaPlugin *previousPlugin = m_currentPlugin;
		m_currentPlugin = plugin;
		int result = lua_pcall( state, argumentCount, 0, 0 );
		m_currentPlugin = previousPlugin;
		if ( result != 0 )
		{
			RecordPluginError( plugin, eventName );
			LuaReportError( state, eventName );
		}
	}
}

void CLuaPluginManager::LevelInit( const char *mapName )
{
	for ( int i = 0; i < m_plugins.Count(); ++i )
		CallPluginFunction( m_plugins[i], "OnLevelInit" );
	EmitEvent( "level_init", 0, mapName, false );
}

void CLuaPluginManager::LevelShutdown()
{
	for ( int i = 0; i < m_plugins.Count(); ++i )
		CallPluginFunction( m_plugins[i], "OnLevelShutdown" );
	EmitEvent( "level_shutdown" );
}

void CLuaPluginManager::Frame( bool simulating, float currentTime )
{
	m_currentTime = currentTime;
	m_frameCallbackCount = 0;
	m_callbackLimitWarning = false;
	for ( int i = 0; i < m_plugins.Count(); ++i )
		CallPluginFunction( m_plugins[i], "OnGameFrame" );
	EmitEvent( "game_frame", 0, NULL, simulating );
	ProcessTimers();
	if ( m_reloadRequested[0] )
	{
		char pluginName[128];
		Q_strncpy( pluginName, m_reloadRequested, sizeof( pluginName ) );
		m_reloadRequested[0] = '\0';
		ReloadPlugin( pluginName );
	}
}

void CLuaPluginManager::ProcessTimers()
{
	if ( !m_state )
		return;

	lua_State *state = (lua_State *)m_state;
	for ( int i = 0; i < m_timers.Count(); )
	{
		LuaTimer *timer = m_timers[i];
		if ( timer->plugin->faulted )
		{
			timer->cancelled = true;
		}
		if ( timer->cancelled || timer->nextTime > m_currentTime )
		{
			if ( timer->cancelled )
			{
				luaL_unref( state, LUA_REGISTRYINDEX, timer->functionRef );
				delete timer;
				m_timers.Remove( i );
				continue;
			}
			++i;
			continue;
		}

		if ( timer->repeat && ( timer->repetitions == 0 || timer->repetitions > 1 ) )
		{
			if ( timer->repetitions > 1 )
				--timer->repetitions;
			timer->nextTime = m_currentTime + timer->interval;
		}
		else
			timer->cancelled = true;

		if ( !BeginScriptCallback() )
		{
			timer->nextTime = m_currentTime + MAX( timer->interval, 0.01f );
			++i;
			continue;
		}
		lua_rawgeti( state, LUA_REGISTRYINDEX, timer->functionRef );
		LuaPlugin *previousPlugin = m_currentPlugin;
		m_currentPlugin = timer->plugin;
		timer->executing = true;
		m_executingTimer = timer;
		int result = lua_pcall( state, 0, 0, 0 );
		m_executingTimer = NULL;
		timer->executing = false;
		m_currentPlugin = previousPlugin;
		if ( result != 0 )
		{
			RecordPluginError( timer->plugin, "timer" );
			LuaReportError( state, "timer" );
		}

		if ( timer->cancelled )
		{
			// The callback may have cancelled itself. Find the pointer again
			// because other Lua calls may have removed timers while it ran.
			for ( int removeIndex = m_timers.Count() - 1; removeIndex >= 0; --removeIndex )
			{
				if ( m_timers[removeIndex] != timer )
					continue;
				luaL_unref( state, LUA_REGISTRYINDEX, timer->functionRef );
				delete timer;
				m_timers.Remove( removeIndex );
				break;
			}
			continue;
		}
		++i;
	}
}

void CLuaPluginManager::ClientPutInServer( int entIndex, const char *playerName )
{
	EmitEvent( "client_put_in_server", entIndex, playerName, false );
}

void CLuaPluginManager::ClientDisconnect( int entIndex )
{
	EmitEvent( "client_disconnect", entIndex );
}

void CLuaPluginManager::NetworkMessage( const char *name, const char *payload )
{
	if ( !m_state || !name )
		return;
	Q_strncpy( m_networkReadPayload, payload ? payload : "", sizeof( m_networkReadPayload ) );
	m_networkReadOffset = 0;

	lua_State *state = (lua_State *)m_state;
	for ( int i = 0; i < m_events.Count(); ++i )
	{
		LuaEvent *event = m_events[i];
		if ( Q_stricmp( event->name, "net_message" ) || event->plugin->faulted )
			continue;
		LuaPlugin *plugin = event->plugin;
		int functionRef = event->functionRef;
		if ( !BeginScriptCallback() )
			break;
		m_networkReadOffset = 0;

		lua_rawgeti( state, LUA_REGISTRYINDEX, functionRef );
		lua_pushstring( state, name );
		lua_pushstring( state, payload ? payload : "" );
		LuaPlugin *previousPlugin = m_currentPlugin;
		m_currentPlugin = plugin;
		int result = lua_pcall( state, 2, 0, 0 );
		m_currentPlugin = previousPlugin;
		if ( result != 0 )
		{
			RecordPluginError( plugin, "net_message" );
			LuaReportError( state, "net_message" );
		}
	}
}

bool CLuaPluginManager::NetworkAppendToken( const char *token )
{
	if ( !token || !token[0] ) return false;
	int current = Q_strlen( m_networkWritePayload );
	int added = Q_strlen( token );
	if ( current + added >= (int)sizeof( m_networkWritePayload ) ) return false;
	Q_strncat( m_networkWritePayload, token, sizeof( m_networkWritePayload ), COPY_ALL_CHARACTERS );
	return true;
}

void CLuaPluginManager::NetworkStart( const char *name )
{
	Q_strncpy( m_networkWriteName, name ? name : "", sizeof( m_networkWriteName ) );
	m_networkWritePayload[0] = '\0';
}

bool CLuaPluginManager::NetworkWriteString( const char *value )
{
	char token[1024];
	const char *text = value ? value : "";
	if ( Q_strlen( text ) > 900 ) return false;
	Q_snprintf( token, sizeof( token ), "s:%d:%s;", Q_strlen( text ), text );
	return NetworkAppendToken( token );
}

bool CLuaPluginManager::NetworkWriteInt( int value )
{
	char token[64]; Q_snprintf( token, sizeof( token ), "i:%d;", value ); return NetworkAppendToken( token );
}

bool CLuaPluginManager::NetworkWriteFloat( float value )
{
	char token[64]; Q_snprintf( token, sizeof( token ), "f:%.9g;", value ); return NetworkAppendToken( token );
}

bool CLuaPluginManager::NetworkWriteBool( bool value )
{
	return NetworkAppendToken( value ? "b:1;" : "b:0;" );
}

bool CLuaPluginManager::NetworkWriteVector( float x, float y, float z )
{
	char token[128]; Q_snprintf( token, sizeof( token ), "v:%.9g,%.9g,%.9g;", x, y, z ); return NetworkAppendToken( token );
}

bool CLuaPluginManager::NetworkWriteEntity( int entityIndex )
{
	char token[64]; Q_snprintf( token, sizeof( token ), "e:%d;", entityIndex ); return NetworkAppendToken( token );
}

bool CLuaPluginManager::NetworkReadToken( char type, char *value, int valueSize )
{
	if ( !value || valueSize <= 0 || m_networkReadOffset < 0 ) return false;
	const int length = Q_strlen( m_networkReadPayload );
	int start = m_networkReadOffset;
	if ( start + 2 >= length || m_networkReadPayload[start] != type || m_networkReadPayload[start + 1] != ':' ) return false;
	int dataStart = start + 2;
	int dataEnd = dataStart;
	if ( type == 's' )
	{
		int lengthEnd = dataStart;
		while ( lengthEnd < length && m_networkReadPayload[lengthEnd] != ':' ) ++lengthEnd;
		if ( lengthEnd >= length ) return false;
		int stringLength = atoi( m_networkReadPayload + dataStart );
		dataStart = lengthEnd + 1;
		dataEnd = dataStart + stringLength;
		if ( stringLength < 0 || dataEnd >= length || m_networkReadPayload[dataEnd] != ';' ) return false;
	}
	else
	{
		while ( dataEnd < length && m_networkReadPayload[dataEnd] != ';' ) ++dataEnd;
		if ( dataEnd >= length ) return false;
	}
	int copyLength = MIN( dataEnd - dataStart, valueSize - 1 );
	memcpy( value, m_networkReadPayload + dataStart, copyLength );
	value[copyLength] = '\0';
	m_networkReadOffset = dataEnd + 1;
	return true;
}

bool CLuaPluginManager::NetworkReadInt( int &value )
{
	char text[64]; if ( !NetworkReadToken( 'i', text, sizeof( text ) ) ) return false; value = atoi( text ); return true;
}

bool CLuaPluginManager::NetworkReadFloat( float &value )
{
	char text[64]; if ( !NetworkReadToken( 'f', text, sizeof( text ) ) ) return false; value = (float)atof( text ); return true;
}

bool CLuaPluginManager::NetworkReadBool( bool &value )
{
	char text[8]; if ( !NetworkReadToken( 'b', text, sizeof( text ) ) ) return false; value = atoi( text ) != 0; return true;
}

bool CLuaPluginManager::NetworkReadString( char *value, int valueSize )
{
	return NetworkReadToken( 's', value, valueSize );
}

bool CLuaPluginManager::NetworkReadVector( float &x, float &y, float &z )
{
	char text[128]; if ( !NetworkReadToken( 'v', text, sizeof( text ) ) ) return false;
	return sscanf( text, "%f,%f,%f", &x, &y, &z ) == 3;
}

bool CLuaPluginManager::NetworkReadEntity( int &entityIndex )
{
	char text[32]; if ( !NetworkReadToken( 'e', text, sizeof( text ) ) ) return false; entityIndex = atoi( text ); return true;
}

void CLuaPluginManager::GameEvent( const char *eventName, int value1, int value2, int value3,
	const char *string1, const char *string2 )
{
	if ( !m_state || !eventName )
		return;

	lua_State *state = (lua_State *)m_state;
	for ( int i = 0; i < m_events.Count(); ++i )
	{
		LuaEvent *event = m_events[i];
		if ( Q_stricmp( event->name, eventName ) || event->plugin->faulted )
			continue;
		LuaPlugin *plugin = event->plugin;
		int functionRef = event->functionRef;
		bool isHook = event->identifier[0] != '\0';
		if ( !BeginScriptCallback() )
			break;

		lua_rawgeti( state, LUA_REGISTRYINDEX, functionRef );
		int argumentCount = 0;
		if ( !Q_stricmp( eventName, "player_death" ) )
		{
			if ( isHook )
			{
				LuaPushHookPlayer( state, value1, true );
				LuaPushHookPlayer( state, value2, true );
			}
			else
			{
				lua_pushinteger( state, value1 );
				lua_pushinteger( state, value2 );
			}
			lua_pushstring( state, string1 ? string1 : "" );
			argumentCount = 3;
		}
		else if ( !Q_stricmp( eventName, "player_hurt" ) )
		{
			if ( isHook )
			{
				LuaPushHookPlayer( state, value1, true );
				LuaPushHookPlayer( state, value2, true );
			}
			else
			{
				lua_pushinteger( state, value1 );
				lua_pushinteger( state, value2 );
			}
			lua_pushinteger( state, value3 );
			argumentCount = 3;
		}
		else if ( !Q_stricmp( eventName, "weapon_fire" ) ||
			!Q_stricmp( eventName, "item_pickup" ) ||
			!Q_stricmp( eventName, "player_say" ) )
		{
			if ( isHook )
				LuaPushHookPlayer( state, value1, true );
			else
				lua_pushinteger( state, value1 );
			lua_pushstring( state, string1 ? string1 : "" );
			argumentCount = 2;
		}
		else if ( !Q_stricmp( eventName, "player_team" ) ||
			!Q_stricmp( eventName, "round_end" ) )
		{
			if ( isHook && !Q_stricmp( eventName, "player_team" ) )
				LuaPushHookPlayer( state, value1, true );
			else
				lua_pushinteger( state, value1 );
			lua_pushinteger( state, value2 );
			argumentCount = 2;
		}
		else if ( !Q_stricmp( eventName, "round_start" ) )
		{
			argumentCount = 0;
		}
		else if ( !Q_stricmp( eventName, "entity_take_damage" ) )
		{
			// Entity indexes remain integers here because the manager is shared by
			// client and server DLLs; scripts can resolve them with ents.GetByIndex.
			lua_pushinteger( state, value1 );
			lua_pushnumber( state, (float)value3 );
			lua_pushinteger( state, value2 );
			argumentCount = 3;
		}
		else if ( !Q_stricmp( eventName, "player_use" ) )
		{
			if ( isHook )
				LuaPushHookPlayer( state, value1, true );
			else
				lua_pushinteger( state, value1 );
			lua_pushinteger( state, value2 );
			argumentCount = 2;
		}
		else if ( !Q_stricmp( eventName, "player_connect" ) )
		{
			lua_pushinteger( state, value1 );
			lua_pushstring( state, string1 ? string1 : "" );
			lua_pushstring( state, string2 ? string2 : "" );
			argumentCount = 3;
		}
		else if ( !Q_stricmp( eventName, "entity_created" ) )
		{
			lua_pushinteger( state, value1 );
			lua_pushstring( state, string1 ? string1 : "" );
			argumentCount = 2;
		}
		else
		{
			if ( isHook && !Q_stricmp( eventName, "player_spawn" ) )
				LuaPushHookPlayer( state, value1, true );
			else
				lua_pushinteger( state, value1 );
			argumentCount = 1;
		}

		LuaPlugin *previousPlugin = m_currentPlugin;
		m_currentPlugin = plugin;
		int result = lua_pcall( state, argumentCount, 0, 0 );
		m_currentPlugin = previousPlugin;
		if ( result != 0 )
		{
			RecordPluginError( plugin, eventName );
			LuaReportError( state, eventName );
		}
	}
}

bool CLuaPluginManager::EntityTakeDamage( CBaseEntity *entity, const CTakeDamageInfo &info )
{
	if ( !m_state || !entity )
		return true;
	lua_State *state = (lua_State *)m_state;
	bool allowDamage = true;
	int attackerIndex = info.GetAttacker() ? info.GetAttacker()->entindex() : -1;
	for ( int i = 0; i < m_events.Count(); ++i )
	{
		LuaEvent *event = m_events[i];
		if ( Q_stricmp( event->name, "entity_take_damage" ) || event->plugin->faulted )
			continue;
		if ( !BeginScriptCallback() )
			break;
		LuaPlugin *plugin = event->plugin;
		int functionRef = event->functionRef;
		bool isHook = event->identifier[0] != '\0';
		lua_rawgeti( state, LUA_REGISTRYINDEX, functionRef );
		lua_pushinteger( state, entity->entindex() );
		lua_pushnumber( state, info.GetDamage() );
		lua_pushinteger( state, attackerIndex );
		LuaPlugin *previousPlugin = m_currentPlugin;
		m_currentPlugin = plugin;
		int result = lua_pcall( state, 3, isHook ? 1 : 0, 0 );
		m_currentPlugin = previousPlugin;
		if ( result != 0 )
		{
			RecordPluginError( plugin, "EntityTakeDamage" );
			LuaReportError( state, "entity_take_damage" );
			continue;
		}
		if ( isHook )
		{
			if ( lua_isboolean( state, -1 ) && !lua_toboolean( state, -1 ) )
				allowDamage = false;
			lua_pop( state, 1 );
		}
	}
	return allowDamage;
}

void CLuaPluginManager::SetCommandExecutor( LuaCommandExecutor_t executor, void *context )
{
	m_commandExecutor = executor;
	m_commandContext = context;
}

void CLuaPluginManager::SetBindingInstaller( LuaBindingInstaller_t installer, void *context )
{
	m_bindingInstaller = installer;
	m_bindingContext = context;
}

void CLuaPluginManager::InvokeCommand( LuaPlugin *plugin, int functionRef, const CCommand &command )
{
	if ( !m_state || !plugin || plugin->faulted )
		return;
	if ( !BeginScriptCallback() )
		return;

	lua_State *state = (lua_State *)m_state;
	lua_rawgeti( state, LUA_REGISTRYINDEX, functionRef );
	lua_pushstring( state, command.Arg( 0 ) );
	lua_newtable( state );
	for ( int i = 0; i < command.ArgC(); ++i )
	{
		lua_pushinteger( state, i );
		lua_pushstring( state, command.Arg( i ) );
		lua_rawset( state, -3 );
	}
	LuaPlugin *previousPlugin = m_currentPlugin;
	m_currentPlugin = plugin;
	int result = lua_pcall( state, 2, 0, 0 );
	m_currentPlugin = previousPlugin;
	if ( result != 0 )
	{
		RecordPluginError( plugin, command.Arg( 0 ) );
		LuaReportError( state, command.Arg( 0 ) );
	}
}

CLuaPluginManager *CLuaPluginManager::FromLuaState( lua_State *state )
{
	lua_getfield( state, LUA_REGISTRYINDEX, s_LuaManagerRegistryKey );
	CLuaPluginManager *manager = (CLuaPluginManager *)lua_touserdata( state, -1 );
	lua_pop( state, 1 );
	return manager;
}

int CLuaPluginManager::LuaLog( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	char message[1024];
	int used = 0;
	message[0] = '\0';
	for ( int i = 1; i <= lua_gettop( state ) && used < (int)sizeof( message ) - 1; ++i )
	{
		const char *value = lua_tostring( state, i );
		if ( !value )
			value = lua_typename( state, lua_type( state, i ) );
		used += Q_snprintf( message + used, sizeof( message ) - used, "%s%s", i == 1 ? "" : "\t", value );
	}
	Msg( "[Lua][%s] %s\n", manager ? manager->m_role : "unknown", message );
	return 0;
}

int CLuaPluginManager::LuaRole( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	lua_pushstring( state, manager ? manager->m_role : "unknown" );
	return 1;
}

static void LuaCopyManifestArray( lua_State *state, int tableIndex, const char *field,
	char *values, int valueStride, int &count, int maxCount )
{
	count = 0;
	lua_getfield( state, tableIndex, field );
	if ( lua_istable( state, -1 ) )
	{
		for ( int i = 1; i <= maxCount; ++i )
		{
			lua_rawgeti( state, -1, i );
			if ( !lua_isstring( state, -1 ) )
			{
				lua_pop( state, 1 );
				break;
			}
			Q_strncpy( values + count * valueStride, lua_tostring( state, -1 ), valueStride );
			++count;
			lua_pop( state, 1 );
		}
	}
	lua_pop( state, 1 );
}

int CLuaPluginManager::LuaManifest( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( !manager || !manager->m_currentPlugin )
		return luaL_error( state, "plugin.Manifest may only be called by a plugin" );
	luaL_checktype( state, 1, LUA_TTABLE );
	LuaPlugin *plugin = manager->m_currentPlugin;
	Q_strncpy( plugin->version, "0.0.0", sizeof( plugin->version ) );
	lua_getfield( state, 1, "version" );
	if ( lua_isstring( state, -1 ) )
		Q_strncpy( plugin->version, lua_tostring( state, -1 ), sizeof( plugin->version ) );
	lua_pop( state, 1 );
	LuaCopyManifestArray( state, 1, "dependencies", &plugin->dependencies[0][0],
		sizeof( plugin->dependencies[0] ), plugin->dependencyCount, ARRAYSIZE( plugin->dependencies ) );
	LuaCopyManifestArray( state, 1, "permissions", &plugin->permissions[0][0],
		sizeof( plugin->permissions[0] ), plugin->permissionCount, ARRAYSIZE( plugin->permissions ) );
	if ( plugin->configRef != LUA_NOREF )
		luaL_unref( state, LUA_REGISTRYINDEX, plugin->configRef );
	lua_getfield( state, 1, "config" );
	if ( lua_istable( state, -1 ) )
		plugin->configRef = luaL_ref( state, LUA_REGISTRYINDEX );
	else
	{
		lua_pop( state, 1 );
		lua_newtable( state );
		plugin->configRef = luaL_ref( state, LUA_REGISTRYINDEX );
	}
	return 0;
}

int CLuaPluginManager::LuaHasPermission( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	lua_pushboolean( state, manager && manager->HasPermission( luaL_checkstring( state, 1 ) ) );
	return 1;
}

int CLuaPluginManager::LuaRequirePermission( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	const char *permission = luaL_checkstring( state, 1 );
	if ( !manager || !manager->HasPermission( permission ) )
		return luaL_error( state, "plugin permission denied: %s", permission );
	return 0;
}

int CLuaPluginManager::LuaConfigGet( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( !manager || !manager->m_currentPlugin || manager->m_currentPlugin->configRef == LUA_NOREF )
	{
		if ( lua_gettop( state ) >= 2 ) lua_pushvalue( state, 2 ); else lua_pushnil( state );
		return 1;
	}
	lua_rawgeti( state, LUA_REGISTRYINDEX, manager->m_currentPlugin->configRef );
	lua_getfield( state, -1, luaL_checkstring( state, 1 ) );
	if ( lua_isnil( state, -1 ) )
	{
		lua_pop( state, 2 );
		if ( lua_gettop( state ) >= 2 ) lua_pushvalue( state, 2 );
		else lua_pushnil( state );
		return 1;
	}
	lua_remove( state, -2 );
	return 1;
}

int CLuaPluginManager::LuaConfigSet( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( !manager || !manager->m_currentPlugin )
		return luaL_error( state, "plugin.config_set may only be called by a plugin" );
	if ( !manager->HasPermission( "config.write" ) )
		return luaL_error( state, "plugin permission denied: config.write" );
	if ( manager->m_currentPlugin->configRef == LUA_NOREF )
	{
		lua_newtable( state );
		manager->m_currentPlugin->configRef = luaL_ref( state, LUA_REGISTRYINDEX );
	}
	lua_rawgeti( state, LUA_REGISTRYINDEX, manager->m_currentPlugin->configRef );
	lua_pushvalue( state, 2 );
	lua_setfield( state, -2, luaL_checkstring( state, 1 ) );
	lua_pop( state, 1 );
	return 0;
}

int CLuaPluginManager::LuaPluginInfo( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( !manager || !manager->m_currentPlugin ) return 0;
	LuaPlugin *plugin = manager->m_currentPlugin;
	lua_newtable( state );
	lua_pushstring( state, plugin->name ); lua_setfield( state, -2, "name" );
	lua_pushstring( state, plugin->version ); lua_setfield( state, -2, "version" );
	lua_pushstring( state, manager->m_role ); lua_setfield( state, -2, "role" );
	lua_pushinteger( state, plugin->errorCount ); lua_setfield( state, -2, "errors" );
	lua_pushinteger( state, plugin->dependencyCount ); lua_setfield( state, -2, "dependency_count" );
	lua_pushinteger( state, plugin->permissionCount ); lua_setfield( state, -2, "permission_count" );
	return 1;
}

int CLuaPluginManager::LuaReloadSelf( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( !manager || !manager->m_currentPlugin ) return 0;
	Q_strncpy( manager->m_reloadRequested, manager->m_currentPlugin->name,
		sizeof( manager->m_reloadRequested ) );
	lua_pushboolean( state, 1 );
	return 1;
}

int CLuaPluginManager::LuaExecute( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	const char *command = luaL_checkstring( state, 1 );
	if ( manager && !manager->HasPermission( "engine.execute" ) )
		return luaL_error( state, "plugin permission denied: engine.execute" );
	if ( manager && manager->m_commandExecutor )
		manager->m_commandExecutor( command, manager->m_commandContext );
	return 0;
}

int CLuaPluginManager::LuaOn( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( !manager || !manager->m_currentPlugin )
		return luaL_error( state, "plugin.on may only be called while loading a plugin" );

	const char *eventName = luaL_checkstring( state, 1 );
	luaL_checktype( state, 2, LUA_TFUNCTION );
	lua_pushvalue( state, 2 );
	int functionRef = luaL_ref( state, LUA_REGISTRYINDEX );

	LuaEvent *event = new LuaEvent;
	Q_strncpy( event->name, eventName, sizeof( event->name ) );
	event->identifier[0] = '\0';
	event->plugin = manager->m_currentPlugin;
	event->functionRef = functionRef;
	manager->m_events.AddToTail( event );
	return 0;
}

int CLuaPluginManager::LuaHookAdd( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( !manager || !manager->m_currentPlugin )
		return luaL_error( state, "hook.Add may only be called by a plugin" );

	const char *hookName = luaL_checkstring( state, 1 );
	const char *identifier = luaL_checkstring( state, 2 );
	luaL_checktype( state, 3, LUA_TFUNCTION );
	char eventName[64];
	NormalizeLuaHookName( hookName, eventName, sizeof( eventName ) );
	if ( !eventName[0] || !identifier[0] )
		return luaL_error( state, "hook.Add requires an event and identifier" );

	for ( int i = manager->m_events.Count() - 1; i >= 0; --i )
	{
		LuaEvent *event = manager->m_events[i];
		if ( event->plugin != manager->m_currentPlugin ||
			Q_stricmp( event->name, eventName ) || Q_stricmp( event->identifier, identifier ) )
			continue;
		luaL_unref( state, LUA_REGISTRYINDEX, event->functionRef );
		delete event;
		manager->m_events.Remove( i );
	}

	lua_pushvalue( state, 3 );
	int functionRef = luaL_ref( state, LUA_REGISTRYINDEX );
	LuaEvent *event = new LuaEvent;
	Q_strncpy( event->name, eventName, sizeof( event->name ) );
	Q_strncpy( event->identifier, identifier, sizeof( event->identifier ) );
	event->plugin = manager->m_currentPlugin;
	event->functionRef = functionRef;
	manager->m_events.AddToTail( event );
	return 0;
}

int CLuaPluginManager::LuaHookRemove( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( !manager || !manager->m_currentPlugin )
		return luaL_error( state, "hook.Remove may only be called by a plugin" );

	const char *hookName = luaL_checkstring( state, 1 );
	const char *identifier = luaL_checkstring( state, 2 );
	char eventName[64];
	NormalizeLuaHookName( hookName, eventName, sizeof( eventName ) );
	bool removed = false;
	for ( int i = manager->m_events.Count() - 1; i >= 0; --i )
	{
		LuaEvent *event = manager->m_events[i];
		if ( event->plugin != manager->m_currentPlugin ||
			Q_stricmp( event->name, eventName ) || Q_stricmp( event->identifier, identifier ) )
			continue;
		luaL_unref( state, LUA_REGISTRYINDEX, event->functionRef );
		delete event;
		manager->m_events.Remove( i );
		removed = true;
	}
	lua_pushboolean( state, removed ? 1 : 0 );
	return 1;
}

int CLuaPluginManager::LuaHookGetTable( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( !manager ) return 0;
	lua_newtable( state );
	for ( int i = 0; i < manager->m_events.Count(); ++i )
	{
		LuaEvent *event = manager->m_events[i];
		if ( !event->identifier[0] )
			continue;
		lua_getfield( state, -1, event->name );
		if ( !lua_istable( state, -1 ) )
		{
			lua_pop( state, 1 );
			lua_newtable( state );
			lua_pushvalue( state, -1 );
			lua_setfield( state, -3, event->name );
		}
		lua_rawgeti( state, LUA_REGISTRYINDEX, event->functionRef );
		lua_setfield( state, -2, event->identifier );
		lua_pop( state, 1 );
	}
	return 1;
}

int CLuaPluginManager::LuaHookList( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( !manager ) return 0;
	lua_newtable( state );
	int resultIndex = 1;
	for ( int i = 0; i < manager->m_events.Count(); ++i )
	{
		LuaEvent *event = manager->m_events[i];
		if ( !event->identifier[0] )
			continue;
		char name[160];
		Q_snprintf( name, sizeof( name ), "%s:%s", event->name, event->identifier );
		lua_pushstring( state, name );
		lua_rawseti( state, -2, resultIndex++ );
	}
	return 1;
}

int CLuaPluginManager::LuaHookCall( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( !manager ) return 0;
	const char *hookName = luaL_checkstring( state, 1 );
	if ( lua_gettop( state ) < 2 )
		return luaL_error( state, "hook.Call requires an event and gamemode value" );
	char eventName[64];
	NormalizeLuaHookName( hookName, eventName, sizeof( eventName ) );
	int top = lua_gettop( state );
	int argumentCount = top - 2;
	for ( int i = 0; i < manager->m_events.Count(); ++i )
	{
		LuaEvent *event = manager->m_events[i];
		if ( Q_stricmp( event->name, eventName ) )
			continue;
		if ( !manager->BeginScriptCallback() )
			break;
		int functionRef = event->functionRef;
		LuaPlugin *plugin = event->plugin;
		lua_rawgeti( state, LUA_REGISTRYINDEX, functionRef );
		for ( int argument = 3; argument <= top; ++argument )
			lua_pushvalue( state, argument );
		LuaPlugin *previousPlugin = manager->m_currentPlugin;
		manager->m_currentPlugin = plugin;
		int result = lua_pcall( state, argumentCount, 1, 0 );
		manager->m_currentPlugin = previousPlugin;
		if ( result != 0 )
		{
			LuaReportError( state, eventName );
			continue;
		}
		if ( !lua_isnil( state, -1 ) )
			return 1;
		lua_pop( state, 1 );
	}
	return 0;
}

int CLuaPluginManager::LuaRegisterCustomWeapon( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( !manager || !manager->m_currentPlugin )
		return luaL_error( state, "weapons.Register may only be called by a plugin" );
	const char *name = luaL_checkstring( state, 1 );
	luaL_checktype( state, 2, LUA_TTABLE );
	if ( !manager->RegisterCustomDefinition( "weapon", name, state, 2 ) )
		return luaL_error( state, "unable to register weapon '%s'", name );
	return 0;
}

int CLuaPluginManager::LuaRegisterCustomNPC( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( !manager || !manager->m_currentPlugin )
		return luaL_error( state, "npcs.Register may only be called by a plugin" );
	const char *name = luaL_checkstring( state, 1 );
	luaL_checktype( state, 2, LUA_TTABLE );
	if ( !manager->RegisterCustomDefinition( "npc", name, state, 2 ) )
		return luaL_error( state, "unable to register NPC '%s'", name );
	return 0;
}

int CLuaPluginManager::LuaRegisterCommand( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( !manager || !manager->m_currentPlugin )
		return luaL_error( state, "plugin.register_command may only be called while loading a plugin" );

	const char *name = luaL_checkstring( state, 1 );
	luaL_checktype( state, 2, LUA_TFUNCTION );
	const char *help = luaL_optstring( state, 3, "Lua plugin command" );

	lua_pushvalue( state, 2 );
	int functionRef = luaL_ref( state, LUA_REGISTRYINDEX );
	LuaCommand *luaCommand = new LuaCommand;
	luaCommand->plugin = manager->m_currentPlugin;
	luaCommand->functionRef = functionRef;
	Q_strncpy( luaCommand->name, name, sizeof( luaCommand->name ) );
	Q_strncpy( luaCommand->help, help, sizeof( luaCommand->help ) );
	LuaCommandCallback *callback = new LuaCommandCallback( manager, manager->m_currentPlugin, functionRef );
	luaCommand->callback = callback;
	luaCommand->command = new ConCommand( luaCommand->name, callback, luaCommand->help );
	manager->m_commands.AddToTail( luaCommand );
	return 0;
}

int CLuaPluginManager::LuaTimerAfter( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( !manager || !manager->m_currentPlugin )
		return luaL_error( state, "plugin.timer.after may only be called by a plugin" );

	float delay = (float)luaL_checknumber( state, 1 );
	if ( delay < 0.0f )
		return luaL_error( state, "timer delay cannot be negative" );
	luaL_checktype( state, 2, LUA_TFUNCTION );

	lua_pushvalue( state, 2 );
	int functionRef = luaL_ref( state, LUA_REGISTRYINDEX );
	LuaTimer *timer = new LuaTimer;
	timer->id = manager->m_nextTimerId++;
	timer->nextTime = manager->m_currentTime + delay;
	timer->interval = delay;
	timer->repeat = false;
	timer->cancelled = false;
	timer->executing = false;
	timer->name[0] = '\0';
	timer->repetitions = 0;
	timer->plugin = manager->m_currentPlugin;
	timer->functionRef = functionRef;
	manager->m_timers.AddToTail( timer );
	lua_pushinteger( state, timer->id );
	return 1;
}

int CLuaPluginManager::LuaTimerEvery( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( !manager || !manager->m_currentPlugin )
		return luaL_error( state, "plugin.timer.every may only be called by a plugin" );

	float interval = (float)luaL_checknumber( state, 1 );
	if ( interval <= 0.0f )
		return luaL_error( state, "timer interval must be greater than zero" );
	luaL_checktype( state, 2, LUA_TFUNCTION );

	lua_pushvalue( state, 2 );
	int functionRef = luaL_ref( state, LUA_REGISTRYINDEX );
	LuaTimer *timer = new LuaTimer;
	timer->id = manager->m_nextTimerId++;
	timer->nextTime = manager->m_currentTime + interval;
	timer->interval = interval;
	timer->repeat = true;
	timer->cancelled = false;
	timer->executing = false;
	timer->name[0] = '\0';
	timer->repetitions = 0;
	timer->plugin = manager->m_currentPlugin;
	timer->functionRef = functionRef;
	manager->m_timers.AddToTail( timer );
	lua_pushinteger( state, timer->id );
	return 1;
}

int CLuaPluginManager::LuaTimerCancel( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( !manager || !manager->m_currentPlugin )
		return luaL_error( state, "plugin.timer.cancel may only be called by a plugin" );

	int timerId = (int)luaL_checkinteger( state, 1 );
	for ( int i = 0; i < manager->m_timers.Count(); ++i )
	{
		LuaTimer *timer = manager->m_timers[i];
		if ( timer->id != timerId )
			continue;
		if ( timer->plugin != manager->m_currentPlugin )
			return 0;

		timer->cancelled = true;
		if ( !timer->executing )
		{
			luaL_unref( state, LUA_REGISTRYINDEX, timer->functionRef );
			delete timer;
			manager->m_timers.Remove( i );
		}
		lua_pushboolean( state, 1 );
		return 1;
	}
	lua_pushboolean( state, 0 );
	return 1;
}

int CLuaPluginManager::LuaTimerCreate( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( !manager || !manager->m_currentPlugin )
		return luaL_error( state, "timer.Create may only be called by a plugin" );

	const char *name = luaL_checkstring( state, 1 );
	float interval = (float)luaL_checknumber( state, 2 );
	int repetitions = (int)luaL_checkinteger( state, 3 );
	if ( !name[0] || interval <= 0.0f || repetitions < 0 )
		return luaL_error( state, "timer.Create received invalid arguments" );
	luaL_checktype( state, 4, LUA_TFUNCTION );

	for ( int i = manager->m_timers.Count() - 1; i >= 0; --i )
	{
		LuaTimer *oldTimer = manager->m_timers[i];
		if ( oldTimer->plugin != manager->m_currentPlugin || Q_stricmp( oldTimer->name, name ) )
			continue;
		oldTimer->cancelled = true;
		if ( !oldTimer->executing )
		{
			luaL_unref( state, LUA_REGISTRYINDEX, oldTimer->functionRef );
			delete oldTimer;
			manager->m_timers.Remove( i );
		}
	}

	lua_pushvalue( state, 4 );
	int functionRef = luaL_ref( state, LUA_REGISTRYINDEX );
	LuaTimer *timer = new LuaTimer;
	timer->id = manager->m_nextTimerId++;
	timer->nextTime = manager->m_currentTime + interval;
	timer->interval = interval;
	timer->repeat = repetitions != 1;
	timer->cancelled = false;
	timer->executing = false;
	Q_strncpy( timer->name, name, sizeof( timer->name ) );
	timer->repetitions = repetitions;
	timer->plugin = manager->m_currentPlugin;
	timer->functionRef = functionRef;
	manager->m_timers.AddToTail( timer );
	return 0;
}

int CLuaPluginManager::LuaTimerRemove( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( !manager || !manager->m_currentPlugin )
		return luaL_error( state, "timer.Remove may only be called by a plugin" );

	const char *name = luaL_checkstring( state, 1 );
	bool removed = false;
	for ( int i = manager->m_timers.Count() - 1; i >= 0; --i )
	{
		LuaTimer *timer = manager->m_timers[i];
		if ( timer->plugin != manager->m_currentPlugin || Q_stricmp( timer->name, name ) )
			continue;
		timer->cancelled = true;
		removed = true;
		if ( !timer->executing )
		{
			luaL_unref( state, LUA_REGISTRYINDEX, timer->functionRef );
			delete timer;
			manager->m_timers.Remove( i );
		}
	}
	lua_pushboolean( state, removed ? 1 : 0 );
	return 1;
}

int CLuaPluginManager::LuaTimerSimple( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( !manager || !manager->m_currentPlugin )
		return luaL_error( state, "timer.Simple may only be called by a plugin" );

	float delay = (float)luaL_checknumber( state, 1 );
	if ( delay < 0.0f )
		return luaL_error( state, "timer.Simple delay cannot be negative" );
	luaL_checktype( state, 2, LUA_TFUNCTION );
	lua_pushvalue( state, 2 );
	int functionRef = luaL_ref( state, LUA_REGISTRYINDEX );
	LuaTimer *timer = new LuaTimer;
	timer->id = manager->m_nextTimerId++;
	timer->nextTime = manager->m_currentTime + delay;
	timer->interval = delay;
	timer->repeat = false;
	timer->cancelled = false;
	timer->executing = false;
	timer->name[0] = '\0';
	timer->repetitions = 1;
	timer->plugin = manager->m_currentPlugin;
	timer->functionRef = functionRef;
	manager->m_timers.AddToTail( timer );
	return 0;
}

int CLuaPluginManager::LuaTime( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	lua_pushnumber( state, manager ? manager->m_currentTime : 0.0 );
	return 1;
}

int CLuaPluginManager::LuaConVarExists( lua_State *state )
{
	const char *name = luaL_checkstring( state, 1 );
	ConVarRef var( name, true );
	lua_pushboolean( state, var.IsValid() ? 1 : 0 );
	return 1;
}

int CLuaPluginManager::LuaConVarGet( lua_State *state )
{
	const char *name = luaL_checkstring( state, 1 );
	ConVarRef var( name, true );
	if ( !var.IsValid() )
		return 0;
	lua_pushstring( state, var.GetString() );
	return 1;
}

int CLuaPluginManager::LuaConVarGetInt( lua_State *state )
{
	const char *name = luaL_checkstring( state, 1 );
	ConVarRef var( name, true );
	if ( !var.IsValid() )
		return 0;
	lua_pushinteger( state, var.GetInt() );
	return 1;
}

int CLuaPluginManager::LuaConVarGetFloat( lua_State *state )
{
	const char *name = luaL_checkstring( state, 1 );
	ConVarRef var( name, true );
	if ( !var.IsValid() )
		return 0;
	lua_pushnumber( state, var.GetFloat() );
	return 1;
}

int CLuaPluginManager::LuaConVarSet( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( manager && !manager->HasPermission( "convar.write" ) )
		return luaL_error( state, "plugin permission denied: convar.write" );
	const char *name = luaL_checkstring( state, 1 );
	const char *value = luaL_checkstring( state, 2 );
	ConVarRef var( name, true );
	if ( !var.IsValid() )
	{
		lua_pushboolean( state, 0 );
		return 1;
	}
	var.SetValue( value );
	lua_pushboolean( state, 1 );
	return 1;
}

static bool LuaIsSafePath( const char *path )
{
	return path && path[0] && path[0] != '/' && path[0] != '\\' &&
		!strstr( path, ".." ) && !strchr( path, '\\' ) && !strchr( path, ':' );
}

int CLuaPluginManager::LuaFileExists( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	const char *path = luaL_checkstring( state, 1 );
	lua_pushboolean( state, manager && manager->m_fileSystem && LuaIsSafePath( path ) &&
		manager->m_fileSystem->FileExists( path, manager->m_pathID ) );
	return 1;
}

int CLuaPluginManager::LuaFileRead( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	const char *path = luaL_checkstring( state, 1 );
	if ( !manager || !manager->m_fileSystem || !LuaIsSafePath( path ) )
		return 0;

	CUtlBuffer buffer;
	if ( !manager->m_fileSystem->ReadFile( path, manager->m_pathID, buffer ) )
		return 0;
	lua_pushlstring( state, (const char *)buffer.Base(), buffer.TellPut() );
	return 1;
}

int CLuaPluginManager::LuaFileWrite( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	if ( manager && !manager->HasPermission( "file.write" ) )
		return luaL_error( state, "plugin permission denied: file.write" );
	const char *path = luaL_checkstring( state, 1 );
	size_t length = 0;
	const char *contents = luaL_checklstring( state, 2, &length );
	const char dataPrefix[] = "scripts/plugins/data/";
	if ( !manager || !manager->m_fileSystem || !LuaIsSafePath( path ) ||
		Q_strncmp( path, dataPrefix, sizeof( dataPrefix ) - 1 ) )
		return 0;

	char directory[256];
	Q_strncpy( directory, path, sizeof( directory ) );
	char *slash = strrchr( directory, '/' );
	if ( slash )
	{
		*slash = '\0';
		manager->m_fileSystem->CreateDirHierarchy( directory, manager->m_pathID );
	}

	CUtlBuffer buffer( 0, 0, CUtlBuffer::TEXT_BUFFER );
	buffer.Put( contents, (int)length );
	lua_pushboolean( state, manager->m_fileSystem->WriteFile( path, manager->m_pathID, buffer ) ? 1 : 0 );
	return 1;
}

static bool LuaReadTriple( lua_State *state, int stackIndex, float value[3] )
{
	if ( !lua_istable( state, stackIndex ) )
		return false;
	for ( int i = 0; i < 3; ++i )
	{
		lua_rawgeti( state, stackIndex, i + 1 );
		value[i] = (float)luaL_optnumber( state, -1, 0.0 );
		lua_pop( state, 1 );
	}
	const char *names[] = { "x", "y", "z" };
	for ( int i = 0; i < 3; ++i )
	{
		lua_getfield( state, stackIndex, names[i] );
		if ( lua_isnumber( state, -1 ) )
			value[i] = (float)lua_tonumber( state, -1 );
		lua_pop( state, 1 );
	}
	return true;
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
}

int CLuaPluginManager::LuaVectorNew( lua_State *state )
{
	float value[3] = {
		(float)luaL_optnumber( state, 1, 0.0 ),
		(float)luaL_optnumber( state, 2, 0.0 ),
		(float)luaL_optnumber( state, 3, 0.0 )
	};
	LuaPushTriple( state, value );
	return 1;
}

int CLuaPluginManager::LuaVectorAdd( lua_State *state )
{
	float a[3], b[3], result[3];
	if ( !LuaReadTriple( state, 1, a ) || !LuaReadTriple( state, 2, b ) )
		return luaL_error( state, "vector.add expects two vector tables" );
	for ( int i = 0; i < 3; ++i ) result[i] = a[i] + b[i];
	LuaPushTriple( state, result );
	return 1;
}

int CLuaPluginManager::LuaVectorSub( lua_State *state )
{
	float a[3], b[3], result[3];
	if ( !LuaReadTriple( state, 1, a ) || !LuaReadTriple( state, 2, b ) )
		return luaL_error( state, "vector.sub expects two vector tables" );
	for ( int i = 0; i < 3; ++i ) result[i] = a[i] - b[i];
	LuaPushTriple( state, result );
	return 1;
}

int CLuaPluginManager::LuaVectorScale( lua_State *state )
{
	float value[3], result[3];
	if ( !LuaReadTriple( state, 1, value ) )
		return luaL_error( state, "vector.scale expects a vector table" );
	float scale = (float)luaL_checknumber( state, 2 );
	for ( int i = 0; i < 3; ++i ) result[i] = value[i] * scale;
	LuaPushTriple( state, result );
	return 1;
}

int CLuaPluginManager::LuaVectorDot( lua_State *state )
{
	float a[3], b[3];
	if ( !LuaReadTriple( state, 1, a ) || !LuaReadTriple( state, 2, b ) )
		return luaL_error( state, "vector.dot expects two vector tables" );
	lua_pushnumber( state, a[0] * b[0] + a[1] * b[1] + a[2] * b[2] );
	return 1;
}

int CLuaPluginManager::LuaVectorLength( lua_State *state )
{
	float value[3];
	if ( !LuaReadTriple( state, 1, value ) )
		return luaL_error( state, "vector.length expects a vector table" );
	lua_pushnumber( state, (lua_Number)sqrt( value[0] * value[0] + value[1] * value[1] + value[2] * value[2] ) );
	return 1;
}

int CLuaPluginManager::LuaVectorDistance( lua_State *state )
{
	float a[3], b[3];
	if ( !LuaReadTriple( state, 1, a ) || !LuaReadTriple( state, 2, b ) )
		return luaL_error( state, "vector.distance expects two vector tables" );
	float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
	lua_pushnumber( state, (lua_Number)sqrt( x * x + y * y + z * z ) );
	return 1;
}

int CLuaPluginManager::LuaVectorNormalize( lua_State *state )
{
	float value[3];
	if ( !LuaReadTriple( state, 1, value ) )
		return luaL_error( state, "vector.normalize expects a vector table" );
	float length = (float)sqrt( value[0] * value[0] + value[1] * value[1] + value[2] * value[2] );
	if ( length > 0.0f )
		for ( int i = 0; i < 3; ++i ) value[i] /= length;
	LuaPushTriple( state, value );
	return 1;
}
