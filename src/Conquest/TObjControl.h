#ifndef TOBJCONTROL_H
#define TOBJCONTROL_H
#include "UserDefaults.h"
//--------------------------------------------------------------------------//
//                                                                          //
//                               TObjControl.h                              //
//                                                                          //
//                  COPYRIGHT (C) 1997 BY DIGITAL ANVIL, INC.               //
//                                                                          //
//--------------------------------------------------------------------------//
/*
    $Header: /Conquest/App/Src/TObjControl.h 30    8/30/00 11:24p Jasony $
*/
//------------------------------- #INCLUDES --------------------------------//
//--------------------------------------------------------------------------//

#ifndef IOBJECT_H
#include "IObject.h"
#endif

#ifndef SUPERTRANS_H
#include "SuperTrans.h"
#endif

#ifndef DBASEDATA_H
#include "DBaseData.h"
#endif

#ifndef ENGINE_H
#include <Engine.h>
#endif

#ifndef _3DMATH_H
#include <3DMath.h>
#endif

#ifndef _INC_STDLIB
#include <stdlib.h>
#endif
#include "vector.h"
//--------------------------------------------------------------------------//
//--------------------------------------------------------------------------//

#define MAX_UPDOWN_VELOCITY 150.0F
#define ANG_SCALE (0.3)          // angVelocity * scale = angVelocity for pitch, roll
#define ZXLATE_SCALE (0.4)		 // scales z accel, velocity
#define ObjectControl _CoC
static bool bUpdateControl = true;

template<class Base>
struct _NO_VTABLE ObjectControl : public Base, DYNAMICS_DATA {
    struct Base::UpdateNode updateNode;
    struct Base::InitNode initNode;
    struct Base::PhysUpdateNode physUpdateNode;

    typedef Base::INITINFO CONTROLINITINFO;

    //----------------------------------

    ObjectControl(void);

    /* ObjectControl methods */

    bool rotateShip(SINGLE relYaw, SINGLE relRoll, SINGLE relPitch) {
        targetYaw = fixAngle(this->transform.get_yaw() + relYaw);
        targetRoll = fixAngle(this->transform.get_roll() + relRoll);
        targetPitch = fixAngle(this->transform.get_pitch() + relPitch);
        bAngleValid = true;

        if (fabs(relYaw) > 0.1 || fabs(relRoll) > 0.1 || fabs(relPitch) > 0.1)
            return false;
        return true;
    }

    void rotateTo(SINGLE absYaw, SINGLE absRoll, SINGLE absPitch) {
        targetYaw = absYaw;
        targetRoll = absRoll;
        targetPitch = absPitch;
        bAngleValid = true;
    }

    void setAltitude(SINGLE relAltitude) {
        if (bPositionValid == false) {
            targetPos = this->transform.translation;
            targetPos.z += relAltitude;
        } else
            targetPos.z = this->transform.translation.z + relAltitude;

        bPositionValid = true;
    }

    bool setPosition(const Vector &relPosition) {
        targetPos = this->transform.translation;
        targetPos += relPosition;
        bPositionValid = true;

        return (relPosition.fast_magnitude() < 50);
    }

    bool setPosition(const Vector &relPosition, SINGLE _finalVel) {
        bool result = setPosition(relPosition);
        finalVelMag = _finalVel;
        bFinalVelocityValid = true;
        return result;
    }

    void moveTo(const Vector &absPosition) {
        targetPos = absPosition;
        bPositionValid = true;
    }

    void moveTo(const Vector &absPosition, SINGLE _finalVel) {
        moveTo(absPosition);
        finalVelMag = _finalVel;
        bFinalVelocityValid = true;
    }

    void setThrustersOn(void) {
        cEnginesOn = 2;
    }

    bool areThrustersOn(void) const {
        return (cEnginesOn > 0);
    }

    const DYNAMICS_DATA &getDynamicsData(void) const {
        return *static_cast<const DYNAMICS_DATA *>(this);
    }

    void setDynamicsData(const DYNAMICS_DATA &data) {
        *static_cast<DYNAMICS_DATA *>(this) = data;
    }

    void restoreDynamicsData(void) {
        *static_cast<DYNAMICS_DATA *>(this) = *pOrigData;
    }

    void restoreLinearDynamicsData(void) {
        linearAcceleration = pOrigData->linearAcceleration;
        maxLinearVelocity = pOrigData->maxLinearVelocity;
    }

