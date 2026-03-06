//--------------------------------------------------------------------------//
//                                                                          //
//                              DumpView.cpp                                //
//                                                                          //
//               COPYRIGHT (C) 1998 BY DIGITAL ANVIL, INC.                  //
//                                                                          //
//--------------------------------------------------------------------------//
/*
   $Header: /Conquest/App/Src/DumpView.cpp 48    11/17/00 2:07p Jasony $
*/
//--------------------------------------------------------------------------//

#include "pch.h"
#include <globals.h>

#include "Startup.h"
#include "CQTrace.h"
#include "UserDefaults.h"
#include "DBHotkeys.h"

#include <EventSys2.h>
#include "Resource.h"
#include <HeapObj.h>
#include <TComponent.h>
#include <WindowManager.h>
#include <FileSys.h>
#include <TSmartPointer.h>

#include <assert.h>
#include <stdlib.h>
#include <comdef.h>
#include <richedit.h>
#include <stdio.h>

//--------------------------------------------------------------------------//

struct DumpView : GlobalComponent, IDAComponent
{
    void *operator new(size_t size)  { return calloc(size, 1); }
    void  operator delete(void *ptr) { ::free(ptr); }

    HINSTANCE      hRichLibrary;
    IDAComponent * _this;

    HWND hDlg, hEdit;
    LONG totalBytesSent;

    const char * buffer;
    LONG         bufferSize;

    COMPTR<IFileSystem> dumpFile;

    /* GlobalComponent methods */

    virtual void Startup(void)
    {
        if (CQFLAGS.bDumpWindow)
            hDlg = CreateDialogParam(hResource, MAKEINTRESOURCE(IDD_DIALOG8), hMainWindow,
                                     DLGPROC(_dlgProc), (LPARAM)this);
        _this = this;
    }

    virtual void Initialize(void)
    {
        if (CQFLAGS.bDumpFile)
        {
            DAFILEDESC fdesc = "CQDump.txt";
            fdesc.dwDesiredAccess       |= GENERIC_WRITE;
            fdesc.dwCreationDistribution = CREATE_ALWAYS;
            fdesc.lpImplementation       = "DOS";

            if (DACOM->CreateInstance(&fdesc, dumpFile.void_addr()) == GR_OK)
            {
            }
        }

        char buf[256];
        getVersion(buf, sizeof(buf));
        CQTRACE11("Build Version %s", buf);
    }

    /* IDAComponent methods */

    DEFMETHOD(QueryInterface)(const C8 *interface_name, void **instance)
    {
        *instance = 0;
        return GR_GENERIC;
    }

    DEFMETHOD_(U32, AddRef)(void)  { return 1; }

    DEFMETHOD_(U32, Release)(void)
    {
        WINDOWPLACEMENT loadStruct;
        loadStruct.length = sizeof(loadStruct);

        if (hDlg && GetWindowPlacement(hDlg, &loadStruct))
            DEFAULTS->SetDataInRegistry("dumpDialog", &loadStruct, sizeof(loadStruct));

        dumpFile.free();
        return 1;
    }

    /* DumpView methods */

    DumpView(void)
    {
        hRichLibrary = ::LoadLibrary("RICHED32.DLL");
        FDUMP        = STANDARD_DUMP;
        HEAP_Acquire()->SetErrorHandler(STANDARD_DUMP);
    }

    ~DumpView(void);

    BOOL dlgProc(HWND hwnd, UINT message, UINT wParam, LONG lParam);

    bool sendData(const char *buffer, DWORD length, DWORD dwFlags = SF_TEXT | SFF_SELECTION);

    // x64: static + DWORD_PTR cookie so 'this' is recovered inside the callback.
    // A non-static member function pointer cannot satisfy a C callback signature —
    // the old code used inline asm to force the assignment and would have
    // truncated the 'this' pointer to 32 bits via the DWORD dwCookie field.
    static DWORD CALLBACK editStreamCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb);

    static int __cdecl    STANDARD_DUMP(ErrorCode code, const C8 *fmt, ...);
    static LONG_PTR CALLBACK _dlgProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static void getVersion(char *buffer, U32 bufferSize);
};

static struct DumpView view;

//--------------------------------------------------------------------------//

DumpView::~DumpView(void)
{
    if (hRichLibrary)
        ::FreeLibrary(hRichLibrary);
    hRichLibrary = 0;
}

//--------------------------------------------------------------------------//

