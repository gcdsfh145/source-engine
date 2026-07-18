//========= Copyright Valve Corporation, All rights reserved. ============//

#include "LuaPluginManagerDialog.h"

#include "EngineInterface.h"
#include "filesystem.h"
#include "GameUI_Interface.h"

#include <vgui_controls/Button.h>
#include <vgui_controls/ListPanel.h>
#include <KeyValues.h>

#include <string.h>

#include "tier0/memdbgon.h"

using namespace vgui;

static bool IsPluginPermanentlyDisabled( const char *role, const char *name )
{
	char path[256];
	Q_snprintf( path, sizeof( path ), "scripts/plugins/%s/disabled.cfg", role );
	if ( !g_pFullFileSystem )
		return false;
	const char *readPathID = "DEFAULT_WRITE_PATH";
	if ( !g_pFullFileSystem->FileExists( path, readPathID ) )
	{
		readPathID = "MOD";
		if ( !g_pFullFileSystem->FileExists( path, readPathID ) )
			return false;
	}

	CUtlBuffer buffer;
	if ( !g_pFullFileSystem->ReadFile( path, readPathID, buffer ) )
		return false;

	char contents[8192];
	int length = buffer.TellPut();
	if ( length >= (int)sizeof( contents ) )
		length = sizeof( contents ) - 1;
	memcpy( contents, buffer.Base(), length );
	contents[length] = '\0';
	char normalizedName[128];
	Q_strncpy( normalizedName, name, sizeof( normalizedName ) );
	char *extension = V_stristr( normalizedName, ".lua" );
	if ( extension && extension[4] == '\0' )
		*extension = '\0';

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
		char normalizedLine[128];
		Q_strncpy( normalizedLine, line, sizeof( normalizedLine ) );
		extension = V_stristr( normalizedLine, ".lua" );
		if ( extension && extension[4] == '\0' )
			*extension = '\0';
		if ( !Q_stricmp( normalizedLine, normalizedName ) )
			return true;
		line = next;
	}
	return false;
}

CLuaPluginManagerDialog::CLuaPluginManagerDialog( Panel *parent ) : BaseClass( parent, "LuaPluginManagerDialog" )
{
	SetDeleteSelfOnClose( true );
	SetTitle( "Lua Plugin Manager", true );
	SetSize( 680, 440 );
	SetSizeable( false );

	m_pPluginList = new ListPanel( this, "PluginList" );
	m_pPluginList->AddColumnHeader( 0, "Name", "Plugin", 250 );
	m_pPluginList->AddColumnHeader( 1, "Role", "Role", 90 );
	m_pPluginList->AddColumnHeader( 2, "Persistent", "Permanent", 170 );
	m_pPluginList->AddActionSignalTarget( this );

	m_pLoadButton = new Button( this, "LoadButton", "Enable This Session" );
	m_pLoadButton->SetCommand( "EnableSession" );
	m_pUnloadButton = new Button( this, "UnloadButton", "Disable This Session" );
	m_pUnloadButton->SetCommand( "DisableSession" );
	m_pEnableButton = new Button( this, "EnableButton", "Permanent Enable" );
	m_pEnableButton->SetCommand( "PermanentEnable" );
	m_pDisableButton = new Button( this, "DisableButton", "Permanent Disable" );
	m_pDisableButton->SetCommand( "PermanentDisable" );

	m_pPluginList->SetBounds( 12, 35, 656, 315 );
	m_pLoadButton->SetBounds( 12, 365, 150, 28 );
	m_pUnloadButton->SetBounds( 172, 365, 150, 28 );
	m_pEnableButton->SetBounds( 332, 365, 150, 28 );
	m_pDisableButton->SetBounds( 492, 365, 150, 28 );
}

void CLuaPluginManagerDialog::Activate()
{
	RefreshList();
	BaseClass::Activate();
}

void CLuaPluginManagerDialog::RefreshList()
{
	m_pPluginList->DeleteAllItems();
	static const char *roles[] = { "client", "server" };
	for ( int roleIndex = 0; roleIndex < 2; ++roleIndex )
	{
		char wildcard[128];
		Q_snprintf( wildcard, sizeof( wildcard ), "scripts/plugins/%s/*.lua", roles[roleIndex] );
		FileFindHandle_t handle;
		const char *fileName = g_pFullFileSystem ? g_pFullFileSystem->FindFirstEx( wildcard, "MOD", &handle ) : NULL;
		while ( fileName )
		{
			if ( !g_pFullFileSystem->FindIsDirectory( handle ) )
			{
				KeyValues *item = new KeyValues( "LuaPlugin" );
				item->SetString( "name", fileName );
				item->SetString( "role", roles[roleIndex] );
				item->SetString( "persistent", IsPluginPermanentlyDisabled( roles[roleIndex], fileName ) ? "Disabled" : "Enabled" );
				m_pPluginList->AddItem( item, 0, false, false );
			}
			fileName = g_pFullFileSystem->FindNext( handle );
		}
		if ( handle != FILESYSTEM_INVALID_FIND_HANDLE )
			g_pFullFileSystem->FindClose( handle );
	}
	if ( m_pPluginList->GetItemCount() > 0 )
		m_pPluginList->SetSingleSelectedItem( m_pPluginList->GetItemIDFromRow( 0 ) );
}

bool CLuaPluginManagerDialog::GetSelectedPlugin( char *role, int roleSize, char *name, int nameSize )
{
	if ( m_pPluginList->GetSelectedItemsCount() <= 0 )
		return false;
	KeyValues *item = m_pPluginList->GetItem( m_pPluginList->GetSelectedItem( 0 ) );
	if ( !item )
		return false;
	Q_strncpy( role, item->GetString( "role" ), roleSize );
	Q_strncpy( name, item->GetString( "name" ), nameSize );
	return role[0] && name[0];
}

void CLuaPluginManagerDialog::SendPluginCommand( const char *action )
{
	char role[32];
	char name[128];
	if ( !GetSelectedPlugin( role, sizeof( role ), name, sizeof( name ) ) )
		return;

	char command[256];
	Q_snprintf( command, sizeof( command ), "lua_%s_%s \"%s\"\n", role, action, name );
	if ( !Q_stricmp( role, "server" ) )
		engine->ServerCmd( command, true );
	else
		engine->ClientCmd_Unrestricted( command );

	if ( !Q_stricmp( action, "permanent_enable" ) || !Q_stricmp( action, "permanent_disable" ) )
		RefreshList();
}

void CLuaPluginManagerDialog::OnItemSelected()
{
	bool enabled = m_pPluginList->GetSelectedItemsCount() > 0;
	m_pLoadButton->SetEnabled( enabled );
	m_pUnloadButton->SetEnabled( enabled );
	m_pEnableButton->SetEnabled( enabled );
	m_pDisableButton->SetEnabled( enabled );
}

void CLuaPluginManagerDialog::OnCommand( const char *command )
{
	if ( !Q_stricmp( command, "EnableSession" ) )
		SendPluginCommand( "load" );
	else if ( !Q_stricmp( command, "DisableSession" ) )
		SendPluginCommand( "unload" );
	else if ( !Q_stricmp( command, "PermanentEnable" ) )
		SendPluginCommand( "permanent_enable" );
	else if ( !Q_stricmp( command, "PermanentDisable" ) )
		SendPluginCommand( "permanent_disable" );
	else
		BaseClass::OnCommand( command );
}
