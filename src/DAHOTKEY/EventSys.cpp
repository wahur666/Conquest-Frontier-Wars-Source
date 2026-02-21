//--------------------------------------------------------------------------//
//                                                                          //
//                               EventSys.cpp                               //
//                                                                          //
//                  COPYRIGHT (C) 1997 BY DIGITAL ANVIL, INC.               //
//                                                                          //
//--------------------------------------------------------------------------//
/*

    $Author: Pbleisch $
*/			    
//--------------------------------------------------------------------------//


#define WIN32_LEAN_AND_MEAN
#include <span>
#include <windows.h>

#include "EventSys2.h"

#include "TComponent.h"
#include "TConnContainer.h"
#include "TConnPoint.h"
#include "System.h"
#include "da_heap_utility.h"

//--------------------------------------------------------------------------//
//---------------------------Local structures-------------------------------//
//--------------------------------------------------------------------------//

struct MESSAGE_NODE
{
	struct MESSAGE_NODE *	pNext;
	U32						message;
	void *					param;
};

static char interface_name[] = "IEventSystem";


//--------------------------------------------------------------------------//
//--------------------------EventSystem Class definition--------------------//
//--------------------------------------------------------------------------//


struct DACOM_NO_VTABLE EventSystem : public ISystemComponent, 
											IEventSystem, 
											IEventCallback,
											ConnectionPointContainer<EventSystem>
{
	BEGIN_DACOM_MAP_INBOUND(EventSystem)
	DACOM_INTERFACE_ENTRY(IEventSystem)
	DACOM_INTERFACE_ENTRY(ISystemComponent)
	DACOM_INTERFACE_ENTRY(IAggregateComponent)
	DACOM_INTERFACE_ENTRY(IEventCallback)
	DACOM_INTERFACE_ENTRY(IDAConnectionPointContainer)
	DACOM_INTERFACE_ENTRY2(IID_IEventSystem,IEventSystem)
	DACOM_INTERFACE_ENTRY2(IID_ISystemComponent,ISystemComponent)
	DACOM_INTERFACE_ENTRY2(IID_IAggregateComponent,IAggregateComponent)
	DACOM_INTERFACE_ENTRY2(IID_IEventCallback,IEventCallback)
	DACOM_INTERFACE_ENTRY2(IID_IDAConnectionPointContainer,IDAConnectionPointContainer)
	END_DACOM_MAP()


	static void *operator new(size_t size);

	static void operator delete(void *ptr);

	GENRESULT init (AGGDESC * info);

	MESSAGE_NODE *pFreeList;
	MESSAGE_NODE *pShadowList, *pShadowListEnd;
	MESSAGE_NODE *pMessageList, *pMessageListEnd;
	DWORD dwLastPeek;
	MESSAGE_NODE *pPeekNode;
	BOOL32 bInitialized;
	ConnectionPoint<EventSystem, IEventCallback> eventCallback;

	EventSystem (void) : eventCallback(0)
	{
	}

	~EventSystem (void);
	
	/* IEventSystem members */

	DEFMETHOD(Initialize) (U32 flags);

	DEFMETHOD(Post) (U32 message, void *param = 0);

	DEFMETHOD(Send) (U32 message, void *param = 0);

	DEFMETHOD(Peek) (U32 index, U32 *message = 0, void **param = 0);

	/* IAggregateComponent members */

    DEFMETHOD(Initialize) (void);
	
	/* ISystemComponent members */

    DEFMETHOD_(void,Update) (void);

	/* IEventCallback members */
	
	DEFMETHOD(Notify) (U32 message, void *param = 0)
	{
		return Post(message, param);
	}

	/* EventSystem members */

	IDAComponent * GetBase(void)
	{
		return static_cast<ISystemComponent *>(this);
	}

	static std::span<const DACOMInterfaceEntry2> GetInterfaceMapOut() {
		static constexpr DACOMInterfaceEntry2 entriesOut[] = {
			{"IEventCallback", [](void* self) -> IDAComponent* {
				auto* doc = static_cast<EventSystem*>(self);
				IDAConnectionPoint* cp = &doc->eventCallback;
				return cp;
			}}
		};
		return entriesOut;
	}
};

DA_HEAP_DEFINE_NEW_OPERATOR(EventSystem)


