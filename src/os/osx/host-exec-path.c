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

// Should include <mach-o/dyld.h> ?
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

    char *path_utf8 = rebAllocN(char, path_size);

    int r = _NSGetExecutablePath(path_utf8, &path_size);
    if (r == -1) { // buffer is too small
        assert(path_size > 1024); // path_size should now hold needed size

        rebFree(path_utf8);
        path_utf8 = rebAllocN(char, path_size);

        int r = _NSGetExecutablePath(path_utf8, &path_size);
        if (r != 0) {
            rebFree(path_utf8);
            return rebBlank();
        }
    }

    // Note: _NSGetExecutablePath returns "a path" not a "real path", and it
    // could be a symbolic link.

    char *resolved_path_utf8 = realpath(path_utf8, NULL);
    if (resolved_path_utf8) {
        REBVAL *result = rebValue(
            "local-to-file", rebT(resolved_path_utf8),
            rebEND
        );
        rebFree(path_utf8);
        free(resolved_path_utf8); // NOTE: realpath() uses malloc()
        return result;
    }

    REBVAL *result = rebValue(
        "local-to-file", rebT(path_utf8), // just return unresolved path
        rebEND
    );
    rebFree(path_utf8);
    return result;
}
