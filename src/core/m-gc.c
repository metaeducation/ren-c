//
//  File: %m-gc.c
//  Summary: "main memory garbage collection"
//  Section: memory
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2023 Ren-C Open Source Contributors
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
// Today's garbage collector is based on a conventional "mark and sweep",
// of series "stubs", which is how it was done in R3-Alpha:
//
//     https://en.wikipedia.org/wiki/Tracing_garbage_collection
//
// A Cell's "payload" and "extra" field may or may not contain pointers to
// stubs that the GC needs to be aware of.  Some small values like LOGIC!
// or INTEGER! don't, because they can fit the entirety of their data into the
// Cell's 4*sizeof(void) capacity...though this would change if INTEGER! added
// support for arbitrary-sized-numbers.
//
// Some cells embed Stub pointers even when the payload would technically
// fit completely in their cell.  They do this in order to create a level of
// indirection so that their data can be shared among copies of that cell.
// For instance, HANDLE! does this.
//
// "Deep" marking in R3-Alpha was originally done with recursion, and the
// recursion would stop whenever a mark was hit.  But this meant deeply nested
// structures could quickly wind up overflowing the C stack.  Consider:
//
//     a: copy []
//     repeat 200'000 [a: append copy [] a]
//     recycle
//
// The simple solution is that when an unmarked array is hit that it is
// marked and put into a queue for processing (instead of recursed on the
// spot).  This queue is then handled as soon as the marking call is exited,
// and the process repeated until no more items are queued.
//
// !!! There is actually not a specific list of roots of the garbage collect,
// so a first pass of all the Stubs must be done to find them.  This is
// because with the redesigned "RL_API" in Ren-C, singular array stubs do
// double duty as lifetime-managed containers for cells handed out by the
// API--without requiring a separate series data allocation.  These could be
// in their own "pool", but that would prevent mingling and reuse among Stubs
// used for other purposes like series.  Review in light of any new garbage
// collect approaches used.
//

#include "sys-core.h"

#include "sys-int-funcs.h"


// The reason the LINK() and MISC() macros are so weird is because regardless
// of who assigns the fields, the GC wants to be able to mark them.  So the
// same generic field must be used for all cases...which those macros help
// to keep distinct for comprehensibility purposes.
//
// But access via the GC just sees the fields as their generic nodes (though
// through a non-const point of view, to mark them).

#define LINK_Node_TYPE      Node*
#define LINK_Node_CAST

#define MISC_Node_TYPE      Node*
#define MISC_Node_CAST


#ifndef NDEBUG
    static bool in_mark = false; // needs to be per-GC thread
#endif

#define Assert_No_GC_Marks_Pending() \
    assert(Series_Used(g_gc.mark_stack) == 0)

// The mark_count double checks that every marker set by the GC is cleared.
// To avoid the cost of incrementing and decrementing, only in debug builds.
//
#if DEBUG
    inline static void Remove_GC_Mark(const Node* node) {  // stub or pairing
        assert(Is_Node_Marked(node));
        Clear_Node_Marked_Bit(node);
        g_gc.mark_count -= 1;
    }

    inline static void Remove_GC_Mark_If_Marked(const Node* node) {
        if (Is_Node_Marked(node)) {
            Clear_Node_Marked_Bit(node);
            g_gc.mark_count -= 1;
        }
    }

    inline static void Add_GC_Mark(const Node* node) {
        assert(not Is_Node_Marked(node));
        Set_Node_Marked_Bit(node);
        g_gc.mark_count += 1;
    }

    inline static void Add_GC_Mark_If_Not_Already_Marked(const Node* node) {
        if (not Is_Node_Marked(node)) {
            Set_Node_Marked_Bit(node);
            g_gc.mark_count += 1;
        }
    }
#else
    #define Remove_GC_Mark(n)                       Clear_Node_Marked_Bit(n)
    #define Remove_GC_Mark_If_Marked(n)             Clear_Node_Marked_Bit(n)
    #define Add_GC_Mark(n)                          Set_Node_Marked_Bit(n)
    #define Add_GC_Mark_If_Not_Already_Marked(n)    Set_Node_Marked_Bit(n)
#endif


static void Queue_Mark_Cell_Deep(const Cell* v);

inline static void Queue_Mark_Maybe_Fresh_Cell_Deep(const Cell* v) {
    if (not Is_Fresh(v))
        Queue_Mark_Cell_Deep(v);
}


// Ren-C's PAIR! uses a special kind of Stub (called a "Pairing") that embeds
// two cells in the stub itself--an array of fixed length 2.  It can do this
// because a cell has a uintptr_t header at the beginning of its struct--just
// like a Series--and cells reserve the NODE_FLAG_MARKED bit for the GC.  So
// pairings can stealthily participate in the marking, as long as the bit is
// cleared at the end.
//
// !!! Marking a pairing has the same recursive problems than an array does,
// while not being an array.  So technically we should queue it, but we
// don't have any real world examples of "deeply nested pairings", as they
// are used only in optimized internal structures...the PAIR! datatype only
// allows INTEGER! and DECIMAL! so you can't overflow the stack with it.
//
// Hence we cheat and don't actually queue, for now.
//
static void Queue_Mark_Pairing_Deep(const Cell* paired)
{
    // !!! Hack doesn't work generically, review

  #if !defined(NDEBUG)
    bool was_in_mark = in_mark;
    in_mark = false;  // would assert about the recursion otherwise
  #endif

    Queue_Mark_Cell_Deep(paired);
    Queue_Mark_Cell_Deep(Pairing_Second(paired));  // QUOTED? uses void

    Add_GC_Mark(paired);

  #if !defined(NDEBUG)
    in_mark = was_in_mark;
  #endif
}

static void Queue_Unmarked_Accessible_Series_Deep(const Series* s);


// This routine is given the *address* of the node to mark, so that if the
// node has become inaccessible it can be canonized into the global node for
// series that are no longer available.  This allows nodes that had been
// turned into a decayed form and only kept alive to prevent referencing
// pointers to be swept.  See Decay_Series()
//
static void Queue_Mark_Node_Deep(const Node** pp) {
    Byte nodebyte = NODE_BYTE(*pp);
    if (nodebyte & NODE_BYTEMASK_0x10_MARKED)
        return;  // may not be finished marking yet, but has been queued

    if (nodebyte & NODE_BYTEMASK_0x01_CELL) {  // e.g. a pairing
        const Value* v = x_cast(const Value*, *pp);
        if (Is_Node_Managed(v))
            Queue_Mark_Pairing_Deep(v);
        else {
            // !!! It's a frame?  API handle?  Skip frame case (keysource)
            // for now, but revisit as technique matures.
        }
        return;  // it's 2 cells, sizeof(Stub), but no room for a Stub's data
    }

    if (Not_Node_Accessible(*pp)) {
        //
        // All inaccessible nodes are collapsed and canonized into a universal
        // inaccessible node so the stub can be freed.
        //
        // Note: This strategy created some friction when bound words depended
        // on contexts to supply their spellings.  This would have required
        // actually moving the spellings back into them, but noting that the
        // word itself was no longer bound through some flag in the cell.  For
        // now it's not an issue since that optimization was removed, but a
        // similar issue could arise again.
        //
        // !!! Here we are setting things that may already be the canon
        // inaccessible series which may not be efficient.  Review.
        //
        *pp = &PG_Inaccessible_Stub;
        return;
    }

    const Series* s = x_cast(const Series*, *pp);

 #if !defined(NDEBUG)
    if (Is_Node_Free(s))
        panic (s);

    if (Not_Node_Managed(s)) {
        printf("Link to non-MANAGED item reached by GC\n");
        panic (s);
    }
  #endif

    Queue_Unmarked_Accessible_Series_Deep(s);
}

