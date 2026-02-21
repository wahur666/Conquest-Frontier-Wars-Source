#ifndef TCOMPONENT2_H
#define TCOMPONENT2_H
//--------------------------------------------------------------------------//
//                                                                          //
//                              TComponent.h                                //
//                                                                          //
//               COPYRIGHT (C) 1997 BY DIGITAL ANVIL, INC.                  //
//                                                                          //
//--------------------------------------------------------------------------//
/*
   $Author: Rmarr $

                       Template class for Base DA Components

*/
//--------------------------------------------------------------------------//

//==============================================================================
//
//  RULE — void* casts ALWAYS require the concrete type as an intermediate
//  -------------------------------------------------------------------------
//  When a pointer passes through void*, all type information is lost.
//  The compiler cannot compute base class offsets from void*.
//  Casting void* directly to a non-first base class gives the WRONG address.
//  It works by accident only when the target base is at offset zero (first base).
//
//  WRONG — works by accident if ISomething is the first base, silent bug otherwise:
//      static IDAComponent* GetISomething(void* self) {
//          return static_cast<ISomething*>(self);   // skips offset adjustment
//      }
//
//  CORRECT — always name the concrete type to force the right offset:
//      static IDAComponent* GetISomething(void* self) {
//          return static_cast<ISomething*>(         // step 2: to target base
//              static_cast<ConcreteType*>(self));   // step 1: void* -> concrete
//      }
//
//  Why it breaks without step 1:
//      struct Foo : public A, public B {};
//      // A is at offset 0 inside Foo  — cast from void* to A* works by accident
//      // B is at offset N inside Foo  — cast from void* to B* returns wrong address
//      // static_cast<Foo*>(self) tells the compiler the real layout, both work
//
//  Decision flowchart:
//      Is the pointer currently void*?
//          YES -> cast to ConcreteType* first, THEN to the target base
//          NO  -> cast directly, compiler already knows the layout
//
//  Copy-paste template for every interface map get() function:
//      static IDAComponent* GetIFoo(void* self) {
//          return static_cast<IFoo*>(
//              static_cast<ConcreteType*>(self));  // always name the concrete type
//      }
//
//==============================================================================
//

/*
    static IDAComponent* GetIFoo(void* self) {
         return static_cast<IFoo*>(
             static_cast<ConcreteType*>(self));
    }
	static std::span<const DACOMInterfaceEntry2> GetInterfaceMap() {
		static const DACOMInterfaceEntry2 map[] = {
			{"IFoo", &GetIFoo},
		};
		return map;
	}
*/

#include <vector>
#include <string>
#include "TComponentx.h"
#include "TempStr.h"

//----------------------------------//

template <class Base> struct DAComponentX : public Base
{
	U32 ref_count;

	
	DAComponentX (void)
	{
		ref_count=1;
	}

	/* IDAComponent methods */

	DEFMETHOD(QueryInterface) (const C8 *name, void **out);
	DEFMETHOD_(U32,AddRef)           (void);
	DEFMETHOD_(U32,Release)          (void);
};

//--------------------------------------------------------------------------//
//
template <class Base>
GENRESULT DAComponentX<Base>::QueryInterface(
	const C8* name,
	void** out)
{
	if (!name || !out)
		return GR_INVALID_PARAM;

	std::string_view requested{name};

	for (const auto& e : Base::GetInterfaceMap())
	{
		if (e.interface_name == requested)
		{
			IDAComponent* iface = e.get(this);
			iface->AddRef();
			*out = iface;
			return GR_OK;
		}
	}

	*out = nullptr;
	return GR_INTERFACE_UNSUPPORTED;
}
//--------------------------------------------------------------------------//
//
template <class Base>
U32 DAComponentX< Base >::AddRef (void)
{
	ref_count++;
	return ref_count;
}
//--------------------------------------------------------------------------//
//
template <class Base>
U32 DAComponentX< Base >::Release (void)
{
	if (ref_count > 0)
		ref_count--;

	if (ref_count == 0)
	{
		ref_count++;		// artificially add reference to prevent infinite loops
		delete this;
		return 0;
	}

	return ref_count;
}


