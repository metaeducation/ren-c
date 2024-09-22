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
// Copyright 2012-2022 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See notes on the stacks in %sys-stack.h
//

#include "sys-core.h"


//
//  Startup_Data_Stack: C
//
void Startup_Data_Stack(Length capacity)
{
    // Start the data stack out with just one element in it, and poison it
    // (unreadable/unwritable).  This helps avoid accidental accesses in the
    // debug build.  It also means that indices into the data stack can be
    // unsigned (no need for -1 to mean empty, because 0 can)
    //
    // We could start it off as a dynamic allocation, but by letting it be
    // a singular array and then expanding it here it's a chance to test out
    // that logic early in the boot.
    //
    ensure(nullptr, g_ds.array) = Make_Array_Core(
        1,
        FLAG_FLAVOR(DATASTACK) | FLEX_FLAGS_NONE
    );
    Set_Flex_Len(g_ds.array, 1);
    assert(Not_Flex_Flag(g_ds.array, DYNAMIC));

    Cell* head = Array_Head(g_ds.array);
    assert(Is_Cell_Erased(head));  // non-dynamic array, length 1 indicator
    Init_Unreadable(head);

    // The tail marker will signal PUSH() that it has run out of space,
    // and it will perform the allocation at that time.
    //
    g_ds.movable_tail = Array_Tail(g_ds.array);

    // Reuse the expansion logic that happens on a PUSH() to get the
    // initial stack size.  It requires you to be on an END to run.
    //
    g_ds.index = 1;
    g_ds.movable_top = Flex_At(Cell, g_ds.array, g_ds.index);
    Expand_Data_Stack_May_Fail(capacity);

    DROP();  // drop the hypothetical thing that triggered the expand

    assert(Get_Flex_Flag(g_ds.array, DYNAMIC));
    Poison_Cell(Array_Head(g_ds.array));  // new head
}


//
//  Shutdown_Data_Stack: C
//
void Shutdown_Data_Stack(void)
{
    assert(TOP_INDEX == 0);
    assert(Is_Cell_Poisoned(Array_Head(g_ds.array)));

    Free_Unmanaged_Flex(g_ds.array);
    g_ds.array = nullptr;
}


//
//  Startup_Feeds: C
//
void Startup_Feeds(void)
{
    PG_Empty_Array = Make_Array_Core(0, NODE_FLAG_MANAGED);
    Freeze_Array_Deep(PG_Empty_Array);

    PG_Feed_At_End.header.bits = FLAG_FIRST_BYTE(END_SIGNAL_BYTE);

    TG_End_Feed = Make_Array_Feed_Core(EMPTY_ARRAY, 0, SPECIFIED);
    Add_Feed_Reference(TG_End_Feed);
    assert(Is_Feed_At_End(TG_End_Feed));
}

//
//  Shutdown_Feeds: C
//
void Shutdown_Feeds(void) {
    PG_Feed_At_End.header.bits = 0;

    Release_Feed(TG_End_Feed);
    TG_End_Feed = nullptr;

    PG_Empty_Array = nullptr;
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
Context* Get_Context_From_Stack(void)
{
    Level* L = TOP_LEVEL;
    Phase* phase = nullptr;  // avoid uninitialized variable warning

    for (; L != BOTTOM_LEVEL; L = L->prior) {
        if (not Is_Action_Level(L))
            continue;

        phase = Level_Phase(L);
        break;
    }

    if (L == BOTTOM_LEVEL) {
        //
        // No natives are in effect, so this is API code running directly from
        // an `int main()`.  Previously this always ran in the user context,
        // but the user context is now no longer available during much of the
        // boot...so we fall back to the Lib_Context during boot.
        //
        return User_Context != nullptr ? User_Context : Lib_Context;
    }

    // This would happen if you call the API from something like a traced
    // eval hook, or a Func_Dispatcher().  For now, just assume that means
    // you want the code to bind into the lib context.
    //
    if (not Is_Action_Native(phase))
        return Lib_Context;

    Details* details = Phase_Details(phase);
    Value* context = Details_At(details, IDX_NATIVE_CONTEXT);
    return VAL_CONTEXT(context);
}


//
//  Expand_Data_Stack_May_Fail: C
//
// The data stack is expanded when the pushed pointer matches the known tail
// of the allocated space.
//
// WARNING: This will invalidate any extant pointers to REBVALs living in
// the stack.  It is for this reason that stack access should be done by
// StackIndex "data stack indices" and not by Value* across *any* operation
// which could do a push or pop.  (Currently stable w.r.t. pop but there may
// be compaction at some point.)
//
void Expand_Data_Stack_May_Fail(REBLEN amount)
{
    REBLEN len_old = Array_Len(g_ds.array);

    // The current requests for expansion should only happen when the stack
    // is at its end.  Sanity check that.
    //
    assert(len_old == g_ds.index);
    assert(g_ds.movable_top == Flex_Tail(Cell, g_ds.array));
    assert(
        g_ds.movable_top - Flex_Head(Cell, g_ds.array)
        == cast(int, len_old)
    );

    // If adding in the requested amount would overflow the stack limit, then
    // give a data stack overflow error.
    //
    if (Flex_Rest(g_ds.array) + amount >= STACK_LIMIT) {
        //
        // Because the stack pointer was incremented and hit the END marker
        // before the expansion, we have to decrement it if failing.
        //
        --g_ds.index;
        --g_ds.movable_top;
        Fail_Stack_Overflow(); // !!! Should this be a "data stack" message?
    }

    Extend_Flex_If_Necessary(g_ds.array, amount);

    // Update the pointer used for fast access to the top of the stack that
    // likely was moved by the above allocation (needed before using TOP)
    //
    g_ds.movable_top = Flex_At(Cell, g_ds.array, g_ds.index);

    REBLEN len_new = len_old + amount;
    Set_Flex_Len(g_ds.array, len_new);

  #if DEBUG_POISON_DROPPED_STACK_CELLS
    Cell* poison = g_ds.movable_top;
    REBLEN n;
    for (n = len_old; n < len_new; ++n, ++poison)
        Poison_Cell(poison);
    assert(poison == Flex_Tail(Cell, g_ds.array));
  #endif

    // Update the end marker to serve as the indicator for when the next
    // stack push would need to expand.
    //
    g_ds.movable_tail = Flex_Tail(Cell, g_ds.array);
}


//
//  Pop_Stack_Values_Core_Masked: C
//
// Pops computed values from the stack to make a new ARRAY.
//
// !!! How can we pass in callsite file/line for tracking info?
//
Array* Pop_Stack_Values_Core_Masked(
    StackIndex base,
    Flags flags,
    Flags copy_mask
){
    Assert_No_DataStack_Pointers_Extant();  // in the future, pop may disrupt

    Length len = TOP_INDEX - base;
    Array* a = Make_Array_Core(len, flags);
    Set_Flex_Len(a, len);

    Flavor flavor = Stub_Flavor(a);  // flavor comes from flags

    Count count = 0;
    Atom* src = Data_Stack_At(base + 1);  // not const, will be Freshen_Cell()
    Cell* dest = Array_Head(a);
    for (; count < len; ++count, ++src, ++dest) {
      #if DEBUG
        if (Is_Antiform(src)) {
            Assert_Cell_Stable(src);
            assert(flavor >= FLAVOR_MIN_ANTIFORMS_OK);
        }
      #endif

          Move_Cell_Untracked(dest, src, copy_mask);

        #if DEBUG_POISON_DROPPED_STACK_CELLS
          Poison_Cell(src);
        #endif
    }

    g_ds.index -= len;
    g_ds.movable_top -= len;

    return a;
}
