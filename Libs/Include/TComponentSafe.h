#ifndef TCOMPONENT_SAFE_H
#define TCOMPONENT_SAFE_H

//--------------------------------------------------------------------------//
//                                                                          //
//                           TComponentSafe.h                               //
//                                                                          //
//                         COPYRIGHT (C) Wahur666                           //
//                                                                          //
//--------------------------------------------------------------------------//
/*
    $Author: Wahur666 $

    ------------------------------------------------------------------------
    Type-safe, aggregation-safe DA Component base infrastructure.
    ------------------------------------------------------------------------

    This header provides modernized replacements for the legacy DACOM
    component system, while remaining ABI-compatible with existing
    factories and descriptors.

    Key design goals:
      - No offset-based interface maps
      - No constructor-time registration
      - Explicit, debuggable interface wiring
      - Correct COM aggregation semantics
      - Safe for both aggregated and non-aggregated components

    ------------------------------------------------------------------------
    HOW TO USE
    ------------------------------------------------------------------------

    1) Inherit from DAComponentSafe<IDAComponent>

        struct MyComponent :
            IMyInterface,
            IOtherInterface,
            DAComponentSafe<IDAComponent>
        {
            ...
        };

    2) Implement FinalizeInterfaces()

        Register *all* COM interfaces here using RegisterInterface().
        This method MUST NOT be called from a constructor.

        Example:

            void FinalizeInterfaces()
            {
                RegisterInterface("IMyInterface", IID_IMyInterface,
                                  static_cast<IMyInterface*>(this));

                RegisterInterface("IOtherInterface", IID_IOtherInterface,
                                  static_cast<IOtherInterface*>(this));
            }

    3) Call FinalizeInterfaces() as the FIRST LINE of init()

        The init() method is guaranteed to be called after full object
        construction and aggregation wiring. This ensures correct
        dynamic dispatch and safe interface registration.

        Example:

            GENRESULT init(DACOMDESC* desc)
            {
                FinalizeInterfaces();
                ...
            }

    ------------------------------------------------------------------------
    AGGREGATION RULES (IMPORTANT)
    ------------------------------------------------------------------------

    - If the component is aggregated, all interfaces are registered
      on the *inner* component.
    - The outer component never exposes interfaces directly.
    - RegisterInterface() automatically forwards to the inner component
      when aggregation is active.
    - QueryInterface(), AddRef(), and Release() are correctly routed.

    ------------------------------------------------------------------------
    WHAT NOT TO DO
    ------------------------------------------------------------------------

    - Do NOT register interfaces in constructors
    - Do NOT rely on virtual dispatch during construction
    - Do NOT use macro-based interface maps
    - Do NOT mix interface names and interface IDs

    ------------------------------------------------------------------------
    RATIONALE
    ------------------------------------------------------------------------

    This design intentionally avoids ATL-style macro maps in favor of
    explicit, auditable C++ code. While more verbose, it is:

      - Easier to debug
      - Easier to reason about
      - Safer under aggregation
      - Resistant to subtle object-lifetime bugs

    This file exists because we are not animals.

*/
//--------------------------------------------------------------------------//


#include <vector>
#include <string>
#include <functional>
#include <typeinfo>

#ifndef DACOM_H
#include "DACOM.h"
#endif

#include "TComponent.h"


//--------------------------------------------------------------------------//
// New Type-Safe Interface Entry System
//--------------------------------------------------------------------------//

struct DaComInterfaceEntry {
    const char *interface_name;
    const char *interface_id;
    std::function<IDAComponent*()> getter; // Returns properly cast interface pointer
};

//--------------------------------------------------------------------------//
// Base for new-style components - keeps old system intact
//--------------------------------------------------------------------------//

template<class Type, class Base = IDAComponent>
struct DAComponentInnerSafe;

template<class Base>
struct DAComponentSafe : public Base {
    U32 ref_count;

    DAComponentInnerSafe<void> *delegate_inner = nullptr;

