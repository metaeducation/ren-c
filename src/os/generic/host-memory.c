//
//  File: %host-memory.c
//  Summary: "Host Memory Allocator"
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
// See notes about OS_ALLOC and OS_FREE in make-os-ext.r
//

#include <stdlib.h>
#include <assert.h>

#include "reb-host.h"


//
//  OS_Alloc_Mem: C
//
// Allocate memory of given size.
//
// This is necessary because some environments may use their
// own specific memory allocation (e.g. private heaps).
//
void *OS_Alloc_Mem(size_t size)
{
#ifdef NDEBUG
    return malloc(size);
#else
    // We skew the return pointer so we don't return exactly at the malloc
    // point, to prevent free() from being used directly on an address
    // acquired from OS_Alloc_Mem.  And because Rebol Core uses the same
    // trick (but stores a positive integral size), we write a negative
    // magic number.
    //
    // A 64-bit size is used in order to maintain a 64-bit alignment
    // (potentially a lesser alignment guarantee than malloc())

    void *ptr = malloc(size + sizeof(REBI64));
    *cast(REBI64*, ptr) = -1020;
    return cast(char*, ptr) + sizeof(REBI64);
#endif
}


//
//  OS_Free_Mem: C
//
// Free memory allocated in this OS environment. (See OS_Alloc_Mem)
//
void OS_Free_Mem(void *mem)
{
#ifdef NDEBUG
    free(mem);
#else
    char *ptr = cast(char *, mem) - sizeof(REBI64);
    if (*cast(REBI64*, ptr) != -1020) {
        rebPanic(
            "OS_Free_Mem() mismatched with allocator!"
            " Did you mean to use FREE() instead of OS_FREE()?"
        );
    }
    free(ptr);
#endif
}
