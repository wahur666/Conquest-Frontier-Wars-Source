//--------------------------------------------------------------------------//
//                                                                          //
//                           Menu_igoptions.cpp                             //
//                                                                          //
//               COPYRIGHT (C) 1999 BY DIGITAL ANVIL, INC.                  //
//                                                                          //
//--------------------------------------------------------------------------//
/*
   $Header: /Conquest/App/Src/Menu_igoptions.cpp 34    5/04/01 2:59p Tmauer $
*/
//--------------------------------------------------------------------------//
 
#include "pch.h"
#include <globals.h>

#include "Frame.h"
#include "IButton2.h"
#include "IStatic.h"
#include <Digoptions.h>
#include "Mission.h"
#include "Hotkeys.h"
#include "OpAgent.h"


#define BUTTON_SAVE    IDS_SAVE
#define BUTTON_LOAD    IDS_LOAD
#define BUTTON_OPTIONS IDS_OPTIONS
#define BUTTON_HELP    IDS_HELP
#define BUTTON_MISSION IDS_MISSION_OBJECTIVES
#define BUTTON_END     IDS_END_MISSION

//U32 __stdcall CreateMissionObjectivesMenu (void);
U32 __stdcall CreateEndMissionMenu (void);
U32 __stdcall CreateOptionsMenu (const bool bFocusing = true);
U32 __stdcall CreateMenuLoadSaveSpecial (bool bLoad, bool bSinglePlayerMaps);

//--------------------------------------------------------------------------//
//
struct dummy_igoptions : public Frame
{
	static IDAComponent* GetIDocumentClient(void* self) {
	    return static_cast<IDocumentClient*>(
	        static_cast<dummy_igoptions*>(self));
	}
	static IDAComponent* GetIEventCallback(void* self) {
	    return static_cast<IEventCallback*>(
	        static_cast<dummy_igoptions*>(self));
	}
	static IDAComponent* GetIResourceClient(void* self) {
	    return static_cast<IResourceClient*>(
	        static_cast<dummy_igoptions*>(self));
	}
	static IDAComponent* GetIDAConnectionPointContainer(void* self) {
	    return static_cast<IDAConnectionPointContainer*>(
	        static_cast<dummy_igoptions*>(self));
	}

	static std::span<const DACOMInterfaceEntry2> GetInterfaceMap() {
	    static const DACOMInterfaceEntry2 map[] = {
	        {"IDocumentClient",               &GetIDocumentClient},
	        {"IEventCallback",                &GetIEventCallback},
	        {"IResourceClient",               &GetIResourceClient},
	        {"IDAConnectionPointContainer",   &GetIDAConnectionPointContainer},
	        {IID_IDAConnectionPointContainer, &GetIDAConnectionPointContainer},
	    };
	    return map;
	}
};
//--------------------------------------------------------------------------//
//
struct Menu_igoptions : public DAComponentX<dummy_igoptions>
{
	//
	// data items
	//
	GT_IGOPTIONS data;
	COMPTR<IStatic> background, title;
	COMPTR<IButton2> buttonSave, buttonLoad;
	COMPTR<IButton2> buttonOptions;
	COMPTR<IButton2> buttonRestart, buttonResign, buttonAbdicate;
	COMPTR<IButton2> buttonReturn;

	bool bCloseOK;

	//
	// instance methods
	//

	Menu_igoptions (void)
	{
		eventPriority = EVENT_PRIORITY_IG_OPTIONS;
		parent->SetCallbackPriority(this, eventPriority);
		initializeFrame(NULL);
		init();
	}

	~Menu_igoptions (void);

    void * operator new (size_t size)
	{
		return calloc(size, 1);
	}

	void   operator delete (void *ptr)
	{
		::free(ptr);
	}

	/* IDocumentClient methods */

	DEFMETHOD(OnUpdate) (struct IDocument *doc, const C8 *message, void *parm);

	/* Menu_igoptions methods */

	virtual void setStateInfo (void);

	virtual bool onTabPressed (void)
	{
		if (childFrame!=0)
			return false;
		return Frame::onTabPressed();
	}

	virtual void onButtonPressed (U32 buttonID)
	{
		switch (buttonID)
		{
		case BUTTON_SAVE:
			// open the save dialog
			SetVisible(false);
			CreateMenuLoadSaveSpecial(false, MISSION->IsSinglePlayerGame());
			SetVisible(true);
			break;

		case BUTTON_LOAD:
			// open the load dialog
			SetVisible(false);
			CreateMenuLoadSaveSpecial(true, MISSION->IsSinglePlayerGame());
			SetVisible(true);
			break;

		case BUTTON_OPTIONS:
			// open the options menu
			SetVisible(false);
			CreateOptionsMenu(false);
			SetVisible(true);
			break;

		case IDS_RESTART:
			// restart the mission
//			MISSION->Reload();
			MISSION->ReloadMission();
			endDialog(0);			
			break;

		case IDS_RESIGN:
			endDialog(1);			
			break;

		case IDS_ABDICATE:
			endDialog(2);
			break;

		case IDS_BACK:
		case IDS_RETURN_TO_GAME:
			endDialog(0);
			break;
		}

		desiredOwnedFlags = RF_CURSOR;
		grabAllResources();

	}

	virtual bool onEscPressed (void)	//  return true if message was handled
	{
		endDialog(0);
		return true;
	}

	void init (void);
	
