/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Title: GOB Hostkit Facilities
**  Purpose:
**      @HostileFork doesn't particularly like the way GOB! is done,
**      and feels it's an instance of a more general need for external
**      types that participate in Rebol's type system and garbage
**      collector.  For now these routines are kept together here.
**
***********************************************************************/

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


#ifndef REB_CORE
REBSER* Gob_To_Image(REBGOB *gob);
#endif

//
//  OS_GOB_To_Image: C
// 
// Render a GOB into an image. Returns an image or zero if
// it cannot be done.
//
REBSER *OS_GOB_To_Image(REBGOB *gob)
{
#if (defined REB_CORE)
    return 0;
#else
    return Gob_To_Image(gob);
#endif
}

//
//  As_OS_Str: C
// 
// If necessary, convert a string series to platform specific format.
// (Handy for GOB/TEXT handling).
// If the string series is empty the resulting string is set to NULL
// 
// Function returns:
//     TRUE - if the resulting string needs to be deallocated by the caller code
//     FALSE - if REBOL string is used (no dealloc needed)
// 
// Note: REBOL strings are allowed to contain nulls.
//
REBOOL As_OS_Str(REBSER *series, REBCHR **string)
{
    int n;
    void *str;
    REBCNT len;

    if ((n = RL_Get_String(series, 0, &str)) < 0) {
        // Latin1 byte string - use as is
        *string = cast(char*, str);
        return FALSE;
    }

    len = n;

    //empty string check
    if (len == 0) { /* shortcut */
        *string = OS_ALLOC_ARRAY(REBCHR, 1);
        *string[0] = '\0';
    } else {
        //convert to UTF8
        REBCNT utf8_len = RL_Length_As_UTF8(str, len, TRUE, FALSE);
        *string = OS_ALLOC_ARRAY(char, utf8_len + 1);
        RL_Encode_UTF8(b_cast(*string), utf8_len, str, &len, TRUE, FALSE);
        (*string)[utf8_len] = '\0';
    }
    return TRUE;
}
