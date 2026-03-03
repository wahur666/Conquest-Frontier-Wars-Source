//--------------------------------------------------------------------------//
//                                                                          //
//                               CQImage.cpp                               //
//                                                                          //
//               COPYRIGHT (C) 2000 BY DIGITAL ANVIL, INC.                  //
//                                                                          //
//--------------------------------------------------------------------------//
/*
   $Header: /Conquest/App/Src/CQImage.cpp 37    11/09/00 11:52a Jasony $
*/
//--------------------------------------------------------------------------//

#include "pch.h"
#include <globals.h>

#include "CQImage.h"
#include "Startup.h"
#include "CQTrace.h"
#include "Resource.h"
#include "EventSys2.h"
#include "DBHotkeys.h"

#include "ObjmapIterator.h"

#include <HeapObj.h>
#include <WindowManager.h>
#include <TSmartPointer.h>
#include <TComponent2.h>

#include <mmsystem.h>

#include <stdio.h>
#include <stdlib.h>
#include <dbghelp.h>        // x64: replaces imagehlp.h
#include <commctrl.h>
#include <span>

#define ERROR_SND "AppGPFault"
#define INFO_SND  "SystemExclamation"

static const char errorMsg[]   = "The program encountered an expected problem.\r\nInformation specific to this problem can be found in the \"Error.txt\" file.";
static const char errorTitle[] = "Program Error";

//--------------------------------------------------------------------------//
// x64: STACK_FRAME / manual EBP-chain walking removed entirely.
// Stack capture is done via CaptureStackBackTrace() and StackWalk64().
//--------------------------------------------------------------------------//

enum CQERROR_TYPE
{
    CQERR_REPORT = 1,
    CQERR_FATAL_EXCEPTION,
    CQERR_ERROR,
    CQERR_ASSERT,
    CQERR_EXCEPTION,
    CQERR_BOMB
};

//--------------------------------------------------------------------------//

template <int Size>
struct TEXT_BUFFER
{
    char *       buffer;
    int          index;
    CQERROR_TYPE type;

    TEXT_BUFFER(void)
    {
        buffer = (char *)VirtualAlloc(0, Size, MEM_COMMIT, PAGE_READWRITE);
        index  = 0;
    }

    ~TEXT_BUFFER(void)
    {
        VirtualFree(buffer, 0, MEM_RELEASE);
        buffer = 0;
    }

    TEXT_BUFFER & operator=(const TEXT_BUFFER<Size> & text)
    {
        memcpy(buffer, text.buffer, Size);
        index = text.index;
        return *this;
    }

    void addText(const char *pText, int len)
    {
        const int imax = Size - index - 1;
        len = __min(imax, len);
        memcpy(buffer + index, pText, len);
        index += len;
    }

    void addText(const char *pText)
    {
        addText(pText, strlen(pText));
    }
};

static bool CQImage_bEnabled = true;

//--------------------------------------------------------------------------//
// x64: Updated DbgHelp function pointer typedefs.
//   SymLoadModule64      replaces SymLoadModule       (DWORD64 base address)
//   SymGetLineFromAddr64 replaces SymGetLineFromAddr   (DWORD64 address)
//   SymFromAddr          replaces SymGetSymFromAddr    (SYMBOL_INFO, PDWORD64 disp)
//   SymGetModuleInfo64   replaces SymGetModuleInfo
//   SymUnloadModule64    replaces SymUnloadModule      (DWORD64 base address)
//   IMAGEHLP_LINE64      replaces IMAGEHLP_LINE
//   SYMBOL_INFO          replaces IMAGEHLP_SYMBOL
//--------------------------------------------------------------------------//

struct CQImage : public ICQImage
{
    static IDAComponent *GetICQImage(void *self)
    {
        return static_cast<ICQImage *>(static_cast<CQImage *>(self));
    }

    static std::span<const DACOMInterfaceEntry2> GetInterfaceMap()
    {
        static const DACOMInterfaceEntry2 map[] = {
            {"ICQImage", &GetICQImage},
        };
        return map;
    }

    typedef BOOL    (__stdcall *SYMINIT)            (IN HANDLE hProcess, IN LPSTR UserSearchPath, IN BOOL fInvadeProcess);
    typedef BOOL    (__stdcall *SYMCLEANUP)         (IN HANDLE hProcess);

    // x64: base address and size are DWORD64
    typedef DWORD64 (__stdcall *SYMLOADMODULE64)    (IN HANDLE  hProcess, IN HANDLE hFile,
                                                     IN PSTR    ImageName, IN PSTR  ModuleName,
                                                     IN DWORD64 BaseOfDll, IN DWORD SizeOfDll);

