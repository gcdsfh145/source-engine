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

#ifdef CLIENT_DLL
#include "cdll_int.h"
extern IVEngineClient *engine;
#else
#include "eiface.h"
extern IVEngineServer *engine;
#endif

extern IFileSystem *g_pFullFileSystem;

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
		if ( !m_manager.Init( g_pFullFileSystem, "MOD", root, role ) )
			return false;

		m_manager.SetCommandExecutor( ExecuteLuaCommand, NULL );
		m_manager.LoadAll();
		return true;
	}

	virtual void Shutdown()
	{
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
		m_manager.Frame( true );
	}
#else
	virtual void FrameUpdatePostEntityThink()
	{
		m_manager.Frame( true );
	}
#endif

	void ReloadAll() { m_manager.ReloadAll(); }
	bool Load( const char *name ) { return m_manager.LoadPlugin( name ); }
	bool Unload( const char *name ) { return m_manager.UnloadPlugin( name ); }
	bool Reload( const char *name ) { return m_manager.ReloadPlugin( name ); }
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

void LuaServerPluginClientPutInServer( edict_t *entity, const char *playerName )
{
	g_LuaGamePluginSystem.ClientPutInServer( entity, playerName );
}

void LuaServerPluginClientDisconnect( edict_t *entity )
{
	g_LuaGamePluginSystem.ClientDisconnect( entity );
}
#endif
