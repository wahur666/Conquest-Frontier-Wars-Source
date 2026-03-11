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

// TODO:  OnMasterChange():  If moveActive() and pathlength==0, complete the move.
// TODO:  Move Patrol to a separate sync function
// TODO:  no autoattack for gunboat when bRecallFighters==true


//---------------------------------------------------------------------------
//
template <class Base>
bool ObjectMove<Base>::testPassible (const struct GRIDVECTOR & pos)
{
	TestPassibleCallback callback;
	COMPTR<ITerrainMap> map;
	bool result;

	SECTOR->GetTerrainMap(this->systemID, map);

	if ((result=map->TestSegment(GetGridPosition(), pos, &callback)) == false)
		result = callback.gridPos.isMostlyEqual(pos);

	return result;
}
//---------------------------------------------------------------------------
//
static bool bUpdateMoveState = true;

template <class Base> 
BOOL32 ObjectMove<Base>::updateMoveState (void)
{
	if(!bUpdateMoveState)
		return 1;
	bool bNoDynamics = 0;
	bool bOrigAutoMovement = isAutoMovementEnabled();

	if (overrideMode != OVERRIDE_NONE)
	{
		IBaseObject * obj = OBJLIST->FindObject(overrideAttackerID);
		if (obj == 0 || obj->GetSystemID() != this->systemID)
		{
			if (overrideMode == OVERRIDE_PUSH)
				cancelPush();
			overrideAttackerID = 0;
			overrideMode = OVERRIDE_NONE;
		}
	}

	if (this->bMoveDisabled==0 && this->effectFlags.canMove())
	{
		if (isAutoMovementEnabled() && this->bReady)
		{
			bNoDynamics=0;
			//
			// set the correct velocity
			//
			if (this->bExploding==0)
			{
				this->restoreDynamicsData();

				SINGLE maxV = this->maxVelocity;
				
				if(slowMove && (moveAgentID || jumpAgentID)) //find the slowest guy in the group.
				{
					const ObjSet * set = NULL;
					if(moveAgentID)
						THEMATRIX->GetOperationSet(moveAgentID,&set);
					else
						THEMATRIX->GetOperationSet(jumpAgentID,&set);
					if(set)
					{
						for(U32 i = 0; i < set->numObjects; ++i)
						{
							IBaseObject * obj = OBJLIST->FindObject(set->objectIDs[i]);
							if(obj)
							{
								MPart part(obj);
								if(part.isValid())
								{
									if(part->maxVelocity < maxV)
										maxV = part->maxVelocity;
								}
							}
						}
					}
				}

				SINGLE fleetMod = 1.0;
				if(this->fleetID)
				{
					VOLPTR(IAdmiral) flagship;
					OBJLIST->FindObject(this->fleetID,TOTALLYVOLATILEPTR,flagship,IAdmiralID);
					if(flagship.Ptr())
					{
						MPart part(this);
						fleetMod = 1 + flagship->GetSpeedBonus(this->mObjClass, part.pInit->armorData.myArmor);
					}				
				}
				SINGLE sectorMod = 1.0 + SECTOR->GetSectorEffects(this->playerID,this->systemID)->getSpeedMod();
				setCruiseSpeed(maxV*completionModifier()*this->fieldFlags.getSpeedModifier()*this->effectFlags.getSpeedModifier()*fleetMod*sectorMod);

				if (isMovingToJump())
					doJumpPreparation();
				else
				if (isMoveActive())
					doPathMove();
				else
				{
					if (this->bVisible && LODPERCENT != 0)
						rockTheBoat();
					else
					if (overrideMode == OVERRIDE_NONE)
					{
						bNoDynamics=1;
						this->velocity.zero();
						this->ang_velocity.zero();
						this->DEBUG_resetInputCounter();
					}
					else
					{
						rotateShip(0, 0-this->transform.get_roll(), 0-this->transform.get_pitch());
						setAltitude(0);
					}
				}
			}
		}
		else
		if (this->bReady)				// let rotates happen when not on screen
		{
			this->restoreDynamicsData();
			bNoDynamics=0;
		}
		else
		{
			this->DEBUG_resetInputCounter();
		}
	}
	else if(((!this->effectFlags.bDestabilizer) && (this->effectFlags.bStasis)) || this->bMoveDisabled) // if (hPart->moveAbility==0)
	{
		bNoDynamics=1;
		this->velocity.zero();
		this->DEBUG_resetInputCounter();
		if(this->effectFlags.canMove())
			this->ang_velocity.zero();
		if (this->effectFlags.canMove() && goalPosition!=currentPosition && THEMATRIX->IsMaster()==0)
		{
			SetPosition(goalPosition, this->systemID);
			currentPosition = goalPosition;
		}
	}

	if (bOrigAutoMovement && this->bReady && this->bMoveDisabled==0 && overrideMode != OVERRIDE_NONE)
	{
		switch (overrideMode)
		{
		case OVERRIDE_PUSH:
			this->restoreLinearDynamicsData();
			this->setAbsOverridePosition(overrideDest, overrideSpeed);
			bNoDynamics = 0;
			break;
		case OVERRIDE_DESTABILIZE:
			this->setDestabilize();
			bNoDynamics = (this->bVisible==0 || LODPERCENT == 0);
			break;
		case OVERRIDE_ORIENT:
			this->overrideAbsShipRotate(overrideYaw, 0, 0);
			bNoDynamics = 0;
			break;
		}
	}

	this->bEnablePhysics = (this->bReady && bNoDynamics==0);
	if (this->bEnablePhysics && !this->bExploding)
	{
		// update the map with our latest location
		// we may have moved outside our square because of various external forces,
		// or because we are pathfinding to a new location.
		COMPTR<ITerrainMap> map;
		SECTOR->GetTerrainMap(this->systemID, map);
		if (map)
			SetTerrainFootprint(map);
	}

	bAutoMovement = true;		// always comes back on by default, must be turned off every frame

	return 1;
}
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
//--------------------------------------------------------------------------//
// actual movement code
//--------------------------------------------------------------------------//
//
template <class Base> 
bool ObjectMove<Base>::doMove (const Vector & goalPos)
{
	Vector pos = this->transform.get_position();
	SINGLE relYaw, relRoll, relPitch;
	SINGLE yaw   = this->transform.get_yaw();
	SINGLE roll  = this->transform.get_roll();
	SINGLE pitch = this->transform.get_pitch();
	bool bContinuing = (bPathOverflow || pathLength > 1);
	Vector goal=goalPos;

	bRotatingBeforeMove = false;

	goal -= pos;

	goal.z = 0;
	const SINGLE goalMag = goal.magnitude();

	relYaw = this->fixAngle(get_angle(goal.x, goal.y) - yaw);

#if 1
	if (goalMag < this->boxRadius)
	{
		bool result;

		relRoll = -roll;
		goal.z = cruiseDepth - this->transform.translation.z;
		if (bContinuing)
			result = (setPosition(goal, this->getDynamicsData().maxLinearVelocity) != 0);
		else
			result = (setPosition(goal) != 0);
		
		SINGLE sidleDist = dot_product(goal, this->transform.get_i());
		if (fabs(sidleDist) > this->getDynamicsData().maxLinearVelocity)
		{
			if (sidleDist < 0)
				relRoll -= 5.0 * MUL_DEG_TO_RAD;
			else
			if (sidleDist > 0)
				relRoll += 5.0 * MUL_DEG_TO_RAD;
		}

		goal.z = 0;
		if (goalMag < this->box[4]-this->box[5])		// don't spin if very near goal
			relYaw = 0;
		rotateShip(relYaw, relRoll, 0 - pitch);
		return result;
	}
#endif

	if (fabs(relYaw) < 60.0 * MUL_DEG_TO_RAD)
	{
		if (bContinuing)
		{
			setPosition(goal, this->maxLinearVelocity);
			setThrustersOn();
		}
		else
		{
			SINGLE coast;

			coast = this->calculateCoastingDistance(this->getDynamicsData().maxLinearVelocity, this->getDynamicsData().linearAcceleration, ELAPSED_TIME);

			if (coast < goalMag)	// not there yet
			{
				setPosition(goal, this->maxLinearVelocity);
				setThrustersOn();
			}
			else
			{
				setPosition(goal);
			}
		}

		setAltitude(cruiseDepth - this->transform.translation.z);
		if (cruiseDepth - this->transform.translation.z < -800)
			relPitch = (-10.0 * MUL_DEG_TO_RAD) - pitch;	// pitch down
		else
			relPitch = 0 - pitch;
	}
	else
	{
		if (this->bVisible)
		{
			Vector diff=this->velocity/2;
			diff.z = cruiseDepth - this->transform.translation.z;

			setPosition(diff);
		}
		else
			setAltitude(cruiseDepth - this->transform.translation.z);
		relPitch = (5.0 * MUL_DEG_TO_RAD) - pitch;
		bRotatingBeforeMove = true;
	}

	relRoll = 0 - roll;
	if (relRoll < -PI)
		relRoll += PI*2;
	else
	if (relRoll > PI)
		relRoll -= PI*2;

	if (fabs(relYaw) > 10.0 * MUL_DEG_TO_RAD)
	{
		if (relYaw < 0)
			relRoll -= 5.0 * MUL_DEG_TO_RAD;	// roll to the left
		else
			relRoll += 5.0 * MUL_DEG_TO_RAD;	// roll to the right
	}

	rotateShip(relYaw, relRoll, relPitch);

	if (goalMag < -this->box[5])
	{
		// return early if we are not at the last stop (cut corners)
		if (pathLength>1 || bPathOverflow || this->velocity.magnitude() < 5.0)
			return true;
	}
	return false;
}			  

