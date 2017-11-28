//
//  File: %host-exec-path.c
//  Summary: "Executable Path"
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
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

// Should include <mach-o/dyld.h>, but it conflicts with reb-c.h because both defined TRUE and FALSE
#ifdef __cplusplus
extern "C"
#endif
int _NSGetExecutablePath(char* buf, uint32_t* bufsize);

#include "reb-host.h"


//
//  OS_Get_Current_Exec: C
//
// Return the current executable path as a STRING!.  The result should be
// freed with rebRelease()
//
REBVAL *OS_Get_Current_Exec(void)
{
    uint32_t path_size = 1024;

    char *path_utf8 = OS_ALLOC_N(char, path_size);
    if (path_utf8 == NULL)
        return rebBlank();

    int r = _NSGetExecutablePath(path_utf8, &path_size);
    if (r == -1) {
        // buffer is too small, length is set to the required size
        assert(path_size > 1024);

        OS_FREE(path_utf8);
        path_utf8 = OS_ALLOC_N(char, path_size);
        if (path_utf8 == NULL)
            return rebBlank();

        int r = _NSGetExecutablePath(path_utf8, &path_size);
        if (r != 0) {
            OS_FREE(path_utf8);
            return rebBlank();
        }
    }

    // _NSGetExecutablePath returns "a path" not a "real path", and it could
    // be a symbolic link.
    //
    const REBOOL is_dir = FALSE;
    char *resolved_path_utf8 = realpath(path_utf8, NULL);
    if (resolved_path_utf8 != NULL) {
        //
        // resolved_path needs to be free'd by free, which might be different
        // from OS_FREE.
        //
        REBVAL *result = rebLocalToFile(resolved_path_utf8, is_dir);
        OS_FREE(path_utf8);
        free(resolved_path_utf8);
        return result;
    }
    else {
        // Failed to resolve, just return the unresolved path.
        //
        REBVAL *result = rebLocalToFile(path_utf8, is_dir);
        OS_FREE(path_utf8);
        return result;
    }
}
