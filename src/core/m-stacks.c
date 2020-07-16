//
//  File: %m-stack.c
//  Summary: "data and function call stack implementation"
//  Section: memory
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

#include "sys-core.h"


//
//  Startup_Data_Stack: C
//
void Startup_Data_Stack(REBCNT size)
{
    // Start the data stack out with just one element in it, and make it an
    // unreadable blank in the debug build.  This helps avoid accidental
    // reads and is easy to notice when it is overwritten.  It also means
    // that indices into the data stack can be unsigned (no need for -1 to
    // mean empty, because 0 can)
    //
    // DS_PUSH checks what you're pushing isn't void, as most arrays can't
    // contain them.  But DS_PUSH_MAYBE_VOID allows you to, in case you
    // are building a context varlist or similar.
    //
    DS_Array = Make_Arr_Core(1, ARRAY_FLAG_NULLEDS_LEGAL);
    Init_Unreadable_Blank(ARR_HEAD(DS_Array));

    // The END marker will signal DS_PUSH that it has run out of space,
    // and it will perform the allocation at that time.
    //
    TERM_ARRAY_LEN(DS_Array, 1);
    ASSERT_ARRAY(DS_Array);

    // Reuse the expansion logic that happens on a DS_PUSH to get the
    // initial stack size.  It requires you to be on an END to run.
    //
    DS_Index = 1;
    DS_Movable_Top = KNOWN(ARR_AT(DS_Array, DS_Index)); // can't push RELVALs
    Expand_Data_Stack_May_Fail(size);

    // Now drop the hypothetical thing pushed that triggered the expand.
    //
    DS_DROP;
}


//
//  Shutdown_Data_Stack: C
//
void Shutdown_Data_Stack(void)
{
    assert(DSP == 0);
    ASSERT_UNREADABLE_IF_DEBUG(ARR_HEAD(DS_Array));

    Free_Unmanaged_Array(DS_Array);
}


//
//  Startup_Frame_Stack: C
//
// We always push one unused frame at the top of the stack.  This way, it is
// not necessary for unused frames to check if `f->prior` is null; it may be
// assumed that it never is.
//
void Startup_Frame_Stack(void)
{
  #if !defined(NDEBUG) // see Startup_Trash_Debug() for explanation
    assert(IS_POINTER_TRASH_DEBUG(TG_Top_Frame));
    assert(IS_POINTER_TRASH_DEBUG(TG_Bottom_Frame));
    TG_Top_Frame = TG_Bottom_Frame = nullptr;
  #endif

    TG_Frame_Source_End.index = 0;
    TG_Frame_Source_End.vaptr = nullptr;
    TG_Frame_Source_End.array = EMPTY_ARRAY; // for HOLD flag in Push_Frame
    TRASH_POINTER_IF_DEBUG(TG_Frame_Source_End.pending);

    REBFRM *f = ALLOC(REBFRM); // needs dynamic allocation
    Prep_Stack_Cell(FRM_CELL(f));
    Init_Unreadable_Blank(FRM_CELL(f));

    f->out = m_cast(REBVAL*, END_NODE); // should not be written
    f->source = &TG_Frame_Source_End;
    Push_Frame_At_End(f, DO_MASK_NONE);

    // It's too early to be using Make_Paramlist_Managed_May_Fail()
    //
    REBARR *paramlist = Make_Arr_Core(
        1,
        NODE_FLAG_MANAGED | SERIES_MASK_ACTION
    );
    MISC(paramlist).meta = nullptr;

    REBVAL *archetype = RESET_CELL(ARR_HEAD(paramlist), REB_ACTION);
    archetype->extra.binding = UNBOUND;
    archetype->payload.action.paramlist = paramlist;
    TERM_ARRAY_LEN(paramlist, 1);

    PG_Dummy_Action = Make_Action(
        paramlist,
        &Null_Dispatcher,
        nullptr, // no underlying action (use paramlist)
        nullptr, // no specialization exemplar (or inherited exemplar)
        1 // details array capacity
    );

    // !!! Null_Dispatcher() currently requires a body for things like fake
    // source.  The user shouldn't get PG_Dummy_Action in their hands to ask
    // for SOURCE of, but still, the Null_Dispatcher() has asserts.
    //
    Init_Block(ARR_HEAD(ACT_DETAILS(PG_Dummy_Action)), EMPTY_ARRAY);

    Reuse_Varlist_If_Available(f); // needed to attach API handles to
    Push_Action(f, PG_Dummy_Action, UNBOUND);

    REBSTR *opt_label = nullptr;
    Begin_Action(f, opt_label, m_cast(REBVAL*, END_NODE));
    assert(IS_END(f->arg));
    f->param = END_NODE; // signal all arguments gathered
    assert(f->refine == END_NODE); // passed to Begin_Action();
    f->arg = m_cast(REBVAL*, END_NODE);
    f->special = END_NODE;

    TRASH_POINTER_IF_DEBUG(f->prior); // help catch enumeration past FS_BOTTOM
    TG_Bottom_Frame = f;

    assert(FS_TOP == f and FS_BOTTOM == f);
}


