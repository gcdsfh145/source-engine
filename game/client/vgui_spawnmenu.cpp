#include "cbase.h"
#include "vgui_spawnmenu.h"
#include <vgui/IVGui.h>
#include <vgui_controls/Controls.h>
#include "ienginevgui.h"
#include "convar.h"

// memdbgon must be the last include file of a .cpp file
#include "tier0/memdbgon.h"

CSpawnMenu* g_pSpawnMenu = NULL;

CSpawnMenu::CSpawnMenu( vgui::VPANEL parent ) : BaseClass( NULL, "SpawnMenu" )
{
    SetParent( parent );
    SetSize( 300, 400 );
    
    // 居中显示
    int sw, sh;
    vgui::surface()->GetScreenSize( sw, sh );
    SetPos( ( sw - 300 ) / 2, ( sh - 400 ) / 2 );
    
    SetTitle( "刷物品菜单", true );

    // 常用武器和物品
    CreateSpawnButton( "给一把 357", "give weapon_357", 10, 40 );
    CreateSpawnButton( "给个 霰弹枪", "give weapon_shotgun", 10, 80 );
    CreateSpawnButton( "给个 弩", "give weapon_crossbow", 10, 120 );
    CreateSpawnButton( "补满弹药", "givecurrentammo", 10, 160 );
    CreateSpawnButton( "给个 药箱", "give item_healthkit", 10, 200 );
    CreateSpawnButton( "给个 电池", "give item_battery", 10, 240 );
    CreateSpawnButton( "开启作弊 (sv_cheats 1)", "sv_cheats 1", 10, 280 );

    SetVisible( false );
}

CSpawnMenu::~CSpawnMenu() {}

void CSpawnMenu::CreateSpawnButton( const char *label, const char *cmd, int x, int y )
{
    vgui::Button *pBtn = new vgui::Button( this, "SpawnBtn", label );
    pBtn->SetPos( x, y );
    pBtn->SetSize( 280, 30 );
    pBtn->SetCommand( cmd );
}

void CSpawnMenu::OnCommand( const char *command )
{
    // 执行控制台指令
    engine->ClientCmd( command );
    BaseClass::OnCommand( command );
}

void CC_ToggleSpawnMenu( void )
{
    if ( !g_pSpawnMenu ) return;

    if ( g_pSpawnMenu->IsVisible() )
    {
        g_pSpawnMenu->SetVisible( false );
        vgui::surface()->SetCursor( vgui::dc_none );
        vgui::input()->SetMouseCapture( NULL );
    }
    else
    {
        g_pSpawnMenu->Activate();
        vgui::surface()->SetCursor( vgui::dc_arrow );
    }
}

static ConCommand toggle_spawnmenu( "toggle_spawnmenu", CC_ToggleSpawnMenu, "显示/隐藏刷物品菜单" );

void VGUI_CreateSpawnMenu( vgui::VPANEL parent )
{
    g_pSpawnMenu = new CSpawnMenu( parent );
}
