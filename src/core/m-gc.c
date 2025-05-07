//
//  file: %m-gc.c
//  summary: "main memory garbage collection"
//  section: memory
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// of Flex Stubs, which is how it was done in R3-Alpha:
//
//     https://en.wikipedia.org/wiki/Tracing_garbage_collection
//
// A Cell's "payload" and "extra" field may or may not contain pointers to
// Stubs that the GC needs to be aware of.  Some small values like LOGIC!
// or INTEGER! don't, because they can fit the entirety of their data into the
// Cell's 4*sizeof(void) capacity...though this would change if INTEGER! added
// support for arbitrary-sized-numbers.
//
// Some Cells embed Stub pointers even when the payload would technically
// fit completely in their Cell.  They do this in order to create a level of
// indirection so that their data can be shared among copies of that Cell.
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
// The simple solution is that when an unmarked Array is hit that it is
// marked and put into a queue for processing (instead of recursed on the
// spot).  This queue is then handled as soon as the marking call is exited,
// and the process repeated until no more items are queued.
//
// !!! There is actually not a specific list of roots of the garbage collect,
// so a first pass of all the Stubs must be done to find them.  This is
// because with the redesigned "librebol" in Ren-C, singular Array Stubs do
// double duty as lifetime-managed containers for Cells handed out by the
// API--without requiring a separate data allocation.  These could be in
// their own "pool", but that would prevent mingling and reuse among Stubs
// used for other purposes like Flex.  Review in light of any new garbage
// collect approaches used.
//

#include "sys-core.h"

#include "sys-int-funcs.h"


#define LINK_Node_TYPE      Node*
#define LINK_Node_CAST

#define MISC_Node_TYPE      Node*
#define MISC_Node_CAST


#if RUNTIME_CHECKS
    static bool in_mark = false; // needs to be per-GC thread
#endif

#define Assert_No_GC_Marks_Pending() \
    assert(Flex_Used(g_gc.mark_stack) == 0)

