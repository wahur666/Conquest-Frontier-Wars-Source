#ifndef FILESYS_H
#define FILESYS_H
//--------------------------------------------------------------------------//
//                                                                          //
//                              FILESYS.H                                   //
//                                                                          //
//               COPYRIGHT (C) 1996 BY DIGITAL ANVIL, INC.                  //
//                                                                          //
//--------------------------------------------------------------------------//
/*

   $Author: Rmarr $
*/


/*

  Features:

  :  Multiple file systems
  :  File systems within file systems
  :  File systems that operate on top of other file systems
  :  Allows multiple files open at same time, for read/write
  :  Installed file systems behave exactly like local OS 
  :  Memory mapped files
  :  Async I/O

*/
//--------------------------------------------------------------------------//

#ifndef DACOM_H
#include "DACOM.h"
#endif

#include <windows.h>
#ifndef _WINDOWS_
#error Windows.h required for this to compile!
#endif

//--------------------------------------------------------------------------//
//-----------------------------IFileSystem Interface------------------------//
//--------------------------------------------------------------------------//

typedef struct IFileSystem *LPFILESYSTEM;
typedef long (__stdcall IFileSystem::*DAFILE_SERIAL_PROC) (void *lpContext);

#define SERIALMETHOD(method)        LONG __stdcall method (void *lpContext=0)

//---------------------
// Use DAFILEINFO structure on call to CreateInstance
//---------------------

struct DAFILEDESC : public DACOMDESC
{
   LPCTSTR               lpImplementation = nullptr;
   LPCTSTR               lpFileName = nullptr;
   DWORD                 dwDesiredAccess = 0;
   DWORD                 dwShareMode = 0;
   LPSECURITY_ATTRIBUTES lpSecurityAttributes = {};
   DWORD                 dwCreationDistribution = 0;
   DWORD                 dwFlagsAndAttributes = 0;
   HANDLE                hTemplateFile = nullptr;
   LPFILESYSTEM          lpParent = {};
   HANDLE                hParent = nullptr;
   HANDLE                hFindFirst = nullptr;

    DAFILEDESC(const C8* _file_name = nullptr, const C8* _interface_name = "IFileSystem")
     : DACOMDESC(_interface_name),
       lpFileName(_file_name),
       dwDesiredAccess(GENERIC_READ),
       dwShareMode(FILE_SHARE_READ),
       dwCreationDistribution(OPEN_EXISTING),
       dwFlagsAndAttributes(FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN),
       hFindFirst(INVALID_HANDLE_VALUE)
     {
          size = sizeof(*this);
     }
};


#define IID_IFileSystem MAKE_IID("IFileSystem",1)

struct DACOM_NO_VTABLE IFileSystem : public IComponentFactory
{
   // *** IDAComponent methods ***

   DEFMETHOD(QueryInterface) (const C8 *interface_name, void **instance) = 0;
   DEFMETHOD_(U32,AddRef)           (void) = 0;
   DEFMETHOD_(U32,Release)          (void) = 0;

   // *** IComponentFactory methods ***

   DEFMETHOD(CreateInstance) (DACOMDESC *descriptor, void **instance) = 0;

   //
   // *** IFileSystem methods ***
   //
   // Note: Windows types used below for optimum compatibility with Win32
   // file system API calls
   //

   DEFMETHOD_(BOOL,CloseHandle) (HANDLE handle) = 0;

   // ReadFile/WriteFile: nNumberOfBytesToRead stays DWORD per Win32 convention
   // (ReadFile itself is limited to DWORD-sized chunks)
   DEFMETHOD_(BOOL,ReadFile) (HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead,
               LPDWORD lpNumberOfBytesRead,
               LPOVERLAPPED lpOverlapped=0) = 0;

   DEFMETHOD_(BOOL,WriteFile) (HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
                LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped=0) = 0;

   DEFMETHOD_(BOOL,GetOverlappedResult)   (HANDLE hFile,
                                 LPOVERLAPPED lpOverlapped,
                                 LPDWORD lpNumberOfBytesTransferred,
                                 BOOL bWait) = 0;

   // SetFilePointer: use LARGE_INTEGER-compatible approach;
   // lDistanceToMove stays LONG but lpDistanceToMoveHigh enables 64-bit offsets
   DEFMETHOD_(DWORD,SetFilePointer) (HANDLE hFile, LONG lDistanceToMove,
                     PLONG lpDistanceToMoveHigh=0, DWORD dwMoveMethod=FILE_BEGIN) = 0;

   DEFMETHOD_(BOOL,SetEndOfFile) (HANDLE hFile = 0) = 0;

   // GetFileSize: returns DWORD (low), lpFileSizeHigh gets high 32 bits — matches Win32
   // For sizes, combine as: ULONGLONG size = ((ULONGLONG)high << 32) | low
   DEFMETHOD_(DWORD,GetFileSize) (HANDLE hFile=0, LPDWORD lpFileSizeHigh=0) = 0;

   // LockFile/UnlockFile: High/Low pairs are correct Win32 convention for 64-bit offsets
   DEFMETHOD_(BOOL,LockFile) (HANDLE hFile,
                              DWORD dwFileOffsetLow,
                              DWORD dwFileOffsetHigh,
                              DWORD nNumberOfBytesToLockLow,
                              DWORD nNumberOfBytesToLockHigh) = 0;

   DEFMETHOD_(BOOL,UnlockFile) (HANDLE hFile,
                                DWORD dwFileOffsetLow,
                                DWORD dwFileOffsetHigh,
                                DWORD nNumberOfBytesToUnlockLow,
                                DWORD nNumberOfBytesToUnlockHigh) = 0;

