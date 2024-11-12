//
//  File: %c-state.c
//  Summary: "Memoization of Status of Global Interpreter State"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2022 Ren-C Open Source Contributors
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
// See remarks in %sys-state.h
//
//=//// NOTES ////////////////////////////////////////////////////////////=//
//
// * WORK IN PROGRESS: "stackless" features are adding demands to tighten up
//   the adjustment and rollback of global state.
//


#include "sys-core.h"


//
//  Snap_State: C
//
// **Note:** Modifying this routine likely means a necessary modification to
// `Assert_State_Balanced_Debug()`.
//
void Snap_State(struct Reb_State *s)
{
    s->stack_base = TOP_INDEX;

    s->guarded_len = Flex_Dynamic_Used(g_gc.guarded);

    s->manuals_len = Flex_Dynamic_Used(g_gc.manuals);
    s->mold_buf_len = String_Len(g_mold.buffer);
    s->mold_buf_size = String_Dynamic_Size(g_mold.buffer);
    s->mold_loop_tail = Flex_Dynamic_Used(g_mold.stack);

  #if RUNTIME_CHECKS
    s->num_evars_outstanding = g_num_evars_outstanding;
  #endif
}


//
//  Rollback_Globals_To_State: C
//
// This routine is used by things like Drop_Level() when a fail occurs, to
// automatically restore the state of globals to how they were at the time
// the passed-in state was Snap_State()'d.
//
void Rollback_Globals_To_State(struct Reb_State *s)
{
    Drop_Data_Stack_To(s->stack_base);

    // Free any manual Flexes that were extant (e.g. Make_Flex() nodes
    // which weren't created with NODE_FLAG_MANAGED and were not transitioned
    // into the managed state).  This will include any Flexes used as backing
    // store for rebAlloc() calls.
    //
    assert(Flex_Dynamic_Used(g_gc.manuals) >= s->manuals_len);
    while (Flex_Dynamic_Used(g_gc.manuals) != s->manuals_len) {
        Free_Unmanaged_Flex(
            *Flex_At(
                Flex*,
                g_gc.manuals,
                Flex_Dynamic_Used(g_gc.manuals) - 1
            )
        );  // ^-- Free_Unmanaged_Flex() will decrement Flex_Used()
    }

    Set_Flex_Len(g_gc.guarded, s->guarded_len);

    Term_String_Len_Size(g_mold.buffer, s->mold_buf_len, s->mold_buf_size);

  #if RUNTIME_CHECKS
    //
    // Because reporting errors in the actual Push_Mold process leads to
    // recursion, this debug flag helps make it clearer what happens if
    // that does happen...and can land on the right comment.  If there's
    // a fail of some kind, the flag for the warning needs to be cleared.
    //
    g_mold.currently_pushing = false;
  #endif

    Set_Flex_Len(g_mold.stack, s->mold_loop_tail);

  #if RUNTIME_CHECKS
    g_num_evars_outstanding = s->num_evars_outstanding;
  #endif
}


#define DATASTACK_FLAG_HAS_PUSHED_CELLS     STUB_SUBCLASS_FLAG_24
#define DATASTACK_FLAG_HAS_MOLD             STUB_SUBCLASS_FLAG_25
#define DATASTACK_FLAG_HAS_SPARE            STUB_SUBCLASS_FLAG_26
#define DATASTACK_FLAG_HAS_SCRATCH          STUB_SUBCLASS_FLAG_27

#define LINK_SuspendedLevel_TYPE       Level*
#define HAS_LINK_SuspendedLevel        FLAVOR_DATASTACK

#define SPARE_PROXY     x_cast(Atom*, Lib(BLANK))
#define SCRATCH_PROXY   x_cast(Atom*, Lib(NULL))


// Depending on whether they have state to restore (mold buffers, data stacks,
// or spare and scratch cells), plugs may have an array of data.  It's
// actually not super uncommon for them not to need state...so there's a
// compressed form that just holds the Level directly.
//
static Level* Level_Of_Plug(const Value* plug) {
    if (Handle_Holds_Node(plug))
        return LINK(SuspendedLevel, c_cast(Array*, Cell_Handle_Node(plug)));

    return Cell_Handle_Pointer(Level, plug);
}

