//
//  File: %library-windows.c
//  Summary: {OS API function library called by REBOL interpreter}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
#include <windows.h>
#undef IS_ERROR
#undef OUT  // %minwindef.h defines this, we have a better use for it
#undef VOID  // %winnt.h defines this, we have a better use for it


#include <process.h>
#include <assert.h>

#include "sys-core.h"


//
//  Open_Library: C
//
// Load a DLL library and return the handle to it.
// If zero is returned, error indicates the reason.
//
void *Open_Library(const REBVAL *path)
{
    // While often when communicating with the OS, the local path should be
    // fully resolved, the LoadLibraryW() function searches DLL directories by
    // default.  So if %foo is passed in, you don't want to prepend the
    // current dir to make it absolute, because it will *only* look there.
    //
    WCHAR *path_utf8 = rebSpellWide("file-to-local", path);

    void *dll = LoadLibraryW(path_utf8);

    rebFree(path_utf8);

    if (not dll)
        rebFail_OS (GetLastError());

    return dll;
}


//
//  Close_Library: C
//
// Free a DLL library opened earlier.
//
void Close_Library(void *dll)
{
    FreeLibrary((HINSTANCE)dll);
}


//
//  Find_Function: C
//
// Get a DLL function address from its string name.
//
CFUNC *Find_Function(void *dll, const char *funcname)
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
