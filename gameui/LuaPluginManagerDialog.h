//========= Copyright Valve Corporation, All rights reserved. ============//

#ifndef LUAPLUGINMANAGERDIALOG_H
#define LUAPLUGINMANAGERDIALOG_H

#include <vgui_controls/Frame.h>

namespace vgui
{
class Button;
class ListPanel;
}

class CLuaPluginManagerDialog : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE( CLuaPluginManagerDialog, vgui::Frame );

public:
	CLuaPluginManagerDialog( vgui::Panel *parent );
	virtual void Activate();

private:
	void RefreshList();
	bool GetSelectedPlugin( char *role, int roleSize, char *name, int nameSize );
	void SendPluginCommand( const char *action );
	virtual void OnCommand( const char *command );
	MESSAGE_FUNC( OnItemSelected, "ItemSelected" );

	vgui::ListPanel *m_pPluginList;
	vgui::Button *m_pLoadButton;
	vgui::Button *m_pUnloadButton;
	vgui::Button *m_pEnableButton;
	vgui::Button *m_pDisableButton;
};

#endif // LUAPLUGINMANAGERDIALOG_H
