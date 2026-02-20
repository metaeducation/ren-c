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
    known_nullptr(g_ds.array) = Make_Array_Core(
        FLAG_FLAVOR(FLAVOR_DATASTACK), 1
    );
    Set_Flex_Len(g_ds.array, 1);  // one element, helps test expansion [1]
    assert(Not_Stub_Flag(g_ds.array, DYNAMIC));

  mark_head_unreadable: { ////////////////////////////////////////////////////

    Slot* head = Flex_Head(Slot, g_ds.array);  // will move after expansion
    assert(Is_Cell_Erased(head));  // non-dynamic array, length 1 indicator
    Init_Unreadable(head);

    g_ds.movable_tail = Flex_Tail(Slot, g_ds.array);  // PUSH() out of space

    g_ds.index = 1;
    g_ds.movable_top = Flex_At(Slot, g_ds.array, g_ds.index);

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
// 1. Sometimes we look at a Feed's pointer and have the same interpretation
//    if it's either an end or a blank.  Because we canonize the feed pointer
//    to g_cell_aligned_end, this is possible to make a tiny bit faster.
//
void Startup_Feeds(void)
{
    g_cell_aligned_end.header.bits = (
        FLAG_FIRST_BYTE(BASE_BYTE_END)
            | FLAG_KIND_BYTE(TYPE_BLANK)  // make testable as BLANK! too [1]
            | FLAG_LIFT_BYTE(NOQUOTE_63)
    );

    static const void* packed = &g_cell_aligned_end;  // "packed feed items"
    require (
      g_end_feed = Make_Variadic_Feed(&packed, nullptr, FEED_MASK_DEFAULT)
    );
    Clear_Feed_Flag(g_end_feed, NEEDS_SYNC);  // !!! or asserts on shutdown
    Add_Feed_Reference(g_end_feed);
    assert(Is_Feed_At_End(g_end_feed));
}


//
//  Shutdown_Feeds: C
//
void Shutdown_Feeds(void) {
    g_cell_aligned_end.header.bits = 0;

    Release_Feed(g_end_feed);
    g_end_feed = nullptr;
}


//
//  Expand_Data_Stack_May_Panic: C
//
// The data stack is expanded when the pushed pointer matches the known tail
// of the allocated space.
//
// WARNING: This will invalidate any extant pointers to REBVALs living in
// the stack.  It is for this reason that stack access should be done by
// StackIndex "data stack indices" and not by Stable* across *any* operation
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
    assert(g_ds.movable_top == Flex_Tail(Slot, g_ds.array));
    assert(
        g_ds.movable_top - Flex_Head(Slot, g_ds.array)
        == i_cast(int, len_old)
    );

    if (Flex_Rest(g_ds.array) + amount >= STACK_LIMIT) {  // catch overflow
        --g_ds.index;  // have to correct for pre-increment [1]
        --g_ds.movable_top;
        Panic_Stack_Overflow(); // !!! Should this be a "data stack" message?
    }

    require (
      Extend_Flex_If_Necessary_But_Dont_Change_Used(g_ds.array, amount)
    );
    g_ds.movable_top = Flex_At(Slot, g_ds.array, g_ds.index);  // needs update

    REBLEN len_new = len_old + amount;
    Set_Flex_Len(g_ds.array, len_new);

  #if DEBUG_POISON_DROPPED_STACK_CELLS
    Slot* poison = g_ds.movable_top;
    REBLEN n;
    for (n = len_old; n < len_new; ++n, ++poison)
        Force_Poison_Cell(poison);
    assert(poison == Flex_Tail(Slot, g_ds.array));
  #endif

    g_ds.movable_tail = Flex_Tail(Slot, g_ds.array);  // next expansion point
}


//
//  Pop_Stack_Values_Core: C
//
// Pops computed values from the stack to make a new ARRAY.
//
// 1. The Pop has CELL_MASK_ALL semantics, so CELL_FLAG_NOTE or _MARKED or
//    _FORMAT etc. are all copied.  There is no other option, because the
//    release build uses memcpy() to implement this.  Assume it's what the
//    caller wants.
//
Array* Pop_Stack_Values_Core(Flags flags, StackIndex base) {
    Assert_No_DataStack_Pointers_Extant();  // in the future, pop may disrupt

    Length len = TOP_INDEX - base;
    Array* a = Make_Array_Core(flags, len);
    Set_Flex_Len(a, len);

    Cell* src = Data_Stack_At(Cell, base + 1);  // moving, not const!
    Cell* dest = Flex_Head(Cell, a);

  #if NO_RUNTIME_CHECKS  // Stack cells lack CELL_MASK_PERSIST, can memcpy()
    STATIC_ASSERT(! DEBUG_POISON_DROPPED_STACK_CELLS);
    Mem_Copy(dest, src, len * sizeof(Cell));  // CELL_MASK_ALL semantics [1]
  #else
    Flavor flavor = Stub_Flavor(a);  // flavor comes from flags

    Count count = 0;
    for (; count < len; ++count, ++src, ++dest) {
        possibly(src->header.bits & CELL_MASK_PERSIST);  // all bits copied [1]
        if (LIFT_BYTE(src) >= MIN_LIFTBYTE_ANTIFORM) {  // ok in *some* arrays
            possibly(LIFT_BYTE(src) == BEDROCK_255);
            if (flavor < MIN_FLAVOR_ANTIFORMS_OK)
                crash ("Unexpected antiform found on data stack");
        }

        Blit_Cell_Untracked(dest, src);

        #if DEBUG_TRACK_EXTEND_CELLS
          #if (DEBUG_TRACK_COPY_PRESERVES)
              // Move_Cell() would already copy the original tracking
          #else
              dont(dest->track_flags = src->track_flags);  // see definition
              dest->file = src->file;
              dest->line = src->line;
              dest->tick = src->tick;
          #endif
        #endif

        #if DEBUG_POISON_DROPPED_STACK_CELLS
          Force_Poison_Cell(src);
        #endif
    }
  #endif

    g_ds.index -= len;
    g_ds.movable_top -= len;

    return a;
}
