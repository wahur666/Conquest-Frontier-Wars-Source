//--------------------------------------------------------------------------//
//                                                                          //
//                                Damage.cpp                                //
//                                                                          //
//                  COPYRIGHT (C) 1998 BY DIGITAL ANVIL, INC.               //
//                                                                          //
//--------------------------------------------------------------------------//
/*
    $Header: /Conquest/App/Src/Damage.cpp 25    10/13/00 12:04p Jasony $
*/			    
//------------------------------- #INCLUDES --------------------------------//
//--------------------------------------------------------------------------//

#include "pch.h"
#include <globals.h>

#include "Damage.h"
#include "Objlist.h"
#include "TObjExtension.h"
#include "TObjDamage.h"
#include "TObjExtent.h"
#include "TObjWarp.h"
#include "TObjMove.h"
#include "TObjControl.h"
#include "TObjPhys.h"
#include "TObjTrans.h"
#include "TObjFrame.h"
#include "TObjSelect.h"
#include "TObjMission.h"
#include "TObjBuild.h"
#include "Field.h"

#include <DSpaceship.h>
#include <DShipSave.h>
#include <DBaseData.h>
#include <DPlatform.h>
#include <DPlatSave.h>

bool SphereTriangle  (Vector v[3],Vector pt,SINGLE rad);

struct ShieldMap shieldMapBank[NUM_SHIELD_MAPS];

ShieldMap * GetShieldHitMap()
{
	for (int i=0;i<NUM_SHIELD_MAPS;i++)
	{
		if (shieldMapBank[i].poly_cnt == 0)
			return &shieldMapBank[i];
	}

	return 0;
}

typedef SPACESHIP_INIT<BASE_SPACESHIP_DATA> BASESHIPINIT;

//--------------------------------------------------------------------------//
//

typedef PLATFORM_INIT<BASE_PLATFORM_DATA> BASEPLATINIT;


