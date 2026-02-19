#ifndef TCONNPOINT_H
#define TCONNPOINT_H
//--------------------------------------------------------------------------//
//                                                                          //
//                                TConnPoint.h                              //
//                                                                          //
//                  COPYRIGHT (C) 1997 BY DIGITAL ANVIL, INC.               //
//                                                                          //
//--------------------------------------------------------------------------//
/*

    $Author: Rmarr $
*/			    
//--------------------------------------------------------------------------//

#ifndef ICONNECTION_H
#include "IConnection.h"
#endif

#ifndef TCOMPONENT_H
#include "TComponent.h"
#endif


//--------------------------------------------------------------------------
//  
template <class Type=IDAComponent> 
struct CONNECTION_NODE
{
	CONNECTION_NODE<Type> *pNext;
	Type *client;
};

template <class Base, class Type=IDAComponent> 
struct ConnectionPoint : public IDAConnectionPoint
{
	std::vector<Type*> clients;
	int index;

	ConnectionPoint (int i) : index(i), clients() {}

	~ConnectionPoint (void) = default;
	
	/* IDAComponent members */

	DEFMETHOD(QueryInterface) (const C8 *interface_name, void **instance);

	DEFMETHOD_(U32,AddRef) (void);

	DEFMETHOD_(U32,Release) (void);

	/* IDAConnectionPoint members */

	DEFMETHOD_(U32,GetOutgoingInterface) (C8 *interfaceName, U32 bufferLength);

	DEFMETHOD(GetContainer) (IDAConnectionPointContainer **container);

	DEFMETHOD(Advise) (IDAComponent *component, U32 *handle);

	DEFMETHOD(Unadvise) (U32 handle);

	DEFMETHOD_(BOOL32,EnumerateConnections) (CONNECTION_ENUM_PROC proc, void *context=0);

	/* ConnectionPoint members */

};
//--------------------------------------------------------------------------//
//
template <class Base, class Type>
GENRESULT ConnectionPoint<Base,Type>::QueryInterface (const C8 *interface_name, void **instance)
{
	*instance = 0;
	return GR_INTERFACE_UNSUPPORTED;
}
//--------------------------------------------------------------------------//
//
template <class Base, class Type>
U32 ConnectionPoint<Base,Type>::AddRef (void)
{
	return 1;
}
//--------------------------------------------------------------------------//
//
template <class Base, class Type>
U32 ConnectionPoint<Base,Type>::Release (void)
{
	return 1;
}
//--------------------------------------------------------------------------//
//
template <class Base, class Type>
U32 ConnectionPoint<Base, Type>::GetOutgoingInterface(char* buffer, U32 bufferLength)
{
	auto entries = Base::GetInterfaceMapOut();
	if (index >= entries.size())
		return 0;

	auto name = entries[index].interface_name;
	U32 len = std::min(bufferLength - 1, static_cast<U32>(name.size()));
	memcpy(buffer, name.data(), len);
	buffer[len] = '\0';
	return len;
}
//--------------------------------------------------------------------------//
//
template <class Base, class Type>
GENRESULT ConnectionPoint<Base, Type>::GetContainer(IDAConnectionPointContainer **container)
{
	auto entries = Base::GetInterfaceMapOut();
	if (index >= entries.size())
		return GR_INVALID_PARAM;

	*container = static_cast<IDAConnectionPointContainer*>(entries[index].get(this));
	(*container)->AddRef();
	return GR_OK;
}
//--------------------------------------------------------------------------//
//
template <class Base, class Type>
GENRESULT ConnectionPoint< Base,Type >::Advise (IDAComponent *component, U32 *handle)
{
	if (!component || !handle)
		return GR_INVALID_PARAM;

	Type* client = nullptr;

	const auto map = Base::GetInterfaceMapOut();
	const auto& entry = map[index];
	auto iname = std::string(entry.interface_name);
	if (component->QueryInterface(iname.c_str(), reinterpret_cast<void**>(&client)) != GR_OK)
		return GR_GENERIC;

	// We only needed validation. Drop the ref immediately.
	client->Release();

	try
	{
		clients.push_back(client);

		// handle = index+1 (0 reserved as invalid)
		*handle = static_cast<U32>(clients.size());
		return GR_OK;
	}
	catch (...)
	{
		return GR_OUT_OF_MEMORY;
	}
}
//--------------------------------------------------------------------------//
//
template <class Base, class Type>
GENRESULT ConnectionPoint< Base,Type >::Unadvise (U32 handle)
{
	if (handle == 0 || handle > clients.size())
		return GR_GENERIC;

	const size_t idx = handle - 1;

	clients.erase(clients.begin() + idx);

	return GR_OK;
}
//--------------------------------------------------------------------------//
//
template <class Base, class Type>
BOOL32 ConnectionPoint< Base,Type >::EnumerateConnections (CONNECTION_ENUM_PROC proc, void *context)
{
	for (auto* c : clients) {
		if (!proc(this, c, context))
			return FALSE;
	}
	return !clients.empty();
}
//--------------------------------------------------------------------------//
//----------------------------End TConnPoint.h------------------------------//
//--------------------------------------------------------------------------//
#endif
