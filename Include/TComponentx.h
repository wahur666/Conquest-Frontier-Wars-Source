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

#pragma warning( push )

#pragma warning( disable : 4355 )		// 'this' used during construction

//----------------------------------//

#define daoffsetofclass(base, derived) ((U32)(static_cast<base*>((derived*)8))-8)
#define daoffsetofmember(base, member) ((U32)offsetof(base, member))
#define dasizeofmember(base,member) (size_t)((&(((base *)0)->member))+1)-(size_t)((&(((base *)0)->member))+0)

struct _DACOM_INTMAP_ENTRY
{
	const C8 *interface_name;
	U32 offset;
};

//--------------------------------------------------------------------------//
//-------------------------End TComponent.h---------------------------------//
//--------------------------------------------------------------------------//
#endif