// This is a generic mark routine, which can sense what type a node is and
// automatically figure out how to mark it.  It takes into account if the
// series was created by an extension and poked nodes into the `custom`
// fields of LINK() and MISC(), which is the only way to "hook" the GC.
//
// (Note: The data structure used for this processing is a "stack" and not
// a "queue".  But when you use 'queue' as a verb, it has more leeway than as
// the CS noun, and can just mean "put into a list for later processing".)
//
// !!! The reason queueing is used was because things were generating stack
// overflows for deeply nested structures.  With the generic marking of fields
// like LINK() and MISC(), the chains are now becoming long enough just through
// that to generate some deep stacks...even without any cells being marked.
// It hasn't caused any crashes yet, but is something that bears scrutiny.
//
static void Queue_Unmarked_Accessible_Series_Deep(const Series* s)
{
    Assert_Node_Accessible(s);

    Add_GC_Mark(s);

  //=//// MARK LINK AND MISC IF DESIRED ////////////////////////////////////=//

    // All nodes have both link and misc fields available, but they don't
    // necessarily hold node pointers (and even if they do, they may not be
    // references that are intended to keep them live).  So the series header
    // flags control whether the marking is done or not.

    if (Get_Series_Flag(s, LINK_NODE_NEEDS_MARK) and s->link.any.node)
        Queue_Mark_Node_Deep(&m_cast(Series*, s)->link.any.node);

    if (Get_Series_Flag(s, MISC_NODE_NEEDS_MARK) and s->misc.any.node)
        Queue_Mark_Node_Deep(&m_cast(Series*, s)->misc.any.node);

  //=//// MARK INODE IF NOT USED FOR INFO //////////////////////////////////=//

    // In the case of the INFO/INODE slot, the setting of the needing mark
    // flag is what determines whether the slot is used for info or not.  So
    // if it's available for non-info uses, it is always a live marked node.

    if (Get_Series_Flag(s, INFO_NODE_NEEDS_MARK) and node_INODE(Node, s))
        Queue_Mark_Node_Deep(&m_cast(Series*, s)->info.node);

    if (IS_KEYLIST(s)) {
        //
        // !!! KeyLists may not be the only category that are just a straight
        // list of node pointers.
        //
        const Key* tail = Series_Tail(Key, s);
        const Key* key = Series_Head(Key, s);
        for (; key != tail; ++key) {
            //
            // Symbol* are not available to the user to free out from under
            // a keylist (can't use FREE on them) and shouldn't vanish.
            //
            Assert_Node_Accessible(*key);
            if (Is_Node_Marked(*key))
                continue;
            Queue_Unmarked_Accessible_Series_Deep(*key);
        }
    }
    else if (Is_Series_Array(s)) {
        Array* a = x_cast(Array*, s);

    //=//// MARK BONUS (if not using slot for `bias`) /////////////////////=//

        // Whether the bonus slot needs to be marked is dictated by internal
        // series type, not an extension-usable flag (due to flag scarcity).
        //
        if (IS_VARLIST(a) and node_BONUS(Node, s)) {
            //
            // !!! The keysource for varlists can be set to a Level*, which
            // at the moment pretends to be a cell to distinguish itself.
            // This makes less sense than pretending to be a series that is
            // already marked, and has a detectable FLAVOR_XXX.  Review.
            //
            if (Is_Non_Cell_Node_A_Level(node_BONUS(Node, s)))
                goto skip_mark_frame_bonus;

            assert(IS_KEYLIST(cast(Series*, node_BONUS(Node, a))));

            Queue_Mark_Node_Deep(
                &m_cast(Series*, s)->content.dynamic.bonus.node
            );
        }

        skip_mark_frame_bonus:

    //=//// MARK ARRAY ELEMENT CELLS (if array) ///////////////////////////=//

        // Submits the array into the deferred stack to be processed later
        // with Propagate_All_GC_Marks().  If it were not queued and just used
        // recursion (as R3-Alpha did) then deeply nested arrays could
        // overflow the C stack.
        //
        // !!! Could the amount of C stack space available be used for some
        // amount of recursion, and only queue if running up against a limit?
        //
        // !!! Should this use a "bumping a NULL at the end" technique to
        // grow, like the data stack?
        //
        if (Is_Series_Full(g_gc.mark_stack))
            Extend_Series_If_Necessary(g_gc.mark_stack, 8);
        *Series_At(Array*, g_gc.mark_stack, Series_Used(g_gc.mark_stack)) = a;
        Set_Series_Used(  // doesn't add a terminator
            g_gc.mark_stack,
            Series_Used(g_gc.mark_stack) + 1
        );
    }
}


//
//  Queue_Mark_Cell_Deep: C
//
static void Queue_Mark_Cell_Deep(const Cell* c)
{
  #if DEBUG_UNREADABLE_CELLS
    if (Is_Unreadable_Debug(c))  // tolerate write-only cells in debug builds
        return;
  #endif

    assert(not Is_Fresh(c));

    // We mark based on the type of payload in the cell, e.g. its "unescaped"
    // form.  So if '''a fits in a WORD! (despite being a QUOTED?), we want
    // to mark the cell as if it were a plain word.  Use the Cell_Heart().
    //
    Heart heart = Cell_Heart(c);

  #if !defined(NDEBUG)  // see Queue_Mark_Node_Deep() for notes on recursion
    assert(not in_mark);
    in_mark = true;
  #endif

    if (Is_Extra_Mark_Kind(heart) and c->extra.Any.node)
        Queue_Mark_Node_Deep(&m_cast(Cell*, c)->extra.Any.node);

    if (Get_Cell_Flag_Unchecked(c, FIRST_IS_NODE) and Cell_Node1(c))
        Queue_Mark_Node_Deep(&PAYLOAD(Any, m_cast(Cell*, c)).first.node);

    if (Get_Cell_Flag_Unchecked(c, SECOND_IS_NODE) and Cell_Node2(c))
        Queue_Mark_Node_Deep(&PAYLOAD(Any, m_cast(Cell*, c)).second.node);

  #if !defined(NDEBUG)
    in_mark = false;
    Assert_Cell_Marked_Correctly(c);
  #endif
}


