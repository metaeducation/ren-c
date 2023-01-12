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
    ensureNullptr(DS_Array) = Make_Array_Core(
        1,
        FLAG_FLAVOR(DATASTACK) | SERIES_FLAGS_NONE
    );
    SET_SERIES_LEN(DS_Array, 1);
    assert(NOT_SERIES_FLAG(DS_Array, DYNAMIC));

    Cell(*) head = ARR_HEAD(DS_Array);
    assert(Is_Cell_Erased(head));  // non-dynamic array, length 1 indicator
    Init_Trash(head);

    // The tail marker will signal PUSH() that it has run out of space,
    // and it will perform the allocation at that time.
    //
    DS_Movable_Tail = ARR_TAIL(DS_Array);

    // Reuse the expansion logic that happens on a PUSH() to get the
    // initial stack size.  It requires you to be on an END to run.
    //
    DS_Index = 1;
    DS_Movable_Top = SPECIFIC(ARR_AT(DS_Array, DS_Index));  // no Cells
    Expand_Data_Stack_May_Fail(capacity);

    DROP();  // drop the hypothetical thing that triggered the expand

    assert(GET_SERIES_FLAG(DS_Array, DYNAMIC));
    Poison_Cell(ARR_HEAD(DS_Array));  // new head
}


//
//  Shutdown_Data_Stack: C
//
void Shutdown_Data_Stack(void)
{
    assert(TOP_INDEX == 0);
    assert(Is_Cell_Poisoned(ARR_HEAD(DS_Array)));

    Free_Unmanaged_Series(DS_Array);
    DS_Array = nullptr;
}


//
//  Startup_Feeds: C
//
void Startup_Feeds(void)
{
    PG_Feed_At_End.header.bits = NODE_FLAG_NODE | NODE_FLAG_STALE;

    TG_End_Feed = Make_Array_Feed_Core(EMPTY_ARRAY, 0, SPECIFIED);
}

//
//  Shutdown_Feeds: C
//
void Shutdown_Feeds(void) {
    PG_Feed_At_End.header.bits = 0;

    Free_Feed(TG_End_Feed);
    TG_End_Feed = nullptr;
}


//
//  Startup_Frame_Stack: C
//
// 1. We always push one unused frame at the top of the stack.  This way, it
//    is not necessary for unused frames to check if `f->prior` is null; it
//    may be assumed that it never is.
//
// 2. Also: since frames are needed to track API handles, this permits making
//    API handles for things that come into existence at boot and aren't freed
//    until shutdown, as they attach to this frame.
//
void Startup_Frame_Stack(void)
{
    assert(TG_Top_Frame == nullptr);
    assert(TG_Bottom_Frame == nullptr);

    Frame(*) f = Make_End_Frame(FRAME_MASK_NONE);  // ensure f->prior, see [1]
    Push_Frame(nullptr, f);  // global API handles attach here, see [2]

    TRASH_POINTER_IF_DEBUG(f->prior);  // catches enumeration past BOTTOM_FRAME
    TG_Bottom_Frame = f;

    assert(TOP_FRAME == f and BOTTOM_FRAME == f);
}


