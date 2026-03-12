#ifndef FRAME_H
#define FRAME_H
//--------------------------------------------------------------------------//
//                                                                          //
//                                Frame.h                                   //
//                                                                          //
//               COPYRIGHT (C) 1998 BY DIGITAL ANVIL, INC.                  //
//                                                                          //
//--------------------------------------------------------------------------//
/*
   $Header: /Conquest/App/Src/Frame.h 44    11/10/00 1:21p Jasony $
*/
//--------------------------------------------------------------------------//

#include "TDocClient.h"
#include "BaseHotRect.h"
#include "Resource.h"
#include "CQTrace.h"
#include "GenData.h"
#include "Menu.h"
#include "VideoSurface.h"
#include "RandomNum.h"

#include <TComponent.h>
#include <IConnection.h>
#include <HeapObj.h>
#include <TSmartPointer.h>
#include <IConnection.h>
#include <Viewer.h>
#include <ViewCnst.h>
#include <Document.h>
#include <IDocClient.h>
#include <EventSys2.h>
#include <WindowManager.h>

//--------------------------------------------------------------------------//
//
#undef FRAMEEXTERN
#ifdef BUILD_TRIM
#define FRAMEEXTERN __declspec(dllexport)
#else
#define FRAMEEXTERN __declspec(dllimport)
#endif
//--------------------------------------------------------------------------//
//
struct DACOM_NO_VTABLE Frame : public BaseHotRect, DocumentClient, ISystemEventCallback
{
	static IDAComponent* GetIDocumentClient(void* self) {
	    return static_cast<IDocumentClient*>(
	        static_cast<Frame*>(self));
	}
	static IDAComponent* GetIEventCallback(void* self) {
	    return static_cast<IEventCallback*>(
	        static_cast<Frame*>(self));
	}
	static IDAComponent* GetBaseHotRect(void* self) {
	    return reinterpret_cast<IDAComponent *> (
			static_cast<BaseHotRect*>(
				static_cast<Frame*>(self)));
	}
	static IDAComponent* GetIResourceClient(void* self) {
	    return static_cast<IResourceClient*>(
	        static_cast<Frame*>(self));
	}
	static IDAComponent* GetIDAConnectionPointContainer(void* self) {
	    return static_cast<IDAConnectionPointContainer*>(
	        static_cast<Frame*>(self));
	}
	static IDAComponent* GetISystemEventCallback(void* self) {
	    return static_cast<ISystemEventCallback*>(
	        static_cast<Frame*>(self));
	}

	static std::span<const DACOMInterfaceEntry2> GetInterfaceMap() {
	    static const DACOMInterfaceEntry2 map[] = {
	        {"IDocumentClient",               &GetIDocumentClient},
	        {"IEventCallback",                &GetIEventCallback},
	        {"BaseHotRect",                   &GetBaseHotRect},
	        {"IResourceClient",               &GetIResourceClient},
	        {"IDAConnectionPointContainer",   &GetIDAConnectionPointContainer},
	        {IID_IDAConnectionPointContainer, &GetIDAConnectionPointContainer},
	        {"ISystemEventCallback",          &GetISystemEventCallback},
	    };
	    return map;
	}

	//
	// data for on-line editing of data
	//
	COMPTR<IViewer> viewer;
	COMPTR<IDocument> doc;
	U32 menuID;
	char m_szInstanceName[64];
	char m_szClassName[64];
	
	FRAMEEXTERN static Frame * g_pFocusFrame;
	Frame * pPrevFocusFrame, * pNextFocusFrame;

	bool bGroupBehavior;
	bool bExitCodeSet;		// true when endDialog() has been called

	U32 eventPriority;

	//
	// data for keyboard focus management
	// 
	COMPTR<IKeyboardFocus> focusControl;

	//
	// data for menu heirarchy
	// 
	Frame * parentFrame, * childFrame;

	//
	// hooked system event (appClose)
	//
	U32 systemEventHandle;

	//
	// instance methods
	//

	Frame (void) : BaseHotRect(FULLSCREEN)
	{
		eventPriority = EVENT_PRIORITY_HOTRECT;
	}