//
//  Propagate_All_GC_Marks: C
//
// The Mark Stack is a series containing series pointers.  They have already
// had their SERIES_FLAG_MARK set to prevent being added to the stack multiple
// times, but the items they can reach are not necessarily marked yet.
//
// Processing continues until all reachable items from the mark stack are
// known to be marked.
//
static void Propagate_All_GC_Marks(void)
{
    assert(not in_mark);

    while (Series_Used(g_gc.mark_stack) != 0) {
        Set_Series_Used(g_gc.mark_stack, Series_Used(g_gc.mark_stack) - 1);

        // Data pointer may change in response to an expansion during
        // Mark_Array_Deep_Core(), so must be refreshed on each loop.
        //
        Array* a = *Series_At(
            Array*,
            g_gc.mark_stack,
            Series_Used(g_gc.mark_stack)
        );

        // Termination is not required in the release build (the length is
        // enough to know where it ends).  But corrupt in debug.
        //
        Corrupt_Pointer_If_Debug(
            *Series_At(
                Array*,
                g_gc.mark_stack,
                Series_Used(g_gc.mark_stack)
            )
        );

        // We should have marked this series at queueing time to keep it from
        // being doubly added before the queue had a chance to be processed
        //
        assert(Is_Node_Marked(a));

        Cell* v = Array_Head(a);
        const Cell* tail = Array_Tail(a);
        for (; v != tail; ++v) {
          #if DEBUG
            Flavor flavor = Series_Flavor(a);
            assert(flavor <= FLAVOR_MAX_ARRAY);

            switch (QUOTE_BYTE(v)) {
              case ANTIFORM_0:
                if (flavor < FLAVOR_MIN_ISOTOPES_OK)
                    panic (v);  // antiforms not legal in many array types

                if (Is_Antiform_Unstable(cast(Atom*, v)))  // always illegal
                    panic (v);
                break;

              case NOQUOTE_1:
                if (HEART_BYTE(v) == REB_VOID) {
                    if (flavor < FLAVOR_MIN_VOIDS_OK)
                        panic (v);  // voids not legal in many array types
                }
                break;

              default:
                 break;
            }
          #endif

            Queue_Mark_Cell_Deep(v);
        }

      #if !defined(NDEBUG)
        Assert_Array_Marked_Correctly(a);
      #endif
    }
}


//
//  Reify_Variadic_Feed_As_Array_Feed: C
//
// For performance and memory usage reasons, a variadic C function call that
// wants to invoke the evaluator with just a comma-delimited list of Value*
// does not need to make a series to hold them.  Fetch_Next_In_Feed() is
// written to use the va_list traversal as an alternative.
//
// However, va_lists cannot be backtracked once advanced.  So in a debug mode
// it can be helpful to turn all the va_lists into arrays before running
// them, so stack levels can be inspected more meaningfully--both for upcoming
// evaluations and those already past.
//
// Because items may well have already been consumed from the va_list() that
// can't be gotten back, we put in a marker to help hint at the truncation
// (unless told that it's not truncated, e.g. a debug mode that calls it
// before any items are consumed).
//
void Reify_Variadic_Feed_As_Array_Feed(
    Feed* feed,
    bool truncated
){
    assert(FEED_IS_VARIADIC(feed));

    StackIndex base = TOP_INDEX;

    if (Not_Feed_At_End(feed)) {
        if (truncated)
            Init_Quasi_Word(PUSH(), Canon(OPTIMIZED_OUT));

        do {
            Derelativize(PUSH(), At_Feed(feed), FEED_SPECIFIER(feed));
            assert(not Is_Void(TOP));
            assert(not Is_Antiform(TOP));
            Fetch_Next_In_Feed(feed);
        } while (Not_Feed_At_End(feed));

        assert(TOP_INDEX != base);
        if (FEED_IS_VARIADIC(feed))  // UTF-8 scan may have finalized it
            Finalize_Variadic_Feed(feed);

        Index index = truncated ? 2 : 1;  // skip --optimized-out--

        Array* a = Pop_Stack_Values_Core(base, NODE_FLAG_MANAGED);
        Init_Array_Cell_At(FEED_SINGLE(feed), REB_BLOCK, a, index);

        // need to be sure feed->p isn't invalid... and not end

        if (truncated)
            feed->p = Array_At(FEED_ARRAY(feed), 1);  // skip trunc
        else
            feed->p = Array_Head(FEED_ARRAY(feed));

        assert(READABLE(At_Feed(feed)));  // not end at start, not end now

        // The array just popped into existence, and it's tied to a running
        // level...so safe to say we're holding it.
        //
        assert(Not_Feed_Flag(feed, TOOK_HOLD));
        Set_Series_Info(FEED_ARRAY(feed), HOLD);
        Set_Feed_Flag(feed, TOOK_HOLD);
    }
    else {
        Finalize_Variadic_Feed(feed);

        if (truncated) {
            Init_Quasi_Word(PUSH(), Canon(OPTIMIZED_OUT));

            Array* a = Pop_Stack_Values_Core(base, NODE_FLAG_MANAGED);
            Init_Array_Cell_At(FEED_SINGLE(feed), REB_BLOCK, a, 1);
        }
        else
            Init_Array_Cell_At(FEED_SINGLE(feed), REB_BLOCK, EMPTY_ARRAY, 0);

        feed->p = &PG_Feed_At_End;
    }

    assert(FEED_INDEX(feed) <= cast(Index, Array_Len(FEED_ARRAY(feed))));
}


//
//  Run_All_Handle_Cleaners: C
//
// !!! There's an issue with handles storing pointers to rebMalloc()'d data,
// which is that they want to do their cleanup work before the system is
// damaged by the shutdown process.  This is a naive extra pass done during
// shutdown to deal with the problem--but it should be folded in with
// Mark_Root_Series().
//
void Run_All_Handle_Cleaners(void) {
    Segment* seg = g_mem.pools[STUB_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Byte* unit = cast(Byte*, seg + 1);
        Length n = g_mem.pools[STUB_POOL].num_units_per_segment;
        for (; n > 0; --n, unit += sizeof(Stub)) {
            //
            // !!! A smarter switch statement here could do this more
            // optimally...see the sweep code for an example.
            //
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;

            if (unit[0] & NODE_BYTEMASK_0x01_CELL)
                continue;  // assume no handles in pairings, for now?

            if (Not_Node_Accessible(u_cast(Node*, unit)))
                continue;

            Stub* stub = cast(Stub*, unit);
            if (stub == g_ds.array)
                continue;
            if (not Is_Series_Array(stub))
                continue;

            const Cell* item_tail = Array_Tail(cast(Array*, stub));
            Cell* item = Array_Head(cast(Array*, stub));
            for (; item != item_tail; ++item) {
              #if DEBUG_UNREADABLE_CELLS
                if (Is_Unreadable_Debug(item))
                    continue;
              #endif
                if (Cell_Heart(item) != REB_HANDLE)
                    continue;
                if (Not_Cell_Flag(item, FIRST_IS_NODE))
                    continue;
                if (Not_Node_Accessible(Cell_Node1(item)))
                    continue;
                Stub* handle_stub = VAL_HANDLE_STUB(item);
                Decay_Series(handle_stub);
            }
        }
    }
}


