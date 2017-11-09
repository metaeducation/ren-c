//
//  File: %host-lib.c
//  Summary: {OS API function library called by REBOL interpreter}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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

#ifndef REB_CORE
REBSER* Gob_To_Image(REBGOB *gob);
#endif


//
//  Convert_Date: C
//
// Convert local format of system time into standard date
// and time structure.
//
void Convert_Date(REBVAL *out, long zone, const SYSTEMTIME *stime)
{
    rebInitDate(
        out,
        stime->wYear, // year
        stime->wMonth, // month
        stime->wDay, // day
        stime->wHour * 3600 + stime->wMinute * 60 + stime->wSecond, // "time"
        1000000 * stime->wMilliseconds, // nano
        zone
    );
}


/***********************************************************************
**
**  OS Library Functions
**
***********************************************************************/


//
//  OS_Config: C
//
// Return a specific runtime configuration parameter.
//
REBINT OS_Config(int id, REBYTE *result)
{
    UNUSED(result);

#define OCID_STACK_SIZE 1  // needs to move to .h file

    switch (id) {
    case OCID_STACK_SIZE:
        return 0;  // (size in bytes should be returned here)
    }

    return 0;
}


//
//  OS_Exit: C
//
// Called in cases where REBOL needs to quit immediately
// without returning from the main() function.
//
void OS_Exit(int code)
{
    //OS_Call_Device(RDI_STDIO, RDC_CLOSE); // close echo
    OS_Quit_Devices(0);
#ifndef REB_CORE
    OS_Destroy_Graphics();
#endif
    exit(code);
}


//
//  OS_Crash: C
//
// Tell user that REBOL has crashed. This function must use
// the most obvious and reliable method of displaying the
// crash message.
//
// If the title is NULL, then REBOL is running in a server mode.
// In that case, we do not want the crash message to appear on
// the screen, because the system may be unattended.
//
// On some systems, the error may be recorded in the system log.
//
void OS_Crash(const REBYTE *title, const REBYTE *content)
{
    // Echo crash message if echo file is open:
    ///PUTE(content);
    OS_Call_Device(RDI_STDIO, RDC_CLOSE); // close echo

    // A title tells us we should alert the user:
    if (title) {
    //  OS_Put_Str(title);
    //  OS_Put_Str(":\n");
        // Use ASCII only
        MessageBoxA(NULL, cs_cast(content), cs_cast(title), MB_ICONHAND);
    }
    //  OS_Put_Str(content);
    exit(100);
}



//
//  OS_Get_Time: C
//
// Get the current system date/time in UTC plus zone offset (mins).
//
void OS_Get_Time(REBVAL *out)
{
    SYSTEMTIME stime;
    TIME_ZONE_INFORMATION tzone;

    GetSystemTime(&stime);

    if (TIME_ZONE_ID_DAYLIGHT == GetTimeZoneInformation(&tzone))
        tzone.Bias += tzone.DaylightBias;

    Convert_Date(out, -tzone.Bias, &stime);
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
i64 OS_Delta_Time(i64 base)
{
    LARGE_INTEGER time;
    if (!QueryPerformanceCounter(&time))
        OS_Crash(cb_cast("Missing resource"), cb_cast("High performance timer"));

    if (base == 0) return time.QuadPart; // counter (may not be time)

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    return ((time.QuadPart - base) * 1000) / (freq.QuadPart / 1000);
}


//
//  OS_Get_Current_Dir: C
//
// Return the current directory path as a string and
// its length in chars (not bytes).
//
// The result should be freed after copy/conversion.
//
int OS_Get_Current_Dir(REBCHR **path)
{
    int len;

    len = GetCurrentDirectory(0, NULL); // length, incl terminator.
    *path = OS_ALLOC_N(wchar_t, len);
    GetCurrentDirectory(len, *path);
    len--; // less terminator

    return len;
}


//
//  OS_Set_Current_Dir: C
//
// Set the current directory to local path. Return FALSE
// on failure.
//
REBOOL OS_Set_Current_Dir(REBCHR *path)
{
    return SetCurrentDirectory(path);
}


//
//  OS_File_Time: C
//
// Convert file.time to REBOL date/time format.
// Time zone is UTC.
//
void OS_File_Time(REBVAL *out, struct devreq_file *file)
{
    SYSTEMTIME stime;
    TIME_ZONE_INFORMATION tzone;

    if (TIME_ZONE_ID_DAYLIGHT == GetTimeZoneInformation(&tzone))
        tzone.Bias += tzone.DaylightBias;

    FileTimeToSystemTime(cast(FILETIME *, &file->time), &stime);
    Convert_Date(out, -tzone.Bias, &stime);
}


//
//  OS_Open_Library: C
//
// Load a DLL library and return the handle to it.
// If zero is returned, error indicates the reason.
//
void *OS_Open_Library(const REBCHR *path, REBCNT *error)
{
    void *dll = LoadLibraryW(path);
    *error = GetLastError();

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
//  OS_GOB_To_Image: C
//
// Render a GOB into an image. Returns an image or zero if
// it cannot be done.
//
REBVAL *OS_GOB_To_Image(REBGOB *gob)
{
#if (defined REB_CORE)
    UNUSED(gob);
    return 0;
#else
    return Gob_To_Image(gob);
#endif
}

//
//  OS_Get_Current_Exec: C
//
// Return the current executable path as a string and
// its length in chars (not bytes).
//
// The result should be freed after copy/conversion.
//
int OS_Get_Current_Exec(REBCHR **path)
{
    DWORD r = 0;
    *path = NULL;
    *path = OS_ALLOC_N(REBCHR, MAX_PATH);
    if (*path == NULL) return -1;

    r = GetModuleFileName(NULL, *path, MAX_PATH);
    if (r == 0) {
        OS_FREE(*path);
        return -1;
    }
    (*path)[r] = '\0'; //It might not be NULL-terminated if buffer is not big enough

    return r;
}