    // x64: address is DWORD64, displacement is PDWORD (line byte offset, always fits 32-bit)
    typedef BOOL    (__stdcall *SYMGETLINEFROMADDR64)(IN  HANDLE            hProcess,
                                                      IN  DWORD64           dwAddr,
                                                      OUT PDWORD            pdwDisplacement,
                                                      OUT PIMAGEHLP_LINE64  Line);

    typedef DWORD   (__stdcall *SYMSETOPTIONS)      (IN DWORD SymOptions);

    typedef BOOL    (__stdcall *SYMGETMODULEINFO64) (IN  HANDLE             hProcess,
                                                     IN  DWORD64            dwAddr,
                                                     OUT PIMAGEHLP_MODULE64 ModuleInfo);

    // x64: replaces SymGetSymFromAddr; displacement is PDWORD64
    typedef BOOL    (__stdcall *SYMFROMADDR)        (IN  HANDLE       hProcess,
                                                     IN  DWORD64      dwAddr,
                                                     OUT PDWORD64     pdwDisplacement,
                                                     OUT PSYMBOL_INFO Symbol);

    // x64: base address is DWORD64
    typedef BOOL    (__stdcall *SYMUNLOADMODULE64)  (IN HANDLE hProcess, IN DWORD64 BaseOfDll);

    //------------------------
    HANDLE               hProcess;
    SYMINIT              SymInitialize;
    SYMCLEANUP           SymCleanup;
    SYMLOADMODULE64      SymLoadModule64;
    SYMGETLINEFROMADDR64 SymGetLineFromAddr64;
    SYMSETOPTIONS        SymSetOptions;
    SYMGETMODULEINFO64   SymGetModuleInfo64;
    SYMFROMADDR          SymFromAddr;
    SYMUNLOADMODULE64    SymUnloadModule64;
    HINSTANCE            hInstance;

    CQImage(void);
    ~CQImage(void);

    void *operator new(size_t size)  { return calloc(size, 1); }
    void  operator delete(void *ptr) { ::free(ptr); }

    /* ICQImage methods */
    virtual void LoadSymTable  (HINSTANCE hInstance);
    virtual void UnloadSymTable(HINSTANCE hInstance);
    virtual void Report        (const char *szInfo);
    virtual void MemoryReport  (struct IHeap *heap);
    virtual void SetMessagesEnabled(bool bSetting) { CQImage_bEnabled = bSetting; }

    /* CQImage methods */
    static long __stdcall PrintBlock(struct IHeap *pHeap, void *allocatedBlock, U32 dwFlags, void *context);

    IDAComponent *getBase(void) { return static_cast<ICQImage *>(this); }

    void init(void);
};

static DAComponentX<struct CQImage> image;

//--------------------------------------------------------------------------//

CQImage::CQImage(void)
{
    CQIMAGE = this;
    init();
}

//--------------------------------------------------------------------------//

void CQImage::init(void)
{
#ifndef FINAL_RELEASE

    hInstance = LoadLibrary("DbgHelp.dll");
    if (hInstance)
    {
        if ((SymInitialize        = (SYMINIT)              GetProcAddress(hInstance, "SymInitialize"))        == 0) goto Done;
        if ((SymCleanup           = (SYMCLEANUP)           GetProcAddress(hInstance, "SymCleanup"))           == 0) goto Done;
        if ((SymLoadModule64      = (SYMLOADMODULE64)      GetProcAddress(hInstance, "SymLoadModule64"))      == 0) goto Done;
        if ((SymUnloadModule64    = (SYMUNLOADMODULE64)    GetProcAddress(hInstance, "SymUnloadModule64"))    == 0) goto Done;
        if ((SymGetLineFromAddr64 = (SYMGETLINEFROMADDR64) GetProcAddress(hInstance, "SymGetLineFromAddr64")) == 0) goto Done;
        if ((SymSetOptions        = (SYMSETOPTIONS)        GetProcAddress(hInstance, "SymSetOptions"))        == 0) goto Done;
        if ((SymGetModuleInfo64   = (SYMGETMODULEINFO64)   GetProcAddress(hInstance, "SymGetModuleInfo64"))   == 0) goto Done;
        if ((SymFromAddr          = (SYMFROMADDR)          GetProcAddress(hInstance, "SymFromAddr"))          == 0) goto Done;

        // x64: GetCurrentProcess() returns a pseudo-handle suitable for DbgHelp.
        // The old code used (HANDLE)GetCurrentProcessId() which passed a PID
        // as a handle — never correct, x86 just got away with it.
        hProcess = GetCurrentProcess();

        char  path[MAX_PATH + 4];
        char *ptr;
        GetModuleFileName(NULL, path, sizeof(path));

        if ((ptr = strrchr(path, '\\')) != 0)
            *ptr = 0;

        if (SymInitialize(hProcess, path, FALSE) == 0)
            hProcess = 0;
        else
            SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);

        GetLastError();

        if (hProcess)
        {
            LoadSymTable(GetModuleHandle("Conquest.exe"));
            LoadSymTable(GetModuleHandle("Mission.dll"));
            LoadSymTable(GetModuleHandle("Trim.dll"));
            LoadSymTable(GetModuleHandle("ZBatcher.dll"));
            LoadSymTable(GetModuleHandle("DACOM.dll"));
        }
    }

