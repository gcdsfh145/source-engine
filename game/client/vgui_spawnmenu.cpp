#include "cbase.h"
#include "vgui_spawnmenu.h"
#include <vgui/IVGui.h>
#include <vgui/ISurface.h>
#include <vgui/IInput.h>
#include <vgui/IScheme.h>
#include <vgui_controls/Controls.h>
#include "ienginevgui.h"
#include "convar.h"

#include "tier0/memdbgon.h"

CSpawnMenu* g_pSpawnMenu = NULL;

CSpawnMenu::CSpawnMenu( vgui::VPANEL parent ) : BaseClass( NULL, "SpawnMenu" )
{
    SetParent( parent );
    
    int wide = vgui::scheme()->GetProportionalScaledValue( 280 );
    int tall = vgui::scheme()->GetProportionalScaledValue( 450 );
    SetSize( wide, tall );
    
    int sw, sh;
    vgui::surface()->GetScreenSize( sw, sh );
    SetPos( ( sw - wide ) / 2, ( sh - tall ) / 2 );
    
    SetTitle( "起源引擎终极调试面板", true );
    m_iLastY = vgui::scheme()->GetProportionalScaledValue( 40 );

    AddMenuButton( "获取全武器 + 全补给", "impulse 101" );
    AddMenuButton( "获取：独立 RPG 手枪", "give weapon_rpgpistol" );
    AddMenuButton( "获取：独立 AR3FIRE", "give weapon_ar3fire" );
    AddMenuButton( "开启：无限弹药", "sv_infinite_ammo 1" );
    AddMenuButton( "开启：二段跳", "sv_jumpset 2" );
    AddMenuButton( "开启：去除速度限制", "sv_speedinf 1" );
    AddMenuButton( "切换：无敌模式 (God)", "god" );
    AddMenuButton( "切换：穿墙模式 (Noclip)", "noclip" );
    AddMenuButton( "开启：自动连跳 (Bhop)", "sv_autobhop 1" );
    AddMenuButton( "修复：渲染空缺 (单线程)", "mat_queue_mode 0" );
    AddMenuButton( "删除全部 NPC", "npc_destroy_unselected" );

    SetVisible( false );
}

CSpawnMenu::~CSpawnMenu() {}

void CSpawnMenu::AddMenuButton( const char *label, const char *cmd )
{
    vgui::Button *pBtn = new vgui::Button( this, "SpawnBtn", label );
    int btnWide = GetWide() - vgui::scheme()->GetProportionalScaledValue( 30 );
    int btnTall = vgui::scheme()->GetProportionalScaledValue( 22 );
    pBtn->SetPos( vgui::scheme()->GetProportionalScaledValue( 15 ), m_iLastY );
    pBtn->SetSize( btnWide, btnTall );
    pBtn->SetCommand( cmd );
    m_iLastY += ( btnTall + vgui::scheme()->GetProportionalScaledValue( 8 ) );
}

void CSpawnMenu::OnCommand( const char *command )
{
    engine->ClientCmd( "sv_cheats 1" );
    engine->ClientCmd( command );
    BaseClass::OnCommand( command );
}

void CC_ToggleSpawnMenu( void )
{
    if ( !g_pSpawnMenu ) return;
    if ( g_pSpawnMenu->IsVisible() ) {
        g_pSpawnMenu->SetVisible( false );
        vgui::surface()->SetCursor( vgui::dc_none );
        vgui::input()->SetMouseCapture( NULL );
    } else {
        g_pSpawnMenu->Activate();
        vgui::surface()->SetCursor( vgui::dc_arrow );
    }
}

static ConCommand toggle_spawnmenu( "toggle_spawnmenu", CC_ToggleSpawnMenu, "显示/隐藏修改菜单" );
void VGUI_CreateSpawnMenu( vgui::VPANEL parent ) { g_pSpawnMenu = new CSpawnMenu( parent ); }
