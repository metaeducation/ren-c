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


#define PLUG_FLAG_HAS_DATA_STACK    STUB_SUBCLASS_FLAG_24
#define PLUG_FLAG_HAS_MOLD          STUB_SUBCLASS_FLAG_25


//
//  Unplug_Stack: C
//
// Pulls a stack out into an independent list of levels, subtracting out the
// base level as a baseline.  The resulting level stack will end in nullptr
// (instead of BOTTOM_LEVEL).  You can then replug with Replug_Stack, e.g.
// the following should be a no-op:
//
//      Level* base = level_->prior->prior;
//      assert(level_->prior != nullptr);
//
//      Unplug_Stack(SPARE, level_, base);
//
//      assert(level_->prior == nullptr);
//      assert(TOP_LEVEL == base);
//
//      Replug_Stack(level_, TOP_LEVEL, SPARE);
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
    Level* L,  // level to unplug (currently can only unplug topmost level)
    Level* base  // base level to unplug relative to
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

        if (temp->out == base->out) {
            //
            // Reassign to mark the output as something randomly bad, but
            // still GC safe.  When the stack gets patched back in, it will
            // be recognized and reset to the new base's out.
            //
            temp->out = m_cast(Value*, Lib(BLANK));
        }
        else if (temp->out == &base->spare) {
            temp->out = m_cast(Value*, Lib(NULL));
        }

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
    Flags flags = FLAG_FLAVOR(PLUG);  // be agnostic, to be generic!

    if (String_Size(g_mold.buffer) > base->baseline.mold_buf_size) {
        flags |= PLUG_FLAG_HAS_MOLD;
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
        flags |= PLUG_FLAG_HAS_DATA_STACK;

    if (flags == FLAG_FLAVOR(PLUG))
        Init_Block(plug, EMPTY_ARRAY);
    else
        Init_Block(
            plug,
            Pop_Managed_Source_From_Stack(base->baseline.stack_base)
        );

    g_ts.top_level = base;
}


//
//  Replug_Stack: C
//
// This reverses the process of Unplug_Stack, patching a stack onto a new
// base location.
//
// 1. The top level for base at unplug time may have targeted any output cell.
//    That output is likely gone (an argument fulfillment for a now-finished
//    function, an API cell that was released, etc.)  But more levels than
//    that could have inherited the same L->out.
//
//    Unplug_Stack() put a bogus pointer to the read-only Lib(BLANK) cell,
//    it's good enough to be GC safe and also distinct.  Anywhere we see that,
//    replace with the output this new base wants to write its output to.
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
void Replug_Stack(Level* L, Level* base, Value* plug) {
    assert(base == TOP_LEVEL);  // currently can only plug atop topmost frame

    Level* temp = L;
    while (true) {
        if (temp->out == Lib(BLANK))  // replace output placeholder [1]
            temp->out = base->out;
        else if (temp->out == Lib(NULL))
            temp->out = cast(Value*, &base->spare);

        temp->baseline.stack_base += base->baseline.stack_base;  // [2]

        if (temp->prior == nullptr)
            break;
        temp = temp->prior;
       /* assert(LEVEL_STATE_BYTE(temp) != 0); */ // must be continuation (why?)
    }

    temp->prior = base;

    // Now add in all the residual elements from the plug to global buffers
    // like the mold buffer and data stack.

    assert(Is_Block(plug));  // restore data stack from plug's block
    assert(VAL_INDEX(plug) == 0);  // could store some number (?)

    if (Cell_Array(plug) == EMPTY_ARRAY)
        goto finished;

  blockscope {

    Array* array = Cell_Array_Known_Mutable(plug);
    Value* item = Flex_Tail(Value, array);

    if (Get_Flavor_Flag(PLUG, array, HAS_MOLD)) {  // restore mold from plug
        --item;
        assert(Is_Text(item));
        assert(VAL_INDEX(item) == 0);
        Append_Any_Utf8(g_mold.buffer, item);
    }

    if (Get_Flavor_Flag(PLUG, array, HAS_DATA_STACK)) {
        Value* stacked = Flex_Head(Value, array);
        for (; stacked != item; ++stacked)
            Move_Cell(PUSH(), stacked);
    }

} finished: {

    Init_Unreadable(plug);  // no longer needed, let it be GC'd

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