void DumpView::getVersion(char *buffer, U32 bufferSize)
{
    char   mname[MAX_PATH + 4];
    char * tmp;
    DWORD  dwHandle, dwSize;
    void * versionBuffer = 0;
    UINT   numChars;
    void * pString = 0;

    *buffer = 0;

    ::GetModuleFileName(0, mname, sizeof(mname));
    if ((tmp = strrchr(mname, '\\')) == 0)
        goto Done;
    tmp++;

    if ((dwSize = GetFileVersionInfoSize(tmp, &dwHandle)) == 0)
        goto Done;

    versionBuffer = malloc(dwSize);

    if (GetFileVersionInfo(tmp, dwHandle, dwSize, versionBuffer) == 0)
        goto Done;

    if (VerQueryValue(versionBuffer, const_cast<char *>(_localLoadString(IDS_BUILD_VERSION)),
                      &pString, &numChars) == 0)
        goto Done;

    if (numChars == 0 || pString == 0 || bufferSize <= 2)
        goto Done;

    if ((numChars + 1) > bufferSize)
        numChars = bufferSize - 1;

    memcpy(buffer, pString, numChars);
#ifdef _DEMO_
    if (numChars + 1 < bufferSize)
    {
        strcat(buffer, "D");
        numChars = strlen(buffer);
    }
#endif
    buffer[numChars] = 0;

Done:
    ::free(versionBuffer);
    return;
}

//--------------------------------------------------------------------------//

bool DumpView::sendData(const char *_buffer, DWORD length, DWORD dwFlags)
{
    if (hEdit)
    {
        EDITSTREAM editStream = {};
        CHARRANGE  charRange;

        // x64: dwCookie is DWORD_PTR so the full 64-bit 'this' pointer fits.
        // pfnCallback assigned directly — no cast needed because editStreamCallback
        // is now a static function whose signature matches EDITSTREAMCALLBACK exactly.
        editStream.dwCookie    = (DWORD_PTR)this;
        editStream.pfnCallback = &DumpView::editStreamCallback;

        charRange.cpMax = charRange.cpMin = totalBytesSent;

        buffer     = _buffer;
        bufferSize = length;

        SendMessage(hEdit, EM_EXSETSEL,   0,           (LPARAM)&charRange);
        SendMessage(hEdit, EM_SCROLLCARET, 0,          0);
        SendMessage(hEdit, EM_STREAMIN,   (WPARAM)dwFlags, (LPARAM)&editStream);
    }
    if (dumpFile)
    {
        DWORD dwWritten;
        dumpFile->WriteFile(0, _buffer, length, &dwWritten, 0);
    }

    return (hEdit != 0 || dumpFile != 0);
}

//--------------------------------------------------------------------------//

int DumpView::STANDARD_DUMP(ErrorCode code, const C8 *fmt, ...)
{
    if (code.kind == ERR_MEMORY || code.kind == ERR_NETWORK || code.kind == ERR_MISSION ||
        ((code.kind != ERR_RULES       || CQFLAGS.bTraceRules) &&
         (code.kind != ERR_PERFORMANCE || CQFLAGS.bTracePerformance) &&
         (code.kind != ERR_FILE        || CQFLAGS.bInsideCreateInstance || code.severity < SEV_TRACE_1) &&
         TRACELEVEL >= (DWORD)code.severity))
    {
        if (code.kind == ERR_FILE)
        {
            if (CQFLAGS.bInsideCreateInstance)
            {
                code.severity = SEV_WARNING;
                CQFLAGS.bInsideCreateInstance = 0;
            }
            else if (code.severity > SEV_WARNING)
                return 0;
        }

        char buffer[5100];

        if (code.kind == ERR_ASSERT)
        {
            C8 *expr = const_cast<C8 *>(fmt);
            if (strncmp(fmt, "%s(%d)", 6) == 0)
                expr = const_cast<C8 *>(*((&fmt) + 3));
            wsprintf(buffer, "%s(%d) : CQASSERT(%s)", (void *)*((&fmt) + 1), *(((S32 *)(&fmt)) + 2), expr);
            if (view.sendData(buffer, strlen(buffer)) == 0)
                OutputDebugString(buffer);

            sprintf(buffer, "Assertion failed. (Press Retry to debug)\nExpression=\"%s\"\nSource file=%s\nLine %d",
                    expr, (void *)*((&fmt) + 1), *(((S32 *)(&fmt)) + 2));
        }
        else
        {
            va_list args;
            va_start(args, fmt);
            vsprintf(buffer, fmt, args);
            va_end(args);
        }

        if (code.kind == ERR_MEMORY)
        {
            switch (*(((S32 *)(&fmt)) + 2))
            {
            case DAHEAP_ALLOC_ZERO:
                code.severity = SEV_TRACE_1;
                break;

            case DAHEAP_VALLOC_FAILED:
                {
                    code.severity = SEV_FATAL;
                    int len = strlen(buffer);

                    // x64: MEMORYSTATUSEX / GlobalMemoryStatusEx — fields are
                    // DWORDLONG so they correctly represent >4 GB values
                    MEMORYSTATUSEX memoryStatus = {};
                    memoryStatus.dwLength = sizeof(memoryStatus);
                    GlobalMemoryStatusEx(&memoryStatus);

                    sprintf(buffer + len,
                            "\r\nPhysical Memory: %llu MB, VMemory: %llu MB, VAddress: %llu MB, HeapSize: %d MB",
                            (unsigned long long)(memoryStatus.ullTotalPhys     >> 20),
                            (unsigned long long)(memoryStatus.ullAvailPageFile  >> 20),
                            (unsigned long long)(memoryStatus.ullAvailVirtual   >> 20),
                            HEAP_Acquire()->GetHeapSize() >> 20);
                }
                break;

            default:
                if (code.severity > SEV_WARNING)
                    code.severity = SEV_WARNING;
                break;
            }
        }

        if (code.severity == SEV_FATAL || code.severity == SEV_ERROR || code.severity == SEV_WARNING)
        {
#pragma message("commented out for demo, generating empty error messages")
        }
        else
        {
            if (view.sendData(buffer, strlen(buffer)) == 0)
                OutputDebugString(buffer);
        }
    }

    return 0;
}

