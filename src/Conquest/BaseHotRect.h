#ifndef BASEHOTRECT_H
#define BASEHOTRECT_H
//--------------------------------------------------------------------------//
//                                                                          //
//                              BaseHotRect.h                               //
//                                                                          //
//                  COPYRIGHT (C) 1998 BY DIGITAL ANVIL, INC.               //
//                                                                          //
//--------------------------------------------------------------------------//
/*
    $Header: /Conquest/App/Src/BaseHotRect.h 30    8/10/00 1:15p Sbarton $
*/			    
//--------------------------------------------------------------------------//
//--------------------------------------------------------------------------//

#ifndef TCPOINT_H 
#include "TCPoint.h"
#endif

#ifndef TCONNCONTAINER_H
#include <TConnContainer.h>
#endif

#ifndef TCONNPOINT_H
#include <TConnPoint.h>
#endif

#ifndef EVENTSYS_H
#include <EventSys2.h>
#endif
#include <span>

#include "TComponent2.h"

#ifndef IRESOURCE_H
#include "IResource.h"
#endif

#ifndef CURSOR_H
#include "Cursor.h"
#endif

#ifndef STATUSBAR_H
#include "StatusBar.h"
#endif

#ifndef TSMARTPOINTER_H
#include <TSmartPointer.h>
#endif

#ifndef HEAPOBJ_H
#include <HeapObj.h>
#endif

#ifndef EVENTPRIORITY_H
#include "EventPriority.h"
#endif

#ifndef HKEVENT_H
#include <hkevent.h>
#endif

#ifndef TRESCLIENT_H
#include "TResClient.h"
#endif

#ifndef USERDEFAULTS_H
#include "UserDefaults.h"
#endif

#ifndef WINDOWMANAGER_H
#include <WindowManager.h>
#endif

//--------------------------------------------------------------------------//
//--------------------------------------------------------------------------//
#define HOTRECT_PRIORITY  0x80000000

#undef BASEHOTRECTEXTERN
#ifdef BUILD_TRIM
#define BASEHOTRECTEXTERN __declspec(dllexport)
#else
#define BASEHOTRECTEXTERN __declspec(dllimport)
#endif

//---------------------------------------------------------------------------------
//
struct IKeyboardFocus : IDAComponent
{
	virtual bool SetKeyboardFocus (bool bEnable) = 0;		// return true if can accept keyboard focus

	virtual U32 GetControlID (void) = 0;
};
//--------------------------------------------------------------------------//
// outgoing interface to get notified about button presses
//
struct DACOM_NO_VTABLE IHotControlEvent : IDAComponent
{
	virtual void OnLeftButtonEvent (U32 menuID, U32 controlID) = 0;

	virtual void OnRightButtonEvent (U32 menuID, U32 controlID) = 0;

	virtual void OnLeftDblButtonEvent (U32 menuID, U32 controlID) = 0;		// double-click
};
//---------------------------------------------------------------------------------
//
struct DACOM_NO_VTABLE __dummyBaseHotRect : IEventCallback, ResourceClient<>, ConnectionPointContainer<struct BaseHotRect>
{
	//
	// incoming interface map
	//
	static IDAComponent* GetIResourceClient(void* self) {
	    return static_cast<IResourceClient*>(
	        static_cast<__dummyBaseHotRect*>(self));
	}
	static IDAComponent* GetIEventCallback(void* self) {
	    return static_cast<IEventCallback*>(
	        static_cast<__dummyBaseHotRect*>(self));
	}
	static IDAComponent* GetIDAConnectionPointContainer(void* self) {
	    return static_cast<IDAConnectionPointContainer*>(
	        static_cast<__dummyBaseHotRect*>(self));
	}

	static std::span<const DACOMInterfaceEntry2> GetInterfaceMap() {
	    static const DACOMInterfaceEntry2 map[] = {
	        {"IResourceClient",               &GetIResourceClient},
	        {"IEventCallback",                &GetIEventCallback},
	        {"IDAConnectionPointContainer",   &GetIDAConnectionPointContainer},
	        {IID_IDAConnectionPointContainer, &GetIDAConnectionPointContainer},
	    };
	    return map;
	}


	ConnectionPoint2<struct BaseHotRect,IEventCallback> point;
	ConnectionPoint<struct BaseHotRect,IKeyboardFocus> point2;
	ConnectionPoint<struct BaseHotRect,IHotControlEvent> point3;


	static std::span<const DACOMInterfaceEntry2> GetInterfaceMapOut() {
		static constexpr DACOMInterfaceEntry2 entriesOut[] = {
			{"IEventCallback", [](void* self) -> IDAComponent* {
				auto* doc = static_cast<__dummyBaseHotRect*>(self);
				IDAConnectionPoint* cp = &doc->point;
				return cp;
			}},
			{"IKeyboardFocus", [](void* self) -> IDAComponent* {
				auto* doc = static_cast<__dummyBaseHotRect*>(self);
				IDAConnectionPoint* cp = &doc->point2;
				return cp;
			}},
			{"IHotControlEvent", [](void* self) -> IDAComponent* {
				auto* doc = static_cast<__dummyBaseHotRect*>(self);
				IDAConnectionPoint* cp = &doc->point3;
				return cp;
			}}
		};
		return entriesOut;
	}

