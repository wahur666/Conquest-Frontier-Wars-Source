#ifndef CFW_REBUILD_VTDUMP_H
#define CFW_REBUILD_VTDUMP_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include "fdump.h"

/**
 * Dumps Vtable of object
 * Get the vtable with: `auto** vt = *(void***)obj;`
 * Link with "dbghelp"
 * @param vtable Vtable pointer
 * @param count Number of items to be listed
 * @param obj_name Debugger friendly name to find the output
 */
inline void dump_vtable_with_names(void** vtable, const int count, const char* obj_name) {
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, nullptr, TRUE);

    for (int i = 0; i < count; ++i) {
        DWORD64 addr = (DWORD64)vtable[i];
        char buffer[sizeof(SYMBOL_INFO) + 256] = {};
        PSYMBOL_INFO symbol = (PSYMBOL_INFO)buffer;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;

        if (SymFromAddr(process, addr, nullptr, symbol)) {
            FDUMP(ErrorCode(ERR_GENERAL, SEV_NOTICE),
                  "VTable[%d]: %s::%s at %p\n", i, obj_name, symbol->Name, vtable[i]);
        } else {
            FDUMP(ErrorCode(ERR_GENERAL, SEV_NOTICE),
                  "VTable[%d]: unknown at %p\n", i, vtable[i]);
        }
    }
}

#endif //CFW_REBUILD_VTDUMP_H