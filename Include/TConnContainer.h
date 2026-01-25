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


#ifndef ICONNECTION_H
#include "IConnection.h"
#endif

#ifndef TCOMPONENT_H
#include "TComponent.h"
#endif


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
	const auto* array = Type::_GetEntriesOut();

	for (int i = 0; array[i].interface_name; i++) {
		if (strcmp(array[i].interface_name, connectionName) != 0)
			continue;

		// Use std::bit_cast or just pointer arithmetic properly
		auto* point = reinterpret_cast<IDAConnectionPoint*>(
			reinterpret_cast<std::uintptr_t>(this) + array[i].offset
		);

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
BOOL32 ConnectionPointContainer< Type >::EnumerateConnectionPoints (CONNCONTAINER_ENUM_PROC proc, void *context)
{
	BOOL32 result=1;
	const _DACOM_INTMAP_ENTRY *array = Type::_GetEntriesOut();
	int i;
	
	for (i = 0; array[i].interface_name && result; i++)
	{
		IDAConnectionPoint *point;

		point = (IDAConnectionPoint *) ( ((U32)this) + array[i].offset - ((U32)
					(static_cast<ConnectionPointContainer<Type>*>((Type*)8) )
					-8) );
		
		result = proc(this, point, context);
	}


	return result;
}


//--------------------------------------------------------------------------//
//------------------------------End TConnContainer.h------------------------//
//--------------------------------------------------------------------------//
#endif