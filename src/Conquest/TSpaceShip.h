#ifndef TSPACESHIP_H
#define TSPACESHIP_H
//--------------------------------------------------------------------------//
//                                                                          //
//                               TSpaceShip.h                               //
//                                                                          //
//                  COPYRIGHT (C) 1998 BY DIGITAL ANVIL, INC.               //
//                                                                          //
//--------------------------------------------------------------------------//
/*
    $Header: /Conquest/App/Src/TSpaceShip.h 146   8/23/01 1:53p Tmauer $
*/
//------------------------------- #INCLUDES --------------------------------//
//--------------------------------------------------------------------------//

#include "TObjMove.h"
#include "TObjFrame.h"
#include "TObjWarp.h"
#include "TObjBuild.h"
#include "TObjSelect.h"
#include "TObjPhys.h"
#include "TObjControl.h"
#include "TObject.h"
#include "IExplosion.h"
#include "IMissionActor.h"
#include "IWeapon.h"
#include "IGotoPos.h"
#include "TObjTrans.h"
#include "TObjExtent.h"
#include "TObjCloak.h"
#include "TObjTeam.h"
#include "TObjGlow.h"
#include "TObjDamage.h"
#include "TObjMission.h"
#include "TObjRepair.h"
#include "TObjEffectTarget.h"
#include "SFX.h"
#include "Anim2d.h"
#include "GenData.h"
#include "IBlinkers.h"
#include "TManager.h"
#include "Sysmap.h"

#include <DSpaceShip.h>

#include "Hotkeys.h"

#ifndef __IANIM_H
#include <IAnim.h>
#endif

typedef SPACESHIP_INIT<BASE_SPACESHIP_DATA> BASESHIPINIT;

#define CASTINITPROC(x) castInitProc(InitProc2(x))
#define CASTSAVELOADPROC(x) castSaveLoadProc(SaveLoadProc2(x))

inline void FreakTexture (LONG_PTR texID,S16 gamma,SINGLE contrast)
{
    RPLOCKDATA data;
    unsigned long level=0;

    unsigned long width,height,num_levels;
    PIPE->get_texture_dim(texID,&width,&height,&num_levels);
    while (level < num_levels)
    {
        PIPE->lock_texture(texID,level,&data);
        RGB colors[256];

        if (data.pf.num_bits() == 16)
        {
            U8 r,g,b;
            for (unsigned int y=0;y<data.height;y++)
            {
                for (unsigned int x=0;x<data.width;x++)
                {
                    U16 *pixel = (U16 *)((U8 *)data.pixels+(y*data.pitch+x*2));
                    r = (*pixel >> data.pf.rl) << data.pf.rr;
                    g = (*pixel >> data.pf.gl) << data.pf.gr;
                    b = (*pixel >> data.pf.bl) << data.pf.br;
                    r = std::min(gamma+r*contrast,255.f);
                    g = std::min(gamma+g*contrast,255.f);
                    b = std::min(gamma+b*contrast,255.f);
                    *pixel = (r >> data.pf.rr) << data.pf.rl;
                    *pixel |= (g >> data.pf.gr) << data.pf.gl;
                    *pixel |= (b >> data.pf.br) << data.pf.bl;
                }
            }
        }
        else
        {
            PIPE->get_texture_palette(texID,0,256,colors);
            S16 r,g,b;
            for (int i=0;i<256;i++)
            {
                r = colors[i].r*contrast+gamma;
                g = colors[i].g*contrast+gamma;
                b = colors[i].b*contrast+gamma;

                colors[i].r = std::max((S16)0,std::min(r,(S16)255));
                colors[i].g = std::max((S16)0,std::min(g,(S16)255));
                colors[i].b = std::max((S16)0,std::min(b,(S16)255));
            }
            PIPE->set_texture_palette(texID,0,256,colors);
        }
        PIPE->unlock_texture(texID,level);

        level++;
    }
}

static void get_hotkey_text (U32 hotkey, wchar_t *buffer, U32 bufferSize)
{
    C8 tmp[64];

    if (HOTKEY->GetHotkeyText(hotkey, tmp, sizeof(tmp)))
    {
        _localAnsiToWide(tmp, buffer, bufferSize);
    }
}

