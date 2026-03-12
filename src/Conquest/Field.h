#ifndef FIELD_H
#define FIELD_H
//--------------------------------------------------------------------------//
//                                                                          //
//                                 Field.h                                  //
//                                                                          //
//                  COPYRIGHT (C) 1998 BY DIGITAL ANVIL, INC.               //
//                                                                          //
//--------------------------------------------------------------------------//
/*
    $Header: /Conquest/App/Src/Field.h 36    6/04/00 1:23p Tmauer $
*/			    
//------------------------------- #INCLUDES --------------------------------//
//--------------------------------------------------------------------------//
#ifndef TOBJECT_H
#include "TObject.h"
#endif
#include "FileSys.h"
#include "Camera.h"
#include "Mpart.h"
#include "ObjList.h"
#include "sysmap.h"
#include "Sector.h"

#ifndef _INC_MALLOC
#include <malloc.h>
#endif

#ifndef DFIELD_H
#include "DField.h"
#endif

#define FIELD_TILE_SIZE	GRIDSIZE
#define FTS FIELD_TILE_SIZE
#define HFTS (FTS/2)

/*enum FieldType
{
	FT_NEBULAE,
	FT_ASTEROID,
	FT_MINEFIELD
};*/
struct IField : IBaseObject
{
	char name[32];
	struct IFieldManager *factory;
	FIELDCLASS fieldType;
	U32 infoHelpID;
	IField * nextField;
	BOOL32 flag:1;
	struct FIELD_ATTRIBUTES attributes;

	//--- IBaseObject ---//

	virtual ~IField (void);	// find this in cloud.cpp

	virtual void TestVisible (const USER_DEFAULTS & defaults, const U32 currentSystem, const U32 currentPlayer)	// set bVisible if possible for any part of object to appear
	{
		bVisible = (GetSystemID() == currentSystem);
	}


	virtual SINGLE TestHighlight (const RECT & rect)	// set bHighlight if possible for any part of object to appear within rect
	{
		bHighlight = FALSE;
		return 0.0f;
	}

	//--- Field ---//

	virtual BOOL32 Setup () = 0;//struct XYCoord *_squares=0,U32 _numSquares=0) = 0;

	virtual BOOL32 Init (HANDLE hArchetype) = 0;

	virtual BOOL32 ObjInField (U32 systemID,const Vector &pos)
	{
		return FALSE;
	}

	virtual void SetSystemID (U32 _systemID) = 0;

	virtual void RenderBackground (void) = 0;

	virtual Vector GetCenterPos (void) = 0;

	virtual void SetFieldFlags (FIELDFLAGS &flags)
	{}

	virtual void PlaceBaseNuggets ()
	{};

	virtual void SetAmbientLight (const Vector &pos,const U8_RGB & old_amb)
	{}

	//	virtual BOOL32 Init (struct BASIC_DATA *data,struct FieldArchetype *fieldArch) = 0;//AnimArchetype *animArch) = 0;
	//virtual void GetRegions(RECT **zone,U32 *numZones) {};
};

template <class Type> 
struct FieldArchetype
{
	Type *pData;
//	const char *name;
	PARCHETYPE pArchetype;
	S32 archIndex;
	IMeshArchetype * meshArch;
	
	void * operator new (size_t size)
	{
		return calloc(size,1);
	}
	void operator delete (void * ptr)
	{
		::free(ptr);
	}

	FieldArchetype (void)
	{
		meshArch = NULL;
		archIndex = -1;
	}

	virtual ~FieldArchetype() {}

	virtual void Notify(U32 message, void *param) = 0;

	virtual void EndEdit() = 0;

	virtual void Edit() = 0;

	virtual void RenderEdit() = 0;

	virtual void SetUpPosition(S32 * arrX, S32 * arrY, U32 numPoints) = 0;

};

#define ANCHOR_OFF -999

template <class Type>
struct DefaultArchetype : FieldArchetype<Type>
{
	//edit stuff
	PARCHETYPE nuggetType[4];

	XYCoord squares[MAX_SQUARES];
	U32 numSquares;
	BOOL32 laidSquare:1;
	BOOL32 snapping:1;
	S32 mouseX,mouseY;
	S32 snapX, snapY, anchorX, anchorY;
	U32 textureID;

	DefaultArchetype (void) {
		anchorX = ANCHOR_OFF;

		const char *fname = "edit.tga";
		textureID = TMANAGER->CreateTextureFromFile(fname, TEXTURESDIR, DA::TGA, PF_4CC_DAA4);
	}

	~DefaultArchetype (void) {
		TMANAGER->ReleaseTextureRef(textureID);
		textureID = 0;
	}

