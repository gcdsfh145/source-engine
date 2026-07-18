//========= Copyright Valve Corporation, All rights reserved. ============//

#include "cbase.h"
#include "hudelement.h"
#include "iclientmode.h"
#include "lua_hud.h"
#include "vgui_controls/Panel.h"
#include "vgui/ISurface.h"
#include "vgui/ILocalize.h"
#include "vgui_controls/Controls.h"
#include "tier1/utlvector.h"

#include <string.h>

struct LuaHudItem
{
	char id[64];
	char text[512];
	int x;
	int y;
	int wide;
	int tall;
	int size;
	Color color;
	bool rect;
	bool visible;
	vgui::HFont font;
};

class CLuaHudElement : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CLuaHudElement, vgui::Panel );

public:
	CLuaHudElement( const char *name ) : CHudElement( name ), BaseClass( NULL, name )
	{
		s_instance = this;
		m_items.SetCount( 0 );
		SetParent( g_pClientMode ? g_pClientMode->GetViewport() : NULL );
		SetPaintBackgroundEnabled( false );
		SetVisible( true );
		SetMouseInputEnabled( false );
		SetKeyBoardInputEnabled( false );
	}

	~CLuaHudElement()
	{
		if ( s_instance == this )
			s_instance = NULL;
	}

	static CLuaHudElement *Instance()
	{
		if ( !s_instance )
			s_instance = dynamic_cast< CLuaHudElement * >( gHUD.FindElement( "LuaHud" ) );
		return s_instance;
	}

	virtual bool ShouldDraw()
	{
		return m_items.Count() > 0 && CHudElement::ShouldDraw();
	}

	virtual void Paint()
	{
		if ( !vgui::surface() )
			return;

		int screenWide, screenTall;
		vgui::surface()->GetScreenSize( screenWide, screenTall );
		SetBounds( 0, 0, screenWide, screenTall );
		for ( int i = 0; i < m_items.Count(); ++i )
		{
			LuaHudItem &item = m_items[i];
			if ( !item.visible )
				continue;
			if ( item.rect )
			{
				vgui::surface()->DrawSetColor( item.color );
				vgui::surface()->DrawFilledRect( item.x, item.y,
					item.x + item.wide, item.y + item.tall );
				continue;
			}
			if ( !item.font )
			{
				item.font = vgui::surface()->CreateFont();
				vgui::surface()->SetFontGlyphSet( item.font, "Tahoma", item.size,
					500, 0, 0, 0 );
			}
			wchar_t unicode[512];
			unicode[0] = L'\0';
			if ( g_pVGuiLocalize )
				g_pVGuiLocalize->ConvertANSIToUnicode( item.text, unicode, sizeof( unicode ) );
			vgui::surface()->DrawSetTextFont( item.font );
			vgui::surface()->DrawSetTextColor( item.color );
			vgui::surface()->DrawSetTextPos( item.x, item.y );
			vgui::surface()->DrawPrintText( unicode, wcslen( unicode ) );
		}
	}

	LuaHudItem *Find( const char *id )
	{
		if ( !id ) return NULL;
		for ( int i = 0; i < m_items.Count(); ++i )
			if ( !Q_stricmp( m_items[i].id, id ) ) return &m_items[i];
		return NULL;
	}

	LuaHudItem *GetOrCreate( const char *id )
	{
		LuaHudItem *item = Find( id );
		if ( item ) return item;
		if ( !id || !id[0] || Q_strlen( id ) >= sizeof( m_items[0].id ) ) return NULL;
		LuaHudItem newItem;
		memset( &newItem, 0, sizeof( newItem ) );
		Q_strncpy( newItem.id, id, sizeof( newItem.id ) );
		newItem.size = 20;
		newItem.color = Color( 255, 255, 255, 255 );
		newItem.visible = true;
		m_items.AddToTail( newItem );
		return &m_items[m_items.Count() - 1];
	}

	void CreateText( const char *id, int x, int y, const char *text, int size, Color color )
	{
		LuaHudItem *item = GetOrCreate( id );
		if ( !item ) return;
		item->rect = false;
		item->x = x; item->y = y; item->size = MAX( 1, MIN( size, 128 ) );
		item->color = color;
		Q_strncpy( item->text, text ? text : "", sizeof( item->text ) );
		item->font = 0;
	}

	void CreateRect( const char *id, int x, int y, int wide, int tall, Color color )
	{
		LuaHudItem *item = GetOrCreate( id );
		if ( !item ) return;
		item->rect = true;
		item->x = x; item->y = y; item->wide = MAX( 0, wide ); item->tall = MAX( 0, tall );
		item->color = color;
	}

	void Remove( const char *id )
	{
		for ( int i = m_items.Count() - 1; i >= 0; --i )
			if ( !Q_stricmp( m_items[i].id, id ? id : "" ) ) m_items.Remove( i );
	}

	void Clear() { m_items.RemoveAll(); }

	static CLuaHudElement *s_instance;
	CUtlVector< LuaHudItem > m_items;
};

CLuaHudElement *CLuaHudElement::s_instance = NULL;
DECLARE_NAMED_HUDELEMENT( CLuaHudElement, LuaHud );

static Color LuaHudColor( int r, int g, int b, int a )
{
	return Color( MAX( 0, MIN( r, 255 ) ), MAX( 0, MIN( g, 255 ) ),
		MAX( 0, MIN( b, 255 ) ), MAX( 0, MIN( a, 255 ) ) );
}

void LuaHudCreateText( const char *id, int x, int y, const char *text, int size,
	int r, int g, int b, int a )
{
	if ( CLuaHudElement *hud = CLuaHudElement::Instance() )
		hud->CreateText( id, x, y, text, size, LuaHudColor( r, g, b, a ) );
}

void LuaHudCreateRect( const char *id, int x, int y, int wide, int tall,
	int r, int g, int b, int a )
{
	if ( CLuaHudElement *hud = CLuaHudElement::Instance() )
		hud->CreateRect( id, x, y, wide, tall, LuaHudColor( r, g, b, a ) );
}

void LuaHudSetText( const char *id, const char *text )
{
	if ( CLuaHudElement *hud = CLuaHudElement::Instance() )
		if ( LuaHudItem *item = hud->Find( id ) ) Q_strncpy( item->text, text ? text : "", sizeof( item->text ) );
}

void LuaHudSetPosition( const char *id, int x, int y )
{
	if ( CLuaHudElement *hud = CLuaHudElement::Instance() )
		if ( LuaHudItem *item = hud->Find( id ) ) { item->x = x; item->y = y; }
}

void LuaHudSetColor( const char *id, int r, int g, int b, int a )
{
	if ( CLuaHudElement *hud = CLuaHudElement::Instance() )
		if ( LuaHudItem *item = hud->Find( id ) ) item->color = LuaHudColor( r, g, b, a );
}

void LuaHudSetVisible( const char *id, bool visible )
{
	if ( CLuaHudElement *hud = CLuaHudElement::Instance() )
		if ( LuaHudItem *item = hud->Find( id ) ) item->visible = visible;
}

void LuaHudRemove( const char *id )
{
	if ( CLuaHudElement *hud = CLuaHudElement::Instance() ) hud->Remove( id );
}

void LuaHudClear()
{
	if ( CLuaHudElement *hud = CLuaHudElement::Instance() ) hud->Clear();
}