template<class SaveStruct, class InitStruct>
struct _NO_VTABLE SpaceShip : public ObjectEffectTarget
                              <ObjectRepair
                                  <ObjectGlow
                                      <ObjectTeam
                                          <ObjectCloak
                                              <ObjectDamage //depends on ObjectBuild
                                                  <ObjectBuild
                                                      <ObjectExtent
                                                          <ObjectWarp
                                                              <ObjectMove
                                                                  <ObjectSelection
                                                                      <ObjectMission
                                                                          <ObjectPhysics
                                                                              <ObjectControl
                                                                                  <ObjectTransform
                                                                                      <ObjectFrame<IBaseObject,
                                                                                          SPACESHIP_SAVELOAD,
                                                                                          BASESHIPINIT> >
                                                                                  >
                                                                              >
                                                                          >
                                                                      >
                                                                  >
                                                              >
                                                          >
                                                      >
                                                  >
                                              >
                                          >
                                      >
                                  >
                              >,
                              IGotoPos, ISaveLoad, IQuickSaveLoad {
    BEGIN_MAP_INBOUND(SpaceShip)
        _INTERFACE_ENTRY(IBaseObject)
        _INTERFACE_ENTRY(IGotoPos)
        _INTERFACE_ENTRY(ISaveLoad)
        _INTERFACE_ENTRY(IQuickSaveLoad)
        _INTERFACE_ENTRY(IExplosionOwner)
        _INTERFACE_ENTRY(IMissionActor)
        _INTERFACE_ENTRY(IWeaponTarget)
        _INTERFACE_ENTRY(IBuild)
        _INTERFACE_ENTRY(IPhysicalObject)
        _INTERFACE_ENTRY(ICloak)
        _INTERFACE_ENTRY(IRepairee)
        _INTERFACE_ENTRY(IShipDamage)
        _INTERFACE_ENTRY(IExtent)
        _INTERFACE_ENTRY(IShipMove)
        _INTERFACE_ENTRY(IEffectTarget)
    END_MAP()

    //----------------------------------
    //
    //	SaveNode    saveNode;
    //	LoadNode    loadNode;
    //	struct UpdateNode  updateNode;
    struct ExplodeNode explodeNode;
    struct RenderNode renderNode;
    struct InitNode initNode;
    struct PreTakeoverNode preTakeoverNode;
    struct GeneralSyncNode genSyncNode;

    typedef SaveStruct SAVEINFO; // override base typedef
    typedef InitStruct INITINFO; // override base typedef

    //----------------------------------
    // animation index
    //----------------------------------
    S32 ambientAnimIndex;

    U32 hiliteTex;

    U32 billboardTex;
    U32 billboardTextTwo;

    bool bUpdateOnce;

    //
    // explosion data
    //
    PARCHETYPE pExplosionType;
    U32 firstNuggetID; // mission part ID for first nugget


    // render hint
    U32 hintID;

    // networking fixups
    S32 displaySupplies; // negative value means "uninitialized"
    S32 displayHull;
    S32 trueNetSupplies; // last value we sent to clients
    S32 trueNetHull; // last value we sent to clients
    U32 myKillerOwnerID; // ID of unit that killed me
    bool bHasHadHullPoints;
    //----------------------------------

    SpaceShip(void) :
        //				updateNode(this, UpdateProc(updateSpaceShip)),
        explodeNode(this, ExplodeProc(&SpaceShip::explodeSpaceShip)),
        renderNode(this, RenderProc(&SpaceShip::renderSpaceShip)),
        initNode(this, CASTINITPROC(&SpaceShip::initSpaceShip)),
        preTakeoverNode(this, PreTakeoverProc(&SpaceShip::preTakeoverShip)),
        genSyncNode(this, SyncGetProc(&SpaceShip::getSyncShipData), SyncPutProc(&SpaceShip::putSyncShipData)) {
        bUpdateOnce = false;
        ambientAnimIndex = -1;
        displaySupplies = displayHull = trueNetSupplies = trueNetHull = -1;
        hintID = 0xffffffff;
    }

    virtual ~SpaceShip(void) {
        if (bReady)
            SpaceShip<SaveStruct, InitStruct>::SetReady(false);
        ANIM->release_script_inst(ambientAnimIndex);
        ambientAnimIndex = -1;
    }

    /* IBaseObject methods */

    virtual BOOL32 Update(void) {
        BOOL32 result;

        updateOnce();

        updateDisplayValues();
        if (isMoveActive())
            updateFieldInfo();

        result = FRAME_update();

        return (result);
    }

    virtual void PhysicalUpdate(SINGLE dt) {
        FRAME_physicalUpdate(dt);

        //update the space ship specific effect parameters
        if (areThrustersOn()) {
            thrustParam += dt * 0.3f;
            if (thrustParam > 1.0f)
                thrustParam = 1.0f;
        } else {
            thrustParam -= dt * 0.3f;
            if (thrustParam < 0.0f)
                thrustParam = 0.0f;
        }
    }

    virtual void DrawFleetMoniker(bool bAllShips) {
        if (bVisible == 0 || (!bReady))
            return;

        // are we an admiral?
        if ((TESTADMIRAL(dwMissionID) || admiralID) && (bAllShips || MGlobals::AreAllies(
                                                            playerID, MGlobals::GetThisPlayer()))) {
            Vector point;
            S32 x, y;

            point.x = 0;
            point.y = box[2] + 250.0;
            point.z = 0;

            CAMERA->PointToScreen(point, &x, &y, &transform);
            PANE *pane = CAMERA->GetPane();

            COLORREF color = COLORTABLE[MGlobals::GetColorID(playerID)];

            DA::LineDraw(pane, x - 30, y, x - 21, y, color);
            DA::LineDraw(pane, x - 30, y, x - 30, y + 5, color);
            DA::LineDraw(pane, x - 30, y + 5, x - 21, y + 5, color);
            DA::LineDraw(pane, x - 22, y, x - 22, y + 5, color);
            DA::LineDraw(pane, x - 26, y, x - 26, y + 5, color);

            if (admiralID) {
                DA::LineDraw(pane, x - 32, y - 2, x - 19, y - 2, color);
                DA::LineDraw(pane, x - 32, y - 2, x - 32, y + 7, color);
                DA::LineDraw(pane, x - 32, y + 7, x - 19, y + 7, color);
                DA::LineDraw(pane, x - 20, y - 2, x - 20, y + 7, color);
            }
        }

        // draw the control group ID and/or the fleet ID
        //	if (bSelected && controlGroupID > 0)
        //	{
        if (bSelected && (controlGroupID || fleetID)) {
            wchar_t buffer[64];
            S32 x, y;

            if (controlGroupID && fleetID) {
                // case one, both groupID and fleetID
                buffer[0] = (controlGroupID > 9) ? '0' : (controlGroupID + '0');
                buffer[1] = ' ';

                // get the admiral's f-key
                VOLPTR(IAdmiral) admiral;
                admiral = OBJLIST->FindObject(fleetID);
                CQASSERT(admiral);
                U32 hotkey = admiral->GetAdmiralHotkey();

                buffer[2] = 0;

                if (hotkey)
                    get_hotkey_text(hotkey + IDH_FLEET_GROUP_1 - 1, buffer + 2, sizeof(buffer) - (2 * 2));
            } else if (controlGroupID) {
                // case two, no fleet ID but have a control group ID
                buffer[0] = (controlGroupID > 9) ? '0' : (controlGroupID + '0');
                buffer[1] = 0;
            } else if (fleetID) {
                // only have a fleetID
                // get the admiral's f-key
                VOLPTR(IAdmiral) admiral;
                admiral = OBJLIST->FindObject(fleetID);
                CQASSERT(admiral);
                U32 hotkey = admiral->GetAdmiralHotkey();

                buffer[0] = 0;

                if (hotkey)
                    get_hotkey_text(hotkey + IDH_FLEET_GROUP_1 - 1, buffer, sizeof(buffer));
            }

            // the following code stolen from TObjSelect.h
            int TBARLENGTH = 100;
            if (hullPointsMax < 1000) {
                if (hullPointsMax < 100) {
                    if (hullPointsMax > 0)
                        TBARLENGTH = 20;
                    else {
                        // no hull points, length should be decided by supplies
                        // use same length as max supplies
                    }
                } else // hullMax >= 100
                {
                    TBARLENGTH = 20 + (((hullPointsMax - 100) * 80) / (1000 - 100));
                }
            }
            TBARLENGTH = IDEAL2REALX(TBARLENGTH);

            Vector point;
            point.x = 0;
            point.y = H2 + 250.0;
            point.z = 0;

            CAMERA->PointToScreen(point, &x, &y, &transform);
            PANE *pane = CAMERA->GetPane();

            COLORREF color = RGB(200, 200, 200); //COLORTABLE[MGlobals::GetColorID(playerID)];

            int xpos = x - (TBARLENGTH / 2);

            COMPTR<IFontDrawAgent> pFont;
            if (OBJLIST->GetUnitFont(pFont.addr()) == GR_OK) {
                pFont->SetFontColor(color | 0xFF000000, 0);
                pFont->StringDraw(pane, xpos - 8, y - 2, buffer);
            }
        }
    }

    virtual void Render(void) {
        if (bVisible)
        {
            FRAME_preRender();

            FRAME_render();

            FRAME_postRender();
        }
    }

    virtual void MapRender(bool bPing) {
        if(IsVisibleToPlayer(MGlobals::GetThisPlayer()) || DEFAULTS->GetDefaults()->bEditorMode || bPing || DEFAULTS->GetDefaults()->bSpectatorModeOn)
        {
            U32 apPlayerID = playerID;

            if(objMapNode)
                apPlayerID = OBJMAP->GetApparentPlayerID(objMapNode,MGlobals::GetAllyMask(MGlobals::GetThisPlayer()));

            COLORREF color;
            if(bSelected)
                color = RGB(255,255,255);
            else
                color = COLORTABLE[MGlobals::GetColorID(apPlayerID)];

            if(isHalfSquare())
            {
                SYSMAP->DrawCircle(GetPosition(),GRIDSIZE/2,color);
                /*			if( shipMapTex!= -1)
                                SYSMAP->DrawPlayerIcon(GetPosition(),GRIDSIZE,shipMapTex,playerID);
                            else
                                SYSMAP->DrawSquare(GetPosition(),GRIDSIZE,COLORTABLE[MGlobals::GetColorID(playerID)]);
                */		}
            else
            {
                SYSMAP->DrawCircle(GetPosition(),GRIDSIZE,color);
                /*			if( shipMapTex!= -1)
                                SYSMAP->DrawPlayerIcon(GetPosition(),GRIDSIZE*2,shipMapTex,playerID);
                            else
                                SYSMAP->DrawSquare(GetPosition(),GRIDSIZE*2,COLORTABLE[MGlobals::GetColorID(playerID)]);
                */		}
        }
    }

    virtual void View(void) {
        SPACESHIP_VIEW view;
        BASIC_INSTANCE data;
        Vector vec;

        memset(&data, 0, sizeof(data));
        view.rtData = &data;
        view.mission = this;
        view.gamma = 0;
        view.contrast = 1;
        view.shipData.nothing = getViewStruct();

        vec = transform.translation;
        memcpy(&data.position, &vec, sizeof(data.position));

        vec = ang_velocity;
        memcpy(&data.rotation, &vec, sizeof(data.rotation));

        if (DEFAULTS->GetUserData("SPACESHIP_VIEW", view.mission->partName, &view, sizeof(view)))
        {
            memcpy(&vec, &data.position, sizeof(data.position));
            ENGINE->set_position(instanceIndex, vec);

            memcpy(&vec, &data.rotation, sizeof(data.rotation));
            ang_velocity = vec;

            bool bReg;
            S32 texCnt = 0;
            U32 textureID[5];
            Mesh *cmesh = REND->get_unique_instance_mesh(instanceIndex);
            int i;
            for (i=0;i<cmesh->material_cnt;i++)
            {
                bReg=0;
                for (int j=0;j<5;j++)
                {
                    if (textureID[j] == cmesh->material_list[i].texture_id)
                    {
                        bReg = TRUE;
                    }
                }

                if (!bReg)
                {
                    textureID[texCnt++] = cmesh->material_list[i].texture_id;
                }
            }

            for (i=0;i<texCnt;i++)
            {
                FreakTexture(textureID[i],view.gamma,view.contrast);
            }

            if (dwMissionID != ((dwMissionID & ~0xF) | playerID))	// playerID has changed
            {
                OBJLIST->RemovePartID(this, dwMissionID);
                dwMissionID = (dwMissionID & ~0xF) | playerID;
                OBJLIST->AddPartID(this, dwMissionID);
            }
        }
    }

    void DEBUG_print(void) const override {
#if ((defined(_JASON) || defined(_ROB)) && defined(_DEBUG))
        if (const_cast<const IBaseObject *>(OBJLIST->GetSelectedList()) == this)
        {
            C8  buffer[256];
            SINGLE yaw, roll, pitch;		// used only at runtime
            char * moveActive = (isMoveActive()) ? "MOVE" : "";
            char * toohigh = (getRollTooHigh()) ? "TOHIGH" : "";


            yaw   = transform.get_yaw() * MUL_RAD_TO_DEG;
            roll  = transform.get_roll() * MUL_RAD_TO_DEG;
            pitch = transform.get_pitch() * MUL_RAD_TO_DEG;

            sprintf(buffer,"yaw: %3.1f, roll: %3.1f, pitch: %3.1f, pos.z: %3.1f, %s, %s", yaw, roll, pitch, transform.get_position().z,
                moveActive, toohigh);

            DEBUGFONT->StringDraw(0, 8, 20, buffer);
        }
#endif
#if 0
        const char * debugName;
        if (IsVisibleToPlayer(MGlobals::GetThisPlayer()) || DEFAULTS->GetDefaults()->bVisibilityRulesOff)
            if (bHighlight && (debugName = GetDebugName()) != 0)
            {
                Vector point;
                S32 x, y;

                point.x = 0;
                point.y = box[2]+250.0F;	// maxY + 250
                point.z = 0;

                if (CAMERA->PointToScreen(point, &x, &y, &transform)==IN_PANE)
                {
                    DEBUGFONT->StringDraw(0, x+20, y, debugName, (THEMATRIX->IsMaster()) ? RGB_LOCAL : RGB_SHADOW);
                }
            }
#endif
    }

    // call the FogOfWar manager, if appropriate
    void RevealFog(const U32 currentSystem) override {
        if (systemID==currentSystem && bReady && MGlobals::AreAllies(playerID, MGlobals::GetThisPlayer()))
        {
            SINGLE admiralSensorMod = 1.0;
            if(fleetID && supplies)
            {
                VOLPTR(IAdmiral) flagship;
                OBJLIST->FindObject(fleetID,TOTALLYVOLATILEPTR,flagship,IAdmiralID);
                if(flagship.Ptr())
                {
                    MPart part(this);
                    admiralSensorMod = 1+flagship->GetSensorBonus(mObjClass,part.pInit->armorData.myArmor);
                }
            }
            SINGLE bonus = fieldFlags.getSensorDampingMod()*effectFlags.getSensorDampingMod()*admiralSensorMod*SECTOR->GetSectorEffects(playerID,systemID)->getSensorMod();
            FOGOFWAR->RevealZone(this, __max(0.75,sensorRadius*bonus), cloakedSensorRadius *bonus);
        }
    }
    // set visible flags of objects nearby
    virtual void CastVisibleArea(void) {
        if ((systemID & HYPER_SYSTEM_MASK)==0)
        {
            const U32 mask = MGlobals::GetAllyMask(playerID);
            SetVisibleToAllies(mask);

            if (bDerelict==false && IsVisibleToPlayer(MGlobals::GetThisPlayer()))
                SECTOR->AddShipToSystem(systemID, mask, playerID, mObjClass);

            SINGLE admiralSensorMod = 1.0;
            if(fleetID && supplies)
            {
                VOLPTR(IAdmiral) flagship;
                OBJLIST->FindObject(fleetID,TOTALLYVOLATILEPTR,flagship,IAdmiralID);
                if(flagship.Ptr())
                {
                    MPart part(this);
                    admiralSensorMod = 1+flagship->GetSensorBonus(mObjClass,part.pInit->armorData.myArmor);
                }
            }

            SINGLE bonus = fieldFlags.getSensorDampingMod()*effectFlags.getSensorDampingMod()*admiralSensorMod*SECTOR->GetSectorEffects(playerID,systemID)->getSensorMod();
            OBJLIST->CastVisibleArea(playerID, systemID, transform.translation,  fieldFlags,
                __max(0.75,sensorRadius*bonus),	cloakedSensorRadius*bonus);
        }
    }

    virtual void SetReady(bool _bReady) {
        bReady = _bReady;
    }

    virtual bool MatchesSomeFilter(DWORD filter) {
        return (frameInitInfo->pData->formationFilterBits & filter) != 0;
    }

    /* IGotoPos methods */

    virtual void GotoPosition(const struct GRIDVECTOR &pos, U32 agentID, bool bSlowMove);

    virtual void PrepareForJump(IBaseObject *jumpgate, bool bUserMove, U32 agentID, bool bSlowMove);

    virtual void UseJumpgate(IBaseObject *outgate, IBaseObject *ingate, const Vector &jumpToPosition, SINGLE heading,
                             SINGLE speed, U32 agentID);

    virtual bool IsJumping(void);

    virtual bool IsHalfSquare();

    virtual void Patrol(const GRIDVECTOR &src, const GRIDVECTOR &dst, U32 agentID);

    /* ISaveLoad methods */

    virtual BOOL32 Save(struct IFileSystem *file);

    virtual BOOL32 Load(struct IFileSystem *file);

    virtual void ResolveAssociations(void);

    /* IQuickSaveLoad methods */

    virtual void QuickSave(struct IFileSystem *file);

    virtual void QuickLoad(const char *szSaveLoadType, const char *szInstanceName, void *buffer, U32 bufferSize);

    virtual void QuickResolveAssociations(void) {
    }


    /* IExplosionOwner methods */

    virtual void RotateShip(SINGLE relYaw, SINGLE relRoll, SINGLE relPitch, SINGLE relAltitude) {
        TRANSFORM transform = ENGINE->get_transform(instanceIndex);

        rotateShip(relYaw, relRoll, relPitch);
        setAltitude(relAltitude);
    }

    void OnChildDeath(INSTANCE_INDEX child) override {}

    virtual U32 GetScrapValue(void) {
        return (bReady) ? pInitData->scrapValue : 0;	// no scrap for uncompleted objects
    }

    U32 GetFirstNuggetID(void) override {
        return firstNuggetID;
    }

    /* IMissionActor methods */

    virtual void InitActor(void) {
        //
        // reserve partID's for the nuggets + 1 ID for explosion
        //
        firstNuggetID = MGlobals::CreateSubordinatePartID();
        for (int i = 0; i < MAX_NUGGETS; i++)
            MGlobals::CreateSubordinatePartID(); // don't need to save the partID, because we know it's sequencial
    }

    // methods for accessing the "displayed" hullPoints/supplies
    virtual U32 GetDisplayHullPoints(void) const {
        return (displayHull >= 0) ? displayHull : hullPoints;
    }

    virtual U32 GetDisplaySupplies(void) const {
        return (displaySupplies >= 0) ? displaySupplies : supplies;
    }

    virtual void TakeoverSwitchID(U32 newMissionID) {
        // if we are part of a fleet, then we need to notify the admiral that we're about to be assaulted
        if (fleetID)
        {
            OBJPTR<IAdmiral> admiral;
            if (OBJLIST->FindObject(fleetID, TOTALLYVOLATILEPTR, admiral, IAdmiralID))
            {
                admiral->OnFleetShipTakeover(this);
            }
            else
            {
                fleetID = 0;
            }
        }

        ObjectEffectTarget::TakeoverSwitchID(newMissionID);
    }

    /* IWeaponTarget methods */

    //returns true if a shield hit was created
    virtual BOOL32 ApplyDamage(IBaseObject *collider, U32 ownerID, const Vector &pos, const Vector &dir, U32 amount,
                               BOOL32 bShieldHit = 1);

    //returns true if a shield hit was created
    virtual BOOL32 ApplyVisualDamage(IBaseObject *collider, U32 ownerID, const Vector &pos, const Vector &dir,
                                     U32 amount, BOOL32 bShieldHit = 1);

    // move "amount" from the pending pile to the actual. (assumes complex formula has already been used)
    virtual void ApplyAOEDamage(U32 ownerID, U32 damageAmount);

    /* SpaceShip methods */

    void initSpaceShip(const InitStruct &data);

    void explodeSpaceShip(bool bExplode);

    void renderSpaceShip(void);

    void drawBillboardShip(void);

    void updateDisplayValues(void);

    void updateOnce();

    void updateFieldInfo(void);

    void preTakeoverShip(U32 newMissionID, U32 troopID) {
        if(THEMATRIX->IsMaster())
        {
            BANKER->FreeCommandPt(playerID,pInitData->resourceCost.commandPt);
            BANKER->UseCommandPt(MGlobals::GetPlayerFromPartID(newMissionID),pInitData->resourceCost.commandPt);
        }
        // nothing to do here!?
    }

    U32 getSyncShipData(void *buffer);

    void putSyncShipData(void *buffer, U32 bufferSize, bool bLateDelivery);

    // override this in derived class, default behavior is to tell the nearest admiral that we are getting damaged
    virtual void broadcastHelpMsg(U32 attackerID);


    virtual const char *getSaveStructName(void) const // must be overriden implemented by derived class
    {
        return "SPACESHIP_SAVELOAD";
    }

    virtual void *getViewStruct(void) // must be overriden implemented by derived class
    {
        CQERROR0("getViewStruct() not be implemented!");
        return 0;
    }

    typedef void (IBaseObject::*InitProc2)(const InitStruct &initStruct);

    typedef void (IBaseObject::*SaveLoadProc2)(SaveStruct &saveStruct);

    InitProc castInitProc(InitProc2 _proc) {
        return reinterpret_cast<InitProc>(_proc);
    }

    SaveLoadProc castSaveLoadProc(SaveLoadProc2 _proc) {
        return reinterpret_cast<SaveLoadProc>(_proc);
    }

    //void FRAME_init (const INITINFO & initStruct)
    void FRAME_init(const InitStruct &_initStruct) {
        const BASESHIPINIT *initStruct = (const BASESHIPINIT *) (&_initStruct);
        ObjectFrame<IBaseObject, SPACESHIP_SAVELOAD, BASESHIPINIT>::FRAME_init(*initStruct);
    }
};

