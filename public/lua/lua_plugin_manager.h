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
class CBaseEntity;
class CTakeDamageInfo;
struct lua_State;

typedef void (*LuaCommandExecutor_t)( const char *command, void *context );
typedef void (*LuaBindingInstaller_t)( lua_State *state, void *context );

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
	bool SetPersistentPluginEnabled( const char *pluginName, bool enabled );
	bool IsPluginLoaded( const char *pluginName ) const;
	void ReloadAll();
	void PrintPlugins() const;

	void LevelInit( const char *mapName );
	void LevelShutdown();
	void Frame( bool simulating, float currentTime = 0.0f );
	void ClientPutInServer( int entIndex, const char *playerName );
	void ClientDisconnect( int entIndex );
	bool PlayerSay( int entIndex, const char *text );
	void SetupMove( int entIndex, int &buttons, float &forwardMove, float &sideMove, float &upMove );
	void NetworkMessage( const char *name, const char *payload, int senderIndex = -1 );
	bool RegisterNetworkReceiver( struct lua_State *state, const char *name, int functionIndex );
	void NetworkStart( const char *name );
	bool NetworkWriteString( const char *value );
	bool NetworkWriteInt( int value );
	bool NetworkWriteFloat( float value );
	bool NetworkWriteBool( bool value );
	bool NetworkWriteVector( float x, float y, float z );
	bool NetworkWriteEntity( int entityIndex );
	const char *NetworkWriteName() const { return m_networkWriteName; }
	const char *NetworkWritePayload() const { return m_networkWritePayload; }
	bool NetworkReadInt( int &value );
	bool NetworkReadFloat( float &value );
	bool NetworkReadBool( bool &value );
	bool NetworkReadString( char *value, int valueSize );
	bool NetworkReadVector( float &x, float &y, float &z );
	bool NetworkReadEntity( int &entityIndex );
	void GameEvent( const char *eventName, int value1 = 0, int value2 = 0, int value3 = 0,
		const char *string1 = NULL, const char *string2 = NULL );
	bool HasCustomDefinition( const char *kind, const char *name ) const;
	bool GetCustomDefinitionString( const char *kind, const char *name, const char *field,
		char *value, int valueSize ) const;
	int GetCustomDefinitionInt( const char *kind, const char *name, const char *field,
		int defaultValue ) const;
	float GetCustomDefinitionFloat( const char *kind, const char *name, const char *field,
		float defaultValue ) const;
	bool CallCustomEntityHook( const char *kind, const char *name, const char *callback,
		int entityIndex, float damage = -1.0f, int attackerIndex = -1, bool *boolResult = NULL );
	bool EntityTakeDamage( CBaseEntity *entity, const CTakeDamageInfo &info );
	bool HasPermission( const char *permission ) const;
	void OnLuaInstruction();
	bool LuaInstructionLimitExceeded() const;

	void SetCommandExecutor( LuaCommandExecutor_t executor, void *context );
	void SetBindingInstaller( LuaBindingInstaller_t installer, void *context );
	const char *GetRole() const { return m_role; }
	bool IsInitialized() const { return m_state != NULL; }