// Plugs hold a detached stack of Levels, which if they don't get plugged back
// into the stack with Replug_Stack() need to be freed.
//
// !!! This raises new questions about the generalized destruction of a Level
// that is not plugged into the running Level stack, and during garbage
// collection where the legal operations are more limited.  It's very much
// a work in progress.
//
static void Clean_Plug_Handle(const RebolValue* plug) {
    Level* L = Level_Of_Plug(plug);
    DECLARE_ATOM (raised);
    Init_Error(raised, Cell_Error(g_error_done_enumerating));  // !!! hack
    Raisify(raised);
    while (L != nullptr) {
        Level* prior = L->prior;
        L->out = raised;  // make API handles free as if there were an error
        if (Is_Action_Level(L))
            Drop_Action(L);
        Drop_Level_Core(L);
        L = prior;
    }
}


//
//  Unplug_Stack: C
//
// Pulls a stack out into an independent list of levels, subtracting out the
// base level as a baseline.  The resulting level stack will end in nullptr
// (instead of BOTTOM_LEVEL).  You can then replug with Replug_Stack(), e.g.
// the following should be a no-op:
//
//      Level* base = level_->prior->prior;
//
//      Unplug_Stack(SPARE, base, level_);
//
//      assert(level_->prior->prior == nullptr);  // detached
//      assert(TOP_LEVEL == base);
//
//      Replug_Stack(level_, TOP_LEVEL);
//      assert(level_->prior->prior == base);
//
// This is used by something like YIELD, which unplugs the stack of Levels
// all the way up to the GENERATOR (or YIELDER) that it's running under...
// restoring the stack so the generator is back on top and able to return
// a value.  Any global state (like mold buffer bits or the data stack) which
// may not be at the same point when the replug happens is moved into a
// cell managed by the caller.  This is referred to as a "plug".
//
void Unplug_Stack(
    Value* plug,  // cell where global state differentials can be stored
    Level* base,  // base level to unplug relative to
    Level* L  // level to unplug (currently can only unplug topmost level)
){
    assert(L == TOP_LEVEL);

    Level* temp = L;
    while (true) {
        if (Get_Level_Flag(temp, ROOT_LEVEL)) {
            //
            // !!! Handling errors in stackless is still a work in progress;
            // avoid confusion on this case by asserting for now.
            //
            assert(!"Can't yield across non-continuation-level");
            fail ("Cannot yield across level that's not a continuation");
        }

        assert(temp->out != base->out);  // can't guarantee restoration!
        if (temp->out == Level_Spare(base))
            temp->out = SPARE_PROXY;
        else if (temp->out == Level_Scratch(base))
            temp->out = SCRATCH_PROXY;

        // We make the baseline stack pointers in each level relative to the
        // base level, with that level as if it were 0.  When the level
        // gets plugged in again, we'll add the new base's stackindex back in.
        //
        // !!! This may confuse a fail() if it expects to climb the stack and
        // see all the L->baseline.stack_base be sane.  But as far as interim
        // state is concerned, there's no good number to put here...leaving it
        // as it was would be wrong too.  This might suggest an EVAL_FLAG for
        // "don't believe the stack_base".  Tricky.
        //
        temp->baseline.stack_base -= base->baseline.stack_base;

        if (temp->prior == base) {
            //
            // The level below the base was not fulfilling an argument, it
            // should be writing into the base's out cell.  But when the
            // base goes off the stack, that cell will most likely be
            // gone.  We'll have to point it at the new base's out cell
            // when we plug it back in.  Also we have to set it to something
            // legal to mark in GC as the cell will go stale.
            //
            assert(LEVEL_STATE_BYTE(temp->prior) != 0);  // must be continuation

            // !!! This is true for YIELD's relationship to the YIELDER, but
            // why would it be generically the case?
            //
            /* assert(temp->out == Lib(BLANK)); */  // should have matched base

            temp->prior = nullptr;  // show where the fragment of stack ends
            break;
        }

        temp = temp->prior;

        if (temp == TOP_LEVEL)  // "alive", but couldn't find in the stack walk
            fail ("Cannot yield to a generator that is suspended");

        assert(LEVEL_STATE_BYTE(temp) != 0);  // must be a continuation
    }

    // If any data stack has been accrued, we capture it into an array.  We
    // will have to re-push the values when the level is plugged back in.
    //
    // !!! We do not technically need to manage this array...just keep the
    // values in it alive during GC.  But for simplicity, we keep it in a
    // value cell, and manage it.
    //
    Flags flags = 0;

    if (Not_Cell_Erased(&base->spare)) {
        if (Is_Cell_Readable(&base->spare))
            Copy_Meta_Cell(PUSH(), &base->spare);
        else
            Init_Trash(PUSH());
        flags |= DATASTACK_FLAG_HAS_SPARE;
    }

    if (Not_Cell_Erased(&base->scratch)) {
        if (Is_Cell_Readable(&base->scratch))
            Copy_Meta_Cell(PUSH(), &base->scratch);
        else
            Init_Trash(PUSH());

        flags |= DATASTACK_FLAG_HAS_SCRATCH;
    }

    if (String_Size(g_mold.buffer) > base->baseline.mold_buf_size) {
        flags |= DATASTACK_FLAG_HAS_MOLD;
        Init_Text(
            PUSH(),
            Pop_Molded_String_Core(
                g_mold.buffer,
                base->baseline.mold_buf_size,
                base->baseline.mold_buf_len
            )
        );
    }

    if (TOP_INDEX > base->baseline.stack_base)  // do first (other flags push)
        flags |= DATASTACK_FLAG_HAS_PUSHED_CELLS;

    if (not flags) {
        Init_Handle_Cdata_Managed(plug, L, 1, &Clean_Plug_Handle);
    }
    else {
        Array* a = Pop_Stack_Values_Core(
            flags | FLAG_FLAVOR(DATASTACK) | NODE_FLAG_MANAGED,
            base->baseline.stack_base
        );
        LINK(SuspendedLevel, a) = L;
        Init_Handle_Node_Managed(plug, a, &Clean_Plug_Handle);
    }
    assert(L == Level_Of_Plug(plug));

    g_ts.top_level = base;
}


