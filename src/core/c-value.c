//
//  file: %c-value.c
//  summary: "Generic Value Cell Support Services and Debug Routines"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// These are mostly RUNTIME_CHECKS routines to support the macros and
// definitions in %sys-cell.h.
//

#include "sys-core.h"



#if DEBUG_HAS_PROBE

INLINE void Probe_Print_Helper(
    const void *p,  // the Cell*, Stub*, or UTF-8 char*
    const char *expr,  // stringified contents of the PROBE() macro
    const char *type,  // detected type of `p` (see %struct-base.h)
    Option(const char*) file,  // file where this PROBE() was invoked
    Option(LineNumber) line  // line where this PROBE() was invoked
){
    printf(
        "\n-- (%s)=0x%p : %s : TICK %" PRIu64 " %s LINE %d\n",
        expr,
        p,
        type,
        TICK,  // 0 if not TRAMPOLINE_COUNTS_TICKS
        file ? unwrap file : "(no file)",
        line ? cast(int, unwrap line) : 0
    );

    fflush(stdout);
    fflush(stderr);
}


INLINE void Probe_Molded_Value(Molder* mo, const Stable* v)
{
    if (Is_Antiform(v)) {
        DECLARE_STABLE (temp);
        Copy_Cell(temp, v);
        Element* elem = Quasify_Antiform(temp);
        Mold_Element(mo, elem);
        require (
          Append_Ascii(mo->strand, "  ; anti")
        );
    }
    else {
        Mold_Element(mo, cast(Element*, v));
    }
}

