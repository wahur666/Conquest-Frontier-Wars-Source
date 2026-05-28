//--------------------------------------------------------------------------//
//                                                                          //
//                              DrawAgent.cpp                               //
//                                                                          //
//               COPYRIGHT (C) 1997 BY DIGITAL ANVIL, INC.                  //
//                                                                          //
//--------------------------------------------------------------------------//
/*
   $Author: Jasony $

   $Header: /Conquest/App/Src/DrawAgent.cpp 65    9/29/00 12:21a Jasony $

   A DrawAgent can draw a 2D image to the screen using the "best" implementation,
   given the circumstances. 
		1) Lock the frame buffer, and use VFX_shape_draw(),
		2) Load image as a texture, draw a quad to screen using GL
		3) Draw pixels using drawPixels().
*/
//--------------------------------------------------------------------------//
 
#include "pch.h"
#include <globals.h>

#include "DrawAgent.h"
#include "IImageReader.h"
#include "CQTrace.h"
#include "VideoSurface.h"
#include "CQBatch.h"
#include "TManager.h"
#include "Camera.h"
#include "UserDefaults.h"

#include <TComponent2.h>
#include <TSmartPointer.h>
#include <FileSys.h>
#include <RendPipeline.h>
//#include <RPUL\PrimitiveBuilder.h>
#include "VFX_shapes.hpp"
#include <malloc.h>
#include <span>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "da_heap_utility.h"
#include "IRenderPrimitive.h"
#include "MyVertex.h"
#include "vtdump.h"

#define AWKWARD_HEIGHT  40

//--------------------------------------------------------------------------//
//--------------------------------------------------------------------------//

struct VFX_SHAPETABLE_EX : VFX_SHAPETABLE
{
//   U32 version;
//   U32 shape_count;

   U32 shape_offset;
   U32 palette_offset;
   
   //
   // rest of table...
   //
};

//--------------------------------------------------------------------------//

struct BLOCKRECT
{
    SINGLE    left;
    SINGLE    top;
    SINGLE    right;
    SINGLE    bottom;

	float twidth, theight;
	float toffsetx, toffsety;

	operator RECT (void)
	{
		RECT rect;
		rect.left = left;
		rect.right = right;
		rect.bottom = bottom;
		rect.top = top;
		return rect;
	}
};

bool bFastDraw=false;
void EnableFastDrawAgent (bool bEnable)
{
	bFastDraw = 0;//bEnable;
}

static bool DrawAgentShouldDump(int& count, const int maxCount = 64)
{
	count++;
	return false;
}

static bool DrawAgentIsLarge(U16 width, U16 height)
{
	return width >= 700 || height >= 500;
}

static bool DrawAgentShouldDumpImage(int& count, U16 width, U16 height, const int maxCount = 64)
{
	DrawAgentShouldDump(count, maxCount);
	DrawAgentIsLarge(width, height);
	return false;
}

static nlohmann::json DrawAgentRectJson(const BLOCKRECT& rect)
{
	return {
		{"left", rect.left},
		{"top", rect.top},
		{"right", rect.right},
		{"bottom", rect.bottom},
		{"twidth", rect.twidth},
		{"theight", rect.theight},
		{"toffsetx", rect.toffsetx},
		{"toffsety", rect.toffsety}
	};
}

struct JsonShapeReplacement
{
	const char* shapeFilename;
	const char* jsonFilename;
};

static const JsonShapeReplacement kJsonShapeReplacements[] = {
	{"mainscreen.shp", "mainscreen_frames.json"},
	{"animSingle.shp", "animSingle_frames.json"}
};

static const char* ShapeBasename(const char* filename)
{
	if (!filename)
	{
		return "";
	}

	const char* basename = filename;
	for (const char* scan = filename; *scan; ++scan)
	{
		if (*scan == '\\' || *scan == '/')
		{
			basename = scan + 1;
		}
	}

	return basename;
}

static const JsonShapeReplacement* FindJsonShapeReplacement(const char* filename)
{
	const char* basename = ShapeBasename(filename);
	for (const JsonShapeReplacement& replacement : kJsonShapeReplacements)
	{
		if (_stricmp(basename, replacement.shapeFilename) == 0)
		{
			return &replacement;
		}
	}
	return nullptr;
}

static bool DecodeBase64(const std::string& input, std::vector<U8>& output)
{
	static const signed char decodeTable[256] = {
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
		52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1,
		-1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,
		15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
		-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
		41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
	};

	output.clear();
	int value = 0;
	int bits = -8;
	for (unsigned char c : input)
	{
		if (std::isspace(c))
		{
			continue;
		}
		const signed char decoded = decodeTable[c];
		if (decoded == -2)
		{
			break;
		}
		if (decoded < 0)
		{
			return false;
		}
		value = (value << 6) + decoded;
		bits += 6;
		if (bits >= 0)
		{
			output.push_back((U8)((value >> bits) & 0xff));
			bits -= 8;
		}
	}
	return !output.empty();
}

static bool LoadFramePngFromJson(const char* jsonPath, U32 frameIndex, std::vector<U8>* pngBytes, U32* outWidth, U32* outHeight)
{
	if (outWidth)
	{
		*outWidth = 0;
	}
	if (outHeight)
	{
		*outHeight = 0;
	}
	if (pngBytes)
	{
		pngBytes->clear();
	}
	if (!jsonPath || !jsonPath[0])
	{
		return false;
	}

	std::ifstream in(jsonPath, std::ios::binary);
	if (!in)
	{
		return false;
	}

	std::string jsonText;
	jsonText.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	if (jsonText.empty())
	{
		return false;
	}

	nlohmann::json root = nlohmann::json::parse(jsonText, nullptr, false);
	if (root.is_discarded())
	{
		return false;
	}

	char frameName[32];
	sprintf(frameName, "frame_%04u", frameIndex);

	const nlohmann::json* frameNode = nullptr;
	const auto framesIt = root.find("frames");
	if (framesIt != root.end() && framesIt->is_object())
	{
		const auto frameIt = framesIt->find(frameName);
		if (frameIt != framesIt->end() && frameIt->is_object())
		{
			frameNode = &(*frameIt);
		}
	}
	if (!frameNode)
	{
		return false;
	}

	const auto widthIt = frameNode->find("width");
	const auto heightIt = frameNode->find("height");
	if (widthIt == frameNode->end() || heightIt == frameNode->end() || !widthIt->is_number() || !heightIt->is_number())
	{
		return false;
	}
	const U32 width = widthIt->get<U32>();
	const U32 height = heightIt->get<U32>();
	if (width == 0 || height == 0)
	{
		return false;
	}

	if (outWidth)
	{
		*outWidth = width;
	}
	if (outHeight)
	{
		*outHeight = height;
	}

	if (!pngBytes)
	{
		return true;
	}

	const auto dataIt = frameNode->find("data");
	if (dataIt == frameNode->end() || !dataIt->is_string())
	{
		return false;
	}

	std::string data = dataIt->get<std::string>();
	const std::string prefix = "data:image/png;base64,";
	if (data.compare(0, prefix.size(), prefix) == 0)
	{
		data.erase(0, prefix.size());
	}
	return DecodeBase64(data, *pngBytes);
}

static bool FindFrameJson(const char* jsonFilename, std::string& result)
{
	if (!jsonFilename || !jsonFilename[0])
	{
		return false;
	}

	const std::string localPath = jsonFilename;
	const std::string debugBinPath = std::string("cmake-build-debug/bin/") + jsonFilename;
	const char* paths[] = {
		localPath.c_str(),
		debugBinPath.c_str()
	};

	for (const char* path : paths)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
		{
			continue;
		}
		result = path;
		return true;
	}
	return false;
}

//--------------------------------------------------------------------------//
//
struct DACOM_NO_VTABLE DrawAgent : IDrawAgent
{
	static IDAComponent* GetIDrawAgent(void* self) {
	    return static_cast<IDrawAgent*>(
	        static_cast<DrawAgent*>(self));
	}

	static std::span<const DACOMInterfaceEntry2> GetInterfaceMap() {
	    static const DACOMInterfaceEntry2 map[] = {
	        {"IDrawAgent", &GetIDrawAgent},
	    };
	    return map;
	}