    void restoreAngDynamicsData(void) {
        angAcceleration = pOrigData->angAcceleration;
        maxAngVelocity = pOrigData->maxAngVelocity;
    }

    static SINGLE fixAngle(SINGLE angle) {
        if (angle < -PI)
            angle += PI * 2;
        else if (angle > PI)
            angle -= PI * 2;
        return angle;
    }

    // override methods
    void setAbsOverridePosition(const Vector &position) {
        OtargetPos = position;
        bOPositionValid = true;
    }

    void setAbsOverridePosition(const Vector &position, SINGLE _finalVel) {
        setAbsOverridePosition(position);
        OfinalVelMag = _finalVel;
        bOFinalVelocityValid = true;
    }

    void overrideAbsShipRotate(SINGLE yaw, SINGLE roll, SINGLE pitch) {
        OtargetYaw = fixAngle(yaw);
        OtargetRoll = fixAngle(roll);
        OtargetPitch = fixAngle(pitch);
        bOAngleValid = true;
    }

    void setDestabilize(void) {
        bDestabilize = true;
    }

    // avoid "floating away assert" when you are floating on purpose
    void DEBUG_resetInputCounter(void) {
        noInputCountA = noInputCountB = 0;
    }

    static SINGLE calculateCoastingDistance(SINGLE initialVel, SINGLE friction, SINGLE timeInterval) {
        SINGLE distance;

        // Vf^2 = Vi^2 + 2*a*(distance)
        if (friction > 0.0) {
#if 1
            distance = (initialVel * initialVel) / (friction * 2);
            if (initialVel < 0)
                distance = -distance;
            //		distance += initialVel * timeInterval;
#else
            //
            // can not assume smooth deceleration
            //
            // approximate as the sum:   (f + 2f + 3f + ... n*f), where n = (startVel / friction)
            //   ==>  friction * n * (n+1) / 2
            //
            //
            friction *= timeInterval;
            SINGLE d = fabs(initialVel / friction);
            S32 n = d;
            SINGLE r = d - n;

            distance = friction * n * (n + 1) * 0.5;
            distance += r * friction * timeInterval; // add in fractional part
            if (initialVel < 0)
                distance = -distance;
            // testing!!
            //		distance += initialVel * ELAPSED_TIME;
#endif
        } else
            distance = 1E5;

        return distance;
    }

    static SINGLE calculateCoastingDistance(SINGLE finalVel, SINGLE initialVel, SINGLE friction, SINGLE timeInterval) {
        SINGLE distance;

        // Vf^2 = Vi^2 + 2*a*(distance)
        if (friction > 0.0) {
#if 1
            SINGLE iVel = initialVel * initialVel;
            SINGLE fVel = finalVel * finalVel;
            if (initialVel < 0)
                iVel = -iVel;
            if (finalVel < 0)
                fVel = -fVel;
            distance = (iVel - fVel) / (friction * 2);
            distance += initialVel * timeInterval;
#else
            //
            // can not assume smooth deceleration
            //
            // approximate as the sum:   (f + 2f + 3f + ... n*f), where n = (startVel / friction)
            //   ==>  friction * n * (n+1) / 2
            //
            //
            friction *= timeInterval;
            SINGLE d = fabs((initialVel - finalVel) / friction);
            S32 n = d;
            SINGLE r = d - n;

            distance = friction * n * (n + 1) * 0.5;
            distance += r * friction * timeInterval; // add in fractional part
            if ((initialVel - finalVel) < 0)
                distance = -distance;
            distance += initialVel * timeInterval;
#endif
        } else
            distance = 1E5;

        return distance;
    }

private:
    const DYNAMICS_DATA *pOrigData;
    Vector targetPos;
    SINGLE finalVelMag;
    SINGLE targetYaw, targetRoll, targetPitch;
    bool bAngleValid;
    bool bPositionValid;
    bool bFinalVelocityValid;
    bool bPhysicalUpdateHappened;
    C8 cEnginesOn;
    //
    // data for external overrides (fields that push things around)
    //
    bool bOAngleValid;
    bool bOPositionValid;
    bool bOFinalVelocityValid;
    bool bDestabilize;
    Vector OtargetPos;
    SINGLE OfinalVelMag;
    SINGLE OtargetYaw, OtargetRoll, OtargetPitch;
    //
    // debugging
    //
    int noInputCountA, noInputCountB; // incrmemented when no inputs were received


