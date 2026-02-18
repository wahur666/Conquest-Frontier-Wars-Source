#ifndef TCOMPONENTX_H
#define TCOMPONENTX_H
//--------------------------------------------------------------------------//
//                                                                          //
//                              TComponentx.h                               //
//                                                                          //
//               COPYRIGHT (C) 1997 BY DIGITAL ANVIL, INC.                  //
//                                                                          //
//--------------------------------------------------------------------------//
/*
   $Author: Wahur66 $

    Only the Macros are here from TComponent to help the discontinuation

*/
//--------------------------------------------------------------------------//


#ifndef DACOM_H
#include "DACOM.h"
#endif
#include <string_view>

#pragma warning( push )

#pragma warning( disable : 4355 )		// 'this' used during construction

//----------------------------------//

#define daoffsetofclass(base, derived)((U32)(intptr_t)(static_cast<base*>((derived*)8)) - 8)

#define daoffsetofmember(base, member)((U32)(uintptr_t)&(((base *)0)->member))

#define dasizeofmember(base, member)(sizeof(((base *)0)->member))

struct _DACOM_INTMAP_ENTRY
{
	const C8 *interface_name;
	U32 offset;
};

struct DACOMInterfaceEntry2 {
	std::string_view interface_name;
	IDAComponent* (*get)(void* self);
};

//--------------------------------------------------------------------------//
//-------------------------End TComponent.h---------------------------------//
//--------------------------------------------------------------------------//
#endif