	//------------------------------
	// 2D shape data
	//------------------------------
	VFX_SHAPETABLE_EX * vfxShape;
	//------------------------------
	// texture data
	//------------------------------
	LONG_PTR			*textureID;
	U32			*hintID;
	BLOCKRECT	*blockRect;
	S16			numTextures;
	//------------------------------
	// general data
	//------------------------------
	U16			imageWidth, imageHeight;
	char		jsonReplacementPath[260];
	U32			jsonReplacementFrame;
	LONG_PTR	jsonReplacementTexture;
	U32			jsonReplacementTextureWidth;
	U32			jsonReplacementTextureHeight;
	bool		jsonReplacementTextureAttempted;

	bool bHiRes;//hack for high resolution images

	bool b3DEnabled:1;		// 3D mode when initialized, can only draw in this mode
	bool bJsonReplacement:1;
#if 0
	bool bDebugMsg:1;			// true when we have given a debug msg
#endif

	DrawAgent (void);

	~DrawAgent (void);

    void * operator new (size_t size)
	{
		return calloc(size, 1);
	}

	void   operator delete (void *ptr)
	{
		::free(ptr);
	}

	//------------------------------
	//------------------------------


	/* IDrawAgent methods */

	DEFMETHOD_(void,Draw) (PANE *pane, S32 x, S32 y);

	DEFMETHOD_(void,Draw) (PANE *src, PANE *dst);
	
	DEFMETHOD_(void,GetDimensions) (U16 & width, U16 & height) const;

	/* DrawAgent methods */

	BOOL32 init (struct IImageReader * reader, BOOL32 bScaleData, BOOL32 bHiRes, const RECT * pRect);

	BOOL32 constructShape (struct IImageReader * reader, COLORREF palette[256], const RECT * pRect);

	void loadTexture (U32 block, const BLOCKRECT & rect, struct IImageReader * reader, BOOL32 bTransparentMode, RGB rgbData[256], BOOL32 bScaleData, BOOL32 bHiRes, const RECT * pRect);

	BOOL32 initJsonReplacement(const char* sourceFilename, const char* jsonPath, U32 frameIndex, BOOL32 bHiRes);

	BOOL32 drawJsonReplacement(PANE *pane, S32 x, S32 y);

	static U32 __fastcall nearestPower (S32 number);

	static inline void makeSquare (U32 & width, U32 & height)
	{
		while (width < height)
			width *= 2;
		while (height < width)
			height *= 2;
	}

