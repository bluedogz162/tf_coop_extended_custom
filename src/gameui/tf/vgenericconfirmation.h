//========= Copyright � 1996-2008, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=====================================================================================//

#ifndef __VGENERICCONFIRMATION_H__
#define __VGENERICCONFIRMATION_H__

#include "vgui_controls/cvartogglecheckbutton.h"

#include "basemodui.h"

class CExMenuButton;

namespace BaseModUI {

class GenericConfirmation : public CBaseModFrame
{
	DECLARE_CLASS_SIMPLE( GenericConfirmation, CBaseModFrame );
public:
	GenericConfirmation(vgui::Panel *parent, const char *panelName);
	~GenericConfirmation();

	// 
	// Public types
	//
	typedef void (*Callback_t)(void);

	struct Data_t
	{
		const char* pWindowTitle;
		const char* pMessageText;
		const wchar_t* pMessageTextW;
		
		bool        bOkButtonEnabled;
		Callback_t	pfnOkCallback;

		bool        bCancelButtonEnabled;
		Callback_t	pfnCancelCallback;

		bool		bCheckBoxEnabled;
		const char *pCheckBoxLabelText;
		const char *pCheckBoxCvarName;

		Data_t();
	};

	int  SetUsageData( const Data_t & data );     // returns the usageId, different number each time this is called
	int  GetUsageId() const { return m_usageId; }

	// 
	// Accessors
	//
	const Data_t & GetUsageData() const { return m_data; }

protected:
	virtual void ApplySchemeSettings(vgui::IScheme *pScheme);
	virtual void OnCommand(const char *command);
	virtual void OnKeyCodePressed(vgui::KeyCode code);
	virtual void OnKeyCodeTyped( vgui::KeyCode code );
	virtual void OnOpen();
	virtual void LoadLayout();
	virtual void PaintBackground();

	vgui::Panel *m_pPnlLowerGarnish;
	vgui::CvarToggleCheckButton<ConVarRef> *m_pCheckBox;

	CExMenuButton *m_pBtnOK;
	CExMenuButton *m_pBtnCancel;

private:
	vgui::Label *m_pLblMessage;
	vgui::Label *m_pLblCheckBox;

	Data_t		 m_data;
	int			 m_usageId;

	static int sm_currentUsageId;

	bool	m_bNeedsMoveToFront;

	vgui::HFont		m_hTitleFont;
	vgui::HFont		m_hMessageFont;
};

};

#endif