   DEFMETHOD_(BOOL,GetFileTime) (HANDLE hFile,   LPFILETIME lpCreationTime,
                   LPFILETIME lpLastAccessTime, LPFILETIME lpLastWriteTime) = 0;

   DEFMETHOD_(BOOL,SetFileTime) (HANDLE hFile,   CONST FILETIME *lpCreationTime,
                  CONST FILETIME *lpLastAccessTime,
                  CONST FILETIME *lpLastWriteTime) = 0;

   // CreateFileMapping: dwMaximumSizeHigh/Low are correct Win32 convention for 64-bit sizes
   DEFMETHOD_(HANDLE,CreateFileMapping)   (HANDLE hFile=0,
                                 LPSECURITY_ATTRIBUTES lpFileMappingAttributes=0,
                                 DWORD flProtect=PAGE_READONLY,
                                 DWORD dwMaximumSizeHigh=0,
                                 DWORD dwMaximumSizeLow=0,
                                 LPCTSTR lpName=0) = 0;

   // FIX: dwNumberOfBytesToMap must be SIZE_T (64-bit on x64) — was DWORD, causing
   // pointer truncation and the 0x0000000054d80000 access violation
   DEFMETHOD_(LPVOID,MapViewOfFile)      (HANDLE hFileMappingObject,
                                 DWORD  dwDesiredAccess=FILE_MAP_READ,
                                 DWORD  dwFileOffsetHigh=0,
                                 DWORD  dwFileOffsetLow=0,
                                 SIZE_T dwNumberOfBytesToMap=0) = 0;

   DEFMETHOD_(BOOL,UnmapViewOfFile)      (LPCVOID lpBaseAddress) = 0;

   DEFMETHOD_(HANDLE,FindFirstFile) (LPCTSTR lpFileName, LPWIN32_FIND_DATA lpFindFileData) = 0;

   DEFMETHOD_(BOOL,FindNextFile) (HANDLE hFindFile, LPWIN32_FIND_DATA lpFindFileData) = 0;

   DEFMETHOD_(BOOL,FindClose) (HANDLE hFindFile) = 0;

   DEFMETHOD_(BOOL,CreateDirectory) (LPCTSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes=0) = 0;

   DEFMETHOD_(BOOL,RemoveDirectory) (LPCTSTR lpPathName) = 0;

   DEFMETHOD_(DWORD,GetCurrentDirectory) (DWORD nBufferLength, LPTSTR lpBuffer) = 0;

   DEFMETHOD_(BOOL,SetCurrentDirectory) (LPCTSTR lpPathName) = 0;

   DEFMETHOD_(BOOL,DeleteFile)  (LPCTSTR lpFileName) = 0;

   DEFMETHOD_(BOOL,CopyFile)    (LPCTSTR lpExistingFileName, LPCTSTR lpNewFileName, BOOL bFailIfExists) = 0;

   DEFMETHOD_(BOOL,MoveFile) (LPCTSTR lpExistingFileName, LPCTSTR lpNewFileName) = 0;

   DEFMETHOD_(DWORD,GetFileAttributes) (LPCTSTR lpFileName) = 0;

   DEFMETHOD_(BOOL,SetFileAttributes) (LPCTSTR lpFileName, DWORD dwFileAttributes) = 0;

   DEFMETHOD_(DWORD,GetLastError) (void) = 0;

   //---------------
   // IFileSystem extensions to WIN32 system
   //---------------

   DEFMETHOD_(HANDLE,OpenChild) (DAFILEDESC *lpDesc) = 0;

   // FIX: GetFilePosition returns DWORD (low 32 bits), pPositionHigh gets upper 32 bits
   // matches SetFilePointer convention — callers should use both for files > 4GB
   DEFMETHOD_(DWORD,GetFilePosition) (HANDLE hFile = 0, PLONG pPositionHigh=0) = 0;

   // FIX: lBufferSize should be SIZE_T to handle large path buffers safely
   DEFMETHOD_(SIZE_T,GetFileName) (LPSTR lpBuffer, SIZE_T lBufferSize) = 0;

   DEFMETHOD_(DWORD,GetAccessType) (void) = 0;

   DEFMETHOD(GetParentSystem) (LPFILESYSTEM *lplpFileSystem) = 0;

   DEFMETHOD(SetPreference)  (DWORD dwNumber, DWORD  dwValue) = 0;

   DEFMETHOD(GetPreference)  (DWORD dwNumber, PDWORD pdwValue) = 0;

   // FIX: nNumberOfBytesToRead and dwStartOffset should be SIZE_T for large directory extensions
   DEFMETHOD(ReadDirectoryExtension) (HANDLE hFile, LPVOID lpBuffer,
                                        SIZE_T nNumberOfBytesToRead,
                                        LPDWORD lpNumberOfBytesRead=0, SIZE_T dwStartOffset=0) = 0;

   DEFMETHOD(WriteDirectoryExtension) (HANDLE hFile, LPCVOID lpBuffer,
                                        SIZE_T nNumberOfBytesToWrite,
                                        LPDWORD lpNumberOfBytesWritten=0, SIZE_T dwStartOffset=0) = 0;

   DEFMETHOD_(LONG,SerialCall) (LPFILESYSTEM lpSystem, DAFILE_SERIAL_PROC lpProc, void *lpContext) = 0;

   DEFMETHOD_(BOOL,GetAbsolutePath) (char *lpOutput, LPCTSTR lpInput, LONG lSize) = 0;
};

#endif