//---------------------------------------------------------------------------
//
template <class Base> 
bool ObjectMove<Base>::fleetRotationCheck (void)
{
	if(this->fleetID)
	{
		VOLPTR(IAdmiral) flagship;
		OBJLIST->FindObject(this->fleetID,TOTALLYVOLATILEPTR,flagship,IAdmiralID);
		if(flagship.Ptr())
		{
			if(flagship->IsInLockedFormation())
			{
				flagship->MoveDoneHint(this);
				bMockRotate = true;
				bFinalMove = 1;
				COMPTR<ITerrainMap> map;

				SECTOR->GetTerrainMap(this->systemID, map);
				GRIDVECTOR from = GetGridPosition();
				SetPath(map, &from, 1);

				// calculate the mock rotate angle
				Vector vecDiff = flagship->GetFormationDir();
				mockRotationAngle =  TRANSFORM::get_yaw(vecDiff);

				return false;
			}
		}
	}
	return true;
}
//---------------------------------------------------------------------------
//
template <class Base> 
bool ObjectMove<Base>::doPathMove (void)
{
	if (isMoveActive() && pathLength >= 1)
	{
		CQASSERT(pathLength >= 1);
		bool bNext = ((bPathOverflow || pathLength > 1) && pathList[pathLength-1].isMostlyEqual(GRIDVECTOR::Create(this->transform.translation)));
		if (bMockRotate)
		{
			// rotate to position, if already there call onPathComplete
			SINGLE relYaw = mockRotationAngle - this->transform.get_yaw();
			Vector relPos = pathList[pathLength-1]+slopOffset;
			relPos -= this->transform.translation;

			if (relYaw < -PI)
				relYaw += PI*2;
			else
			if (relYaw > PI)
				relYaw -= PI*2;
			
			BOOL32 bMoveResult = setPosition(relPos);

			if (bMoveResult && fabs(relYaw) <  MUL_DEG_TO_RAD * 20)
			{
				onPathComplete();
			}
			else
			{
				rotateShip(relYaw, 0, 0);
			}
			
		}
		else if (doMove(pathList[pathLength-1]+slopOffset) || bNext)
		{
			currentPosition = pathList[--pathLength];

			if (pathLength == 0 && bFinalMove)
			{
				if(fleetRotationCheck())
					onPathComplete();
			}
			else
			if (pathLength<=1 && (bFinalMove==false||bPathOverflow))
			{
				COMPTR<ITerrainMap> map;
				GRIDVECTOR from = GetGridPosition();

				SECTOR->GetTerrainMap(this->systemID, map);
				U32 flags = bHalfSquare ? TERRAIN_FP_HALFSQUARE : TERRAIN_FP_FULLSQUARE; 
				if (bPathOverflow==0)	// did we receive the full path last time?
					bFinalMove = 1;
				else
					calcSlopOffset();
				if (bFinalMove)
					flags |= TERRAIN_FP_FINALPATH;

				#if (defined(_JASON) || defined(_SEAN))
					bool bParked = map->IsParkedAtGrid(from, dwMissionID, bHalfSquare==0);
				#endif

				if (map->FindPath(from, goalPosition, this->dwMissionID, flags, this) == 0)
				{
					if(fleetRotationCheck())
						onPathComplete();

					#if (defined(_JASON) || defined(_SEAN))
					if (bParked == false)
						CQBOMB1("TerrainMap returned pathlength==0, but \"%s\" isn't parked there! (Ignorable)", (char *)partName);
					#endif
		//			CQASSERT(bNext==false);		// we ended our move early, cannot exit path here!
				}
			}
		}
	}
	else
	{
		// on the client, we may complete the move before the host, so pathLength may be zero for a time
		if (isMoveActive() && pathLength==0 && THEMATRIX->IsMaster())	// can happen after master change
			onPathComplete();
		else
		{
			rotateShip(0,0,0);
			setAltitude(0);
		}
	}
	return isMoveActive()==0;
}
//---------------------------------------------------------------------------
//
template <class Base> 
void ObjectMove<Base>::doJumpPreparation (void)
{
	// force ship to stay put, by default
	rotateShip(0,0,0);
	setAltitude(0);
	// check bRecallFighters here, so that we don't actually move to a grid
	// position while we are waiting to assemble.
	bool bMoveResult = (isMoveActive() == 0) || (this->bRecallFighters == true) || doPathMove();

	if ((this->bRecallFighters == false) && (bMoveResult == false))
	{
		// first stage, moving towards jumpgate
		// do a distance check and a line-of-sight
		GRIDVECTOR gridpos = GetGridPosition();
		SINGLE fdist = gridpos - jumpToPosition;
		if (fdist < __min(3, this->sensorRadius))
		{
			// we are less than 3 grid units away
			if (testPassible(jumpToPosition) == true)
			{
				resetMoveVars();
				this->bRecallFighters = true;
			}
		}
	}
	else if (bMoveResult == false)
	{
		// second stage, moving to a settling place
		// nothing yet...
	}
	else 	// not moving into position
	{
		if (testPassible(jumpToPosition) == false)
		{
			if (THEMATRIX->IsMaster())
			{
				resetMoveVars();
				THEMATRIX->SendOperationData(jumpAgentID, this->dwMissionID, &currentPosition, sizeof(currentPosition));
				THEMATRIX->OperationCompleted2(jumpAgentID, this->dwMissionID);
				bSyncNeeded = 0;
				this->bRecallFighters = false;
				THEMATRIX->FlushOpQueueForUnit(this);
				if (CQFLAGS.bTraceMission)
					CQTRACE11("Jump failed for unit \"%s\"", (char *)this->partName);
			}
		}
		else
		{
			this->bRecallFighters = true;

			GRIDVECTOR goal_pos  = jumpToPosition;

			if (goal_pos.cornerpos() - GetGridPosition() < 1)
			{
				// too close to rotate
				bMoveResult = true;
			}
			else
			{
				Vector goal = goal_pos.cornerpos() - this->transform.translation;
				SINGLE relYaw = get_angle(goal.x, goal.y) - this->transform.get_yaw();
				if (relYaw < -PI)
					relYaw += PI*2;
				else
				if (relYaw > PI)
					relYaw -= PI*2;

				rotateShip(relYaw, 0 - this->transform.get_roll(), 0 - this->transform.get_pitch());

				bMoveResult = (fabs(relYaw) < 15 * MUL_DEG_TO_RAD);
			}
			if (bMoveResult)
				bMoveResult = testReadyForJump();

			if (bMoveResult && THEMATRIX->IsMaster())
			{
				resetMoveVars();
				THEMATRIX->SendOperationData(jumpAgentID, this->dwMissionID, &currentPosition, sizeof(currentPosition));
				THEMATRIX->OperationCompleted2(jumpAgentID, this->dwMissionID);
				bSyncNeeded = 0;
			}
		}
	}
}

