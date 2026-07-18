//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Embedded Lua plugin runtime shared by the client and server DLLs.
//
//=============================================================================

#ifndef LUA_PLUGIN_MANAGER_H
#define LUA_PLUGIN_MANAGER_H

#ifdef _WIN32
#pragma once
#endif

#include "tier1/utlvector.h"

class CCommand;
class IFileSystem;

typedef void (*LuaCommandExecutor_t)( const char *command, void *context );

// One manager is created in the client DLL and another in the server DLL.
// They deliberately own separate Lua states and never share script globals.
class CLuaPluginManager
{
public:
	CLuaPluginManager();
	~CLuaPluginManager();

	bool Init( IFileSystem *fileSystem, const char *pathID, const char *pluginRoot, const char *role );
	void Shutdown();

	bool LoadAll();
	bool LoadPlugin( const char *fileName );
	bool UnloadPlugin( const char *pluginName );
	bool ReloadPlugin( const char *pluginName );
	void ReloadAll();
	void PrintPlugins() const;

	void LevelInit( const char *mapName );
	void LevelShutdown();
	void Frame( bool simulating );
	void ClientPutInServer( int entIndex, const char *playerName );
	void ClientDisconnect( int entIndex );

	void SetCommandExecutor( LuaCommandExecutor_t executor, void *context );
	const char *GetRole() const { return m_role; }
	bool IsInitialized() const { return m_state != NULL; }

private:
	struct LuaPlugin;
	struct LuaCommand;
	struct LuaEvent;
	class LuaCommandCallback;

	LuaPlugin *FindPlugin( const char *pluginName ) const;
	bool LoadPluginFile( const char *fileName );
	void RemovePluginBindings( LuaPlugin *plugin );
	void CallPluginFunction( LuaPlugin *plugin, const char *functionName, int argumentCount = 0 );
	void EmitEvent( const char *eventName, int intArg = 0, const char *stringArg = NULL, bool boolArg = false );
	void InvokeCommand( LuaPlugin *plugin, int functionRef, const CCommand &command );
	void SetCurrentPlugin( LuaPlugin *plugin ) { m_currentPlugin = plugin; }

	static int LuaLog( struct lua_State *state );
	static int LuaRole( struct lua_State *state );
	static int LuaExecute( struct lua_State *state );
	static int LuaOn( struct lua_State *state );
	static int LuaRegisterCommand( struct lua_State *state );
	static CLuaPluginManager *FromLuaState( struct lua_State *state );

	void *m_state;
	IFileSystem *m_fileSystem;
	LuaCommandExecutor_t m_commandExecutor;
	void *m_commandContext;
	LuaPlugin *m_currentPlugin;
	CUtlVector< LuaPlugin * > m_plugins;
	CUtlVector< LuaCommand * > m_commands;
	CUtlVector< LuaEvent * > m_events;
	char m_pathID[16];
	char m_pluginRoot[128];
	char m_role[16];
};

#endif // LUA_PLUGIN_MANAGER_H