// The mark_count double checks that every marker set by the GC is cleared.
// To avoid the cost of incrementing and decrementing, only in checked builds.
//
#if RUNTIME_CHECKS
    INLINE void Remove_GC_Mark(const Node* node) {  // stub or pairing
        assert(Is_Node_Marked(node));
        Clear_Node_Marked_Bit(node);
        g_gc.mark_count -= 1;
    }

    INLINE void Remove_GC_Mark_If_Marked(const Node* node) {
        if (Is_Node_Marked(node)) {
            Clear_Node_Marked_Bit(node);
            g_gc.mark_count -= 1;
        }
    }

    INLINE void Add_GC_Mark(const Node* node) {
        assert(not Is_Node_Marked(node));
        Set_Node_Marked_Bit(node);
        g_gc.mark_count += 1;
    }

    INLINE void Add_GC_Mark_If_Not_Already_Marked(const Node* node) {
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

INLINE void Queue_Mark_Maybe_Erased_Cell_Deep(const Cell* v) {
    if (not Is_Cell_Erased(v))
        Queue_Mark_Cell_Deep(v);
}


// Ren-C's PAIR! uses a special kind of Node (called a "Pairing") that embeds
// two Cells in a Stub allocation--a C array of fixed length 2.  It can do this
// because a Cell has a uintptr_t header at the beginning of its struct--just
// like a Stub--and cells reserve the NODE_FLAG_MARKED bit for the GC.  So
// pairings can stealthily participate in the marking, as long as the bit is
// cleared at the end.
//
// !!! Marking a Pairing has the same recursive problems than an array does,
// while not being an array.  So technically we should queue it, but we
// don't have any real world examples of "deeply nested pairings", as they
// are used only in optimized internal structures...the PAIR! datatype only
// allows INTEGER! and DECIMAL! so you can't overflow the stack with it.
//
// Hence we cheat and don't actually queue, for now.
//
static void Queue_Mark_Pairing_Deep(const Pairing* p)
{
    // !!! Hack doesn't work generically, review

  #if RUNTIME_CHECKS
    bool was_in_mark = in_mark;
    in_mark = false;  // would assert about the recursion otherwise
  #endif

    Queue_Mark_Cell_Deep(Pairing_First(p));
    Queue_Mark_Cell_Deep(Pairing_Second(p));  // QUOTED? uses void

    Add_GC_Mark(p);

  #if RUNTIME_CHECKS
    in_mark = was_in_mark;
  #endif
}

static void Queue_Unmarked_Accessible_Stub_Deep(const Stub*);


//
//   Queue_Mark_Node_Deep: C
//
// This routine is given the *address* of the Node to mark, so that the node
// pointer can be updated.  This allows us to fix up lingering references to
// Nodes that are conceptually freed, but only being kept around until
// referencing sites can be fixed up to not refer to them.  As the GC marks
// the nodes, it canonizes such "diminished" pointers to a single global
// "diminished thing".  See Diminish_Stub()
//
// Note: This strategy created some friction when bound words depended on
// contexts to supply their spellings.  This would have required actually
// moving the spellings back into them, but noting that the word itself was
// no longer bound through some flag in the cell.  For now it's not an issue
// since that optimization was removed, but a similar issue could arise again.
//
static void Queue_Mark_Node_Deep(Node** npp) {  // ** for canonizing
    Byte nodebyte = NODE_BYTE(*npp);
    if (nodebyte & NODE_BYTEMASK_0x01_MARKED)  // incl. canon diminished Stub
        return;  // may not be finished marking yet, but has been queued

    if (nodebyte & NODE_BYTEMASK_0x08_CELL) {  // e.g. a pairing
        const Pairing* p = u_cast(const Pairing*, *npp);
        if (Is_Node_Managed(p))
            Queue_Mark_Pairing_Deep(p);
        else {
            // !!! It's a frame?  API handle?  Skip frame case (keysource)
            // for now, but revisit as technique matures.
        }
        return;
    }

    const Stub* s = u_cast(const Stub*, *npp);

    if (nodebyte == DIMINISHED_NON_CANON_BYTE) {  // fixup to global diminished
        *npp = &PG_Inaccessible_Stub;
        return;
    }

  #if RUNTIME_CHECKS
    if (Not_Node_Managed(*npp)) {
        printf("Link to non-MANAGED item reached by GC\n");
        panic (*npp);
    }
  #endif

    Queue_Unmarked_Accessible_Stub_Deep(s);
}

// This is a generic mark routine, which can sense what type a Stub is and
// automatically figure out how to mark it based on flags in the header.
//
// (Note: The data structure used for this processing is a "stack" and not
// a "queue".  But when you use 'queue' as a verb, it has more leeway than as
// the CS noun, and can just mean "put into a list for later processing".)
//
// !!! The reason queueing is used was because things were generating stack
// overflows for deeply nested structures.  With the generic marking of fields
// like Stub.link and Stub.misc, the chains are now long enough just through
// that to generate some deep stacks...even without any cells being marked.
// It hasn't caused any crashes yet, but is something that bears scrutiny.
//
static void Queue_Unmarked_Accessible_Stub_Deep(const Stub* s)
{
    assert(Is_Node_Readable(s));

    Add_GC_Mark(s);

  //=//// MARK LINK AND MISC IF DESIRED ////////////////////////////////////=//

    // All stubs have both link and misc fields available, but they don't
    // necessarily hold node pointers (and even if they do, they may not be
    // references that are intended to keep them live).  So the Flex header
    // flags control whether the marking is done or not.

    if (Get_Stub_Flag(s, LINK_NODE_NEEDS_MARK) and s->link.node)
        Queue_Mark_Node_Deep(&m_cast(Stub*, s)->link.node);

    if (Get_Stub_Flag(s, MISC_NODE_NEEDS_MARK) and s->misc.node)
        Queue_Mark_Node_Deep(&m_cast(Stub*, s)->misc.node);

  //=//// MARK INODE IF NOT USED FOR INFO //////////////////////////////////=//

    // In the case of the INFO/INODE slot, the setting of the needing mark
    // flag is what determines whether the slot is used for info or not.  So
    // if it's available for non-info uses, it is always a live marked node.

    if (Get_Stub_Flag(s, INFO_NODE_NEEDS_MARK) and s->info.node)
        Queue_Mark_Node_Deep(&m_cast(Stub*, s)->info.node);

    if (Is_Stub_Keylist(s)) {
        //
        // !!! KeyLists may not be the only category that are just a straight
        // list of node pointers.
        //
        const KeyList* keylist = u_cast(const KeyList*, s);
        const Key* tail = Flex_Tail(Key, keylist);
        const Key* key = Flex_Head(Key, keylist);
        for (; key != tail; ++key) {
            //
            // Symbol* are not available to the user to free out from under
            // a keylist (can't use FREE on them) and shouldn't vanish.
            //
            assert(Is_Node_Readable(*key));
            if (Is_Node_Marked(*key))
                continue;
            Queue_Unmarked_Accessible_Stub_Deep(*key);
        }
    }
    else if (Stub_Holds_Cells(s)) {
        Array* a = x_cast(Array*, s);

    //=//// MARK BONUS (if not using slot for `bias`) /////////////////////=//

        // Whether the bonus slot needs to be marked is dictated by internal
        // Flex Flavor, not an extension-usable flag (due to flag scarcity).
        //
        if (Is_Stub_Varlist(a)) {  // bonus is keylist (if not module varlist)
            assert(Is_Stub_Keylist(cast(Stub*, BONUS_VARLIST_KEYLIST(a))));
            Queue_Mark_Node_Deep(&a->content.dynamic.bonus.node);
        }

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
        if (Is_Flex_Full(g_gc.mark_stack))
            Extend_Flex_If_Necessary(g_gc.mark_stack, 8);
        *Flex_At(Array*, g_gc.mark_stack, Flex_Used(g_gc.mark_stack)) = a;
        Set_Flex_Used(  // doesn't add a terminator
            g_gc.mark_stack,
            Flex_Used(g_gc.mark_stack) + 1
        );
    }
}


//
//  Queue_Mark_Cell_Deep: C
//
static void Queue_Mark_Cell_Deep(const Cell* c)
{
    if (Not_Cell_Readable(c))
        return;

    // We mark based on the type of payload in the cell, e.g. its "unescaped"
    // form.  So if '''a fits in a WORD! (despite being a QUOTED?), we want
    // to mark the cell as if it were a plain word.  Use the Heart_Of().
    //
    Option(Heart) heart = Heart_Of(c);

  #if RUNTIME_CHECKS  // see Queue_Mark_Node_Deep() for notes on recursion
    assert(not in_mark);
    in_mark = true;
  #endif

    if (Is_Extra_Mark_Heart(heart))
        if (c->extra.node)
            Queue_Mark_Node_Deep(&m_cast(Cell*, c)->extra.node);

    if (Not_Cell_Flag_Unchecked(c, DONT_MARK_NODE1))
        if (c->payload.split.one.node)
            Queue_Mark_Node_Deep(&m_cast(Cell*, c)->payload.split.one.node);

    if (Not_Cell_Flag_Unchecked(c, DONT_MARK_NODE2))
        if (c->payload.split.two.node)
            Queue_Mark_Node_Deep(&m_cast(Cell*, c)->payload.split.two.node);

  #if RUNTIME_CHECKS
    in_mark = false;
    Assert_Cell_Marked_Correctly(c);
  #endif
}


//
//  Propagate_All_GC_Marks: C
//
// The Mark Stack is a Flex containing Flex pointers.  They have already
// had their FLEX_FLAG_MARK set to prevent being added to the stack multiple
// times, but the items they can reach are not necessarily marked yet.
//
// Processing continues until all reachable items from the mark stack are
// known to be marked.
//
static void Propagate_All_GC_Marks(void)
{
    assert(not in_mark);

    while (Flex_Used(g_gc.mark_stack) != 0) {
        Set_Flex_Used(g_gc.mark_stack, Flex_Used(g_gc.mark_stack) - 1);

        // Data pointer may change in response to an expansion during
        // Mark_Array_Deep_Core(), so must be refreshed on each loop.
        //
        Array* a = *Flex_At(
            Array*,
            g_gc.mark_stack,
            Flex_Used(g_gc.mark_stack)
        );

        // Termination is not required in the release build (the length is
        // enough to know where it ends).  But corrupt in debug.
        //
        Corrupt_Pointer_If_Debug(
            *Flex_At(
                Array*,
                g_gc.mark_stack,
                Flex_Used(g_gc.mark_stack)
            )
        );

        // We should have marked this Flex at queueing time to keep it from
        // being doubly added before the queue had a chance to be processed
        //
        assert(Is_Node_Marked(a));

        Cell* v = Array_Head(a);
        const Cell* tail = Array_Tail(a);
        for (; v != tail; ++v) {
          #if RUNTIME_CHECKS
            Flavor flavor = Stub_Flavor(a);
            assert(flavor <= MAX_FLAVOR_HOLDS_CELLS);

            if (QUOTE_BYTE(v) == ANTIFORM_0) {
                if (flavor < MIN_FLAVOR_ANTIFORMS_OK)
                    panic (v);  // antiforms not legal in many array types

                if (Is_Antiform_Unstable(cast(Atom*, v)))  // always illegal
                    panic (v);
            }
          #endif

            Queue_Mark_Cell_Deep(v);
        }

      #if RUNTIME_CHECKS
        Assert_Array_Marked_Correctly(a);
      #endif
    }
}


//
//  Reify_Variadic_Feed_As_Array_Feed: C
//
// For performance and memory usage reasons, a variadic C function call that
// wants to invoke the evaluator with just a comma-delimited list of Value*
// does not need to make an Array to hold them.  Fetch_Next_In_Feed() is
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
            Init_Quasi_Word(PUSH(), CANON(OPTIMIZED_OUT));

        do {
            Derelativize(PUSH(), At_Feed(feed), Feed_Binding(feed));
            assert(Not_Antiform(TOP));
            Fetch_Next_In_Feed(feed);
        } while (Not_Feed_At_End(feed));

        assert(TOP_INDEX != base);
        if (FEED_IS_VARIADIC(feed))  // UTF-8 scan may have finalized it
            Finalize_Variadic_Feed(feed);

        Offset index = truncated ? 2 : 1;  // skip --optimized-out--

        Source* a = Pop_Managed_Source_From_Stack(base);
        Init_Any_List_At(Feed_Data(feed), TYPE_BLOCK, a, index);

        // need to be sure feed->p isn't invalid... and not end

        if (truncated)
            feed->p = Array_At(Feed_Array(feed), 1);  // skip trunc
        else
            feed->p = Array_Head(Feed_Array(feed));

        assert(Ensure_Readable(At_Feed(feed)));  // not end at start, not end now

        // The array just popped into existence, and it's tied to a running
        // level...so safe to say we're holding it.
        //
        assert(Not_Feed_Flag(feed, TOOK_HOLD));
        Set_Flex_Info(Feed_Array(feed), HOLD);
        Set_Feed_Flag(feed, TOOK_HOLD);
    }
    else {
        Finalize_Variadic_Feed(feed);

        if (truncated) {
            Init_Quasi_Word(PUSH(), CANON(OPTIMIZED_OUT));

            Source* a = Pop_Managed_Source_From_Stack(base);
            Init_Any_List_At(Feed_Data(feed), TYPE_BLOCK, a, 1);
        }
        else
            Init_Any_List_At(Feed_Data(feed), TYPE_BLOCK, g_empty_array, 0);

        feed->p = &PG_Feed_At_End;
    }

    assert(FEED_INDEX(feed) <= Array_Len(Feed_Array(feed)));
}


