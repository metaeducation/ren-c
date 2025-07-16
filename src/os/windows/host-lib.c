//
//  file: %host-lib.c
//  summary:{OS API function library called by REBOL interpreter}
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This module is parsed for function declarations used to
// build prototypes, tables, and other definitions. To change
// function arguments requires a rebuild of the REBOL library.
//
// This module provides the functions that REBOL calls
// to interface to the native (host) operating system.
// REBOL accesses these functions through the structure
// defined in host-lib.h (auto-generated, do not modify).
//
// compile with -DUNICODE for Win32 wide char API
//
//=////////////////////////////////////////////////////////////////////////=//
//
// WARNING: The function declarations here cannot be modified without also
// modifying those found in the other OS host-lib files!  Do not even modify
// the argument names.
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <process.h>
#include <assert.h>

#include "reb-host.h"


//
//  Convert_Date: C
//
// Convert local format of system time into standard date
// and time structure.
//
Value* Convert_Date(long zone, const SYSTEMTIME *stime)
{
    return rebValue("ensure date! (make-date-ymdsnz",
        rebI(stime->wYear), // year
        rebI(stime->wMonth), // month
        rebI(stime->wDay), // day
        rebI(
            stime->wHour * 3600 + stime->wMinute * 60 + stime->wSecond
        ), // "secs"
        rebI(1000000 * stime->wMilliseconds), // nano
        rebI(zone), // zone
    ")");
}


/***********************************************************************
**
**  OS Library Functions
**
***********************************************************************/


//
//  OS_Get_Time: C
//
// Get the current system date/time in UTC plus zone offset (mins).
//
Value* OS_Get_Time(void)
{
    SYSTEMTIME stime;
    TIME_ZONE_INFORMATION tzone;

    GetSystemTime(&stime);

    if (TIME_ZONE_ID_DAYLIGHT == GetTimeZoneInformation(&tzone))
        tzone.Bias += tzone.DaylightBias;

    return Convert_Date(-tzone.Bias, &stime);
}


//
//  OS_Delta_Time: C
//
// Return time difference in microseconds. If base = 0, then
// return the counter. If base != 0, compute the time difference.
//
// Note: Requires high performance timer.
//      Q: If not found, use timeGetTime() instead ?!
//
int64_t OS_Delta_Time(int64_t base)
{
    LARGE_INTEGER time;
    if (not QueryPerformanceCounter(&time))
        rebJumps("crash {Missing high performance timer}");

    if (base == 0) return time.QuadPart; // counter (may not be time)

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    return ((time.QuadPart - base) * 1000) / (freq.QuadPart / 1000);
}


//
//  OS_Get_Current_Dir: C
//
// Return the current directory path as a FILE!.  Result should be freed
// with rebRelease()
//
Value* OS_Get_Current_Dir(void)
{
    DWORD len = GetCurrentDirectory(0, nullptr);  // length, incl terminator.
    WCHAR *path = rebAllocN(WCHAR, len);
    GetCurrentDirectory(len, path);

    Value* result = rebValue("local-to-file/dir", rebR(rebTextWide(path)));
    rebFree(path);
    return result;
}


//
//  OS_Set_Current_Dir: C
//
// Set the current directory to local path.  Return false on failure.
//
bool OS_Set_Current_Dir(const Value* path)
{
    WCHAR *path_wide = rebSpellW("file-to-local/full", path);

    BOOL success = SetCurrentDirectory(path_wide);

    rebFree(path_wide);

    return success == TRUE;
}


//
//  OS_File_Time: C
//
// Convert file.time to REBOL date/time format.
// Time zone is UTC.
//
Value* OS_File_Time(struct devreq_file *file)
{
    SYSTEMTIME stime;
    TIME_ZONE_INFORMATION tzone;

    if (TIME_ZONE_ID_DAYLIGHT == GetTimeZoneInformation(&tzone))
        tzone.Bias += tzone.DaylightBias;

    FileTimeToSystemTime(cast(FILETIME *, &file->time), &stime);
    return Convert_Date(-tzone.Bias, &stime);
}


//
//  OS_Open_Library: C
//
// Load a DLL library and return the handle to it.
// If zero is returned, error indicates the reason.
//
void *OS_Open_Library(const Value* path)
{
    // While often when communicating with the OS, the local path should be
    // fully resolved, the LoadLibraryW() function searches DLL directories by
    // default.  So if %foo is passed in, you don't want to prepend the
    // current dir to make it absolute, because it will *only* look there.
    //
    WCHAR *path_utf8 = rebSpellW("file-to-local", path);

    void *dll = LoadLibraryW(path_utf8);

    rebFree(path_utf8);

    if (not dll)
        rebPanic_OS (GetLastError());

    return dll;
}


//
//  OS_Close_Library: C
//
// Free a DLL library opened earlier.
//
void OS_Close_Library(void *dll)
{
    FreeLibrary((HINSTANCE)dll);
}


//
//  OS_Find_Function: C
//
// Get a DLL function address from its string name.
//
CFUNC *OS_Find_Function(void *dll, const char *funcname)
{
    // !!! See notes about data pointers vs. function pointers in the
    // definition of CFUNC.  This is trying to stay on the right side
    // of the specification, but OS APIs often are not standard C.  So
    // this implementation is not guaranteed to work, just to suppress
    // compiler warnings.  See:
    //
    //      http://stackoverflow.com/a/1096349/211160

    FARPROC fp = GetProcAddress((HMODULE)dll, funcname);

    //DWORD err = GetLastError();

    return cast(CFUNC*, fp);
}


//
//  OS_Reap_Process: C
//
// pid:
//      > 0, a single process
//      -1, any child process
// flags:
//      0: return immediately
//
//      Return -1 on error
//
int OS_Reap_Process(int pid, int *status, int flags)
{
    UNUSED(pid);
    UNUSED(status);
    UNUSED(flags);

    // !!! It seems that process doesn't need to be reaped on Windows
    return 0;
}


//
//  OS_Get_Current_Exec: C
//
// Return the current executable path as a FILE!.  The result should be freed
// with rebRelease()
//
Value* OS_Get_Current_Exec(void)
{
    WCHAR *path = rebAllocN(WCHAR, MAX_PATH);

    DWORD r = GetModuleFileName(nullptr, path, MAX_PATH);
    if (r == 0) {
        rebFree(path);
        return rebBlank();
    }
    path[r] = '\0'; // May not be nullptr-terminated if buffer is not big enough

    Value* result = rebValue("local-to-file", rebR(rebTextWide(path)));
    rebFree(path);

    return result;
}