Done:
    return;

#endif  // !FINAL_RELEASE
}

//--------------------------------------------------------------------------//

CQImage::~CQImage(void)
{
    if (hProcess)
    {
        SymCleanup(hProcess);
        hProcess = 0;
    }
    if (hInstance)
    {
        FreeLibrary(hInstance);
        hInstance = 0;
    }

    CQIMAGE = 0;
}

//--------------------------------------------------------------------------//

void CQImage::LoadSymTable(HINSTANCE hInstance)
{
#ifndef FINAL_RELEASE
    if (hProcess)
    {
        char imageName[MAX_PATH + 4];

        if (GetModuleFileName(hInstance, imageName, sizeof(imageName)))
        {
            imageName[sizeof(imageName) - 1] = 0;
            // x64: cast through ULONG_PTR to avoid truncation warning
            SymLoadModule64(hProcess, NULL, imageName, NULL, (DWORD64)(ULONG_PTR)hInstance, 0);
        }
    }
#endif  // !FINAL_RELEASE
}

//--------------------------------------------------------------------------//

void CQImage::UnloadSymTable(HINSTANCE hInstance)
{
#ifndef FINAL_RELEASE
    if (hProcess)
    {
        // x64: DWORD64 base address
        SymUnloadModule64(hProcess, (DWORD64)(ULONG_PTR)hInstance);
    }
#endif  // !FINAL_RELEASE
}

#ifndef FINAL_RELEASE

//--------------------------------------------------------------------------//

static void restorePriority(void)
{
    if (CQFLAGS.b3DEnabled && CQFLAGS.bNoGDI)
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    else
        SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
}

//--------------------------------------------------------------------------//

static void getVersion(char *buffer, U32 bufferSize)
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

    if (VerQueryValue(versionBuffer, const_cast<char *>(_localLoadString(IDS_BUILD_VERSION)), &pString, &numChars) == 0)
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

static void initStatic(HWND hwnd)
{
    char format[128];
    char version[128];
    char buffer[128];

    GetWindowText(hwnd, format, sizeof(format));
    getVersion(version, sizeof(version));
    sprintf(buffer, format, version);
    SetWindowText(hwnd, buffer);
}

//--------------------------------------------------------------------------//

static LONG_PTR CALLBACK dlgProc(HWND hwnd, UINT message, WPARAM wParam, LONG lParam)
{
    BOOL result = 0;

    switch (message)
    {
    case WM_INITDIALOG:
        {
            HWND hItem;
            const TEXT_BUFFER<4096> *pText = (const TEXT_BUFFER<4096> *)lParam;

            hItem = GetDlgItem(hwnd, IDC_STATIC_VERSION);
            initStatic(hItem);

            switch (pText->type)
            {
            case CQERR_REPORT:
                SetWindowText(hwnd, "Report");
                EnableWindow(GetDlgItem(hwnd, IDABORT), 0);
                EnableWindow(GetDlgItem(hwnd, IDDEBUG), 0);
                break;
            case CQERR_FATAL_EXCEPTION:
                SetWindowText(hwnd, "Exception");
                EnableWindow(GetDlgItem(hwnd, IDIGNORE), 0);
                break;
            case CQERR_ERROR:
                SetWindowText(hwnd, "Recoverable Error");
                break;
            case CQERR_EXCEPTION:
                SetWindowText(hwnd, "Exception");
                break;
            case CQERR_BOMB:
                SetWindowText(hwnd, "Bomb");
                break;
            }

            hItem = GetDlgItem(hwnd, IDC_EDIT1);
            SetWindowText(hItem, pText->buffer);
            SetFocus(hItem);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDCANCEL:
            if (IsWindowEnabled(GetDlgItem(hwnd, IDIGNORE)) == 0)
                break;
            else
                wParam = IDIGNORE;
            // fall through intentional
        case IDDEBUG:
        case IDABORT:
        case IDIGNORE:
            EndDialog(hwnd, LOWORD(wParam));
            break;
        }
        break;
    }

    return result;
}

