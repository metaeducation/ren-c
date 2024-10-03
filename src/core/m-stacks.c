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
void Startup_Data_Stack(REBLEN size)
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
    DS_Array = Make_Array_Core(1, ARRAY_FLAG_NULLEDS_LEGAL);
    Init_Unreadable(Array_Head(DS_Array));

    DS_Movable_Tail = cast(Value*, Array_Tail(DS_Array));

    // The END marker will signal DS_PUSH that it has run out of space,
    // and it will perform the allocation at that time.
    //
    Term_Array_Len(DS_Array, 1);
    Assert_Array(DS_Array);

    // Reuse the expansion logic that happens on a DS_PUSH to get the
    // initial stack size.  It requires you to be on an END to run.
    //
    DS_Index = 1;
    DS_Movable_Top = KNOWN(Array_At(DS_Array, DS_Index)); // can't push RELVALs
    Expand_Data_Stack_May_Fail(size);

    // Now drop the hypothetical thing pushed that triggered the expand.
    //
    DROP();
}


//
//  Shutdown_Data_Stack: C
//
void Shutdown_Data_Stack(void)
{
    assert(TOP_INDEX == 0);
    Assert_Unreadable_If_Debug(Array_Head(DS_Array));

    Free_Unmanaged_Flex(DS_Array);
}


//
//  Startup_Level_Stack: C
//
// We always push one unused frame at the top of the stack.  This way, it is
// not necessary for unused frames to check if `L->prior` is null; it may be
// assumed that it never is.
//
void Startup_Level_Stack(void)
{
  #if !defined(NDEBUG) // see Startup_Corrupt_Globals() for explanation
    assert(Is_Pointer_Corrupt_Debug(TG_Top_Level));
    assert(Is_Pointer_Corrupt_Debug(TG_Bottom_Level));
    TG_Top_Level = TG_Bottom_Level = nullptr;
  #endif

    TG_Level_Source_End.index = 0;
    TG_Level_Source_End.vaptr = nullptr;
    TG_Level_Source_End.array = EMPTY_ARRAY; // for HOLD flag in Push_Level
    Corrupt_Pointer_If_Debug(TG_Level_Source_End.pending);

    Level* L = ALLOC(Level); // needs dynamic allocation
    Erase_Cell(Level_Spare(L));
    Init_Unreadable(Level_Spare(L));

    L->out = m_cast(Value*, END_NODE); // should not be written
    L->source = &TG_Level_Source_End;
    Push_Level_At_End(L, DO_MASK_NONE);

    // It's too early to be using Make_Paramlist_Managed_May_Fail()
    //
    Array* paramlist = Make_Array_Core(
        1,
        NODE_FLAG_MANAGED | SERIES_MASK_ACTION
    );
    MISC(paramlist).meta = nullptr;

    Value* archetype = RESET_CELL(Array_Head(paramlist), REB_ACTION);
    archetype->extra.binding = UNBOUND;
    archetype->payload.action.paramlist = paramlist;
    Term_Array_Len(paramlist, 1);

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
    Init_Block(Array_Head(ACT_DETAILS(PG_Dummy_Action)), EMPTY_ARRAY);

    Reuse_Varlist_If_Available(L); // needed to attach API handles to
    Push_Action(L, PG_Dummy_Action, UNBOUND);

    Symbol* opt_label = nullptr;
    Begin_Action(L, opt_label, m_cast(Value*, END_NODE));
    assert(IS_END(L->arg));
    L->param = END_NODE; // signal all arguments gathered
    assert(L->refine == END_NODE); // passed to Begin_Action();
    L->arg = m_cast(Value*, END_NODE);
    L->special = END_NODE;

    Corrupt_Pointer_If_Debug(L->prior); // help catch enumeration past BOTTOM_LEVEL
    TG_Bottom_Level = L;

    assert(TOP_LEVEL == L and BOTTOM_LEVEL == L);
}


