//
//  File: %c-value.c
//  Summary: "Generic Value Cell Support Services and Debug Routines"
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
// These are not specific to any given type.  For the type-specific cell
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
// Additionally, if it happens to be NULLED, NOTHING, LOGIC!, VOID!, BLANK!, or
// it will dump out where the initialization happened if that information was
// stored.  (See DEBUG_TRACK_EXTEND_CELLS for more intense debugging scenarios,
// which track all cell types, but at greater cost.)
//
ATTRIBUTE_NO_RETURN void Panic_Value_Debug(const Cell* v) {
    fflush(stdout);
    fflush(stderr);

    Node* containing = Try_Find_Containing_Node_Debug(v);

    switch (VAL_TYPE_RAW(v)) {
    case REB_MAX_NULLED:
    case REB_BLANK:
    case REB_LOGIC:
      #if defined(DEBUG_TRACK_CELLS)
        printf("Cell init ");

        #if defined(DEBUG_TRACK_EXTEND_CELLS)
            #if defined(DEBUG_COUNT_TICKS)
                printf("@ tick #%d", cast(unsigned int, v->tick));
                if (v->touch != 0)
                    printf("@ touch #%d", cast(unsigned int, v->touch));
            #endif

            printf("@ %s:%d\n", v->track.file, v->track.line);
        #else
            #if defined(DEBUG_COUNT_TICKS)
                printf("@ tick #%d", cast(unsigned int, v->extra.tick));
            #endif

            printf("@ %s:%d\n", v->payload.track.file, v->payload.track.line);
        #endif
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

    if (containing and Is_Node_A_Stub(containing)) {
        printf("Containing series for value pointer found, panicking it:\n");
        Panic_Flex_Debug(cast_Flex(containing));
    }

    if (containing) {
        printf("Containing pairing for value pointer found, panicking it:\n");
        Panic_Value_Debug(VAL(containing));  // won't pass cast_Flex()
    }

    printf("No containing series for value...panicking to make stack dump:\n");
    Panic_Flex_Debug(EMPTY_ARRAY);
}

#endif // !defined(NDEBUG)


#ifdef DEBUG_HAS_PROBE

INLINE void Probe_Print_Helper(
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


INLINE void Probe_Molded_Value(const Value* v)
{
    DECLARE_MOLDER (mo);
    Push_Mold(mo);
    Mold_Value(mo, v);

    printf("%s\n", s_cast(Binary_At(mo->utf8flex, mo->start)));
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
    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    bool was_disabled = GC_Disabled;
    GC_Disabled = true;

    if (not p) {

        Probe_Print_Helper(p, "C nullptr", file, line);

    } else switch (Detect_Rebol_Pointer(p)) {

    case DETECTED_AS_UTF8:
        Probe_Print_Helper(p, "C String", file, line);
        printf("\"%s\"\n", cast(const char*, p));
        break;

    case DETECTED_AS_SERIES: {
        Flex* s = m_cast(Flex*, cast(const Flex*, p));

        Assert_Flex(s); // if corrupt, gives better info than a print crash

        // This routine is also a little catalog of the outlying series
        // types in terms of sizing, just to know what they are.

        if (Get_Flex_Flag(s, UTF8_SYMBOL)) {
            assert(Flex_Wide(s) == sizeof(Byte));
            Probe_Print_Helper(p, "Symbol Flex", file, line);
            Symbol* sym = cast(Symbol*, m_cast(void*, p));

            const char *head = Symbol_Head(sym);  // UTF-8
            size_t size = Symbol_Size(sym);  // number of UTF-8 bytes

            Append_Utf8_Utf8(mo->utf8flex, head, size);
        }
        else if (Flex_Wide(s) == sizeof(Byte)) {
            Probe_Print_Helper(p, "Byte-Size Flex", file, line);
            Binary* bin = cast(Binary*, m_cast(void*, p));

            // !!! Duplication of code in MF_Binary
            //
            const bool brk = (Binary_Len(bin) > 32);
            Binary* enbased = Encode_Base16(
                Binary_Head(bin),
                Binary_Len(bin),
                brk
            );
            Append_Unencoded(mo->utf8flex, "#{");
            Append_Utf8_Utf8(
                mo->utf8flex,
                cs_cast(Binary_Head(enbased)), Binary_Len(enbased)
            );
            Append_Unencoded(mo->utf8flex, "}");
            Free_Unmanaged_Flex(enbased);
        }
        else if (Flex_Wide(s) == sizeof(Ucs2Unit)) {
            Probe_Print_Helper(p, "REBWCHAR-Size Flex", file, line);
            String* str = cast(String*, m_cast(void*, p));

            Mold_Text_Series_At(mo, str, 0); // might be TAG! etc, not TEXT!
        }
        else if (Is_Flex_Array(s)) {
            if (Get_Array_Flag(s, IS_VARLIST)) {
                Probe_Print_Helper(p, "Context Varlist", file, line);
                Probe_Molded_Value(Varlist_Archetype(CTX(s)));
            }
            else {
                Probe_Print_Helper(p, "Array Flex", file, line);
                Mold_Array_At(mo, cast_Array(s), 0, "[]"); // not necessarily BLOCK!
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

    case DETECTED_AS_FREED_FLEX:
        Probe_Print_Helper(p, "Freed Flex", file, line);
        panic (p);

    case DETECTED_AS_CELL: {
        Probe_Print_Helper(p, "Value", file, line);
        Mold_Value(mo, cast(const Value*, p));
        break; }

    case DETECTED_AS_END:
        Probe_Print_Helper(p, "END", file, line);
        break;

    case DETECTED_AS_FREED_CELL:
        Probe_Print_Helper(p, "Freed Cell", file, line);
        panic (p);
    }

    if (mo->start != Flex_Len(mo->utf8flex))
        printf("%s\n", s_cast(Binary_At(mo->utf8flex, mo->start)));
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
  { Probe_Core_Debug(p, "N/A", 0); }


#endif // defined(DEBUG_HAS_PROBE)
