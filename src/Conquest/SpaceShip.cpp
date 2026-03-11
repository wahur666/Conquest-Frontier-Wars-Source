//--------------------------------------------------------------------------//
//                                                                          //
//                               SpaceShip.cpp                              //
//                                                                          //
//                  COPYRIGHT (C) 1997 BY DIGITAL ANVIL, INC.               //
//                                                                          //
//--------------------------------------------------------------------------//
/*
    $Header: /Conquest/App/Src/SpaceShip.cpp 390   8/23/01 1:53p Tmauer $
*/			    
//------------------------------- #INCLUDES --------------------------------//
//--------------------------------------------------------------------------//

#include "pch.h"
#include <globals.h>

#include "TSpaceship.h"
#include "CQLight.h"

#include <Engine.h>
#include <TSmartPointer.h>
#include <FileSys.h>
#include <Renderer.h>

#include <stdlib.h>

#pragma warning (disable : 4660)


//---------------------------------------------------------------------------
//
#define MAXDAMAGECHANGE(x)  F2LONG(1+ (x / (5 * REALTIME_FRAMERATE)))
#define MAXSUPPLYCHANGE(x)  F2LONG(1+ (x / (1 * REALTIME_FRAMERATE)))


//---------------------------------------------------------------------------
//
/*template <class SaveStruct, class InitStruct>
void SpaceShip<SaveStruct,InitStruct>::PreRender (void)
{
	if (bVisible)
	{
		FRAME_preRender();
	}
}*/

//---------------------------------------------------------------------------
//
/*template <class SaveStruct, class InitStruct>
void SpaceShip<SaveStruct,InitStruct>::PostRender (void)
{
	if (bVisible)
	{
		FRAME_postRender();
	}
}*/

//-------------------------------------------------------------------
//
bool arch_callback( ARCHETYPE_INDEX parent_arch_index, ARCHETYPE_INDEX child_arch_index, void *user_data )
{
	float *rad = (float *)user_data;
	float fp_radius;

	float local_box[6];
	REND->get_archetype_bounding_box(child_arch_index,1.0,local_box);
	fp_radius = __max(local_box[BBOX_MAX_X],-local_box[BBOX_MIN_X]),
	fp_radius = __max(fp_radius,local_box[BBOX_MAX_Z]);
	fp_radius = __max(fp_radius,-local_box[BBOX_MIN_Z]);
	fp_radius *= 2;
	*rad = __max(fp_radius,*rad);

	return true;
}
//---------------------------------------------------------------------------
//------------------------End SpaceShip.cpp----------------------------------
//---------------------------------------------------------------------------
