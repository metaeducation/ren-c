//
//  File: %c-value.c
//  Summary: "Generic Value Cell Support Services and Debug Routines"
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
// in %sys-cell.h.
//
// These are not specific to any given type.  For the type-specific cell
// code, see files with names like %t-word.c, %t-logic.c, %t-integer.c...
//

#include "sys-core.h"


#if DEBUG_FANCY_PANIC  // !!! Separate setting for Dump routines?

//
//  Dump_Value_Debug: C
//
// Returns the containing node.
//
Node* Dump_Value_Debug(const Cell* v)
{
    fflush(stdout);
    fflush(stderr);

    Node* containing = Try_Find_Containing_Node_Debug(v);

  #if DEBUG_TRACK_EXTEND_CELLS
    printf("Cell init");

    printf(" @ tick #%d", cast(unsigned int, v->tick));
    if (v->touch != 0)
        printf(" @ touch #%d", cast(unsigned int, v->touch));

    printf(" @ %s:%ld\n", v->file, cast(unsigned long, v->line));
  #else
    printf("- no track info (see DEBUG_TRACK_EXTEND_CELLS)\n");
  #endif
    fflush(stdout);

    Heart heart = Cell_Heart(v);
    const char *type = String_UTF8(Canon_Symbol(SYM_FROM_KIND(heart)));
    printf("cell_heart=%s\n", type);
    fflush(stdout);
    printf("quote_byte=%d\n", QUOTE_BYTE(v));
    fflush(stdout);

    if (Cell_Has_Node1(v))
        printf("has node1: %p\n", cast(void*, Cell_Node1(v)));
    if (Cell_Has_Node2(v))
        printf("has node2: %p\n", cast(void*, Cell_Node2(v)));

    if (not containing)
        return nullptr;

    if (Is_Node_A_Stub(containing)) {
        printf(
            "Containing Flex for value pointer found, %p:\n",
            cast(void*, containing)
        );
    }
    else{
        printf(
            "Containing Pairing for value pointer found %p:\n",
            cast(void*, containing)
        );
    }

    return containing;
}


//
//  Panic_Value_Debug: C
//
// This is a debug-only "error generator", which will hunt through all the
// Flex allocations and panic on the Flex that contains the value (if
// it can find it).  This will allow those using Address Sanitizer or
// Valgrind to know a bit more about where the value came from.
//
// Additionally, it can dump out where the initialization happened if that
// information was stored.  See DEBUG_TRACK_EXTEND_CELLS.
//
ATTRIBUTE_NO_RETURN void Panic_Value_Debug(const Cell* v) {
    Node* containing = Dump_Value_Debug(v);

    if (containing) {
        printf("Panicking the containing Flex...\n");
        Panic_Flex_Debug(cast(Flex*, containing));
    }

    printf("No containing Flex for value, panicking for stack dump:\n");
    Panic_Flex_Debug(EMPTY_ARRAY);
}

#endif // !defined(NDEBUG)


#if DEBUG_HAS_PROBE

INLINE void Probe_Print_Helper(
    const void *p,  // the Value*, Flex*, or UTF-8 char*
    const char *expr,  // stringified contents of the PROBE() macro
    const char *label,  // detected type of `p` (see %rebnod.h)
    const char *file,  // file where this PROBE() was invoked
    int line  // line where this PROBE() was invoked
){
    printf(
        "\n-- (%s)=0x%p : %s : TICK %" PRIu64 " %s LINE %d\n",
        expr,
        p,
        label,
        TICK,  // 0 if not DEBUG_COUNT_TICKS
        file,
        line
    );

    fflush(stdout);
    fflush(stderr);
}


INLINE void Probe_Molded_Value(const Value* v)
{
    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    if (Is_Antiform(v)) {
        DECLARE_VALUE (temp);
        Copy_Cell(temp, v);
        Element* elem = Quasify_Antiform(temp);
        Mold_Element(mo, elem);
        Append_Ascii(mo->string, "  ; anti");
    }
    else {
        Mold_Element(mo, c_cast(Element*, v));
    }

    printf("%s\n", c_cast(char*, Binary_At(mo->string, mo->base.size)));
    fflush(stdout);

    Drop_Mold(mo);
}