//
//  Mark_Root_Series: C
//
// Root Series are any manual series that were allocated but have not been
// managed yet, as well as Alloc_Value() nodes that are explicitly "roots".
//
// For root nodes, this checks to see if their lifetime was dependent on a
// FRAME!, and if that frame is no longer on the stack.  If so, it (currently)
// will panic if that frame did not end due to a fail().  This could be
// relaxed to automatically free those nodes as a normal GC.
//
// !!! This implementation walks over *all* the stubs.  It wouldn't have to
// if API nodes were in their own pool, or if the outstanding manuals list
// were maintained even in non-debug builds--it could just walk those.  This
// should be weighed against background GC and other more sophisticated
// methods which might come down the road for the GC than this simple one.
//
// !!! A smarter switch statement here could do this more
// optimally...see the sweep code for an example.
//
static void Mark_Root_Series(void)
{
    Segment* seg = g_mem.pools[STUB_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Byte* unit = cast(Byte*, seg + 1);
        Length n = g_mem.pools[STUB_POOL].num_units_per_segment;

        for (; n > 0; --n, unit += sizeof(Stub)) {
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;

            assert(unit[0] & NODE_BYTEMASK_0x80_NODE);

            if (unit[0] & NODE_BYTEMASK_0x01_CELL) {  // a "Pairing"
                if (unit[0] & NODE_BYTEMASK_0x20_MANAGED)
                    continue; // PAIR! or other value will mark it

                assert(!"unmanaged pairings not believed to exist yet");

                Value* paired = x_cast(Value*, cast(void*, unit));
                Queue_Mark_Cell_Deep(paired);
                Queue_Mark_Cell_Deep(Pairing_Second(paired));
                continue;
            }

            if (Is_Node_Free(cast(Node*, unit)))
                continue;

            Series* s = cast(Series*, unit);

            if (Is_Node_Root_Bit_Set(s)) {

                // This stub came from Alloc_Value() or rebMalloc(); the only
                // references should be from the C stack.  So this pass is the
                // only place where these stubs could be marked.

                if (Not_Node_Managed(s)) {
                    //
                    // If it's not managed, don't mark it (don't have to)
                    //
                    assert(not Is_Node_Marked(s));
                }
                else {
                    Add_GC_Mark(s);
                }

                if (Is_Series_Array(s)) {  // It's an Alloc_Value()
                    //
                    // 1. Mark_Level_Stack_Deep() marks the owner.
                    //
                    // 2. Evaluation may target API cells, may be Is_Fresh().
                    // (They should only be fresh if they are targeted by some
                    // Level's L->out...could we verify that?)
                    //
                    Queue_Mark_Maybe_Fresh_Cell_Deep(Stub_Cell(s));  // [2]
                }
                else {  // It's a rebMalloc()
                    assert(Series_Flavor(s) == FLAVOR_BINARY);
                }

                continue;
            }

            // !!! The g_ds.array does not currently keep its `used` field up
            // to date, because this is one more than the g_ds.index and would
            // require math to be useful.  (While the array tends to want to
            // know the length and the tail pointer to stop at, clients of
            // the stack want to know the "last".)  Hence it is exempt from
            // this marking rather than keeping the length up to date.  Review.
            //
            if (
                Is_Series_Array(s)
                and s != g_ds.array  // !!! Review g_ds.array exemption!
            ){
                if (Is_Node_Managed(s))
                    continue;  // BLOCK! or OBJECT! etc. holding it should mark

                Array* a = cast(Array*, s);

                if (IS_VARLIST(a))
                    if (CTX_TYPE(cast(Context*, a)) == REB_FRAME)
                        continue;  // Mark_Level_Stack_Deep() etc. mark it

                // This means someone did something like Make_Array() and then
                // ran an evaluation before referencing it somewhere from the
                // root set.  (Review: Should this be supported?  If so,
                // couldn't we just walk GC_manuals?)

                // Only plain arrays are supported as unmanaged across
                // evaluations, because Context and REBACT and REBMAP are too
                // complex...they must be managed before evaluations happen.
                // Manage and use Push_GC_Guard and Drop_GC_Guard on them.
                //
                assert(
                    not IS_VARLIST(a)
                    and not IS_DETAILS(a)
                    and not IS_PAIRLIST(a)
                );

                if (Get_Series_Flag(a, LINK_NODE_NEEDS_MARK))
                    if (node_LINK(Node, a))
                        Queue_Mark_Node_Deep(&a->link.any.node);
                if (Get_Series_Flag(a, MISC_NODE_NEEDS_MARK))
                    if (node_MISC(Node, a))
                        Queue_Mark_Node_Deep(&a->misc.any.node);

                const Cell* item_tail = Array_Tail(a);
                Cell* item = Array_Head(a);
                for (; item != item_tail; ++item)
                    Queue_Mark_Cell_Deep(item);
            }

            // At present, no handling for unmanaged STRING!, BINARY!, etc.
            // This would have to change, e.g. if any of other types stored
            // something on the heap in their LINK() or MISC()
        }

        Propagate_All_GC_Marks(); // !!! is propagating on each segment good?
    }
}


//
//  Mark_Data_Stack: C
//
// The data stack usually has unused capacity in its array allocation.  But
// it only marks the live cells--not all the way to the tail.  The unused
// cells can just have garbage unless DEBUG_POISON_DROPPED_STACK_CELLS.
//
static void Mark_Data_Stack(void)
{
    const Cell* head = Series_Head(Cell, g_ds.array);  // unstable allowed
    assert(Is_Cell_Poisoned(head));  // Data_Stack_At(0) is deliberately invalid

    Cell* stackval = g_ds.movable_top;
    for (; stackval != head; --stackval)  // stop before Data_Stack_At(0)
        Queue_Mark_Cell_Deep(stackval);

  #if DEBUG_POISON_DROPPED_STACK_CELLS
    stackval = g_ds.movable_top + 1;
    for (; stackval != Series_Tail(Cell, g_ds.array); ++stackval)
        assert(Is_Cell_Poisoned(stackval));
  #endif

    Propagate_All_GC_Marks();
}