//--------------------------------------------------------------------------//
// x64: Shared helper — appends one frame's file / line / symbol info to text.
// Called from Assert, Bomb, Error, and Exception to avoid duplicated loops.
//
//   addr    — 64-bit return address for this frame
//   text    — output TEXT_BUFFER
//   scratch — caller-owned 1024-byte workspace
//--------------------------------------------------------------------------//

static void AppendFrameInfo(DWORD64 addr, TEXT_BUFFER<4096> &text, char *scratch)
{
    bool  bLineValid = false;
    DWORD dwLineDisp = 0;

    // x64: IMAGEHLP_LINE64 (was IMAGEHLP_LINE)
    IMAGEHLP_LINE64 iLine = {};
    iLine.SizeOfStruct = sizeof(iLine);

    if (image.SymGetLineFromAddr64 &&
        image.SymGetLineFromAddr64(image.hProcess, addr, &dwLineDisp, &iLine))
    {
        bLineValid       = true;
        const char *ptr  = strrchr(iLine.FileName, '\\');
        ptr              = ptr ? ptr + 1 : iLine.FileName;
        text.addText(ptr);
        sprintf(scratch, ", Line %lu", iLine.LineNumber);
        text.addText(scratch);
    }
    GetLastError();

    // x64: SYMBOL_INFO replaces IMAGEHLP_SYMBOL.
    //   SizeOfStruct = sizeof(SYMBOL_INFO)  (fixed header only, NOT including Name[])
    //   MaxNameLen   = space reserved for the trailing name string
    // Allocate sizeof(SYMBOL_INFO) + MAX_SYM_NAME so the name always fits.
    alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
    SYMBOL_INFO *iSymbol = reinterpret_cast<SYMBOL_INFO *>(symBuf);
    memset(iSymbol, 0, sizeof(symBuf));
    iSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    iSymbol->MaxNameLen   = MAX_SYM_NAME;   // x64: MaxNameLen, NOT MaxNameLength

    DWORD64 dwSymDisp = 0;                  // x64: displacement is DWORD64

    if (image.SymFromAddr &&
        image.SymFromAddr(image.hProcess, addr, &dwSymDisp, iSymbol))
    {
        if (bLineValid) text.addText(", ");
        text.addText(iSymbol->Name);
        if (bLineValid)
        {
            text.addText("()\r\n");
        }
        else
        {
            sprintf(scratch, " + %llu bytes\r\n", (unsigned long long)dwSymDisp);
            text.addText(scratch);
        }
    }
    else if (!bLineValid)
    {
        // Neither line nor symbol resolved — print the raw address
        sprintf(scratch, "Address=0x%016llX, ??Unknown??\r\n", (unsigned long long)addr);
        text.addText(scratch);
    }
}

#endif  // !FINAL_RELEASE

//--------------------------------------------------------------------------//

void CQImage::Report(const char *szInfo)
{
#ifndef FINAL_RELEASE

    TEXT_BUFFER<4096> text;

    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);

    text.type = CQERR_REPORT;
    PlaySound(INFO_SND, NULL, SND_ALIAS | SND_ASYNC);
    text.addText("Report: ");
    text.addText(szInfo);

    FDUMP(ErrorCode(ERR_GENERAL, SEV_TRACE_1), szInfo);

    FlipToGDI();
    DialogBoxParam(hResource, MAKEINTRESOURCE(IDD_DIALOG13), hMainWindow,
                   reinterpret_cast<DLGPROC>(dlgProc), reinterpret_cast<LPARAM>(&text));
    restorePriority();

#endif  // !FINAL_RELEASE
}

//--------------------------------------------------------------------------//

#define OPPRINT0(exp) FDUMP(ErrorCode(ERR_GENERAL, SEV_TRACE_1), exp)

//--------------------------------------------------------------------------//