//
//  Replug_Stack: C
//
// This reverses the process of Unplug_Stack, patching a stack onto a new
// base location.
//
// 1. The previous base Level probably was freed, which means that if it had
//    given out pointers to its SPARE or SCRATCH cells that serve as the
//    OUT pointer for any nested Levels those pointers would have gone bad.
//    We redirect the pointers to the new base's SPARE and SCRATCH.
//
// 2. Unplug made the stack_base be relative to 0.  We're going to restore the
//    values that were between the base and the unplugged level on the data
//    stack.  But that means we have to touch up the `stack_base` pointers as
//    well in the levels.
//
// 3. We chain the stack that was underneath the old base to the new base so
//    that it now considers this base the parent.  We also update the outputs
//    of that sublevel to match the output of the current level (see assert in
//    Unplug_Stack() proving sublevel had same L->out).
//
void Replug_Stack(Level* base, Value* plug) {
    assert(base == TOP_LEVEL);  // currently can only plug atop topmost frame

    Level* L = Level_Of_Plug(plug);

    Level* temp = L;
    while (true) {
        if (temp->out == SPARE_PROXY)  // replace output placeholder [1]
            temp->out = Level_Spare(base);
        else if (temp->out == SCRATCH_PROXY)
            temp->out = Level_Scratch(base);

        temp->baseline.stack_base += base->baseline.stack_base;  // [2]

        if (temp->prior == nullptr)
            break;
        temp = temp->prior;
       /* assert(LEVEL_STATE_BYTE(temp) != 0); */ // must be continuation (why?)
    }

    temp->prior = base;

    // Now add in all the residual elements from the plug to global buffers
    // like the mold buffer and data stack.

    if (not Handle_Holds_Node(plug))  // no array of additional information
        goto finished;

  blockscope {

    Array* array = x_cast(Array*, Cell_Handle_Node(plug));
    assert(Stub_Flavor(array) == FLAVOR_DATASTACK);

    Value* item = Flex_Tail(Value, array);

    if (Get_Flavor_Flag(DATASTACK, array, HAS_MOLD)) {  // restore mold
        --item;
        assert(Is_Text(item));
        assert(VAL_INDEX(item) == 0);
        Append_Any_Utf8(g_mold.buffer, item);
    }

    if (Get_Flavor_Flag(DATASTACK, array, HAS_SCRATCH)) {
        --item;
        if (Is_Trash(item))
            Init_Unreadable(Level_Scratch(base));
        else {
            Copy_Cell(Level_Scratch(base), item);
            Meta_Unquotify_Undecayed(Level_Scratch(base));
        }
    }
    else
        Erase_Cell(Level_Scratch(base));

    if (Get_Flavor_Flag(DATASTACK, array, HAS_SPARE)) {
        --item;
        if (Is_Trash(item))
            Init_Unreadable(Level_Spare(base));
        else {
            Copy_Cell(&base->spare, item);
            Meta_Unquotify_Undecayed(Level_Spare(base));
        }
    }
    else
        Erase_Cell(&base->spare);

    if (Get_Flavor_Flag(DATASTACK, array, HAS_PUSHED_CELLS)) {
        Value* stacked = Flex_Head(Value, array);
        for (; stacked != item; ++stacked)
            Move_Cell(PUSH(), stacked);
    }

    Decay_Stub(array);  // didn't technically need to be managed...

} finished: {

    Stub* stub = Extract_Cell_Handle_Stub(plug);
    Set_Stub_Unreadable(stub);  // indicate decayed, but skip cleaner
    GC_Kill_Stub(stub);
    Init_Unreadable(plug);  // no longer needed

    g_ts.top_level = L;  // make the jump deeper into the stack official...
}}