private:
	struct LuaPlugin;
	struct LuaCommand;
	struct LuaEvent;
	struct LuaTimer;
	struct LuaDisabledPlugin;
	struct LuaCustomDefinition;
	class LuaCommandCallback;

	LuaPlugin *FindPlugin( const char *pluginName ) const;
	bool IsPluginDisabled( const char *pluginName ) const;
	void LoadDisabledPlugins();
	bool SaveDisabledPlugins() const;
	bool LoadPluginFile( const char *fileName );
	void RemovePluginBindings( LuaPlugin *plugin );
	void RemovePluginTimers( LuaPlugin *plugin );
	bool RegisterCustomDefinition( const char *kind, const char *name, struct lua_State *state, int tableIndex );
	bool UnregisterCustomDefinition( const char *kind, const char *name );
	bool LoadPluginConfig( LuaPlugin *plugin );
	bool SavePluginConfig( LuaPlugin *plugin ) const;
	LuaCustomDefinition *FindCustomDefinition( const char *kind, const char *name ) const;
	void ProcessTimers();
	void CallPluginFunction( LuaPlugin *plugin, const char *functionName, int argumentCount = 0 );
	void EmitEvent( const char *eventName, int intArg = 0, const char *stringArg = NULL, bool boolArg = false );
	void InvokeCommand( LuaPlugin *plugin, int functionRef, const CCommand &command );
	bool PluginDependenciesLoaded( LuaPlugin *plugin ) const;
	void RecordPluginError( LuaPlugin *plugin, const char *where );
	bool NetworkAppendToken( const char *token );
	bool NetworkReadToken( char type, char *value, int valueSize );
	bool BeginScriptCallback();
	void SetCurrentPlugin( LuaPlugin *plugin ) { m_currentPlugin = plugin; }

	static int LuaLog( struct lua_State *state );
	static int LuaRole( struct lua_State *state );
	static int LuaManifest( struct lua_State *state );
	static int LuaHasPermission( struct lua_State *state );
	static int LuaRequirePermission( struct lua_State *state );
	static int LuaConfigGet( struct lua_State *state );
	static int LuaConfigSet( struct lua_State *state );
	static int LuaConfigSave( struct lua_State *state );
	static int LuaPluginInfo( struct lua_State *state );
	static int LuaReloadSelf( struct lua_State *state );
	static int LuaExecute( struct lua_State *state );
	static int LuaOn( struct lua_State *state );
	static int LuaHookAdd( struct lua_State *state );
	static int LuaHookRemove( struct lua_State *state );
	static int LuaHookGetTable( struct lua_State *state );
	static int LuaHookList( struct lua_State *state );
	static int LuaHookCall( struct lua_State *state );
	static int LuaDebugStats( struct lua_State *state );
	static int LuaRegisterCustomWeapon( struct lua_State *state );
	static int LuaRegisterCustomNPC( struct lua_State *state );
	static int LuaUnregisterCustomWeapon( struct lua_State *state );
	static int LuaUnregisterCustomNPC( struct lua_State *state );
	static int LuaRegisterCommand( struct lua_State *state );
	static int LuaTimerAfter( struct lua_State *state );
	static int LuaTimerEvery( struct lua_State *state );
	static int LuaTimerCancel( struct lua_State *state );
	static int LuaTimerCreate( struct lua_State *state );
	static int LuaTimerRemove( struct lua_State *state );
	static int LuaTimerSimple( struct lua_State *state );
	static int LuaTime( struct lua_State *state );
	static int LuaConVarExists( struct lua_State *state );
	static int LuaConVarGet( struct lua_State *state );
	static int LuaConVarGetInt( struct lua_State *state );
	static int LuaConVarGetFloat( struct lua_State *state );
	static int LuaConVarSet( struct lua_State *state );
	static int LuaFileExists( struct lua_State *state );
	static int LuaFileRead( struct lua_State *state );
	static int LuaFileList( struct lua_State *state );
	static int LuaFileWrite( struct lua_State *state );
	static int LuaVectorNew( struct lua_State *state );
	static int LuaVectorAdd( struct lua_State *state );
	static int LuaVectorSub( struct lua_State *state );
	static int LuaVectorScale( struct lua_State *state );
	static int LuaVectorDot( struct lua_State *state );
	static int LuaVectorLength( struct lua_State *state );
	static int LuaVectorDistance( struct lua_State *state );
	static int LuaVectorNormalize( struct lua_State *state );
	static int LuaVectorMultiply( struct lua_State *state );
	static int LuaVectorDivide( struct lua_State *state );
	static int LuaVectorUnm( struct lua_State *state );
	static int LuaVectorToString( struct lua_State *state );
	static CLuaPluginManager *FromLuaState( struct lua_State *state );

	void *m_state;
	IFileSystem *m_fileSystem;
	LuaCommandExecutor_t m_commandExecutor;
	void *m_commandContext;
	LuaPlugin *m_currentPlugin;
	CUtlVector< LuaPlugin * > m_plugins;
	CUtlVector< LuaCommand * > m_commands;
	CUtlVector< LuaEvent * > m_events;
	CUtlVector< LuaTimer * > m_timers;
	CUtlVector< LuaDisabledPlugin > m_disabledPlugins;
	CUtlVector< LuaCustomDefinition * > m_customDefinitions;
	LuaTimer *m_executingTimer;
	LuaBindingInstaller_t m_bindingInstaller;
	void *m_bindingContext;
	float m_currentTime;
	int m_frameCallbackCount;
	bool m_callbackLimitWarning;
	int m_instructionCount;
	int m_nextTimerId;
	char m_pathID[16];
	char m_pluginRoot[128];
	char m_disabledFile[192];
	char m_reloadRequested[128];
	char m_networkWriteName[128];
	char m_networkWritePayload[4000];
	char m_networkReadPayload[4000];
	int m_networkReadOffset;
	char m_role[16];
};

#endif // LUA_PLUGIN_MANAGER_H