    /* private methods */

    void initControl(const CONTROLINITINFO &data) {
        *static_cast<DYNAMICS_DATA *>(this) = data.pData->dynamicsData;
        pOrigData = &data.pData->dynamicsData;
    }

    BOOL32 updateControl(void) {
        if (!bUpdateControl)
            return 1;

        if (bPhysicalUpdateHappened)
            goto Done;

        if (bDestabilize) {
            bFinalVelocityValid = bOPositionValid = bOAngleValid = bAngleValid = false;
            bPositionValid = true;
            targetPos = this->transform.translation;
        }

        if (bOAngleValid) {
            targetYaw = OtargetYaw;
            targetRoll = OtargetRoll;
            targetPitch = OtargetPitch;
            bOAngleValid = false;
            bAngleValid = true;
        }

        if (bOPositionValid) {
            targetPos = OtargetPos;
            bFinalVelocityValid = bOFinalVelocityValid;
            bOPositionValid = bOFinalVelocityValid = false;
            bPositionValid = true;
            finalVelMag = OfinalVelMag;
        }

        //
        // do ultra-cheap update...
        // calculate angular velocity needed
        //
        if (bAngleValid) {
            SINGLE diff = fixAngle(targetYaw - this->transform.get_yaw());
            SINGLE vel;
            SINGLE coast = calculateCoastingDistance(-this->ang_velocity.z, angAcceleration, ELAPSED_TIME);

            SINGLE newdiff = fixAngle(diff - coast);

            vel = newdiff / ELAPSED_TIME;
            if (vel > maxAngVelocity)
                vel = maxAngVelocity;
            else if (vel < -maxAngVelocity)
                vel = -maxAngVelocity;

            vel = limitAcceleration(-this->ang_velocity.z, vel, angAcceleration, ELAPSED_TIME);

            this->ang_velocity.z = -vel;

            // see if we're about to go too far
            if (diff > 0) {
                SINGLE newvel = diff / ELAPSED_TIME;

                if (newvel < vel) // we're going too fast!
                    this->ang_velocity.z = -newvel;
            } else {
                SINGLE newvel = diff / ELAPSED_TIME;

                if (newvel > vel) // we're going too fast!
                    this->ang_velocity.z = -newvel;
            }
            //
            // now do roll
            //
            diff = fixAngle(targetRoll - this->transform.get_roll());
            vel = REALTIME_FRAMERATE * diff;
            if (vel > maxAngVelocity * ANG_SCALE)
                vel = maxAngVelocity * ANG_SCALE;
            else if (vel < -maxAngVelocity * ANG_SCALE)
                vel = -maxAngVelocity * ANG_SCALE;

            SINGLE oldVel = -dot_product(this->ang_velocity, this->transform.get_j());
            this->ang_velocity -= this->transform.get_j() * (vel - oldVel); // subtract because k is backwards

            // now do pitch
            diff = fixAngle(targetPitch - this->transform.get_pitch());
            vel = REALTIME_FRAMERATE * diff;
            if (vel > maxAngVelocity * ANG_SCALE)
                vel = maxAngVelocity * ANG_SCALE;
            else if (vel < -maxAngVelocity * ANG_SCALE)
                vel = -maxAngVelocity * ANG_SCALE;

            oldVel = dot_product(this->ang_velocity, this->transform.get_i());
            this->ang_velocity += this->transform.get_i() * (vel - oldVel);
        }

        //DoneAngle:

        if (bPositionValid) {
            Vector distance = targetPos - this->transform.translation;
            SINGLE diff = distance.fast_magnitude();
            SINGLE vel;

            vel = REALTIME_FRAMERATE * diff;
            if (vel > maxLinearVelocity)
                vel = maxLinearVelocity;
            else if (vel < -maxLinearVelocity)
                vel = -maxLinearVelocity;
            else {
                // we are close enough for highway work, just set position and call it a day
                this->transform.translation = targetPos;
                vel = diff = 0;
            }

            if (bFinalVelocityValid) {
                if (vel >= 0)
                    vel = __max(vel, finalVelMag);
                else
                    vel = __min(vel, -finalVelMag);
            }

            if (diff > 1)
                this->velocity = distance * (vel / diff);
            else
                this->velocity.zero();
        }


    Done:
        // debugging
        if (bPositionValid == 0) {
            if (this->bExploding == 0) {
                noInputCountA++;
                if (noInputCountA == 3)
                    CQBOMB1("Object 0x%X is floating away...(ignorable)", this->GetPartID());
            }
        } else
            noInputCountA = 0;

        if (bAngleValid == 0 && bDestabilize == 0) {
            if (this->bExploding == 0) {
                noInputCountB++;
                if (noInputCountB == 3)
                    CQBOMB1("Object 0x%X is tumbling...(ignorable)", this->GetPartID());
            }
        } else
            noInputCountB = 0;

        bDestabilize = false;
        bFinalVelocityValid = false;
        bPositionValid = false;
        bAngleValid = false;
        bOAngleValid = false;
        bOPositionValid = false;
        bOFinalVelocityValid = false;

        bPhysicalUpdateHappened = false;
        if (cEnginesOn)
            cEnginesOn--;

        return 1;
    }