//
//  Run_All_Handle_Cleaners: C
//
// !!! There's an issue with handles storing pointers to rebAlloc()'d data,
// which is that they want to do their cleanup work before the system is
// damaged by the shutdown process.  This is a naive extra pass done during
// shutdown to deal with the problem--but it should be folded in with
// Mark_Root_Stubs().
//
void Run_All_Handle_Cleaners(void) {
    Segment* seg = g_mem.pools[STUB_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Byte* unit = cast(Byte*, seg + 1);
        Length n = g_mem.pools[STUB_POOL].num_units_per_segment;
        for (; n > 0; --n, unit += sizeof(Stub)) {
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;

            if (unit[0] & NODE_BYTEMASK_0x08_CELL)
                continue;  // not a stub

            Stub* stub = cast(Stub*, unit);
            if (Is_Stub_Diminished(stub))
                continue;  // freed, but not canonized

            if (Stub_Flavor(stub) != FLAVOR_HANDLE)
                continue;  // only interested in handles

            assert(Is_Node_Managed(stub));  // it's why handle stubs exist

            Diminish_Stub(stub);
        }
    }
}


//
//  Mark_Root_Stubs: C
//
// A Root Stub is any manual Flex that was allocated but has not been
// managed yet, as well as Alloc_Value() Stubs that are explicitly "roots".
//
// For root Stubs, this checks to see if their lifetime was dependent on a
// FRAME!, and if that frame is no longer on the stack.  If so, it (currently)
// will panic if that frame did not end due to a fail().  This could be
// relaxed to automatically free those Nodes as a normal GC.
//
// !!! This implementation walks over *all* the Stubs.  It wouldn't have to
// if API Stubs were in their own pool, or if the outstanding manuals list
// were maintained even in release builds--it could just walk those.  This
// should be weighed against background GC and other more sophisticated
// methods which might come down the road for the GC than this simple one.
//
// !!! A smarter switch statement here could do this more optimally...see the
// sweep code for an example.
//
static void Mark_Root_Stubs(void)
{
    Segment* seg = g_mem.pools[STUB_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Byte* unit = cast(Byte*, seg + 1);
        Length n = g_mem.pools[STUB_POOL].num_units_per_segment;

        for (; n > 0; --n, unit += sizeof(Stub)) {
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;

            assert(unit[0] & NODE_BYTEMASK_0x80_NODE);

            if (not (unit[0] & NODE_BYTEMASK_0x02_ROOT))
                continue;

            assert(not (unit[0] & NODE_BYTEMASK_0x08_CELL));  // no root pairs

            Stub* s = cast(Stub*, unit);
            assert(Is_Node_Readable(s));

            // This stub came from Alloc_Value() or rebAlloc(); the only
            // references should be from the C stack.  So this pass is the
            // only place where these stubs could be marked.

            if (Not_Node_Managed(s))
                assert(not Is_Node_Marked(s));
            else
                Add_GC_Mark(s);

            if (Stub_Holds_Cells(s)) {  // It's an Alloc_Value()
                //
                // 1. Mark_Level_Stack_Deep() marks the owner.
                //
                // 2. Evaluation may target API cells, may be Is_Cell_Erased().
                // (though they should not have NODE_FLAG_ROOT set until after
                // the evaluation is finished).  (They should only be fresh if
                // targeted by some Level's L->out...could we verify that?)
                //
                Queue_Mark_Maybe_Erased_Cell_Deep(Stub_Cell(s));  // [2]
            }
            else {  // It's a rebAlloc()
                assert(Stub_Flavor(s) == FLAVOR_BINARY);
            }
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
    const Cell* head = Flex_Head(Cell, g_ds.array);  // unstable allowed
    assert(Is_Cell_Poisoned(head));  // Data_Stack_At(0) deliberately invalid

    Cell* stackval = g_ds.movable_top;
    for (; stackval != head; --stackval)  // stop before Data_Stack_At(0)
        Queue_Mark_Cell_Deep(stackval);

  #if DEBUG_POISON_DROPPED_STACK_CELLS
    stackval = g_ds.movable_top + 1;
    for (; stackval != Flex_Tail(Cell, g_ds.array); ++stackval)
        assert(Is_Cell_Poisoned(stackval));
  #endif

    Propagate_All_GC_Marks();
}


//
//  Mark_Guarded_Nodes: C
//
// Mark Stubs and Cells that have been temporarily protected from garbage
// collection with Push_Lifeguard.
//
// 1. For efficiency, the system allows ranges of places that cells will be
//    put to be memset() to 0.  The Init_XXX() routines will then make sure
//    the NODE_FLAG_NODE and NODE_FLAG_CELL are OR'd onto it.  If you GC Guard
//    a cell made with DECLARE_ATOM()/DECLARE_VALUE()/DECLARE_ELEMENT() it
//    will be in the erased state, and even if you put the NODE and CELL
//    bits on it, the evaluator may transitionally Erase_Cell() on it.
//
// 2. Guarding a Cell means keeping its contents alive...the Cell is assumed
//    to not live in a Flex or Pairing.  Marks on the Cell itself are not
//    covered... if this happens, treat it as a bug.
//
static void Mark_Guarded_Nodes(void)
{
    void* *pp = Flex_Head(void*, g_gc.guarded);
    REBLEN n = Flex_Used(g_gc.guarded);
    for (; n > 0; --n, ++pp) {
        if (FIRST_BYTE(*pp) == 0) {  // assume erased cell, tolerate [1]
            assert(Is_Cell_Erased(c_cast(Cell*, *pp)));
            continue;
        }

        Node** npp = cast(Node**, pp);
        if (Is_Node_A_Cell(*npp)) {
            assert(Not_Node_Marked(*npp));  // shouldn't live in array [2]
            Queue_Mark_Maybe_Erased_Cell_Deep(c_cast(Cell*, *npp));
        }
        else  // a Stub
            Queue_Mark_Node_Deep(npp);

        Propagate_All_GC_Marks();
    }
}


//
//  Mark_Level: C
//
// Some logic applies to all Levels, with a bit of nuance for marking the
// fields in the L->u union based on their Executor.
//
static void Mark_Level(Level* L) {

  //=//// MARK FEED (INCLUDES BINDING) ////////////////////////////////////=//

    // 1. Misc_Feedstub_Pending() should either live in Feed_Array(), or it
    //    may be corrupt (e.g. if it's an apply).  GC can ignore it.
    //
    // 2. This used to mark L->feed->p; but we probably do not need to.  All
    //    variadics are reified as arrays in the GC (we could avoid this
    //    using va_copy, but probably not worth it).  All values in feed
    //    should be covered in terms of GC protection.
    //
    // 3. If ->gotten is set, it usually shouldn't need marking because
    //    it's fetched via L->value and so would be kept alive by it.  Any
    //    code that a level runs that might disrupt that relationship so it
    //    would fetch differently should have meant clearing ->gotten.

    Stub* singular = Feed_Singular(L->feed);  // don't mark Misc Pending [1]
    do {
        Queue_Mark_Cell_Deep(Stub_Cell(singular));
        singular = maybe Link_Feedstub_Splice(singular);
    } while (singular);

    Context* L_binding = Level_Binding(L);  // marks binding, not feed->p [2]
    if (
        L_binding != SPECIFIED
        and (L_binding->leader.bits & NODE_FLAG_MANAGED)
    ){
        Queue_Mark_Node_Deep(&Feed_Data(L->feed)->extra.node);
    }

    if (L->feed->gotten)  // shouldn't need to mark feed->gotten [3]
        assert(L->feed->gotten == Lookup_Word(At_Level(L), L_binding));

  //=//// MARK FRAME CELLS ////////////////////////////////////////////////=//

    // Level cells should always contain initialized bits, though erased or
    // fresh cells are allowed.

    Queue_Mark_Maybe_Erased_Cell_Deep(L->out);
    Queue_Mark_Maybe_Erased_Cell_Deep(&L->feed->fetched);
    Queue_Mark_Maybe_Erased_Cell_Deep(&L->spare);
    Queue_Mark_Maybe_Erased_Cell_Deep(&L->scratch);

    if (not Is_Action_Level(L)) {
        if (L->executor == &Evaluator_Executor)
            Queue_Mark_Maybe_Erased_Cell_Deep(&L->u.eval.primed);

        return;
    }

  //=//// SPECIAL MARKING FOR ACTION_EXECUTOR() LEVELS ////////////////////=//

    // 1. If the context is all set up with valid values and managed, then it
    //    can be marked normally...no need for partial parameter traversal.
    //
    // 2. The cast(VarList, ...) operation does extra integrity checking of
    //    the VarList in some debug builds, and the VarList may not be
    //    complete at this point.  Cast to an array.
    //
    // 3. For efficiency, function argument slots are not pre-formatted--they
    //    are initialized during the sunk cost of the parameter walk.  Hence
    //    how far the function has gotten in its fulfillment must be taken
    //    into account.  Only those argument slots that have been fulfilled
    //    may be GC protected, since the others contain random bits.
    //
    // 4. Erasure (CELL_MASK_ERASED_0 in a cell's header) is a state that is
    //    legal during evaluation, but not a valid state for cells in VarLists
    //    (or Arrays).  It's thus only legal for frames that are fulfilling,
    //    and only in the slot that is being fulfilled at the present moment
    //    (skipped arguments picked up later are set to CELL_MASK_UNREADABLE).

    Queue_Mark_Node_Deep(  // L->u.action.original is never nullptr
        cast(Node**, m_cast(Phase**, &L->u.action.original))
    );

  #if DEBUG_LEVEL_LABELS
    assert(L->label_utf8 != nullptr);
  #endif
    if (L->u.action.label) {  // nullptr if ANONYMOUS
        const Symbol* s = unwrap L->u.action.label;
        if (not Is_Node_Marked(s))
            Queue_Unmarked_Accessible_Stub_Deep(s);
    }

    if (L->varlist and Is_Node_Managed(L->varlist)) {  // normal marking [1]
        assert(
            not Is_Level_Fulfilling(L)
            or LEVEL_STATE_BYTE(L) == ST_ACTION_TYPECHECKING  // filled/safe
        );

        Queue_Mark_Node_Deep(  // may be incomplete, can't cast(VarList*) [2]
            cast(Node**, m_cast(Array**, &L->varlist))
        );
        return;
    }

    if (
        Is_Level_Fulfilling(L)
        and (
            LEVEL_STATE_BYTE(L) == ST_ACTION_INITIAL_ENTRY
            or LEVEL_STATE_BYTE(L) == ST_ACTION_INITIAL_ENTRY_INFIX
        )
    ){
        return;  // args and locals are poison/garbage
    }

    Phase* phase = Level_Phase(L);
    const Key* key_tail;
    const Key* key = Phase_Keys(&key_tail, phase);

    if (
        Is_Level_Fulfilling(L)
        and Not_Executor_Flag(ACTION, L, DOING_PICKUPS)
    ){
        key_tail = L->u.action.key + 1;  // don't mark uninitialized bits [3]
    }

    Value* arg = Level_Args_Head(L);
    for (; key != key_tail; ++key, ++arg) {  // key_tail may be truncated [3]
        if (Is_Cell_Erased(arg)) {
            assert(Is_Level_Fulfilling(L) and key == L->u.action.key);
            continue;  // only the current cell is allowed to be erased [4]
        }
        Queue_Mark_Cell_Deep(arg);
    }
}


//
//  Mark_All_Levels: C
//
// Levels are not "Nodes" and are not garbage collected.  But they may not
// all be reachable from the TOP_LEVEL -> BOTTOM_LEVEL stack, due to the
// fact that ranges of Levels are sometimes "unplugged" by Generators and
// Yielders.  The HANDLE!s holding those Levels are responsible for the
// replugging of the Levels or freeing of them, but we have to enumerate
// the pool to find all the live Levels since there's not another good way.
//
static void Mark_All_Levels(void)
{
    Segment* seg = g_mem.pools[LEVEL_POOL].segments;
    Size wide = g_mem.pools[LEVEL_POOL].wide;
    assert(wide >= Size_Of(Level));

    for (; seg != nullptr; seg = seg->next) {
        Count n = g_mem.pools[LEVEL_POOL].num_units_per_segment;
        Byte* unit = cast(Byte*, seg + 1);  // byte beats strict alias

        for (; n > 0; --n, unit += wide) {
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;

            Level* level = cast(Level*, unit);
            Mark_Level(level);
            Propagate_All_GC_Marks();
        }
    }
}


#if UNUSUAL_CELL_SIZE

static REBLEN Sweep_Distinct_Pairing_Pool(void)
{
    Count sweep_count = 0;

    Segment* seg = g_mem.pools[PAIR_POOL].segments;
    Size wide = g_mem.pools[PAIR_POOL].wide;
    assert(wide >= 2 * Size_Of(Cell));

    for (; seg != nullptr; seg = seg->next) {
        Length n = g_mem.pools[PAIR_POOL].num_units_per_segment;

        Byte *unit = cast(Byte*, seg + 1);
        for (; n > 0; --n, unit += wide) {
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;

            assert(unit[0] & NODE_BYTEMASK_0x08_CELL);

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

    return sweep_count;
}

#endif


//
//  Sweep_Stubs: C
//
// Scans all Stub Nodes (Stub structs) in all segments that are part of
// the STUB_POOL.  If a Stub had its lifetime management delegated to the
// garbage collector with Manage_Flex(), then if it didn't get "marked" as
// live during the marking phase then free it.
//
// 1. We use a generic byte pointer (unsigned char*) to dodge the rules for
//    strict aliases, as the pool contain pairs of Cell from Alloc_Pairing(),
//    or a Stub from Prep_Stub().  The shared first byte node masks are
//    defined and explained in %struct-node.h
//
// 2. For efficiency of memory use, Stub is nominally 2*sizeof(Cell), and so
//    Pairings can use the same Stub nodes.  But features that might make the
//    two Cells a size greater than the Stub size require doing pairings in a
//    different pool.
//
Count Sweep_Stubs(void)
{
    Count sweep_count = 0;

    Segment* seg = g_mem.pools[STUB_POOL].segments;
    Size wide = g_mem.pools[STUB_POOL].wide;
    assert(wide == sizeof(Stub));
    UNUSED(wide);

    for (; seg != nullptr; seg = seg->next) {
        Count n = g_mem.pools[STUB_POOL].num_units_per_segment;
        Byte* unit = cast(Byte*, seg + 1);  // byte beats strict alias [1]

        for (; n > 0; --n, unit += sizeof(Stub)) {
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;  // only unit without NODE_FLAG_NODE (in ASCII range)

            assert(unit[0] & NODE_BYTEMASK_0x80_NODE);

            if (not (unit[0] & NODE_BYTEMASK_0x04_MANAGED)) {
                assert(not (unit[0] & NODE_BYTEMASK_0x01_MARKED));

                if (unit[0] == DIMINISHED_NON_CANON_BYTE) {
                    Free_Pooled(STUB_POOL, unit);
                    continue;
                }
                assert(not (unit[0] & NODE_BYTEMASK_0x40_UNREADABLE));
                continue;  // ignore all unmanaged Stubs/Pairings
            }

            if (unit[0] & NODE_BYTEMASK_0x01_MARKED) {  // managed and marked
                Remove_GC_Mark(u_cast(Node*, unit));  // just remove mark
                continue;
            }

            assert(not (unit[0] & NODE_BYTEMASK_0x02_ROOT));  // roots marked

            ++sweep_count;  // managed but not marked => free it!

            if (unit[0] & NODE_BYTEMASK_0x08_CELL) {  // managed pairing
                Free_Pooled(STUB_POOL, unit);  // manuals use Free_Pairing()
                continue;
            }

            if (unit[0] & NODE_BYTEMASK_0x40_UNREADABLE) {
                //
                // Stubs that have been marked freed may have outstanding
                // references at the moment of being marked free...but the GC
                // canonizes the reference pointers to PG_Inaccessible_Stub.
                // So they should not have been marked, and once the GC pass
                // is over we can just free them.
            }
            else
                Diminish_Stub(u_cast(Stub*, unit));

            GC_Kill_Stub(u_cast(Stub*, unit));
        }
    }

  #if UNUSUAL_CELL_SIZE  // pairing pool is separate in this case [2]
    sweep_count += Sweep_Distinct_Pairing_Pool();
  #endif

    return sweep_count;
}


#if RUNTIME_CHECKS

//
//  Fill_Sweeplist: C
//
REBLEN Fill_Sweeplist(Flex* sweeplist)
{
    assert(Flex_Wide(sweeplist) == sizeof(Node*));
    assert(Flex_Used(sweeplist) == 0);

    Count sweep_count = 0;

    Segment* seg = g_mem.pools[STUB_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Count n = g_mem.pools[STUB_POOL].num_units_per_segment;
        Byte* stub = cast(Byte*, seg + 1);

        for (; n > 0; --n, stub += sizeof(Stub)) {
            switch (*stub >> 4) {
              case 9: {  // 0x8 + 0x1
                Flex* s = x_cast(Flex*, stub);
                Assert_Flex_Managed(s);
                if (Is_Node_Marked(s)) {
                    Remove_GC_Mark(s);
                }
                else {
                    Expand_Flex_Tail(sweeplist, 1);
                    *Flex_At(Node*, sweeplist, sweep_count) = s;
                    ++sweep_count;
                }
                break; }

              case 11: {  // 0x8 + 0x2 + 0x1
                //
                // It's a cell which is managed where the value is not an END.
                // This is a managed Pairing, so mark bit should be heeded.
                //
                // !!! It is (usually) in the STUB_POOL, but *not* a "Stub".
                //
                Pairing* pairing = x_cast(Pairing*, stub);
                assert(Is_Node_Managed(pairing));
                if (Is_Node_Marked(pairing)) {
                    Remove_GC_Mark(pairing);
                }
                else {
                    Expand_Flex_Tail(sweeplist, 1);
                    *Flex_At(Node*, sweeplist, sweep_count) = pairing;
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
// to be a Flex whose width is sizeof(Stub), and it will be filled with
// the list of Stubs that *would* be recycled.
//
REBLEN Recycle_Core(Flex* sweeplist)
{
    // Ordinarily, it should not be possible to spawn a recycle during a
    // recycle.  But when debug code is added into the recycling code, it
    // could cause a recursion.  Be tolerant of such recursions to make that
    // debugging easier...but make a note that it's not ordinarily legal.
    //
  #if RUNTIME_CHECKS
    if (g_gc.recycling) {
        printf("Recycle re-entry; should only happen in debug scenarios.\n");
        Set_Trampoline_Flag(RECYCLE);
        return 0;
    }
  #endif

    // If disabled by RECYCLE:OFF, exit now but set the pending flag.  (If
    // shutdown, ignore so recycling runs and can be checked for balance.)
    //
    if (g_gc.disabled) {
        Set_Trampoline_Flag(RECYCLE);
        return 0;
    }

    g_gc.recycling = true;

    Assert_No_GC_Marks_Pending();

  #if DEBUG_COLLECT_STATS
    g_gc.recycle_counter++;
    g_gc.recycled_stubs = g_mem.pools[STUB_POOL].free;
  #endif

    // Builtin patches for Lib contain variables that can be read by LIB(XXX)
    // in the C code.  Since effectively any of them could become referred
    // to in code, we need to keep the cells alive.
    //
    // We don't technically need to mark the patches themselves to protect
    // them from GC--because they're not in the STUB_POOL so Sweep_Stubs()
    // wouldn't free them.  But we mark them anyway--for clarity, and it
    // speeds up references that would mark them to see they're spoken for
    // (so they don't have to detect it's an array, queue the cell...)

    assert(Is_Stub_Erased(&g_datatype_patches[cast(Byte, TYPE_0)]));  // skip

    for (
        SymId16 id16 = MIN_SYM_BUILTIN_TYPES;
        id16 <= MAX_SYM_BUILTIN_TYPES;
        ++id16
    ){
        Type type = Type_From_Symbol_Id(cast(SymId, id16));
        Patch* patch = &g_datatype_patches[cast(Byte, type)];
        if (Not_Node_Marked(patch)) {  // this loop's prior steps can mark
            Add_GC_Mark(patch);
            Queue_Mark_Maybe_Erased_Cell_Deep(Stub_Cell(patch));
        }
    }
    Propagate_All_GC_Marks();

    assert(Is_Stub_Erased(&g_lib_patches[SYM_0]));  // skip SYM_0

    for (SymId16 id = 1; id <= MAX_SYM_LIB_PREMADE; ++id) {
        Patch* patch = &g_lib_patches[id];
        if (Not_Node_Marked(patch)) {  // this loop's prior steps can mark
            Add_GC_Mark(patch);
            Queue_Mark_Maybe_Erased_Cell_Deep(Stub_Cell(patch));
        }
    }
    Propagate_All_GC_Marks();

    // It was previously assumed no recycle would happen while the evaluator
    // was in a thrown state.  There's no particular reason to enforce that
    // in stackless, so it has been relaxed.
    //
    Queue_Mark_Maybe_Erased_Cell_Deep(&g_ts.thrown_arg);
    Queue_Mark_Maybe_Erased_Cell_Deep(&g_ts.thrown_label);
    Propagate_All_GC_Marks();

    // MARKING PHASE: the "root set" from which we determine the liveness
    // (or deadness) of a Stub.  If we are shutting down, we do not mark
    // several categories of Stub...but we do need to run the root marking.
    // (In particular because that is when API Stubs whose lifetimes
    // are bound to Levels will be freed, if the Level is expired.)
    //
    Mark_Root_Stubs();
    Assert_No_GC_Marks_Pending();

    Mark_Data_Stack();
    Assert_No_GC_Marks_Pending();

    Mark_Guarded_Nodes();
    Assert_No_GC_Marks_Pending();

    Mark_All_Levels();
    Assert_No_GC_Marks_Pending();

    // The last thing we do is go through all the "sea contexts" and make sure
    // that if anyone referenced the context, then their variables remain live.
    //
    // This must be done *iteratively* so long as the process transitions any
    // more modules into the live set.  Our weak method at the moment is just
    // to check if any more markings occur.
    //
    while (true) {
        bool added_marks = false;

        Symbol** psym = Flex_Head(Symbol*, g_symbols.by_hash);
        Symbol** psym_tail = Flex_Tail(Symbol*, g_symbols.by_hash);
        for (; psym != psym_tail; ++psym) {
            if (*psym == nullptr or *psym == &g_symbols.deleted_symbol)
                continue;
            Stub* stub = Misc_Hitch(*psym);
            for (; stub != *psym; stub = Misc_Hitch(stub)) {
                SeaOfVars* sea = Info_Patch_Sea(cast(Patch*, stub));
                if (Is_Node_Marked(stub)) {
                    assert(Is_Node_Marked(sea));
                    continue;
                }
                if (Is_Node_Marked(sea)) {
                    Add_GC_Mark(stub);
                    added_marks = true;

                    Queue_Mark_Cell_Deep(Stub_Cell(stub));

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

    // The PG_Inaccessible_Stub always looks marked, so we can skip it
    // quickly in GC (and not confuse it with non-canon diminished stubs).
    //
    assert(Is_Node_Marked(&PG_Inaccessible_Stub));

    REBLEN sweep_count;

    if (sweeplist != NULL) {
    #if NO_RUNTIME_CHECKS
        panic (sweeplist);
    #else
        sweep_count = Fill_Sweeplist(sweeplist);
    #endif
    }
    else
        sweep_count = Sweep_Stubs();

    // Unmark the LIB() fixed patches (not in stub pool, never get swept)

    assert(Is_Stub_Erased(&g_datatype_patches[cast(Byte, SYM_0)]));  // skip

    for (
        SymId16 id = MIN_SYM_BUILTIN_TYPES;
        id <= MAX_SYM_BUILTIN_TYPES;
        ++id
    ){
        Type type = Type_From_Symbol_Id(cast(SymId, id));
        Patch* patch = &g_datatype_patches[cast(Byte, type)];
        Remove_GC_Mark(patch);
    }

    assert(Is_Stub_Erased(&g_lib_patches[SYM_0]));  // skip SYM_0

    for (SymId16 id = 1; id <= MAX_SYM_LIB_PREMADE; ++id) {
        Patch* patch = &g_lib_patches[id];
        Remove_GC_Mark(patch);
    }

    // Unmark the CANON() fixed symbols (not in stub pool, never get swept)

    assert(Is_Stub_Erased(&g_symbols.builtin_canons[SYM_0]));  // skip SYM_0

    for (SymId16 id = 1; id <= MAX_SYM_BUILTIN; ++id) {
        Symbol* canon = &g_symbols.builtin_canons[id];
        Remove_GC_Mark_If_Marked(canon);
    }

  #if RUNTIME_CHECKS
    assert(g_gc.mark_count == 0);  // should have balanced out
  #endif

  #if DEBUG_COLLECT_STATS
    // Compute new stats:
    g_gc.recycled_stubs
        = g_mem.pools[STUB_POOL].free - g_gc.recycled_stubs;
    g_gc.recycled_stubs_total += g_gc.recycled_stubs;
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

    g_gc.recycling = false;

  #if RUNTIME_CHECKS
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
    // any additional Stubs that were not freed by the first.  (It also
    // shouldn't crash.)  This is an expensive check, but helpful to try if
    // it seems a GC left things in a bad state that crashed a later GC.
    //
    REBLEN n2 = Recycle_Core(nullptr);
    assert(n2 == 0);
  #endif

    return n;
}


//
//  Push_Lifeguard: C
//
// 1. It is legal to guard erased cells, which do not have the NODE_FLAG_NODE
//    bit set.  So an exemption is made if a header slot is all 0 bits.
//
// 2. Technically we should never call this routine to guard a value that lives
//    in some array.  Not only would we have to guard the containing array, we
//    would also have to lock the array from being able to resize and
//    reallocate the data pointer.  But this is a somewhat expensive check, so
//    only feasible to run occasionally.
//
// 3. At one time this didn't ensure the Stub being guarded was managed, based
//    on the idea of guarding the contents of an unmanaged array.  That idea
//    didn't get any usage, and allowing unmanaged guards here just obfuscated
//    errors when they occurred.  So the assert has been put back.  Review.
//
void Push_Lifeguard(const void* p)  // NODE_FLAG_NODE may not be set [1]
{
    if (FIRST_BYTE(p) == 0) {  // assume erased cell [1]
        assert(Is_Cell_Erased(c_cast(Cell*, p)));
    }
    else if (Is_Node_A_Cell(c_cast(Node*, p))) {
        assert(Not_Node_Marked(c_cast(Node*, p)));  // don't guard during GC

      #ifdef STRESS_CHECK_GUARD_VALUE_POINTER
        const Cell* cell = c_cast(Cell*, p);

        Node* containing = Try_Find_Containing_Node_Debug(v);
        if (containing)  // cell shouldn't live in array or pairing [2]
            panic (containing);
      #endif
    }
    else {  // It's a Stub
        assert(Is_Node_Readable(c_cast(Node*, p)));  // not diminished
        assert(Not_Node_Marked(c_cast(Node*, p)));  // don't guard during GC
        assert(Is_Node_Managed(c_cast(Node*, p)));  // [3]
    }

    if (Is_Flex_Full(g_gc.guarded))
        Extend_Flex_If_Necessary(g_gc.guarded, 8);

    *Flex_At(const void*, g_gc.guarded, Flex_Used(g_gc.guarded)) = p;

    Set_Flex_Used(g_gc.guarded, Flex_Used(g_gc.guarded) + 1);
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
    assert(g_gc.recycled_stubs_total == 0);
    assert(g_gc.recycled_stubs == 0);
  #endif

    // Manually allocated Flex that GC is not responsible for (unless a
    // fail() occurs). Holds Flex pointers.  Must happen before any unmanaged
    // allocations!
    //
    // As a trick to keep this Flex from trying to track itself, say it's
    // managed, then sneak the flag off.
    //
    ensure(nullptr, g_gc.manuals) = Make_Flex(
        FLAG_FLAVOR(FLEXLIST) | NODE_FLAG_MANAGED,  // lie!
        Flex,
        15
    );
    Clear_Node_Managed_Bit(g_gc.manuals);  // untracked and indefinite lifetime

    // Flexes and Cells protected from GC.  Holds node pointers.
    //
    ensure(nullptr, g_gc.guarded) = Make_Flex(
        FLAG_FLAVOR(NODELIST),
        Flex,
        15
    );

    // The marking queue used in lieu of recursion to ensure that deeply
    // nested structures don't cause the C stack to overflow.
    //
    ensure(nullptr, g_gc.mark_stack) = Make_Flex(
        FLAG_FLAVOR(NODELIST),
        Flex,
        100
    );

    g_gc.ballast = MEM_BALLAST;  // or overwritten by R3_RECYCLE_TORTURE below

    const char *env_recycle_torture = getenv("R3_RECYCLE_TORTURE");
    if (env_recycle_torture and atoi(env_recycle_torture) != 0)
        g_gc.ballast = 0;

  #if RUNTIME_CHECKS  // !!! How to give message in release build (no printf?)
    if (g_gc.ballast == 0) {
        printf(
            "**\n" \
            "** R3_RECYCLE_TORTURE is nonzero in environment variable!\n" \
            "** (or g_gc.ballast is set to 0 manually in the init code)\n" \
            "** Recycling on EVERY evaluator step, *EXTREMELY* SLOW!...\n" \
            "** Useful in finding bugs before you can run RECYCLE:TORTURE\n" \
            "** But you might only want to do this with -O2 checked builds.\n"
            "**\n"
        );
        fflush(stdout);
    }
  #endif

    g_gc.depletion = g_gc.ballast;

    // When a Flex needs to expire its content for some reason (including
    // the user explicitly saying FREE), then there can still be references to
    // that Flex around.  Since we don't want to trigger a GC synchronously
    // each time this happens, the NODE_FLAG_UNREADABLE flag is added to Flex,
    // it is checked for by value extractors (like Cell_Varlist()).  But once
    // the GC gets a chance to run, those stubs can be swept with all the
    // inaccessible references canonized to this one global Stub.
    //
    Stub* s = Prep_Stub(
        FLAG_FLAVOR(THE_GLOBAL_INACCESSIBLE)
            | NODE_FLAG_UNREADABLE
            | NODE_FLAG_MARKED,
        &PG_Inaccessible_Stub
    );
    assert(Is_Stub_Diminished(&PG_Inaccessible_Stub));
    assert(NODE_BYTE(s) == DIMINISHED_CANON_BYTE);
    UNUSED(s);
}


//
//  Shutdown_GC: C
//
void Shutdown_GC(void)
{
    assert(not g_gc.recycling);

    Clear_Node_Marked_Bit(&PG_Inaccessible_Stub);
    GC_Kill_Stub(&PG_Inaccessible_Stub);

    assert(Flex_Used(g_gc.guarded) == 0);
    Free_Unmanaged_Flex(g_gc.guarded);
    g_gc.guarded = nullptr;

    assert(Flex_Used(g_gc.mark_stack) == 0);
    Free_Unmanaged_Flex(g_gc.mark_stack);
    g_gc.mark_stack = nullptr;

    // Can't use Free_Unmanaged_Flex() because g_gc.manuals couldn't be put
    // in the manuals list...Catch-22!  This free must happen after all
    // unmanaged Flexes have been freed.
    //
  #if RUNTIME_CHECKS
    if (Flex_Used(g_gc.manuals) != 0) {
        printf("g_gc.manuals not empty at shutdown!\n");
        Flex** leaked = Flex_Head(Flex*, g_gc.manuals);
        panic (*leaked);
    }
  #endif
    GC_Kill_Flex(g_gc.manuals);
    g_gc.manuals = nullptr;

  #if DEBUG_COLLECT_STATS
    g_gc.recycle_counter = 0;
    g_gc.recycled_stubs_total = 0;
    g_gc.recycled_stubs = 0;
  #endif

    g_gc.disabled = false;
}