#if 0
template <class Base>
void ObjectDamage< Base >::GenerateShieldHit(const Vector & pos,const Vector &dir,U32 damage)
{

	const BaseExtent *extent,*e_pos,*cm_extent=0;
	CollisionMesh *cmesh=0;
	PHYSICS->get_extent(&extent,instanceIndex);
	if (extent)
	{
		//find first convex mesh for now
		while (extent && cmesh==0)
		{
			e_pos = extent;
			while (e_pos && cmesh==0)
			{
				if (e_pos->type == ET_CONVEX_MESH)
				{
					cmesh = (CollisionMesh *)e_pos->get_primitive();
					cm_extent = e_pos;
				}
				e_pos = e_pos->next;
			}
			extent = extent->child;
		}
		
		if (cmesh)
		{
			
			if (vertex_normals == 0)
				GenerateNormals(cmesh);
			
			S32 slot=-1;
			for (int cc=0;cc<NUM_SHIELD_HITS;cc++)
			{
				if (shieldHitPolys[cc] == 0)
					slot = cc;
			}
			if (slot != -1)
			{
				shieldHitPolys[slot] = GetShieldHitMap();
				if (shieldHitPolys[slot])
				{
					Vector norm;
					Vector collide_point;
					//	TRANSFORM scaleTrans;
					//	scaleTrans.d[0][0] = SHIELD_SCALE;
					//	scaleTrans.d[1][1] = SHIELD_SCALE;
					//	scaleTrans.d[2][2] = SHIELD_SCALE;
					
					//TRANSFORM trans = scaleTrans*transform;
					Vector opos = transform.inverse_rotate_translate(pos);
					Vector odir = transform.inverse_rotate(dir);
					opos = opos/SHIELD_SCALE;
					odir.normalize();
					if (target->GetModelCollisionPosition(collide_point,norm, pos-dir*1000, dir))
					{
						//collide_point = trans.inverse_rotate_translate(collide_point);
						//	norm = trans.inverse_rotate(norm);
						
						Vector base_i,base_j;
						base_i.set(-norm.y,norm.x,0);
						if (base_i.x == 0 && base_i.y ==0)
							base_i.set(0,0,1);
						else
							base_i.normalize();
						
						base_j = cross_product(norm,base_i);
						
						//TEST CODE START
						/*	shieldHitPolys.poly_cnt = 1;
						shieldHitPolys.v[0][0] = collide_point-base_j*400;
						shieldHitPolys.v[0][1] = collide_point+base_j*200-base_i*300;
						shieldHitPolys.v[0][2] = collide_point+base_j*200+base_i*300;
						shieldHitPolys.t[0][0].u = 0.5;
						shieldHitPolys.t[0][0].v = -0.333333;
						shieldHitPolys.t[0][1].u = -0.333333;
						shieldHitPolys.t[0][1].v = 1.0;
						shieldHitPolys.t[0][2].u = 1.333333;
						shieldHitPolys.t[0][2].v = 1.0;
						
						return;*/
						
						//TEST CODE END
						U32 next_poly = 0;
						
						for (int t=0;t<cmesh->num_triangles;t++)
						{
							TexCoord tc[3];
							Vector v[3];
							Vector n[3];
							Vector i,j;
							
							v[0] = cmesh->vertices[cmesh->triangles[t].v[0]].p;
							v[1] = cmesh->vertices[cmesh->triangles[t].v[1]].p;
							v[2] = cmesh->vertices[cmesh->triangles[t].v[2]].p;
							
							/*	n = cmesh->normals[cmesh->triangles[t].normal];
							
							  SINGLE angle = acos(dot_product(norm,n));
							  if (angle)
							  {
							  Vector rot = cross_product(norm,n);
							  Quaternion quat(rot,angle);
							  Vector test = quat.transform(norm);
							  i = quat.transform(base_i);
							  j = quat.transform(base_j);
							  }
							  else
							  {
							  i = base_i;
							  j = base_j;
						}*/
							
							SINGLE dist[3],minDist=99999;
							int minVert=4;
							
							for (int c=0;c<3;c++)
							{
								dist[c] = (v[c]-collide_point).magnitude();
								if (dist[c] < minDist)
								{
									minDist = dist[c];
									minVert = c;
								}
							}
							
							//define BBOX_MAX__DIST 800.0
							
							SINGLE BBOX_MAX__DIST = 400+box[BBOX_MAX_Z]*0.15;
							
							n[0] = vertex_normals[cmesh->triangles[t].v[0]];
							n[1] = vertex_normals[cmesh->triangles[t].v[1]];
							n[2] = vertex_normals[cmesh->triangles[t].v[2]];
							
							SINGLE n_dot[3];
							
							n_dot[0] = dot_product(n[0],norm);
							n_dot[1] = dot_product(n[1],norm);
							n_dot[2] = dot_product(n[2],norm);
							
							CQASSERT(minVert < 3);
							
							if (dist[minVert] < BBOX_MAX__DIST*1.4 && (n_dot[0] > 0 && n_dot[1] > 0 && n_dot[2] > 0))
							{
								if (next_poly == POLYS_PER_HOLE)
									next_poly--;
									/*	shieldHitPolys.v[next_poly][minVert] = v[minVert];
									for (c=0;c<3;c++)
									{
									if (minVert != c)
									{
									if (dist[c] < BBOX_MAX__DIST)
									shieldHitPolys.v[next_poly][c] = v[c];
									else
									shieldHitPolys.v[next_poly][c] = v[minVert]+(v[c]-v[minVert])*(1-((dist[c]-BBOX_MAX__DIST)/(dist[c]-dist[minVert])));
									}
							}*/
								
								for (c=0;c<3;c++)
								{
									
									
									SINGLE angle = acos(dot_product(norm,n[c]));
									if (angle)
									{
										Vector rot = cross_product(norm,n[c]);
										Quaternion quat(rot,angle);
										Vector test = quat.transform(norm);
										i = quat.transform(base_i);
										j = quat.transform(base_j);
									}
									else
									{
										i = base_i;
										j = base_j;
									}
									
									shieldHitPolys[slot]->v[next_poly][c] = v[c];
									//	shieldHitPolys.t[next_poly][c].u = 0.5 + dot_product(v[c]-collide_point,i) / BBOX_MAX__DIST;
									//	shieldHitPolys.t[next_poly][c].v = 0.5 + dot_product(v[c]-collide_point,j) / BBOX_MAX__DIST;
									
									Vector offset = v[c]-collide_point;
									Vector offset_2d;
									SINGLE nor_mag;
									nor_mag = dot_product(offset,n[c]);
									offset_2d = offset-nor_mag*n[c];
									offset_2d.normalize();
									SINGLE scale = damage/12.0;
									shieldHitPolys[slot]->t[next_poly][c].u = 0.5 + scale*dist[c]*dot_product(i,offset_2d) / BBOX_MAX__DIST;
									shieldHitPolys[slot]->t[next_poly][c].v = 0.5 + scale*dist[c]*dot_product(j,offset_2d) / BBOX_MAX__DIST;
									
								}
								next_poly++;
							}
						}
						shieldHitPolys[slot]->poly_cnt = next_poly;
						shieldHitPolys[slot]->timer = 1.0;
					}
				}
			}
		}
	}
}

