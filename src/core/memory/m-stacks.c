//
//  file: %m-stack.c
//  summary: "data and function call stack implementation"
//  section: memory
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// See notes on the stacks in %sys-datastack.h
//

#include "sys-core.h"


//
//  Startup_Data_Stack: C
//
// 1. We could start the stack off as a large dynamic pre-allocation.  But
//    letting it be a singular array and then expanding it here is a chance
//    to test the expansion logic early on.
//
void Startup_Data_Stack(Length capacity)
{
    ensure_nullptr(g_ds.array) = Make_Array_Core(FLAG_FLAVOR(FLAVOR_DATASTACK), 1);
    Set_Flex_Len(g_ds.array, 1);  // one element, helps test expansion [1]
    assert(Not_Stub_Flag(g_ds.array, DYNAMIC));

  mark_head_unreadable: { ////////////////////////////////////////////////////

    Cell* head = Array_Head(g_ds.array);  // head will move after expansion
    assert(Is_Cell_Erased(head));  // non-dynamic array, length 1 indicator
    Init_Unreadable(head);

    g_ds.movable_tail = Array_Tail(g_ds.array);  // signals PUSH() out of space

    g_ds.index = 1;
    g_ds.movable_top = Flex_At(Value, g_ds.array, g_ds.index);

} expand_stack: { ////////////////////////////////////////////////////////////

    // 1. Poison cell at the head of the data stack (unreadable/unwritable).
    //    Having nothing at [0] means that StackIndex can be unsigned (no need
    //    for -1 to mean empty, because 0 means that).

    Expand_Data_Stack_May_Panic(capacity);  // leverage expansion logic

    DROP();  // drop the hypothetical thing that triggered the expand

    assert(Get_Stub_Flag(g_ds.array, DYNAMIC));
    Force_Poison_Cell(Array_Head(g_ds.array));  // poison the head [1]
}}


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
    PG_Feed_At_End.header.bits = FLAG_FIRST_BYTE(END_SIGNAL_BYTE);

    static const void* packed = &PG_Feed_At_End;  // "packed feed items"
    TG_End_Feed = Make_Variadic_Feed(&packed, nullptr, FEED_MASK_DEFAULT);
    Clear_Feed_Flag(TG_End_Feed, NEEDS_SYNC);  // !!! or asserts on shutdown
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
}


//
//  Expand_Data_Stack_May_Panic: C
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
// 1. Operations like PUSH() increment first, and then notice they hit the
//    `movable_tail` to call into an expand.  So if we're not going to grant
//    the expansion, we have to decrement the pointer prior to failing.
//
void Expand_Data_Stack_May_Panic(REBLEN amount)
{
    REBLEN len_old = Array_Len(g_ds.array);

    assert(len_old == g_ds.index);  // only request expansion when tail hit
    assert(g_ds.movable_top == Flex_Tail(Value, g_ds.array));
    assert(
        g_ds.movable_top - Flex_Head(Value, g_ds.array)
        == cast(int, len_old)
    );

    if (Flex_Rest(g_ds.array) + amount >= STACK_LIMIT) {  // catch overflow
        --g_ds.index;  // have to correct for pre-increment [1]
        --g_ds.movable_top;
        Panic_Stack_Overflow(); // !!! Should this be a "data stack" message?
    }

    Extend_Flex_If_Necessary(g_ds.array, amount);

    g_ds.movable_top = Flex_At(Value, g_ds.array, g_ds.index);  // needs update

    REBLEN len_new = len_old + amount;
    Set_Flex_Len(g_ds.array, len_new);

  #if DEBUG_POISON_DROPPED_STACK_CELLS
    Value* poison = g_ds.movable_top;
    REBLEN n;
    for (n = len_old; n < len_new; ++n, ++poison)
        Force_Poison_Cell(poison);
    assert(poison == Flex_Tail(Value, g_ds.array));
  #endif

    g_ds.movable_tail = Flex_Tail(Value, g_ds.array);  // next expansion point
}


//
//  Pop_Stack_Values_Core: C
//
// Pops computed values from the stack to make a new ARRAY.
//
// 1. The Pop has CELL_MASK_ALL semantics, so anything like CELL_FLAG_NOTE
//    will be copied.  There is no other option, because the release build
//    uses memcpy() to implement this.  Hence we make sure none of the
//    persistent flags
//
// !!! How can we pass in callsite file and line for tracking info?
//
Array* Pop_Stack_Values_Core(Flags flags, StackIndex base) {
    Assert_No_DataStack_Pointers_Extant();  // in the future, pop may disrupt

    Length len = TOP_INDEX - base;
    Array* a = Make_Array_Core(flags, len);
    Set_Flex_Len(a, len);

    Value* src = Data_Stack_At(Value, base + 1);  // moving, not const!
    Value* dest = Flex_Head(Value, a);

  #if NO_RUNTIME_CHECKS  // Stack cells lack CELL_MASK_PERSIST, can memcpy()
    STATIC_ASSERT(! DEBUG_POISON_DROPPED_STACK_CELLS);
    Mem_Copy(dest, src, len * sizeof(Cell));  // CELL_MASK_ALL semantics [1]
  #else
    Flavor flavor = Stub_Flavor(a);  // flavor comes from flags

    Count count = 0;
    for (; count < len; ++count, ++src, ++dest) {
        assert(not (src->header.bits & CELL_MASK_PERSIST));  // would copy [1]
        if (Is_Antiform(src)) {  // only ok in some arrays
            Assert_Cell_Stable(src);
            if (flavor < MIN_FLAVOR_ANTIFORMS_OK)
                crash ("Unexpected antiform found on data stack");
        }

        Move_Cell_Untracked(dest, src, CELL_MASK_ALL);

        #if DEBUG_POISON_DROPPED_STACK_CELLS
          Force_Poison_Cell(src);
        #endif
    }
  #endif

    g_ds.index -= len;
    g_ds.movable_top -= len;

    return a;
}