bool ICQImage::Assert(const char *exp, const char *file, unsigned line)
{
#ifndef FINAL_RELEASE

    if (!CQImage_bEnabled)
        return false;

    if (image.hProcess)
    {
        char              buffer[1024];
        TEXT_BUFFER<4096> text;
        char              version[64];
        getVersion(version, sizeof(version));

        SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);

        text.type = CQERR_ASSERT;
        if (CQFLAGS.bTraceMission != 0 && EVENTSYS)
            EVENTSYS->Send(CQE_DEBUG_HOTKEY, (void *)IDH_PRINT_OPLIST);
        PlaySound(ERROR_SND, NULL, SND_ALIAS | SND_ASYNC);

        sprintf(buffer, "Expression: %s [%s]\r\n", exp, version);
        text.addText(buffer);
        text.addText("Call Stack:\r\n");

        // x64: CaptureStackBackTrace replaces manual EBP-chain walking.
        // Skip 1 frame (this function) so the first reported frame is the
        // caller that triggered the assert.
        void * frames[16] = {};
        USHORT frameCount = CaptureStackBackTrace(1, 16, frames, NULL);

        for (USHORT i = 0; i < frameCount; i++)
            AppendFrameInfo((DWORD64)(ULONG_PTR)frames[i], text, buffer);

        FDUMP(ErrorCode(ERR_GENERAL, SEV_TRACE_1), "-------------------------------------------------\r\nAssertion Failed: \r\n");
        FDUMP(ErrorCode(ERR_GENERAL, SEV_TRACE_1), text.buffer);
        int result = IDIGNORE;

        FlipToGDI();
        result = DialogBoxParam(hResource, MAKEINTRESOURCE(IDD_DIALOG13), hMainWindow,
                                DLGPROC(dlgProc), (LPARAM)&text);
        restorePriority();

        if (result == IDABORT)
        {
            CQFLAGS.bNoExitConfirm = 1;
            PostQuitMessage(-1);
            if (WM)
                WM->ServeMessageQueue();
        }
        else if (result == IDDEBUG)
            return true;

        return false;
    }

    FDUMP(ErrorCode(ERR_ASSERT, SEV_FATAL), exp, file, line);

#endif  // !FINAL_RELEASE

    return false;
}

//--------------------------------------------------------------------------//

bool __cdecl ICQImage::Bomb(const char *exp, ...)
{
#ifndef FINAL_RELEASE

    if (!CQImage_bEnabled)
        return false;

    char buffer[1024];

    va_list args;
    va_start(args, exp);
    vsprintf(buffer, exp, args);
    va_end(args);

    if (image.hProcess)
    {
        TEXT_BUFFER<4096> text;
        char              version[64];
        getVersion(version, sizeof(version));

        // x64: CaptureStackBackTrace replaces the inline asm EBP trick.
        // On x64 the compiler routinely omits frame pointers, so the old
        // "mov DWORD ptr [pFrame], ebp" chain walk produces garbage silently.
        // CaptureStackBackTrace uses RtlCaptureStackBackTrace internally and
        // is fully supported on x64.
        void * frames[16] = {};
        USHORT frameCount = CaptureStackBackTrace(1, 16, frames, NULL);

        SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);

        text.type = CQERR_BOMB;
        if (CQFLAGS.bTraceMission != 0 && EVENTSYS)
            EVENTSYS->Send(CQE_DEBUG_HOTKEY, (void *)IDH_PRINT_OPLIST);
        PlaySound(ERROR_SND, NULL, SND_ALIAS | SND_ASYNC);

        {
            char *ptr = buffer;
            if (ptr[0] && ptr[1] == ':')
                ptr += 2;
            ptr = strchr(ptr, ':');
            ptr = ptr ? ptr + 2 : buffer;
            text.addText("Error: ");
            text.addText(ptr);
            sprintf(buffer, " [%s]\r\n", version);
            text.addText(buffer);
            text.addText("Call Stack:\r\n");
        }

        for (USHORT i = 0; i < frameCount; i++)
            AppendFrameInfo((DWORD64)(ULONG_PTR)frames[i], text, buffer);

        FDUMP(ErrorCode(ERR_GENERAL, SEV_TRACE_1), "-------------------------------------------------\r\nBomb: \r\n");
        FDUMP(ErrorCode(ERR_GENERAL, SEV_TRACE_1), text.buffer);
        int result = IDABORT;

        FlipToGDI();
        result = DialogBoxParam(hResource, MAKEINTRESOURCE(IDD_DIALOG13), hMainWindow,
                                DLGPROC(dlgProc), (LPARAM)&text);
        restorePriority();

        if (result == IDABORT)
        {
            CQFLAGS.bNoExitConfirm = 1;
            PostQuitMessage(-1);
            if (WM)
                WM->ServeMessageQueue();
        }
        else if (result == IDDEBUG)
            return true;

        return false;
    }

    FDUMP(ErrorCode(ERR_GENERAL, SEV_FATAL), buffer);

#endif  // !FINAL_RELEASE

    return false;
}

