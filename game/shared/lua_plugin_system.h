//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Game DLL integration for the client/server Lua plugin managers.
//
//=============================================================================

#ifndef LUA_PLUGIN_SYSTEM_H
#define LUA_PLUGIN_SYSTEM_H

#ifdef _WIN32
#pragma once
#endif

class edict_t;
class CBaseEntity;
class CBasePlayer;
class CMoveData;
class CTakeDamageInfo;

void LuaPluginSetupMove( CBasePlayer *player, CMoveData *move );

#ifndef CLIENT_DLL
void LuaServerPluginClientPutInServer( edict_t *entity, const char *playerName );
void LuaServerPluginClientDisconnect( edict_t *entity );
bool LuaServerPluginPlayerSay( CBaseEntity *player, const char *text );
void LuaServerPluginClientNetworkMessage( CBaseEntity *player, const char *name, const char *payload );
bool LuaServerPluginEntityTakeDamage( CBaseEntity *entity, const CTakeDamageInfo &info );
#endif

#endif // LUA_PLUGIN_SYSTEM_H