//
//  Mark_Guarded_Nodes: C
//
// Mark series and values that have been temporarily protected from garbage
// collection with Push_GC_Guard.  Subclasses e.g. ARRAY_IS_CONTEXT will
// have their LINK() and MISC() fields guarded appropriately for the class.
//
static void Mark_Guarded_Nodes(void)
{
    const Node* *np = Series_Head(const Node*, g_gc.guarded);
    REBLEN n = Series_Used(g_gc.guarded);
    for (; n > 0; --n, ++np) {
        if (Is_Node_A_Cell(*np)) {
            //
            // !!! Guarding a cell means keeping its contents alive...the
            // cell is assumed to not live in a series or pairing.  Marks
            // on the cell itself are not covered... if this happens, treat
            // it as a bug.
            //
            assert(Not_Node_Marked(*np));

            Queue_Mark_Maybe_Fresh_Cell_Deep(x_cast(const Value*, *np));
        }
        else  // a series stub
            Queue_Mark_Node_Deep(np);

        Propagate_All_GC_Marks();
    }
}


//
//  Mark_Level_Stack_Deep: C
//
// Mark values being kept live by all stack levels.  If a function is running,
// then this will keep the function itself live, as well as the arguments.
// There is also an "out" slot--which may point to an arbitrary cell
// on the C stack (and must contain valid GC-readable bits at all times).
//
// Since function argument slots are not pre-initialized, how far the function
// has gotten in its fulfillment must be taken into account.  Only those
// argument slots through points of fulfillment may be GC protected.
//
// This should be called at the top level, not from Propagate_All_GC_Marks().
// All marks will be propagated.
//
static void Mark_Level_Stack_Deep(void)
{
    Level* L = TOP_LEVEL;

    while (true) {  // mark all levels (even BOTTOM_LEVEL)
        //
        // Note: MISC_PENDING() should either live in FEED_ARRAY(), or
        // it may be corrupt (e.g. if it's an apply).  GC can ignore it.
        //
        Stub* singular = FEED_SINGULAR(L->feed);
        do {
            Queue_Mark_Cell_Deep(Stub_Cell(singular));
            singular = LINK(Splice, singular);
        } while (singular);

        // !!! This used to mark L->feed->p; but we probably do not need to.
        // All variadics are reified as arrays in the GC (we could avoid this
        // using va_copy, but probably not worth it).  All values in feed
        // should be covered in terms of GC protection.

        Specifier* L_specifier = Level_Specifier(L);

        // If ->gotten is set, it usually shouldn't need markeding because
        // it's fetched via L->value and so would be kept alive by it.  Any
        // code that a level runs that might disrupt that relationship so it
        // would fetch differently should have meant clearing ->gotten.
        //
        if (L->feed->gotten)
            assert(L->feed->gotten == Lookup_Word(At_Level(L), L_specifier));

        if (
            L_specifier != SPECIFIED
            and (L_specifier->leader.bits & NODE_FLAG_MANAGED)
        ){
            // Expand L_specifier.
            //
            // !!! Should this instead check that it isn't inaccessible?
            //
            Queue_Mark_Node_Deep(&FEED_SINGLE(L->feed)->extra.Any.node);
        }

        // L->out can be nullptr at the moment, when a level is created that
        // can ask for a different output each evaluation.
        //
        if (L->out)  // output is allowed to be FRESHEN()
            Queue_Mark_Maybe_Fresh_Cell_Deep(L->out);

        // Level temporary cell should always contain initialized bits, as
        // Make_Level() sets it up and no one is supposed to corrupt it.
        //
        Queue_Mark_Maybe_Fresh_Cell_Deep(&L->feed->fetched);
        Queue_Mark_Maybe_Fresh_Cell_Deep(&L->spare);

        if (L->executor == &Evaluator_Executor) {  // has extra GC-safe cell
            Queue_Mark_Maybe_Fresh_Cell_Deep(&L->u.eval.current);
            goto propagate_and_continue;
        }

        if (not Is_Action_Level(L)) {
            //
            // Consider something like `eval copy '(recycle)`, because
            // while evaluating the group it has no anchor anywhere in the
            // root set and could be GC'd.  The Level's array ref is it.
            //
            goto propagate_and_continue;
        }

        Queue_Mark_Node_Deep(  // L->u.action.original is never nullptr
            cast(const Node**, m_cast(const Action**, &L->u.action.original))
        );

        if (L->label) { // nullptr if anonymous
            const Symbol* sym = unwrap(L->label);
            if (not Is_Node_Marked(sym))
                Queue_Unmarked_Accessible_Series_Deep(sym);
        }

        if (L->varlist and Is_Node_Managed(L->varlist)) {
            //
            // If the context is all set up with valid values and managed,
            // then it can just be marked normally...no need to do custom
            // partial parameter traversal.
            //
            assert(
                not Is_Level_Fulfilling(L)
                or Level_State_Byte(L) == ST_ACTION_TYPECHECKING  // filled/safe
            );

            // "may not pass cast(Context*) test in DEBUG_CHECK_CASTS"
            //
            Queue_Mark_Node_Deep(
                cast(const Node**, m_cast(const Array**, &L->varlist))
            );
            goto propagate_and_continue;
        }

        if (
            Is_Level_Fulfilling(L)
            and (
                Level_State_Byte(L) == ST_ACTION_INITIAL_ENTRY
                or Level_State_Byte(L) == ST_ACTION_INITIAL_ENTRY_ENFIX
            )
        ){
            goto propagate_and_continue;  // args and locals poison/garbage
        }

        Phase* phase; // goto would cross initialization
        phase = Level_Phase(L);
        const Key* key;
        const Key* key_tail;
        key = ACT_KEYS(&key_tail, phase);

        if (
            Is_Level_Fulfilling(L)
            and Not_Executor_Flag(ACTION, L, DOING_PICKUPS)
        ){
            key_tail = L->u.action.key + 1;  // key may be fresh
        }

        Value* arg;
        for (arg = Level_Args_Head(L); key != key_tail; ++key, ++arg) {
            if (not Is_Fresh(arg)) {
                Queue_Mark_Cell_Deep(arg);
                continue;
            }

            if (Is_Level_Fulfilling(L)) {
                assert(key == L->u.action.key);
                continue;
            }

            // Natives are allowed to use their locals as evaluation targets,
            // which can be fresh at various times when the GC sees them.
            // But we want the debug build to catch cases where erased cells
            // appear anywhere that the user might encounter them.
            //
            Action* action = cast(Action*, cast(Series*, phase));
            assert(Is_Action_Native(action));
            Param* param = ACT_PARAMS_HEAD(action);
            param += (key - ACT_KEYS_HEAD(action));
            assert(Is_Specialized(param));
            UNUSED(param);
            UNUSED(action);
        }

      propagate_and_continue:;

        Propagate_All_GC_Marks();
        if (L == BOTTOM_LEVEL)
            break;

        L = L->prior;
    }
}