//
//  Shutdown_Frame_Stack: C
//
void Shutdown_Frame_Stack(void)
{
    assert(FS_TOP == FS_BOTTOM);

    // To stop enumerations from using nullptr to stop the walk, and not count
    // the bottom frame as a "real stack level", it had a trash pointer put
    // in the debug build.  Restore it to a typical null before the drop.
    //
    assert(IS_POINTER_TRASH_DEBUG(TG_Bottom_Frame->prior));
    TG_Bottom_Frame->prior = nullptr;

    REBFRM *f = FS_TOP;
    Drop_Action(f);

    // There's a Catch-22 on checking the balanced state for outstanding
    // manual series allocations, e.g. it can't check *before* the mold buffer
    // is freed because it would look like it was a leaked series, but it
    // can't check *after* because the mold buffer balance check would crash.
    //
    Drop_Frame_Core(f); // can't be Drop_Frame() or Drop_Frame_Unbalanced()

    assert(not FS_TOP);
    FREE(REBFRM, f);

    TG_Top_Frame = nullptr;
    TG_Bottom_Frame = nullptr;

    PG_Dummy_Action = nullptr; // was GC protected as FS_BOTTOM's f->original
}


//
//  Get_Context_From_Stack: C
//
// Generally speaking, Rebol does not have a "current context" in effect; as
// should you call an `IF` in a function body, there is now a Rebol IF on the
// stack.  But the story for ACTION!s that are implemented in C is different,
// as they have one Rebol action in effect while their C code is in control.
//
// This is used to an advantage in the APIs like rebValue(), to be able to get
// a notion of a "current context" applicable *only* to when natives run.
//
REBCTX *Get_Context_From_Stack(void)
{
    REBFRM *f = FS_TOP;
    REBACT *phase;
    while (true) {
        if (f == FS_BOTTOM) {
            //
            // Special case, no natives are in effect, so basically API code
            // running directly from an `int main()`.  This is dangerous, as
            // it means any failures will crash.  For the moment, go with
            // user, though console code would probably prefer to be in the
            // console module (configure this in rebStartup()?).
            //
            return VAL_CONTEXT(Get_System(SYS_CONTEXTS, CTX_USER));
        }

        phase = FRM_PHASE_OR_DUMMY(f);
        if (phase == PG_Dummy_Action) {
            //
            // Some frames are set up just to catch failures, but aren't
            // tied to a function call themselves.  Ignore them (unless they
            // are FS_BOTTOM, handled above.)
            //
            f = f->prior;
            continue;
        }

        break;
    }

    // The topmost stack level must be a native if we call this function.
    // (So don't call it from something like Returner_Dispatcher, where you
    // know for a fact it's a user function and not a native on the stack.)
    //
  #if !defined(NDEBUG)
    if (not GET_ACT_FLAG(phase, ACTION_FLAG_NATIVE)) {
        printf("!!! WARNING: calling API code from unsafe location\n");
        printf("(only do this in special debugging scenarios...)\n");
        return Lib_Context;
    }
  #endif

    REBARR *details = ACT_DETAILS(phase);
    REBVAL *context = KNOWN(ARR_AT(details, 1));
    return VAL_CONTEXT(context);
}


