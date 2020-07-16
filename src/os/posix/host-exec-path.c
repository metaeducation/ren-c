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

#ifndef __cplusplus
    // See feature_test_macros(7)
    // This definition is redundant under C++
    #define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#if defined(HAVE_PROC_PATHNAME)
#include <sys/sysctl.h>
#endif

#include "reb-host.h"

#ifndef PATH_MAX
#define PATH_MAX 4096  // generally lacking in Posix
#endif

//
//  OS_Get_Current_Exec: C
//
// Return the current executable path as a FILE!
//
// https://stackoverflow.com/questions/1023306/
//
REBVAL *OS_Get_Current_Exec(void)
{
  #if !defined(PROC_EXEC_PATH) && !defined(HAVE_PROC_PATHNAME)
    return rebBlank();
  #else
    char *buffer;
    const char *self;
      #if defined(PROC_EXEC_PATH)
        buffer = NULL;
        self = PROC_EXEC_PATH;
      #else //HAVE_PROC_PATHNAME
        int mib[4] = {
            CTL_KERN,
            KERN_PROC,
            KERN_PROC_PATHNAME,
            -1 //current process
        };
        buffer = rebAllocN(char, PATH_MAX + 1);
        size_t len = PATH_MAX + 1;
        if (sysctl(mib, sizeof(mib), buffer, &len, NULL, 0) != 0) {
            rebFree(buffer);
            return rebBlank();
        }
        self = buffer;
    #endif

    char *path_utf8 = rebAllocN(char, PATH_MAX);
    int r = readlink(self, path_utf8, PATH_MAX);

    if (buffer)
        rebFree(buffer);

    if (r < 0) {
        rebFree(path_utf8);
        return rebBlank();
    }

    path_utf8[r] = '\0';

    REBVAL *result = rebValue(
        "local-to-file", rebT(path_utf8),
        rebEND
    );
    rebFree(path_utf8);
    return result;
  #endif
}
