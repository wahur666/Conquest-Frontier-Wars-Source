//--------------------------------------------------------------------------//
//                                                                          //
//                              ObjControl.cpp                              //
//                                                                          //
//                  COPYRIGHT (C) 2000 BY DIGITAL ANVIL, INC.               //
//                                                                          //
//--------------------------------------------------------------------------//
/*
    $Header: /Conquest/App/Src/ObjControl.cpp 11    9/22/00 11:38a Jasony $
*/			    
//------------------------------- #INCLUDES --------------------------------//
//--------------------------------------------------------------------------//

#include "pch.h"
#include <globals.h>

#include "TObjControl.h"
#include "TObjPhys.h"
#include "TObjTrans.h"
#include "TObjFrame.h"
#include "UserDefaults.h"

#include <DSpaceship.h>
#include <DShipSave.h>
#include <DFighter.h>

typedef SPACESHIP_INIT<BASE_SPACESHIP_DATA> BASESHIPINIT;
//--------------------------------------------------------------------------//
//

static bool bUpdateControl = true;
template <class Base> 
BOOL32 ObjectControl< Base >::updateControl (void)
{
	if(!bUpdateControl)
		return 1;

	if (bPhysicalUpdateHappened)
		goto Done;

	if (bDestabilize)
	{
		bFinalVelocityValid = bOPositionValid = bOAngleValid = bAngleValid = false;
		bPositionValid = true;
		targetPos = this->transform.translation;
	}
		
	if (bOAngleValid)
	{
		targetYaw = OtargetYaw;
		targetRoll = OtargetRoll;
		targetPitch = OtargetPitch;
		bOAngleValid = false;
		bAngleValid = true;
	}

	if (bOPositionValid)
	{
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
	if (bAngleValid)
	{
		SINGLE diff = fixAngle(targetYaw - this->transform.get_yaw());
		SINGLE vel;
		SINGLE coast = calculateCoastingDistance(-this->ang_velocity.z, angAcceleration, ELAPSED_TIME);

		SINGLE newdiff = fixAngle(diff - coast);

		vel = newdiff / ELAPSED_TIME;
		if (vel > maxAngVelocity)
			vel = maxAngVelocity;
		else
		if (vel < -maxAngVelocity)
			vel = -maxAngVelocity;

		vel = limitAcceleration(-this->ang_velocity.z, vel, angAcceleration, ELAPSED_TIME);

		this->ang_velocity.z = -vel;

		// see if we're about to go too far
		if (diff > 0)
		{
			SINGLE newvel = diff / ELAPSED_TIME;

			if (newvel < vel)		// we're going too fast!
				this->ang_velocity.z = -newvel;
		}
		else
		{
			SINGLE newvel = diff / ELAPSED_TIME;

			if (newvel > vel)		// we're going too fast!
				this->ang_velocity.z = -newvel;
		}
		//
		// now do roll
		//
		diff = fixAngle(targetRoll - this->transform.get_roll());
		vel = REALTIME_FRAMERATE * diff;
		if (vel > maxAngVelocity*ANG_SCALE)
			vel = maxAngVelocity*ANG_SCALE;
		else
		if (vel < -maxAngVelocity*ANG_SCALE)
			vel = -maxAngVelocity*ANG_SCALE;

		SINGLE oldVel = -this->dot_product(this->ang_velocity, this->transform.get_j());
		this->ang_velocity -= this->transform.get_j() * (vel - oldVel);	// subtract because k is backwards

		// now do pitch
		diff = fixAngle(targetPitch - this->transform.get_pitch());
		vel = REALTIME_FRAMERATE * diff;
		if (vel > maxAngVelocity*ANG_SCALE)
			vel = maxAngVelocity*ANG_SCALE;
		else
		if (vel < -maxAngVelocity*ANG_SCALE)
			vel = -maxAngVelocity*ANG_SCALE;

		oldVel = this->dot_product(this->ang_velocity, this->transform.get_i());
		this->ang_velocity += this->transform.get_i() * (vel - oldVel);
	}

//DoneAngle:

	if (bPositionValid)
	{
		Vector distance = targetPos - this->transform.translation;
		SINGLE diff = distance.fast_magnitude();
		SINGLE vel;

		vel = REALTIME_FRAMERATE * diff;
		if (vel > maxLinearVelocity)
			vel = maxLinearVelocity;
		else
		if (vel < -maxLinearVelocity)
			vel = -maxLinearVelocity;
		else
		{
			// we are close enough for highway work, just set position and call it a day
			this->transform.translation = targetPos;
			vel = diff = 0;
		}

		if (bFinalVelocityValid)
		{
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
	if (bPositionValid==0)
	{
		if (this->bExploding==0)
		{
			noInputCountA++;
			if (noInputCountA == 3)
				CQBOMB1("Object 0x%X is floating away...(ignorable)", this->GetPartID());
		}
	}
	else
		noInputCountA = 0;

	if (bAngleValid==0 && bDestabilize==0)
	{
		if (this->bExploding==0)
		{
			noInputCountB++;
			if (noInputCountB == 3)
				CQBOMB1("Object 0x%X is tumbling...(ignorable)", this->GetPartID());
		}
	}
	else
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
//---------------------------------------------------------------------------
//
template <class Base> 
void ObjectControl< Base >::physUpdateControl (SINGLE dt)
{
	const USER_DEFAULTS * const pDefaults = DEFAULTS->GetDefaults();

	if (this->bVisible==0 || pDefaults->bCheapMovement || (pDefaults->bConstUpdateRate && pDefaults->gameSpeed!=0))
		return;

	if (bDestabilize)
	{
		bFinalVelocityValid = bOPositionValid = bOAngleValid = bAngleValid = false;
		bPositionValid = true;
		targetPos = this->transform.translation;
	}
		
	if (bOAngleValid)
	{
		targetYaw = OtargetYaw;
		targetRoll = OtargetRoll;
		targetPitch = OtargetPitch;
		bOAngleValid = false;
		bAngleValid = true;
	}

	if (bOPositionValid)
	{
		targetPos = OtargetPos;
		bFinalVelocityValid = bOFinalVelocityValid;
		bOPositionValid = bOFinalVelocityValid = false;
		bPositionValid = true;
		finalVelMag = OfinalVelMag;
	}

	if (bAngleValid)
	{
		SINGLE diff = fixAngle(targetYaw - this->transform.get_yaw());
		SINGLE vel;
		SINGLE coast = calculateCoastingDistance(-this->ang_velocity.z, angAcceleration, dt);

		SINGLE newdiff = fixAngle(diff - coast);

		vel = newdiff / dt;
		if (vel > maxAngVelocity)
			vel = maxAngVelocity;
		else
		if (vel < -maxAngVelocity)
			vel = -maxAngVelocity;

		vel = limitAcceleration(-this->ang_velocity.z, vel, angAcceleration, dt);

		this->ang_velocity.z = -vel;

		// see if we're about to go too far
		if (diff > 0)
		{
			SINGLE newvel = diff / dt;

			if (newvel < vel)		// we're going too fast!
				this->ang_velocity.z = -newvel;
		}
		else
		{
			SINGLE newvel = diff / dt;

			if (newvel > vel)		// we're going too fast!
				this->ang_velocity.z = -newvel;
		}


		//
		// now do roll
		//
		diff = fixAngle(targetRoll - this->transform.get_roll());
		SINGLE oldVel = -this->dot_product(this->ang_velocity, this->transform.get_j());
		coast = calculateCoastingDistance(oldVel, angAcceleration*ANG_SCALE, dt);
		newdiff = fixAngle(diff - coast);

		vel = newdiff / dt;
		if (vel > maxAngVelocity*ANG_SCALE)
			vel = maxAngVelocity*ANG_SCALE;
		else
		if (vel < -maxAngVelocity*ANG_SCALE)
			vel = -maxAngVelocity*ANG_SCALE;


		vel = limitAcceleration(oldVel, vel, angAcceleration*ANG_SCALE, dt);

		// see if we're about to go too far
		if (diff > 0)
		{
			SINGLE newvel = diff / dt;

			if (newvel < vel)		// we're going too fast!
				vel = newvel;
		}
		else
		{
			SINGLE newvel = diff / dt;

			if (newvel > vel)		// we're going too fast!
				vel = newvel;
		}

		this->ang_velocity -= this->transform.get_j() * (vel - oldVel);	// subtract because k is backwards

		//
		// now do pitch
		//
		diff = fixAngle(targetPitch - this->transform.get_pitch());
		oldVel = this->dot_product(this->ang_velocity, this->transform.get_i());
		coast = calculateCoastingDistance(oldVel, angAcceleration*ANG_SCALE, dt);
		newdiff = fixAngle(diff - coast);
		
		vel = newdiff / dt;
		if (vel > maxAngVelocity*ANG_SCALE)
			vel = maxAngVelocity*ANG_SCALE;
		else
		if (vel < -maxAngVelocity*ANG_SCALE)
			vel = -maxAngVelocity*ANG_SCALE;

		vel = limitAcceleration(oldVel, vel, angAcceleration*ANG_SCALE, dt);

		// see if we're about to go too far
		if (diff > 0)
		{
			SINGLE newvel = diff / dt;

			if (newvel < vel)		// we're going too fast!
				vel = newvel;
		}
		else
		{
			SINGLE newvel = diff / dt;

			if (newvel > vel)		// we're going too fast!
				vel = newvel;
		}

		this->ang_velocity += this->transform.get_i() * (vel - oldVel);
		
	}

	if (bPositionValid)
	{
		//
		// first do the z difference
		//
		Vector distance = targetPos - this->transform.translation;
		SINGLE diff = distance.z;
		SINGLE oldVel = this->velocity.z;
		this->velocity.z = 0;
		SINGLE old2DVel;
		SINGLE coast = calculateCoastingDistance(oldVel, linearAcceleration*ZXLATE_SCALE, dt);
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
		if (vel > maxLinearVelocity*ZXLATE_SCALE)
			vel = maxLinearVelocity*ZXLATE_SCALE;
		else
		if (vel < -maxLinearVelocity*ZXLATE_SCALE)
			vel = -maxLinearVelocity*ZXLATE_SCALE;

		vel = limitAcceleration(oldVel, vel, linearAcceleration*ZXLATE_SCALE, dt);

		// see if we're about to go too far
		if (diff > 0)
		{
			SINGLE newvel = diff / dt;

			if (newvel < vel)		// we're going too fast!
				vel = newvel;
		}
		else
		{
			SINGLE newvel = diff / dt;

			if (newvel > vel)		// we're going too fast!
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
		else
		if (vel < -maxLinearVelocity)
			vel = -maxLinearVelocity;

		if (bFinalVelocityValid)
		{
			if (vel > 0)
				vel = __max(vel, finalVelMag);
			else
				vel = __min(vel, -finalVelMag);
		}

		vel = limitAcceleration(old2DVel, vel, linearAcceleration, dt);

		// see if we're about to go too far
		if (diff > 0)
		{
			SINGLE newvel = diff / dt;

			if (newvel < vel)		// we're going too fast!
				vel = newvel;
		}
		else
		{
			SINGLE newvel = diff / dt;

			if (newvel > vel)		// we're going too fast!
				vel = newvel;
		}

		this->velocity.x = this->velocity.y = 0;
		if (diff > 1)
			this->velocity += distance * (vel / diff);
	}

	bPhysicalUpdateHappened = true;
}
//---------------------------------------------------------------------------
//
template <class Base> 
SINGLE ObjectControl< Base >::limitAcceleration (SINGLE oldVel, SINGLE desiredVel, SINGLE acceleration, SINGLE dt)
{
	SINGLE change = desiredVel - oldVel;

	acceleration *= dt;		// max change allowed

	if (fabs(change) <= acceleration)
		return desiredVel;
	else
	if (change < 0)
		return oldVel - acceleration;
	else
		return oldVel + acceleration;
}
//---------------------------------------------------------------------------
//
template <class Base> 
SINGLE ObjectControl< Base >::calculateCoastingDistance (SINGLE initialVel, SINGLE friction, SINGLE timeInterval)
{
	SINGLE distance;

	// Vf^2 = Vi^2 + 2*a*(distance)
	if (friction > 0.0)
	{
#if 1
 		distance = (initialVel*initialVel)/(friction*2);
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
		
		distance = friction * n * (n+1) * 0.5;
		distance += r * friction * timeInterval;	// add in fractional part
		if (initialVel < 0)
			distance = -distance;
		// testing!!
		//		distance += initialVel * ELAPSED_TIME;
#endif
	}
	else
		distance = 1E5;

	return distance;
}
//---------------------------------------------------------------------------
//
template <class Base> 
SINGLE ObjectControl< Base >::calculateCoastingDistance (SINGLE finalVel, SINGLE initialVel, SINGLE friction, SINGLE timeInterval)
{
	SINGLE distance;

	// Vf^2 = Vi^2 + 2*a*(distance)
	if (friction > 0.0)
	{
#if 1
		SINGLE iVel = initialVel*initialVel;
		SINGLE fVel = finalVel*finalVel;
		if (initialVel < 0)
			iVel = -iVel;
		if (finalVel < 0)
			fVel = -fVel;
 		distance = (iVel-fVel)/(friction*2);
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
		SINGLE d = fabs((initialVel-finalVel) / friction);
		S32 n = d;
		SINGLE r = d - n;
		
		distance = friction * n * (n+1) * 0.5;
		distance += r * friction * timeInterval;	// add in fractional part
		if ((initialVel-finalVel) < 0)
			distance = -distance;
		distance += initialVel * timeInterval;
#endif
	}
	else
		distance = 1E5;

	return distance;
}
//----------------------------------------------------------------------------
//------------------------End ObjControl.cpp----------------------------------
//----------------------------------------------------------------------------