//
//  Expand_Data_Stack_May_Fail: C
//
// The data stack maintains an invariant that you may never push an END to it.
// So each push looks to see if it's pushing to a cell that contains an END
// and if so requests an expansion.
//
// WARNING: This will invalidate any extant pointers to REBVALs living in
// the stack.  It is for this reason that stack access should be done by
// REBDSP "data stack pointers" and not by REBVAL* across *any* operation
// which could do a push or pop.  (Currently stable w.r.t. pop but there may
// be compaction at some point.)
//
void Expand_Data_Stack_May_Fail(REBCNT amount)
{
    REBCNT len_old = ARR_LEN(DS_Array);

    // The current requests for expansion should only happen when the stack
    // is at its end.  Sanity check that.
    //
    assert(len_old == DS_Index);
    assert(IS_END(DS_Movable_Top));
    assert(DS_Movable_Top == KNOWN(ARR_TAIL(DS_Array)));
    assert(DS_Movable_Top - KNOWN(ARR_HEAD(DS_Array)) == cast(int, len_old));

    // If adding in the requested amount would overflow the stack limit, then
    // give a data stack overflow error.
    //
    if (SER_REST(SER(DS_Array)) + amount >= STACK_LIMIT) {
        //
        // Because the stack pointer was incremented and hit the END marker
        // before the expansion, we have to decrement it if failing.
        //
        --DS_Index;
        Fail_Stack_Overflow(); // !!! Should this be a "data stack" message?
    }

    Extend_Series(SER(DS_Array), amount);

    // Update the pointer used for fast access to the top of the stack that
    // likely was moved by the above allocation (needed before using DS_TOP)
    //
    DS_Movable_Top = cast(REBVAL*, ARR_AT(DS_Array, DS_Index));

    // We fill in the data stack with "GC safe trash" (which is void in the
    // release build, but will raise an alarm if VAL_TYPE() called on it in
    // the debug build).  In order to serve as a marker for the stack slot
    // being available, it merely must not be IS_END()...

    REBVAL *cell = DS_Movable_Top;

    REBCNT len_new = len_old + amount;
    REBCNT n;
    for (n = len_old; n < len_new; ++n) {
        Init_Unreadable_Blank(cell);
        SET_VAL_FLAGS(cell, CELL_FLAG_STACK | CELL_FLAG_TRANSIENT);
        ++cell;
    }

    // Update the end marker to serve as the indicator for when the next
    // stack push would need to expand.
    //
    TERM_ARRAY_LEN(DS_Array, len_new);
    assert(cell == ARR_TAIL(DS_Array));

    ASSERT_ARRAY(DS_Array);
}


//
//  Pop_Stack_Values_Core: C
//
// Pops computed values from the stack to make a new ARRAY.
//
REBARR *Pop_Stack_Values_Core(REBDSP dsp_start, REBFLGS flags)
{
    REBARR *array = Copy_Values_Len_Shallow_Core(
        DS_AT(dsp_start + 1), // start somewhere in the stack, end at DS_TOP
        SPECIFIED, // data stack should be fully specified--no relative values
        DSP - dsp_start, // len
        flags
    );

    DS_DROP_TO(dsp_start);
    return array;
}


//
//  Pop_Stack_Values_Into: C
//
// Pops computed values from the stack into an existing ANY-ARRAY.  The
// index of that array will be updated to the insertion tail (/INTO protocol)
//
void Pop_Stack_Values_Into(REBVAL *into, REBDSP dsp_start) {
    REBCNT len = DSP - dsp_start;
    REBVAL *values = KNOWN(ARR_AT(DS_Array, dsp_start + 1));

    assert(ANY_ARRAY(into));
    FAIL_IF_READ_ONLY_ARRAY(VAL_ARRAY(into));

    VAL_INDEX(into) = Insert_Series(
        SER(VAL_ARRAY(into)),
        VAL_INDEX(into),
        cast(REBYTE*, values), // stack only holds fully specified REBVALs
        len // multiplied by width (sizeof(REBVAL)) in Insert_Series
    );

    DS_DROP_TO(dsp_start);
}
