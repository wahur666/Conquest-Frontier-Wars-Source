#ifndef TCONNCONTAINER_H
#define TCONNCONTAINER_H
//--------------------------------------------------------------------------//
//                                                                          //
//                               TConnContainer.h                           //
//                                                                          //
//                  COPYRIGHT (C) 1997 BY DIGITAL ANVIL, INC.               //
//                                                                          //
//--------------------------------------------------------------------------//
/*

    $Author: Rmarr $
*/			    
//--------------------------------------------------------------------------//

#include <string_view>
#include "IConnection.h"
#include "vtdump.h"


//-----------------------------------------

template <class Type> struct DACOM_NO_VTABLE ConnectionPointContainer : public IDAConnectionPointContainer
{
	DEFMETHOD(FindConnectionPoint) (const C8 *connectionName, struct IDAConnectionPoint **connPoint);

	DEFMETHOD_(BOOL32,EnumerateConnectionPoints) (CONNCONTAINER_ENUM_PROC proc, void *context=0);
};

//-----------------------------------------
//
template <class Type>
GENRESULT ConnectionPointContainer<Type>::FindConnectionPoint(
	const C8* connectionName,
	IDAConnectionPoint** connPoint)
{
	if (!connectionName || !connPoint)
		return GR_INVALID_PARAM;

	const auto map = Type::GetInterfaceMapOut();
	std::string_view name {connectionName};
	for (const auto& entry : map)
	{
		if (entry.interface_name != name)
			continue;
		auto* self = static_cast<Type*>(this);  // adjust to Document*
		auto* point = static_cast<IDAConnectionPoint*>(entry.get(self));
		*connPoint = point;
		point->AddRef();
		return GR_OK;
	}

	*connPoint = nullptr;
	return GR_INTERFACE_UNSUPPORTED;
}
//-----------------------------------------
//
template <class Type>
BOOL32 ConnectionPointContainer<Type>::EnumerateConnectionPoints(CONNCONTAINER_ENUM_PROC proc, void* context)
{
	if (!proc)
		return FALSE;

	const auto map = Type::GetInterfaceMapOut();

	for (const auto& entry : map)
	{
		auto* point =
			static_cast<IDAConnectionPoint*>(entry.get(this));

		if (!proc(this, point, context))
			return FALSE;
	}

	return TRUE;
}

//--------------------------------------------------------------------------//
//------------------------------End TConnContainer.h------------------------//
//--------------------------------------------------------------------------//
#endif