template <class Base>
void ObjectDamage< Base >::GenerateNormals(CollisionMesh *cmesh)
{
	vertex_normals = new Vector[cmesh->num_vertices];
	for (int c=0;c<cmesh->num_vertices;c++)
	{
		Vector norm;
		Triangle * t = cmesh->triangles;
		for (int f=0;f<cmesh->num_triangles;f++)
		{
			if (t[f].v[0] == c || t[f].v[1] == c || t[f].v[2] == c)
			{
				norm += cmesh->normals[t[f].normal];
			}
		}
		norm.normalize();
		vertex_normals[c] = norm;
	}
}
#endif

//---------------------------------------------------------------------------
//
/*template <class Base>
void ObjectDamage< Base >::DamageFace (S32 childID,S32 faceID)
{
	mc[childID].hiddenArray[faceID] = FS_DAMAGED;
//	MyFace *face = &mc[childID].myFaceArray[faceID];
//	FACE_PROPERTY *prop = &mc[childID].buildMesh->face_groups[face->groupID].face_properties[face->index];
//	*prop |= HIDDEN;
}*/
void Dummy()
{
};

#define DMG_TEXFACTOR 0.0012
#define BLAST_SIZE 500.0

//---------------------------------------------------------------------------
bool SphereTriangle  (Vector v[3],Vector pt,SINGLE rad)
{
	bool result;
	
	float r_squared = (rad*rad);
	
	float min = FLT_MAX;
//	int num_tri_cols = 0;
	
	// Sphere is in mesh frame. Compute distance from sphere center to each triangle in mesh.
	
	const Vector & b0 = v[0];
	const Vector & v1 = v[1];
	const Vector & v2 = v[2];
	
	Vector e0 = v1 - b0;
	Vector e1 = v2 - b0;
	
	Vector diff = b0 - pt;
	
	float a = dot_product(e0, e0);
	float b = dot_product(e0, e1);
	float c = dot_product(e1, e1);
	float d = dot_product(e0, diff);
	float e = dot_product(e1, diff);
//	float f = dot_product(diff, diff);

	// This determinant will never be zero for a non-degenerate triangle.
	float det = a * c - b * b;
	
	float s = (b * e - c * d) / det;
	float t = (b * d - a * e) / det;
	
	if (s+t <= 1.0f)
	{
		if (s < 0.0f)
		{
			if (t < 0.0f)  // region 4
			{
				if (d < 0)
				{
					t = 0.0f;
					s = -d / a;
					if (s > 1.0f) 
					{
						s = 1.0f;
					}
				}
				else if (e < 0)
				{
					s = 0.0f;
					t = - e / c;
					if (t > 1.0f) 
					{
						t = 1.0f;
					}
				}
				else
				{
					s = 0.0f;
					t = 0.0f;
				}
			}
			else  // region 3
			{
				s = 0.0f;
				t = - e / c;
				if (t < 0.0f)
				{
					t = 0.0f;
				}
				else if (t > 1.0f)
				{
					t = 1.0f;
				}
			}
		}
		else if (t < 0.0f)  // region 5
		{
			t = 0.0f;
			s = -d / a;
			if (s < 0.0f)
			{
				s = 0.0f;
			}
			else if (s > 1.0f)
			{
				s = 1.0f;
			}
		}
		else  // region 0
		{
			// minimum at interior point
		}
	}
	else
	{
		if (s < 0.0f)  // region 2
		{
			if (b - c + d - e < 0.0f)
			{
				s = -(b - c + d - e) / (a - 2 * b + c);
				if (s < 0.0f) 
				{
					s = 0.0f; 
				}
				else if (s > 1.0f)
				{
					s = 1.0f;
				}
				t = 1.0f - s;
			}
			else if (c + e > 0.0f)
			{
				s = 0.0f;
				t = - e / c;
				if (t < 0.0f)
				{
					t = 0.0f;
				}
				else if (t > 1.0f)
				{
					t = 1.0f;
				}
			}
			else
			{
				s = 0.0f;
				t = 1.0f;
			}
		}
		else if (t < 0.0f)  // region 6
		{
			if (a - b + d - e > 0.0f)
			{
				t = (a - b + d - e) / (a - 2 * b + c);
				if (t < 0.0f) 
				{
					t = 0.0f;
				}
				else if (t > 1.0f)
				{
					t = 1.0f;
				}
				s = 1.0f - t;
			}
			else if (a + d > 0.0f)
			{
				t = 0.0f;
				s = -d / a;
				if (s < 0.0f) 
				{
					s = 0.0f;
				}
				else if (s > 1.0f)
				{
					s = 1.0f;
				}
			}
			else
			{
				s = 1.0f;
				t = 0.0f;
			}
		}
		else  // region 1
		{
			s = -(b - c + d - e) / (a - 2 * b + c);
			if (s < 0.0f )
			{
				s = 0.0f; 
			}
			else if (s > 1.0f) 
			{
				s = 1.0f;
			}
			t = 1.0f - s;
		}
	}

	
	// Alternate computation of min distance.
	//float dist_squared = fabs(s * (a * s + b * t + 2 * d) + t * (b * s + c * t + 2 * e) + f);
	
	Vector closest = b0 + s * e0 + t * e1;
	Vector dc = closest - pt;
	float dc_squared = dot_product(dc, dc);
	/*float dd = fabs(dc_squared - min);

	if (dd <= 1e-3)
	{
		tricol[num_tri_cols].tri1 = tri;
		tricol[num_tri_cols].pt = closest;
		num_tri_cols++;
	}
	else if (dc_squared < min)
	{*/
		min = dc_squared;
	/*	tricol[0].tri1 = tri;
		tricol[0].pt = closest;
		num_tri_cols = 1;
	}*/

	result = (min <= r_squared);

	return result;
}