//--------------------------------------------------------------------------//
//----------------Inner Component Implementation (for Aggregation)----------//
//--------------------------------------------------------------------------//
//

template <class Type, class Base=IDAComponent>
struct DAComponentInnerX : public Base
{
	U32 ref_count;
	Type * owner;

	DAComponentInnerX (Type * _owner)
	{
		ref_count=1;
		owner = _owner;
	}

	/* IDAComponent methods */

	DEFMETHOD(QueryInterface) (const C8 *name, void **instance);
	DEFMETHOD_(U32,AddRef)           (void);
	DEFMETHOD_(U32,Release)          (void);
};

//--------------------------------------------------------------------------//
//
template <class Type, class Base>
GENRESULT DAComponentInnerX<Type, Base>::QueryInterface(
	const C8* name,
	void** instance)
{
	if (!name || !instance)
		return GR_INVALID_PARAM;

	std::string_view requested{name};

	for (const auto& e : owner->GetInterfaceMap())
	{
		if (e.interface_name == requested)
		{
			auto* iface = e.get(owner);
			iface->AddRef();
			*instance = iface;
			return GR_OK;
		}
	}

	*instance = nullptr;
	return GR_INTERFACE_UNSUPPORTED;
}
//--------------------------------------------------------------------------//
//
template <class Type, class Base>
U32 DAComponentInnerX< Type, Base >::AddRef (void)
{
	ref_count++;
	return ref_count;
}
//--------------------------------------------------------------------------//
//
template <class Type, class Base>
U32 DAComponentInnerX< Type, Base >::Release (void)
{
	if (ref_count > 0)
		ref_count--;

	if (ref_count == 0)
	{
		ref_count++;		// artificially add reference to prevent infinite loops
		delete owner;
		return 0;
	}

	return ref_count;
}

//--------------------------------------------------------------------------//
//--------------------------Aggregate implementation------------------------//
//--------------------------------------------------------------------------//
//

template <class Base>
struct DAComponentAggregateX : public Base
{
	IDAComponent *outerComponent;
	DAComponentInnerX<Base> innerComponent;


	DAComponentAggregateX (struct AGGDESC * desc) : innerComponent(this)
	{
		if ((outerComponent = desc->outer) == 0)
			outerComponent = &innerComponent;
		else
			*(desc->inner) = &innerComponent;
	}

	/* IDAComponent methods */

	DEFMETHOD(QueryInterface) (const C8 *interface_name, void **instance);
	DEFMETHOD_(U32,AddRef)           (void);
	DEFMETHOD_(U32,Release)          (void);
};

//--------------------------------------------------------------------------//
//
template <class Base>
GENRESULT DAComponentAggregateX< Base >::QueryInterface (const C8 *interface_name, void **instance)
{
	return outerComponent->QueryInterface(interface_name, instance);
}
//--------------------------------------------------------------------------//
//
template <class Base>
U32 DAComponentAggregateX< Base >::AddRef (void)
{
	return outerComponent->AddRef();
}
//--------------------------------------------------------------------------//
//
template <class Base>
U32 DAComponentAggregateX< Base >::Release (void)
{
	return outerComponent->Release();
}

//--------------------------------------------------------------------------//
//-------------------Component Factory Implementation-----------------------//
//--------------------------------------------------------------------------//
//
template <class ClassType, class DescType>
struct DACOM_NO_VTABLE DAComponentFactoryBaseX : public IComponentFactory
{
	U32 ref_count;
	std::string_view className;

	DAComponentFactoryBaseX (std::string_view _className): ref_count(1)
	{
		if (!_className.empty())
			className = _className;
		else
			className = ClassType::GetInterfaceMap().front().interface_name;
	}

	/* IDAComponent methods */