	Frame (BaseHotRect * _parent) : BaseHotRect(_parent)
	{
		eventPriority = EVENT_PRIORITY_HOTRECT;
	}

	void initializeFrame (Frame * _parentFrame)
	{
		if (parent == 0)
		{
			if ((parent = _parentFrame) == 0)
				parent = FULLSCREEN;
			lateInitialize(parent->GetBase());
		}
		if (_parentFrame)
		{
			parentFrame = _parentFrame;
			_parentFrame->childFrame = this;
			eventPriority = _parentFrame->eventPriority + 1;
			parent->SetCallbackPriority(this, eventPriority);
		}
	}
	
	virtual ~Frame (void)
	{
		if (MENU && menuID)
		{
			HMENU hMenu = MENU->GetSubMenu(MENUPOS_VIEW);
			RemoveMenu(hMenu, menuID, MF_BYCOMMAND);
			menuID = 0;
		}


		if (parentFrame)
			parentFrame->childFrame = 0;
		delete childFrame;

		endModalFocus();
		unhookSystemEvent();
	}

	void * operator new (size_t size)
	{
		return HEAP_Acquire()->ClearAllocateMemory(size, "Frame");
	}

	/* IEventCallback methods */

	DEFMETHOD(Notify) (U32 message, void *param);

	/* IDocumentClient methods */

	DEFMETHOD(OnUpdate) (struct IDocument *doc, const C8 *message, void *parm)
	{
		return GR_OK;
	}

	/* ISystemEventCallback methods */

	DEFMETHOD_(BOOL32,OnAppClose) (void)
	{
		return FALSE;		// allow app to close
	}

	/* Frame methods */

	void beginModalFocus (void)
	{
		if (this == g_pFocusFrame || pPrevFocusFrame || pNextFocusFrame)
		{
			// do nothing, already have fcous
		}
		else
		{
			if (g_pFocusFrame == 0)
			{
				g_pFocusFrame = this;
				FULLSCREEN->Notify(CQE_KILL_FOCUS, (void *)static_cast<BaseHotRect *>(this));
			}
			else
			{
				pPrevFocusFrame = g_pFocusFrame;
				CQASSERT(pPrevFocusFrame->pNextFocusFrame == 0);
				pPrevFocusFrame->pNextFocusFrame = this;
				g_pFocusFrame = this;
				pPrevFocusFrame->Notify(CQE_KILL_FOCUS, (void *)static_cast<BaseHotRect *>(this));
			}
		}
	}

	void endModalFocus (void)
	{
		if (this == g_pFocusFrame || pPrevFocusFrame || pNextFocusFrame)
		{
			if (g_pFocusFrame == this)
			{
				CQASSERT(pNextFocusFrame==0);
				if ((g_pFocusFrame = pPrevFocusFrame) != 0)
				{
					pPrevFocusFrame->pNextFocusFrame = 0;
					pPrevFocusFrame->Notify(CQE_SET_FOCUS, 0);
				}
				else
				{
					FULLSCREEN->Notify(CQE_SET_FOCUS, 0);
				}
			}
			else // remove from middle of the list
			{
				if (pPrevFocusFrame)
					pPrevFocusFrame->pNextFocusFrame = pNextFocusFrame;
				if (pNextFocusFrame)
					pNextFocusFrame->pPrevFocusFrame = pPrevFocusFrame;
			}
			pPrevFocusFrame = pNextFocusFrame = 0;
		}
	}

	void hookSystemEvent (void)
	{
		if (systemEventHandle==0)
		{
			COMPTR<IDAConnectionPoint> connection;
			if (GS->QueryOutgoingInterface("ISystemEventCallback", connection.addr()) == GR_OK)
				connection->Advise(GetBase(), &systemEventHandle);
		}
	}

	void unhookSystemEvent (void)
	{
		if (systemEventHandle)
		{
			COMPTR<IDAConnectionPoint> connection;

			if (GS && GS->QueryOutgoingInterface("ISystemEventCallback", connection.addr()) == GR_OK)
				connection->Unadvise(systemEventHandle);
			systemEventHandle = 0;
		}
	}