struct DamageRender : IRenderChannel
{
	SINGLE dmgTimer;
	U32 damageTexID;

	DamageRender();

	virtual void Render(SINGLE dt);

	virtual IRenderChannel *Clone();
};

DamageRender::DamageRender()
{
	dmgTimer = 0;
}

void DamageRender::Render(SINGLE dt)
{
	dmgTimer += dt;
	if (dmgTimer > 314.16f)
		dmgTimer -= 314.16f;

	BATCH->set_state(RPR_BATCH,TRUE);
	const Transform &trans = ENGINE->get_transform(ec->mi->instanceIndex);
	CAMERA->SetModelView(&trans);
	
	int val = 255*(0.7+0.3*cos(2*dmgTimer));
	
	val += 30;
	if (val > 510)
		val -= 510;
	S32 mag=val;
	if (mag > 255)
		mag = 510-mag;
	CQASSERT( mag >= 0 && mag <= 255);
	U32 color = 0xff000000 | (mag<<16) | (mag<<8) | mag;
	BATCH->set_state(RPR_DELAY,1);
	BATCH->set_render_state(D3DRS_DEPTHBIAS ,1);
	ec->RenderWithTexture(damageTexID,color,true); //true for clamp
	BATCH->set_render_state(D3DRS_DEPTHBIAS ,0);
	BATCH->set_state(RPR_DELAY,0);
}

IRenderChannel * DamageRender::Clone()
{
	DamageRender *new_dr = new DamageRender;
	new_dr->damageTexID = damageTexID;
	new_dr->dmgTimer = dmgTimer;

	return new_dr;
}
//---------------------------------------------------------------------------
//-------------------------End Damage.cpp------------------------------------
//---------------------------------------------------------------------------
