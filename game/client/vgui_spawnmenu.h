#ifndef VGUI_SPAWNMENU_H
#define VGUI_SPAWNMENU_H

#include <vgui_controls/Frame.h>
#include <vgui_controls/Button.h>

class CSpawnMenu : public vgui::Frame
{
    DECLARE_CLASS_SIMPLE( CSpawnMenu, vgui::Frame );

public:
    CSpawnMenu( vgui::VPANEL parent );
    ~CSpawnMenu();

    virtual void OnCommand( const char *command ) override;
    void AddMenuButton( const char *label, const char *cmd );
    void UpdateMenu();

private:
    int m_iLastY;
};

extern void VGUI_CreateSpawnMenu( vgui::VPANEL parent );

#endif // VGUI_SPAWNMENU_H