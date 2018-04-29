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
      #if defined(DEBUG_TRACK_CELLS)
        printf("REBVAL init ");

        #if defined(DEBUG_TRACK_CELLS) && defined(DEBUG_COUNT_TICKS)
            printf("on tick #%d", cast(unsigned int, v->extra.tick));
        #endif

        printf("at %s:%d\n", v->payload.track.file, v->payload.track.line);
      #else
        printf("No track info (see DEBUG_TRACK_CELLS/DEBUG_COUNT_TICKS)\n");
      #endif
        fflush(stdout);
        break;

    default:
        break;
    }

    printf("Kind=%d\n", cast(int, VAL_TYPE_RAW(v)));
    fflush(stdout);

    if (containing != NULL and NOT_CELL(containing)) {
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
        or ANY_WORD(v)
        or ANY_ARRAY(v)
        or IS_VARARGS(v)
        or IS_ACTION(v)
        or ANY_CONTEXT(v)
    );

    REBCTX *specific = VAL_SPECIFIC_COMMON(v);

    if (SPC(specific) != SPECIFIED) {
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
        // paramlist, which should have an ACTION! value in keylist[0]
        //
        if (ANY_ARRAY(v))
            assert(IS_ACTION(CTX_ROOTKEY(specific)));
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

#endif // !defined(NDEBUG)


#ifdef DEBUG_HAS_PROBE

inline static void Probe_Print_Helper(
    const void *p,
    const char *label,
    const char *file,
    int line
){
    printf("\n**PROBE(%s, %p): ", label, p);
  #ifdef DEBUG_COUNT_TICKS
    printf("tick %d ", cast(int, TG_Tick));
  #endif
    printf("%s:%d\n", file, line);

    fflush(stdout);
    fflush(stderr);
}


inline static void Probe_Molded_Value(const REBVAL *v)
{
    DECLARE_MOLD (mo);
    Push_Mold(mo);
    Mold_Value(mo, v);

    printf("%s\n", s_cast(BIN_AT(mo->series, mo->start)));
    fflush(stdout);

    Drop_Mold(mo);
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
){
    REBOOL was_disabled = GC_Disabled;
    GC_Disabled = TRUE;

    switch (Detect_Rebol_Pointer(p)) {
    case DETECTED_AS_UTF8:
        Probe_Print_Helper(p, "C String", file, line);
        printf("\"%s\"\n", cast(const char*, p));
        fflush(stdout);
        break;

    case DETECTED_AS_SERIES: {
        REBSER *s = m_cast(REBSER*, cast(const REBSER*, p));

        ASSERT_SERIES(s); // if corrupt, gives better info than a print crash

        if (GET_SER_FLAG(s, ARRAY_FLAG_VARLIST)) {
            Probe_Print_Helper(p, "Context Varlist", file, line);
            Probe_Molded_Value(CTX_ARCHETYPE(CTX(s)));
        }
        else {
            // This routine is also a little catalog of the outlying series
            // types in terms of sizing, just to know what they are.

            if (SER_WIDE(s) == sizeof(REBYTE)) {
                Probe_Print_Helper(p, "Byte-Size Series", file, line);

                DECLARE_LOCAL (value);
                RESET_VAL_HEADER(value, REB_BINARY);
                INIT_VAL_SERIES(value, s);
                VAL_INDEX(value) = 0;

                Probe_Molded_Value(value);
            }
            else if (SER_WIDE(s) == sizeof(REBUNI)) {
                Probe_Print_Helper(p, "REBWCHAR-Size Series", file, line);

                DECLARE_LOCAL (value);
                RESET_VAL_HEADER(value, REB_STRING);
                INIT_VAL_SERIES(value, s);
                VAL_INDEX(value) = 0;

                Probe_Molded_Value(value);
            }
            else if (GET_SER_FLAG(s, SERIES_FLAG_ARRAY)) {
                Probe_Print_Helper(p, "Array", file, line);

                // May not actually be a REB_BLOCK, but we put it in a value
                // container for now saying it is so we can output it.  May
                // not want to Manage_Series here, so we use a raw
                // initialization instead of Init_Block.
                //
                DECLARE_LOCAL (block);
                RESET_VAL_HEADER(block, REB_BLOCK);
                INIT_VAL_ARRAY(block, ARR(s));
                VAL_INDEX(block) = 0;

                Probe_Molded_Value(block);
            }
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

        }
        break; }

    case DETECTED_AS_FREED_SERIES:
        Probe_Print_Helper(p, "Freed Series", file, line);
        panic (p);

    case DETECTED_AS_VALUE: {
        Probe_Print_Helper(p, "Value", file, line);
        Probe_Molded_Value(cast(const REBVAL*, p));
        break; }

    case DETECTED_AS_END:
        Probe_Print_Helper(p, "END", file, line);
        break;

    case DETECTED_AS_TRASH_CELL:
        Probe_Print_Helper(p, "Trash Cell", file, line);
        panic (p);
    }

    assert(GC_Disabled == TRUE);
    GC_Disabled = was_disabled;

    return m_cast(void*, p); // must be cast back to const if source was const
}

#endif // defined(DEBUG_HAS_PROBE)
