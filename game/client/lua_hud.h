//========= Copyright Valve Corporation, All rights reserved. ============//

#ifndef LUA_HUD_H
#define LUA_HUD_H

void LuaHudCreateText( const char *id, int x, int y, const char *text,
	int size, int r, int g, int b, int a );
void LuaHudCreateRect( const char *id, int x, int y, int wide, int tall,
	int r, int g, int b, int a );
void LuaHudSetText( const char *id, const char *text );
void LuaHudSetPosition( const char *id, int x, int y );
void LuaHudSetColor( const char *id, int r, int g, int b, int a );
void LuaHudSetVisible( const char *id, bool visible );
void LuaHudRemove( const char *id );
void LuaHudClear();

#endif // LUA_HUD_H
