//
//  File: %host-browse.c
//  Summary: "Browser Launch Host"
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
// This provides the ability to launch a web browser or file
// browser on the host.
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


#ifndef PATH_MAX
#define PATH_MAX 4096  // generally lacking in Posix
#endif

void OS_Destroy_Graphics(void);


//
//  OS_Get_Current_Dir: C
//
// Return the current directory path as a FILE!.  The result should be freed
// with rebRelease().
//
REBVAL *OS_Get_Current_Dir(void)
{
    char *path = rebAllocN(char, PATH_MAX);

    if (getcwd(path, PATH_MAX - 1) == 0) {
        rebFree(path);
        return rebBlank();
    }

    REBVAL *result = rebValue(
        "local-to-file/dir", rebT(path),
        rebEND
    );

    rebFree(path);
    return result;
}


//
//  OS_Set_Current_Dir: C
//
// Set the current directory to local path.  Return false on failure.
//
bool OS_Set_Current_Dir(const REBVAL *path)
{
    char *path_utf8 = rebSpell("file-to-local/full", path, rebEND);

    int chdir_result = chdir(path_utf8);

    rebFree(path_utf8);

    return chdir_result == 0;
}
