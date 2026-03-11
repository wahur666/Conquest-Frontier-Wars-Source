//--------------------------------------------------------------------------//
//                                                                          //
//                              ObjMove.cpp                                 //
//                                                                          //
//                  COPYRIGHT (C) 2000 BY DIGITAL ANVIL, INC.               //
//                                                                          //
//--------------------------------------------------------------------------//
/*
    $Header: /Conquest/App/Src/ObjMove.cpp 55    1/15/02 10:10a Tmauer $
*/			    
//------------------------------- #INCLUDES --------------------------------//
//--------------------------------------------------------------------------//

#include "pch.h"
#include <globals.h>

#include "TObjMove.h"
#include "TObjControl.h"
#include "TObjPhys.h"
#include "TObjTrans.h"
#include "TObjFrame.h"
#include "TObjSelect.h"
#include "TObjMission.h"
#include "UserDefaults.h"
#include "Scripting.h"

#include <DSpaceship.h>
#include <DShipSave.h>

#include <IAnim.h>

typedef SPACESHIP_INIT<BASE_SPACESHIP_DATA> BASESHIPINIT;

//---------------------------------------------------------------------------
//
static inline void getVFXPoints2 (VFX_POINT points[2], const TRANSFORM & transform, SINGLE distance, SINGLE b5)
{
	//
	// calculate points (in clock-wise order) for terrain foot-pad system
	//
	Vector pt, wpt;

	pt.y = 0;
	pt.x = 0;		// _box[0];	// maxx
	pt.z = b5 - distance;		//box[5];	// minz

	wpt = transform.rotate_translate(pt);
	points[0].x = wpt.x;
	points[0].y = wpt.y;

	pt.z = distance;		//box[4];	// maxz

	wpt = transform.rotate_translate(pt);
	points[1].x = wpt.x;
	points[1].y = wpt.y;
}

//---------------------------------------------------------------------------
//
template <class Base>
void ObjectMove<Base>::TakeoverSwitchID (U32 newMissionID)
{
	// first thing, undo the current footprint
	COMPTR<ITerrainMap> map;
	SECTOR->GetTerrainMap(this->systemID, map);
	undoFootprintInfo(map);


	OBJLIST->RemovePartID(this, this->dwMissionID);
	this->dwMissionID = newMissionID;

	OBJLIST->AddPartID(this, this->dwMissionID);

	UnregisterWatchersForObject(this);

	// now set our footprint again
	SetTerrainFootprint(map);
}

//----------------------------------------------------------------------------
//
template <class Base> 
void ObjectMove< Base >::TESTING_shudder (const Vector & dir, SINGLE mag)
{
	if (this->bVisible && fabs(this->transform.get_roll()) < 10 * MUL_DEG_TO_RAD)
	{
		this->restoreDynamicsData();
		mag *= (this->getDynamicsData().angAcceleration*0.12);
		SINGLE angle = this->transform.get_yaw() - TRANSFORM::get_yaw(dir);
		if (angle < -PI)
			angle += PI*2;
		else
		if (angle > PI)
			angle -= PI*2;
		if (angle < 0)
			mag = -mag;
		mag *= 1.0 - ((fabs(fabs(angle)-(MUL_DEG_TO_RAD * 90.0)) / (MUL_DEG_TO_RAD * 90.0)));
		this->ang_velocity += this->transform.get_k() * mag;		//  take 1/3 second to stop velocity
		bRollTooHigh = 1;		// current roll is too much
		tooHighCounter = 0;
	}
}

//----------------------------------------------------------------------------
//------------------------End ObjMove.cpp-------------------------------------
//----------------------------------------------------------------------------
