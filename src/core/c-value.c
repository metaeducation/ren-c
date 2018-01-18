//
//  File: %c-value.c
//  Summary: "Generic REBVAL Support Services and Debug Routines"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
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
// These are mostly DEBUG-build routines to support the macros and definitions
// in %sys-value.h.
//
// These are not specific to any given type.  For the type-specific REBVAL
// code, see files with names like %t-word.c, %t-logic.c, %t-integer.c...
//

#include "sys-core.h"


#if !defined(NDEBUG)

//
//  Panic_Value_Debug: C
//
// This is a debug-only "error generator", which will hunt through all the
// series allocations and panic on the series that contains the value (if
// it can find it).  This will allow those using Address Sanitizer or
// Valgrind to know a bit more about where the value came from.
//
// Additionally, if it happens to be a void or trash, LOGIC!, BAR!, or NONE!
// it will dump out where the initialization happened if that information
// was stored.
//
ATTRIBUTE_NO_RETURN void Panic_Value_Debug(const RELVAL *v) {
    fflush(stdout);
    fflush(stderr);

    REBNOD *containing = Try_Find_Containing_Node_Debug(v);

    switch (VAL_TYPE_RAW(v)) {
    case REB_MAX_VOID:
    case REB_BLANK:
    case REB_LOGIC:
    case REB_BAR:
        printf(
            "REBVAL init on tick #%d at %s:%d\n",
            cast(unsigned int, v->extra.tick),
            v->payload.track.file,
            v->payload.track.line
        );
        fflush(stdout);
        break;

    default:
        break;
    }

    printf("Kind=%d\n", cast(int, VAL_TYPE_RAW(v)));
    fflush(stdout);

    if (containing != NULL && NOT(containing->header.bits & NODE_FLAG_CELL)) {
        printf("Containing series for value pointer found, panicking it:\n");
        Panic_Series_Debug(SER(containing));
    }

    if (containing != NULL) {
        printf("Containing pairing for value pointer found, panicking it:\n");
        Panic_Series_Debug(cast(REBSER*, containing)); // won't pass SER()
    }

    printf("No containing series for value...panicking to make stack dump:\n");
    Panic_Series_Debug(SER(EMPTY_ARRAY));
}


//
//  VAL_SPECIFIC_Debug: C
//
REBCTX *VAL_SPECIFIC_Debug(const REBVAL *v)
{
    assert(
        VAL_TYPE(v) == REB_0_REFERENCE
        || ANY_WORD(v)
        || ANY_ARRAY(v)
        || IS_VARARGS(v)
        || IS_FUNCTION(v)
        || ANY_CONTEXT(v)
    );

    REBCTX *specific = VAL_SPECIFIC_COMMON(v);

    if (AS_SPECIFIER(specific) != SPECIFIED) {
        //
        // Basic sanity check: make sure it's a context at all
        //
        if (NOT_SER_FLAG(CTX_VARLIST(specific), ARRAY_FLAG_VARLIST)) {
            printf("Non-CONTEXT found as specifier in specific value\n");
            panic (specific); // may not be a series, either
        }

        // While an ANY-WORD! can be bound specifically to an arbitrary
        // object, an ANY-ARRAY! only becomes bound specifically to frames.
        // The keylist for a frame's context should come from a function's
        // paramlist, which should have a FUNCTION! value in keylist[0]
        //
        if (ANY_ARRAY(v))
            assert(IS_FUNCTION(CTX_ROOTKEY(specific)));
    }

    return specific;
}


#ifdef CPLUSPLUS_11
//
// This destructor checks to make sure that any cell that was created via
// DECLARE_LOCAL got properly initialized.
//
Reb_Specific_Value::~Reb_Specific_Value ()
{
    assert(header.bits & NODE_FLAG_CELL);

    enum Reb_Kind kind = VAL_TYPE_RAW(this);
    assert(
        header.bits & NODE_FLAG_FREE
            ? kind == REB_MAX_PLUS_ONE_TRASH
            : kind <= REB_MAX_VOID
    );
}
#endif

//
//  Assert_No_Relative: C
//
// Check to make sure there are no relative values in an array, maybe deeply.
//
// !!! What if you have an ANY-ARRAY! inside your array at a position N,
// but there is a relative value in the VAL_ARRAY() of that value at an
// index earlier than N?  This currently considers that an error since it
// checks the whole array...which is more conservative (asserts on more
// cases).  But should there be a flag to ask to honor the index?
//
void Assert_No_Relative(REBARR *array, REBU64 types)
{
    RELVAL *v;
    for (v = ARR_HEAD(array); NOT_END(v); ++v) {
        if (IS_RELATIVE(v)) {
            printf("Array contained relative item and wasn't supposed to\n");
            panic (v);
        }
        if (IS_UNREADABLE_IF_DEBUG(v))
            continue;
        if (types & FLAGIT_KIND(VAL_TYPE(v)) & TS_ARRAYS_OBJ)
             Assert_No_Relative(VAL_ARRAY(v), types);
    }
}


//
//  Probe_Core_Debug: C
//
// Use PROBE() to invoke, see notes there.
//
void* Probe_Core_Debug(
    const void *p,
    const char *file,
    int line
) {
    const struct Reb_Header *h = cast(const struct Reb_Header*, p);

    printf("\n** PROBE() ");
    printf("tick %d %s:%d\n", cast(int, TG_Tick), file, line);

    fflush(stdout);
    fflush(stderr);

    if (h->bits & NODE_FLAG_CELL)
        Debug_Fmt("%r\n", cast(const REBVAL*, p));
    else {
        REBSER *s = m_cast(REBSER*, cast(const REBSER*, p));

        // Invalid series would possibly (but not necessarily) crash the print
        // routines--which are the same ones used to output a series normally.
        // Hence don't attempt to print a known malformed series.  A more
        // pointed message will probably come from ASSERT_SERIES, saying
        // what is wrong rather than just crashing the print code...
        //
        ASSERT_SERIES(s);

        if (GET_SER_FLAG(s, ARRAY_FLAG_VARLIST))
            Debug_Fmt("%r\n", CTX_VALUE(CTX(s)));
        else {
            REBOOL disabled = GC_Disabled;
            GC_Disabled = TRUE;

            // This routine is also a little catalog of the outlying series
            // types in terms of sizing, just to know what they are.

            if (BYTE_SIZE(s))
                Debug_Str(s_cast(BIN_HEAD(s)));
            else if (GET_SER_FLAG(s, SERIES_FLAG_ARRAY)) {
                //
                // May not actually be a REB_BLOCK, but we put it in a value
                // container for now saying it is so we can output it.  May
                // not want to Manage_Series here, so we use a raw
                // initialization instead of Init_Block.
                //
                DECLARE_LOCAL (value);
                VAL_RESET_HEADER(value, REB_BLOCK);
                INIT_VAL_ARRAY(value, ARR(s));
                VAL_INDEX(value) = 0;

                Debug_Fmt("%r", value);
            }
            else if (SER_WIDE(s) == sizeof(REBUNI))
                Debug_Uni(s);
            else if (s == PG_Canons_By_Hash) {
                printf("can't probe PG_Canons_By_Hash\n");
                panic (s);
            }
            else if (s == GC_Guarded) {
                printf("can't probe GC_Guarded\n");
                panic (s);
            }
            else
                panic (s);

            assert(GC_Disabled == TRUE);
            GC_Disabled = disabled;
        }
    }

    return m_cast(void*, p); // must be cast back to const if source was const
}

#endif