	__dummyBaseHotRect (void) : point(0), point2(1), point3(2)
	{
	}
};
//---------------------------------------------------------------------------------
//
struct BaseHotRect : public DAComponentX<__dummyBaseHotRect>
{
	COMPTR<IDAConnectionPoint> parentEventConnection;
	COMPTR<IDAConnectionPoint> parentKeyboardFocusConnection;
	BaseHotRect *	parent;			// parent 

	U32 eventHandle, keyboardFocusHandle;
	bool bHasFocus:1;
	bool bAlert:1;			// mouse is over rect
	bool bInvisible:1;		// control show act invisible
	
	RECT  screenRect;	// inclusive screen coordinates

	//------------------------
	// private data and methods to support PostMessage
	//------------------------
private:

	struct MESSAGE
	{	
		struct MESSAGE *	pNext;
		U32					message;
		void *				param;
	};

	MESSAGE *pMessageList;
	
	GENRESULT ReverseNotifyClients(U32 message, void* param)
	{
		// notify in reverse order for end-of-frame
		for (auto it = point.clients.rbegin(); it != point.clients.rend(); ++it)
		{
			it->client->Notify(message, param);
		}
		return GR_OK;
	}

	BASEHOTRECTEXTERN void sendPostedMessages (MESSAGE * pNode);		// system.cpp
	
	GENRESULT NotifyClients (U32 message, void * param)
	{
		if (message == CQE_ENDFRAME)
			return ReverseNotifyClients(message, param);

		for (auto& node : point.clients)
		{
			if (node.client->Notify(message, param) != GR_OK)
				return GR_GENERIC;
		}
		return GR_OK;
	}

	void removeClient (BaseHotRect *client)
	{
#ifdef _DEBUG
		auto it = std::find_if(point.clients.begin(), point.clients.end(),
			[client](const CONNECTION_NODE2<IEventCallback>& node) {
				return node.client == client;
			});

		CQASSERT(it != point.clients.end() && "BaseHotrect::Cannot remove unknown client");
#endif
		client->Release();
	}
	
	void initializeBaseHotRect (void)
	{
		bHasFocus = 1;
		resPriority = HOTRECT_PRIORITY;

		parentEventConnection->Advise(GetBase(), &eventHandle);
		if (parent)
			parent->SetCallbackPriority(this, EVENT_PRIORITY_HOTRECT);
		if (parentKeyboardFocusConnection)
			parentKeyboardFocusConnection->Advise(GetBase(), &keyboardFocusHandle);
		initializeResources();
	}

public:
	//------------------------
	// public methods
	//------------------------

	BaseHotRect (IDAComponent * systemComponent)
	{
		if(systemComponent)
		{
			systemComponent->QueryOutgoingInterface("IEventCallback", parentEventConnection.addr());
			CQASSERT(parentEventConnection!=0);
		 	systemComponent->QueryOutgoingInterface("IKeyboardFocus", parentKeyboardFocusConnection.addr());
			initializeBaseHotRect();
		}
	}

	BaseHotRect (BaseHotRect * _parent)
	{
		if(_parent)
		{
			parent = _parent;
			CQASSERT(parent);
			parent->GetBase()->QueryOutgoingInterface("IEventCallback", parentEventConnection.addr());
			CQASSERT(parentEventConnection!=0);
	 		parent->QueryOutgoingInterface("IKeyboardFocus", parentKeyboardFocusConnection.addr());
			initializeBaseHotRect();
		}
	}

	// use this if cannot initialize resources at construction time
	//
	void lateInitialize (IDAComponent * systemComponent)
	{
	 	CQASSERT(parentEventConnection==0);	// cannot init twice!
	 	systemComponent->QueryOutgoingInterface("IEventCallback", parentEventConnection.addr());
	 	CQASSERT(parentEventConnection!=0);
	 	systemComponent->QueryOutgoingInterface("IKeyboardFocus", parentKeyboardFocusConnection.addr());
	 	initializeBaseHotRect();
	}

	BASEHOTRECTEXTERN virtual ~BaseHotRect (void);		// system.cpp

	void * operator new (size_t size)
	{
		return HEAP_Acquire()->ClearAllocateMemory(size, "BaseHotRect");
	}

	GENRESULT QueryOutgoingInterface (const C8 *connectionName, struct IDAConnectionPoint **connection)
	{
		return FindConnectionPoint(connectionName, connection);
	}

	IDAComponent * GetBase (void)
	{
		return static_cast<IEventCallback *>(this);
	}