void Probe_Cell_Print_Helper(
  Molder* mo,
  const void *p,
  const char *expr,
  const char *file,
  int line
){
    Probe_Print_Helper(p, expr, "Value", file, line);

    const Atom* atom = c_cast(Value*, p);

    if (Is_Cell_Unreadable(atom)) {  // Is_Nulled() asserts on unreadables
        Append_Ascii(mo->string, "\\\\unreadable\\\\");
        return;
    }

    if (Is_Cell_Poisoned(atom)) {
        Append_Ascii(mo->string, "**POISONED CELL**");
    }
    else if (Is_Antiform(atom)) {
        DECLARE_ELEMENT (reified);
        Copy_Meta_Cell(reified, atom);
        Mold_Element(mo, reified);
        Append_Ascii(mo->string, "  ; anti");
    }
    else
        Mold_Element(mo, cast(const Element*, atom));
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
    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    bool was_disabled = g_gc.disabled;
    g_gc.disabled = true;

    if (not p) {
        Probe_Print_Helper(p, expr, "C nullptr", file, line);
        goto cleanup;
    }
    else switch (Detect_Rebol_Pointer(p)) {
      case DETECTED_AS_UTF8:
        if (*c_cast(Byte*, p) == '\0')
            Probe_Print_Helper(
                p,
                expr,
                "Empty UTF-8 String or Is_Cell_Erased() / Is_Stub_Erased()",
                file, line
              );
        else {
            Probe_Print_Helper(p, expr, "UTF-8 String", file, line);
            printf("\"%s\"\n", c_cast(char*, p));
        }
        goto cleanup;

      case DETECTED_AS_CELL:
        Probe_Cell_Print_Helper(mo, p, expr, file, line);
        goto cleanup;

      case DETECTED_AS_END:
        Probe_Print_Helper(p, expr, "rebEND Signal (192)", file, line);
        goto cleanup;

      case DETECTED_AS_STUB:
        break;  // lots of possibilities, break to handle

      case DETECTED_AS_FREE:
        Probe_Print_Helper(p, expr, "Freed PoolUnit (193)", file, line);
        goto cleanup;
    }

    // If we didn't jump to cleanup above, it's a Flex.  New switch().

  blockscope {
    const Flex* f = c_cast(Flex* , p);
    assert(Is_Node_Readable(f));
    Flavor flavor = Stub_Flavor(f);
    Assert_Flex(f);  // if corrupt, gives better info than a print crash

    switch (flavor) {
      case REB_0:
        Probe_Print_Helper(p, expr, "!!! CORRUPT Flex !!!", file, line);
        break;

    //=//// ARRAY FLEXES //////////////////////////////////////////////////=//

      case FLAVOR_SOURCE:
        Probe_Print_Helper(p, expr, "Generic Array", file, line);
        Mold_Array_At(mo, cast(const Array*, f), 0, "[]");
        break;

      case FLAVOR_VARLIST:  // currently same as FLAVOR_PARAMLIST
        Probe_Print_Helper(p, expr, "Varlist (or Paramlist)", file, line);
        Probe_Molded_Value(Varlist_Archetype(x_cast(VarList*, f)));
        break;

      case FLAVOR_DETAILS:
        Probe_Print_Helper(p, expr, "Details", file, line);
        MF_Frame(
            mo,
            Phase_Archetype(cast(Phase*, cast(Action*, m_cast(void*, p)))),
            false
        );
        break;

      case FLAVOR_PAIRLIST:
        Probe_Print_Helper(p, expr, "Pairlist", file, line);
        break;

      case FLAVOR_PATCH:
        Probe_Print_Helper(p, expr, "Module Item Patch", file, line);
        break;

      case FLAVOR_LET: {
        Probe_Print_Helper(p, expr, "LET single variable", file, line);
        Append_Spelling(mo->string, INODE(LetSymbol, f));
        break; }

      case FLAVOR_USE: {
        Probe_Print_Helper(p, expr, "Virtual Bind USE", file, line);
        break; }

      case FLAVOR_HITCH:
        Probe_Print_Helper(p, expr, "Hitch", file, line);
        break;

      case FLAVOR_PARTIALS:
        Probe_Print_Helper(p, expr, "Partials", file, line);
        break;

      case FLAVOR_LIBRARY:
        Probe_Print_Helper(p, expr, "Library", file, line);
        break;

      case FLAVOR_HANDLE:
        Probe_Print_Helper(p, expr, "Handle", file, line);
        break;

      case FLAVOR_DATASTACK:
        Probe_Print_Helper(p, expr, "Datastack", file, line);
        break;

      case FLAVOR_FEED:
        Probe_Print_Helper(p, expr, "Feed", file, line);
        break;

      case FLAVOR_API:
        Probe_Print_Helper(p, expr, "API Handle", file, line);
        break;

      case FLAVOR_INSTRUCTION_SPLICE:
        Probe_Print_Helper(p, expr, "Splicing Instruction", file, line);
        break;

    //=//// FLEXES WITH ELEMENTS sizeof(void*) ////////////////////////////=//

      case FLAVOR_KEYLIST: {
        assert(Flex_Wide(f) == sizeof(Key));  // ^-- or is byte size
        Probe_Print_Helper(p, expr, "KeyList Flex", file, line);
        const Key* tail = Flex_Tail(Key, f);
        const Key* key = Flex_Head(Key, f);
        Append_Ascii(mo->string, "<< ");
        for (; key != tail; ++key) {
            Mold_Text_Flex_At(mo, Key_Symbol(key), 0);
            Append_Codepoint(mo->string, ' ');
        }
        Append_Ascii(mo->string, ">>");
        break; }

      case FLAVOR_POINTER:
        Probe_Print_Helper(p, expr, "Flex of void*", file, line);
        break;

      case FLAVOR_CANONTABLE:
        Probe_Print_Helper(p, expr, "Canon Table", file, line);
        break;

      case FLAVOR_NODELIST:  // e.g. GC protect list
        Probe_Print_Helper(p, expr, "Flex of Node*", file, line);
        break;

      case FLAVOR_FLEXLIST:  // e.g. manually allocated Flex* list
        Probe_Print_Helper(p, expr, "Flex of Flex*", file, line);
        break;

      case FLAVOR_MOLDSTACK:
        Probe_Print_Helper(p, expr, "Mold Stack", file, line);
        break;

    //=//// FLEXES WITH ELEMENTS sizeof(REBLEN) ///////////////////////////=//

      case FLAVOR_HASHLIST:
        Probe_Print_Helper(p, expr, "Hashlist", file, line);
        break;

    //=//// FLEXES WITH ELEMENTS sizeof(Bookmark) /////////////////////////=//

      case FLAVOR_BOOKMARKLIST:
        Probe_Print_Helper(p, expr, "BookmarkList", file, line);
        break;

    //=//// FLEXES WITH ELEMENTS WIDTH 1 //////////////////////////////////=//

      case FLAVOR_BINARY: {
        const Binary* b = cast(const Binary*, f);
        Probe_Print_Helper(p, expr, "Byte-Size Flex", file, line);

        const bool brk = (Binary_Len(b) > 32);  // !!! duplicates MF_Binary code
        Append_Ascii(mo->string, "#{");
        Form_Base16(mo, Binary_Head(b), Binary_Len(b), brk);
        Append_Ascii(mo->string, "}");
        break; }

    //=//// FLEXES WITH ELEMENTS WIDTH 1 INTERPRETED AS UTF-8 /////////////=//

      case FLAVOR_NONSYMBOL: {
        Probe_Print_Helper(p, expr, "Non-Symbol String Flex", file, line);
        Mold_Text_Flex_At(mo, c_cast(String*, f), 0);  // could be TAG!, etc.
        break; }

      case FLAVOR_SYMBOL: {
        Probe_Print_Helper(p, expr, "Interned (Symbol) Flex", file, line);
        Mold_Text_Flex_At(mo, c_cast(Symbol*, f), 0);
        break; }

      case FLAVOR_THE_GLOBAL_INACCESSIBLE: {
        Probe_Print_Helper(p, expr, "Global Inaccessible Stub", file, line);
        break; }

      default:
        Probe_Print_Helper(p, expr, "!!! Unknown Stub_Flavor() !!!", file, line);
        break;
    }
  }

  cleanup:

    if (mo->base.size != String_Size(mo->string))
        printf("%s\n", c_cast(char*, Binary_At(mo->string, mo->base.size)));
    fflush(stdout);

    Drop_Mold(mo);

    assert(g_gc.disabled);
    g_gc.disabled = was_disabled;

    return m_cast(void*, p); // must be cast back to const if source was const
}


