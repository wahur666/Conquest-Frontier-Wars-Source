#ifndef TOBJECTX_H
#define TOBJECTX_H
//--------------------------------------------------------------------------//
//                                                                          //
//                               TObjectX.h                                 //
//                                                                          //
//               COPYRIGHT (C) 1997 BY DIGITAL ANVIL, INC.                  //
//                                                                          //
//--------------------------------------------------------------------------//
/*
   Modernized replacement for TObject.h

   Changes vs TObject.h:
   - Drops custom heap operator new/delete (MSHeap was a facade over malloc anyway)
   - Replaces raw offset arithmetic (_INTMAP_ENTRY + cqoffsetofclass) with
     typed function pointers, matching the pattern used in TComponent2.h
   - QueryInterface no longer needs InitObjectPointer offset arithmetic;
     get() already returns the correctly adjusted pointer
   - std::span replaces null-terminated array iteration
   - Macros kept for map declaration to preserve call-site compatibility,
     but now emit get() function pointers instead of raw offsets

   Usage example:

       struct MyObject : public IBaseObject, public IUnit, public ISelectable
       {
           static IBaseObject* GetIUnit(void* self) {
               return static_cast<IUnit*>(
                   static_cast<MyObject*>(self));   // always name the concrete type
           }
           static IBaseObject* GetISelectable(void* self) {
               return static_cast<ISelectable*>(
                   static_cast<MyObject*>(self));
           }

           BEGIN_CQMAP_INBOUND(MyObject)
               CQMAP_ENTRY(IUnitID,       &GetIUnit)
               CQMAP_ENTRY(ISelectableID, &GetISelectable)
           END_CQMAP()
       };

       typedef ObjectImplX<MyObject> MyObjectImpl;
*/
//--------------------------------------------------------------------------//

#ifndef IOBJECT_H
#include "IObject.h"
#endif

#include <span>

//--------------------------------------------------------------------------//
// Interface map entry
// get() replaces raw offset — returns the correctly adjusted base pointer
//--------------------------------------------------------------------------//

struct CQInterfaceEntry
{
    OBJID           objid;
    IBaseObject*    (*get)(void* self);
};

//--------------------------------------------------------------------------//
// Map declaration macros
//
// Replaces BEGIN_MAP_INBOUND / _INTERFACE_ENTRY / END_MAP
// Each CQMAP_ENTRY names a static get() function defined in the same class.
//
// CQMAP_ENTRY_AGGREGATE is for pointer members (held-by-pointer aggregates).
// The get() for those should dereference the member pointer, e.g.:
//
//   static IBaseObject* GetIFoo(void* self) {
//       return static_cast<ConcreteType*>(self)->m_pFoo;
//   }
//--------------------------------------------------------------------------//

#define BEGIN_CQMAP_INBOUND(x)                                              \
public:                                                                     \
    static std::span<const CQInterfaceEntry> _CQGetEntriesIn() {           \
        static const CQInterfaceEntry _entries[] = {

#define BEGIN_CQMAP_OUTBOUND(x)                                             \
public:                                                                     \
    static std::span<const CQInterfaceEntry> _CQGetEntriesOut() {          \
        static const CQInterfaceEntry _entries[] = {

#define CQMAP_ENTRY(objid, getfn)                                           \
        { objid, getfn },

#define END_CQMAP()                                                         \
        };                                                                  \
        return _entries;                                                    \
    }

//--------------------------------------------------------------------------//
// ObjectImplX
//
// Drop-in replacement for ObjectImpl<Base>.
// - No custom allocator (use standard new/delete; heap was malloc anyway)
// - QueryInterface walks the typed map and calls get() directly
//--------------------------------------------------------------------------//

#define ObjectImplX _CoiX

template <class Base>
struct ObjectImplX : public Base
{
    ObjectImplX(void) = default;

    virtual void* __fastcall QueryInterface(
        OBJID                   objid,
        OBJPTR<IBaseObject>&    pInterface,
        U32                     playerID);
};

//--------------------------------------------------------------------------//

template <class Base>
void* ObjectImplX<Base>::QueryInterface(
    OBJID                   objid,
    OBJPTR<IBaseObject>&    pInterface,
    U32                     playerID)
{
    for (const auto& e : Base::_CQGetEntriesIn())
    {
        if (e.objid == objid)
        {
            IBaseObject* iface = e.get(this);
            InitObjectPointer(pInterface, playerID, iface, 0);
            return pInterface.Ptr();
        }
    }

    pInterface.Null();
    return nullptr;
}

//--------------------------------------------------------------------------//
//---------------------------End TObjectX.h---------------------------------//
//--------------------------------------------------------------------------//
#endif