//
//  Shutdown_Level_Stack: C
//
void Shutdown_Level_Stack(void)
{
    assert(TOP_LEVEL == BOTTOM_LEVEL);

    // To stop enumerations from using nullptr to stop the walk, and not count
    // the bottom frame as a "real stack level", it had a trash pointer put
    // in the debug build.  Restore it to a typical null before the drop.
    //
    assert(Is_Pointer_Corrupt_Debug(TG_Bottom_Level->prior));
    TG_Bottom_Level->prior = nullptr;

    Level* L = TOP_LEVEL;
    Drop_Action(L);

    // There's a Catch-22 on checking the balanced state for outstanding
    // manual series allocations, e.g. it can't check *before* the mold buffer
    // is freed because it would look like it was a leaked series, but it
    // can't check *after* because the mold buffer balance check would crash.
    //
    Drop_Level_Core(L); // can't be Drop_Level() or Drop_Level_Unbalanced()

    assert(not TOP_LEVEL);
    FREE(Level, L);

    TG_Top_Level = nullptr;
    TG_Bottom_Level = nullptr;

    PG_Dummy_Action = nullptr; // was GC protected as BOTTOM_LEVEL's L->original
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
VarList* Get_Context_From_Stack(void)
{
    Level* L = TOP_LEVEL;
    REBACT *phase;
    while (true) {
        if (L == BOTTOM_LEVEL) {
            //
            // Special case, no natives are in effect, so basically API code
            // running directly from an `int main()`.  This is dangerous, as
            // it means any failures will crash.  For the moment, go with
            // user, though console code would probably prefer to be in the
            // console module (configure this in rebStartup()?).
            //
            return Cell_Varlist(Get_System(SYS_CONTEXTS, CTX_USER));
        }

        phase = LVL_PHASE_OR_DUMMY(L);
        if (phase == PG_Dummy_Action) {
            //
            // Some frames are set up just to catch failures, but aren't
            // tied to a function call themselves.  Ignore them (unless they
            // are BOTTOM_LEVEL, handled above.)
            //
            L = L->prior;
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

    Array* details = ACT_DETAILS(phase);
    Value* context = KNOWN(Array_At(details, 1));
    return Cell_Varlist(context);
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
// StackIndex and not by Value* across *any* operation which could do a push
// or pop.  (Currently stable w.r.t. pop but there may be compaction.)
//
void Expand_Data_Stack_May_Fail(REBLEN amount)
{
    REBLEN len_old = Array_Len(DS_Array);

    // The current requests for expansion should only happen when the stack
    // is at its end.  Sanity check that.
    //
    assert(len_old == DS_Index);
    assert(IS_END(DS_Movable_Top));
    assert(DS_Movable_Top == KNOWN(Array_Tail(DS_Array)));
    assert(DS_Movable_Top - KNOWN(Array_Head(DS_Array)) == cast(int, len_old));

    // If adding in the requested amount would overflow the stack limit, then
    // give a data stack overflow error.
    //
    if (Flex_Rest(DS_Array) + amount >= STACK_LIMIT) {
        //
        // Because the stack pointer was incremented and hit the END marker
        // before the expansion, we have to decrement it if failing.
        //
        --DS_Index;
        Fail_Stack_Overflow(); // !!! Should this be a "data stack" message?
    }

    Extend_Flex(DS_Array, amount);

    // Update the pointer used for fast access to the top of the stack that
    // likely was moved by the above allocation (needed before using TOP)
    //
    DS_Movable_Top = cast(Value*, Array_At(DS_Array, DS_Index));

    // We fill in the data stack with "GC safe trash" (which is void in the
    // release build, but will raise an alarm if VAL_TYPE() called on it in
    // the debug build).  In order to serve as a marker for the stack slot
    // being available, it merely must not be IS_END()...

    Value* cell = DS_Movable_Top;

    REBLEN len_new = len_old + amount;
    REBLEN n;
    for (n = len_old; n < len_new; ++n) {
        Poison_Cell(cell);
        ++cell;
    }

    // Update the end marker to serve as the indicator for when the next
    // stack push would need to expand.
    //
    Term_Array_Len(DS_Array, len_new);
    assert(cell == Array_Tail(DS_Array));

    DS_Movable_Tail = cell;
}


//
//  Pop_Stack_Values_Core: C
//
// Pops computed values from the stack to make a new ARRAY.
//
Array* Pop_Stack_Values_Core(StackIndex base, REBFLGS flags)
{
    Array* array = Copy_Values_Len_Shallow_Core(
        Data_Stack_At(base + 1),  // start somewhere in the stack, end at TOP
        SPECIFIED,  // data stack should be fully specified--no relative values
        TOP_INDEX - base,  // len
        flags
    );

    Drop_Data_Stack_To(base);
    return array;
}


//
//  Pop_Stack_Values_Into: C
//
// Pops computed values from the stack into an existing ANY-ARRAY.  The
// index of that array will be updated to the insertion tail (/INTO protocol)
//
void Pop_Stack_Values_Into(Value* into, StackIndex base) {
    REBLEN len = TOP_INDEX - base;
    Value* values = KNOWN(Array_At(DS_Array, base + 1));

    assert(Any_List(into));
    Fail_If_Read_Only_Flex(Cell_Array(into));

    VAL_INDEX(into) = Insert_Flex(
        Cell_Array(into),
        VAL_INDEX(into),
        cast(Byte*, values), // stack only holds fully specified REBVALs
        len // multiplied by width (sizeof(Cell)) in Insert_Flex
    );

    Drop_Data_Stack_To(base);
}
