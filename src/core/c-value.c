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


#if defined(DEBUG_FANCY_PANIC)  // !!! Separate setting for Dump routines?

//
//  Dump_Value_Debug: C
//
// Returns the containing node.
//
REBNOD *Dump_Value_Debug(const RELVAL *v)
{
    fflush(stdout);
    fflush(stderr);

    REBNOD *containing = Try_Find_Containing_Node_Debug(v);

  #if defined(DEBUG_TRACK_EXTEND_CELLS)
    printf("REBVAL init");

    printf(" @ tick #%d", cast(unsigned int, v->tick));
    if (v->touch != 0)
        printf(" @ touch #%d", cast(unsigned int, v->touch));

    printf(" @ %s:%ld\n", v->file, cast(unsigned long, v->line));
  #else
    printf("- no track info (see DEBUG_TRACK_EXTEND_CELLS)\n");
  #endif
    fflush(stdout);

    printf("kind_byte=%d\n", cast(int, KIND3Q_BYTE_UNCHECKED(v)));

    enum Reb_Kind kind = CELL_KIND(VAL_UNESCAPED(v));
    const char *type = STR_UTF8(Canon(SYM_FROM_KIND(kind)));
    printf("cell_kind=%s\n", type);
    fflush(stdout);

    if (GET_CELL_FLAG(v, FIRST_IS_NODE))
        printf("has first node: %p\n", cast(void*, VAL_NODE1(v)));
    if (GET_CELL_FLAG(v, SECOND_IS_NODE))
        printf("has second node: %p\n", cast(void*, VAL_NODE2(v)));

    if (not containing)
        return nullptr;

    if (not Is_Node_Cell(containing)) {
        printf(
            "Containing series for value pointer found, %p:\n",
            cast(void*, containing)
        );
    }
    else{
        printf(
            "Containing pairing for value pointer found %p:\n",
            cast(void*, containing)
        );
    }

    return containing;
}


//
//  Panic_Value_Debug: C
//
// This is a debug-only "error generator", which will hunt through all the
// series allocations and panic on the series that contains the value (if
// it can find it).  This will allow those using Address Sanitizer or
// Valgrind to know a bit more about where the value came from.
//
// Additionally, it can dump out where the initialization happened if that
// information was stored.  See DEBUG_TRACK_EXTEND_CELLS.
//
ATTRIBUTE_NO_RETURN void Panic_Value_Debug(const RELVAL *v) {
    REBNOD *containing = Dump_Value_Debug(v);

    if (containing) {
        printf("Panicking the containing REBSER...\n");
        Panic_Series_Debug(SER(containing));
    }

    printf("No containing series for value, panicking for stack dump:\n");
    Panic_Series_Debug(EMPTY_ARRAY);
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
// Use PROBE() to invoke from code; this gives more information like line
// numbers, and in the C++ build will return the input (like the PROBE native
// function does).
//
// Use Probe() to invoke from the C debugger (non-macro, single-arity form).
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
            if (IS_SER_UTF8(s)) {
                if (IS_SYMBOL(s))
                    Probe_Print_Helper(p, expr, "WORD! series", file, line);
                else
                    Probe_Print_Helper(p, expr, "STRING! series", file, line);
                Mold_Text_Series_At(mo, STR(s), 0);  // or could be TAG!, etc.
            }
            else {
                REBBIN *bin = BIN(s);
                Probe_Print_Helper(p, expr, "Byte-Size Series", file, line);

                // !!! Duplication of code in MF_Binary
                //
                const bool brk = (BIN_LEN(bin) > 32);
                Append_Ascii(mo->series, "#{");
                Form_Base16(mo, BIN_HEAD(bin), BIN_LEN(bin), brk);
                Append_Ascii(mo->series, "}");
            }
        }
        else if (IS_SER_ARRAY(s)) {
            if (IS_VARLIST(s)) {
                Probe_Print_Helper(p, expr, "Context Varlist", file, line);
                Probe_Molded_Value(CTX_ARCHETYPE(CTX(s)));
            }
            else {
                Probe_Print_Helper(p, expr, "Array", file, line);
                Mold_Array_At(mo, ARR(s), 0, "[]"); // not necessarily BLOCK!
            }
        }
        else if (IS_KEYLIST(s)) {
            assert(SER_WIDE(s) == sizeof(REBKEY));  // ^-- or is byte size
            Probe_Print_Helper(p, expr, "Keylist Series", file, line);
            const REBKEY *tail = SER_TAIL(REBKEY, s);
            const REBKEY *key = SER_HEAD(REBKEY, s);
            Append_Ascii(mo->series, "<< ");
            for (; key != tail; ++key) {
                Mold_Text_Series_At(mo, KEY_SYMBOL(key), 0);
                Append_Codepoint(mo->series, ' ');
            }
            Append_Ascii(mo->series, ">>");
        }
        else if (s == PG_Symbols_By_Hash) {
            printf("can't probe PG_Symbols_By_Hash (TBD: add probing)\n");
        }
        else if (s == GC_Guarded) {
            printf("can't probe GC_Guarded (TBD: add probing)\n");
        }
        else
            panic (s);
        break; }

      case DETECTED_AS_FREED_SERIES:
        Probe_Print_Helper(p, expr, "Freed Series", file, line);
        panic (p);

      case DETECTED_AS_CELL: {
        const REBVAL *v = cast(const REBVAL*, p);

      #if defined(DEBUG_UNREADABLE_TRASH)
        if (IS_TRASH(v)) {  // IS_NULLED() asserts on trash
            Probe_Print_Helper(p, expr, "Value", file, line);
            Append_Ascii(mo->series, "~trash~");
            break;
        }
      #endif

        Probe_Print_Helper(p, expr, "Value", file, line);
        if (IS_NULLED(v)) {
            Append_Ascii(mo->series, "; null");
        }
        else if (IS_BAD_WORD(v)) {
            Mold_Value(mo, v);
            if (GET_CELL_FLAG(v, ISOTOPE))
                Append_Ascii(mo->series, "  ; isotope");
        }
        else
            Mold_Value(mo, v);
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