//
//  Sweep_Series: C
//
// Scans all series nodes (Stub structs) in all segments that are part of
// the STUB_POOL.  If a series had its lifetime management delegated to the
// garbage collector with Manage_Series(), then if it didn't get "marked" as
// live during the marking phase then free it.
//
//////////////////////////////////////////////////////////////////////////////
//
// 1. We use a generic byte pointer (unsigned char*) to dodge the rules for
//    strict aliases, as the pool contain pairs of Cell from Alloc_Pairing(),
//    or a Series from Prep_Stub().  The shared first byte node masks are
//    defined and explained in %struct-node.h
//
// 2. For efficiency of memory use, Stub is nominally 2*sizeof(Cell), and
//    so pairs can use the same Stub nodes.  But features that might make the
//    two cells a size greater than Stub size require doing pairings in a
//    different pool.
//
Count Sweep_Series(void)
{
    Count sweep_count = 0;

  blockscope {
    Segment* seg = g_mem.pools[STUB_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Count n = g_mem.pools[STUB_POOL].num_units_per_segment;
        Byte* unit = cast(Byte*, seg + 1);  // byte beats strict alias [1]

        for (; n > 0; --n, unit += sizeof(Stub)) {
            switch (unit[0] >> 4) {
              case 0:
              case 1:  // 0x1
              case 2:  // 0x2
              case 3:  // 0x2 + 0x1
              case 4:  // 0x4
              case 5:  // 0x4 + 0x1
              case 6:  // 0x4 + 0x2
              case 7:  // 0x4 + 0x2 + 0x1
                //
                // NODE_FLAG_NODE (0x80 => 0x8) is clear.  This signature is
                // reserved for UTF-8 strings (corresponding to valid ASCII
                // values in the first byte), and the only exception is the
                // free-not-a-node FREE_POOLUNIT_BYTE (0x7F).
                //
                assert(unit[0] == FREE_POOLUNIT_BYTE);
                continue;

            // v-- Everything below here has NODE_FLAG_NODE set (0x8)

              case 8:
                // 0x8: unmanaged and unmarked, e.g. a series that was made
                // with Make_Series() and hasn't been managed.  It doesn't
                // participate in the GC.  Leave it as is.
                //
                // (Alloc_Value() and rebMalloc() produce these by default)
                //
                break;

              case 9:
                // 0x8 + 0x1: marked but not managed, this can't happen,
                // because the marking itself asserts nodes are managed.
                //
                panic (unit);

              case 10:
                // 0x8 + 0x2: managed but didn't get marked, should be GC'd
                //
                // (It would be nice if we could have NODE_FLAG_CELL here
                // as part of the switch, but see its definition for why it
                // is at position 8 from left and not an earlier bit.)
                //
                if (unit[0] & NODE_BYTEMASK_0x01_CELL) {
                    assert(not (unit[0] & NODE_BYTEMASK_0x02_ROOT));
                    Free_Pooled(STUB_POOL, unit);  // manuals use Free_Pairing
                }
                else {
                    Decay_Series(u_cast(Series*, unit));
                    GC_Kill_Stub(u_cast(Stub*, unit));
                }
                ++sweep_count;
                break;

              case 11:
                // 0x8 + 0x2 + 0x1: managed and marked, so it's still live.
                // Don't GC it, just clear the mark.
                //
                Remove_GC_Mark(cast(Node*, unit));
                break;

            // v-- Everything below this line has the two leftmost bits set
            // in the header.  In the *general* case this could be a valid
            // first byte of a multi-byte sequence in UTF-8.  But since we
            // are looking at a stub pool, these are free (inaccessible) stubs

              case 12:
                // 0x8 + 0x4: unmanaged free node, these should not exist
                // Should have just been killed vs. marked free and wait!
                //
                panic (unit);

              case 13:
                // 0x8 + 0x4 + 0x1: unmanaged but marked free node (?)
                //
                panic (unit);

              case 14:
                // 0x8 + 0x4 + 0x2: free stub, managed but not marked
                // GC pass canonized refs to these to PG_Inaccessible_Stub
                // With no more refs outstanding, time to clean them up.
                //
                assert(not (unit[0] & NODE_BYTEMASK_0x01_CELL));
                GC_Kill_Stub(u_cast(Stub*, unit));
                break;

              case 15:
                // 0x8 + 0x4 + 0x2 + 0x1: managed, marked, free stub
                // Should have canonized to PG_Inaccessible_Stub vs. marked!
                //
                panic (unit);  // 0x8 + 0x4 + ... reserved for UTF-8
            }
        }
    }
  }

  #if UNUSUAL_CELL_SIZE  // pairing pool is separate in this case [2]
  blockscope {
    Segment* seg = g_mem.pools[PAIR_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Length n = g_mem.pools[PAIR_POOL].num_units_per_segment;

        Byte *unit = cast(Byte*, seg + 1);
        for (; n > 0; --n, unit += 2 * sizeof(Cell)) {
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;

            assert(unit[0] & NODE_BYTEMASK_0x01_CELL);

            Value* v = cast(Value*, unit);
            if (v->header.bits & NODE_FLAG_MANAGED) {
                assert(not (v->header.bits & NODE_FLAG_ROOT));
                if (Is_Node_Marked(v)) {
                    Remove_GC_Mark(v);
                }
                else {
                    Free_Pooled(PAIR_POOL, unit);  // manuals use Free_Pairing
                    ++sweep_count;
                }
            }
        }
    }
  }
  #endif

    return sweep_count;
}


#if !defined(NDEBUG)

//
//  Fill_Sweeplist: C
//
REBLEN Fill_Sweeplist(Series* sweeplist)
{
    assert(Series_Wide(sweeplist) == sizeof(Node*));
    assert(Series_Used(sweeplist) == 0);

    Count sweep_count = 0;

    Segment* seg = g_mem.pools[STUB_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Count n = g_mem.pools[STUB_POOL].num_units_per_segment;
        Byte* stub = cast(Byte*, seg + 1);

        for (; n > 0; --n, stub += sizeof(Stub)) {
            switch (*stub >> 4) {
              case 9: {  // 0x8 + 0x1
                Series* s = x_cast(Series*, stub);
                Assert_Series_Managed(s);
                if (Is_Node_Marked(s)) {
                    Remove_GC_Mark(s);
                }
                else {
                    Expand_Series_Tail(sweeplist, 1);
                    *Series_At(Node*, sweeplist, sweep_count) = s;
                    ++sweep_count;
                }
                break; }

              case 11: {  // 0x8 + 0x2 + 0x1
                //
                // It's a cell which is managed where the value is not an END.
                // This is a managed pairing, so mark bit should be heeded.
                //
                // !!! It is a Stub Node, but *not* a "series".
                //
                Value* pairing = x_cast(Value*, cast(void*, stub));
                assert(pairing->header.bits & NODE_FLAG_MANAGED);
                if (Is_Node_Marked(pairing)) {
                    Remove_GC_Mark(pairing);
                }
                else {
                    Expand_Series_Tail(sweeplist, 1);
                    *Series_At(Node*, sweeplist, sweep_count) = pairing;
                    ++sweep_count;
                }
                break; }
            }
        }
    }

    return sweep_count;
}

#endif