void Probe_Cell_Print_Helper(
    Molder* mo,
    const void *p,
    const char *expr,
    Option(const char*) file,
    Option(LineNumber) line
){
    Probe_Print_Helper(p, expr, "Value", file, line);

    const Value* atom = cast(Value*, p);

    if (Is_Cell_Poisoned(atom)) {
        require (
          Append_Ascii(mo->strand, "\\\\poisoned\\\\")
        );
        return;
    }

    if (Not_Cell_Readable(atom)) {
        require (
          Append_Ascii(mo->strand, "\\\\unreadable\\\\")
        );
        return;
    }

    if (Is_Antiform(atom)) {
        DECLARE_ELEMENT (reified);
        Copy_Lifted_Cell(reified, atom);
        Mold_Element(mo, reified);
        require (
          Append_Ascii(mo->strand, "  ; anti")
        );
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
    Length limit,
    const char *expr,
    Option(const char*) file,
    Option(LineNumber) line
){
  #if TRAMPOLINE_COUNTS_TICKS
    Tick saved_tick = g_tick;

    #if RUNTIME_CHECKS
        Tick saved_break_at_tick = g_break_at_tick;
        g_break_at_tick = 0;  // prevent breaking during the Probe()
    #endif
  #endif

    bool top_was_intrinsic = Get_Level_Flag(TOP_LEVEL, DISPATCHING_INTRINSIC);
    Clear_Level_Flag(TOP_LEVEL, DISPATCHING_INTRINSIC);

    DECLARE_MOLDER (mo);
    if (limit != 0) {
        SET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT);
        mo->limit = 10000;  // what's useful?
    }

    Push_Mold(mo);

    bool was_disabled = g_gc.disabled;
    g_gc.disabled = true;

    if (not p) {
        Probe_Print_Helper(p, expr, "C nullptr", file, line);
        goto cleanup;
    }
    else switch (Detect_Rebol_Pointer(p)) {
      case DETECTED_AS_UTF8:
        if (*cast(Byte*, p) == '\0')
            Probe_Print_Helper(
                p,
                expr,
                "Empty UTF-8 String or Is_Cell_Erased() / Is_Stub_Erased()",
                file, line
              );
        else {
            Probe_Print_Helper(p, expr, "UTF-8 String", file, line);
            printf("\"%s\"\n", cast(char*, p));
        }
        goto cleanup;

      case DETECTED_AS_CELL: {
        Probe_Cell_Print_Helper(mo, p, expr, file, line);
        goto cleanup; }

      case DETECTED_AS_END:
        Probe_Print_Helper(p, expr, "rebEND Signal (192)", file, line);
        goto cleanup;

      case DETECTED_AS_STUB:
        break;  // lots of possibilities, break to handle

      case DETECTED_AS_FREE:
        Probe_Print_Helper(p, expr, "Freed PoolUnit (193)", file, line);
        goto cleanup;

      case DETECTED_AS_WILD:
        Probe_Print_Helper(p, expr, "Wild Pointer (194)", file, line);
        goto cleanup;
    }

    // If we didn't jump to cleanup above, it's a Flex.  New switch().

  handle_flex: {

    const Flex* f = cast(Flex* , p);
    assert(Is_Base_Readable(f));
    Flavor flavor = Stub_Flavor(f);
    Assert_Flex(f);  // if corrupt, gives better info than a print crash

    switch (flavor) {
      case FLAVOR_0:
        Probe_Print_Helper(p, expr, "!!! CORRUPT Flex !!!", file, line);
        break;

    //=//// ARRAY FLEXES //////////////////////////////////////////////////=//

      case FLAVOR_SOURCE:
        Probe_Print_Helper(p, expr, "Generic Array", file, line);
        Mold_Array_At(mo, cast(const Array*, f), 0, "[]");
        break;

      case FLAVOR_SEA: {
        Probe_Print_Helper(p, expr, "Sea of Variables", file, line);
        DECLARE_ELEMENT (elem);
        Init_Module(elem, cast(SeaOfVars*, m_cast(void*, p)));
        Push_Lifeguard(elem);

        DECLARE_ELEMENT (molder);
        Init_Handle_Cdata(molder, mo, 1);
        rebElide(CANON(MOLDIFY), elem, molder, rebQ(LIB(NULL)));

        Drop_Lifeguard(elem);
        break; }

      case FLAVOR_VARLIST: {  // currently same as FLAVOR_PARAMLIST
        Probe_Print_Helper(p, expr, "Varlist (or Paramlist)", file, line);
        DECLARE_ELEMENT (elem);
        VarList* varlist = cast(VarList*, m_cast(void*, p));
        if (CTX_TYPE(varlist) == TYPE_FRAME) {
            if (
                Not_Stub_Flag(varlist, MISC_NEEDS_MARK)
                and Not_Base_Managed(varlist)
            ){
                Set_Base_Managed_Bit(varlist);
            }
            ParamList* paramlist = cast(ParamList*, varlist);
            Phase* lens = cast(Phase*, Phase_Details(paramlist));  // show all
            Init_Lensed_Frame(elem, paramlist, lens, UNCOUPLED);
        }
        else
            Init_Context_Cell(elem, CTX_TYPE(varlist), varlist);
        Push_Lifeguard(elem);
        Probe_Molded_Value(mo, elem);
        Drop_Lifeguard(elem);
        break; }

      case FLAVOR_DETAILS: {
        Probe_Print_Helper(p, expr, "Details", file, line);
        DECLARE_ELEMENT (frame);
        Details* details = cast(Details*, m_cast(void*, p));
        Init_Frame(frame, details, ANONYMOUS, UNCOUPLED);
        Push_Lifeguard(frame);

        DECLARE_ELEMENT (molder);
        Init_Handle_Cdata(molder, mo, 1);
        rebElide(CANON(MOLDIFY), frame, molder, rebQ(LIB(NULL)));

        Drop_Lifeguard(frame);
        break; }

      case FLAVOR_PAIRLIST:
        Probe_Print_Helper(p, expr, "Pairlist", file, line);
        break;

      case FLAVOR_PATCH:
        Probe_Print_Helper(p, expr, "Module Item Patch", file, line);
        break;

      case FLAVOR_LET: {
        Probe_Print_Helper(p, expr, "LET single variable", file, line);
        Append_Spelling(mo->strand, Let_Symbol(cast(Let*, f)));
        break; }

      case FLAVOR_USE: {
        Probe_Print_Helper(p, expr, "Virtual Bind USE", file, line);
        break; }

      case FLAVOR_STUMP:
        Probe_Print_Helper(p, expr, "Binding Stump", file, line);
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
        require (
          Append_Ascii(mo->strand, "<< ")
        );
        for (; key != tail; ++key) {
            Mold_Text_Flex_At(mo, Key_Symbol(key), 0);
            Append_Codepoint(mo->strand, ' ');
        }
        require (
          Append_Ascii(mo->strand, ">>")
        );
        break; }

      case FLAVOR_POINTERS:
        Probe_Print_Helper(p, expr, "Flex of void*", file, line);
        break;

      case FLAVOR_CANONTABLE:
        Probe_Print_Helper(p, expr, "Canon Table", file, line);
        break;

      case FLAVOR_NODELIST:  // e.g. GC protect list
        Probe_Print_Helper(p, expr, "Flex of Base*", file, line);
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

        const bool brk = (Binary_Len(b) > 32);  // !!! duplicates MF_Blob code
        require (
          Append_Ascii(mo->strand, "#{")
        );
        Form_Base16(mo, Binary_Head(b), Binary_Len(b), brk);
        require (
          Append_Ascii(mo->strand, "}")
        );
        break; }

    //=//// FLEXES WITH ELEMENTS WIDTH 1 INTERPRETED AS UTF-8 /////////////=//

      case FLAVOR_NONSYMBOL: {
        Probe_Print_Helper(p, expr, "Non-Symbol String Flex", file, line);
        Mold_Text_Flex_At(mo, cast(Strand*, f), 0);  // could be TAG!, etc.
        break; }

      case FLAVOR_SYMBOL: {
        Probe_Print_Helper(p, expr, "Interned (Symbol) Flex", file, line);
        Mold_Text_Flex_At(mo, cast(Symbol*, f), 0);
        break; }

      case FLAVOR_THE_GLOBAL_INACCESSIBLE: {
        Probe_Print_Helper(p, expr, "Global Inaccessible Stub", file, line);
        break; }

      default:
        Probe_Print_Helper(p, expr, "!!! Unknown Stub_Flavor() !!!", file, line);
        break;
    }

} cleanup: {

    if (mo->base.size != Strand_Size(mo->strand))
        printf("%s\n", cast(char*, Binary_At(mo->strand, mo->base.size)));

    if (mo->opts & MOLD_FLAG_WAS_TRUNCATED)
        printf("...\\\\truncated\\\\...\n");

    fflush(stdout);

    Drop_Mold(mo);

    assert(g_gc.disabled);
    g_gc.disabled = was_disabled;

    if (top_was_intrinsic)
        Set_Level_Flag(TOP_LEVEL, DISPATCHING_INTRINSIC);

  #if TRAMPOLINE_COUNTS_TICKS
    Reconcile_Ticks();
    g_tick = saved_tick;
    g_ts.total_eval_cycles = saved_tick;

    #if RUNTIME_CHECKS
        g_break_at_tick = saved_break_at_tick;
    #endif
  #endif

    return m_cast(void*, p);  // must cast back to const if source was const
}}


// Version with fewer parameters, useful to call from the C debugger (which
// cannot call macros like PROBE())
//
void Probe(const void *p) {
    Length limit = 0;  // unlimited
    Option(const char*) file = nullptr;
    Option(LineNumber) line = 0;
    Probe_Core_Debug(p, limit, "Probe()", file, line);
}

void Probe_Limit(const void *p, Length limit) {
    Option(const char*) file = nullptr;
    Option(LineNumber) line = 0;
    Probe_Core_Debug(p, limit, "Probe_Limit()", file, line);
}

#endif  // DEBUG_HAS_PROBE