// Version with fewer parameters, useful to call from the C debugger (which
// cannot call macros like PROBE())
//
void Probe(const void *p)
  { Probe_Core_Debug(p, "C debug", "N/A", 0); }


//
//  Where_Core_Debug: C
//
void Where_Core_Debug(Level* L) {
    if (FEED_IS_VARIADIC(L->feed))
        Reify_Variadic_Feed_As_Array_Feed(L->feed, false);

    REBLEN index = FEED_INDEX(L->feed);

    if (index > 0) {
        DECLARE_MOLDER (mo);
        SET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT);
        mo->limit = 40 * 20;  // 20 lines of length 40, or so?

        REBLEN before_index = index > 3 ? index - 3 : 0;
        Push_Mold(mo);
        Mold_Array_At(mo, FEED_ARRAY(L->feed), before_index, "[]");
        Throttle_Mold(mo);
        printf("Where(Before):\n");
        printf("%s\n\n", Binary_At(mo->string, mo->base.size));
        Drop_Mold(mo);
    }

    DECLARE_MOLDER (mo);
    SET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT);
    mo->limit = 40 * 20;  // 20 lines of length 40, or so?
    Push_Mold(mo);
    Mold_Array_At(mo, FEED_ARRAY(L->feed), index, "[]");
    Throttle_Mold(mo);
    printf("Where(At):\n");
    printf("%s\n\n", Binary_At(mo->string, mo->base.size));
    Drop_Mold(mo);
}

void Where(Level* L)
  { Where_Core_Debug(L); }

#endif  // DEBUG_HAS_PROBE
