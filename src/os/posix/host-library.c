//
//  File: %host-library.c
//  Summary: "POSIX Library-related functions"
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
// This is for support of the LIBRARY! type from the host on
// systems that support 'dlopen'.
//

#ifndef __cplusplus
    // See feature_test_macros(7)
    // This definition is redundant under C++
    #define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <poll.h>
#include <fcntl.h>              /* Obtain O_* constant definitions */
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "reb-host.h"


#ifndef NO_DL_LIB
#include <dlfcn.h>
#endif


//
//  OS_Open_Library: C
//
// Load a DLL library and return the handle to it.
// If zero is returned, error indicates the reason.
//
void *OS_Open_Library(const REBVAL *path)
{
#ifndef NO_DL_LIB
    //
    // While often when communicating with the OS, the local path should be
    // fully resolved, the dlopen() function searches library directories by
    // default.  So if %foo is passed in, you don't want to prepend the
    // current dir to make it absolute, because it will only look there.
    //
    const REBOOL full = FALSE;
    char *path_utf8 = rebFileToLocalAlloc(NULL, path, full);

    void *dll = dlopen(path_utf8, RTLD_LAZY/*|RTLD_GLOBAL*/);

    OS_FREE(path_utf8);

    if (dll == NULL)
        rebFail (dlerror()); // dlerror() returns a const char*

    return dll;
#else
    return 0;
#endif
}


//
//  OS_Close_Library: C
//
// Free a DLL library opened earlier.
//
void OS_Close_Library(void *dll)
{
#ifndef NO_DL_LIB
    dlclose(dll);
#endif
}


//
//  OS_Find_Function: C
//
// Get a DLL function address from its string name.
//
CFUNC *OS_Find_Function(void *dll, const char *funcname)
{
#ifndef NO_DL_LIB
    // !!! See notes about data pointers vs. function pointers in the
    // definition of CFUNC.  This is trying to stay on the right side
    // of the specification, but OS APIs often are not standard C.  So
    // this implementation is not guaranteed to work, just to suppress
    // compiler warnings.  See:
    //
    //      http://stackoverflow.com/a/1096349/211160

    CFUNC *fp;
    *cast(void**, &fp) = dlsym(dll, funcname);
    return fp;
#else
    return NULL;
#endif
}