//
//  Shutdown_Frame_Stack: C
//
// 1. To stop enumerations from using nullptr to stop the walk, and not count
//    the bottom frame as a "real stack level", it had a trash pointer put
//    in the debug build.  Restore it to a typical null before the drop.
//
// 2. There's a Catch-22 on checking the balanced state for outstanding
//    manual series allocations, e.g. it can't check *before* the mold buffer
//    is freed because it would look like it was a leaked series, but it
//    can't check *after* because the mold buffer balance check would crash.
//
void Shutdown_Frame_Stack(void)
{
    assert(TOP_FRAME == BOTTOM_FRAME);

    assert(IS_POINTER_TRASH_DEBUG(TG_Bottom_Frame->prior));  // trash, see [1]
    TG_Bottom_Frame->prior = nullptr;

  blockscope {
    Frame(*) f = TOP_FRAME;
    Drop_Frame_Core(f);  // can't Drop_Frame()/Drop_Frame_Unbalanced(), see [2]
    assert(not TOP_FRAME);
  }

    TG_Top_Frame = nullptr;
    TG_Bottom_Frame = nullptr;

  #if !defined(NDEBUG)
  blockscope {
    Segment* seg = Mem_Pools[FRAME_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Count n = Mem_Pools[FRAME_POOL].num_units_per_segment;
        Byte* unit = cast(Byte*, seg + 1);

        for (; n > 0; --n, unit += Mem_Pools[FRAME_POOL].wide) {
            Frame(*) f = cast(Frame(*), unit);  // ^-- pool size may round up
            if (IS_FREE_NODE(f))
                continue;
          #if DEBUG_COUNT_TICKS
            printf(
                "** FRAME LEAKED at tick %lu\n",
                cast(unsigned long, f->tick)
            );
          #else
            assert(!"** FRAME LEAKED but DEBUG_COUNT_TICKS not enabled");
          #endif
        }
    }
  }
  #endif

  #if !defined(NDEBUG)
  blockscope {
    Segment* seg = Mem_Pools[FEED_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        REBLEN n = Mem_Pools[FEED_POOL].num_units_per_segment;
        Byte* unit = cast(Byte*, seg + 1);

        for (; n > 0; --n, unit += Mem_Pools[FEED_POOL].wide) {
            Feed(*) feed = cast(Feed(*), unit);
            if (IS_FREE_NODE(feed))
                continue;
          #if DEBUG_COUNT_TICKS
            printf(
                "** FEED LEAKED at tick %lu\n",
                cast(unsigned long, feed->tick)
            );
          #else
            assert(!"** FEED LEAKED but no DEBUG_COUNT_TICKS enabled\n");
          #endif
        }
    }
  }
  #endif
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
Context(*) Get_Context_From_Stack(void)
{
    Frame(*) f = TOP_FRAME;
    Action(*) phase = nullptr; // avoid potential uninitialized variable warning

    for (; f != BOTTOM_FRAME; f = f->prior) {
        if (not Is_Action_Frame(f))
            continue;

        phase = FRM_PHASE(f);
        break;
    }

    if (f == BOTTOM_FRAME) {
        //
        // No natives are in effect, so this is API code running directly from
        // an `int main()`.  Previously this always ran in the user context,
        // but the user context is now no longer available during much of the
        // boot...so we fall back to the Lib_Context during boot.
        //
        // Note: This can be dangerous if no rebRescue() or TRAP is in effect.
        //
        return User_Context != nullptr ? User_Context : Lib_Context;
    }

    // This would happen if you call the API from something like a traced
    // eval hook, or a Func_Dispatcher().  For now, just assume that means
    // you want the code to bind into the lib context.
    //
    if (not Is_Action_Native(phase))
        return Lib_Context;

    Array(*) details = ACT_DETAILS(phase);
    REBVAL *context = DETAILS_AT(details, IDX_NATIVE_CONTEXT);
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
// StackIndex "data stack indices" and not by REBVAL* across *any* operation
// which could do a push or pop.  (Currently stable w.r.t. pop but there may
// be compaction at some point.)
//
void Expand_Data_Stack_May_Fail(REBLEN amount)
{
    REBLEN len_old = ARR_LEN(DS_Array);

    // The current requests for expansion should only happen when the stack
    // is at its end.  Sanity check that.
    //
    assert(len_old == DS_Index);
    assert(DS_Movable_Top == ARR_TAIL(DS_Array));
    assert(
        cast(Cell(*), DS_Movable_Top) - ARR_HEAD(DS_Array)
        == cast(int, len_old)
    );

    // If adding in the requested amount would overflow the stack limit, then
    // give a data stack overflow error.
    //
    if (SER_REST(DS_Array) + amount >= STACK_LIMIT) {
        //
        // Because the stack pointer was incremented and hit the END marker
        // before the expansion, we have to decrement it if failing.
        //
        --DS_Index;
        Fail_Stack_Overflow(); // !!! Should this be a "data stack" message?
    }

    Extend_Series_If_Necessary(DS_Array, amount);

    // Update the pointer used for fast access to the top of the stack that
    // likely was moved by the above allocation (needed before using TOP)
    //
    DS_Movable_Top = cast(REBVAL*, ARR_AT(DS_Array, DS_Index));

    REBLEN len_new = len_old + amount;
    SET_SERIES_LEN(DS_Array, len_new);

  #if DEBUG_POISON_DROPPED_STACK_CELLS
    REBVAL *poison = DS_Movable_Top;
    REBLEN n;
    for (n = len_old; n < len_new; ++n, ++poison)
        Poison_Cell(poison);
    assert(poison == ARR_TAIL(DS_Array));
  #endif

    // Update the end marker to serve as the indicator for when the next
    // stack push would need to expand.
    //
    DS_Movable_Tail = ARR_TAIL(DS_Array);
}


//
//  Pop_Stack_Values_Core: C
//
// Pops computed values from the stack to make a new ARRAY.
//
// !!! How can we pass in callsite file/line for tracking info?
//
Array(*) Pop_Stack_Values_Core(StackIndex base, Flags flags)
{
    ASSERT_NO_DATA_STACK_POINTERS_EXTANT();  // in the future, pop may disrupt

    Length len = TOP_INDEX - base;
    Array(*) a = Make_Array_Core(len, flags);
    SET_SERIES_LEN(a, len);

  #if DEBUG
    Flavor flavor = SER_FLAVOR(a);  // flavor comes from flags
  #endif

    Count count = 0;
    Value(*) src = Data_Stack_At(base + 1);  // not const, will be FRESHEN()
    Cell(*) dest = ARR_HEAD(a);
    for (; count < len; ++count, ++src, ++dest) {
      #if DEBUG
        if (Is_Isotope(src)) {
            assert(Is_Isotope_Stable(src));
            assert(flavor >= FLAVOR_MIN_ISOTOPES_OK);
        }
        if (VAL_TYPE_UNCHECKED(src) == REB_VOID)  // allow unreadable trash
            assert(flavor >= FLAVOR_MIN_VOIDS_OK);
      #endif

        Move_Cell_Untracked(dest, src, CELL_MASK_MOVE);

      #if DEBUG_POISON_DROPPED_STACK_CELLS
        Poison_Cell(src);
      #endif
    }

    DS_Index -= len;
    DS_Movable_Top -= len;

    return a;
}
