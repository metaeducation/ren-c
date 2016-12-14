//
//  File: %reb-defs.h
//  Summary: "Miscellaneous structures and definitions"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
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
// This file is used by internal and external C code.  It should not depend
// on any other include files before it.
//
// If REB_DEF is defined, it expects full definitions of the structures behind
// REBVAL and REBSER.  If not, then it treats them opaquely.  The reason this
// is done in a single file with an #ifdef as opposed to just doing the
// opaque definitions in %reb-ext.h (and not including %reb-defs.h there) is
// because of %a-lib.c - which wants to use the non-opaque definitions to
// implement the API while still having the various enums in %reb-ext.h
// available to the compiler.
//

#ifndef REB_DEFS_H  // due to sequences within the lib build itself
#define REB_DEFS_H

//
// Forward declarations of the series subclasses defined in %sys-series.h
// Because the Reb_Series structure includes a Reb_Value by value, it
// must be included *after* %sys-value.h
//
#ifdef REB_DEF
    struct Reb_Value;
    #define RELVAL struct Reb_Value // maybe IS_RELATIVE()

    #ifdef __cplusplus
        #define REBVAL struct Reb_Specific_Value // guaranteed IS_SPECIFIC()
    #else
        #define REBVAL struct Reb_Value // IS_SPECIFIC(), unchecked
    #endif

    struct Reb_Series; // Rebol series node
    typedef struct Reb_Series REBSER;

    // UTF-8 Everywhere series (used for WORD!s only ATM)
    typedef REBSER REBSTR;

    struct Reb_Array; // REBSER containing REBVALs ("Rebol Array")
    typedef struct Reb_Array REBARR;

    struct Reb_Context; // parallel REBARR key/var arrays + ANY-CONTEXT! value
    typedef struct Reb_Context REBCTX;

    struct Reb_Func;  // function parameters + FUNCTION! value
    typedef struct Reb_Func REBFUN;

    struct Reb_Map; // REBARR listing key/value pairs with hash
    typedef struct Reb_Map REBMAP;

    struct Reb_Frame; // Non-GC'd raw call frame, see %sys-frame.h
    typedef struct Reb_Frame REBFRM;

    struct Reb_Binder; // used as argument in %tmp-funcs.h, needs forward decl

    struct Reb_Path_Value_State;
    typedef struct Reb_Path_Value_State REBPVS;

    // A standard integer is currently used to represent the data stack
    // pointer.  `unsigned int` instead of a `REBCNT` in order to leverage the
    // native performance of the integer type unconstrained by bit size, as
    // data stack pointers are not stored in REBVALs or similar, and
    // performance in comparing and manipulation is more important than size.
    //
    // Note that a value of 0 indicates an empty stack; the [0] entry is made
    // to be alerting trash to trap invalid reads or writes of empty stacks.
    //
    typedef unsigned int REBDSP;
    struct Reb_Chunk;
    struct Reb_Chunker;
#else
    // The %reb-xxx.h files define structures visible to host code (client)
    // which don't also require pulling in all of the %sys-xxx.h files and
    // dependencies.  Some of these definitions are shared with the core,
    // and mention things like REBSER.  When building as core that's fine,
    // but when building as host this will be undefined unless something
    // is there.  Define as a void so that it can point at it, but not know
    // anything else about it (including size).
    //
    typedef void REBSER;
    typedef void REBARR;
    typedef void REBOBJ;
    typedef void REBSTR;
    typedef void REBFRM;

    // !!! The previous definition of RXIARG let them be stack-instantiated,
    // and as such their size needed to be known.  However, the API version
    // of REBVAL* is seeking to use GC cells, not stack ones.  This is a
    // stopgap until all the routines are changed to speak in pointers,
    // so callers can allocate stack storage in the meantime...
    //
    typedef struct {
        char opaque[sizeof(REBUPT) * 4];
    } REBVAL;
#endif


struct Reb_Header {
    REBUPT bits;
};

#endif