	BOOL32 createViewer (const char *szInstanceName, const char *szClassName, U32 menuID);

	void removeViewer (void);

	void actuallyCreateViewer (void);

	void setFocus (struct IDAComponent * component);

	inline void nextFocus (void);

	size_t findPrevFocusIndex(IKeyboardFocus *pCurr);

	inline void prevFocus (void);

	inline bool isNextFocusBehind (void);

	inline bool isPrevFocusAhead (void);

	void setGroupBehavior (void)
	{
		bGroupBehavior = true;
	}

	void endDialog (U32 exitCode)
	{
		// post message to prevent message from going to wrong modal loop
		parent->PostMessage(CQE_DLG_RESULT, (void*) exitCode);
		bExitCodeSet = true;
	}

	// override this method to hook event
	virtual void onSetDefaultFocus (bool bFromPrevious)
	{
		setFocus(focusControl);
	}

	// override this method to hook event -- return true if message was handled
	virtual bool onTabPressed (void)
	{
		if (focusControl!=0)
		{	
			nextFocus();
			return true;
		}
		return false;
	}

	void setLastFocus (void)
	{
		// set the last possible focus in the control
		if (!point2.clients.empty())
		{
			setFocus(point2.clients.front());
			prevFocus();
		}
	}

	bool onGroupTabPressed (void)
	{
		// switch focus to the next control, if we are at the end of the control list, than return false
		// so that we know to switch control to the next group
		if (bGroupBehavior)
		{
			if (focusControl != 0)
			{
				// do we want the focus control to go to the next frame/menu?
				return isNextFocusBehind();
			}
		}
		return true;
	}

	bool onGroupShiftTabPressed (void)
	{
		// switch focus to the prve control, if we are at the beginning of the control list, than return false
		// so that we know to switch control to the next group
		if (bGroupBehavior)
		{
			if (focusControl != 0)
			{
				// do we want the focus control to go to the next frame/menu?
				return isPrevFocusAhead();
			}
		}
		return true;
	}

	virtual bool onShiftTabPressed (void)
	{
		if (focusControl != 0)
		{
			prevFocus();
			return true;
		}
		return false;
	}

	virtual bool onPrevPressed (void)
	{
		if (focusControl != 0)
		{
			prevFocus();
			return true;
		}
		return false;
	}

	virtual bool onNextPressed (void)
	{
		if (focusControl != 0)
		{
			nextFocus();
		}
		return false;
	}

	// override this method to hook event
	virtual bool onEscPressed (void)	//  return true if message was handled
	{
		return false;
	}

	// override this method to hook event
	virtual void onFocusRequested (IDAComponent * control)
	{
		setFocus(control);
	}

	// override this method to hook event
	virtual void onButtonPressed (U32 buttonID)
	{
	}

	// override this method to hook event
	virtual void onSliderPressed (U32 sliderID)
	{
	}

	virtual void onEditChanged (U32 editID)
	{
	}

	// override this method to hook event, pass message to child
	virtual void setStateInfo (void)
	{
	}

	// override this method to hook event
	virtual void onUpdate (U32 dt)   // dt in milliseconds
	{
	}

	// override this method to hook event
	virtual void onListSelection (S32 listControlID)		// user has selected a list item
	{
	}

		// override this method to hook event
	virtual void onListCaretMove (S32 listControlID)		// user has moved the caret
	{
	}

	// override this method to hook event
	virtual void onFocusChanged (void)		// focusControl has already been set to the new control
	{
	}

	// override this method to hook event, return true if handled
	virtual bool onHotkeyEvent (U32 eventID)
	{
		return false;
	}

	// override this method to hook event, return true if handled
	// user has attempted to close the main window, return true to cancel that attempt
	virtual bool onAppClose (void)
	{
		if (childFrame)
			return childFrame->onAppClose();
		return false;
	}

	// override this to hook CQE_ENDFRAME events
	virtual void onDraw (void)
	{
	}