//--------------------------------------------------------------------------//

bool __cdecl ICQImage::Error(const char *exp, ...)
{
#ifndef FINAL_RELEASE

    if (!CQImage_bEnabled)
        return false;

    char buffer[1024];

    va_list args;
    va_start(args, exp);
    vsprintf(buffer, exp, args);
    va_end(args);

    if (image.hProcess)
    {
        TEXT_BUFFER<4096> text;
        char              version[64];
        getVersion(version, sizeof(version));

        // x64: same replacement as Bomb() above
        void * frames[16] = {};
        USHORT frameCount = CaptureStackBackTrace(1, 16, frames, NULL);

        SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);

        text.type = CQERR_ERROR;
        PlaySound(INFO_SND, NULL, SND_ALIAS | SND_ASYNC);

        {
            char *ptr = buffer;
            if (ptr[0] && ptr[1] == ':')
                ptr += 2;
            ptr = strchr(ptr, ':');
            ptr = ptr ? ptr + 2 : buffer;
            text.addText("Error: ");
            text.addText(ptr);
            sprintf(buffer, " [%s]\r\n", version);
            text.addText(buffer);
            text.addText("Call Stack:\r\n");
        }

        for (USHORT i = 0; i < frameCount; i++)
            AppendFrameInfo((DWORD64)(ULONG_PTR)frames[i], text, buffer);

        FDUMP(ErrorCode(ERR_GENERAL, SEV_TRACE_1), "-------------------------------------------------\r\nRecoverable Error: \r\n");
        FDUMP(ErrorCode(ERR_GENERAL, SEV_TRACE_1), text.buffer);
        int result = IDIGNORE;

        FlipToGDI();
        result = DialogBoxParam(hResource, MAKEINTRESOURCE(IDD_DIALOG13), hMainWindow,
                                DLGPROC(dlgProc), (LPARAM)&text);
        restorePriority();

        if (result == IDABORT)
        {
            CQFLAGS.bNoExitConfirm = 1;
            PostQuitMessage(-1);
            if (WM)
                WM->ServeMessageQueue();
        }
        else if (result == IDDEBUG)
            return true;

        return false;
    }

    FDUMP(ErrorCode(ERR_GENERAL, SEV_ERROR), buffer);

#endif  // !FINAL_RELEASE

    return false;
}

//--------------------------------------------------------------------------//