    void physUpdateControl(SINGLE dt) {
        const USER_DEFAULTS *const pDefaults = DEFAULTS->GetDefaults();

        if (this->bVisible == 0 || pDefaults->bCheapMovement || (
                pDefaults->bConstUpdateRate && pDefaults->gameSpeed != 0))
            return;

        if (bDestabilize) {
            bFinalVelocityValid = bOPositionValid = bOAngleValid = bAngleValid = false;
            bPositionValid = true;
            targetPos = this->transform.translation;
        }

        if (bOAngleValid) {
            targetYaw = OtargetYaw;
            targetRoll = OtargetRoll;
            targetPitch = OtargetPitch;
            bOAngleValid = false;
            bAngleValid = true;
        }

        if (bOPositionValid) {
            targetPos = OtargetPos;
            bFinalVelocityValid = bOFinalVelocityValid;
            bOPositionValid = bOFinalVelocityValid = false;
            bPositionValid = true;
            finalVelMag = OfinalVelMag;
        }

        if (bAngleValid) {
            SINGLE diff = fixAngle(targetYaw - this->transform.get_yaw());
            SINGLE vel;
            SINGLE coast = calculateCoastingDistance(-this->ang_velocity.z, angAcceleration, dt);

            SINGLE newdiff = fixAngle(diff - coast);

            vel = newdiff / dt;
            if (vel > maxAngVelocity)
                vel = maxAngVelocity;
            else if (vel < -maxAngVelocity)
                vel = -maxAngVelocity;

            vel = limitAcceleration(-this->ang_velocity.z, vel, angAcceleration, dt);

            this->ang_velocity.z = -vel;

            // see if we're about to go too far
            if (diff > 0) {
                SINGLE newvel = diff / dt;

                if (newvel < vel) // we're going too fast!
                    this->ang_velocity.z = -newvel;
            } else {
                SINGLE newvel = diff / dt;

                if (newvel > vel) // we're going too fast!
                    this->ang_velocity.z = -newvel;
            }


            //
            // now do roll
            //
            diff = fixAngle(targetRoll - this->transform.get_roll());
            SINGLE oldVel = -dot_product(this->ang_velocity, this->transform.get_j());
            coast = calculateCoastingDistance(oldVel, angAcceleration * ANG_SCALE, dt);
            newdiff = fixAngle(diff - coast);

            vel = newdiff / dt;
            if (vel > maxAngVelocity * ANG_SCALE)
                vel = maxAngVelocity * ANG_SCALE;
            else if (vel < -maxAngVelocity * ANG_SCALE)
                vel = -maxAngVelocity * ANG_SCALE;


            vel = limitAcceleration(oldVel, vel, angAcceleration * ANG_SCALE, dt);

            // see if we're about to go too far
            if (diff > 0) {
                SINGLE newvel = diff / dt;

                if (newvel < vel) // we're going too fast!
                    vel = newvel;
            } else {
                SINGLE newvel = diff / dt;

                if (newvel > vel) // we're going too fast!
                    vel = newvel;
            }

            this->ang_velocity -= this->transform.get_j() * (vel - oldVel); // subtract because k is backwards

            //
            // now do pitch
            //
            diff = fixAngle(targetPitch - this->transform.get_pitch());
            oldVel = dot_product(this->ang_velocity, this->transform.get_i());
            coast = calculateCoastingDistance(oldVel, angAcceleration * ANG_SCALE, dt);
            newdiff = fixAngle(diff - coast);

            vel = newdiff / dt;
            if (vel > maxAngVelocity * ANG_SCALE)
                vel = maxAngVelocity * ANG_SCALE;
            else if (vel < -maxAngVelocity * ANG_SCALE)
                vel = -maxAngVelocity * ANG_SCALE;

            vel = limitAcceleration(oldVel, vel, angAcceleration * ANG_SCALE, dt);

            // see if we're about to go too far
            if (diff > 0) {
                SINGLE newvel = diff / dt;

                if (newvel < vel) // we're going too fast!
                    vel = newvel;
            } else {
                SINGLE newvel = diff / dt;

                if (newvel > vel) // we're going too fast!
                    vel = newvel;
            }

            this->ang_velocity += this->transform.get_i() * (vel - oldVel);
        }

        if (bPositionValid) {
            //
            // first do the z difference
            //
            Vector distance = targetPos - this->transform.translation;
            SINGLE diff = distance.z;
            SINGLE oldVel = this->velocity.z;
            this->velocity.z = 0;
            SINGLE old2DVel;
            SINGLE coast = calculateCoastingDistance(oldVel, linearAcceleration * ZXLATE_SCALE, dt);
            SINGLE newdiff = diff - coast;
            SINGLE vel;

            //
            // calculate old2DVel for later
            //
            {
                Vector dir = distance;
                dir.z = 0;
                if (dir.x || dir.y)
                    dir.normalize();

                old2DVel = dot_product(dir, this->velocity);
            }

            vel = newdiff / dt;
            if (vel > maxLinearVelocity * ZXLATE_SCALE)
                vel = maxLinearVelocity * ZXLATE_SCALE;
            else if (vel < -maxLinearVelocity * ZXLATE_SCALE)
                vel = -maxLinearVelocity * ZXLATE_SCALE;

            vel = limitAcceleration(oldVel, vel, linearAcceleration * ZXLATE_SCALE, dt);

            // see if we're about to go too far
            if (diff > 0) {
                SINGLE newvel = diff / dt;

                if (newvel < vel) // we're going too fast!
                    vel = newvel;
            } else {
                SINGLE newvel = diff / dt;

                if (newvel > vel) // we're going too fast!
                    vel = newvel;
            }

            this->velocity.z = vel;

            //
            // now do the 2D portion of the velocity
            //
            distance.z = 0;
            diff = distance.magnitude();
            coast = calculateCoastingDistance(old2DVel, linearAcceleration, dt);
            newdiff = diff - coast;

            vel = newdiff / dt;
            if (vel > maxLinearVelocity)
                vel = maxLinearVelocity;
            else if (vel < -maxLinearVelocity)
                vel = -maxLinearVelocity;

            if (bFinalVelocityValid) {
                if (vel > 0)
                    vel = __max(vel, finalVelMag);
                else
                    vel = __min(vel, -finalVelMag);
            }

            vel = limitAcceleration(old2DVel, vel, linearAcceleration, dt);

            // see if we're about to go too far
            if (diff > 0) {
                SINGLE newvel = diff / dt;

                if (newvel < vel) // we're going too fast!
                    vel = newvel;
            } else {
                SINGLE newvel = diff / dt;

                if (newvel > vel) // we're going too fast!
                    vel = newvel;
            }

            this->velocity.x = this->velocity.y = 0;
            if (diff > 1)
                this->velocity += distance * (vel / diff);
        }

        bPhysicalUpdateHappened = true;
    }

    static SINGLE limitAcceleration(SINGLE oldVel, SINGLE desiredVel, SINGLE acceleration, SINGLE dt) {
        SINGLE change = desiredVel - oldVel;

        acceleration *= dt;		// max change allowed

        if (fabs(change) <= acceleration)
            return desiredVel;
        if (change < 0)
            return oldVel - acceleration;
        return oldVel + acceleration;
    }
};

//---------------------------------------------------------------------------
//
template<class Base>
ObjectControl<Base>::ObjectControl(void) : updateNode(this, Base::UpdateProc(&ObjectControl::updateControl)),
                                           initNode(this, Base::InitProc(&ObjectControl::initControl)),
                                           physUpdateNode(
                                               this, Base::PhysUpdateProc(&ObjectControl::physUpdateControl)) {
}

//---------------------------------------------------------------------------
//------------------------End TObjControl.h----------------------------------
//---------------------------------------------------------------------------
#endif