	BOOL32 CheckRect (S32 x, S32 y)
	{
		bAlert = 0;

		if (bHasFocus && bInvisible==false && x >= screenRect.left && x <= screenRect.right && y >= screenRect.top && y <= screenRect.bottom)
			bAlert = 1;

		return bAlert;
	}

	void SetRect (struct tagRECT * rect)
	{
		screenRect = *rect;
	}

	void GetRect (struct tagRECT * rect) const
	{
		*rect = screenRect;
	}

	BOOL32 IsAlert (void) const
	{
		return bAlert;
	}

	GENRESULT SetCallbackPriority (struct IEventCallback * client, U32 priority)
	{
		if (client && MoveToFront(client, priority))
			return GR_OK;
		else
			return GR_INVALID_PARMS;
	}

	BASEHOTRECTEXTERN void PostMessage (U32 message, void * param);		// system.cpp

	virtual void DrawRect (void)
	{
		U32 color = (U32)-1;
		//
		// draw the box
		//
		DA::LineDraw(0, screenRect.left, screenRect.top, screenRect.right, screenRect.top, color);
		DA::LineDraw(0, screenRect.right, screenRect.top, screenRect.right, screenRect.bottom, color);
		DA::LineDraw(0, screenRect.right, screenRect.bottom, screenRect.left, screenRect.bottom, color);
		DA::LineDraw(0, screenRect.left, screenRect.bottom, screenRect.left, screenRect.top, color);
	}

	/* IEventCallback methods */

	DEFMETHOD(Notify) (U32 message, void *param)
	{
		MSG *msg = (MSG *) param;
		GENRESULT genresult = GR_OK;

		switch (message)
		{
		case CQE_DELETE_HOTRECT:
			removeClient((BaseHotRect *)param);
			break;

		case CQE_SLIDER:
		case CQE_LIST_SELECTION:
		case CQE_LIST_CARET_MOVED:
		case CQE_BUTTON:
		case CQE_LHOTBUTTON:
		case CQE_RHOTBUTTON:
			break;
		case CQE_KILL_FOCUS:
		case CQE_SET_FOCUS:
			if (param == this)
				break;
			// fall through intentional
		default:
			genresult = NotifyClients(message, param);	// send the message to clients first
		} // end switch (message)

		switch (message)
		{
		case CQE_UNALERT:
			bAlert = 0;
			break;

		case CQE_KILL_FOCUS:
			if (param != this)
				onSetFocus(false);
			break;

		case CQE_SET_FOCUS:
			if (param != this)
				onSetFocus(true);
			break;

		case CQE_ENDFRAME:
			if (bInvisible==false && DEFAULTS->GetDefaults()->bDrawHotrects)
				DrawRect();
			break;

		case WM_MOUSEMOVE:
			CheckRect(S16(LOWORD(msg->lParam)), S16(HIWORD(msg->lParam)));
			break;

		case WM_LBUTTONDOWN:
			onRequestKeyboardFocus(S16(LOWORD(msg->lParam)), S16(HIWORD(msg->lParam)));
			break;

		case WM_CLOSE:
		case CQE_FLUSHMESSAGES:
			{
				MESSAGE *pNode;

				if ((pNode = pMessageList) != 0)
				{
					pMessageList = 0;
					sendPostedMessages(pNode);
				}
			}
			break;  // end case CQE_UPDATE

		} // end switch (message)

		return genresult;
	}

	/* BaseHotRect methods */

	BOOL32 MoveToFront (struct IEventCallback * res, U32 priority)
	{
		auto it = std::find_if(point.clients.begin(), point.clients.end(),
			[res](const CONNECTION_NODE2<IEventCallback>& node) {
				return node.client == res;
			});

		if (it == point.clients.end())
			return 0;

		// Update priority
		it->priority = priority;

		// Move to front
		auto node = *it;
		point.clients.erase(it);
		point.clients.insert(point.clients.begin(), node);

		// Sort from index 1 onward to find correct insertion point
		// (descending by priority, node is at front with its new priority)
		// Re-find after insert (it's at index 0), then bubble it to correct position
		size_t pos = 0;
		while (pos + 1 < point.clients.size() && point.clients[pos + 1].priority > priority)
		{
			std::swap(point.clients[pos], point.clients[pos + 1]);
			++pos;
		}

		return 1;
	}

	// override this if you want to
	virtual void onSetFocus (bool bFocus)
	{
		if (bFocus == false)
		{
			bAlert = 0;
			bHasFocus = 0;
			desiredOwnedFlags = 0;
			releaseResources();
		}
		else
		{
			bHasFocus = 1;
		}
	}

	// override this if you want to
	virtual void onRequestKeyboardFocus (int x, int y)
	{
		if (bAlert && bInvisible==false && parent && keyboardFocusHandle)
			parent->PostMessage(CQE_KEYBOARD_FOCUS, static_cast<IEventCallback *>(this));
	}
};

//--------------------------------------------------------------------------//
//---------------------------End BaseHotRect.h------------------------------//
//--------------------------------------------------------------------------//
#endif