	static void extractAlphaMap (const COLORREF palette[256], U8 alphaMap[256])
	{
		int i;

		for (i = 0; i < 256; i++)
			alphaMap[i] = palette[i] >> 24;
	}
};
//--------------------------------------------------------------------------//
//
DrawAgent::DrawAgent (void)
{
}
//--------------------------------------------------------------------------//
//
DrawAgent::~DrawAgent (void)
{
	if (bJsonReplacement && jsonReplacementTexture && PIPE)
	{
		PIPE->destroy_texture(jsonReplacementTexture);
		jsonReplacementTexture = 0;
	}

	ITManager * const TMANAGER = ::TMANAGER;
	if (TMANAGER)
	{
		for (S32 i=0;i<numTextures;i++)
			TMANAGER->ReleaseDrawAgentTexture(textureID[i]);
	}
	free(vfxShape);

	delete [] textureID;
	delete [] hintID;
	delete [] blockRect;
}
//--------------------------------------------------------------------------//
//
BOOL32 DrawAgent::initJsonReplacement(const char* sourceFilename, const char* jsonPath, U32 frameIndex, BOOL32 _bHiRes)
{
	U32 width = 0;
	U32 height = 0;
	if (!LoadFramePngFromJson(jsonPath, frameIndex, nullptr, &width, &height))
	{
		return 0;
	}

	bJsonReplacement = true;
	bHiRes = _bHiRes != 0;
	b3DEnabled = (CQFLAGS.b3DEnabled != 0);
	imageWidth = (U16)width;
	imageHeight = (U16)height;
	jsonReplacementFrame = frameIndex;
	jsonReplacementTexture = 0;
	jsonReplacementTextureWidth = 0;
	jsonReplacementTextureHeight = 0;
	jsonReplacementTextureAttempted = false;
	strncpy(jsonReplacementPath, jsonPath, sizeof(jsonReplacementPath) - 1);
	jsonReplacementPath[sizeof(jsonReplacementPath) - 1] = 0;
	numTextures = 0;

	nlohmann::json dump = {
		{"event", "DrawAgent::jsonReplacementInit"},
		{"this", reinterpret_cast<std::uintptr_t>(this)},
		{"source", sourceFilename ? sourceFilename : ""},
		{"jsonPath", jsonReplacementPath},
		{"frame", jsonReplacementFrame},
		{"width", imageWidth},
		{"height", imageHeight}
	};
	VTDUMP_LOG(dump);

	return 1;
}
//--------------------------------------------------------------------------
//
BOOL32 DrawAgent::drawJsonReplacement(PANE *pane, S32 x, S32 y)
{
	if (!bJsonReplacement)
	{
		return 0;
	}

	if (!jsonReplacementTexture && !jsonReplacementTextureAttempted)
	{
		jsonReplacementTextureAttempted = true;
		std::vector<U8> pngBytes;
		U32 width = 0;
		U32 height = 0;
		if (LoadFramePngFromJson(jsonReplacementPath, jsonReplacementFrame, &pngBytes, &width, &height))
		{
			if (PIPE->create_texture_from_file_in_memory(pngBytes.data(), (U32)pngBytes.size(), jsonReplacementTexture, &jsonReplacementTextureWidth, &jsonReplacementTextureHeight) != GR_OK)
			{
				jsonReplacementTexture = 0;
				jsonReplacementTextureWidth = 0;
				jsonReplacementTextureHeight = 0;
			}
		}

		nlohmann::json dump = {
			{"event", "DrawAgent::jsonReplacementTexture"},
			{"this", reinterpret_cast<std::uintptr_t>(this)},
			{"jsonPath", jsonReplacementPath},
			{"frame", jsonReplacementFrame},
			{"loaded", jsonReplacementTexture != 0},
			{"texture", static_cast<long long>(jsonReplacementTexture)},
			{"textureWidth", jsonReplacementTextureWidth},
			{"textureHeight", jsonReplacementTextureHeight},
			{"imageWidth", imageWidth},
			{"imageHeight", imageHeight}
		};
		VTDUMP_LOG(dump);
	}

	if (!jsonReplacementTexture)
	{
		return 0;
	}

	OrthoView(pane);
	BATCH->set_render_state(D3DRS_ZENABLE,FALSE);
	BATCH->set_render_state(D3DRS_ZWRITEENABLE,FALSE);
	BATCH->set_render_state(D3DRS_ALPHABLENDENABLE,FALSE);
	BATCH->set_render_state(D3DRS_CULLMODE,D3DCULL_NONE);
	SetupDiffuseBlend(jsonReplacementTexture, FALSE);
	BATCH->set_state(RPR_STATE_ID, jsonReplacementTexture);

	if (pane)
	{
		x += pane->x0;
		y += pane->y0;
	}

	BLOCKRECT rect {};
	rect.left = 0;
	rect.top = 0;
	rect.right = IDEAL2REALX(imageWidth);
	rect.bottom = IDEAL2REALY(imageHeight);

	rect.left += x;
	rect.right += x;
	rect.top += y;
	rect.bottom += y;

	PB.Color4ub(255,255,255,255);
	PB.Begin(PB_QUADS);
	PB.TexCoord2f(0.0f, 0.0f);
	PB.Vertex3f(rect.left, rect.top, 0);
	PB.TexCoord2f(1.0f, 0.0f);
	PB.Vertex3f(rect.right, rect.top, 0);
	PB.TexCoord2f(1.0f, 1.0f);
	PB.Vertex3f(rect.right, rect.bottom, 0);
	PB.TexCoord2f(0.0f, 1.0f);
	PB.Vertex3f(rect.left, rect.bottom, 0);
	PB.End();

	BATCH->set_state(RPR_STATE_ID,0);
	return 1;
}
//--------------------------------------------------------------------------
//
void DrawAgent::Draw (PANE *pane, S32 x, S32 y)
{
	if (b3DEnabled != (CQFLAGS.b3DEnabled!=0))
	{
#if 0
		if (bDebugMsg==0)
			CQTRACE10("DrawAgent created in wrong context.");
		bDebugMsg = 1;
#endif
		return;
	}

	if (b3DEnabled==false && CQFLAGS.bFrameLockEnabled==false)
		return;		// cannot draw, frame lock failed

	if (bJsonReplacement)
	{
		drawJsonReplacement(pane, x, y);
		BATCH->set_state(RPR_STATE_ID,0);
		return;
	}

	// guard against the awkward system map issue
	if (pane && pane == CAMERA->GetPane())
	{
		if (y > pane->y1 - IDEAL2REALY(AWKWARD_HEIGHT))
		{
			if (CQFLAGS.bNoToolbar==0)
				return;
		}
	}

	// if locking the frame buffer is supported
	if (CQFLAGS.bFrameLockEnabled)
	{
		if (pane == 0)
			pane = const_cast<PANE *>(&SURFACE->GetPane());
		else
		if (pane->window == 0)
			pane->window = const_cast<VFX_WINDOW *>(&SURFACE->GetWindow());

		VFX_shape_draw(pane, vfxShape, 0, x, y);
	}
	else	// use 3D rendering system
	{
		int i;
		BLOCKRECT rect;
		static int drawDumpCount = 0;

		OrthoView(pane);

		if (bFastDraw)
		{
			BATCH->set_texture_stage_texture(0,textureID[0]);
		}
		else
		{
			BATCH->set_render_state(D3DRS_ZENABLE,FALSE);
			BATCH->set_render_state(D3DRS_ALPHABLENDENABLE,TRUE);
			BATCH->set_render_state(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
			BATCH->set_render_state(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
			BATCH->set_render_state(D3DRS_CULLMODE,D3DCULL_NONE);
		
			SetupDiffuseBlend(textureID[0],TRUE);
		}


		if (pane)
		{
			x += pane->x0;
			y += pane->y0;
		}

		PB.Color3ub(255,255,255);
		LONG_PTR currentTexID = textureID[0];

		if (DrawAgentShouldDumpImage(drawDumpCount, imageWidth, imageHeight))
		{
			nlohmann::json dump = {
				{"event", "DrawAgent::DrawXY"},
				{"this", reinterpret_cast<std::uintptr_t>(this)},
				{"x", x},
				{"y", y},
				{"imageWidth", imageWidth},
				{"imageHeight", imageHeight},
				{"numTextures", numTextures},
				{"firstTexture", static_cast<long long>(textureID ? textureID[0] : 0)},
				{"firstBlock", textureID && blockRect ? DrawAgentRectJson(blockRect[0]) : nlohmann::json()}
			};
			if (pane)
			{
				dump["pane"] = {{"x0", pane->x0}, {"y0", pane->y0}, {"x1", pane->x1}, {"y1", pane->y1}};
			}
			VTDUMP_LOG(dump);
		}

		BATCH->set_state(RPR_STATE_ID,textureID[0]);
	/*	if (CQBATCH)
		{
			CQBATCH->SetMStateID(textureID[0],hintID[0]);
		}*/
		PB.Begin(PB_QUADS);

		for (i = 0; i < numTextures; i++)
		{
			//optimize shared textures which we don't have yet, but, oh yes, we shall
			if (textureID[i] != currentTexID)
			{
				PB.End();
				BATCH->set_texture_stage_texture(0,textureID[i]);
				BATCH->set_state(RPR_STATE_ID,textureID[i]);
				/*if (CQBATCH)
				{
					CQBATCH->SetMStateID(textureID[i],hintID[i]);
				}*/
				PB.Begin(PB_QUADS);
			}
			
			rect = blockRect[i];
			rect.left += x;
			rect.right += x;// + 1;	
			rect.top += y;
			rect.bottom += y;// + 1;
			

	  		PB.TexCoord2f(rect.toffsetx,rect.toffsety);
			PB.Vertex3f(rect.left,rect.top,0);

	  		PB.TexCoord2f(rect.twidth,rect.toffsety);
			PB.Vertex3f(rect.right,rect.top,0);

			PB.TexCoord2f(rect.twidth,rect.theight);
			PB.Vertex3f(rect.right,rect.bottom,0);

			PB.TexCoord2f(rect.toffsetx, rect.theight);
			PB.Vertex3f(rect.left,rect.bottom,0);

		}
		PB.End();
	}
	BATCH->set_state(RPR_STATE_ID,0);
}
//--------------------------------------------------------------------------//
//
void DrawAgent::Draw (PANE *src, PANE *dst)
{
	if (b3DEnabled != (CQFLAGS.b3DEnabled!=0))
	{
		return;
	}

	if (b3DEnabled==false && CQFLAGS.bFrameLockEnabled==false)
		return;		// cannot draw, frame lock failed

	if (bJsonReplacement)
	{
		drawJsonReplacement(src, dst ? dst->x0 : 0, dst ? dst->y0 : 0);
		BATCH->set_state(RPR_STATE_ID,0);
		return;
	}

	// guard against the awkward system map issue
	if (src && src == CAMERA->GetPane())
	{
		if (src->y0 > src->y1 - IDEAL2REALY(AWKWARD_HEIGHT))
		{
			if (CQFLAGS.bNoToolbar==0)
				return;
		}
	}

	// if locking the frame buffer is supported
	if (CQFLAGS.bFrameLockEnabled)
	{
		if (src == 0)
			src = const_cast<PANE *>(&SURFACE->GetPane());
		else
		if (src->window == 0)
			src->window = const_cast<VFX_WINDOW *>(&SURFACE->GetWindow());

		VFX_shape_draw(src, vfxShape, 0, dst->x0, dst->y0);
	}
	else	// use 3D rendering system
	{
		int i;
		BLOCKRECT rect;
		static int drawPaneDumpCount = 0;

		Transform trans;
		trans.translation.x = -0.5f;
		trans.translation.y = -0.5f;
		BATCH->set_modelview(trans);

		OrthoView(src);

		if (dst != 0)
		{
			BATCH->set_ortho(0,dst->x1,dst->y1,0,-1,+1);
			BATCH->set_viewport(0,0,src->x1,src->y1);
		}

		BATCH->set_render_state( D3DRS_CULLMODE, D3DCULL_NONE);

		if (bFastDraw)
		{
			BATCH->set_texture_stage_texture(0,textureID[0]);
		}
		else
		{
			BATCH->set_render_state(D3DRS_ZENABLE,FALSE);
			BATCH->set_render_state(D3DRS_ALPHABLENDENABLE,TRUE);
			BATCH->set_render_state(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
			BATCH->set_render_state(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
			BATCH->set_render_state(D3DRS_CULLMODE,D3DCULL_NONE);
		
			
			SetupDiffuseBlend(textureID[0],TRUE);
		}


		if (src)
		{
			dst->x0 += src->x0;
			dst->y0 += src->y0;
		}

		PB.Color3ub(255,255,255);
		LONG_PTR currentTexID = textureID[0];

		if (DrawAgentShouldDumpImage(drawPaneDumpCount, imageWidth, imageHeight))
		{
			nlohmann::json dump = {
				{"event", "DrawAgent::DrawPane"},
				{"this", reinterpret_cast<std::uintptr_t>(this)},
				{"imageWidth", imageWidth},
				{"imageHeight", imageHeight},
				{"numTextures", numTextures},
				{"firstTexture", static_cast<long long>(textureID ? textureID[0] : 0)},
				{"firstBlock", textureID && blockRect ? DrawAgentRectJson(blockRect[0]) : nlohmann::json()}
			};
			if (src)
			{
				dump["src"] = {{"x0", src->x0}, {"y0", src->y0}, {"x1", src->x1}, {"y1", src->y1}};
			}
			if (dst)
			{
				dump["dst"] = {{"x0", dst->x0}, {"y0", dst->y0}, {"x1", dst->x1}, {"y1", dst->y1}};
			}
			VTDUMP_LOG(dump);
		}

		BATCH->set_state(RPR_STATE_ID,textureID[0]);
		PB.Begin(PB_QUADS);

		for (i = 0; i < numTextures; i++)
		{
			//optimize shared textures which we don't have yet, but, oh yes, we shall
			if (textureID[i] != currentTexID)
			{
				PB.End();
				BATCH->set_texture_stage_texture(0,textureID[i]);
				BATCH->set_state(RPR_STATE_ID,textureID[i]);
				PB.Begin(PB_QUADS);
			}
			
			rect = blockRect[i];
			rect.left += dst->x0;
			rect.right += dst->x0;
			rect.top += dst->y0;
			rect.bottom += dst->y0;

	  		PB.TexCoord2f(rect.toffsetx,rect.toffsety);		PB.Vertex3f(rect.left,rect.top,0);
	  		PB.TexCoord2f(rect.twidth,rect.toffsety);		PB.Vertex3f(rect.right,rect.top,0);
	  		PB.TexCoord2f(rect.twidth,rect.theight);		PB.Vertex3f(rect.right,rect.bottom,0);
	  		PB.TexCoord2f(rect.toffsetx, rect.theight);		PB.Vertex3f(rect.left,rect.bottom,0);

		}
		PB.End();
	}
	BATCH->set_state(RPR_STATE_ID,0);
}
//--------------------------------------------------------------------------//
//
void DrawAgent::GetDimensions (U16 & width, U16 & height) const
{
	if(bHiRes)
	{
		width = IDEAL2REALX(HRIDEAL2IDEALX(imageWidth));
		height = IDEAL2REALY(HRIDEAL2IDEALY(imageHeight));
	}
	else
	{
		width = IDEAL2REALX(imageWidth);
		height = IDEAL2REALY(imageHeight);
	}
}
//--------------------------------------------------------------------------//
//
void DrawAgent::loadTexture (U32 block, const BLOCKRECT & rect, struct IImageReader * reader, BOOL32 bTransparentMode, RGB rgbData[256], BOOL32 bScaleData, BOOL32 bHiRes, const RECT * pRect)
{
	U32 width, height;
	S32 over;
	PixelFormat desiredFormat(16, 5, 6-bTransparentMode, 5, bTransparentMode);		// GL_RGB5_A1

	CQASSERT(U32(bTransparentMode) <= 1);
	width  = rect.right - rect.left + 1;
	height = rect.bottom - rect.top + 1;

	blockRect[block] = rect;

#if 0
	if (PIPE->create_texture(width, height, desiredFormat, 0, textureID[block]) != GR_OK)
#endif
	{
		makeSquare(width, height);
		textureID[block] = TMANAGER->CreateDrawAgentTexture(width, (bTransparentMode!=0));
		blockRect[block].right  = width + rect.left - 1;
		blockRect[block].bottom = height + rect.top - 1;
		// actual image height over texture height
		if (imageHeight-rect.top <= height)
			blockRect[block].theight = (float)(imageHeight-rect.top) / (float)height;         //(float)nearestPower(imageHeight - rect.top);
		if (imageWidth-rect.left <= width)
			blockRect[block].twidth  = (float)(imageWidth-rect.left) / (float)width;
	}

	//PIPE->set_texture_render_state(textureID[block], D3DRS_TEXTUREADDRESSU, D3DTADDRESS_CLAMP);
	//PIPE->set_texture_render_state(textureID[block], D3DRS_TEXTUREADDRESSV, D3DTADDRESS_CLAMP);

	if (bTransparentMode==0)	// palettized, has no transparency
	{
		U8 * buffer = (U8 *) calloc(width * height*sizeof(*buffer), 1);
		RECT rect = blockRect[block];

		if (pRect)
		{
			rect.left   += pRect->left;
			rect.right  += pRect->left;
			rect.top    += pRect->top;
			rect.bottom += pRect->top;
		}

		reader->GetImage(PF_COLOR_INDEX, buffer, &rect);
		PIPE->set_texture_level_data(textureID[block], 0, width, height, width, 
			PixelFormat(PF_COLOR_INDEX),
			buffer, 0, rgbData);
		::free(buffer);
	}
	else	// palettized, but transparency in the palette
	{
		COLORREF * buffer = (COLORREF *) calloc(width * height*sizeof(*buffer), 1);
		RECT rect = blockRect[block];

		if (pRect)
		{
			rect.left   += pRect->left;
			rect.right  += pRect->left;
			rect.top    += pRect->top;
			rect.bottom += pRect->top;
		}

		reader->GetImage(PF_RGBA, buffer, &rect);
		PIPE->set_texture_level_data(textureID[block], 0, width, height, width*sizeof(*buffer), 
			PixelFormat(PF_RGBA), 
			buffer, 0, 0);
		::free(buffer);
	}

	if ((over = rect.left + width - imageWidth) > 0)
		blockRect[block].right -= over;
	if ((over = rect.top + height - imageHeight) > 0)
		blockRect[block].bottom -= over;

	//
	// add offset for D3DTexture mode
	//
	if (CQFLAGS.bTextureBias)
	{
		blockRect[block].toffsetx = (1.0F / (width*2.0F));
		blockRect[block].toffsety = (1.0F / (height*2.0F));
		blockRect[block].twidth += blockRect[block].toffsetx;
		blockRect[block].theight += blockRect[block].toffsety;
	}
	else
	{
		blockRect[block].toffsetx = blockRect[block].toffsety = 0;
	}

	//
	// convert to real coordinates
	//
	if(bHiRes)
	{
		blockRect[block].left  = HRIDEAL2IDEALX(blockRect[block].left);
		blockRect[block].right = HRIDEAL2IDEALX(blockRect[block].right);
		blockRect[block].top = HRIDEAL2IDEALY(blockRect[block].top);
		blockRect[block].bottom = HRIDEAL2IDEALY(blockRect[block].bottom);
	}
	if (bScaleData)
	{
		blockRect[block].left  = IDEAL2REALX(blockRect[block].left);
		blockRect[block].right = IDEAL2REALX(blockRect[block].right+1);
		blockRect[block].top = IDEAL2REALY(blockRect[block].top);
		blockRect[block].bottom = IDEAL2REALY(blockRect[block].bottom+1);
	}
	else
	{
		blockRect[block].left  = blockRect[block].left;
		blockRect[block].right = blockRect[block].right+1;
		blockRect[block].top = blockRect[block].top;
		blockRect[block].bottom = blockRect[block].bottom+1;
	}

	static int loadTextureDumpCount = 0;
	if (DrawAgentShouldDumpImage(loadTextureDumpCount, imageWidth, imageHeight, 128))
	{
		nlohmann::json dump = {
			{"event", "DrawAgent::loadTexture"},
			{"this", reinterpret_cast<std::uintptr_t>(this)},
			{"block", block},
			{"texture", static_cast<long long>(textureID[block])},
			{"imageWidth", imageWidth},
			{"imageHeight", imageHeight},
			{"textureWidth", width},
			{"textureHeight", height},
			{"transparent", bTransparentMode != 0},
			{"scaleData", bScaleData != 0},
			{"hiRes", bHiRes != 0},
			{"inputRect", DrawAgentRectJson(rect)},
			{"blockRect", DrawAgentRectJson(blockRect[block])}
		};
		VTDUMP_LOG(dump);
	}
}
//--------------------------------------------------------------------------//
//
U32 DrawAgent::nearestPower (S32 number)
{
	int i = 31;

	while (number > 0)
	{
		number <<= 1;
		i--;
	}

	if (i && (number==0 || (number << 1) != 0))
		i++;	// round up

	return (1 << i);
}
//--------------------------------------------------------------------------//
//
BOOL32 DrawAgent::init (struct IImageReader * reader, BOOL32 bScaleData, BOOL32 _bHiRes, const RECT * pRect)
{
	BOOL32 result = 0;
	U32 width, height, imax, jmax, t;
	U32 i, j;
	BLOCKRECT rect;
	COLORREF palette[256];
	RGB rgbData[256];
	BOOL32 bTransparentMode;

	bHiRes = (_bHiRes != 0);

	if (pRect)
	{
		imageWidth  = width  = pRect->right - pRect->left + 1;
		imageHeight = height = pRect->bottom - pRect->top + 1;
	}
	else
	{
		imageWidth  = width = reader->GetWidth();
		imageHeight = height = reader->GetHeight();
	}

	if (reader->GetColorTable(PF_RGBA, palette) != GR_OK)		// palettized image
		CQBOMB0("Non-palettized image.");	

	bTransparentMode = constructShape(reader, palette, pRect);
	const BOOL32 bForcedOpaqueLargeImage = bTransparentMode && width >= 700 && height >= 500;
	if (bForcedOpaqueLargeImage)
	{
		bTransparentMode = 0;
	}
	reader->GetColorTable(PF_RGB, rgbData);

	b3DEnabled = (CQFLAGS.b3DEnabled!=0);

	if (CQFLAGS.bFrameLockEnabled || CQFLAGS.b3DEnabled==0)
	{
		result = 1;
		return result;
	}

	U32 maxWidth, maxHeight, maxSize;

	PIPE->query_device_ability(RP_A_TEXTURE_MAX_WIDTH, &maxWidth);
	PIPE->query_device_ability(RP_A_TEXTURE_MAX_HEIGHT, &maxHeight);

	maxSize = __min(maxWidth, maxHeight);
	CQASSERT(maxSize == nearestPower(maxSize));
	const int TMAX = __min(maxSize, nearestPower(__min(width, height)));

	//
	// calculate the number of textures we'll need
	//

	imax = (width + (TMAX-1)) / TMAX;
	jmax = (height + (TMAX-1)) / TMAX;

	if ((numTextures = imax * jmax) == 0)
	{
		CQBOMB0("numTextures==0");
		goto Done;
	}

	textureID = new LONG_PTR[numTextures];
	hintID = new U32[numTextures];
	memset(hintID,0xff,sizeof(U32)*numTextures);
	blockRect = new BLOCKRECT[numTextures];

	t = 0;
	for (j = 0; j < jmax; j++)
	{
		for (i = 0; i < imax; i++)
		{
			rect.left = i * TMAX;
			rect.top = j * TMAX;

			const U32 sourceTileWidth = __min(TMAX, width - rect.left);
			const U32 sourceTileHeight = __min(TMAX, height - rect.top);
			const U32 textureTileWidth = nearestPower(sourceTileWidth);
			const U32 textureTileHeight = nearestPower(sourceTileHeight);

			rect.right = rect.left + textureTileWidth - 1;
			rect.bottom = rect.top + textureTileHeight - 1;
			rect.twidth = (float)sourceTileWidth / (float)textureTileWidth;
			rect.theight = (float)sourceTileHeight / (float)textureTileHeight;

			loadTexture(t++, rect, reader, bTransparentMode, rgbData, bScaleData, bHiRes, pRect);
		}
	}

	static int initDumpCount = 0;
	if (DrawAgentShouldDumpImage(initDumpCount, imageWidth, imageHeight, 64))
	{
		nlohmann::json dump = {
			{"event", "DrawAgent::init"},
			{"this", reinterpret_cast<std::uintptr_t>(this)},
			{"imageWidth", imageWidth},
			{"imageHeight", imageHeight},
			{"sourceWidth", width},
			{"sourceHeight", height},
			{"maxWidth", maxWidth},
			{"maxHeight", maxHeight},
			{"maxSize", maxSize},
			{"tmax", TMAX},
			{"imax", imax},
			{"jmax", jmax},
			{"numTextures", numTextures},
			{"loadedTextures", t},
			{"transparent", bTransparentMode != 0},
			{"forcedOpaqueLargeImage", bForcedOpaqueLargeImage != 0},
			{"scaleData", bScaleData != 0},
			{"hiRes", bHiRes != 0}
		};
		VTDUMP_LOG(dump);
	}

	result = 1;

Done:
	return result;
}
//--------------------------------------------------------------------------//
// return TRUE if palettized image has any transparency.
//
BOOL32 DrawAgent::constructShape (struct IImageReader * reader, COLORREF palette[256], const RECT * pRect)
{
	const U32 size = imageHeight * imageWidth;
	U8 * buffer = (U8 *) xmalloc(size * sizeof(*buffer));
	U32 transparent = (U32)-1;
	U32 i;
	PANE pane;
	VFX_WINDOW window;
	U32 shapeSize;
	VFX_CRGB vfx_palette[256];
	BOOL32 result = 0;

	reader->GetImage(PF_COLOR_INDEX, buffer, pRect);

	//
	// find a pixel whose alpha level is 0
	//
	for (i = 0; i < size; i++)
	{
		if ((palette[buffer[i]] & 0xFF000000) == 0)
		{
			transparent = buffer[i];
			result = 1;
			break;
		}
	}

	if (transparent != (U32)-1)		// found a transparent pixel
	{
		// force all transparent pixels to this index
		for (i = 0; i < size; i++)
		{
			if ((palette[buffer[i]] & 0xFF000000) == 0)
				buffer[i] = (U8) transparent;
		}
	}
	else
	{
		//
		// else find an index value that is not used
		//
		U8 used[256];

		memset(used, 0, sizeof(used));
		for (i = 0; i < size; i++)
		{
			used[buffer[i]] = 1;
		}

		for (i = 0; i < 256; i++)
		{
			if (used[i] == 0)
			{
			 	palette[i] &= ~0xFF000000;		// clear the alpha channel of this one
				break;
			}
		}

		if (i < 256)	// we have an unused index
		{
			COLORREF * colors = (COLORREF *) xmalloc(size * sizeof(*colors));

			transparent = i;
			result = 1;
			reader->GetImage(PF_RGBA, colors, pRect);
			for (i = 0; i < size; i++)
			{
				if ((colors[i] & 0xFF000000) == 0)	// transparent
				{
					buffer[i] = (U8) transparent;
				}
			}
			free(colors);
		}
	}

	if (CQFLAGS.bFrameLockEnabled==0)		// don't bother constructing the shape
		goto Done;

	window.buffer = buffer;
	window.x_max = imageWidth - 1;
	window.y_max = imageHeight - 1;
	window.pixel_pitch = 1;
	window.bytes_per_pixel = 1;

	pane.window = &window;
	pane.x0 = pane.y0 = 0;
	pane.x1 = imageWidth - 1;
	pane.y1 = imageHeight - 1;

	//
	// allocate memory for the shape, write shape into the buffer
	//

	shapeSize = (VFX_shape_scan8(&pane, transparent, 0, 0, 0) + 3) & ~3;
	vfxShape = (VFX_SHAPETABLE_EX *) xmalloc(shapeSize + sizeof(vfx_palette) + sizeof(U32));
	VFX_shape_scan8(&pane, transparent, 0, 0, vfxShape);
	vfxShape->palette_offset = shapeSize;

	//
	// construct vfx palette
	//

	for (i = 0; i < 256; i++)
	{
		vfx_palette[i].color = (U8 ) i;
		vfx_palette[i].rgb.r = GetRValue(palette[i]) >> 2;
		vfx_palette[i].rgb.g = GetGValue(palette[i]) >> 2;
		vfx_palette[i].rgb.b = GetBValue(palette[i]) >> 2;
	}

	//
	// add the palette to the shape
	//

	((U32 *)(((U8 *)vfxShape)+shapeSize))[0] = 256;		// number of palette entries
	memcpy(((U8 *)vfxShape)+shapeSize+sizeof(U32), vfx_palette, sizeof(vfx_palette));

Done:
	free(buffer);
	return result;
}
//--------------------------------------------------------------------------//
//--------------------------------------------------------------------------//
//
void __stdcall CreateDrawAgent (struct IImageReader * reader, struct IDrawAgent ** _drawAgent,BOOL32 bHiRes ,RECT * pRect)
{
	DrawAgent * drawAgent = new DAComponentX<DrawAgent>;
	
	if (drawAgent->init(reader, 1, bHiRes, pRect))
		*_drawAgent = drawAgent;
	else
	{
		*_drawAgent = 0;
		delete drawAgent;
	}
}
//--------------------------------------------------------------------------//
//
void __stdcall CreateDrawAgentForFonts (struct IImageReader * reader, struct IDrawAgent ** _drawAgent, RECT * pRect)
{
	DrawAgent * drawAgent = new DAComponentX<DrawAgent>;
	
	if (drawAgent->init(reader, 0, false, pRect))
		*_drawAgent = drawAgent;
	else
	{
		*_drawAgent = 0;
		delete drawAgent;
	}
}
//--------------------------------------------------------------------------//
//
BOOL32 __stdcall CreateJsonReplacementDrawAgent(const char* filename, U32 subImage, struct IDrawAgent** drawAgent, BOOL32 bHiRes)
{
	if (drawAgent)
	{
		*drawAgent = 0;
	}
	if (!drawAgent)
	{
		return 0;
	}

	const JsonShapeReplacement* replacementInfo = FindJsonShapeReplacement(filename);
	if (!replacementInfo)
	{
		return 0;
	}

	std::string jsonPath;
	if (FindFrameJson(replacementInfo->jsonFilename, jsonPath))
	{
		DrawAgent * replacement = new DAComponentX<DrawAgent>;
		if (replacement->initJsonReplacement(filename, jsonPath.c_str(), subImage, bHiRes))
		{
			*drawAgent = replacement;
			return 1;
		}
		delete replacement;
	}

	nlohmann::json dump = {
		{"event", "DrawAgent::jsonReplacementMiss"},
		{"source", filename ? filename : ""},
		{"json", replacementInfo->jsonFilename},
		{"frame", subImage}
	};
	VTDUMP_LOG(dump);

	return 0;
}
//--------------------------------------------------------------------------
//
void __stdcall CreateDrawAgent (const char * filename, IComponentFactory *parentFile,
								DA::FILETYPE type, U32 subImage, struct IDrawAgent ** drawAgent, BOOL32 bHiRes)
{
	COMPTR<IImageReader> reader;
	COMPTR<IFileSystem> file;
	DAFILEDESC fdesc = filename;
	*drawAgent = 0;

	if (CreateJsonReplacementDrawAgent(filename, subImage, drawAgent, bHiRes))
	{
		return;
	}

	fdesc.lpImplementation = "DOS";
	if (parentFile == 0)
		parentFile = DACOM;
	
	if (parentFile->CreateInstance(&fdesc, file.void_addr()) != GR_OK)
	{
		CQFILENOTFOUND(fdesc.lpFileName);
		if (strcmp(fdesc.lpFileName, "ZeroDeg.bmp") == 0)		// no infinite loops
			return;
		CreateDrawAgent("ZeroDeg.bmp", TEXTURESDIR, type, subImage, drawAgent);
		return;
	}

	HANDLE hMapping = file->CreateFileMapping();
	U8 *pMemory = static_cast<U8 *>(file->MapViewOfFile(hMapping));
	U32 fileSize = file->GetFileSize();

	if (type == DA::UNKTYPE)
	{
		U32 filetype = ((U32 *)(filename + strlen(filename) - 4))[0] & ~0x20202000;

		switch (filetype)
		{
		case 'PMB.':
			type = DA::BMP;
			break;
		case 'PHS.':
			type = DA::VFX;
			break;
		case 'AGT.':
			type = DA::TGA;
			break;
		}
	}

	switch (type)
	{
	case DA::BMP:
		CreateBMPReader(reader.addr());
		reader->LoadImage(pMemory+14, fileSize, subImage);		// +14 skip bogus header stuff
		break;
	case DA::VFX:
		CreateVFXReader(reader.addr());
		reader->LoadImage(pMemory, fileSize, subImage);
		break;
	case DA::TGA:
		CreateTGAReader(reader.addr());
		reader->LoadImage(pMemory, fileSize, subImage);
		break;
	default:
		CQBOMB0("Invalid file type");
	}

	file->UnmapViewOfFile(pMemory);
	file->CloseHandle(hMapping);

	CreateDrawAgent(reader, drawAgent,bHiRes);
}
//--------------------------------------------------------------------------//
//
void __stdcall CreateDrawAgent (const VFX_SHAPETABLE * vfxShape, U32 subImage, struct IDrawAgent ** drawAgent, BOOL32 bHiRes, RECT * pRect)
{
	COMPTR<IImageReader> reader;
  	CreateVFXReader(reader.addr());
 
	if (reader->LoadImage((void *)vfxShape, 0, subImage) == GR_OK)
		CreateDrawAgent(reader, drawAgent, bHiRes, pRect);
	else
		*drawAgent = 0;
}
//--------------------------------------------------------------------------//
//--------------------------FontDrawAgent class-----------------------------//
//--------------------------------------------------------------------------//
//
struct TEXCOORD
{
	float x0, y0;
	float x1, y1;

};

struct DACOM_NO_VTABLE FontDrawAgent : IDebugFontDrawAgent
{
	static IDAComponent* GetIDebugFontDrawAgent(void* self) {
	    return static_cast<IDebugFontDrawAgent*>(
	        static_cast<FontDrawAgent*>(self));
	}

	static std::span<const DACOMInterfaceEntry2> GetInterfaceMap() {
	    static const DACOMInterfaceEntry2 map[] = {
	        {"IDebugFontDrawAgent", &GetIDebugFontDrawAgent},
	    };
	    return map;
	}

	//-------------------------------
	// vfx data
	//-------------------------------

	VFX_FONT *vfxFont;
	U16	     lookup[256];	 //	8 -> 16 bit converter

	//-------------------------------
	// texture data
	//-------------------------------
	
	U32		 numChars;
	TEXCOORD *texCoord;
	LONG_PTR textureID;


	
	FontDrawAgent (void);

	~FontDrawAgent (void);

    void * operator new (size_t size)
	{
		return calloc(size, 1);
	}

	void   operator delete (void *ptr)
	{
		::free(ptr);
	}

	/* IFontDrawAgent methods  */
 
	DEFMETHOD_(U32,GetFontHeight) (void) const;

	DEFMETHOD_(U32,GetStringWidth) (const char *string) const;

	DEFMETHOD_(void,StringDraw) (PANE *pane, S32 x, S32 y, const char *string, COLORREF color);
	
	/* FontDrawAgent methods  */

	BOOL32 init (const VFX_FONT * font, U32 fontImageSize, const char * txm_name = 0);

	static inline void makeSquare (U32 & width, U32 & height)
	{
		while (width < height)
			width *= 2;
		while (height < width)
			height *= 2;
	}
};
//--------------------------------------------------------------------------//
//
FontDrawAgent::FontDrawAgent (void)
{
}
//--------------------------------------------------------------------------//
//
FontDrawAgent::~FontDrawAgent (void)
{
	free(vfxFont);
	free(texCoord);
	PIPE->destroy_texture(textureID);
}
//--------------------------------------------------------------------------//
//
U32 FontDrawAgent::GetFontHeight (void) const
{
	return vfxFont->char_height;
}
//--------------------------------------------------------------------------//
//
U32 FontDrawAgent::GetStringWidth (const char *string) const
{
	U32 result=0;

	while (string[0])
		result += VFX_character_width(vfxFont, *string++);

	return result;
}
//--------------------------------------------------------------------------//
//
void FontDrawAgent::StringDraw (PANE *pane, S32 x, S32 y, const char *string, COLORREF color)
{
	lookup[255] = (U16) ColorRefToPixel(color);
	
	if (CQFLAGS.b3DEnabled==false && CQFLAGS.bFrameLockEnabled==false)
		return;		// cannot draw, frame lock failed

	// if locking the frame buffer is supported
	if (CQFLAGS.bFrameLockEnabled)
	{
		if (pane == 0)
			pane = const_cast<PANE *>(&SURFACE->GetPane());
		else
		if (pane->window == 0)
			pane->window = const_cast<VFX_WINDOW *>(&SURFACE->GetWindow());

		VFX_string_draw(pane, x, y, vfxFont, string, lookup);
	}
	else	// use 3D rendering system
	{
		S32 c, width;

		OrthoView(pane);
	/*	glEnable(GL_TEXTURE_2D);
		glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_BLEND);
		glDisable(GL_CULL_FACE);		// dont do back facing check
		glColor3ub(GetRValue(color),GetGValue(color),GetBValue(color));
		glDisable(GL_LIGHTING);
		glDisable(GL_DEPTH_TEST);*/

		PIPE->set_render_state(D3DRS_ZENABLE,FALSE);
		PIPE->set_render_state(D3DRS_ALPHABLENDENABLE,TRUE);
		PIPE->set_render_state(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
		PIPE->set_render_state(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);

//		PrimitiveBuilder PB(PIPE);
		PB.Color3ub(GetRValue(color),GetGValue(color),GetBValue(color));

		if (pane)
		{
			x += pane->x0;
			y += pane->y0;
		}

		//glBindTexture(GL_TEXTURE_2D,textureID);
		//PIPE->set_texture_stage_texture(0,textureID);
		SetupDiffuseBlend(textureID,TRUE);

		PB.Begin(PB_QUADS);

		while ((c = *string++) != 0)
		{
			width = VFX_character_width(vfxFont, (U8)c);
		
	  		PB.TexCoord2f(texCoord[c].x0,texCoord[c].y0);  		PB.Vertex3f(x,y,0);
		  	PB.TexCoord2f(texCoord[c].x1,texCoord[c].y0);		PB.Vertex3f(x+width, y,0);
		  	PB.TexCoord2f(texCoord[c].x1,texCoord[c].y1);		PB.Vertex3f(x+width, y+vfxFont->char_height,0);
		  	PB.TexCoord2f(texCoord[c].x0,texCoord[c].y1); 		PB.Vertex3f(x, y+vfxFont->char_height,0);

			x += width; 
		}

		PB.End(); 	// end of GL_QUADS

		//glEnable(GL_DEPTH_TEST);
	}
}
//--------------------------------------------------------------------------//
//
BOOL32 FontDrawAgent::init (const VFX_FONT * font, U32 fontImageSize, const char * txm_name)
{
	BOOL32 result = 0;
	U32 i, j;
	U32 maxWidth;
	U32 numEmpty=0;	// number of cells that have 0 width
	U8 * buffer = 0;
	PANE pane;
	VFX_WINDOW window;
	PixelFormat desiredFormat(16, 5, 5, 5, 1);		// GL_RGB5_A1


	vfxFont = (VFX_FONT *) xmalloc(fontImageSize);
	memcpy(vfxFont, font, fontImageSize);

/*	for (i = 0; i < 256; i++)	  
	{
		if (_palette[i] & 0x80000000)
			lookup[i] = (U16) ColorRefToPixel(_palette[i]);
		else
			lookup[i] = RGB_TRANSPARENT;
	}*/

	lookup[0] = RGB_TRANSPARENT;

	if (CQFLAGS.b3DEnabled)
	{
		//
		// find the maximum character width
		//
		numChars = font->char_count;
		maxWidth = 0;
		for (i = 0; i < numChars; i++)
		{
			U32 cwidth;
			if ((cwidth = VFX_character_width(vfxFont, i)) > maxWidth)
				maxWidth = cwidth;
			else
			if (cwidth == 0)
				numEmpty++;
		}

		U32 twidth = 64;
		U32 chars_per_row=0, numRows=0;
		U32 theight=0;

		while (1)
		{
			if ((chars_per_row = twidth / maxWidth) != 0)
				numRows = (numChars - numEmpty + chars_per_row - 1) / chars_per_row;
			else
				numRows = 256;		// a big number

			theight = DrawAgent::nearestPower(numRows * font->char_height);

			if (theight <= twidth)
				break;

			twidth *= 2;
			if (twidth > 256)
				goto Done;			// can't fit the font on one texture
		}

		//
		// create the texture
		//

		if (PIPE->create_texture(twidth, theight, desiredFormat,1, 0, textureID) != GR_OK)
		{
			makeSquare(twidth, theight);
			if (PIPE->create_texture(twidth, theight, desiredFormat,1, 0, textureID) != GR_OK)
				CQBOMB0("create_texture() failed on debug font.");
		}

		texCoord = (TEXCOORD *) xmalloc(numChars * sizeof(*texCoord));
		buffer = (U8 *) xmalloc(twidth * theight);
		memset(buffer, font->font_background, twidth * theight);

		window.buffer = buffer;
		window.x_max = twidth - 1;
		window.y_max = theight - 1;
		window.pixel_pitch = 1;
		window.bytes_per_pixel = 1;

		pane.window = &window;
		pane.x0 = pane.y0 = 0;
		pane.x1 = twidth - 1;
		pane.y1 = theight - 1;

		{
			U32 c;
			S32 x, y;
			const SINGLE soffset = (CQFLAGS.bTextureBias) ? (1.0F / (twidth*2.0F)) : 0;
			const SINGLE toffset = (CQFLAGS.bTextureBias) ? (1.0F / (theight*2.0F)) : 0;

			for (c=y=j=0; j < numRows && c < numChars; j++, y+=font->char_height)
			{
				for (x=i=0; i < chars_per_row && c < numChars; c++)
				{
					U32 w;
					w = VFX_character_draw(&pane, x, y, vfxFont, c, 0);
					texCoord[c].x0 = (float(x) / float(twidth)) + soffset;
					texCoord[c].y0 = (float(y) / float(theight)) + toffset;
					texCoord[c].x1 = (float(x+w) / float(twidth)) + soffset;
					texCoord[c].y1 = (float(y+font->char_height) / float(theight)) + toffset;
					if (w)
					{
						i++;
						x+=maxWidth;
					}
				}
			}
		}
		
		{
			RGB palette[256];
			palette[0].r = 0;			// background color is black
			palette[0].g = 0;
			palette[0].b = 0;
			palette[255].r = 255;		// pen color is white by default
			palette[255].g = 255;
			palette[255].b = 255;
			U8 *alpha = (U8 *) xmalloc(twidth * theight);
			memset(alpha, -1, twidth * theight);
			U32 i;
			for (i = 0; i < twidth * theight; i++)
				if (buffer[i] == 0)
					alpha[i] = 0;				// background is transparent

			PIPE->set_texture_level_data(textureID, 0, twidth, theight, twidth, 
										 PixelFormat(PF_COLOR_INDEX), buffer, alpha, palette);
			free(alpha);
		}
	}
	result = 1;

Done:
	free(buffer);
	return result;
}

//--------------------------------------------------------------------------//
//--------------------------------------------------------------------------//
//
void __stdcall DEBUGCreateFontDrawAgent (const VFX_FONT * font, U32 fontImageSize, struct IDebugFontDrawAgent ** _fontDrawAgent, const char *txm_name)
{
	FontDrawAgent * fontDrawAgent = new DAComponentX<FontDrawAgent>;
	
	if (fontDrawAgent->init(font, fontImageSize, txm_name))
		*_fontDrawAgent = fontDrawAgent;
	else
	{
		*_fontDrawAgent = 0;
		delete fontDrawAgent;
	}
}
//--------------------------------------------------------------------------//
//
void DA::LineDraw (const PANE * pane, S32 x0, S32 y0, S32 x1, S32 y1, COLORREF color, BOOL32 bSmooth)
{
	// guard against the awkward system map issue
	if (pane && pane == CAMERA->GetPane())
	{
		if (y0 > pane->y1 - IDEAL2REALY(AWKWARD_HEIGHT) && y1 > pane->y1 - IDEAL2REALY(AWKWARD_HEIGHT))
		{
			if (CQFLAGS.bNoToolbar==0)
				return;
		}
	}

	// if locking the frame buffer is supported
	if (CQFLAGS.bFrameLockEnabled && SURFACE->IsLocked())
	{
		if (pane == 0)
			pane = const_cast<PANE *>(&SURFACE->GetPane());
		else
		if (pane->window == 0)
			const_cast<PANE *>(pane)->window = const_cast<VFX_WINDOW *>(&SURFACE->GetWindow());

		if (bSmooth)
			AALine(pane, x0, y0, x1, y1, color);
		else
			VFX_line_draw(pane, x0, y0, x1, y1, LD_DRAW, ColorRefToPixel(color));
	}
	else  // must use 3D rendering system
	{
		OrthoView(pane);
		PB.Color3ub(GetRValue(color),GetGValue(color),GetBValue(color));

		static U32 hintID=0xffffffff;
		//		PIPE->set_texture_stage_texture(0,0);
		DisableTextures();
		if (CQBATCH)
		{
			CQBATCH->SetMStateID(5,hintID,D3DPT_LINELIST,D3DFVF_RPVERTEX2);
		}
		
		/*	if (bSmooth)
		{
			glEnable(GL_LINE_SMOOTH);
			glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
		}*/

		if (pane)
		{
			x0 += pane->x0;
			x1 += pane->x0;
			y0 += pane->y0;
			y1 += pane->y0;
		}
		
		PB.Begin(PB_LINES);
		PB.Vertex3f(float(x0)+0.5F, float(y0)+0.5F,0);
		PB.Vertex3f(float(x1)+0.5F, float(y1)+0.5F,0);
  		PB.End();

		if (CQBATCH)
		{
			BATCH->set_state(RPR_STATE_ID,0);
		}

		//if (bSmooth)
		//	glDisable(GL_LINE_SMOOTH);
	}
}
//--------------------------------------------------------------------------//
//
void DA::PointDraw (const PANE * pane, S32 x, S32 y, COLORREF color)
{
	// guard against the awkward system map issue
	if (pane && pane == CAMERA->GetPane())
	{
		if (y > pane->y1 - IDEAL2REALY(AWKWARD_HEIGHT))
		{
			if (CQFLAGS.bNoToolbar==0)
				return;
		}
	}

	// if locking the frame buffer is supported
	if (CQFLAGS.bFrameLockEnabled)
	{
		if (pane == 0)
			pane = const_cast<PANE *>(&SURFACE->GetPane());
		else
		if (pane->window == 0)
			const_cast<PANE *>(pane)->window = const_cast<VFX_WINDOW *>(&SURFACE->GetWindow());

		VFX_pixel_write(pane, x, y, ColorRefToPixel(color));
	}
	else  // must use 3D rendering system
	{
		float x0,x1,y0,y1;

		OrthoView(pane);
	//	PrimitiveBuilder PB(PIPE);
		//glHint(GL_VOLUME_CLIPPING_HINT,GL_NICEST);
		PB.Color3ub(GetRValue(color),GetGValue(color),GetBValue(color));

		x0 = (float)x; y0 = (float)y;
		x0 -= 0.5F;
		y0 -= 0.5F;
		x1 = x0 + 1.0F;
		y1 = y0 + 1.0F;
		
		if (pane)
		{
			x0 += (float)pane->x0;
			x1 += (float)pane->x0;
			y0 += (float)pane->y0;
			y1 += (float)pane->y0;
		}
		
		
		PB.Begin(PB_LINES);
		PB.Vertex3f(x0, y0,0);
		PB.Vertex3f(x1, y1,0);
  		PB.End();
	}
}
//--------------------------------------------------------------------------//
//
void DA::RectangleHash (const PANE *pane, S32 x0, S32 y0, S32 x1, S32 y1, COLORREF color)
{
	// guard against the awkward system map issue
	if (pane && pane == CAMERA->GetPane())
	{
		if (y0 > pane->y1 - IDEAL2REALY(AWKWARD_HEIGHT) && y1 > pane->y1 - IDEAL2REALY(AWKWARD_HEIGHT))
		{
			if (CQFLAGS.bNoToolbar==0)
				return;
		}
	}

	// if locking the frame buffer is supported
	if (CQFLAGS.bFrameLockEnabled)
	{
		if (pane == 0)
			pane = const_cast<PANE *>(&SURFACE->GetPane());
		else
		if (pane->window == 0)
			const_cast<PANE *>(pane)->window = const_cast<VFX_WINDOW *>(&SURFACE->GetWindow());

		VFX_rectangle_hash(pane, x0, y0, x1, y1, ColorRefToPixel(color));
	}
	else	// must use 3D rendering system
	{
		OrthoView(pane);

		PIPE->set_render_state(D3DRS_ZENABLE,FALSE);
		PIPE->set_render_state(D3DRS_ALPHABLENDENABLE,TRUE);
		PIPE->set_render_state(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
		PIPE->set_render_state(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
		DisableTextures();

		if (pane)
		{
			x0 += pane->x0;
			x1 += pane->x0;
			y0 += pane->y0;
			y1 += pane->y0;
		}
		
//		PB.Color3ub(0.4*GetRValue(color),0.4*GetGValue(color),0.4*GetBValue(color));
		PB.Color4ub(GetRValue(color),GetGValue(color),GetBValue(color),100);
		PB.Begin(PB_QUADS);

	  	PB.Vertex3f(x0,y0,0);
	  	PB.Vertex3f(x1+1,y0,0);
	  	PB.Vertex3f(x1+1,y1+1,0);
	  	PB.Vertex3f(x0,y1+1,0);

		PB.End(); 	// end of GL_QUADS
	}
}
//--------------------------------------------------------------------------//
//
void DA::RectangleFill (const PANE *pane, S32 x0, S32 y0, S32 x1, S32 y1, COLORREF color)
{
	// guard against the awkward system map issue
	if (pane && pane == CAMERA->GetPane())
	{
		if (y0 > pane->y1 - IDEAL2REALY(AWKWARD_HEIGHT) && y1 > pane->y1 - IDEAL2REALY(AWKWARD_HEIGHT))
		{
			if (CQFLAGS.bNoToolbar==0)
				return;
		}
	}

	// if locking the frame buffer is supported
	if (CQFLAGS.bFrameLockEnabled)
	{
		PANE local;
		if (pane == 0)
			pane = const_cast<PANE *>(&SURFACE->GetPane());
		else
		if (pane->window == 0)
			const_cast<PANE *>(pane)->window = const_cast<VFX_WINDOW *>(&SURFACE->GetWindow());

		local.window = pane->window;
		local.x0 = x0+pane->x0;
		local.x1 = x1+pane->x0;
		local.y0 = y0+pane->y0;
		local.y1 = y1+pane->y0;

		if (local.x0 < pane->x0)
			local.x0 = pane->x0;
		if (local.y0 < pane->y0)
			local.y0 = pane->y0;
		if (local.x1 > pane->x1)
			local.x1 = pane->x1;
		if (local.y1 > pane->y1)
			local.y1 = pane->y1;

		VFX_pane_wipe(&local, ColorRefToPixel(color));
	}
	else	// must use 3D rendering system
	{
		OrthoView(pane);
		DisableTextures();
	/*	glHint(GL_VOLUME_CLIPPING_HINT,GL_NICEST);
		glColor3ub(GetRValue(color),GetGValue(color),GetBValue(color));
		glDisable(GL_BLEND);
		glDisable(GL_TEXTURE_2D);
		glDisable(GL_DEPTH_TEST);*/

		if (pane)
		{
			x0 += pane->x0;
			x1 += pane->x0;
			y0 += pane->y0;
			y1 += pane->y0;
		}
		
//		PrimitiveBuilder PB(PIPE);
		BATCH->set_state(RPR_STATE_ID,6);
		PB.Color3ub(GetRValue(color),GetGValue(color),GetBValue(color));
		PB.Begin(PB_QUADS);

	  	PB.Vertex3f(x0,y0,0);
	  	PB.Vertex3f(x1+1,y0,0);
	  	PB.Vertex3f(x1+1,y1+1,0);
	  	PB.Vertex3f(x0,y1+1,0);

		PB.End(); 	// end of GL_QUADS
		BATCH->set_state(RPR_STATE_ID,0);
	}
}
//--------------------------------------------------------------------------//
//
void DA::DrawTexture (const PANE *pane, S32 x0, S32 y0, S32 x1, S32 y1, U32 textureHandle, SINGLE u0, SINGLE v0, SINGLE u1, SINGLE v1)
{
	// guard against the awkward system map issue
	if (pane && pane == CAMERA->GetPane())
	{
		if (y0 > pane->y1 - IDEAL2REALY(AWKWARD_HEIGHT) && y1 > pane->y1 - IDEAL2REALY(AWKWARD_HEIGHT))
		{
			if (CQFLAGS.bNoToolbar==0)
				return;
		}
	}

	BATCH->set_state(RPR_BATCH,false);
	OrthoView(pane);
	BATCH->set_render_state(D3DRS_ALPHABLENDENABLE,FALSE);
	BATCH->set_render_state(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
	BATCH->set_render_state(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
	BATCH->set_render_state(D3DRS_ZENABLE,false);
	BATCH->set_render_state(D3DRS_ZWRITEENABLE, false);
	SetupDiffuseBlend(textureHandle,false);

	if (pane)
	{
		x0 += pane->x0;
		x1 += pane->x0;
		y0 += pane->y0;
		y1 += pane->y0;
	}
	
	PB.Color4ub(255,255,255,255);
	PB.Begin(PB_QUADS);

	PB.TexCoord2f(u0,v0); PB.Vertex3f(x0,y0,0);
	PB.TexCoord2f(u1,v0); PB.Vertex3f(x1+1,y0,0);
	PB.TexCoord2f(u1,v1); PB.Vertex3f(x1+1,y1+1,0);
	PB.TexCoord2f(u0,v1); PB.Vertex3f(x0,y1+1,0);

	PB.End(); 	// end of GL_QUADS
	BATCH->set_state(RPR_STATE_ID,0);
}//--------------------------------------------------------------------------//
//------------------------------End DrawAgent.cpp---------------------------//
//--------------------------------------------------------------------------//
