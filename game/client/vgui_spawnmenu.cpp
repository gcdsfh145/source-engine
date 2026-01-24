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
    
    // 极大面板尺寸，防止安卓端分辨率过小时挤在一起
    int wide = vgui::scheme()->GetProportionalScaledValue( 280 );
    int tall = vgui::scheme()->GetProportionalScaledValue( 450 );
    SetSize( wide, tall );
    
    int sw, sh;
    vgui::surface()->GetScreenSize( sw, sh );
    SetPos( ( sw - wide ) / 2, ( sh - tall ) / 2 );
    
    SetTitle( "起源引擎终极调试面板", true );
    
    // 起始高度
    m_iLastY = vgui::scheme()->GetProportionalScaledValue( 40 );

    // --- 按钮列表 ---
    AddMenuButton( "获取全武器 + 全补给", "impulse 101" );
    
    AddMenuButton( "开启：真正弹药不消耗", "sv_infinite_ammo 1" );
    AddMenuButton( "关闭：真正弹药不消耗", "sv_infinite_ammo 0" );
    
    AddMenuButton( "开启：全员可杀 (无敌NPC除外)", "sv_allow_all_kill 1" );
    AddMenuButton( "关闭：全员可杀", "sv_allow_all_kill 0" );
    
    AddMenuButton( "开启：自动连跳 (Auto-Bhop)", "sv_autobhop 1" );
    AddMenuButton( "开启：无限加速叠加 (SpeedStack)", "sv_speedinf 1" );
    AddMenuButton( "关闭：连跳与加速", "sv_autobhop 0; sv_speedinf 0" );
    
    AddMenuButton( "切换：无敌模式 (God)", "god" );
    AddMenuButton( "切换：穿墙模式 (Noclip)", "noclip" );
    
    AddMenuButton( "生成：医疗包 + 电池", "give item_healthkit; give item_battery" );
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
    
    // 增加间距到 28 像素比例值，绝对不会重叠
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

static ConCommand toggle_spawnmenu( "toggle_spawnmenu", CC_ToggleSpawnMenu, "显示/隐藏修改菜单" );

void VGUI_CreateSpawnMenu( vgui::VPANEL parent )
{
    g_pSpawnMenu = new CSpawnMenu( parent );
}