//
//  Recycle_Core: C
//
// Recycle memory no longer needed.  If sweeplist is not NULL, then it needs
// to be a series whose width is sizeof(Stub), and it will be filled with
// the list of series that *would* be recycled.
//
REBLEN Recycle_Core(Series* sweeplist)
{
    // Ordinarily, it should not be possible to spawn a recycle during a
    // recycle.  But when debug code is added into the recycling code, it
    // could cause a recursion.  Be tolerant of such recursions to make that
    // debugging easier...but make a note that it's not ordinarily legal.
    //
  #if !defined(NDEBUG)
    if (g_gc.recycling) {
        printf("Recycle re-entry; should only happen in debug scenarios.\n");
        SET_SIGNAL(SIG_RECYCLE);
        return 0;
    }
  #endif

    // If disabled by RECYCLE/OFF, exit now but set the pending flag.  (If
    // shutdown, ignore so recycling runs and can be checked for balance.)
    //
    if (g_gc.disabled) {
        SET_SIGNAL(SIG_RECYCLE);
        return 0;
    }

  #if !defined(NDEBUG)
    g_gc.recycling = true;
  #endif

    Assert_No_GC_Marks_Pending();

  #if DEBUG_COLLECT_STATS
    g_gc.recycle_counter++;
    g_gc.recycle_series = g_mem.pools[STUB_POOL].free;
  #endif

    // Builtin patches for Lib contain variables that can be read by Lib(XXX)
    // in the C code.  Since effectively any of them could become referred
    // to in code, we need to keep the cells alive.
    //
    // We don't technically need to mark the patches themselves to protect
    // them from GC--because they're not in the STUB_POOL so Sweep_Series()
    // wouldn't free them.  But we mark them anyway--for clarity, and it
    // speeds up references that would mark them to see they're spoken for
    // (so they don't have to detect it's an array, queue the cell...)
    //
    assert(Is_Node_Free(&PG_Lib_Patches[0]));  // skip SYM_0
    for (REBLEN i = 1; i < LIB_SYMS_MAX; ++i) {
        Array* patch = &PG_Lib_Patches[i];
        if (Not_Node_Marked(patch)) {  // the prior loop iterations can mark
            Add_GC_Mark(patch);
            Queue_Mark_Maybe_Fresh_Cell_Deep(Stub_Cell(patch));
        }
    }
    Propagate_All_GC_Marks();

    // It was previously assumed no recycle would happen while the evaluator
    // was in a thrown state.  There's no particular reason to enforce that
    // in stackless, so it has been relaxed.
    //
    Queue_Mark_Maybe_Fresh_Cell_Deep(&g_ts.thrown_arg);
    Queue_Mark_Maybe_Fresh_Cell_Deep(&g_ts.thrown_label);
    Propagate_All_GC_Marks();

    // MARKING PHASE: the "root set" from which we determine the liveness
    // (or deadness) of a series.  If we are shutting down, we do not mark
    // several categories of series...but we do need to run the root marking.
    // (In particular because that is when API series whose lifetimes
    // are bound to levels will be freed, if the level is expired.)
    //
    Mark_Root_Series();
    Propagate_All_GC_Marks();

    Mark_Data_Stack();
    Mark_Guarded_Nodes();
    Mark_Level_Stack_Deep();
    Propagate_All_GC_Marks();

    // The last thing we do is go through all the "sea contexts" and make sure
    // that if anyone referenced the context, then their variables remain live.
    //
    // This must be done *iteratively* so long as the process transitions any
    // more modules into the live set.  Our weak method at the moment is just
    // to check if any more markings occur.
    //
    while (true) {
        bool added_marks = false;

        Symbol** psym = Series_Head(Symbol*, g_symbols.by_hash);
        Symbol** psym_tail = Series_Tail(Symbol*, g_symbols.by_hash);
        for (; psym != psym_tail; ++psym) {
            if (*psym == nullptr or *psym == &g_symbols.deleted_symbol)
                continue;
            Stub* patch = MISC(Hitch, *psym);
            for (
                ;
                patch != *psym;
                patch = cast(Stub*, node_MISC(Hitch, patch))
            ){
                Context* context = INODE(PatchContext, patch);
                if (Is_Node_Marked(patch)) {
                    assert(Is_Node_Marked(CTX_VARLIST(context)));
                    continue;
                }
                if (Is_Node_Marked(CTX_VARLIST(context))) {
                    Add_GC_Mark(patch);
                    added_marks = true;

                    Queue_Mark_Cell_Deep(Stub_Cell(patch));

                    // We also have to keep the word alive, but not necessarily
                    // keep all the other declarations in other modules alive.
                    //
                    Add_GC_Mark_If_Not_Already_Marked(*psym);
                }
            }
            Propagate_All_GC_Marks();
        }

        if (not added_marks)
            break;
    }

    // SWEEPING PHASE

    Assert_No_GC_Marks_Pending();

    // Note: We do not need to mark the PG_Inaccessible_Stub, because it is
    // not subject to GC and no one should mark it.  Make sure that's true.
    //
    assert(not Is_Node_Marked(&PG_Inaccessible_Stub));

    REBLEN sweep_count;

    if (sweeplist != NULL) {
    #if defined(NDEBUG)
        panic (sweeplist);
    #else
        sweep_count = Fill_Sweeplist(sweeplist);
    #endif
    }
    else
        sweep_count = Sweep_Series();

    // Unmark the Lib() fixed patches (not in stub pool, never get swept)
    //
    assert(Is_Node_Free(&PG_Lib_Patches[0]));  // skip SYM_0
    for (REBLEN i = 1; i < LIB_SYMS_MAX; ++i) {
        Array* patch = &PG_Lib_Patches[i];
        Remove_GC_Mark(patch);
    }

    // Unmark the Canon() fixed symbols (not in stub pool, never get swept)
    //
    assert(Is_Node_Free(&g_symbols.builtin_canons[0]));  // skip SYM_0
    for (REBLEN i = 1; i < ALL_SYMS_MAX; ++i) {
        Symbol* canon = &g_symbols.builtin_canons[i];
        Remove_GC_Mark_If_Marked(canon);
    }

  #if !defined(NDEBUG)
    assert(g_gc.mark_count == 0);  // should have balanced out
  #endif

  #if DEBUG_COLLECT_STATS
    // Compute new stats:
    g_gc.recycle_series
        = g_mem.pools[STUB_POOL].free - g_gc.recycle_series;
    g_gc.recycle_series_total += g_gc.recycle_series;
  #endif

    // !!! This reset of the "ballast" is the original code from R3-Alpha:
    //
    // https://github.com/rebol/rebol/blob/25033f897b2bd466068d7663563cd3ff64740b94/src/core/m-gc.c#L599
    //
    // Atronix R3 modified it, but that modification created problems:
    //
    // https://github.com/zsx/r3/issues/32
    //
    // Reverted to the R3-Alpha state, accommodating a comment "do not adjust
    // task variables or boot strings in shutdown when they are being freed."
    //
    g_gc.depletion = g_gc.ballast;

    Assert_No_GC_Marks_Pending();

  #if !defined(NDEBUG)
    g_gc.recycling = false;
  #endif

  #if !defined(NDEBUG)
    //
    // This might be an interesting feature for release builds, but using
    // normal I/O here that runs evaluations could be problematic.  Even
    // though we've finished the recycle, we're still in the signal handling
    // stack, so calling into the evaluator e.g. for rebPrint() may be bad.
    //
    if (g_gc.watch_recycle) {
        printf("RECYCLE: %u nodes\n", cast(unsigned int, sweep_count));
        fflush(stdout);
    }
  #endif

    return sweep_count;
}