int ICQImage::Exception(struct _EXCEPTION_POINTERS *exceptionInfo)
{
#ifndef FINAL_RELEASE

    if (!CQImage_bEnabled)
        return IDABORT;

    const CONTEXT *pContext = exceptionInfo->ContextRecord;

    if (image.hProcess && (pContext->ContextFlags & CONTEXT_CONTROL) != 0)
    {
        char              buffer[1024];
        TEXT_BUFFER<4096> text;
        bool              bFatal = (exceptionInfo->ExceptionRecord->ExceptionFlags != 0);
        char              version[64];
        getVersion(version, sizeof(version));

        SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
        PlaySound(ERROR_SND, NULL, SND_ALIAS | SND_ASYNC);

        text.type = CQERR_EXCEPTION;
        if (bFatal)
        {
            text.addText("Fatal ");
            text.type = CQERR_FATAL_EXCEPTION;
        }
        text.addText("Exception: ");

        {
            const char *name = "??Unknown type??";
            switch (exceptionInfo->ExceptionRecord->ExceptionCode)
            {
            case EXCEPTION_ACCESS_VIOLATION:         name = "Access Violation";     break;
            case EXCEPTION_BREAKPOINT:               name = "User Breakpoint";      break;
            case EXCEPTION_FLT_DIVIDE_BY_ZERO:       name = "Float Div by Zero";    break;
            case EXCEPTION_FLT_INVALID_OPERATION:    name = "Float Invalid Op";     break;
            case EXCEPTION_FLT_DENORMAL_OPERAND:
            case EXCEPTION_FLT_INEXACT_RESULT:
            case EXCEPTION_FLT_OVERFLOW:
            case EXCEPTION_FLT_STACK_CHECK:
            case EXCEPTION_FLT_UNDERFLOW:            name = "FPU";                  break;
            case EXCEPTION_PRIV_INSTRUCTION:
            case EXCEPTION_ILLEGAL_INSTRUCTION:      name = "Illegal Instruction";  break;
            case EXCEPTION_IN_PAGE_ERROR:            name = "Paging error";         break;
            case EXCEPTION_INT_DIVIDE_BY_ZERO:       name = "Integer Div by Zero";  break;
            case EXCEPTION_INT_OVERFLOW:             name = "Integer overflow";     break;
            case EXCEPTION_NONCONTINUABLE_EXCEPTION: name = "Noncontinuable";       break;
            case EXCEPTION_SINGLE_STEP:              name = "Single Step";          break;
            case EXCEPTION_STACK_OVERFLOW:           name = "Stack overflow";       break;
            }
            text.addText(name);
            sprintf(buffer, " [%s]\r\n", version);
            text.addText(buffer);
        }

        if (DEBUG_ITERATOR)
        {
            text.addText("Live iterator:\r\n");
            if (IsBadReadPtr(DEBUG_ITERATOR, sizeof(ObjMapIterator)) == 0)
            {
                // x64: 16-digit hex for pointers
                sprintf(buffer, "   current [0x%016llX] -> ",
                        (unsigned long long)(ULONG_PTR)DEBUG_ITERATOR->current);
                text.addText(buffer);
                if (IsBadReadPtr(DEBUG_ITERATOR->current, sizeof(ObjMapNode)) == 0)
                {
                    sprintf(buffer, "obj=0x%016llX, dwMissionID=0x%X, flags=0x%X, next=0x%016llX\r\n",
                            (unsigned long long)(ULONG_PTR)DEBUG_ITERATOR->current->obj,
                            DEBUG_ITERATOR->current->dwMissionID,
                            DEBUG_ITERATOR->current->flags,
                            (unsigned long long)(ULONG_PTR)DEBUG_ITERATOR->current->next);
                    text.addText(buffer);
                }
                else
                    text.addText("  ???\r\n");
            }
            else
                text.addText("  ???\r\n");
        }

        text.addText("Call Stack:\r\n");

        // x64: Use StackWalk64 seeded from the exception CONTEXT so we get the
        // actual faulting call chain, not the SEH handler chain.
        // StackWalk64 modifies the CONTEXT in-place, so we work on a copy.
        CONTEXT ctx = *pContext;

        STACKFRAME64 sf  = {};
        sf.AddrPC.Mode    = AddrModeFlat;
        sf.AddrFrame.Mode = AddrModeFlat;
        sf.AddrStack.Mode = AddrModeFlat;

#if defined(_M_X64)
        sf.AddrPC.Offset    = ctx.Rip;
        sf.AddrFrame.Offset = ctx.Rbp;
        sf.AddrStack.Offset = ctx.Rsp;
        const DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_IX86)
        sf.AddrPC.Offset    = ctx.Eip;
        sf.AddrFrame.Offset = ctx.Ebp;
        sf.AddrStack.Offset = ctx.Esp;
        const DWORD machineType = IMAGE_FILE_MACHINE_I386;
#else
#error Unsupported target architecture
#endif

        for (int i = 0; i < 16; i++)
        {
            if (!StackWalk64(machineType,
                             image.hProcess,
                             GetCurrentThread(),
                             &sf,
                             &ctx,
                             NULL,                      // use default ReadMemory
                             SymFunctionTableAccess64,  // from DbgHelp.lib
                             SymGetModuleBase64,        // from DbgHelp.lib
                             NULL))
                break;

            if (sf.AddrPC.Offset == 0)
                break;

            AppendFrameInfo(sf.AddrPC.Offset, text, buffer);
        }

        FDUMP(ErrorCode(ERR_GENERAL, SEV_TRACE_1), "-------------------------------------------------\r\nException: \r\n");
        FDUMP(ErrorCode(ERR_GENERAL, SEV_TRACE_1), text.buffer);
        int result = IDABORT;

        FlipToGDI();
        result = DialogBoxParam(hResource, MAKEINTRESOURCE(IDD_DIALOG13), hMainWindow,
                                DLGPROC(dlgProc), (LPARAM)&text);
        restorePriority();

        return result;
    }

#endif  // !FINAL_RELEASE

    return IDABORT;
}

//--------------------------------------------------------------------------//

void CQImage::MemoryReport(struct IHeap *heap)
{
#ifndef FINAL_RELEASE
    if (heap->GetLargestBlock() != heap->GetHeapSize())
    {
        TEXT_BUFFER<4096> text;
        text.type = CQERR_REPORT;
        text.addText("Memory Leaks Detected:\r\n");
        int oldOffset = text.index;

        heap->EnumerateBlocks(PrintBlock, &text);

        if (oldOffset != text.index)
        {
            SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
            PlaySound(INFO_SND, NULL, SND_ALIAS | SND_ASYNC);
            FlipToGDI();
            DialogBoxParam(hResource, MAKEINTRESOURCE(IDD_DIALOG13), hMainWindow,
                           DLGPROC(dlgProc), (LPARAM)&text);
            restorePriority();
        }
    }
#endif  // !FINAL_RELEASE
}

