#ifndef TOBJPHYS_H
#define TOBJOHYS_H
//--------------------------------------------------------------------------//
//                                                                          //
//                                 TObjPhys.h                              //
//                                                                          //
//                  COPYRIGHT (C) 2000 BY DIGITAL ANVIL, INC.               //
//                                                                          //
//--------------------------------------------------------------------------//
/*
    $Header: /Conquest/App/Src/TObjPhys.h 1     2/24/00 4:41p Jasony $
*/			    
//------------------------------- #INCLUDES --------------------------------//
//--------------------------------------------------------------------------//

#ifndef IOBJECT_H
#include "IObject.h"
#endif

#ifndef SUPERTRANS_H
#include "SuperTrans.h"
#endif

#ifndef _3DMATH_H
#include <3DMath.h>
#endif

#define ObjectPhysics _CoP
//--------------------------------------------------------------------------//
//--------------------------------------------------------------------------//
template <class Base=IBaseObject> 
struct _NO_VTABLE ObjectPhysics : public Base
{
	struct Base::InitNode			initNode;
	struct Base::PhysUpdateNode physUpdateNode;
	typedef Base::INITINFO PHYSICSINITINFO;

	bool bEnablePhysics;


	//----------------------------------
	
	ObjectPhysics (void);

	void initPhysics (const PHYSICSINITINFO & data);
	void physUpdatePhysics (SINGLE dt);


private:
	const Vector * pArm;
};

//---------------------------------------------------------------------------
//
template <class Base> 
ObjectPhysics< Base >::ObjectPhysics (void) :
					initNode(this, Base::InitProc(&ObjectPhysics::initPhysics)),
					physUpdateNode(this, Base::PhysUpdateProc(&ObjectPhysics::physUpdatePhysics))
{
	bEnablePhysics = true;
}
//---------------------------------------------------------------------------
//
template <class Base> 
void ObjectPhysics< Base >::initPhysics (const PHYSICSINITINFO & data)
{
	if (data.rigidBodyArm.x!=0 && data.rigidBodyArm.y!=0 && data.rigidBodyArm.z!=0)
		pArm = &data.rigidBodyArm;
}
//---------------------------------------------------------------------------
//
template <class Base> 
void ObjectPhysics< Base >::physUpdatePhysics (SINGLE dt)
{
	if (bEnablePhysics)
	{
		Quaternion q;
		Vector x;
		Matrix & R = this->transform;

		q.set(R);
		x = this->transform.translation;

		// recalc center_of_mass
		if (pArm)
			x -= R * *pArm;
		
		// update position.
		x += this->velocity * dt;

		// update ang position
		Quaternion qw(this->ang_velocity);
		Quaternion qdot = qw * q * 0.5;
		q.w += qdot.w * dt;
		q.x += qdot.x * dt;
		q.y += qdot.y * dt;
		q.z += qdot.z * dt;
		q.normalize();

	// be sure to sync rotation matrix with quaternion:
		R = q;

		// now resync position
		this->transform.translation = x;
		if (pArm)
			this->transform.translation += R * *pArm;
	}
}

//---------------------------------------------------------------------------
//---------------------------End TObjPhys.h----------------------------------
//---------------------------------------------------------------------------
#endif