//--------------------------------------------------------------------------//

// x64: static callback — recover 'this' from dwCookie instead of using
// implicit 'this' from a member function pointer.
DWORD CALLBACK DumpView::editStreamCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb)
{
    DumpView *self = reinterpret_cast<DumpView *>(dwCookie);

    cb = __min(cb, self->bufferSize);
    memcpy(pbBuff, self->buffer, cb);
    self->buffer      += cb;
    self->bufferSize  -= cb;
    self->totalBytesSent += cb;
    *pcb = cb;

    return 0;  // continue the operation
}

//--------------------------------------------------------------------------//

LONG_PTR CALLBACK DumpView::_dlgProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    DumpView *_this;

    if (message == WM_INITDIALOG)
        SetWindowLongPtr(hwnd, DWLP_USER, lParam);

    if ((_this = (DumpView *)GetWindowLongPtr(hwnd, DWLP_USER)) != 0)
        return _this->dlgProc(hwnd, message, wParam, lParam);

    return 1;
}

//--------------------------------------------------------------------------//

BOOL DumpView::dlgProc(HWND hwnd, UINT message, UINT wParam, LONG lParam)
{
    BOOL result = 0;

    switch (message)
    {
    case WM_INITDIALOG:
        {
            WINDOWPLACEMENT loadStruct;

            HMENU hMenu = GetSystemMenu(hwnd, 0);
            EnableMenuItem(hMenu, SC_CLOSE, MF_BYCOMMAND | MF_DISABLED);
            hEdit = GetDlgItem(hwnd, IDC_RICHEDIT1);

            if (DEFAULTS->GetDataFromRegistry("dumpDialog", &loadStruct, sizeof(loadStruct)) == sizeof(loadStruct))
            {
                SetWindowPos(hwnd, HWND_TOPMOST,
                             loadStruct.rcNormalPosition.left,
                             loadStruct.rcNormalPosition.top,
                             loadStruct.rcNormalPosition.right  - loadStruct.rcNormalPosition.left,
                             loadStruct.rcNormalPosition.bottom - loadStruct.rcNormalPosition.top,
                             SWP_NOZORDER);
                ShowWindow(hwnd, loadStruct.showCmd);
            }
            else
                ShowWindow(hwnd, SW_SHOW);

            result = 1;
        }
        break;

    case WM_SIZE:
        if (hEdit)
        {
            SetWindowPos(hEdit, HWND_TOP, 0, 0, LOWORD(lParam), HIWORD(lParam), SWP_NOZORDER);
            SendMessage(hEdit, EM_SCROLLCARET, 0, 0);
        }
        break;
    }

    return result;
}

//--------------------------------------------------------------------------//

void __stdcall TrimResetHeap(IHeap *heap)
{
    // HEAP = heap;
}

//--------------------------------------------------------------------------//
//-----------------------------End DumpView.cpp-----------------------------//
//--------------------------------------------------------------------------//