	virtual void Notify(U32 message, void *param) {
		MSG *msg = (MSG *) param;

		switch (message)
		{
			case WM_LBUTTONDOWN:
				OnLButtonDown();
				break;
			case WM_LBUTTONUP:
				OnLButtonUp();
				break;
			case WM_MOUSEMOVE:
				mouseX = LOWORD(msg->lParam);
				mouseY = HIWORD(msg->lParam);
				break;
		}
	}

	virtual void OnLButtonUp() {
		Vector vec,vec2;
		S32 intx,inty;

		if (!laidSquare)
		{
			laidSquare = TRUE;
			numSquares = 0;
		}

		if (numSquares != MAX_SQUARES)
		{
			vec.x = anchorX;
			vec.y = anchorY;
			CAMERA->ScreenToPoint(vec.x, vec.y, 0);

			S32 intx2,inty2;
			vec2.x = mouseX;
			vec2.y = mouseY;
			CAMERA->ScreenToPoint(vec2.x, vec2.y, 0);

			vec.x = floor(vec.x/FTS)*FTS+FTS*0.5;// + snapX;
			vec.y = floor(vec.y/FTS)*FTS+FTS*0.5;// + snapY;

			vec2.x = floor(vec2.x/FTS)*FTS+FTS*0.5;// + snapX;
			vec2.y = floor(vec2.y/FTS)*FTS+FTS*0.5;// + snapY;

			intx = (S32)vec.x;
			inty = (S32)vec.y;
			intx2 = (S32)vec2.x;
			inty2 = (S32)vec2.y;

			S32 incX=1,incY=1;

			if (intx2-intx != 0)
			{
				incX = (intx2-intx)/abs(intx2-intx);
			}
			if (inty2-inty != 0)
			{
				incY = (inty2-inty)/abs(inty2-inty);
			}

			for (int cx=intx;incX*cx<=incX*intx2;cx+=incX*FTS)
			{
				for (int cy=inty;incY*cy<=incY*inty2;cy+=incY*FTS)
				{
					if (numSquares != MAX_SQUARES)
					{
						bool bFound = 0;
						for (unsigned int n=0;n<numSquares;n++)
						{
							if (squares[n].x == cx && squares[n].y == cy)
								bFound = 1;
						}

						if (!bFound)
						{
							squares[numSquares].x = cx;
							squares[numSquares].y = cy;

							numSquares++;
						}
					}
				}
			}
		}
		anchorX = ANCHOR_OFF;
	}

	virtual void OnLButtonDown() {
		//	S32 intx,inty;
		Vector vec;
		anchorX = mouseX;
		anchorY = mouseY;
		if (!snapping)
		{
			//		vec.x = anchorX;
			//		vec.y = anchorY;
			//		CAMERA->ScreenToPoint(vec.x, vec.y, 0);
			//	intx = (S32)vec.x;
			//	inty = (S32)vec.y;
			//	snapX = intx % HFTS;
			//	snapY = inty % HFTS;
			snapX = snapY = 0;
			snapping = TRUE;
		}
	}

	void EndEdit() override {
		if (laidSquare == 0)
			return;

		laidSquare = FALSE;

		IField *obj = (IField *) ARCHLIST->CreateInstance(this->pArchetype);
		if (obj==0)
			return;

		MPartNC part = obj;
		part->dwMissionID = MGlobals::CreateNewPartID(0);
		_ltoa(part->dwMissionID,part->partName,16);
		OBJLIST->AddPartID(obj,obj->GetPartID());

		obj->Setup();//squares,numSquares);

		OBJLIST->AddObject(obj);

		obj->PlaceBaseNuggets();
		SYSMAP->InvalidateMap(SECTOR->GetCurrentSystem());
	}

	virtual void Edit() {
		snapping = FALSE;
		numSquares = 0;
	}

