//--------------------------------------------------------------------------//
//                                                                          //
//                                TObjMove.h                                //
//                                                                          //
//                  COPYRIGHT (C) 1998 BY DIGITAL ANVIL, INC.               //
//                                                                          //
//--------------------------------------------------------------------------//
/*
    $Header: /Conquest/App/Src/TObjMove.h 189   8/23/01 1:53p Tmauer $
*/
//------------------------------- #INCLUDES --------------------------------//
//--------------------------------------------------------------------------//

#include "ObjList.h"
#include "Sector.h"
#include "TSmartpointer.h"
#include "Scripting.h"
#include "Search.h"
#ifndef IOBJECT_H
#include "IObject.h"
#endif

#ifndef XFORM_H
#include <xform.h>
#endif

#ifndef TERRAINMAP_H
#include "TerrainMap.h"
#endif

#ifndef DSHIPSAVE_H
#include <DShipSave.h>
#endif

#ifndef DSPACESHIP_H
#include <DSpaceShip.h>
#endif

#ifndef _INC_STDLIB
#include <stdlib.h>
#endif

#ifndef OPAGENT_H
#include "OPAgent.h"
#endif

#ifndef GRIDVECTOR_H
#include "GridVector.h"
#endif

#ifndef OBJMAP_H
#include "ObjMap.h"
#endif

#ifndef MPART_H
#include "MPart.h"
#endif

#ifndef DTROOPSHIP_H
#include <DTroopship.h>
#endif

#ifndef OBJSET_H
#include "Objset.h"
#endif

#ifndef ISHIPMOVE_H
#include "IShipMove.h"
#endif

#ifndef IADMIRAL_H
#include "IAdmiral.h"
#endif

#define ObjectMove _CoM

struct TRUEANIMPOS {
    NETGRIDVECTOR gridVec;
    Vector pos;
};


struct FootprintQuickList : ITerrainSegCallback {
    enum {
        MAX = 16
    };

    struct Info {
        FootprintInfo fpi;
        GRIDVECTOR grid;

        bool operator ==(const Info &_info) {
            return
            (
                fpi.flags == _info.fpi.flags &&
                fpi.missionID == _info.fpi.missionID
            );
        }
    };

    Info list[MAX];
    U32 count;

    FootprintQuickList() : count(0) {
    }

    virtual bool TerrainCallback(const struct FootprintInfo &info, struct GRIDVECTOR &pos) {
        if (count < MAX) {
            if (info.flags & TERRAIN_REGION) {
                list[count].fpi = info;
                list[count].grid = pos;
                count++;
            }
        }
        return (count < MAX);
    }

    bool hasFootprint(const Info &_test) {
        for (U32 i = 0; i < count; i++) {
            if (list[i] == _test) {
                return true;
            }
        }
        return false;
    }
};

extern TRUEANIMPOS g_trueAnimPos;

#define SEND_OPERATION(x)   \
	{	TSHIP_NET_COMMANDS command = x;   \
		THEMATRIX->SendOperationData(attackAgentID, dwMissionID, &command, 1); }

#define DEF_ROCK_LINEAR_MAX 150.0F
#define DEF_ROCK_ANG_MAX 1
static bool bUpdateMoveState = true;