	void initLobby (void);
};
//----------------------------------------------------------------------------------//
//
Menu_igoptions::~Menu_igoptions (void)
{
}
//----------------------------------------------------------------------------------//
//
GENRESULT Menu_igoptions::OnUpdate (struct IDocument *doc, const C8 *message, void *parm)
{
	DWORD dwRead;

	doc->SetFilePointer(0,0);
	doc->ReadFile(0, &data, sizeof(data), &dwRead, 0);
	setStateInfo();

	return GR_OK;
}
//----------------------------------------------------------------------------------//
//
void Menu_igoptions::setStateInfo (void)
{
	//
	// create members if not done already
	//
	screenRect.left		= IDEAL2REALX(data.screenRect.left);
	screenRect.right	= IDEAL2REALX(data.screenRect.right);
	screenRect.top		= IDEAL2REALY(data.screenRect.top);
	screenRect.bottom	= IDEAL2REALY(data.screenRect.bottom);

	buttonSave->InitButton(data.buttonSave, this);
	buttonLoad->InitButton(data.buttonLoad, this);
	buttonOptions->InitButton(data.buttonOptions, this);
	buttonRestart->InitButton(data.buttonRestart, this);
	buttonResign->InitButton(data.buttonResign, this);
	buttonAbdicate->InitButton(data.buttonAbdicate, this);
	buttonReturn->InitButton(data.buttonReturn, this);

	buttonSave->SetTransparent(true);
	buttonLoad->SetTransparent(true);
	buttonOptions->SetTransparent(true);
	buttonRestart->SetTransparent(true);
	buttonResign->SetTransparent(true);
	buttonAbdicate->SetTransparent(true);
	buttonReturn->SetTransparent(true);
	
	background->InitStatic(data.background, this);
	title->InitStatic(data.title, this);

	// if we're in multiplayer mode, than we cannot load another game
	if (PLAYERID!=0)
		buttonLoad->EnableButton(false);
	if (PLAYERID!=0 && CQFLAGS.bLimitMapSettings)
		buttonSave->EnableButton(false);

	// we can only restart a mission based (single player) game
	buttonRestart->EnableButton(MISSION->IsSinglePlayerGame());
	buttonAbdicate->EnableButton(PLAYERID != 0);

	if (childFrame)
		childFrame->setStateInfo();
	else
	{
		setFocus(buttonReturn);
	}
}
//--------------------------------------------------------------------------//
//
void Menu_igoptions::init (void)
{
	data = 	*((GT_IGOPTIONS *) GENDATA->GetArchetypeData("Menu_igoptions"));

	COMPTR<IDAComponent> pComp;
	
	GENDATA->CreateInstance(data.background.staticType, pComp.addr());
	pComp->QueryInterface("IStatic", background.void_addr());

	GENDATA->CreateInstance(data.title.staticType, pComp.addr());
	pComp->QueryInterface("IStatic", title.void_addr());

	GENDATA->CreateInstance(data.buttonSave.buttonType, pComp.addr());
	pComp->QueryInterface("IButton2", buttonSave.void_addr());

	GENDATA->CreateInstance(data.buttonLoad.buttonType, pComp.addr());
	pComp->QueryInterface("IButton2", buttonLoad.void_addr());

	GENDATA->CreateInstance(data.buttonOptions.buttonType, pComp.addr());
	pComp->QueryInterface("IButton2", buttonOptions.void_addr());

	GENDATA->CreateInstance(data.buttonRestart.buttonType, pComp.addr());
	pComp->QueryInterface("IButton2", buttonRestart.void_addr());

	GENDATA->CreateInstance(data.buttonResign.buttonType, pComp.addr());
	pComp->QueryInterface("IButton2", buttonResign.void_addr());

	GENDATA->CreateInstance(data.buttonAbdicate.buttonType, pComp.addr());
	pComp->QueryInterface("IButton2", buttonAbdicate.void_addr());

	GENDATA->CreateInstance(data.buttonReturn.buttonType, pComp.addr());
	pComp->QueryInterface("IButton2", buttonReturn.void_addr());

	resPriority = RES_PRIORITY_HIGH;
	cursorID = IDC_CURSOR_ARROW;
	desiredOwnedFlags = RF_CURSOR;
	grabAllResources();
}
//--------------------------------------------------------------------------//
//
U32 __stdcall CreateMenu_igoptions (void)
{
	static U8 recurse;

	CQASSERT(recurse==0);
	recurse++;

	Menu_igoptions * menu = new Menu_igoptions;

	menu->createViewer("\\GT_IGOPTIONS\\Menu_igoptions", "GT_IGOPTIONS", IDS_VIEWIGOPTIONS);
	menu->beginModalFocus();

	// pause the game if in single player mode
	BOOL32 bOldPause = CQFLAGS.bGamePaused;
	if (PLAYERID == 0)
	{
		CQFLAGS.bGamePaused = TRUE;
		EVENTSYS->Send(CQE_PAUSE_CHANGE, (void *)(CQFLAGS.bGamePaused));
	}

	U32 result = CQDoModal(menu);
	delete menu;

	// unpause the game, if we must
	if (PLAYERID == 0)
	{
		CQFLAGS.bGamePaused = bOldPause;
		EVENTSYS->Send(CQE_PAUSE_CHANGE, (void *)(CQFLAGS.bGamePaused));
	}

	recurse--;
	return result;
}

//--------------------------------------------------------------------------//
//-----------------------------End Menu_igoptions.cpp-----------------------//
//--------------------------------------------------------------------------//
