// da_heap_utility.h
//

#ifndef __da_heap_utility_h__
#define __da_heap_utility_h__

#include <malloc.h>

#define DA_HEAP_MARK_ALLOCATED(heapptr) 
#define DA_HEAP_PRINT(heapptr) 

#define DA_HEAP_ACQUIRE_HEAP(dstptr) 

// NOTE: the clearing behavior of the new operator is necessary
// NOTE: for some components, hence let it in there.
//
#define DA_HEAP_DEFINE_NEW_OPERATOR(classname)	\
	inline void * classname::operator new( size_t size ) { return calloc( size, 1 ); } \
	inline void classname::operator delete( void *ptr ) { ::free( ptr ); }

#define DA_HEAP_DEFINE_HEAP_MESSAGE(hinstance) 

#define DA_HEAP_MALLOC( heap, size, tag )	malloc( (size) )

#define DA_HEAP_CALLOC( heap, size, tag )	calloc( (size),


#endif // EOF
