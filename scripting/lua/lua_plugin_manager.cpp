//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Embedded Lua plugin runtime shared by the client and server DLLs.
//
//=============================================================================

#include "lua/lua_plugin_manager.h"

#include <stdio.h>
#include <string.h>

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include "filesystem.h"
#include "tier1/convar.h"
#include "tier1/strtools.h"
#include "tier1/utlbuffer.h"
#include "tier0/dbg.h"

// memdbgon must be the last include file in a .cpp file.
#include "tier0/memdbgon.h"

static const char s_LuaManagerRegistryKey[] = "SourceEngine.LuaPluginManager";

struct CLuaPluginManager::LuaPlugin
{
	char name[128];
	int environmentRef;
};

struct CLuaPluginManager::LuaCommand
{
	ConCommand *command;
	LuaCommandCallback *callback;
	LuaPlugin *plugin;
	int functionRef;
};

struct CLuaPluginManager::LuaEvent
{
	char name[64];
	LuaPlugin *plugin;
	int functionRef;
};

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

CLuaPluginManager::CLuaPluginManager() :
	m_state( NULL ),
	m_fileSystem( NULL ),
	m_commandExecutor( NULL ),
	m_commandContext( NULL ),
	m_currentPlugin( NULL )
{
	m_pathID[0] = '\0';
	m_pluginRoot[0] = '\0';
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

	static const luaL_Reg pluginFunctions[] =
	{
		{ "log",              &CLuaPluginManager::LuaLog },
		{ "role",             &CLuaPluginManager::LuaRole },
		{ "execute",          &CLuaPluginManager::LuaExecute },
		{ "on",               &CLuaPluginManager::LuaOn },
		{ "register_command", &CLuaPluginManager::LuaRegisterCommand },
		{ NULL, NULL }
	};

	lua_newtable( state );
	luaL_register( state, NULL, pluginFunctions );
	lua_setglobal( state, "plugin" );

	m_state = state;
	m_fileSystem = fileSystem;
	Q_strncpy( m_pathID, pathID && pathID[0] ? pathID : "MOD", sizeof( m_pathID ) );
	Q_strncpy( m_pluginRoot, pluginRoot, sizeof( m_pluginRoot ) );
	Q_strncpy( m_role, role && role[0] ? role : "unknown", sizeof( m_role ) );
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
	Q_strncpy( name, pluginName, sizeof( name ) );
	char *extension = strstr( name, ".lua" );
	if ( extension && extension[4] == '\0' )
		*extension = '\0';

	for ( int i = 0; i < m_plugins.Count(); ++i )
	{
		if ( !Q_stricmp( m_plugins[i]->name, name ) )
			return m_plugins[i];
	}
	return NULL;
}

bool CLuaPluginManager::LoadAll()
{
	if ( !m_state || !m_fileSystem )
		return false;

	char wildcard[sizeof( m_pluginRoot ) + 8];
	Q_snprintf( wildcard, sizeof( wildcard ), "%s/*.lua", m_pluginRoot );

	FileFindHandle_t handle;
	const char *fileName = m_fileSystem->FindFirstEx( wildcard, m_pathID, &handle );
	while ( fileName )
	{
		char currentFile[128];
		Q_strncpy( currentFile, fileName, sizeof( currentFile ) );
		if ( !m_fileSystem->FindIsDirectory( handle ) )
			LoadPluginFile( currentFile );
		fileName = m_fileSystem->FindNext( handle );
	}
	if ( handle != FILESYSTEM_INVALID_FIND_HANDLE )
		m_fileSystem->FindClose( handle );

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
	Q_strncpy( plugin->name, scriptName, sizeof( plugin->name ) );
	char *extension = V_stristr( plugin->name, ".lua" );
	if ( extension )
		*extension = '\0';
	plugin->environmentRef = environmentRef;
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
	CallPluginFunction( plugin, "OnPluginLoad" );
	m_currentPlugin = NULL;

	Msg( "[Lua] Loaded %s plugin '%s'\n", m_role, plugin->name );
	return true;
}

void CLuaPluginManager::RemovePluginBindings( LuaPlugin *plugin )
{
	lua_State *state = (lua_State *)m_state;
	for ( int i = m_commands.Count() - 1; i >= 0; --i )
	{
		if ( m_commands[i]->plugin != plugin )
			continue;
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

void CLuaPluginManager::PrintPlugins() const
{
	Msg( "Lua %s plugins:\n", m_role );
	for ( int i = 0; i < m_plugins.Count(); ++i )
		Msg( "  %d: %s\n", i, m_plugins[i]->name );
}

void CLuaPluginManager::CallPluginFunction( LuaPlugin *plugin, const char *functionName, int argumentCount )
{
	if ( !m_state || !plugin )
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
	if ( lua_pcall( state, argumentCount, 0, 0 ) != 0 )
		LuaReportError( state, functionName );
}

void CLuaPluginManager::EmitEvent( const char *eventName, int intArg, const char *stringArg, bool boolArg )
{
	if ( !m_state )
		return;

	lua_State *state = (lua_State *)m_state;
	for ( int i = 0; i < m_events.Count(); ++i )
	{
		LuaEvent *event = m_events[i];
		if ( Q_stricmp( event->name, eventName ) )
			continue;

		lua_rawgeti( state, LUA_REGISTRYINDEX, event->functionRef );
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
			lua_pushinteger( state, intArg );
			lua_pushstring( state, stringArg ? stringArg : "" );
			argumentCount = 2;
		}
		else if ( !Q_stricmp( eventName, "client_disconnect" ) )
		{
			lua_pushinteger( state, intArg );
			argumentCount = 1;
		}

		if ( lua_pcall( state, argumentCount, 0, 0 ) != 0 )
			LuaReportError( state, eventName );
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

void CLuaPluginManager::Frame( bool simulating )
{
	for ( int i = 0; i < m_plugins.Count(); ++i )
		CallPluginFunction( m_plugins[i], "OnGameFrame" );
	EmitEvent( "game_frame", 0, NULL, simulating );
}

void CLuaPluginManager::ClientPutInServer( int entIndex, const char *playerName )
{
	EmitEvent( "client_put_in_server", entIndex, playerName, false );
}

void CLuaPluginManager::ClientDisconnect( int entIndex )
{
	EmitEvent( "client_disconnect", entIndex );
}

void CLuaPluginManager::SetCommandExecutor( LuaCommandExecutor_t executor, void *context )
{
	m_commandExecutor = executor;
	m_commandContext = context;
}

void CLuaPluginManager::InvokeCommand( LuaPlugin *plugin, int functionRef, const CCommand &command )
{
	if ( !m_state || !plugin )
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
	if ( lua_pcall( state, 2, 0, 0 ) != 0 )
		LuaReportError( state, command.Arg( 0 ) );
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

int CLuaPluginManager::LuaExecute( lua_State *state )
{
	CLuaPluginManager *manager = FromLuaState( state );
	const char *command = luaL_checkstring( state, 1 );
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
	event->plugin = manager->m_currentPlugin;
	event->functionRef = functionRef;
	manager->m_events.AddToTail( event );
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
	LuaCommandCallback *callback = new LuaCommandCallback( manager, manager->m_currentPlugin, functionRef );
	LuaCommand *luaCommand = new LuaCommand;
	luaCommand->plugin = manager->m_currentPlugin;
	luaCommand->functionRef = functionRef;
	luaCommand->callback = callback;
	luaCommand->command = new ConCommand( name, callback, help );
	manager->m_commands.AddToTail( luaCommand );
	return 0;
}
