//--------------------------------------------------------------------------//
//                                                                          //
//                               StringSet.cpp                               //
//                                                                          //
//                  COPYRIGHT (C) 2002 Fever Pitch Studios, INC.               //
//                                                                          //
//--------------------------------------------------------------------------//
/*
    $Author: Ajackson $
*/			    
//--------------------------------------------------------------------------//

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "IStringSet.h"

#include "HeapObj.h"
#include "IConnection.h"
#include "fdump.h"
#include "TComponent2.h"

#include <ctype.h>
#include <span>

#include <stdlib.h>

#include "Resource.h"

//--------------------------------------------------------------------------
//---------------------------StringSet class-------------------------------
//--------------------------------------------------------------------------
//
//--------------------------------------------------------------------------//
//
//--------------------------------------------------------------------------//
//
struct StringSet : public IStringSet
{
public:
	struct StringHolder
	{
		StringHolder * next;
		char * string;
		
		~StringHolder()
		{
			free(string);
		}
	};

	StringHolder * stringList;
	U32 dwRefs;

	IStringSetCallback * callStruct;
	//----------------------------------------------------------
	//----------------------------------------------------------

	StringSet (void)
	{
		stringList = NULL;
		dwRefs = 0;
		callStruct = NULL;
	}

	void * operator new (size_t size);		// calls calloc()

	~StringSet (void);

	/* IDAComponent members */

	virtual GENRESULT COMAPI QueryInterface (const C8 *interface_name, void **instance);
	virtual U32 COMAPI AddRef (void);
	virtual U32 COMAPI Release (void);

	/* IComponentFactory members */

	virtual GENRESULT COMAPI CreateInstance (DACOMDESC *descriptor, void **instance);

	/* IStringSet members */	
	virtual GENRESULT COMAPI AddString (const char * string);

	virtual GENRESULT COMAPI RemoveString (const char * string);

	virtual BOOL32 COMAPI HasString (const char * string);

	virtual GENRESULT COMAPI Clear (void) ;

	virtual GENRESULT COMAPI CallBack(const char * string);

	virtual GENRESULT COMAPI SetCallBack(IStringSetCallback * callback);

	virtual const char* COMAPI GetString(const char* lastString);

	// StringSet members

	GENRESULT init(STRINGSETDESC * desc);

	IDAComponent * BaseComponent (void)
	{
		return static_cast<IStringSet*>(this);
	}
	static IDAComponent* GetIStringSet(void* self) {
		return static_cast<IStringSet*>(self);
	}

	static std::span<const DACOMInterfaceEntry2> GetInterfaceMap() {
		static const DACOMInterfaceEntry2 map[] = {
			{"IStringSet", &GetIStringSet},
		};
		return map;
	}
};

//--------------------------------------------------------------------------
//
StringSet::~StringSet (void)
{
	while(stringList)
	{
		StringHolder * tmp = stringList;
		stringList = stringList->next;
		delete tmp;
	}
}
//--------------------------------------------------------------------------
//
GENRESULT StringSet::QueryInterface (const C8 *interface_name, void **instance)
{
	if (!interface_name || !instance)
		return GR_INVALID_PARAM;

	std::string_view requested{interface_name};

	for (const auto& e : GetInterfaceMap())
	{
		if (e.interface_name == requested)
		{
			IDAComponent* iface = e.get(this);
			iface->AddRef();
			*instance = iface;
			return GR_OK;
		}
	}

	*instance = nullptr;
	return GR_INTERFACE_UNSUPPORTED;
}
//--------------------------------------------------------------------------
//
U32 StringSet::AddRef (void)
{
	dwRefs++;
	return dwRefs;
}
//--------------------------------------------------------------------------
//
U32 StringSet::Release (void)
{
	if (dwRefs > 0)
		dwRefs--;
	if (dwRefs <= 1)
	{
		if (dwRefs == 0)
		{
 			dwRefs++;	// prevent infinite looping
			delete this;
			return 0;
		}
	}

	return dwRefs;
}
//--------------------------------------------------------------------------
//
GENRESULT StringSet::CreateInstance (DACOMDESC *, void **instance)
{
	*instance = 0;
	return GR_GENERIC;
}
//--------------------------------------------------------------------------
//
void * StringSet::operator new (size_t size)
{
	return calloc(size, 1);
}
//--------------------------------------------------------------------------
//
GENRESULT StringSet::AddString (const char * string)
{
	StringHolder * newString = new StringHolder;
	newString->next = stringList;
	newString->string = strdup(string);
	stringList = newString;
	return GR_OK;
}
//--------------------------------------------------------------------------
//
GENRESULT StringSet::RemoveString (const char * string)
{
	StringHolder * search = stringList;
	StringHolder * prev = NULL;
	while(search)
	{
		if(strcmp(string,search->string) == 0)
		{
			if(prev)
				prev->next = search->next;
			else
				stringList = search->next;
			delete search;
			return GR_OK;
		}
		search = search->next;
	}
	return GR_GENERIC;
}
//--------------------------------------------------------------------------
//
BOOL32 StringSet::HasString(const char * string)
{
	if(string[0] == 0)
		return true;
	StringHolder * search = stringList;
	while(search)
	{
		if(strcmp(string,search->string) == 0)
		{
			return true;
		}
		search = search->next;
	}
	return false;
}
//--------------------------------------------------------------------------//
//
GENRESULT COMAPI StringSet::Clear (void)
{
	while(stringList)
	{
		StringHolder * tmp = stringList;
		stringList = stringList->next;
		delete tmp;
	}
	return GR_OK;
}
//--------------------------------------------------------------------------//
//
GENRESULT COMAPI StringSet::CallBack(const char * string)
{
	if(string[0] == 0)
		return GR_GENERIC;
	if(callStruct)
	{
		StringHolder * search = stringList;
		while(search)
		{
			if(strcmp(string,search->string) == 0)
			{
				return callStruct->StringSetCallback(string);
			}
			search = search->next;
		}
	}
	return GR_GENERIC;
}
//--------------------------------------------------------------------------//
//
GENRESULT COMAPI StringSet::SetCallBack(IStringSetCallback * callback)
{
	callStruct = callback;
	return GR_OK;
}
//--------------------------------------------------------------------------//
//
const char* COMAPI StringSet::GetString(const char* lastString)
{
	if( !lastString )
	{
		return stringList->string;
	}
	else
	{
		StringHolder * search = stringList;
		while(search)
		{
			if(strcmp(lastString,search->string) == 0)
			{
				if( search->next )
				{
					return search->next->string;
				}
				else
					return NULL;
			}
			search = search->next;
		}
	}
	return NULL;
}
//--------------------------------------------------------------------------//
//
GENRESULT StringSet::init(STRINGSETDESC *)
{
	return GR_OK;
}
//--------------------------------------------------------------------------//
//
void RegisterStringSet (ICOManager * DACOM)
{
	IComponentFactory * doc;

	if ((doc = new DAComponentFactoryX<StringSet,STRINGSETDESC>("IStringSet")) != 0)
	{
		DACOM->RegisterComponent(doc, "IStringSet");
		doc->Release();
	}
}
//--------------------------------------------------------------------------//
//---------------------------END StringSet.cpp-------------------------------//
//--------------------------------------------------------------------------//