//--------------------------------------------------------------------------//
//
GENRESULT EventSystem::init (AGGDESC * info)
{
	if (info->description && strcmp(info->description, "EventSystem"))
		return GR_INTERFACE_UNSUPPORTED;
	return GR_OK;
}
//--------------------------------------------------------------------------//
//
EventSystem::~EventSystem (void)
{
	MESSAGE_NODE *pTmp;

	while (pFreeList)
	{
		pTmp = pFreeList->pNext;
		delete pFreeList;
		pFreeList = pTmp;
	}
	while (pShadowList)
	{
		pTmp = pShadowList->pNext;
		delete pShadowList;
		pShadowList = pTmp;
	}
	while (pMessageList)
	{
		pTmp = pMessageList->pNext;
		delete pMessageList;
		pMessageList = pTmp;
	}
}
//--------------------------------------------------------------------------//
//
GENRESULT EventSystem::Initialize (U32 flags)
{
	if (flags)
		return GR_GENERIC;
	return GR_OK;	// no flags implemented yet
}
//--------------------------------------------------------------------------//
//
GENRESULT EventSystem::Post (U32 message, void *param)
{
	GENRESULT result = GR_OK;
	MESSAGE_NODE *pNode;

	if ((pNode = pFreeList) != 0)
		pFreeList = pNode->pNext;
	else
	if ((pNode = (MESSAGE_NODE *) DA_HEAP_MALLOC( HEAP, sizeof(MESSAGE_NODE), "IEventSystem::Message cell" ) ) == 0)
	{
		result = GR_OUT_OF_MEMORY;
		goto Done;
	}

	pNode->pNext = 0;
	pNode->message = message;
	pNode->param = param;

	if (pShadowListEnd)
	{
		pShadowListEnd->pNext = pNode;
		pShadowListEnd = pNode;
	}
	else	// empty list
	{
		pShadowList = pShadowListEnd = pNode;
	}

Done:
	return result;
}
//--------------------------------------------------------------------------//
//
GENRESULT EventSystem::Send (U32 message, void *param)
{
	//------------------------------
	// service all of the callbacks
	//------------------------------
	for (const auto client	: eventCallback.clients) {
		client->Notify(message, param);
	}
	return GR_OK;
}
//--------------------------------------------------------------------------//
//
GENRESULT EventSystem::Peek (U32 index, U32 *message, void **param)
{
	GENRESULT result = GR_GENERIC;
	MESSAGE_NODE *pNode;
	U32 lastPeek = dwLastPeek;

	if ((pNode = pPeekNode) == 0 || index < lastPeek)
	{
		if ((pNode = pMessageList) == 0)
			goto Done;
		lastPeek = 0;
	}

	while (lastPeek < index)
	{
		if ((pNode = pNode->pNext) == 0)
			goto Done;
		lastPeek++;
	}
	
	if (message)
		*message = pNode->message;
	if (param)
		*param = pNode->param;
	// save these values for next time
	pPeekNode = pNode;
	dwLastPeek = lastPeek;

	result = GR_OK;
Done:
	return result;
}
//--------------------------------------------------------------------------//
//
GENRESULT EventSystem::Initialize (void)
{
	bInitialized = 1;
	return GR_OK;
}
//--------------------------------------------------------------------------//
//
void EventSystem::Update (void)
{
	MESSAGE_NODE *pTmp = pShadowList;

	//------------------------------
	// service all of the callbacks
	//------------------------------
	
	if (!eventCallback.clients.empty())
	{
		for (auto* tmp = pTmp; tmp; tmp = tmp->pNext)
		{
			for (auto* client : eventCallback.clients)
			{
				client->Notify(tmp->message, tmp->param);
			}
		}
	}

	// put everyone in the message list into the free list

	if (pMessageListEnd)
	{
		pMessageListEnd->pNext = pFreeList;
		pFreeList = pMessageList;
	}

	pMessageList = pShadowList;
	pMessageListEnd = pShadowListEnd;

	pShadowList = pShadowListEnd = 0;
	dwLastPeek = 0;
	pPeekNode = 0;
}
//--------------------------------------------------------------------------//
//--------------------------------------------------------------------------//
//--------------------------------------------------------------------------//
//
void RegisterTheEventSystem (ICOManager *DACOM)
{
	IComponentFactory *pServer = new DAComponentFactory2<DAComponentAggregate<EventSystem>, AGGDESC> (interface_name);

	if (pServer)
	{
		DACOM->RegisterComponent(pServer, interface_name);
		pServer->Release();
	}
}


//--------------------------------------------------------------------------//
//---------------------------------EventSys.cpp-----------------------------//
//--------------------------------------------------------------------------//