//--------------------------------------------------------------------------//

long CQImage::PrintBlock(struct IHeap *pHeap, void *allocatedBlock, U32 dwFlags, void *context)
{
#ifndef FINAL_RELEASE

    if (image.hProcess)
    {
        TEXT_BUFFER<4096> *pText = (TEXT_BUFFER<4096> *)context;
        char               buffer[1024];
        // x64: owner address is DWORD64
        DWORD64 owner    = (DWORD64)(ULONG_PTR)pHeap->GetBlockOwner(allocatedBlock);
        int     oldOffset = pText->index;
        DWORD   dwLineDisp = 0;

        // x64: sentinel was 0xFFFFFFFF on x86; use pointer-width -1 cast
        if (owner == (DWORD64)(ULONG_PTR)(-1))
            goto Done;
        if ((dwFlags & DAHEAPFLAG_ALLOCATED_BLOCK) == 0)
            goto Done;

        {
            // x64: IMAGEHLP_LINE64
            IMAGEHLP_LINE64 iLine = {};
            iLine.SizeOfStruct = sizeof(iLine);

            if (image.SymGetLineFromAddr64 &&
                image.SymGetLineFromAddr64(image.hProcess, owner, &dwLineDisp, &iLine))
            {
                pText->addText(iLine.FileName);
                sprintf(buffer, "(%lu) : ", iLine.LineNumber);
                pText->addText(buffer);
            }
            else
            {
                const char *pMsg = pHeap->GetBlockMessage(allocatedBlock);
                pText->addText("?");

                if (IsBadReadPtr(pMsg, 32) == 0)
                {
                    if (strcmp(pMsg, "Heap control block") == 0)
                    {
                        pText->index = oldOffset;
                        goto Done;
                    }
                    pText->addText(pMsg);
                }
                pText->addText("(?) : ");
            }
        }

        // x64: 16-digit hex pointer
        sprintf(buffer, "%d byte memory leak. [PTR=0x%016llX]\r\n",
                pHeap->GetBlockSize(allocatedBlock),
                (unsigned long long)(ULONG_PTR)allocatedBlock);
        pText->addText(buffer);

        OutputDebugString(pText->buffer + oldOffset);
    }

Done:

#endif  // !FINAL_RELEASE

    return 1;
}

//--------------------------------------------------------------------------//

int ICQImage::STANDARD_DUMP(ErrorCode code, const C8 *fmt, ...)
{
#ifndef FINAL_RELEASE

    char buffer[4096];

    {
        va_list args;
        va_start(args, fmt);
        vsprintf(buffer, fmt, args);
        va_end(args);
    }

    if (code.kind == ERR_MEMORY)
    {
        switch (*(reinterpret_cast<S32 *>(&fmt) + 2))
        {
        case DAHEAP_ALLOC_ZERO:
            return 0;

        case DAHEAP_VALLOC_FAILED:
            {
                code.severity = SEV_FATAL;
                int len = strlen(buffer);

                // x64: MEMORYSTATUSEX / GlobalMemoryStatusEx replaces
                // MEMORYSTATUS / GlobalMemoryStatus.
                // Fields are DWORDLONG so they correctly report >4 GB.
                MEMORYSTATUSEX memoryStatus = {};
                memoryStatus.dwLength = sizeof(memoryStatus);
                GlobalMemoryStatusEx(&memoryStatus);

                sprintf(buffer + len,
                        "\r\nPhysical Memory: %llu MB, VMemory: %llu MB, VAddress: %llu MB, HeapSize: %d MB",
                        (unsigned long long)(memoryStatus.ullTotalPhys     >> 20),
                        (unsigned long long)(memoryStatus.ullAvailPageFile  >> 20),
                        (unsigned long long)(memoryStatus.ullAvailVirtual   >> 20),
                        HEAP->GetHeapSize() >> 20);
            }
            break;

        case DAHEAP_OUT_OF_MEMORY:
            Bomb(buffer);
            InitializeDAHeap(0x800000, 0x800000, DAHEAPFLAG_DEBUGFILL_SNAN | DAHEAPFLAG_NOMSGS);
            return 1;
        }
    }

    return Bomb(buffer);

#else   // FINAL_RELEASE

    return 0;

#endif  // FINAL_RELEASE
}

//--------------------------------------------------------------------------//
//----------------------------End CQImage.cpp-------------------------------//
//--------------------------------------------------------------------------//