	virtual void RenderEdit() {
		Vector vec,vec2;
	U32 i;
	S32 intx,inty,intx2,inty2;

	vec.x = mouseX;
	vec.y = mouseY;
	BATCH->set_state(RPR_BATCH,FALSE);
	DisableTextures();

	CAMERA->SetPerspective();
	CAMERA->SetModelView();
	BATCH->set_render_state(D3DRS_ZENABLE,0);


	if (CAMERA->ScreenToPoint(vec.x, vec.y, 0) != 0)
	{
		vec2 = vec;
		if (1)//snapping)
		{
			vec.x = floor(vec.x/FTS)*FTS+FTS*0.5;// + snapX;
			vec.y = floor(vec.y/FTS)*FTS+FTS*0.5;// + snapY;
			vec2 = vec;
			if (anchorX != ANCHOR_OFF)
			{
				//Always "snapping" by this point
				vec2.x = anchorX;
				vec2.y = anchorY;
				CAMERA->ScreenToPoint(vec2.x, vec2.y, 0);
				vec2.x = floor(vec2.x/FTS)*FTS+FTS*0.5;// + snapX;
				vec2.y = floor(vec2.y/FTS)*FTS+FTS*0.5;// + snapY;
			}
		}



		intx = (S32)vec.x;
		inty = (S32)vec.y;
		intx2 = (S32)vec2.x;
		inty2 = (S32)vec2.y;
		S32 incX=1,incY=1;

		if (intx2-intx != 0)
		{
			incX = (intx-intx2)/abs(intx2-intx);
		}
		if (inty2-inty != 0)
		{
			incY = (inty-inty2)/abs(inty2-inty);
		}

		PB.Color3ub(255,255,255);

		PB.Begin(PB_LINES);
		for (int cx=intx2;incX*cx<=incX*intx;cx+=incX*2*HFTS)
		{
			for (int cy=inty2;incY*cy<=incY*inty;cy+=incY*2*HFTS)
			{


				PB.Vertex3f(cx-HFTS,cy-HFTS,0);
				PB.Vertex3f(cx+HFTS,cy-HFTS,0);

				PB.Vertex3f(cx-HFTS,cy-HFTS,0);
				PB.Vertex3f(cx-HFTS,cy+HFTS,0);

				PB.Vertex3f(cx-HFTS,cy+HFTS,0);
				PB.Vertex3f(cx+HFTS,cy+HFTS,0);

				PB.Vertex3f(cx+HFTS,cy-HFTS,0);
				PB.Vertex3f(cx+HFTS,cy+HFTS,0);


			}
		}

		PB.End(); //PB_LINES
	}

	//Render happy faces
	//		BATCH->set_texture_stage_texture( 0,textureID);
		SetupDiffuseBlend(textureID,TRUE);
		BATCH->set_render_state(D3DRS_CULLMODE, D3DCULL_NONE);
		BATCH->set_render_state(D3DRS_ZWRITEENABLE,FALSE);
		PB.Begin(PB_QUADS);
		for (i = 0; i < numSquares; i++) {
			XYCoord *sq = &squares[i];


			SINGLE RVAL, GVAL, BVAL;

			RVAL = 1.0;
			GVAL = 1.0;
			BVAL = 1.0;

			Vector v[3], n[3];

			PB.TexCoord2f(0, 0);
			PB.Vertex3f(sq->x - HFTS, sq->y + HFTS, 0);
			PB.TexCoord2f(1, 0);
			PB.Vertex3f(sq->x + HFTS, sq->y + HFTS, 0);
			PB.TexCoord2f(1, 1);
			PB.Vertex3f(sq->x + HFTS, sq->y - HFTS, 0);
			PB.TexCoord2f(0, 1);
			PB.Vertex3f(sq->x - HFTS, sq->y - HFTS, 0);
		}

		PB.End(); // PB_TRIANGLES

		BATCH->set_render_state(D3DRS_ZWRITEENABLE,TRUE);
	}

	virtual void SetUpPosition(S32 * arrX, S32 * arrY, U32 numPoints) {
		CQASSERT(numPoints < MAX_SQUARES);
		numSquares = numPoints;
		for(U32 i = 0; i <numPoints; ++i)
		{
			squares[i].x = arrX[i];
			squares[i].y = arrY[i];
		}
	}
};



//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

struct DACOM_NO_VTABLE IFieldManager: IDAComponent
{
	virtual void DeleteField (struct IField *field) = 0;

	virtual IField *GetFields () = 0;

	virtual IField *GetFieldContaining (IBaseObject *obj);

	virtual void RenderBackground () = 0;

//	virtual void SetShipBits () = 0;

	virtual void FieldHighlighted (struct IField * field);

	virtual void CreateField(char * fieldArchtype,S32 * xPos,S32 * yPos, U32 numberInList, U32 systemID) = 0;

	//obj is the pointer to the ship that is in the field
	virtual void SetAttributes(IBaseObject *obj,U32 dwFieldMissionID) = 0;

	virtual void AddFieldToList(IField * field) = 0;

	virtual void RemoveFieldFromList(IField * field) = 0;

	virtual void MapRenderFields(U32 systemID) = 0;

	virtual void CreateFieldBlast(IBaseObject * target,Vector pos,U32 systemID) = 0;
};
//---------------------------------------------------------------------------
//-------------------------END Field.h---------------------------------------
//---------------------------------------------------------------------------
#endif