//---------------------------------------------------------------------------
//
template <class Base> 
ObjectMove<Base>::~ObjectMove (void)
{
	if (footprintHistory.numEntries>0)
	{
		COMPTR<ITerrainMap> map;

		if (SECTOR)
		{
			SECTOR->GetTerrainMap(this->systemID, map);
			if (map)
				undoFootprintInfo(map);
		}
	}
	if (OBJMAP && map_sys && map_sys <= MAX_SYSTEMS)
		OBJMAP->RemoveObjectFromMap(this,map_sys,map_square);
}
//---------------------------------------------------------------------------
//
template <class Base> 
void ObjectMove<Base>::rockTheBoat (void)
{
	SINGLE relRoll, relAlt;

	SINGLE roll  = this->transform.get_roll();
	SINGLE pitch = this->transform.get_pitch();

	if (bRollTooHigh)
	{
		this->restoreDynamicsData();

		this->bEnablePhysics = 1;
		bool rotresult = rotateShip(0, 0 - roll, 0 - pitch);
		if (++tooHighCounter > U8(REALTIME_FRAMERATE+1) && rotresult)
			bRollTooHigh = 0;
		/*
		if (bAltUp)
			relAlt = rockingData.rockLinearMax - transform.translation.z + getCruiseDepth();
		else
			relAlt = -rockingData.rockLinearMax - transform.translation.z + getCruiseDepth();
		*/
		relAlt = 0;
		setAltitude(relAlt);
	}
	else
	{
		if (bRollUp)
			relRoll = rockingData.rockAngMax - roll;
		else
			relRoll = -rockingData.rockAngMax - roll;

		if (bAltUp)
			relAlt = rockingData.rockLinearMax - this->transform.translation.z;
		else
			relAlt = -rockingData.rockLinearMax - this->transform.translation.z;

		relRoll = this->fixAngle(relRoll);

		this->setDynamicsData(rockingData);
		if (fabs(this->ang_velocity.z) > 10*MUL_DEG_TO_RAD || fabs(roll) > rockingData.rockAngMax*2 || fabs(pitch) > rockingData.rockAngMax*2)
			this->restoreAngDynamicsData();

		rotateShip(0, relRoll, 0 - pitch);
		if (fabs(relRoll) < rockingData.rockAngMax*0.1)
			bRollUp = !bRollUp;
		setAltitude(relAlt);
		if (fabs(relAlt) < rockingData.rockLinearMax*0.1)
			bAltUp = !bAltUp;
	}
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

//---------------------------------------------------------------------------
//
template <class Base> 
void ObjectMove< Base >::physUpdateMove (SINGLE dt)
{
	ANIM->update_instance(this->instanceIndex,dt); //my solution for now is to go back to updating all moving objects every frame

	if (this->bVisible && !this->bReady)
	{
		ENGINE->update_instance(this->instanceIndex, 0, dt);
	}
	else
	if (this->bEnablePhysics)
		ENGINE->update_instance(this->instanceIndex, 0, 0);		// 0 dt means "just update tree"
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
