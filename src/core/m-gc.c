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
// Copyright 2012-2019 Ren-C Open Source Contributors
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
// fit inside their cell.  They do this in order to create a level of
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

static REBI64 mark_count = 0;

#define ASSERT_NO_GC_MARKS_PENDING() \
    assert(Series_Used(g_gc.mark_stack) == 0)


static void Queue_Mark_Cell_Deep(Cell(const*) v);

inline static void Queue_Mark_Maybe_Fresh_Cell_Deep(Cell(*) v) {
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
static void Queue_Mark_Pairing_Deep(REBVAL *paired)
{
    assert(not (paired->header.bits & NODE_FLAG_MARKED));

    // !!! Hack doesn't work generically, review

  #if !defined(NDEBUG)
    bool was_in_mark = in_mark;
    in_mark = false;  // would assert about the recursion otherwise
  #endif

    Queue_Mark_Cell_Deep(paired);
    Queue_Mark_Cell_Deep(PAIRING_KEY(paired));  // QUOTED! uses void

    paired->header.bits |= NODE_FLAG_MARKED;
    ++mark_count;

  #if !defined(NDEBUG)
    in_mark = was_in_mark;
  #endif
}

static void Queue_Unmarked_Accessible_Series_Deep(Series(*) s);


// This routine is given the *address* of the node to mark, so that if the
// node has become inaccessible it can be canonized into the global node for
// series that are no longer available.  This allows nodes that had been
// turned into a decayed form and only kept alive to prevent referencing
// pointers to be swept.  See Decay_Series()
//
static void Queue_Mark_Node_Deep(const Node** pp) {
    Byte first = *cast(const Byte*, *pp);
    if (first & NODE_BYTEMASK_0x10_MARKED)
        return;  // may not be finished marking yet, but has been queued

    if (first & NODE_BYTEMASK_0x01_CELL) {  // e.g. a pairing
        REBVAL *v = VAL(m_cast(Node*, *pp));
        if (Get_Cell_Flag(v, MANAGED))
            Queue_Mark_Pairing_Deep(v);
        else {
            // !!! It's a frame?  API handle?  Skip frame case (keysource)
            // for now, but revisit as technique matures.
        }
        return;  // it's 2 cells, sizeof(Stub), but no room for a Stub's data
    }

    Series(*) s = SER(m_cast(Node*, *pp));
    if (Get_Series_Flag(s, INACCESSIBLE)) {
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
        *pp = &PG_Inaccessible_Series;
        return;
    }

 #if !defined(NDEBUG)
    if (Is_Free_Node(s))
        panic (s);

    if (Not_Series_Flag(s, MANAGED)) {
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
static void Queue_Unmarked_Accessible_Series_Deep(Series(*) s)
{
    s->leader.bits |= NODE_FLAG_MARKED;
    ++mark_count;

  //=//// MARK LINK AND MISC IF DESIRED ////////////////////////////////////=//

    // All nodes have both link and misc fields available, but they don't
    // necessarily hold node pointers (and even if they do, they may not be
    // references that are intended to keep them live).  So the series header
    // flags control whether the marking is done or not.

    if (Get_Series_Flag(s, LINK_NODE_NEEDS_MARK) and s->link.any.node)
        Queue_Mark_Node_Deep(&s->link.any.node);

    if (Get_Series_Flag(s, MISC_NODE_NEEDS_MARK) and s->misc.any.node)
        Queue_Mark_Node_Deep(&s->misc.any.node);

  //=//// MARK INODE IF NOT USED FOR INFO //////////////////////////////////=//

    // In the case of the INFO/INODE slot, the setting of the needing mark
    // flag is what determines whether the slot is used for info or not.  So
    // if it's available for non-info uses, it is always a live marked node.

    if (Get_Series_Flag(s, INFO_NODE_NEEDS_MARK) and node_INODE(Node, s))
        Queue_Mark_Node_Deep(&s->info.node);

    if (IS_KEYLIST(s)) {
        //
        // !!! KeyLists may not be the only category that are just a straight
        // list of node pointers.
        //
        REBKEY *tail = Series_Tail(REBKEY, s);
        REBKEY *key = Series_Head(REBKEY, s);
        for (; key != tail; ++key) {
            //
            // Symbol(*) are not available to the user to free out from under
            // a keylist (can't use FREE on them) and shouldn't vanish.
            //
            assert(Not_Series_Flag(*key, INACCESSIBLE));
            if (Get_Series_Flag(*key, MARKED))
                continue;
            Queue_Unmarked_Accessible_Series_Deep(m_cast(SymbolT*, *key));
        }
    }
    else if (Is_Series_Array(s)) {
        Array(*) a = ARR(s);

    //=//// MARK BONUS (if not using slot for `bias`) /////////////////////=//

        // Whether the bonus slot needs to be marked is dictated by internal
        // series type, not an extension-usable flag (due to flag scarcity).
        //
        if (IS_VARLIST(a) and node_BONUS(Node, s)) {
            //
            // !!! The keysource for varlists can be set to a Level(*), which
            // at the moment pretends to be a cell to distinguish itself.
            // This makes less sense than pretending to be a series that is
            // already marked, and has a detectable FLAVOR_XXX.  Review.
            //
            if (Is_Node_A_Cell(node_BONUS(Node, s)))
                goto skip_mark_frame_bonus;

            assert(IS_KEYLIST(SER(node_BONUS(Node, a))));

            Queue_Mark_Node_Deep(&s->content.dynamic.bonus.node);
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
        *Series_At(Array(*), g_gc.mark_stack, Series_Used(g_gc.mark_stack)) = a;
        Set_Series_Used(  // doesn't add a terminator
            g_gc.mark_stack,
            Series_Used(g_gc.mark_stack) + 1
        );
    }
}


//
//  Queue_Mark_Cell_Deep: C
//
static void Queue_Mark_Cell_Deep(Cell(const*) cv)
{
    Cell(*) v = m_cast(Cell(*), cv);  // we're the system, we can do this

  #if DEBUG_UNREADABLE_TRASH
    if (IS_TRASH(cv))  // tolerate unreadable "trash" in debug builds
        return;
  #endif

    // We mark based on the type of payload in the cell, e.g. its "unescaped"
    // form.  So if '''a fits in a WORD! (despite being a QUOTED!), we want
    // to mark the cell as if it were a plain word.  Use the CELL_HEART().
    //
    enum Reb_Kind heart = CELL_HEART(v);

  #if !defined(NDEBUG)  // see Queue_Mark_Node_Deep() for notes on recursion
    assert(not in_mark);
    in_mark = true;
  #endif

    if (IS_BINDABLE_KIND(heart)) {
        Series(*) binding = BINDING(v);
        if (binding != UNBOUND)
            if (NODE_BYTE(binding) & NODE_BYTEMASK_0x20_MANAGED)
                Queue_Mark_Node_Deep(&v->extra.Binding);
    }

    if (Get_Cell_Flag_Unchecked(v, FIRST_IS_NODE) and VAL_NODE1(v))
        Queue_Mark_Node_Deep(&PAYLOAD(Any, v).first.node);

    if (Get_Cell_Flag_Unchecked(v, SECOND_IS_NODE) and VAL_NODE2(v))
        Queue_Mark_Node_Deep(&PAYLOAD(Any, v).second.node);

  #if !defined(NDEBUG)
    in_mark = false;
    Assert_Cell_Marked_Correctly(v);
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
        Array(*) a = *Series_At(
            ArrayT*,
            g_gc.mark_stack,
            Series_Used(g_gc.mark_stack)
        );

        // Termination is not required in the release build (the length is
        // enough to know where it ends).  But overwrite with trash in debug.
        //
        Trash_Pointer_If_Debug(
            *Series_At(
                ArrayT*,
                g_gc.mark_stack,
                Series_Used(g_gc.mark_stack)
            )
        );

        // We should have marked this series at queueing time to keep it from
        // being doubly added before the queue had a chance to be processed
        //
        assert(a->leader.bits & NODE_FLAG_MARKED);

        Cell(*) v = Array_Head(a);
        Cell(const*) tail = Array_Tail(a);
        for (; v != tail; ++v) {
          #if DEBUG
            Flavor flavor = Series_Flavor(a);
            assert(flavor <= FLAVOR_MAX_ARRAY);

            switch (QUOTE_BYTE(v)) {
              case ISOTOPE_0:
                if (flavor < FLAVOR_MIN_ISOTOPES_OK)
                    panic (v);  // isotopes not legal in some array types

                if (Is_Isotope_Unstable(cast(Atom(*), v)))  // illegal in array
                    panic (v);
                break;

              case UNQUOTED_1:
                if (HEART_BYTE(v) == REB_VOID) {
                    if (flavor < FLAVOR_MIN_VOIDS_OK)
                        panic (v);  // voids not legal in some array types
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
// wants to invoke the evaluator with just a comma-delimited list of REBVAL*
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
    Feed(*) feed,
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
            assert(not Is_Isotope(TOP));
            Fetch_Next_In_Feed(feed);
        } while (Not_Feed_At_End(feed));

        assert(TOP_INDEX != base);
        if (FEED_IS_VARIADIC(feed))  // UTF-8 scan may have finalized it
            Finalize_Variadic_Feed(feed);

        Index index = truncated ? 2 : 1;  // skip --optimized-out--

        Array(*) a = Pop_Stack_Values_Core(base, SERIES_FLAG_MANAGED);
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
        Set_Series_Info(m_cast(Array(*), FEED_ARRAY(feed)), HOLD);
        Set_Feed_Flag(feed, TOOK_HOLD);
    }
    else {
        Finalize_Variadic_Feed(feed);

        if (truncated) {
            Init_Quasi_Word(PUSH(), Canon(OPTIMIZED_OUT));

            Array(*) a = Pop_Stack_Values_Core(base, SERIES_FLAG_MANAGED);
            Init_Array_Cell_At(FEED_SINGLE(feed), REB_BLOCK, a, 1);
        }
        else
            Init_Array_Cell_At(FEED_SINGLE(feed), REB_BLOCK, EMPTY_ARRAY, 0);

        feed->p = &PG_Feed_At_End;
    }

    assert(FEED_INDEX(feed) <= cast(Index, Array_Len(FEED_ARRAY(feed))));
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
// !!! This implementation walks over *all* the nodes.  It wouldn't have to
// if API nodes were in their own pool, or if the outstanding manuals list
// were maintained even in non-debug builds--it could just walk those.  This
// should be weighed against background GC and other more sophisticated
// methods which might come down the road for the GC than this simple one.
//
static void Mark_Root_Series(void)
{
    Segment* seg = g_mem.pools[STUB_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Byte* stub = cast(Byte*, seg + 1);
        Length n = g_mem.pools[STUB_POOL].num_units_per_segment;
        for (; n > 0; --n, stub += sizeof(Stub)) {
            //
            // !!! A smarter switch statement here could do this more
            // optimally...see the sweep code for an example.
            //
            Byte nodebyte = *stub;
            if (nodebyte & NODE_BYTEMASK_0x40_STALE)
                continue;

            assert(nodebyte & NODE_BYTEMASK_0x80_NODE);

            if (nodebyte & NODE_BYTEMASK_0x02_ROOT) {
                //
                // This came from Alloc_Value(); all references should be
                // from the C stack, only this visit should be marking it.
                //
                Array(*) a = ARR(cast(void*, stub));

                assert(not (a->leader.bits & NODE_FLAG_MARKED));

                // Note: Eval_Core() might target API cells, uses END
                //
                if (not (a->leader.bits & NODE_FLAG_MANAGED)) {
                    // if it's not managed, don't mark it (don't have to?)
                    Queue_Mark_Cell_Deep(Array_Single(a));
                }
                else {  // Note that Mark_Level_Stack_Deep() marks the owner
                    if (not (a->leader.bits & NODE_FLAG_MARKED)) {
                        a->leader.bits |= NODE_FLAG_MARKED;
                        ++mark_count;

                        // Like frame cells or locals, API cells can be
                        // evaluation targets.  They should only be fresh if
                        // they are targeted by some frame's L->out.
                        //
                        // !!! Should we verify this?
                        //
                        Queue_Mark_Maybe_Fresh_Cell_Deep(Array_Single(a));
                    }
                }

                continue;
            }

            if (nodebyte & NODE_BYTEMASK_0x01_CELL) {  // a pairing
                REBVAL *paired = VAL(cast(void*, stub));
                if (paired->header.bits & NODE_FLAG_MANAGED)
                    continue; // PAIR! or other value will mark it

                assert(!"unmanaged pairings not believed to exist yet");
                Queue_Mark_Cell_Deep(paired);
                Queue_Mark_Cell_Deep(PAIRING_KEY(paired));
            }

            // !!! The g_ds.array does not currently keep its `used` field up
            // to date, because this is one more than the g_ds.index and would
            // require math to be useful.  (While the array tends to want to
            // know the length and the tail pointer to stop at, clients of
            // the stack want to know the "last".)  Hence it is exempt from
            // this marking rather than keeping the length up to date.  Review.
            //
            Series(*) s = SER(cast(void*, stub));
            if (
                Is_Series_Array(s)
                and s != g_ds.array  // !!! Review g_ds.array exemption!
            ){
                if (s->leader.bits & NODE_FLAG_MANAGED)
                    continue; // BLOCK! should mark it

                Array(*) a = ARR(s);

                if (IS_VARLIST(a))
                    if (CTX_TYPE(CTX(a)) == REB_FRAME)
                        continue;  // Mark_Level_Stack_Deep() etc. mark it

                // This means someone did something like Make_Array() and then
                // ran an evaluation before referencing it somewhere from the
                // root set.

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

                Cell(const*) item_tail = Array_Tail(a);
                Cell(*) item = Array_Head(a);
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
    Cell(const*) head = Array_Head(g_ds.array);
    assert(Is_Cell_Poisoned(head));  // Data_Stack_At(0) is deliberately invalid

    REBVAL *stackval = g_ds.movable_top;
    for (; stackval != head; --stackval)  // stop before Data_Stack_At(0)
        Queue_Mark_Cell_Deep(stackval);

  #if DEBUG_POISON_DROPPED_STACK_CELLS
    stackval = g_ds.movable_top + 1;
    for (; stackval != Array_Tail(g_ds.array); ++stackval)
        assert(Is_Cell_Poisoned(stackval));
  #endif

    Propagate_All_GC_Marks();
}


//
//  Mark_Symbol_Series: C
//
// Mark symbol series.  These canon words for SYM_XXX are the only ones that
// are never candidates for GC (until shutdown).  All other symbol series may
// go away if no words, parameters, object keys, etc. refer to them.
//
static void Mark_Symbol_Series(void)
{
    SymbolT* canon = &g_symbols.builtin_canons[0];
    SymbolT* tail = &g_symbols.builtin_canons[0] + ALL_SYMS_MAX;

    assert(canon->leader.bits & SERIES_FLAG_FREE);  // SYM_0, we corrupt it
    ++canon;

    for (; canon != tail; ++canon) {
        assert(not (canon->leader.bits & NODE_FLAG_MARKED));
        canon->leader.bits |= NODE_FLAG_MARKED;
        ++mark_count;
    }

    ASSERT_NO_GC_MARKS_PENDING(); // doesn't ues any queueing
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
        if (*cast(const Byte*, *np) == PREP_SIGNAL_BYTE) {
            //
            // We allow you to protect a "fresh" cell, e.g. one that's 0 bytes
            // in its header.
        }
        else if (Is_Node_A_Cell(*np)) {
            //
            // !!! What if someone tried to GC_GUARD a managed paired Stub?
            //
            Queue_Mark_Cell_Deep(cast(const REBVAL*, *np));
        }
        else  // a series
            Queue_Mark_Node_Deep(np);

        Propagate_All_GC_Marks();
    }
}


//
//  Mark_Level_Stack_Deep: C
//
// Mark values being kept live by all stack levels.  If a function is running,
// then this will keep the function itself live, as well as the arguments.
// There is also an "out" slot--which may point to an arbitrary REBVAL cell
// on the C stack (and must contain valid GC-readable bits at all times).
//
// Since function argument slots are not pre-initialized, how far the function
// has gotten in its fulfillment must be taken into account.  Only those
// argument slots through points of fulfillment may be GC protected.
//
// This should be called at the top level, and not from inside a
// Propagate_All_GC_Marks().  All marks will be propagated.
//
static void Mark_Level_Stack_Deep(void)
{
    Level(*) L = TOP_LEVEL;

    while (true) {  // mark all levels (even BOTTOM_LEVEL)
        //
        // Note: MISC_PENDING() should either live in FEED_ARRAY(), or
        // it may be trash (e.g. if it's an apply).  GC can ignore it.
        //
        Array(*) singular = FEED_SINGULAR(L->feed);
        do {
            Queue_Mark_Cell_Deep(Array_Single(singular));
            singular = LINK(Splice, singular);
        } while (singular);

        // !!! This used to mark L->feed->p; but we probably do not need to.
        // All variadics are reified as arrays in the GC (we could avoid this
        // using va_copy, but probably not worth it).  All values in feed
        // should be covered in terms of GC protection.

        REBSPC* L_specifier = Level_Specifier(L);

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
            // !!! Should this instead check that it isn't INACCESSIBLE?
            //
            Queue_Mark_Node_Deep(&FEED_SINGLE(L->feed)->extra.Binding);
        }

        // L->out can be nullptr at the moment, when a level is created that
        // can ask for a different output each evaluation.
        //
        if (L->out)  // output is allowed to be FRESHEN()
            Queue_Mark_Maybe_Fresh_Cell_Deep(L->out);

        // Level temporary cell should always contain initialized bits, as
        // Make_Level() sets it up and no one is supposed to trash it.
        //
        Queue_Mark_Maybe_Fresh_Cell_Deep(&L->feed->fetched);
        Queue_Mark_Maybe_Fresh_Cell_Deep(&L->feed->lookback);
        Queue_Mark_Maybe_Fresh_Cell_Deep(&L->spare);

        if (L->executor == &Evaluator_Executor) {
            if (not Is_Cell_Erased(&L->u.eval.scratch))  // extra GC-safe cell
                Queue_Mark_Cell_Deep(cast(Cell(*), &L->u.eval.scratch));
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
            cast(const Node**, m_cast(const Action(*)*, &L->u.action.original))
        );

        if (L->label) { // nullptr if anonymous
            Symbol(const*) sym = unwrap(L->label);
            if (Not_Series_Flag(sym, MARKED)) {
                assert(Not_Series_Flag(sym, INACCESSIBLE));  // can't happen
                Queue_Unmarked_Accessible_Series_Deep(m_cast(Symbol(*), sym));
            }
        }

        if (L->varlist and Get_Series_Flag(L->varlist, MANAGED)) {
            //
            // If the context is all set up with valid values and managed,
            // then it can just be marked normally...no need to do custom
            // partial parameter traversal.
            //
            assert(
                not Is_Level_Fulfilling(L)
                or Level_State_Byte(L) == ST_ACTION_TYPECHECKING  // filled/safe
            );

            // "may not pass CTX() test"
            //
            Queue_Mark_Node_Deep(
                cast(const Node**, m_cast(Array(const*)*, &L->varlist))
            );
            goto propagate_and_continue;
        }

        if (L->varlist and Get_Series_Flag(L->varlist, INACCESSIBLE)) {
            //
            // This happens in Encloser_Dispatcher(), where it can capture a
            // varlist that may not be managed (e.g. if there were no ADAPTs
            // or other phases running that triggered it).
            //
            goto propagate_and_continue;
        }

        // Mark arguments as used, but only as far as parameter filling has
        // gotten (may be garbage bits past that).  Could also be an END value
        // of an in-progress arg fulfillment, but in that case it is protected
        // by the *evaluating frame's L->out* (!)
        //
        // Refinements need special treatment, and also consideration of if
        // this is the "doing pickups" or not.  If doing pickups then skip the
        // cells for pending refinement arguments.
        //
        Phase(*) phase; // goto would cross initialization
        phase = Level_Phase(L);
        const REBKEY *key;
        const REBKEY *tail;
        key = ACT_KEYS(&tail, phase);

        REBVAL *arg;
        for (arg = Level_Args_Head(L); key != tail; ++key, ++arg) {
            //
            // We only tolerate unfulfilled cells during the fulfillment phase.
            // Once the frame is fulfilled, it may be exposed to usermode code
            // as a FRAME!...and there can be no END/prep cells.
            //
            // (Note that when key == L->u.action.key, that means that arg is
            // the output slot for some other level's L->out...which is a case
            // where transient FRESHEN() can also leave voids in slots.)
            //
            if (Is_Cell_Erased(arg))
                assert(L->u.action.key != tail);
            else {
                if (key == L->u.action.key)
                    Queue_Mark_Maybe_Fresh_Cell_Deep(arg);
                else
                    Queue_Mark_Cell_Deep(arg);
            }
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
//    strict aliases, as the pool contain pairs of cells from Alloc_Pairing(),
//    or a SeriesT from Prep_Stub().  The shared first byte node masks are
//    defined and explained in %sys-rebnod.h
//
// 2. For efficiency of memory use, Stub is nominally 2*sizeof(CellT), and
//    so pairs can use the same Stub nodes.  But features that might make the
//    two cells a size greater than Stub size require doing pairings in a
//    different pool.
//
static Count Sweep_Series(void)
{
    Count sweep_count = 0;

  blockscope {
    Segment* seg = g_mem.pools[STUB_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Count n = g_mem.pools[STUB_POOL].num_units_per_segment;
        Byte* stub = cast(Byte*, seg + 1);  // byte beats strict alias, see [1]

        for (; n > 0; --n, stub += sizeof(Stub)) {
            switch (*stub >> 4) {
              case 0:
              case 1:  // 0x1
              case 2:  // 0x2
              case 3:  // 0x2 + 0x1
              case 4:  // 0x4
              case 5:  // 0x4 + 0x1
              case 6:  // 0x4 + 0x2
              case 7:  // 0x4 + 0x2 + 0x1
                //
                // NODE_FLAG_NODE (0x8) is clear.  This signature is
                // reserved for UTF-8 strings (corresponding to valid ASCII
                // values in the first byte).
                //
                panic (stub);

            // v-- Everything below here has NODE_FLAG_NODE set (0x8)

              case 8:
                // 0x8: unmanaged and unmarked, e.g. a series that was made
                // with Make_Series() and hasn't been managed.  It doesn't
                // participate in the GC.  Leave it as is.
                //
                // !!! Are there actually legitimate reasons to do this with
                // arrays, where the creator knows the cells do not need
                // GC protection?  Should finding an array in this state be
                // considered a problem (e.g. the GC ran when you thought it
                // couldn't run yet, hence would be able to free the array?)
                //
                break;

              case 9:
                // 0x8 + 0x1: marked but not managed, this can't happen,
                // because the marking itself asserts nodes are managed.
                //
                panic (stub);

              case 10:
                // 0x8 + 0x2: managed but didn't get marked, should be GC'd
                //
                // !!! It would be nice if we could have NODE_FLAG_CELL here
                // as part of the switch, but see its definition for why it
                // is at position 8 from left and not an earlier bit.
                //
                if (*stub & NODE_BYTEMASK_0x01_CELL) {
                    assert(not (*stub & NODE_BYTEMASK_0x02_ROOT));
                    Free_Pooled(STUB_POOL, stub);  // Free_Pairing manual
                }
                else {
                    Series(*) s = cast(Series(*), stub);
                    GC_Kill_Series(s);
                }
                ++sweep_count;
                break;

              case 11:
                // 0x8 + 0x2 + 0x1: managed and marked, so it's still live.
                // Don't GC it, just clear the mark.
                //
                *stub &= ~NODE_BYTEMASK_0x10_MARKED;
              #if !defined(NDEBUG)
                --mark_count;
              #endif
                break;

            // v-- Everything below this line has the two leftmost bits set
            // in the header.  In the *general* case this could be a valid
            // first byte of a multi-byte sequence in UTF-8...so only the
            // special bit pattern of the free case uses this.

              case 12:
                // 0x8 + 0x4: free node, uses special illegal UTF-8 byte
                //
                assert(*stub == FREED_SERIES_BYTE);
                break;

              case 13:
              case 14:
              case 15:
                panic (stub);  // 0x8 + 0x4 + ... reserved for UTF-8
            }
        }
    }
  }

  #if UNUSUAL_CELL_SIZE  // pairing pool is separate in this case, see [2]
  blockscope {
    Segment* seg = g_mem.pools[PAIR_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Length n = g_mem.pools[PAIR_POOL].num_units_per_segment;

        REBVAL *v = cast(REBVAL*, seg + 1);
        for (; n > 0; --n, v += 2) {
            if (v->header.bits & SERIES_FLAG_FREE) {
                assert(FIRST_BYTE(v->header) == FREED_SERIES_BYTE);
                continue;
            }

            assert(v->header.bits & NODE_FLAG_CELL);

            if (v->header.bits & NODE_FLAG_MANAGED) {
                assert(not (v->header.bits & NODE_FLAG_ROOT));
                if (v->header.bits & NODE_FLAG_MARKED) {
                    v->header.bits &= ~NODE_FLAG_MARKED;
                  #if !defined(NDEBUG)
                    --mark_count;
                  #endif
                }
                else {
                    Free_Pooled(PAIR_POOL, v);  // Free_Pairing is for manuals
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
REBLEN Fill_Sweeplist(Series(*) sweeplist)
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
                Series(*) s = SER(cast(void*, stub));
                Assert_Series_Managed(s);
                if (s->leader.bits & NODE_FLAG_MARKED) {
                    s->leader.bits &= ~NODE_FLAG_MARKED;
                  #if !defined(NDEBUG)
                    --mark_count;
                  #endif
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
                REBVAL *pairing = VAL(cast(void*, stub));
                assert(pairing->header.bits & NODE_FLAG_MANAGED);
                if (pairing->header.bits & NODE_FLAG_MARKED) {
                    pairing->header.bits &= ~NODE_FLAG_MARKED;
                  #if !defined(NDEBUG)
                    --mark_count;
                  #endif
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
// to be a series whose width is sizeof(Series(*)), and it will be filled with
// the list of series that *would* be recycled.
//
REBLEN Recycle_Core(bool shutdown, Series(*) sweeplist)
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
    if (not shutdown and g_gc.disabled) {
        SET_SIGNAL(SIG_RECYCLE);
        return 0;
    }

  #if !defined(NDEBUG)
    g_gc.recycling = true;
  #endif

    ASSERT_NO_GC_MARKS_PENDING();

  #if DEBUG_COLLECT_STATS
    g_gc.recycle_counter++;
    g_gc.recycle_series = g_mem.pools[STUB_POOL].free;
  #endif

    if (not shutdown)
        Mark_Symbol_Series();

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

    if (not shutdown) {
        Propagate_All_GC_Marks();

        Mark_Data_Stack();

        Mark_Guarded_Nodes();

        Mark_Level_Stack_Deep();

        Propagate_All_GC_Marks();
    }

    // The last thing we do is go through all the "sea contexts" and make sure
    // that if anyone referenced the context, then their variables remain live.
    //
    // This must be done *iteratively* so long as the process transitions any
    // more modules into the live set.  Our weak method at the moment is just
    // to check if any more markings occur.
    //
    while (true) {
        REBI64 before_count = mark_count;

        SymbolT** psym = Series_Head(SymbolT*, g_symbols.by_hash);
        SymbolT** psym_tail = Series_Tail(SymbolT*, g_symbols.by_hash);
        for (; psym != psym_tail; ++psym) {
            if (*psym == nullptr or *psym == &g_symbols.deleted_symbol)
                continue;
            Series(*) patch = MISC(Hitch, *psym);
            for (; patch != *psym; patch = SER(node_MISC(Hitch, patch))) {
                Context(*) context = INODE(PatchContext, patch);
                if (Get_Series_Flag(patch, MARKED)) {
                    assert(Get_Series_Flag(CTX_VARLIST(context), MARKED));
                    continue;
                }
                if (Get_Series_Flag(CTX_VARLIST(context), MARKED)) {
                    Set_Series_Flag(patch, MARKED);
                    ++mark_count;

                    Queue_Mark_Cell_Deep(Array_Single(ARR(patch)));

                    // We also have to keep the word alive, but not necessarily
                    // keep all the other declarations in other modules alive.
                    //
                    if (Not_Series_Flag(*psym, MARKED)) {
                        Set_Series_Flag(*psym, MARKED);
                        ++mark_count;
                    }
                }
            }
            Propagate_All_GC_Marks();
        }

        if (before_count == mark_count)
            break;  // no more added
    }

    // SWEEPING PHASE

    ASSERT_NO_GC_MARKS_PENDING();

    // Note: We do not need to mark the PG_Inaccessible_Series, because it is
    // not subject to GC and no one should mark it.  Make sure that's true.
    //
    assert(Not_Series_Flag(&PG_Inaccessible_Series, MARKED));

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

    // Unmark the Lib() fixed patches
    //
    for (REBLEN i = 1; i < LIB_SYMS_MAX; ++i) {
        Array(*) patch = &PG_Lib_Patches[i];
        if (Get_Series_Flag(patch, MARKED)) {
            Clear_Series_Flag(patch, MARKED);
            --mark_count;
        }
    }

    // Unmark the Canon() fixed symbols
    //
    for (REBLEN i = 1; i < ALL_SYMS_MAX; ++i) {
        Symbol(*) canon = &g_symbols.builtin_canons[i];

        if (not shutdown)
           assert(Get_Series_Flag(canon, MARKED));

        if (Get_Series_Flag(canon, MARKED)) {
            Clear_Series_Flag(canon, MARKED);
            --mark_count;
        }
    }

   #if !defined(NDEBUG)
     assert(mark_count == 0);  // should balance out
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
    if (not shutdown)
        g_gc.depletion = g_gc.ballast;

    ASSERT_NO_GC_MARKS_PENDING();

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
    // Default to not passing the `shutdown` flag.
    //
    REBLEN n = Recycle_Core(false, NULL);

  #ifdef DOUBLE_RECYCLE_TEST
    //
    // If there are two recycles in a row, then the second should not free
    // any additional series that were not freed by the first.  (It also
    // shouldn't crash.)  This is an expensive check, but helpful to try if
    // it seems a GC left things in a bad state that crashed a later GC.
    //
    REBLEN n2 = Recycle_Core(false, NULL);
    assert(n2 == 0);
  #endif

    return n;
}


//
//  Push_Guard_Node: C
//
void Push_Guard_Node(const Node* node)
{
  #if !defined(NDEBUG)
    if (Is_Node_A_Cell(node)) {
        //
        // It is a value.  Cheap check: require that it already contain valid
        // data when the guard call is made (even if GC isn't necessarily
        // going to happen immediately, and value could theoretically become
        // valid before then.)
        //
        const REBVAL* v = cast(const REBVAL*, node);
        assert(CELL_HEART_UNCHECKED(v) < REB_MAX);

      #ifdef STRESS_CHECK_GUARD_VALUE_POINTER
        //
        // Technically we should never call this routine to guard a value
        // that lives inside of a series.  Not only would we have to guard the
        // containing series, we would also have to lock the series from
        // being able to resize and reallocate the data pointer.  But this is
        // a somewhat expensive check, so only feasible to run occasionally.
        //
        Node* containing = Try_Find_Containing_Node_Debug(v);
        if (containing)
            panic (containing);
      #endif
    }
    else {
        // It's a series.  Does not ensure the series being guarded is
        // managed, since it can be interesting to guard the managed
        // *contents* of an unmanaged array.  The calling wrappers ensure
        // managedness or not.
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
    ensureNullptr(g_gc.manuals) = Make_Series_Core(
        15,
        FLAG_FLAVOR(SERIESLIST) | NODE_FLAG_MANAGED
    );
    Clear_Series_Flag(g_gc.manuals, MANAGED);

    // Temporary series and values protected from GC. Holds node pointers.
    //
    ensureNullptr(g_gc.guarded) = Make_Series_Core(15, FLAG_FLAVOR(NODELIST));

    // The marking queue used in lieu of recursion to ensure that deeply
    // nested structures don't cause the C stack to overflow.
    //
    ensureNullptr(g_gc.mark_stack) = Make_Series_Core(
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
}


//
//  Shutdown_GC: C
//
void Shutdown_GC(void)
{
    Free_Unmanaged_Series(g_gc.guarded);
    g_gc.guarded = nullptr;

    Free_Unmanaged_Series(g_gc.mark_stack);
    g_gc.mark_stack = nullptr;

    // Can't use Free_Unmanaged_Series() because g_gc.manuals couldn't be put
    // in the manuals list...Catch-22!  This free must happen after all
    // unmanaged series have been freed.
    //
    GC_Kill_Series(g_gc.manuals);
    g_gc.manuals = nullptr;

  #if DEBUG_COLLECT_STATS
    g_gc.recycle_counter = 0;
    g_gc.recycle_series_total = 0;
    g_gc.recycle_series = 0;
  #endif
}
