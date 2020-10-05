//
//  File: %c-value.c
//  Summary: "Generic REBVAL Support Services and Debug Routines"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
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
// Additionally, if it happens to be NULLED, VOID!, LOGIC!, BAR!, BLANK!, or
// a trash cell, it will dump out where the initialization happened if that
// information was stored.  (See DEBUG_TRACK_EXTEND_CELLS for more intense
// debugging scenarios, which track all cell types, but at greater cost.)
//
ATTRIBUTE_NO_RETURN void Panic_Value_Debug(const RELVAL *v) {
    fflush(stdout);
    fflush(stderr);

    REBNOD *containing = Try_Find_Containing_Node_Debug(v);

    switch (KIND_BYTE_UNCHECKED(v)) {
      case REB_NULLED:
      case REB_VOID:
      case REB_BLANK:
      #if defined(DEBUG_TRACK_CELLS)
        printf("REBVAL init");

        #if defined(DEBUG_TRACK_EXTEND_CELLS)
            #if defined(DEBUG_COUNT_TICKS)
                printf(" @ tick #%d", cast(unsigned int, v->tick));
                if (v->touch != 0)
                    printf(" @ touch #%d", cast(unsigned int, v->touch));
            #endif

            printf(" @ %s:%d\n", v->track.file, v->track.line);
        #else
            #if defined(DEBUG_COUNT_TICKS)
                printf(" @ tick #%d", cast(unsigned int, v->extra.tick));
            #endif

            printf(
                " @ %s:%d\n", PAYLOAD(Track, v).file, PAYLOAD(Track, v).line
            );
        #endif
      #else
        printf("- no track info (see DEBUG_TRACK_CELLS/DEBUG_COUNT_TICKS)\n");
      #endif
        fflush(stdout);
        break;

      default:
        break;
    }

    printf("kind_byte=%d\n", cast(int, KIND_BYTE_UNCHECKED(v)));
    fflush(stdout);

    if (containing and not (containing->header.bits & NODE_FLAG_CELL)) {
        printf("Containing series for value pointer found, panicking it:\n");
        Panic_Series_Debug(SER(containing));
    }

    if (containing) {
        printf("Containing pairing for value pointer found, panicking it:\n");
        Panic_Series_Debug(cast(REBSER*, containing)); // won't pass SER()
    }

    printf("No containing series for value...panicking to make stack dump:\n");
    Panic_Series_Debug(SER(EMPTY_ARRAY));
}

#endif // !defined(NDEBUG)


#ifdef DEBUG_HAS_PROBE

inline static void Probe_Print_Helper(
    const void *p,  // the REBVAL*, REBSER*, or UTF-8 char*
    const char *expr,  // stringified contents of the PROBE() macro
    const char *label,  // detected type of `p` (see %rebnod.h)
    const char *file,  // file where this PROBE() was invoked
    int line  // line where this PROBE() was invoked
){
    printf("\n-- (%s)=0x%p : %s", expr, p, label);
  #ifdef DEBUG_COUNT_TICKS
    printf(" : tick %d", cast(int, TG_Tick));
  #endif
    printf(" %s @%d\n", file, line);

    fflush(stdout);
    fflush(stderr);
}