	DEFMETHOD(QueryInterface) (const C8 *interface_name, void **instance);
	DEFMETHOD_(U32,AddRef)           (void);
	DEFMETHOD_(U32,Release)          (void);
};
//--------------------------------------------------------------------------//
//
template <class ClassType, class DescType>
GENRESULT DAComponentFactoryBaseX<ClassType,DescType>::QueryInterface (const C8 *interface_name, void **instance)
{
	*instance = 0;
	if (strcmp(interface_name, "IComponentFactory") != 0)
		return GR_INTERFACE_UNSUPPORTED;

	*instance = this;
	AddRef();
	return GR_OK;
}
//--------------------------------------------------------------------------//
//
template <class ClassType, class DescType>
U32 DAComponentFactoryBaseX<ClassType,DescType>::AddRef (void)
{
	ref_count++;
	return ref_count;
}
//--------------------------------------------------------------------------//
//
template <class ClassType, class DescType>
U32 DAComponentFactoryBaseX<ClassType,DescType>::Release (void)
{
	if (ref_count > 0)
		ref_count--;
	if (ref_count == 0)
	{
		ref_count++;		// artificially add reference to prevent infinite loops
		delete this;
		return 0;
	}
	return ref_count;
}
//--------------------------------------------------------------------------//
//
template <class ClassType, class DescType>
struct DAComponentFactoryX : public DAComponentFactoryBaseX<ClassType,DescType>
{
	DAComponentFactoryX (const char * _className) : DAComponentFactoryBaseX<ClassType,DescType>(_className)
	{
	}

	/* IComponentFactory methods */

	DEFMETHOD(CreateInstance) (DACOMDESC *descriptor, void **instance);
};
//--------------------------------------------------------------------------//
//
template <class ClassType, class DescType>
GENRESULT DAComponentFactoryX<ClassType,DescType>::CreateInstance (DACOMDESC *descriptor, void **instance)
{
	GENRESULT result = GR_OK;
	ClassType *pNewInstance = 0;
	DescType *lpDesc = (DescType *) descriptor;

	//
	// If unsupported interface requested, fail call
	//

	if ((lpDesc->size != sizeof(*lpDesc)) || std::string_view(lpDesc->interface_name) != this->className)
	{
		result = GR_INTERFACE_UNSUPPORTED;
		goto Done;
	}

	//
	// Create an instance of ClassType
	//

	if ((pNewInstance = new ClassType) == 0)
	{
		result = GR_OUT_OF_MEMORY;
		goto Done;
	}

	if ((result = pNewInstance->init(lpDesc)) != GR_OK)
	{
		//
		// initialization failed!
		//
		delete pNewInstance;
		pNewInstance = 0;
	}
Done:
	*instance = pNewInstance;
	return result;
}
//--------------------------------------------------------------------------//
//-------------------Component Factory Implementation 2---------------------//
//--------------------------------------------------------------------------//
//
template <class ClassType, class DescType>
struct DAComponentFactoryX2 : public DAComponentFactoryBaseX<ClassType,DescType>
{
	DAComponentFactoryX2 (const char * _className) : DAComponentFactoryBaseX<ClassType,DescType>(_className)
	{
	}


	/* IComponentFactory methods */

	DEFMETHOD(CreateInstance) (DACOMDESC *descriptor, void **instance);
};
//--------------------------------------------------------------------------//
//
template <class ClassType, class DescType>
GENRESULT DAComponentFactoryX2<ClassType,DescType>::CreateInstance (DACOMDESC *descriptor, void **instance)
{
	GENRESULT result = GR_OK;
	ClassType *pNewInstance = 0;
	DescType *lpDesc = (DescType *) descriptor;

	//
	// If unsupported interface requested, fail call
	//

	if ((lpDesc->size != sizeof(*lpDesc)) || std::string_view(lpDesc->interface_name) != this->className)
	{
		result = GR_INTERFACE_UNSUPPORTED;
		goto Done;
	}

	//
	// Create an instance of ClassType
	//

	if ((pNewInstance = new ClassType(lpDesc)) == 0)
	{
		result = GR_OUT_OF_MEMORY;
		goto Done;
	}

	if ((result = pNewInstance->init(lpDesc)) != GR_OK)
	{
		if (lpDesc->inner)
			*(lpDesc->inner) = 0;		// reset it to NULL
		//
		// initialization failed!
		//
		delete pNewInstance;
		pNewInstance = 0;
	}
Done:
	*instance = pNewInstance;
	return result;
}

#pragma warning( pop )

//--------------------------------------------------------------------------//
//-------------------------End TComponent.h---------------------------------//
//--------------------------------------------------------------------------//
#endif