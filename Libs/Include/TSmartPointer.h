#ifndef TSMARTPOINTER_H
#define TSMARTPOINTER_H
//--------------------------------------------------------------------------//
//                                                                          //
//                              TSmartPointer.H                             //
//                                                                          //
//--------------------------------------------------------------------------//
/*
    $Author: Rmarr $
*/			    
//---------------------------------------------------------------------------
/*
	Use a smart pointer in place of a raw component pointer.

 */

#ifndef DACOM_H
#include "DACOM.h"
#endif

#include <memory>

//-------------------------------------------------------------------
//-------------------------------------------------------------------
//-------------------------------------------------------------------
//
template <class Type>
class COMPTR
{
private:
	std::shared_ptr<Type> ptr;

public:
	COMPTR() : ptr(nullptr)
	{
	}

	COMPTR(Type* _ptr) : ptr(_ptr)
	{
	}

	COMPTR(const COMPTR<Type>& new_ptr) : ptr(new_ptr.ptr)
	{
	}

	// Move constructor for efficiency
	COMPTR(COMPTR<Type>&& new_ptr) noexcept : ptr(std::move(new_ptr.ptr))
	{
	}

	~COMPTR() = default;

	void free()
	{
		ptr.reset();
	}

	Type* operator=(Type* new_ptr)
	{
		ptr.reset(new_ptr);
		return ptr.get();
	}

	const COMPTR& operator=(const COMPTR<Type>& new_ptr)
	{
		ptr = new_ptr.ptr;
		return *this;
	}

	// Move assignment
	COMPTR& operator=(COMPTR<Type>&& new_ptr) noexcept
	{
		ptr = std::move(new_ptr.ptr);
		return *this;
	}

	// Safe accessor for output parameters
	// Usage: GetChildDocument("name", viewer.addr())
	Type** addr()
	{
		ptr.reset();
		return (Type**)&ptr;
	}

	// For APIs expecting void**
	void** void_addr()
	{
		ptr.reset();
		return (void**)&ptr;
	}

	operator Type*() const
	{
		return ptr.get();
	}

	Type* operator->() const
	{
		return ptr.get();
	}

	bool operator==(const int i) const
	{
		return (ptr.get() == (Type*)i);
	}

	bool operator!=(const int i) const
	{
		return (ptr.get() != (Type*)i);
	}

	bool operator==(Type* cmp) const
	{
		return (ptr.get() == cmp);
	}

	bool operator!=(Type* cmp) const
	{
		return (ptr.get() != cmp);
	}

	operator bool() const
	{
		return ptr != nullptr;
	}

	bool operator!() const
	{
		return ptr == nullptr;
	}
};

#endif