//--------------------------------------------------------------------------//
//--------------------------------------------------------------------------//
//
template<class Base=IBaseObject>
struct _NO_VTABLE ObjectMove : public Base, private SPACESHIP_SAVELOAD::TOBJMOVE, private IFindPathCallback,
                               public IShipMove {
    struct Base::SaveNode saveNode;
    struct Base::LoadNode loadNode;
    struct Base::InitNode initNode;
    struct Base::UpdateNode updateNode;
    struct Base::OnOpCancelNode onOpCancelNode;
    struct Base::PreTakeoverNode preTakeoverNode;
    struct Base::GeneralSyncNode genSyncNode;
    struct Base::GeneralSyncNode genSyncNode2;
    struct Base::PhysUpdateNode physUpdateNode;
    struct Base::ReceiveOpDataNode receiveOpDataNode;
    struct Base::ExplodeNode explodeNode;

    typedef Base::SAVEINFO MOVESAVEINFO;
    typedef Base::INITINFO MOVEINITINFO;

private:
    ROCKING_DATA rockingData;

    SINGLE origAcceleration;

    SINGLE maxMoveSlop; // make movement irregular, for Chris
    Vector slopOffset;
    bool bRollTooHigh: 1; // current roll is too much
    bool bHalfSquare: 1;
    U8 tooHighCounter;
    int map_square;
    U32 map_sys;

    struct FootprintHistory {
        FootprintInfo info[2];
        GRIDVECTOR vec[2];
        int numEntries; // how many entries are valid ?
        U32 systemID;

        FootprintHistory(void) {
            numEntries = 0;
            systemID = 0;
            vec[0].zero();
            vec[1].zero();
        }

        bool operator ==(const FootprintHistory &foot) {
            return (memcmp(this, &foot, sizeof(*this)) == 0);
        }

        bool operator !=(const FootprintHistory &foot) {
            return (memcmp(this, &foot, sizeof(*this)) != 0);
        }
    } footprintHistory;


    //---------------------------------------------------------------------------
    //
    struct TestPassibleCallback : ITerrainSegCallback {
        GRIDVECTOR gridPos;

        TestPassibleCallback(void) {
        }

        virtual bool TerrainCallback(const struct FootprintInfo &info, struct GRIDVECTOR &pos) {
            if (info.flags & TERRAIN_IMPASSIBLE) {
                gridPos = pos;
                return false;
            }
            return true;
        }
    };

    struct TestPushLOSCallback : ITerrainSegCallback {
        GRIDVECTOR gridPos;

        TestPushLOSCallback(void) {
            gridPos.zero();
        }

        virtual bool TerrainCallback(const struct FootprintInfo &info, struct GRIDVECTOR &pos) {
            if (info.flags & (TERRAIN_IMPASSIBLE | TERRAIN_BLOCKLOS | TERRAIN_OUTOFSYSTEM))
                return false;
            gridPos = pos;
            return true;
        }
    };

    //----------------------------------

public:
    //----------------------------------

    ObjectMove(void);

    ~ObjectMove(void) {
        if (footprintHistory.numEntries > 0) {
            COMPTR<ITerrainMap> map;

            if (SECTOR) {
                SECTOR->GetTerrainMap(this->systemID, map.addr());
                if (map)
                    undoFootprintInfo(map);
            }
        }
        if (OBJMAP && map_sys && map_sys <= MAX_SYSTEMS)
            OBJMAP->RemoveObjectFromMap(this, map_sys, map_square);
    }

    /* ObjectMove methods */

    void resetMoveVars(void) {
        if (moveAgentID != 0)
            CQBOMB1("Invalid call to resetMoveVars() for part=%s (Ignorable)", (char *)this->partName);
        // should already be completed before this call
        if (bMoveActive) {
            currentPosition = this->transform.translation;
            bMoveActive = 0;
        }
        this->velocity.z = 0;
        setCruiseSpeed(0); // set it back to default
        setForwardAcceleration(0); // set it back to default
        bRotatingBeforeMove = false;
        bFinalMove = false;
        cruiseDepth = 0;
        bMockRotate = false;
        bCompletionAllowed = false;
    }

    void moveToPos(const GRIDVECTOR &pos) // start a move operation
    {
        moveToPos(pos, 0); // start a move operation
    }

    void moveToPos(const GRIDVECTOR &pos, U32 agentID, bool bSlowMove = false) {
        // start a move operation
        CQASSERT(this->bExploding == false || agentID == false);
        if (this->bExploding)
            return;

        COMPTR<ITerrainMap> map;

#ifndef FINAL_RELEASE
#pragma message ("commented out for demo, put this back in after to fix basalisk teleport bug")
        //	if (moveAgentID!=0)
        //		CQBOMB1("Invalid call to moveToPos() for part=%s (Ignorable)", (char *)partName);	// should already be completed before this call
#endif
        SECTOR->GetTerrainMap(this->systemID, map.addr());
        pathLength = 0;
        goalPosition = pos;
        U32 flags = bHalfSquare ? TERRAIN_FP_HALFSQUARE : TERRAIN_FP_FULLSQUARE;
        GRIDVECTOR from = GetGridPosition();

        if (bHalfSquare == 0)
            goalPosition.centerpos();
        else
            goalPosition.quarterpos();

#if (defined(_JASON) || defined(_SEAN))
        bool bParked = map->IsParkedAtGrid(from, dwMissionID, bHalfSquare == 0);
#endif
        bFinalMove = 0;
        bMoveActive = 1;

        if (map->FindPath(from, goalPosition, this->dwMissionID, flags, this) == 0) {
#if (defined(_JASON) || defined(_SEAN))
            if (bParked == false)
                CQBOMB1("TerrainMap returned pathlength==0, but \"%s\" isn't parked there! (Ignorable)",
                        (char *) partName);
#endif
            //
            // if the user asked us to move, then sidle over a bit
            //

            if (agentID && (GRIDVECTOR(g_trueAnimPos.gridVec) == pos) && (
                    g_trueAnimPos.gridVec.systemID == this->systemID)) {
                bMockRotate = true;
                SetPath(map, &from, 1);
                bFinalMove = 1;

                bool bFoundMockAngle = false;

                if (this->fleetID) {
                    VOLPTR(IAdmiral) flagship;
                    OBJLIST->FindObject(this->fleetID,TOTALLYVOLATILEPTR, flagship, IAdmiralID);
                    if (flagship.Ptr()) {
                        if (flagship->IsInLockedFormation()) {
                            flagship->MoveDoneHint(this);
                            // calculate the mock rotate angle
                            Vector vecDiff = flagship->GetFormationDir();
                            mockRotationAngle = TRANSFORM::get_yaw(vecDiff);
                            bFoundMockAngle = true;
                        }
                    }
                }

                if (!bFoundMockAngle) {
                    // calculate the mock rotate angle
                    Vector vecDiff = g_trueAnimPos.pos - this->transform.translation;
                    mockRotationAngle = TRANSFORM::get_yaw(vecDiff);
                }

                //
                // calc slop offset based on where the user clicked
                //
                slopOffset = g_trueAnimPos.pos - this->transform.translation;
                slopOffset.fast_normalize();
                slopOffset *= ((rand() & 127) + (rand() & 127)) * maxMoveSlop * (1.0 / 256.0);
            } else {
                moveAgentID = agentID;
                onPathComplete();
                return;
            }
        } else {
            bSyncNeeded = true;
            calcSlopOffset();

#if (defined(_JASON) || defined(_SEAN))
            if (pathLength == 1 && bParked && pathList[0] == from)
                CQBOMB1("TerrainMap returned pathlength==1, but \"%s\" is ALREADY parked at destination! (Ignorable)",
                        (char *) partName);
#endif
        }

        slowMove = bSlowMove;
        moveAgentID = agentID;
        setCruiseSpeed(0); // set it back to default
        setForwardAcceleration(0); // set it back to default
        setRandomCruiseDepth();
    }

    void moveToJump(IBaseObject *jumpgate, U32 agentID, bool bSlowMove) {
        jumpToPosition = jumpgate->GetPosition();
        jumpAgentID = agentID;
        moveToPos(jumpToPosition);
        slowMove = bSlowMove;

        this->bRecallFighters = false;
    }

    // actual movement code
    // move toward goal position, return true when we get there
    // does not use path finding or avoidance
    bool doMove(const Vector &goalPos) {
        Vector pos = this->transform.get_position();
        SINGLE relYaw, relRoll, relPitch;
        SINGLE yaw = this->transform.get_yaw();
        SINGLE roll = this->transform.get_roll();
        SINGLE pitch = this->transform.get_pitch();
        bool bContinuing = (bPathOverflow || pathLength > 1);
        Vector goal = goalPos;

        bRotatingBeforeMove = false;

        goal -= pos;

        goal.z = 0;
        const SINGLE goalMag = goal.magnitude();

        relYaw = this->fixAngle(get_angle(goal.x, goal.y) - yaw);

#if 1
        if (goalMag < this->boxRadius) {
            bool result;

            relRoll = -roll;
            goal.z = cruiseDepth - this->transform.translation.z;
            if (bContinuing)
                result = (setPosition(goal, this->getDynamicsData().maxLinearVelocity) != 0);
            else
                result = (setPosition(goal) != 0);

            SINGLE sidleDist = dot_product(goal, this->transform.get_i());
            if (fabs(sidleDist) > this->getDynamicsData().maxLinearVelocity) {
                if (sidleDist < 0)
                    relRoll -= 5.0 * MUL_DEG_TO_RAD;
                else if (sidleDist > 0)
                    relRoll += 5.0 * MUL_DEG_TO_RAD;
            }

            goal.z = 0;
            if (goalMag < this->box[4] - this->box[5]) // don't spin if very near goal
                relYaw = 0;
            rotateShip(relYaw, relRoll, 0 - pitch);
            return result;
        }
#endif

        if (fabs(relYaw) < 60.0 * MUL_DEG_TO_RAD) {
            if (bContinuing) {
                setPosition(goal, this->maxLinearVelocity);
                setThrustersOn();
            } else {
                SINGLE coast;

                coast = this->calculateCoastingDistance(this->getDynamicsData().maxLinearVelocity,
                                                        this->getDynamicsData().linearAcceleration, ELAPSED_TIME);

                if (coast < goalMag) // not there yet
                {
                    setPosition(goal, this->maxLinearVelocity);
                    setThrustersOn();
                } else {
                    setPosition(goal);
                }
            }

            setAltitude(cruiseDepth - this->transform.translation.z);
            if (cruiseDepth - this->transform.translation.z < -800)
                relPitch = (-10.0 * MUL_DEG_TO_RAD) - pitch; // pitch down
            else
                relPitch = 0 - pitch;
        } else {
            if (this->bVisible) {
                Vector diff = this->velocity / 2;
                diff.z = cruiseDepth - this->transform.translation.z;

                setPosition(diff);
            } else
                setAltitude(cruiseDepth - this->transform.translation.z);
            relPitch = (5.0 * MUL_DEG_TO_RAD) - pitch;
            bRotatingBeforeMove = true;
        }

        relRoll = 0 - roll;
        if (relRoll < -PI)
            relRoll += PI * 2;
        else if (relRoll > PI)
            relRoll -= PI * 2;

        if (fabs(relYaw) > 10.0 * MUL_DEG_TO_RAD) {
            if (relYaw < 0)
                relRoll -= 5.0 * MUL_DEG_TO_RAD; // roll to the left
            else
                relRoll += 5.0 * MUL_DEG_TO_RAD; // roll to the right
        }

        rotateShip(relYaw, relRoll, relPitch);

        if (goalMag < -this->box[5]) {
            // return early if we are not at the last stop (cut corners)
            if (pathLength > 1 || bPathOverflow || this->velocity.magnitude() < 5.0)
                return true;
        }
        return false;
    }

    bool isMovingToJump(void) const {
        return (jumpAgentID != 0);
    }

    bool isMoveActive(void) const {
        return bMoveActive;
    }

    bool isPatroling(void) const {
        return bPatroling;
    }

    bool isAutoMovementEnabled(void) const {
        return bAutoMovement;
    }

    void disableAutoMovement(void) {
        bAutoMovement = false;
    }

    void enableAutoMovement(void) {
        bAutoMovement = true;
    }

    // override this method in derived class if you want to
    virtual bool testReadyForJump(void) {
        return true;
    }

    bool isHalfSquare() {
        return bHalfSquare;
    }

    GRIDVECTOR getLastGrid() {
        return footprintHistory.vec[0];
    }

    const GRIDVECTOR &getGoalPosition(void) const {
        return goalPosition;
    }

    /* IMissionActor overrides */

    virtual bool IsMoveActive(void) const {
        return bMoveActive;
    }

    virtual void TakeoverSwitchID(U32 newID);

    SINGLE getCruiseSpeed(void) {
        SINGLE fleetMod = 1.0;
        if (this->fleetID) {
            VOLPTR(IAdmiral) flagship;
            OBJLIST->FindObject(this->fleetID,TOTALLYVOLATILEPTR, flagship, IAdmiralID);
            if (flagship.Ptr()) {
                MPart part(this);
                fleetMod = 1 + flagship->GetSpeedBonus(this->mObjClass, part.pInit->armorData.myArmor);
            }
        }
        SINGLE sectorMod = 1.0 + SECTOR->GetSectorEffects(this->playerID, this->systemID)->getSpeedMod();
        return (this->maxVelocity * this->fieldFlags.getSpeedModifier() * this->effectFlags.getSpeedModifier() *
                fleetMod * sectorMod);
    }

    void setCruiseSpeed(SINGLE speed) {
        CQASSERT(speed >= 0);
        DYNAMICS_DATA dyn = this->getDynamicsData();

        if (speed)
            dyn.maxLinearVelocity = cruiseSpeed = speed;
        else
            dyn.maxLinearVelocity = cruiseSpeed = this->maxVelocity;

        this->setDynamicsData(dyn);
    }

    SINGLE getForwardAcceleration(void) {
        const DYNAMICS_DATA &dyn = this->getDynamicsData();
        return dyn.linearAcceleration;
    }

    void setForwardAcceleration(SINGLE acceleration) {
        CQASSERT(acceleration >= 0);
        DYNAMICS_DATA dyn = this->getDynamicsData();

        if (acceleration)
            dyn.linearAcceleration = groupAcceleration = acceleration;
        else
            dyn.linearAcceleration = groupAcceleration = origAcceleration;

        this->setDynamicsData(dyn);
    }

    void setRandomCruiseDepth(void) {
        SINGLE depths[4] = {-200, -400, -800, -1000};
        cruiseDepth = depths[rand() & 3];
    }

    SINGLE getCruiseDepth(void) {
        return cruiseDepth;
    }

    bool isRotatingBeforeMove(void) const {
        return bRotatingBeforeMove;
    }

    U32 getSyncData(void *buffer) {
        GRIDVECTOR *const data = (GRIDVECTOR *) buffer;
        CQASSERT(THEMATRIX->IsMaster());

        if (isAutoMovementEnabled() == false)
            return 0;

        /*
        if (bPatroling && bMoveActive==0)
        {
            patrolIndex = (patrolIndex + 1) & 1;
            *data = patrolVectors[patrolIndex];
            bSyncNeeded=false;
            moveToPos(patrolVectors[patrolIndex]);
            return sizeof(*data);
        }
        */

        if (bSyncNeeded && bMoveActive == 0) {
            *data = currentPosition;
            bSyncNeeded = false;
            return sizeof(*data);
        }

        return 0;
    }

    void putSyncData(void *buffer, U32 bufferSize, bool bLateDelivery) {
        GRIDVECTOR *vec = (GRIDVECTOR *) buffer;

        CQASSERT(bufferSize == sizeof(GRIDVECTOR));
        CQASSERT(THEMATRIX->IsMaster()==0 && "Sync data received on non-master machine!");

        bCompletionAllowed = true;
        bMoveActive = 1;
        bFinalMove = 1;
        bPathOverflow = 0;
        pathLength = 1;
        pathList[0] = goalPosition = *vec;
    }

    U32 getSyncPatrolData(void *buffer) {
        GRIDVECTOR *const data = (GRIDVECTOR *) buffer;
        CQASSERT(THEMATRIX->IsMaster());

        if (isAutoMovementEnabled() == false)
            return 0;

        if (bPatroling && bMoveActive == 0) {
            patrolIndex = (patrolIndex + 1) & 1;
            *data = patrolVectors[patrolIndex];
            bSyncNeeded = false;
            moveToPos(patrolVectors[patrolIndex]);
            return sizeof(*data);
        }

        return 0;
    }

    void putSyncPatrolData(void *buffer, U32 bufferSize, bool bLateDelivery) {
        GRIDVECTOR *vec = (GRIDVECTOR *) buffer;

        CQASSERT(bufferSize == sizeof(GRIDVECTOR));
        CQASSERT(THEMATRIX->IsMaster()==0 && "Sync data received on non-master machine!");

        moveToPos(*vec);
    }

    void TESTING_shudder(const Vector &dir, SINGLE mag);

    bool getRollTooHigh(void) const {
        return bRollTooHigh;
    }

    const GRIDVECTOR &getCurrentPosition(void) const {
        return currentPosition;
    }

    // terrainMap points to the map for the current system, which might be different than the history
    void undoFootprintInfo(struct ITerrainMap *terrainMap) {
        if (footprintHistory.numEntries > 0) {
            COMPTR<ITerrainMap> map;

            if (footprintHistory.systemID != this->systemID)
                SECTOR->GetTerrainMap(footprintHistory.systemID, map.addr());
            else
                map = terrainMap;
            map->UndoFootprint(&footprintHistory.vec[0], 1, footprintHistory.info[0]);

            if (footprintHistory.numEntries > 1)
                map->UndoFootprint(&footprintHistory.vec[1], 1, footprintHistory.info[1]);
        }

        footprintHistory.numEntries = 0;

        //if this code is ever removed, change RemoveFromMap
        if (OBJMAP && map_sys && map_sys <= MAX_SYSTEMS) {
            OBJMAP->RemoveObjectFromMap(this, map_sys, map_square);
            this->objMapNode = 0;
            map_sys = map_square = 0;
        }
    }

    void patrol(const GRIDVECTOR &src, const GRIDVECTOR &dst, U32 agentID) {
        // patrol between two points
        patrolIndex = 0;
        patrolVectors[0] = src;
        patrolVectors[1] = dst;
        bPatroling = true;
    }

    /* IPhysicalObject methods */

    virtual void SetPosition(const Vector &position, U32 newSystemID) {
        CQASSERT(newSystemID && newSystemID <= MAX_SYSTEMS);
        bool bNewSystem = (this->systemID != newSystemID);
        this->systemID = newSystemID;
        Base::SetPosition(position, newSystemID);
        if (bNewSystem || isMoveActive() == 0) // don't do this thing on load if we are busy
        {
            currentPosition = this->transform.translation;
            if (bHalfSquare)
                currentPosition.quarterpos();
            else
                currentPosition.centerpos();
            if (isAutoMovementEnabled())
                moveToPos(currentPosition);
            else
                resetMoveVars(); // could be moving but temporarily disabled
        }
    }

    virtual void SetTransform(const TRANSFORM &_transform, U32 newSystemID) {
        CQASSERT(newSystemID && newSystemID <= MAX_SYSTEMS);
        bool bNewSystem = (this->systemID != newSystemID);
        this->systemID = newSystemID;
        Base::SetTransform(_transform, newSystemID);
        if (bNewSystem || isMoveActive() == 0) // don't do this thing on load if we are busy
        {
            currentPosition = this->transform.translation;
            if (bHalfSquare)
                currentPosition.quarterpos();
            else
                currentPosition.centerpos();
            if (isAutoMovementEnabled())
                moveToPos(currentPosition); // don't do this if disabled (admirals)
            else
                resetMoveVars(); // could be moving but temporarily disabled
        }
    }

    /* TObjControl methods */

    bool rotateShip(SINGLE relYaw, SINGLE relRoll, SINGLE relPitch) {
        disableAutoMovement();
        return Base::rotateShip(relYaw, relRoll, relPitch);
    }

    void rotateTo(SINGLE absYaw, SINGLE absRoll, SINGLE absPitch) {
        disableAutoMovement();
        Base::rotateTo(absYaw, absRoll, absPitch);
    }

    void setAltitude(SINGLE relAltitude) {
        disableAutoMovement();
        Base::setAltitude(relAltitude);
    }

    bool setPosition(const Vector &relPosition) {
        disableAutoMovement();
        return Base::setPosition(relPosition);
    }

    bool setPosition(const Vector &relPosition, SINGLE _finalVel) {
        disableAutoMovement();
        return Base::setPosition(relPosition, _finalVel);
    }

    void moveTo(const Vector &absPosition) {
        disableAutoMovement();
        Base::moveTo(absPosition);
    }

    void moveTo(const Vector &absPosition, SINGLE _finalVel) {
        disableAutoMovement();
        Base::moveTo(absPosition, _finalVel);
    }

    void setThrustersOn(void) {
        disableAutoMovement();
        Base::setThrustersOn();
    }

    /* IBaseObject methods */

    virtual struct GRIDVECTOR GetGridPosition(void) const {
        if (currentPosition.isZero() || overrideMode != OVERRIDE_NONE)
        // can happen if unit is not built yet, or is being pushed around
        {
            GRIDVECTOR vec;
            vec = this->transform.translation;
            return vec;
        } else {
            if (isMoveActive()) {
                return findValidGridPosition();
            } else
                return currentPosition;
        }
    }

    virtual void SetTerrainFootprint(struct ITerrainMap *terrainMap) {
        FootprintHistory footprint;
        //
        // calculate new footprint
        //
        if (this->bReady && this->systemID <= MAX_SYSTEMS) {
            footprint.info[0].flags = footprint.info[1].flags = bHalfSquare ? TERRAIN_HALFSQUARE : TERRAIN_FULLSQUARE;
            footprint.info[0].height = footprint.info[1].height = this->box[2]; // maxy
            footprint.info[0].missionID = footprint.info[1].missionID = this->dwMissionID;
            footprint.systemID = this->systemID;

            if (isMoveActive()) {
                // took out the check for autoMovementEnabled because ships were setting the parked flag
                // at in-appropriate times
                // we may find that big ships are trying to take the spot of a gunboat that is rotating
                // let's wait and see...
                if (bMockRotate) {
                    footprint.info[0].flags |= (TERRAIN_PARKED | TERRAIN_UNITROTATING);
                } else {
                    footprint.info[0].flags |= TERRAIN_MOVING;
                }

                if (bFinalMove && bPathOverflow == 0) {
                    footprint.info[1].flags |= (TERRAIN_PARKED | TERRAIN_DESTINATION);
                } else {
                    footprint.info[1].flags |= TERRAIN_DESTINATION;
                }

                // set the grid vector for the footprintf
                footprint.vec[0] = this->transform.translation;
                footprint.vec[1] = pathList[0];
                footprint.numEntries = 2;
            } else {
                footprint.info[0].flags |= TERRAIN_PARKED;
                footprint.vec[0] = GetGridPosition();
                footprint.numEntries = 1;
            }
        }

        //
        // send information if different
        //
        if (footprint != footprintHistory) {
            // update the scripting engine BEFORE the terrain map really changes
            scriptingUpdate(footprintHistory, footprint);

            undoFootprintInfo(terrainMap);

            if (footprint.numEntries > 0) {
                terrainMap->SetFootprint(&footprint.vec[0], 1, footprint.info[0]);
            }

            if (footprint.numEntries > 1) {
                terrainMap->SetFootprint(&footprint.vec[1], 1, footprint.info[1]);
            }

            footprintHistory = footprint;
        }

        // always update the OBJMAP node, even if not updating the terrain footprint!
        // e.g. (ship being build, or derelict)

        updateObjMap();
    }

    /* IShipMove methods */

    virtual void PushShip(U32 attackerID, const Vector &direction, SINGLE velMag) {
        overrideAttackerID = attackerID;
        overrideMode = OVERRIDE_PUSH;
        overrideSpeed = velMag;
        Vector dir = direction;
        dir *= (GRIDSIZE / direction.magnitude());
        Vector position = this->transform.translation;

        while (1) {
            position += dir;
            U32 _x = ((F2LONG(position.x) * 4) + ((GRIDSIZE - 1) / 2)) / GRIDSIZE;
            U32 _y = ((F2LONG(position.y) * 4) + ((GRIDSIZE - 1) / 2)) / GRIDSIZE;

            if (_x > 255 || _y > 255)
                break;
        }
        position -= dir; // undo the extra one
        pushShipTo(position);
    }

    virtual void PushShipTo(U32 attackerID, const Vector &position, SINGLE velMag) {
        overrideAttackerID = attackerID;
        overrideMode = OVERRIDE_PUSH;
        overrideSpeed = velMag;
        pushShipTo(position);
    }

    virtual void DestabilizeShip(U32 attackerID) {
        if (overrideMode == OVERRIDE_PUSH)
            cancelPush();
        overrideAttackerID = attackerID;
        overrideMode = OVERRIDE_DESTABILIZE;
        this->setDestabilize(); // have this take affect immediately!
    }

    virtual void ForceShipOrientation(U32 attackerID, SINGLE yaw) {
        if (overrideMode == OVERRIDE_PUSH)
            cancelPush();
        overrideAttackerID = attackerID;
        overrideMode = OVERRIDE_ORIENT;
        overrideYaw = yaw;
    }

    virtual void ReleaseShipControl(U32 attackerID) {
        if (attackerID == overrideAttackerID) {
            if (overrideMode == OVERRIDE_PUSH)
                cancelPush();
            overrideAttackerID = 0;
            overrideMode = OVERRIDE_NONE;
        }
    }

    virtual SINGLE GetCurrentCruiseVelocity(void) {
        return getCruiseSpeed();
    }

    virtual void RemoveFromMap(void) {
        COMPTR<ITerrainMap> map;
        SECTOR->GetTerrainMap(this->systemID, map.addr());
        if (map)
            undoFootprintInfo(map);
        this->bExploding = true;
    }

    virtual bool IsMoving(void) {
        return isMoveActive();
    }

private:
    void updateObjMap(void) {
        if (this->systemID && this->systemID <= MAX_SYSTEMS) // don't do this in hyperspace
        {
            int new_map_square = OBJMAP->GetMapSquare(this->systemID, this->transform.translation);
            if (new_map_square != map_square || map_sys != this->systemID) {
                OBJMAP->RemoveObjectFromMap(this, map_sys, map_square);
                map_square = new_map_square;
                map_sys = this->systemID;
                U32 flags = (this->bDerelict) ? 0 : OM_TARGETABLE;
                if (this->aliasPlayerID)
                    flags |= OM_MIMIC;
                this->objMapNode = OBJMAP->AddObjectToMap(this, map_sys, map_square, flags);
                CQASSERT(this->objMapNode);
            }
        }
    }

    virtual void SetPath(ITerrainMap *map, const GRIDVECTOR *const squares, int numSquares) {
        if (numSquares > MAX_PATH_SIZE) {
            pathLength = MAX_PATH_SIZE;
            bPathOverflow = true;
        } else {
            pathLength = numSquares;
            bPathOverflow = false;
        }
        memcpy(pathList, squares + (numSquares - pathLength), pathLength * sizeof(GRIDVECTOR));

        if (bPathOverflow == false) {
            SetTerrainFootprint(map);
        }
    }

    BOOL32 updateMoveState(void) {
        if (!bUpdateMoveState)
            return 1;
        bool bNoDynamics = 0;
        bool bOrigAutoMovement = isAutoMovementEnabled();

        if (overrideMode != OVERRIDE_NONE) {
            IBaseObject *obj = OBJLIST->FindObject(overrideAttackerID);
            if (obj == 0 || obj->GetSystemID() != this->systemID) {
                if (overrideMode == OVERRIDE_PUSH)
                    cancelPush();
                overrideAttackerID = 0;
                overrideMode = OVERRIDE_NONE;
            }
        }

        if (this->bMoveDisabled == 0 && this->effectFlags.canMove()) {
            if (isAutoMovementEnabled() && this->bReady) {
                bNoDynamics = 0;
                //
                // set the correct velocity
                //
                if (this->bExploding == 0) {
                    this->restoreDynamicsData();

                    SINGLE maxV = this->maxVelocity;

                    if (slowMove && (moveAgentID || jumpAgentID)) //find the slowest guy in the group.
                    {
                        const ObjSet *set = NULL;
                        if (moveAgentID)
                            THEMATRIX->GetOperationSet(moveAgentID, &set);
                        else
                            THEMATRIX->GetOperationSet(jumpAgentID, &set);
                        if (set) {
                            for (U32 i = 0; i < set->numObjects; ++i) {
                                IBaseObject *obj = OBJLIST->FindObject(set->objectIDs[i]);
                                if (obj) {
                                    MPart part(obj);
                                    if (part.isValid()) {
                                        if (part->maxVelocity < maxV)
                                            maxV = part->maxVelocity;
                                    }
                                }
                            }
                        }
                    }

                    SINGLE fleetMod = 1.0;
                    if (this->fleetID) {
                        VOLPTR(IAdmiral) flagship;
                        OBJLIST->FindObject(this->fleetID,TOTALLYVOLATILEPTR, flagship, IAdmiralID);
                        if (flagship.Ptr()) {
                            MPart part(this);
                            fleetMod = 1 + flagship->GetSpeedBonus(this->mObjClass, part.pInit->armorData.myArmor);
                        }
                    }
                    SINGLE sectorMod = 1.0 + SECTOR->GetSectorEffects(this->playerID, this->systemID)->getSpeedMod();
                    setCruiseSpeed(
                        maxV * completionModifier() * this->fieldFlags.getSpeedModifier() * this->effectFlags.
                        getSpeedModifier() * fleetMod * sectorMod);

                    if (isMovingToJump())
                        doJumpPreparation();
                    else if (isMoveActive())
                        doPathMove();
                    else {
                        if (this->bVisible && LODPERCENT != 0)
                            rockTheBoat();
                        else if (overrideMode == OVERRIDE_NONE) {
                            bNoDynamics = 1;
                            this->velocity.zero();
                            this->ang_velocity.zero();
                            this->DEBUG_resetInputCounter();
                        } else {
                            rotateShip(0, 0 - this->transform.get_roll(), 0 - this->transform.get_pitch());
                            setAltitude(0);
                        }
                    }
                }
            } else if (this->bReady) // let rotates happen when not on screen
            {
                this->restoreDynamicsData();
                bNoDynamics = 0;
            } else {
                this->DEBUG_resetInputCounter();
            }
        } else if (((!this->effectFlags.bDestabilizer) && (this->effectFlags.bStasis)) || this->bMoveDisabled)
        // if (hPart->moveAbility==0)
        {
            bNoDynamics = 1;
            this->velocity.zero();
            this->DEBUG_resetInputCounter();
            if (this->effectFlags.canMove())
                this->ang_velocity.zero();
            if (this->effectFlags.canMove() && goalPosition != currentPosition && THEMATRIX->IsMaster() == 0) {
                SetPosition(goalPosition, this->systemID);
                currentPosition = goalPosition;
            }
        }

        if (bOrigAutoMovement && this->bReady && this->bMoveDisabled == 0 && overrideMode != OVERRIDE_NONE) {
            switch (overrideMode) {
                case OVERRIDE_PUSH:
                    this->restoreLinearDynamicsData();
                    this->setAbsOverridePosition(overrideDest, overrideSpeed);
                    bNoDynamics = 0;
                    break;
                case OVERRIDE_DESTABILIZE:
                    this->setDestabilize();
                    bNoDynamics = (this->bVisible == 0 || LODPERCENT == 0);
                    break;
                case OVERRIDE_ORIENT:
                    this->overrideAbsShipRotate(overrideYaw, 0, 0);
                    bNoDynamics = 0;
                    break;
            }
        }

        this->bEnablePhysics = (this->bReady && bNoDynamics == 0);
        if (this->bEnablePhysics && !this->bExploding) {
            // update the map with our latest location
            // we may have moved outside our square because of various external forces,
            // or because we are pathfinding to a new location.
            COMPTR<ITerrainMap> map;
            SECTOR->GetTerrainMap(this->systemID, map.addr());
            if (map)
                SetTerrainFootprint(map);
        }

        bAutoMovement = true; // always comes back on by default, must be turned off every frame

        return 1;
    }

    void initMoveState(const MOVEINITINFO &data) {
        rockingData = data.pData->rockingData;
        if (rockingData.maxLinearVelocity == 0)
            rockingData.maxLinearVelocity = DEF_ROCK_LINEAR_MAX;
        if (rockingData.maxAngVelocity == 0)
            rockingData.maxAngVelocity = DEF_ROCK_ANG_MAX;

        bRollUp = ((rand() & 1) == 0);
        bAltUp = ((rand() & 1) == 0);
        DYNAMICS_DATA dyn = this->getDynamicsData();

        cruiseSpeed = dyn.maxLinearVelocity = this->maxVelocity = data.pData->missionData.maxVelocity;
        groupAcceleration = origAcceleration = dyn.linearAcceleration;
        this->setDynamicsData(dyn);

        if ((maxMoveSlop = HALFGRID / 2 - this->boxRadius) >= 0)
            bHalfSquare = true;
        else {
            bHalfSquare = false;
            if ((maxMoveSlop += HALFGRID / 2) < 0)
                maxMoveSlop = 0;
        }
    }

    void loadMoveState(MOVESAVEINFO &load) {
        *static_cast<SPACESHIP_SAVELOAD::TOBJMOVE *>(this) = load.tobjmove;
        DYNAMICS_DATA dyn = this->getDynamicsData();
        if (cruiseSpeed)
            dyn.maxLinearVelocity = cruiseSpeed;
        else
            dyn.maxLinearVelocity = cruiseSpeed = this->maxVelocity;
        if (groupAcceleration)
            dyn.angAcceleration = groupAcceleration;
        else
            groupAcceleration = origAcceleration;

        this->setDynamicsData(dyn);
    }

    void saveMoveState(MOVESAVEINFO &save) {
        save.tobjmove = *static_cast<SPACESHIP_SAVELOAD::TOBJMOVE *>(this);
    }

    void rockTheBoat(void) {
        SINGLE relRoll, relAlt;

        SINGLE roll = this->transform.get_roll();
        SINGLE pitch = this->transform.get_pitch();

        if (bRollTooHigh) {
            this->restoreDynamicsData();

            this->bEnablePhysics = 1;
            bool rotresult = rotateShip(0, 0 - roll, 0 - pitch);
            if (++tooHighCounter > U8(REALTIME_FRAMERATE + 1) && rotresult)
                bRollTooHigh = 0;
            /*
            if (bAltUp)
                relAlt = rockingData.rockLinearMax - transform.translation.z + getCruiseDepth();
            else
                relAlt = -rockingData.rockLinearMax - transform.translation.z + getCruiseDepth();
            */
            relAlt = 0;
            setAltitude(relAlt);
        } else {
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
            if (fabs(this->ang_velocity.z) > 10 * MUL_DEG_TO_RAD || fabs(roll) > rockingData.rockAngMax * 2 ||
                fabs(pitch) > rockingData.rockAngMax * 2)
                this->restoreAngDynamicsData();

            rotateShip(0, relRoll, 0 - pitch);
            if (fabs(relRoll) < rockingData.rockAngMax * 0.1)
                bRollUp = !bRollUp;
            setAltitude(relAlt);
            if (fabs(relAlt) < rockingData.rockLinearMax * 0.1)
                bAltUp = !bAltUp;
        }
    }

    void onOpCancel(U32 agentID) {
        if (agentID == moveAgentID) {
            moveAgentID = 0;
        }
        if (agentID == jumpAgentID) {
            jumpAgentID = 0;
        }
        this->bRecallFighters = false;
        bPatroling = false;

        if (isMoveActive() && THEMATRIX->IsMaster()) {
            GRIDVECTOR vec;
            vec = this->transform.translation + (this->velocity / 2);
            moveToPos(vec); // end in a predictable location
        }
    }

    void preTakeover(U32 newMissionID, U32 troopID) {
        if (moveAgentID) {
            if (THEMATRIX->IsMaster())
                THEMATRIX->SendOperationData(moveAgentID, this->dwMissionID, NULL, 0);
            THEMATRIX->OperationCompleted2(moveAgentID, this->dwMissionID);
        }
        if (jumpAgentID) {
            if (THEMATRIX->IsMaster())
                THEMATRIX->SendOperationData(jumpAgentID, this->dwMissionID, NULL, 0);
            THEMATRIX->OperationCompleted2(jumpAgentID, this->dwMissionID);
        }

        this->bRecallFighters = false;
        bPatroling = false;

        if (isMoveActive() && THEMATRIX->IsMaster()) {
            GRIDVECTOR vec;
            vec = this->transform.translation + (this->velocity / 2);
            moveToPos(vec); // end in a predictable location
        } else
            resetMoveVars();
    }

    // use pathfinding, avoidance to reach goalPosition
    bool doPathMove(void) {
        if (isMoveActive() && pathLength >= 1) {
            CQASSERT(pathLength >= 1);
            bool bNext = ((bPathOverflow || pathLength > 1) && pathList[pathLength - 1].isMostlyEqual(
                              GRIDVECTOR::Create(this->transform.translation)));
            if (bMockRotate) {
                // rotate to position, if already there call onPathComplete
                SINGLE relYaw = mockRotationAngle - this->transform.get_yaw();
                Vector relPos = pathList[pathLength - 1] + slopOffset;
                relPos -= this->transform.translation;

                if (relYaw < -PI)
                    relYaw += PI * 2;
                else if (relYaw > PI)
                    relYaw -= PI * 2;

                BOOL32 bMoveResult = setPosition(relPos);

                if (bMoveResult && fabs(relYaw) < MUL_DEG_TO_RAD * 20) {
                    onPathComplete();
                } else {
                    rotateShip(relYaw, 0, 0);
                }
            } else if (doMove(pathList[pathLength - 1] + slopOffset) || bNext) {
                currentPosition = pathList[--pathLength];

                if (pathLength == 0 && bFinalMove) {
                    if (fleetRotationCheck())
                        onPathComplete();
                } else if (pathLength <= 1 && (bFinalMove == false || bPathOverflow)) {
                    COMPTR<ITerrainMap> map;
                    GRIDVECTOR from = GetGridPosition();

                    SECTOR->GetTerrainMap(this->systemID, map.addr());
                    U32 flags = bHalfSquare ? TERRAIN_FP_HALFSQUARE : TERRAIN_FP_FULLSQUARE;
                    if (bPathOverflow == 0) // did we receive the full path last time?
                        bFinalMove = 1;
                    else
                        calcSlopOffset();
                    if (bFinalMove)
                        flags |= TERRAIN_FP_FINALPATH;

#if (defined(_JASON) || defined(_SEAN))
                    bool bParked = map->IsParkedAtGrid(from, dwMissionID, bHalfSquare == 0);
#endif

                    if (map->FindPath(from, goalPosition, this->dwMissionID, flags, this) == 0) {
                        if (fleetRotationCheck())
                            onPathComplete();

#if (defined(_JASON) || defined(_SEAN))
                        if (bParked == false)
                            CQBOMB1("TerrainMap returned pathlength==0, but \"%s\" isn't parked there! (Ignorable)",
                                    (char *) partName);
#endif
                        //			CQASSERT(bNext==false);		// we ended our move early, cannot exit path here!
                    }
                }
            }
        } else {
            // on the client, we may complete the move before the host, so pathLength may be zero for a time
            if (isMoveActive() && pathLength == 0 && THEMATRIX->IsMaster()) // can happen after master change
                onPathComplete();
            else {
                rotateShip(0, 0, 0);
                setAltitude(0);
            }
        }
        return isMoveActive() == 0;
    }

    void doJumpPreparation(void) {
        // force ship to stay put, by default
        rotateShip(0, 0, 0);
        setAltitude(0);
        // check bRecallFighters here, so that we don't actually move to a grid
        // position while we are waiting to assemble.
        bool bMoveResult = (isMoveActive() == 0) || (this->bRecallFighters == true) || doPathMove();

        if ((this->bRecallFighters == false) && (bMoveResult == false)) {
            // first stage, moving towards jumpgate
            // do a distance check and a line-of-sight
            GRIDVECTOR gridpos = GetGridPosition();
            SINGLE fdist = gridpos - jumpToPosition;
            if (fdist < __min(3, this->sensorRadius)) {
                // we are less than 3 grid units away
                if (testPassible(jumpToPosition) == true) {
                    resetMoveVars();
                    this->bRecallFighters = true;
                }
            }
        } else if (bMoveResult == false) {
            // second stage, moving to a settling place
            // nothing yet...
        } else // not moving into position
        {
            if (testPassible(jumpToPosition) == false) {
                if (THEMATRIX->IsMaster()) {
                    resetMoveVars();
                    THEMATRIX->SendOperationData(jumpAgentID, this->dwMissionID, &currentPosition,
                                                 sizeof(currentPosition));
                    THEMATRIX->OperationCompleted2(jumpAgentID, this->dwMissionID);
                    bSyncNeeded = 0;
                    this->bRecallFighters = false;
                    THEMATRIX->FlushOpQueueForUnit(this);
                    if (CQFLAGS.bTraceMission)
                        CQTRACE11("Jump failed for unit \"%s\"", (char *)this->partName);
                }
            } else {
                this->bRecallFighters = true;

                GRIDVECTOR goal_pos = jumpToPosition;

                if (goal_pos.cornerpos() - GetGridPosition() < 1) {
                    // too close to rotate
                    bMoveResult = true;
                } else {
                    Vector goal = goal_pos.cornerpos() - this->transform.translation;
                    SINGLE relYaw = get_angle(goal.x, goal.y) - this->transform.get_yaw();
                    if (relYaw < -PI)
                        relYaw += PI * 2;
                    else if (relYaw > PI)
                        relYaw -= PI * 2;

                    rotateShip(relYaw, 0 - this->transform.get_roll(), 0 - this->transform.get_pitch());

                    bMoveResult = (fabs(relYaw) < 15 * MUL_DEG_TO_RAD);
                }
                if (bMoveResult)
                    bMoveResult = testReadyForJump();

                if (bMoveResult && THEMATRIX->IsMaster()) {
                    resetMoveVars();
                    THEMATRIX->SendOperationData(jumpAgentID, this->dwMissionID, &currentPosition,
                                                 sizeof(currentPosition));
                    THEMATRIX->OperationCompleted2(jumpAgentID, this->dwMissionID);
                    bSyncNeeded = 0;
                }
            }
        }
    }

    void onPathComplete(void) {
        if (moveAgentID == 0 || THEMATRIX->IsMaster()) {
            if (moveAgentID) {
                THEMATRIX->SendOperationData(moveAgentID, this->dwMissionID, &currentPosition, sizeof(currentPosition));
                THEMATRIX->OperationCompleted2(moveAgentID, this->dwMissionID);
                bSyncNeeded = 0;
            }
            GRIDVECTOR savedVec = currentPosition;
            resetMoveVars();
            currentPosition = savedVec; // restore it
            cruiseDepth = 0;
        }
        pathLength = 0;
    }

    bool fleetRotationCheck(void) {
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

                    SECTOR->GetTerrainMap(this->systemID, map.addr());
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
    // TODO:  OnMasterChange():  If moveActive() and pathlength==0, complete the move.
    // TODO:  Move Patrol to a separate sync function
    // TODO:  no autoattack for gunboat when bRecallFighters==true
    bool testPassible(const struct GRIDVECTOR &pos) {
        TestPassibleCallback callback;
        COMPTR<ITerrainMap> map;
        bool result;

        SECTOR->GetTerrainMap(this->systemID, map.addr());

        if ((result=map->TestSegment(GetGridPosition(), pos, &callback)) == false)
            result = callback.gridPos.isMostlyEqual(pos);

        return result;
    }

    GRIDVECTOR findValidGridPosition(void) const {
        GRIDVECTOR vec;
        COMPTR<ITerrainMap> map;

        vec = this->transform.translation;
        if (bHalfSquare)
            vec.quarterpos();
        else
            vec.centerpos();

        SECTOR->GetTerrainMap(this->systemID, map.addr());
        if (map->IsGridValid(vec))
            return vec;
        else {
            // back up along our path
            Vector pos = currentPosition - this->transform.translation;
            pos.fast_normalize();
            pos *= GRIDSIZE / 2;
            pos += this->transform.translation;
            vec = pos;
            if (bHalfSquare)
                vec.quarterpos();
            else
                vec.centerpos();

            if (map->IsGridValid(vec))
                return vec;
            else
                return currentPosition;
        }
    }

    void calcSlopOffset(void) {
        SINGLE angle = (SINGLE(rand() & 255) / 256 * 360 * MUL_DEG_TO_RAD);
        slopOffset = TRANSFORM::rotate_about_z(this->transform.get_k(), angle); // yaw
        slopOffset *= ((rand() & 127) + (rand() & 127)) * maxMoveSlop * (1.0 / 256.0);
    }

    void physUpdateMove(SINGLE dt) {
        ANIM->update_instance(this->instanceIndex,dt); //my solution for now is to go back to updating all moving objects every frame

        if (this->bVisible && !this->bReady)
        {
            ENGINE->update_instance(this->instanceIndex, 0, dt);
        }
        else
            if (this->bEnablePhysics)
                ENGINE->update_instance(this->instanceIndex, 0, 0);		// 0 dt means "just update tree"
    }

    // find furthest position allowed within line-of-sight
    void pushShipTo(const Vector &position) {
        TestPushLOSCallback callback;
        COMPTR<ITerrainMap> map;

        CQASSERT(this->systemID && this->systemID <= MAX_SYSTEMS);

        SECTOR->GetTerrainMap(this->systemID, map.addr());

        GRIDVECTOR toPos;
        toPos = position;
        if (map->TestSegment(GetGridPosition(), toPos, &callback))
            overrideDest = toPos;
        else {
            if (callback.gridPos.isZero()) // no where to go
                callback.gridPos = GetGridPosition();

            overrideDest = callback.gridPos;
        }
    }

    void cancelPush(void) {
        CQASSERT(overrideMode==OVERRIDE_PUSH);
        if (isMoveActive() == 0 && THEMATRIX->IsMaster()) {
            GRIDVECTOR vec;
            vec = this->transform.translation + (this->velocity / 2);
            moveToPos(vec); // end in a predictable location
        }
    }

    void receiveOperationData(U32 agentID, void *buffer, U32 bufferSize) {
        if (moveAgentID && moveAgentID == agentID) {
            if (buffer)
                putSyncData(buffer, bufferSize, false);
            THEMATRIX->OperationCompleted2(moveAgentID, this->dwMissionID);
        }
        if (jumpAgentID && jumpAgentID == agentID) {
            if (buffer)
                putSyncData(buffer, bufferSize, false);

            THEMATRIX->OperationCompleted2(jumpAgentID, this->dwMissionID);
        }
    }

    void explodeMove(bool bExplode) {
        if (footprintHistory.numEntries > 0) {
            COMPTR<ITerrainMap> map;

            if (SECTOR) {
                SECTOR->GetTerrainMap(this->systemID, map.addr());
                if (map)
                    undoFootprintInfo(map);
            }
        }
        if (OBJMAP && map_sys && map_sys <= MAX_SYSTEMS) {
            OBJMAP->RemoveObjectFromMap(this, map_sys, map_square);
            map_sys = map_square = 0;
        }
    }

    SINGLE completionModifier(void) const {
        return (bCompletionAllowed) ? 1.5 : 1.0; // catch up with host
    }

    void scriptingUpdate(FootprintHistory &oldFootprint, FootprintHistory &newFootprint) {
        if (oldFootprint.numEntries <= 0 || newFootprint.numEntries <= 0) {
            // invalid footprint history
            return;
        }

        // make quick copies
        FootprintHistory lastFootprint = oldFootprint;
        FootprintHistory nextFootprint = newFootprint;

        // make the grid space the center of the LARGE grid square
        lastFootprint.vec[0].centerpos();
        nextFootprint.vec[0].centerpos();

        if (lastFootprint.vec[0] == nextFootprint.vec[0]) {
            // same grid, no change
            return;
        }

        FootprintQuickList fqlLast;
        FootprintQuickList fqlNext;

        // get a list of footprints for the grid ship is leaving
        COMPTR<ITerrainMap> oldMap;
        SECTOR->GetTerrainMap(lastFootprint.systemID, oldMap.addr());
        if (oldMap) {
            oldMap->TestSegment(lastFootprint.vec[0], lastFootprint.vec[0], &fqlLast);
        }

        // get a list of footprints for the grid ship is entering
        COMPTR<ITerrainMap> nextMap;
        SECTOR->GetTerrainMap(nextFootprint.systemID, nextMap.addr());
        if (nextMap) {
            nextMap->TestSegment(nextFootprint.vec[0], nextFootprint.vec[0], &fqlNext);
        }

        // testing for EXITing
        U32 i;
        for (i = 0; i < fqlLast.count; i++) {
            // was this region NOT in the last region?
            if (!fqlNext.hasFootprint(fqlLast.list[i])) {
                // send a Ship is Exiting message to scripting here
                ScriptParameterList params;
                params.Push(this->dwMissionID, "shipID");
                params.Push(fqlLast.list[i].fpi.missionID, "regionID");

                SCRIPTING->CallScriptEvent(SE_SHIP_EXIT, &params);
            }
        }

        // testing for ENTERing
        for (i = 0; i < fqlNext.count; i++) {
            // is this region NOT in the next region?
            if (!fqlLast.hasFootprint(fqlNext.list[i])) {
                // send a Ship is Entering message to scripting here
                ScriptParameterList params;
                params.Push(this->dwMissionID, "shipID");
                params.Push(fqlNext.list[i].fpi.missionID, "regionID");

                SCRIPTING->CallScriptEvent(SE_SHIP_ENTER, &params);
            }
        }
    }
};

//---------------------------------------------------------------------------
//
template<class Base>
ObjectMove<Base>::ObjectMove(void) : physUpdateNode(this, Base::PhysUpdateProc(&ObjectMove::physUpdateMove)),
                                     saveNode(this, Base::SaveLoadProc(&ObjectMove::saveMoveState)),
                                     loadNode(this, Base::SaveLoadProc(&ObjectMove::loadMoveState)),
                                     initNode(this, Base::InitProc(&ObjectMove::initMoveState)),
                                     updateNode(this, Base::UpdateProc(&ObjectMove::updateMoveState)),
                                     onOpCancelNode(this, Base::OnOpCancelProc(&ObjectMove::onOpCancel)),
                                     preTakeoverNode(this, Base::PreTakeoverProc(&ObjectMove::preTakeover)),
                                     receiveOpDataNode(
                                         this, Base::ReceiveOpDataProc(&ObjectMove::receiveOperationData)),
                                     explodeNode(this, Base::ExplodeProc(&ObjectMove::explodeMove)),
                                     genSyncNode(this, Base::SyncGetProc(&ObjectMove::getSyncData),
                                                 Base::SyncPutProc(&ObjectMove::putSyncData)),
                                     genSyncNode2(this, Base::SyncGetProc(&ObjectMove::getSyncPatrolData),
                                                  Base::SyncPutProc(&ObjectMove::putSyncPatrolData)) {
    bAutoMovement = true;
}

//---------------------------------------------------------------------------
//------------------------End TObjMove.h-------------------------------------
//---------------------------------------------------------------------------