//
//  Recycle: C
//
// Recycle memory no longer needed.
//
REBLEN Recycle(void)
{
    REBLEN n = Recycle_Core(nullptr);

  #ifdef DOUBLE_RECYCLE_TEST
    //
    // If there are two recycles in a row, then the second should not free
    // any additional series that were not freed by the first.  (It also
    // shouldn't crash.)  This is an expensive check, but helpful to try if
    // it seems a GC left things in a bad state that crashed a later GC.
    //
    REBLEN n2 = Recycle_Core(nullptr);
    assert(n2 == 0);
  #endif

    return n;
}


//
//  Push_Guard_Node: C
//
void Push_Guard_Node(const Node* node)
{
    assert(Is_Node(node));

  #if !defined(NDEBUG)
    if (Is_Node_A_Cell(node)) {

      #ifdef STRESS_CHECK_GUARD_VALUE_POINTER
        //
        // Technically we should never call this routine to guard a value
        // that lives in some array.  Not only would we have to guard the
        // containing array, we would also have to lock the array from
        // being able to resize and reallocate the data pointer.  But this is
        // a somewhat expensive check, so only feasible to run occasionally.
        //
        Node* containing = Try_Find_Containing_Node_Debug(v);
        if (containing)
            panic (containing);
      #endif
    }
    else {  // It's a series
        //
        // !!! At one time this didn't ensure the series being guarded was
        // managed, based on the idea of guarding the contents of an unmanaged
        // array.  That idea didn't get any usage, and allowing unmanaged
        // guards here just obfuscated errors when they occurred.  So the
        // assert has been put back.  Review.

        assert(Is_Node_Managed(node));
    }
  #endif

    if (Is_Series_Full(g_gc.guarded))
        Extend_Series_If_Necessary(g_gc.guarded, 8);

    *Series_At(const Node*, g_gc.guarded, Series_Used(g_gc.guarded)) = node;

    Set_Series_Used(g_gc.guarded, Series_Used(g_gc.guarded) + 1);
}


//
//  Startup_GC: C
//
// Initialize garbage collector.
//
void Startup_GC(void)
{
    assert(not g_gc.disabled);
    assert(not g_gc.recycling);

  #if DEBUG_COLLECT_STATS
    assert(g_gc.recycle_counter == 0);
    assert(g_gc.recycle_series_total == 0);
    assert(g_gc.recycle_series == 0);
  #endif

    // Manually allocated series that GC is not responsible for (unless a
    // fail() occurs). Holds series pointers.  Must happen before any unmanaged
    // allocations!
    //
    // As a trick to keep this series from trying to track itself, say it's
    // managed, then sneak the flag off.
    //
    ensure(nullptr, g_gc.manuals) = Make_Series_Core(
        15,
        FLAG_FLAVOR(SERIESLIST) | NODE_FLAG_MANAGED  // lie!
    );
    Clear_Node_Managed_Bit(g_gc.manuals);  // untracked and indefinite lifetime

    // Temporary series and values protected from GC. Holds node pointers.
    //
    ensure(nullptr, g_gc.guarded) = Make_Series_Core(15, FLAG_FLAVOR(NODELIST));

    // The marking queue used in lieu of recursion to ensure that deeply
    // nested structures don't cause the C stack to overflow.
    //
    ensure(nullptr, g_gc.mark_stack) = Make_Series_Core(
        100,
        FLAG_FLAVOR(NODELIST)
    );

    g_gc.ballast = MEM_BALLAST; // or overwritten by debug build below...

  #ifndef NDEBUG
    const char *env_recycle_torture = getenv("R3_RECYCLE_TORTURE");
    if (env_recycle_torture and atoi(env_recycle_torture) != 0)
        g_gc.ballast = 0;

    if (g_gc.ballast == 0) {
        printf(
            "**\n" \
            "** R3_RECYCLE_TORTURE is nonzero in environment variable!\n" \
            "** (or g_gc.ballast is set to 0 manually in the init code)\n" \
            "** Recycling on EVERY evaluator step, *EXTREMELY* SLOW!...\n" \
            "** Useful in finding bugs before you can run RECYCLE/TORTURE\n" \
            "** But you might only want to do this with -O2 debug builds.\n"
            "**\n"
        );
        fflush(stdout);
     }
  #endif

    g_gc.depletion = g_gc.ballast;

    // When a series needs to expire its content for some reason (including
    // the user explicitly saying FREE), then there can still be references to
    // that series around.  Since we don't want to trigger a GC synchronously
    // each time this happens, the NODE_FLAG_FREE flag is added to series...and
    // it is checked for by value extractors (like VAL_CONTEXT()).  But once
    // the GC gets a chance to run, those stubs can be swept with all the
    // inaccessible references canonized to this one global node.
    //
    Make_Series_Into(
        &PG_Inaccessible_Stub,
        1,
        FLAG_FLAVOR(THE_GLOBAL_INACCESSIBLE)
            | NODE_FLAG_MANAGED
            // Don't confuse the series creation machinery by trying to pass
            // in NODE_FLAG_FREE to the creation.  Do it after.
    );
    Set_Series_Inaccessible(&PG_Inaccessible_Stub);
    assert(Not_Node_Accessible(&PG_Inaccessible_Stub));
}


//
//  Shutdown_GC: C
//
void Shutdown_GC(void)
{
    GC_Kill_Stub(&PG_Inaccessible_Stub);

    assert(Series_Used(g_gc.guarded) == 0);
    Free_Unmanaged_Series(g_gc.guarded);
    g_gc.guarded = nullptr;

    assert(Series_Used(g_gc.mark_stack) == 0);
    Free_Unmanaged_Series(g_gc.mark_stack);
    g_gc.mark_stack = nullptr;

    // Can't use Free_Unmanaged_Series() because g_gc.manuals couldn't be put
    // in the manuals list...Catch-22!  This free must happen after all
    // unmanaged series have been freed.
    //
    assert(Series_Used(g_gc.manuals) == 0);
    GC_Kill_Series(g_gc.manuals);
    g_gc.manuals = nullptr;

  #if DEBUG_COLLECT_STATS
    g_gc.recycle_counter = 0;
    g_gc.recycle_series_total = 0;
    g_gc.recycle_series = 0;
  #endif
}
