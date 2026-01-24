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
static int g_iMenuPage = 0;

// 实现构造函数，显式调用基类构造
CSpawnMenu::CSpawnMenu( vgui::VPANEL parent ) : vgui::Frame( NULL, "SpawnMenu" )
{
    SetParent( parent );
    UpdateMenu();
    SetVisible( false );
}

CSpawnMenu::~CSpawnMenu()
{
}

void CSpawnMenu::UpdateMenu()
{
    // 只清理我们自己添加的按钮 (名称为 SpawnBtn)，不要动系统的标题栏和关闭按钮
    for ( int i = GetChildCount() - 1; i >= 0; i-- )
    {
        vgui::Panel *pChild = GetChild( i );
        if ( pChild && Q_strcmp( pChild->GetName(), "SpawnBtn" ) == 0 )
        {
            pChild->MarkForDeletion();
        }
    }

    int wide = vgui::scheme()->GetProportionalScaledValue( 280 );
    int tall = vgui::scheme()->GetProportionalScaledValue( 450 );
    SetSize( wide, tall );
    
    int sw, sh;
    vgui::surface()->GetScreenSize( sw, sh );
    SetPos( ( sw - wide ) / 2, ( sh - tall ) / 2 );
    
    SetTitle( g_iMenuPage == 0 ? "修改菜单 (第 1 页)" : "修改菜单 (第 2 页)", true );
    m_iLastY = vgui::scheme()->GetProportionalScaledValue( 40 );

    if ( g_iMenuPage == 0 )
    {
        AddMenuButton( "获取全武器补给", "impulse 101" );
        AddMenuButton( "开启：无限弹药", "sv_infinite_ammo 1" );
        AddMenuButton( "开启：独立 RPG 手枪", "give weapon_rpgpistol" );
        AddMenuButton( "开启：二段跳", "sv_jumpset 2" );
        AddMenuButton( "开启：去除速度限制", "sv_speedinf 1" );
        AddMenuButton( "关闭：速度限制", "sv_speedinf 0" );
        AddMenuButton( "切换：无敌模式", "god" );
        AddMenuButton( "切换：穿墙模式", "noclip" );
        AddMenuButton( ">>> 下一页 >>>", "menu_next_page" );
    }
    else
    {
        AddMenuButton( "生成：医疗包电池", "give item_healthkit; give item_battery" );
        AddMenuButton( "开启：连跳 (Bhop)", "sv_autobhop 1" );
        AddMenuButton( "开启：五段跳", "sv_jumpset 5" );
        AddMenuButton( "开启：全员可杀", "sv_allow_all_kill 1" );
        AddMenuButton( "删除全部 NPC", "npc_destroy_unselected" );
        AddMenuButton( "<<< 返回前一页", "menu_prev_page" );
    }
}

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
    if ( !Q_strcmp( command, "menu_next_page" ) )
    {
        g_iMenuPage = 1;
        UpdateMenu();
        return;
    }
    else if ( !Q_strcmp( command, "menu_prev_page" ) )
    {
        g_iMenuPage = 0;
        UpdateMenu();
        return;
    }

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
