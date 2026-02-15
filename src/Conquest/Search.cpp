#include <string.h>
#include <math.h>

//--------------------------------------------------------------------------//
//                          SEARCH.C                                        //
// Converted from Search.asm - assembly utility functions to C              //
//--------------------------------------------------------------------------//

// Constants for angle calculations
static const double angle_neg90 = -1.5707963267948966;  // -90 degrees (-1.57 radians)
static const double angle_pos90 = 1.5707963267948966;   // 90 degrees (1.57 radians)

//--------------------------------------------------------------------------
// dwordsearch: Search for a DWORD value in a buffer
// Returns pointer to matching DWORD, or NULL if not found
//--------------------------------------------------------------------------
const unsigned long* dwordsearch(unsigned long len, unsigned long value, const unsigned long* buffer)
{
    if (len == 0)
        return NULL;

    for (unsigned long i = 0; i < len; i++)
    {
        if (buffer[i] == value)
            return &buffer[i];
    }

    return NULL;
}

//--------------------------------------------------------------------------
// unmemchr: Find first non-matching BYTE
// Returns pointer to first non-matching byte, or NULL if all match
//--------------------------------------------------------------------------
void* unmemchr(const void* ptr, int c, int size)
{
    const unsigned char* buffer = (const unsigned char*)ptr;
    unsigned char value = (unsigned char)c;

    for (int i = 0; i < size; i++)
    {
        if (buffer[i] != value)
            return (void*)&buffer[i];
    }

    return NULL;
}

//--------------------------------------------------------------------------
// clearmemFPU: Clear memory to zero using FPU (32-byte aligned)
//--------------------------------------------------------------------------
void clearmemFPU(void* ptr, int size)
{
    int num_32byte_blocks = size / 32;
    unsigned char* mem = (unsigned char*)ptr;

    if (num_32byte_blocks == 0)
        return;

    // Clear in 32-byte chunks
    for (int i = 0; i < num_32byte_blocks; i++)
    {
        memset(mem, 0, 32);
        mem += 32;
    }
}

//--------------------------------------------------------------------------
// get_angle (double version): Calculate atan2(x, y)
//--------------------------------------------------------------------------
double get_angle_double(const double* x, const double* y)
{
    if (*y == 0.0)
    {
        if (*x >= 0.0)
            return angle_pos90;   // 90 degrees
        else
            return angle_neg90;   // -90 degrees
    }

    return atan2(*x, *y);
}

//--------------------------------------------------------------------------
// get_angle (float version): Calculate atan2(x, y)
//--------------------------------------------------------------------------
float get_angle_float(float x, float y)
{
    if (y == 0.0f)
    {
        if (x >= 0.0f)
            return (float)angle_pos90;   // 90 degrees
        else
            return (float)angle_neg90;   // -90 degrees
    }

    return atan2f(x, y);
}

//--------------------------------------------------------------------------
// ftol: Convert float to long (replacement for standard C routine)
//--------------------------------------------------------------------------
long ftol(double d)
{
    long result = (long)d;
    double remainder = d - result;

    if (remainder > 0.0)
    {
        result++;
    }
    else if (remainder < 0.0)
    {
        result--;
    }

    return result;
}

//--------------------------------------------------------------------------
// rmemcpy: Reverse memory copy (copy from end to start)
//--------------------------------------------------------------------------
void* rmemcpy(void* dest, const void* src, int size)
{
    unsigned char* d = (unsigned char*)dest + size - 1;
    const unsigned char* s = (const unsigned char*)src + size - 1;

    for (int i = 0; i < size; i++)
    {
        *d-- = *s--;
    }

    return dest;
}

//--------------------------------------------------------------------------
//                        END Search.c
//--------------------------------------------------------------------------