bool arch_callback(ARCHETYPE_INDEX parent_arch_index, ARCHETYPE_INDEX child_arch_index, void *user_data);

extern U32 shipMapTex;
extern U32 shipMapTexRef;

//-------------------------------------------------------------------------
// SpaceShip archetype data class, defined in DSpaceship.h
//-------------------------------------------------------------------------
//
template<class BT_TYPE>
bool SPACESHIP_INIT<BT_TYPE>::loadSpaceshipArchetype(BT_TYPE *_pData, PARCHETYPE _pArchetype) // load archetype data
{
    bool result = false;

    pData = _pData;
    pArchetype = _pArchetype;

    meshArch = MESHMAN->CreateMeshArch(_pData->fileName);

    if (pData->ambientEffect[0])
        ambientEffect = EFFECTPLAYER->LoadEffect(pData->ambientEffect);

    //try to determine the ship's footprint size for usage by ObjGen
    if (ENGINE->is_archetype_compound(archIndex)) {
        ENGINE->enumerate_archetype_parts(archIndex, arch_callback, &fp_radius);
    } else {
        float local_box[6];
        REND->get_archetype_bounding_box(archIndex, 1.0, local_box);
        fp_radius = __max(local_box[BBOX_MAX_X], -local_box[BBOX_MIN_X]);
        fp_radius = __max(fp_radius, local_box[BBOX_MAX_Z]);
        fp_radius = __max(fp_radius, -local_box[BBOX_MIN_Z]);
        fp_radius *= 2;
    }
    ///

    if (pData->explosionType[0]) {
        pExplosionType = ARCHLIST->LoadArchetype(pData->explosionType);
        CQASSERT(pExplosionType);
        ARCHLIST->AddRef(pExplosionType, OBJREFNAME);
    }

    pSparkBlast = ARCHLIST->LoadArchetype("BLAST!!Spark");
    if (pSparkBlast)
        ARCHLIST->AddRef(pSparkBlast, OBJREFNAME);

    /*	if (pData->shieldHitType[0])
        {
            pShieldHitType = ARCHLIST->LoadArchetype(pData->shieldHitType);
            if (pShieldHitType)
                ARCHLIST->AddRef(pShieldHitType);
        }*/
    if (pData->trailType[0]) {
        pTrailType = ARCHLIST->LoadArchetype(pData->trailType);
        if (pTrailType)
            ARCHLIST->AddRef(pTrailType, OBJREFNAME);
    }

    if (pData->damage.damageBlast[0]) {
        pDamageBlast = ARCHLIST->LoadArchetype(pData->damage.damageBlast);
        if (pDamageBlast)
            ARCHLIST->AddRef(pDamageBlast, OBJREFNAME);
    }

    if (pData->cloak.cloakEffectType[0]) {
        pCloakEffect = ARCHLIST->LoadArchetype(pData->cloak.cloakEffectType);
        if (pCloakEffect)
            ARCHLIST->AddRef(pCloakEffect, OBJREFNAME);
    } {
        DAFILEDESC fdesc = "tinnard_fire.anm";
        COMPTR<IFileSystem> objFile;
        //fdesc.lpFileName = objData->animName;
        if (OBJECTDIR->CreateInstance(&fdesc, objFile.void_addr()) == GR_OK) {
            damageAnimArch = ANIM2D->create_archetype(objFile);
        } else {
            CQFILENOTFOUND(fdesc.lpFileName);
            damageAnimArch = 0;
            return 0;
        }
    } {
        DAFILEDESC fdesc = pData->shield.animName;
        switch (pData->missionData.race) {
            case M_TERRAN:
                fdesc.lpFileName = "SH_Terran.anm";
                break;
            case M_MANTIS:
                fdesc.lpFileName = "SH_Mantis.anm";
                break;
            default:
            case M_SOLARIAN:
                fdesc.lpFileName = "SH_Celareon.anm";
                break;
        }

        COMPTR<IFileSystem> objFile;
        if (OBJECTDIR->CreateInstance(&fdesc, objFile.void_addr()) == GR_OK) {
            shieldAnimArch = ANIM2D->create_archetype(objFile);
        } else {
            CQFILENOTFOUND(fdesc.lpFileName);
            shieldAnimArch = 0;
            return 0;
        }
    } {
        DAFILEDESC fdesc = pData->shield.fizzAnimName;
        COMPTR<IFileSystem> objFile;
        if (OBJECTDIR->CreateInstance(&fdesc, objFile.void_addr()) == GR_OK) {
            shieldFizzAnimArch = ANIM2D->create_archetype(objFile);
        } else {
            CQFILENOTFOUND(fdesc.lpFileName);
            shieldFizzAnimArch = 0;
            return 0;
        }
    } {
        char outName[64];
        strcpy(outName, pData->fileName);
        strlwr(outName);
        char *extenpos = strstr(outName, ".3db");
        if (extenpos == 0)
            extenpos = strstr(outName, ".cmp");
        if (extenpos) {
            strcpy(extenpos, ".shield");
            DAFILEDESC fdesc = outName;
            COMPTR<IFileSystem> objFile;
            if (OBJECTDIR->CreateInstance(&fdesc, objFile.void_addr()) == GR_OK) {
                smesh = new SMesh;
                if (smesh->load(objFile)) {
                    /*					CollisionMesh *mesh = new CollisionMesh;
                                        smesh->MakeCollisionMesh(mesh);
                                        m_extent = new MeshExtent(mesh);
                                        m_extent->xform.set_identity();
                                        smesh->Sort(Vector(0,0,-1));*/
                } else {
                    delete smesh;
                    smesh = 0;
                }
            }
        }
    } {
        COMPTR<IFileSystem> file;
        DAFILEDESC fdesc = "smoke.pte";

        if (OBJECTDIR->CreateInstance(&fdesc, file.void_addr()) == GR_OK)
            smoke_archID = ENGINE->create_archetype("smoke.pte", file);
    }


    const char *fname;

    fname = pData->engineGlow.engine_texture_name;
    if (fname[0]) {
        engineTex = TMANAGER->CreateTextureFromFile(fname, TEXTURESDIR, DA::TGA, PF_4CC_DAA4);
    }

    if (pData->blinkers.light_script[0]) {
        blink_arch = CreateBlinkersArchetype(pData->blinkers.light_script, archIndex);
        fname = pData->blinkers.textureName;
        if (fname[0]) {
            blinkTex = TMANAGER->CreateTextureFromFile(fname, TEXTURESDIR, DA::TGA, PF_4CC_DAA4);
        }
    }

    fname = pData->cloak.cloakTex;
    if (fname[0]) {
        cloakTex = TMANAGER->CreateTextureFromFile(fname, TEXTURESDIR, DA::TGA, PF_4CC_DAA4);
    }

    fname = "comm.tga";
    if (fname[0]) {
        hiliteTex = TMANAGER->CreateTextureFromFile(fname, TEXTURESDIR, DA::TGA, PF_4CC_DAA4);
    }

    fname = pData->billboard.billboardTexName;
    if (fname[0]) {
        billboardTex = TMANAGER->CreateTextureFromFile(fname, TEXTURESDIR, DA::TGA, PF_4CC_DAA4);
    }

    //	if (cloakTex2 == 0)
    //		cloakTex2 = CreateTextureFromFile("Cloak2.tga",TEXTURESDIR,DA::TGA,PF_4CC_DAA4);

    switch (pData->missionData.race) {
        case M_MANTIS:
            fname = "dmg_mantis.tga";
            break;
        case M_SOLARIAN:
            fname = "dmg_solarian.tga";
            break;
        case M_TERRAN:
        default:
            fname = "dmg_terran.tga";
            break;
    }

    damageTexID = TMANAGER->CreateTextureFromFile(fname, TEXTURESDIR, DA::TGA, PF_4CC_DAA4);

    // preload some sound effectds

    SFXMANAGER->Preload(pData->shield.sfx);
    SFXMANAGER->Preload(pData->shield.fizzOut);

    if (shipMapTex == -1) {
        shipMapTex = SYSMAP->RegisterPlayerIcon("SysMap\\ship.bmp");
    }
    ++shipMapTexRef;

    result = true;

    return result;
}

//---------------------------------------------------------------------------
//------------------------End TSpaceShip.h-----------------------------------
//---------------------------------------------------------------------------

#endif