inline static void Probe_Molded_Value(const REBVAL *v)
{
    DECLARE_MOLD (mo);
    Push_Mold(mo);
    Mold_Value(mo, v);

    printf("%s\n", cast(const char*, STR_AT(mo->series, mo->index)));
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
    const char *expr,
    const char *file,
    int line
){
    DECLARE_MOLD (mo);
    Push_Mold(mo);

    bool was_disabled = GC_Disabled;
    GC_Disabled = true;

    if (not p) {
        Probe_Print_Helper(p, expr, "C nullptr", file, line);
    }
    else switch (Detect_Rebol_Pointer(p)) {
      case DETECTED_AS_UTF8:
        Probe_Print_Helper(p, expr, "C String", file, line);
        printf("\"%s\"\n", cast(const char*, p));
        break;

      case DETECTED_AS_SERIES: {
        REBSER *s = m_cast(REBSER*, cast(const REBSER*, p));

        ASSERT_SERIES(s); // if corrupt, gives better info than a print crash

        // This routine is also a little catalog of the outlying series
        // types in terms of sizing, just to know what they are.

        if (SER_WIDE(s) == sizeof(REBYTE)) {
            if (GET_SERIES_FLAG(s, IS_STRING)) {
                REBSTR *str = STR(s);
                if (IS_STR_SYMBOL(str))
                    Probe_Print_Helper(p, expr, "WORD! series", file, line);
                else
                    Probe_Print_Helper(p, expr, "STRING! series", file, line);
                Mold_Text_Series_At(mo, str, 0);  // or could be TAG!, etc.
            }
            else {
                Probe_Print_Helper(p, expr, "Byte-Size Series", file, line);

                // !!! Duplication of code in MF_Binary
                //
                const bool brk = (BIN_LEN(s) > 32);
                Append_Ascii(mo->series, "#{");
                Form_Base16(mo, BIN_HEAD(s), BIN_LEN(s), brk);
                Append_Ascii(mo->series, "}");
            }
        }
        else if (IS_SER_ARRAY(s)) {
            if (GET_ARRAY_FLAG(s, IS_VARLIST)) {
                Probe_Print_Helper(p, expr, "Context Varlist", file, line);
                Probe_Molded_Value(CTX_ARCHETYPE(CTX(s)));
            }
            else {
                Probe_Print_Helper(p, expr, "Array", file, line);
                Mold_Array_At(mo, ARR(s), 0, "[]"); // not necessarily BLOCK!
            }
        }
        else if (s == PG_Canons_By_Hash) {
            printf("can't probe PG_Canons_By_Hash (TBD: add probing)\n");
            panic (s);
        }
        else if (s == GC_Guarded) {
            printf("can't probe GC_Guarded (TBD: add probing)\n");
            panic (s);
        }
        else
            panic (s);
        break; }

      case DETECTED_AS_FREED_SERIES:
        Probe_Print_Helper(p, expr, "Freed Series", file, line);
        panic (p);

      case DETECTED_AS_CELL: {
        const REBVAL *v = cast(const REBVAL*, p);

      #if !defined(NDEBUG)  // IS_PARAM() etc. would crash on unreadable void
        if (IS_UNREADABLE_DEBUG(v)) {
            Probe_Print_Helper(p, expr, "Value", file, line);
            Append_Ascii(mo->series, "\\\\Unreadable VOID!\\\\");
            break;
        }
      #endif

        if (IS_PARAM(v)) {
            Probe_Print_Helper(p, expr, "Param Cell", file, line);

            const REBSTR *spelling = VAL_KEY_SPELLING(v);
            Append_Ascii(mo->series, "(");
            Append_Utf8(
                mo->series,
                STR_UTF8(spelling),
                STR_SIZE(spelling)
            );
            Append_Ascii(mo->series, ") ");
            Append_Ascii(mo->series, "..."); // probe types?
        }
        else {
            Probe_Print_Helper(p, expr, "Value", file, line);
            if (IS_NULLED(v))
                Append_Ascii(mo->series, "; null");
            else
                Mold_Value(mo, v);
        }
        break; }

      case DETECTED_AS_END:
        Probe_Print_Helper(p, expr, "END", file, line);
        break;

      case DETECTED_AS_FREED_CELL:
        Probe_Print_Helper(p, expr, "Freed Cell", file, line);
        panic (p);
    }

    if (mo->offset != STR_LEN(mo->series))
        printf("%s\n", cast(const char*, STR_AT(mo->series, mo->index)));
    fflush(stdout);

    Drop_Mold(mo);

    assert(GC_Disabled);
    GC_Disabled = was_disabled;

    return m_cast(void*, p); // must be cast back to const if source was const
}


// Version with fewer parameters, useful to call from the C debugger (which
// cannot call macros like PROBE())
//
void Probe(const void *p)
  { Probe_Core_Debug(p, "C debug", "N/A", 0); }

#endif // defined(DEBUG_HAS_PROBE)