#if RUNTIME_CHECKS

//
//  Assert_State_Balanced_Debug: C
//
// Check that all variables in `state` have returned to what they were at
// the time of snapshot.
//
void Assert_State_Balanced_Debug(
    struct Reb_State *s,
    const char *file,
    int line
){
    if (s->stack_base != TOP_INDEX) {
        printf(
            "PUSH()x%d without DROP()\n",
            cast(int, TOP_INDEX - s->stack_base)
        );
        panic_at (nullptr, file, line);
    }

    if (s->guarded_len != Flex_Used(g_gc.guarded)) {
        printf(
            "Push_Lifeguard()x%d without Drop_Lifeguard()\n",
            cast(int, Flex_Used(g_gc.guarded) - s->guarded_len)
        );
        Node* guarded = *Flex_At(
            Node*,
            g_gc.guarded,
            Flex_Used(g_gc.guarded) - 1
        );
        panic_at (guarded, file, line);
    }

    // !!! Note that this inherits a test that uses g_gc.manuals->content.xxx
    // instead of Flex_Used().  The idea being that although some Flex
    // are able to fit in the Stub node, the g_gc.manuals wouldn't ever
    // pay for that check because it would always be known not to.  Review
    // this in general for things that may not need "Flex" overhead,
    // e.g. a contiguous pointer stack.
    //
    if (s->manuals_len > Flex_Used(g_gc.manuals)) {
        //
        // Note: Should this ever actually happen, panic() on the Flex won't
        // do any real good in helping debug it.  You'll probably need
        // additional checks in Manage_Flex() and Free_Unmanaged_Flex()
        // that check against the caller's manuals_len.
        //
        panic_at ("manual Flex freed outside checkpoint", file, line);
    }
    else if (s->manuals_len < Flex_Used(g_gc.manuals)) {
        printf(
            "Make_Flex()x%d w/o Free_Unmanaged_Flex() or Manage_Flex()\n",
            cast(int, Flex_Used(g_gc.manuals) - s->manuals_len)
        );
        Flex* manual = *(Flex_At(
            Flex*,
            g_gc.manuals,
            Flex_Used(g_gc.manuals) - 1
        ));
        panic_at (manual, file, line);
    }

    assert(s->mold_buf_len == String_Len(g_mold.buffer));
    assert(s->mold_buf_size == String_Size(g_mold.buffer));
    assert(s->mold_loop_tail == Flex_Used(g_mold.stack));

  #if RUNTIME_CHECKS
    assert(s->num_evars_outstanding == g_num_evars_outstanding);
  #endif
}

#endif