    std::vector<DaComInterfaceEntry> interface_registry;

    DAComponentSafe() : ref_count(1) {
    }

    virtual ~DAComponentSafe() = default;

    template<typename InterfaceType>
    void RegisterInterface(const char *name,
                           const char *iid,
                           InterfaceType *ptr) {
        if (delegate_inner) {
            delegate_inner->RegisterInterface(name, iid, ptr);
            return;
        }

        interface_registry.push_back({
            name,
            iid,
            [ptr]() -> IDAComponent * {
                return static_cast<IDAComponent *>(ptr);
            }
        });
    }

    void SetDelegateInner(void *inner) {
        delegate_inner = static_cast<DAComponentInnerSafe<void> *>(inner);
    }

    /* IDAComponent */

    DEFMETHOD(QueryInterface)(const C8 *name, void **out) override {
        if (!name || !out) return GR_INVALID_PARAM;

        for (auto &e: interface_registry) {
            if (strcmp(e.interface_name, name) == 0) {
                auto *r = e.getter();
                r->AddRef();
                *out = r;
                return GR_OK;
            }
        }

        *out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    DEFMETHOD(QueryInterface2)(const C8 *id, void **out) {
        if (!id || !out) return GR_INVALID_PARAM;

        for (auto &e: interface_registry) {
            if (strcmp(e.interface_id, id) == 0) {
                auto *r = e.getter();
                r->AddRef();
                *out = r;
                return GR_OK;
            }
        }

        *out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    DEFMETHOD_(U32, AddRef)(void) override { return ++ref_count; }

    DEFMETHOD_(U32, Release)(void) override {
        if (--ref_count == 0) {
            ref_count++;
            delete this;
            return 0;
        }
        return ref_count;
    }

    static const _DACOM_INTMAP_ENTRY * __stdcall _GetEntriesIn() {
        static const _DACOM_INTMAP_ENTRY e[] = {{nullptr, 0}};
        return e;
    }
};


//--------------------------------------------------------------------------//
// Debug version of Safe Component
//--------------------------------------------------------------------------//

template<class Base>
struct DADebugComponentSafe : public DAComponentSafe<Base> {
    DEFMETHOD_(U32, AddRef)(void) {
        U32 ret = DAComponentSafe<Base>::AddRef();
        GENERAL_TRACE_1(TempStr("AddRef %d\n", ret));
        return ret;
    }

    DEFMETHOD_(U32, Release)(void) {
        U32 ret = DAComponentSafe<Base>::Release();
        GENERAL_TRACE_1(TempStr("Release %d\n", ret));
        return ret;
    }
};

//--------------------------------------------------------------------------//
// Safe Component Factory Base
//--------------------------------------------------------------------------//

template<class ClassType, class DescType>
struct DACOM_NO_VTABLE DAComponentFactoryBaseSafe : public IComponentFactory {
    U32 ref_count;
    const char *className;

    DAComponentFactoryBaseSafe(const char *_className)
        : ref_count(1), className(_className) {
        if (className == nullptr)
            className = "IDAComponent";
    }

    virtual ~DAComponentFactoryBaseSafe(void) = default;

    DEFMETHOD(QueryInterface)(const C8 *interface_name, void **instance);

    DEFMETHOD_(U32, AddRef)(void);

    DEFMETHOD_(U32, Release)(void);
};

//--------------------------------------------------------------------------//

template<class ClassType, class DescType>
GENRESULT DAComponentFactoryBaseSafe<ClassType, DescType>::QueryInterface(const C8 *interface_name, void **instance) {
    if (!interface_name || !instance)
        return GR_INVALID_PARAM;

    *instance = nullptr;
    if (strcmp(interface_name, "IComponentFactory") != 0)
        return GR_INTERFACE_UNSUPPORTED;

    *instance = this;
    AddRef();
    return GR_OK;
}

template<class ClassType, class DescType>
U32 DAComponentFactoryBaseSafe<ClassType, DescType>::AddRef(void) {
    return ++ref_count;
}

template<class ClassType, class DescType>
U32 DAComponentFactoryBaseSafe<ClassType, DescType>::Release(void) {
    if (ref_count > 0)
        ref_count--;

    if (ref_count == 0) {
        ref_count++; // prevent infinite loops
        delete this;
        return 0;
    }

    return ref_count;
}

//--------------------------------------------------------------------------//
// Safe Aggregate Component Implementation
//--------------------------------------------------------------------------//

template<class Type, class Base>
struct DAComponentInnerSafe : public Base {
    U32 ref_count;
    Type *owner;
    std::vector<DaComInterfaceEntry> interface_registry;

    DAComponentInnerSafe(Type *_owner)
        : ref_count(1), owner(_owner) {
        // Don't call RegisterInterfaces here
    }

    virtual ~DAComponentInnerSafe(void) = default;

    DEFMETHOD(QueryInterface)(const C8 *interface_name, void **instance);

    DEFMETHOD_(U32, AddRef)(void);

    DEFMETHOD_(U32, Release)(void);

    // Legacy support for factories
    static const _DACOM_INTMAP_ENTRY * __stdcall _GetEntriesIn() {
        static const _DACOM_INTMAP_ENTRY _entries[] = {{nullptr, 0}};
        return _entries;
    }

    template<typename InterfaceType>
    void RegisterInterface(const char *iface_name, const char *iface_id, InterfaceType *ptr) {
        interface_registry.push_back({
            iface_name,
            iface_id,
            [ptr]() -> IDAComponent * {
                return static_cast<IDAComponent *>(ptr);
            }
        });
    }
};

template<class Type, class Base>
GENRESULT DAComponentInnerSafe<Type, Base>::QueryInterface(
    const C8 *interface_name, void **instance) {
    if (!interface_name || !instance)
        return GR_INVALID_PARAM;

    for (const auto &entry: interface_registry) {
        if (entry.interface_id &&
            strcmp(entry.interface_id, interface_name) == 0) {
            IDAComponent *result = entry.getter();
            if (result) {
                result->AddRef();
                *instance = result;
                return GR_OK;
            }
        }
    }

    *instance = nullptr;
    return GR_INTERFACE_UNSUPPORTED;
}

template<class Type, class Base>
U32 DAComponentInnerSafe<Type, Base>::AddRef(void) {
    return ++ref_count;
}

template<class Type, class Base>
U32 DAComponentInnerSafe<Type, Base>::Release(void) {
    if (ref_count > 0)
        ref_count--;

    if (ref_count == 0) {
        ref_count++; // prevent recursion
        delete owner;
        return 0;
    }

    return ref_count;
}

//--------------------------------------------------------------------------//
// Safe Aggregate Component - replaces DAComponentAggregate
//--------------------------------------------------------------------------//

template<class Base>
struct DAComponentAggregateSafe : public Base {
    IDAComponent *outerComponent;
    DAComponentInnerSafe<DAComponentAggregateSafe<Base> > innerComponent;

    DAComponentAggregateSafe(AGGDESC *desc)
        : innerComponent(this) {
        if ((outerComponent = desc->outer) == nullptr)
            outerComponent = &innerComponent;
        else if (desc->inner)
            *desc->inner = &innerComponent;

        static_cast<DAComponentAggregateSafe<Base> *>(this)
                ->SetDelegateInner(&innerComponent);
    }

    DAComponentAggregateSafe()
        : innerComponent(this), outerComponent(&innerComponent) {
    }

    virtual ~DAComponentAggregateSafe() = default;

    // Derived class override
    virtual void RegisterInterfaces() {
    }

    // QI always routed to inner
    DEFMETHOD(QueryInterface)(const C8 *n, void **i) override {
        return outerComponent->QueryInterface(n, i);
    }

    DEFMETHOD_(U32, AddRef)(void) override {
        return outerComponent->AddRef();
    }

    DEFMETHOD_(U32, Release)(void) override {
        return outerComponent->Release();
    }

    template<typename InterfaceType>
    void RegisterInterface(const char *name,
                           const char *iid,
                           InterfaceType *ptr) {
        innerComponent.RegisterInterface(name, iid, ptr);
    }

    static const _DACOM_INTMAP_ENTRY * __stdcall _GetEntriesIn() {
        static const _DACOM_INTMAP_ENTRY e[] = {{nullptr, 0}};
        return e;
    }
};

//--------------------------------------------------------------------------//
// Safe Component Factory - replaces DAComponentFactory
// For ClassType that has a parameterless constructor
//--------------------------------------------------------------------------//

template<class ClassType, class DescType>
struct DAComponentFactorySafe : public DAComponentFactoryBaseSafe<ClassType, DescType> {
    DAComponentFactorySafe(const char *_className)
        : DAComponentFactoryBaseSafe<ClassType, DescType>(_className) {
    }

    DEFMETHOD(CreateInstance)(DACOMDESC *descriptor, void **instance);
};

template<class ClassType, class DescType>
GENRESULT DAComponentFactorySafe<ClassType, DescType>::CreateInstance(DACOMDESC *descriptor, void **instance) {
    GENRESULT result = GR_OK;
    ClassType *pNewInstance = nullptr;
    DescType *lpDesc = (DescType *) descriptor;

    if (!descriptor || !instance) {
        if (instance)
            *instance = nullptr;
        return GR_INVALID_PARAM;
    }

    // Verify descriptor
    if ((lpDesc->size != sizeof(*lpDesc)) || strcmp(lpDesc->interface_name, this->className) != 0) {
        result = GR_INTERFACE_UNSUPPORTED;
        goto Done;
    }

    // Create instance
    pNewInstance = new ClassType();
    if (pNewInstance == nullptr) {
        result = GR_OUT_OF_MEMORY;
        goto Done;
    }

    // Initialize
    if ((result = pNewInstance->init(lpDesc)) != GR_OK) {
        delete pNewInstance;
        pNewInstance = nullptr;
    }

Done:
    *instance = pNewInstance;
    return result;
}

//--------------------------------------------------------------------------//
// Safe Component Factory 2 - replaces DAComponentFactory2
// For ClassType that requires a DescType parameter in constructor
//--------------------------------------------------------------------------//

template<class ClassType, class DescType>
struct DAComponentFactorySafe2 : public DAComponentFactoryBaseSafe<ClassType, DescType> {
    DAComponentFactorySafe2(const char *_className)
        : DAComponentFactoryBaseSafe<ClassType, DescType>(_className) {
    }

    DEFMETHOD(CreateInstance)(DACOMDESC *descriptor, void **instance);
};

template<class ClassType, class DescType>
GENRESULT DAComponentFactorySafe2<ClassType, DescType>::CreateInstance(DACOMDESC *descriptor, void **instance) {
    GENRESULT result = GR_OK;
    ClassType *pNewInstance = nullptr;
    DescType *lpDesc = (DescType *) descriptor;

    if (!descriptor || !instance) {
        if (instance)
            *instance = nullptr;
        return GR_INVALID_PARMS;
    }

    // Verify descriptor
    if ((lpDesc->size != sizeof(*lpDesc)) || strcmp(lpDesc->interface_name, this->className) != 0) {
        result = GR_INTERFACE_UNSUPPORTED;
        goto Done;
    }

    // Create instance with descriptor parameter
    pNewInstance = new ClassType(lpDesc);
    if (pNewInstance == nullptr) {
        result = GR_OUT_OF_MEMORY;
        goto Done;
    }

    // Initialize
    if ((result = pNewInstance->init(lpDesc)) != GR_OK) {
        if (lpDesc->inner)
            *(lpDesc->inner) = nullptr;
        delete pNewInstance;
        pNewInstance = nullptr;
    }

Done:
    *instance = pNewInstance;
    return result;
}

#endif // TCOMPONENT_SAFE_H