	// implement invisibility by setting our invisible flag, 
	// and making all our children loose focus.
	virtual void SetVisible (bool bVisible)
	{
		if (bVisible==false)
		{
			bool bOrigFocus = bHasFocus;
			BaseHotRect::Notify(CQE_KILL_FOCUS, 0);
			bHasFocus = bOrigFocus;
			bInvisible = true;
		}
		else
		{
			bInvisible = false;
			if (bHasFocus)
				BaseHotRect::Notify(CQE_SET_FOCUS, 0);
		}

		// send a mouse update
		{
			MSG msg;
			S32 x, y;

			WM->GetCursorPos(x, y);

			msg.hwnd = hMainWindow;
			msg.message = WM_MOUSEMOVE;
			msg.wParam = 0;
			msg.lParam = MAKELPARAM(x,y);
			Notify(WM_MOUSEMOVE, &msg);
		}
	}
};
//----------------------------------------------------------------------------------//
//
inline BOOL32 Frame::createViewer (const char *szInstanceName, const char *szClassName, U32 _menuID)
{
	if (viewer==0 && CQFLAGS.bNoGDI==0)
	{
		HMENU hMenu = MENU->GetSubMenu(MENUPOS_VIEW);
		MENUITEMINFO minfo;
		const char *ptr = strrchr(szInstanceName, '\\')+1;

		strncpy(m_szInstanceName, szInstanceName, sizeof(m_szInstanceName)-1);
		strncpy(m_szClassName, szClassName, sizeof(m_szClassName)-1);

		memset(&minfo, 0, sizeof(minfo));
		minfo.cbSize = sizeof(minfo);
		minfo.fMask = MIIM_ID | MIIM_TYPE;
		minfo.fType = MFT_STRING;
		minfo.wID = _menuID;
		minfo.dwTypeData = const_cast<char *>(ptr);
		minfo.cch = strlen(ptr);
				
		if (InsertMenuItem(hMenu, 0x7FFE, 1, &minfo))
			menuID = _menuID;	
	}

	setStateInfo();

	return (viewer != 0);
}
//----------------------------------------------------------------------------------//
//
inline void Frame::actuallyCreateViewer (void)
{
	if (CQFLAGS.bNoGDI)
		return;

	COMPTR<IDocument> pDatabase;

	if (viewer==0 && GENDATA->GetDataFile(pDatabase.addr()) == GR_OK)
	{
		if (pDatabase->GetChildDocument(m_szInstanceName, doc.addr()) != GR_OK)
		{
			CQBOMB0("Could not create document");
		}
		else
		{
			VIEWDESC vdesc;
			HWND hwnd;
			const char *ptr = strrchr(m_szInstanceName, '\\')+1;

			vdesc.className = m_szClassName;
			vdesc.doc = doc;
			vdesc.hOwnerWindow = hMainWindow;
			 
			if (PARSER->CreateInstance(&vdesc, viewer.void_addr()) == GR_OK)
			{
				viewer->get_main_window((void **) &hwnd);
				MoveWindow(hwnd, 100, 100, 400, 200, 1);

				viewer->set_instance_name(ptr);
			}

			MakeConnection(doc);
		}
	}
}
//----------------------------------------------------------------------------------//
//
inline void Frame::removeViewer (void)
{
	if (MENU && menuID)
	{
		HMENU hMenu = MENU->GetSubMenu(MENUPOS_VIEW);
		RemoveMenu(hMenu, menuID, MF_BYCOMMAND);
		menuID = 0;
	}

	if (doc!=0)
	{
		OnClose(doc);
		doc.free();
		viewer.free();
	}
}
//----------------------------------------------------------------------------------//
//
inline GENRESULT Frame::Notify (U32 message, void *param)
{
	MSG *msg = (MSG *) param;

	switch (message)
	{
	case WM_KEYDOWN:
		if (bHasFocus && bInvisible == false)
		{
			if (BaseHotRect::Notify(message, param) != GR_OK)		// give children a chance first
				return GR_GENERIC;
			else
			switch (msg->wParam)
			{
				case VK_UP:
				case VK_LEFT:
					onPrevPressed();
					break;

				case VK_DOWN:
				case VK_RIGHT:
					onNextPressed();
					break;
			}
			return GR_OK;
		}
		break;

	case WM_CHAR:
		if (bHasFocus && bInvisible==false)
		{
			if (BaseHotRect::Notify(message, param) != GR_OK)		// give children a chance first
				return GR_GENERIC;
			else
			switch (TCHAR(msg->wParam))
			{
			case 27:
				if (onEscPressed())
					return GR_GENERIC;
				break;

			case '\t':
				// was shift pressed as well?
				if (GetAsyncKeyState(VK_SHIFT) >> 1)
				{
					if (onShiftTabPressed())
						return GR_GENERIC;
				}
				else if (onTabPressed())
					return GR_GENERIC;		// don't propagate this message
				break;
			} // end switch (wParam)
			return GR_OK;
		}
		break; // end switch WM_CHAR

	case WM_CLOSE:
		if (parentFrame==0)
			parent->PostMessage(CQE_DELETE_HOTRECT, static_cast<BaseHotRect *>(this));
		break;

	case WM_COMMAND:
		if (LOWORD(msg->wParam) == menuID)
		{
			if (viewer==0)
				actuallyCreateViewer();
			if (viewer)
				viewer->set_display_state(1);
		}
		break;

	case CQE_UPDATE:
		onUpdate(U32(param) >> 10);
		break;

	case CQE_ENDFRAME:
		onDraw();
		break;

	case CQE_HOTKEY:
		if (onHotkeyEvent(U32(param)))
			return GR_GENERIC;
		break;

	case CQE_BUTTON:
		onButtonPressed(U32(param));
		return GR_OK;

	case CQE_SLIDER:
		onSliderPressed(U32(param));
		return GR_OK;

	case CQE_EDIT_CHANGED:
		onEditChanged(U32(param));
		return GR_OK;

	case CQE_KEYBOARD_FOCUS:
		onFocusRequested((IDAComponent *)param);
		return GR_OK;

	case CQE_LIST_SELECTION:
		onListSelection(S32(param));
		return GR_OK;

	case CQE_LIST_CARET_MOVED:
		onListCaretMove(S32(param));
		return GR_OK;

	case CQE_SET_FOCUS:
		if (bInvisible)
		{
			onSetFocus(true);
			return GR_OK;
		}
		break;
		
	case CQE_KILL_FOCUS:
		if (bInvisible)
		{
			onSetFocus(false);
			return GR_OK;
		}
		break;
		
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MOUSEMOVE:
	case WM_RBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_LBUTTONDOWN:
		if (bInvisible)
		{
			return GR_OK;
		}
		break;
	}
	
	if (bInvisible && message == CQE_ENDFRAME)
		return GR_OK;
	return BaseHotRect::Notify(message, param);
}
//----------------------------------------------------------------------------------//
//
inline void Frame::setFocus (struct IDAComponent * component)
{
	COMPTR<IKeyboardFocus> res;
	if (component)
	{
		component->QueryInterface("IKeyboardFocus", res.void_addr());
		CQASSERT(res!=0);
	}

	if (focusControl != res)
	{
		if (focusControl!=0)
		{
			focusControl->SetKeyboardFocus(false);
			focusControl.free();
		}

		if (res!=0)
		{
			//
			// clear the keyboard focus for other frames
			//
			Frame * frame=parentFrame;

			while (frame)
			{
				frame->setFocus(NULL);
				frame = frame->parentFrame;
			}
			frame = childFrame;
			while (frame)
			{
				frame->setFocus(NULL);
				frame = frame->childFrame;
			}

			if (res->SetKeyboardFocus(true))
				focusControl = res;
		}

		onFocusChanged();
	}
}
//----------------------------------------------------------------------------------//
//
inline bool Frame::isNextFocusBehind (void)
{
	auto& clients = point2.clients;
	if (clients.empty())
		return true;

	auto it = std::find(clients.begin(), clients.end(), (IKeyboardFocus*)focusControl);

	if (it == clients.end())
		return true;

	// advance past current
	++it;
	if (it == clients.end())
		return false;  // we are going back

	if (*it != (IKeyboardFocus*)focusControl)
	{
		focusControl->SetKeyboardFocus(false);

		while (*it != (IKeyboardFocus*)focusControl)
		{
			if ((*it)->SetKeyboardFocus(true))
			{
				focusControl->SetKeyboardFocus(true);
				return true;
			}
			++it;
			if (it == clients.end())
				return false;  // we are going back
		}
		focusControl->SetKeyboardFocus(true);
	}

	return true;
}
//----------------------------------------------------------------------------------//
//
inline void Frame::nextFocus (void)
{
	auto& clients = point2.clients;

	if (focusControl == 0)
	{
		if (clients.empty())
			return;
		focusControl = clients.front();
		if (focusControl->SetKeyboardFocus(true))
			onFocusChanged();
		else
			nextFocus();
		return;
	}

	auto it = std::find(clients.begin(), clients.end(), (IKeyboardFocus*)focusControl);

	if (it != clients.end())
	{
		++it;
		if (it == clients.end())
			it = clients.begin();  // wrap around
	}

	if (it != clients.end() && *it != (IKeyboardFocus*)focusControl)
	{
		focusControl->SetKeyboardFocus(false);

		while (*it != (IKeyboardFocus*)focusControl)
		{
			if ((*it)->SetKeyboardFocus(true))
			{
				focusControl = *it;
				onFocusChanged();
				return;
			}
			++it;
			if (it == clients.end())
				it = clients.begin();  // wrap around
		}
		focusControl->SetKeyboardFocus(true);
	}
}
//----------------------------------------------------------------------------------//
//
inline size_t Frame::findPrevFocusIndex (IKeyboardFocus* pCurr)
{
    auto& clients = point2.clients;

    if (pCurr == nullptr)
        pCurr = focusControl;

    // find the element whose next is pCurr
    for (size_t i = 0; i + 1 < clients.size(); ++i)
    {
        if (clients[i + 1] == pCurr)
            return i;
    }

    // not found - return last element
    return clients.size() - 1;
}
//----------------------------------------------------------------------------------//
//
inline bool Frame::isPrevFocusAhead (void)
{
    auto& clients = point2.clients;
    if (clients.empty())
        return true;

    size_t idx = findPrevFocusIndex((IKeyboardFocus*)focusControl);

    if (idx == clients.size() - 1 && clients[idx] != (IKeyboardFocus*)focusControl)
        return false;  // we are looking ahead

    if (clients[idx] != (IKeyboardFocus*)focusControl)
    {
        focusControl->SetKeyboardFocus(false);

        while (clients[idx] != (IKeyboardFocus*)focusControl)
        {
            if (clients[idx]->SetKeyboardFocus(true))
            {
                clients[idx]->SetKeyboardFocus(false);
                break;
            }
            idx = findPrevFocusIndex(clients[idx]);
            if (idx == clients.size())
                return false;
        }

        if (clients[idx] == (IKeyboardFocus*)focusControl)
            return false;

        // check if found node is before focusControl in the list
        bool bBeforeCurrent = false;
        for (auto* p : clients)
        {
            if (p == clients[idx])
            {
                bBeforeCurrent = true;
                break;
            }
            else if (p == (IKeyboardFocus*)focusControl)
            {
                bBeforeCurrent = false;
                break;
            }
        }

        focusControl->SetKeyboardFocus(true);
        return bBeforeCurrent;
    }

    return true;
}
//----------------------------------------------------------------------------------//
//
inline void Frame::prevFocus (void) {
	auto& clients = point2.clients;

	if (focusControl == 0)
	{
		if (clients.empty())
			return;
		focusControl = clients.front();
		if (focusControl->SetKeyboardFocus(true))
			onFocusChanged();
		else
			prevFocus();
		return;
	}

	size_t idx = findPrevFocusIndex(nullptr);

	if (clients[idx] != (IKeyboardFocus*)focusControl)
	{
		focusControl->SetKeyboardFocus(false);

		while (clients[idx] != (IKeyboardFocus*)focusControl)
		{
			if (clients[idx]->SetKeyboardFocus(true))
			{
				focusControl = clients[idx];
				onFocusChanged();
				return;
			}
			idx = findPrevFocusIndex(clients[idx]);
		}
		focusControl->SetKeyboardFocus(true);
	}
}
#endif