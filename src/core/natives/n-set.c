//
//  file: %n-set.c
//  summary: "Native functions to SET (Words, Tuples, Blocks...)"
//  section: natives
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2025 Ren-C Open Source Contributors
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
// Getting and Setting in Ren-C are far more nuanced than the "lookup word to
// direct Cell value" technique of historical Redbol.  Things like OBJECT!
// store "dual states", allowing for things like FRAME! to represent a
// "getter" or "setter" for a field.  It's important for all code that does
// reads and writes to go through the SET and GET layer, which is built on
// top of "TWEAK" that speaks in lifted/dual values.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. In the case of non-^META assignment, the only way to get it to return a
//    raised ERROR! will be if the value being assigned was an ERROR!--and
//    the assignment will not be performed.  In a meta-assignment, the assign
//    will happen and the error will be passed through.  (You may have to
//    IGNORE the result to suppress escalation to PANIC.)
//
//     This raises questions about what should happen here:
//
//         >> eval [try (print "printing" $word): fail "what happens?"]
//         ; does the message print or not?
//         == ~null~  ; antiform
//
//     The same issues apply whether you are in the evaluator or the native.
//     It would seem that left-to-right evaluation order would make people
//     think that it would print first, so that's the direction we're going.
//

#include "sys-core.h"


// When a SET-BLOCK! is being processed for multi-returns, it may encounter
// leading-SPACE chains as in ([foo :bar]: 10).  Once the work of extracting
// the real variable from the path is done and pushed to the stack, this bit
// is used to record that the variable was optional.  This makes it easier
// for the phase after the right hand side is evaluated--vs. making it pick
// apart the path again.
//
#define CELL_FLAG_STACK_HINT_OPTIONAL  CELL_FLAG_HINT


//
//   Push_Set_Block_Instructions_To_Stack_Throws: C
//
// The evaluator treats SET-BLOCK! specially as a means for implementing
// multiple return values.  It unpacks antiform blocks into components.
//
//     >> pack [1 2]
//     == ~['1 '2]~  ; anti
//
//     >> [a b]: pack [1 2]
//     == 1
//
//     >> a
//     == 1
//
//     >> b
//     == 2
//
// If a component is optional (e.g. the pack is too short to provide it), it
// can be marked with a leading colon to get null instead of unset:
//
//     >> [a b]: pack [1]
//     == ~['1]~
//
//     >> b
//     ** PANIC: b is unset
//
//     >> [a :b]: pack [1]
//     == 1
//
//     >> b
//     == ~null~  ; anti
//
// It supports `_` in slots you don't want to name, and `^` in slots you don't
// want to name but also want to tolerate unstable antiforms.  It evaluates
// GROUP!s to produce variable names, and also allows FENCE! to {circle} which
// result you want to be the overall result of the expression (defaults to
// passing through the entire pack).
//
// 1. Empty SET-BLOCK follows the same rules as any other block receiving
//    more values than it wants: it ignores the extra values, and passes
//    through the original assignment.  That's technically *all* potential
//    states that might come up on the right hand side--including ERROR!
//    The behavior naturally "falls out" of the implementation.
//
Result(bool) Push_Set_Block_Instructions_To_Stack_Throws(
    Level* const L,
    Context* binding
){
    USE_LEVEL_SHORTHANDS (L);

    Element* scratch = Known_Element(SCRATCH);

    assert(STATE == ST_STEPPER_SET_BLOCK and Is_Block(scratch));

    possibly(Series_Len_At(scratch) == 0);  // pass through everything [1]

    const Element* tail;
    const Element* check = List_At(&tail, scratch);
    Context* check_binding = Derive_Binding(binding, scratch);

  // we've extracted the array at and tail, can reuse scratch now

    Option(StackIndex) circled = 0;

  push_variables_loop: for (; check != tail; ++check) {

  // We pre-process the SET-BLOCK! first and collect the variables to write on
  // the stack.  (It makes more sense for any GROUP!s in the set-block to be
  // evaluated on the left before the right.)
  //
  // !!! Should the block be locked while advancement happens?  It wouldn't
  // need to be since everything is on the stack before code is run on the
  // right...but it might reduce confusion.

    if (Is_Quoted(check))
        panic ("QUOTED? not currently permitted in SET-BLOCK!s");

    bool circle_this;

    if (not Is_Fence(check)) {  // not "circled"
        circle_this = false;
        scratch = Derelativize(SCRATCH, check, check_binding);  // same heart
        goto circle_detection_finished;
    }

  handle_fence_in_set_block: {  // "circled"

  // By default, the evaluation product of a SET-BLOCK expression is what the
  // right hand side was (e.g. an entire pack).  But {xxx} indicates a desire
  // to pick a specific unpacked result as the return.
  //
  //     >> [a b]: pack [1 2]
  //     == ~['1 '2]~  ; anti
  //
  //     >> [a {b}]: pack [1 2]
  //     == 2

    if (circled)
        panic ("Can only {Circle} one multi-return result");

    Length len_at = Series_Len_At(check);
    if (len_at == 1) {
        scratch = Derelativize(
            SCRATCH,
            List_Item_At(check),
            check_binding
        );
    }
    else  // !!! should {} be a synonym for {_}?
        panic ("{Circle} only one element in multi-return");

    circle_this = true;

} circle_detection_finished: {

    bool is_optional;
    bool is_action;

    if (not Is_Chain(scratch)) {
        is_optional = false;
        goto optional_detection_finished;
    }

  handle_chain_in_set_block: {

    Option(SingleHeart) single;
    if (
        not (single = Try_Get_Sequence_Singleheart(scratch))
        or not Singleheart_Has_Leading_Space(unwrap single)
    ){
        panic (
            "Only leading SPACE CHAIN! in SET BLOCK! dialect"
        );
    }
    assume (
      Unsingleheart_Sequence(scratch)
    );
    is_optional = true;

} optional_detection_finished: {

    if (not Is_Path(scratch)) {
        is_action = false;
        goto path_detection_finished;
    }

    Option(SingleHeart) single;
    if (
        not (single = Try_Get_Sequence_Singleheart(scratch))
        or not Singleheart_Has_Leading_Space(unwrap single)
    ){
        panic (
            "Only leading SPACE PATH! in SET BLOCK! dialect"
        );
    }
    assume (
      Unsingleheart_Sequence(scratch)
    );
    is_action = true;

} path_detection_finished: {

    if (
        Is_Group(scratch)
        or Is_Pinned_Form_Of(GROUP, scratch)
        or Is_Meta_Form_Of(GROUP, scratch)
    ){
        if (Eval_Any_List_At_Throws(SPARE, scratch, SPECIFIED)) {
            Drop_Data_Stack_To(STACK_BASE);
            return true;
        }
        if (Is_Void(SPARE) and Is_Group(scratch)) {
            Init_Quasar(PUSH());  // [(void)]: ... pass thru
        }
        else {
            require (
              Stable* spare = Decay_If_Unstable(SPARE)
            );
            if (Is_Antiform(spare))
                panic (Error_Bad_Antiform(spare));

            if (Is_Pinned_Form_Of(GROUP, scratch)) {
                Pinify_Cell(Known_Element(spare));  // add @ decoration
            }
            else if (Is_Meta_Form_Of(GROUP, scratch)) {
                Metafy_Cell(Known_Element(spare));  // add ^ decoration
            }
            else
                assert(Is_Group(scratch));
            Copy_Cell(PUSH(), spare);
        }
    }
    else
        Copy_Cell(PUSH(), scratch);

    Corrupt_Cell_If_Needful(SCRATCH);  // look at stack top now

    if (is_optional)  // so next phase won't worry about leading slash
        Set_Cell_Flag(TOP, STACK_HINT_OPTIONAL);

    if (is_action)
        Set_Cell_Flag(TOP, SCRATCH_VAR_NOTE_ONLY_ACTION);

    if (circle_this)
        circled = TOP_INDEX;

    if (Is_Metaform_Space(TOP) or Is_Meta_Form_Of(WORD, TOP))  // meta-assign
        continue;

    if (Is_Word(TOP) or Is_Tuple(TOP))
        continue;

    if (Is_Space(TOP))
        continue;

    panic (
        "SET-BLOCK! items are (@THE, ^META) WORD/TUPLE or _ or ^]"
    );

}}} set_block_eval_right_hand_side: {

    level_->u.eval.stackindex_circled = circled;  // remember it

    return false;
}}


//
//  Set_Block_From_Instructions_On_Stack_To_Out: C
//
Result(None) Set_Block_From_Instructions_On_Stack_To_Out(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

  // 1. On errors we don't assign variables, yet pass the error through.  That
  // permits code like this to work:
  //
  //     rescue [[a b]: transcode "1&aa"]

    if (Is_Error(OUT))  // don't assign variables [1]
        goto set_block_drop_stack_and_continue;

 set_block_result_not_error: {

  // 1. The OUT cell is used by the Set_Var() mechanics as the place to write
  //    from.  Free it up so there's more space to work.  (This means we have
  //    to stop our variable enumeration right before the top of the stack.)
  //
  // 2. We enumerate from left to right in the SET-BLOCK!, with the "main"
  //    being the first assigned to any variables.  This has the benefit that
  //    if any of the multi-returns were marked as "circled" then the
  //    overwrite of the returned OUT for the whole evaluation will happen
  //    *after* the original OUT was captured into any desired variable.

    Copy_Cell(PUSH(), OUT);  // free up OUT cell [1]

    const Source* pack_array;  // needs GC guarding when OUT overwritten
    const Element* pack_at_lifted;  // individual pack block items are lifted
    const Element* pack_tail;

    if (Is_Pack(OUT)) {  // antiform block
        pack_at_lifted = List_At(&pack_tail, OUT);

        pack_array = Cell_Array(OUT);
        Push_Lifeguard(pack_array);
    }
    else {  // single item
        Copy_Lifted_Cell(SPARE, OUT);
        pack_at_lifted = cast(Element*, SPARE);
        pack_tail = cast(Element*, SPARE) + 1;  // not a valid cell

        pack_array = nullptr;
    }

    StackIndex stackindex_var = STACK_BASE + 1;  // [2]
    Option(StackIndex) circled = level_->u.eval.stackindex_circled;

  next_pack_item: {

    if (stackindex_var == (TOP_INDEX + 1) - 1)  // -1 accounts for pushed OUT
        goto set_block_finalize_and_drop_stack;

    bool is_optional = Get_Cell_Flag(
        Data_Stack_Cell_At(stackindex_var),
        STACK_HINT_OPTIONAL
    );

    bool is_action = Get_Cell_Flag(
        Data_Stack_Cell_At(stackindex_var),
        SCRATCH_VAR_NOTE_ONLY_ACTION
    );

    Sink(Element) var = SCRATCH;  // stable location, safe across SET
    Copy_Cell(var, Data_Stack_At(Element, stackindex_var));
    if (is_action) {
        assert(var == &level_->scratch);
        heeded (Set_Cell_Flag(var, SCRATCH_VAR_NOTE_ONLY_ACTION));
    }

    assert(LIFT_BYTE(var) == NOQUOTE_2);

    if (pack_at_lifted == pack_tail) {  // no more multi-return values
        if (not is_optional) {
            if (circled == stackindex_var)
                panic ("Circled item has no multi-return value to use");

            Init_Ghost_For_End(OUT);

            heeded (Corrupt_Cell_If_Needful(SPARE));
            require (
              Set_Var_In_Scratch_To_Out(LEVEL, NO_STEPS)
            );
            goto skip_circled_check;  // we checked it wasn't circled
        }

        Init_Nulled(OUT);
    }
    else {
        Copy_Cell(OUT, pack_at_lifted);
        require (
          Unliftify_Undecayed(OUT)  // unlift for output...
        );
    }

    if (Is_Metaform_Space(var))
        goto circled_check;

    if (Is_Meta_Form_Of(WORD, var)) {
        heeded (Corrupt_Cell_If_Needful(SPARE));
        require (
          Set_Var_In_Scratch_To_Out(LEVEL, NO_STEPS)
        );
        goto circled_check;  // ...because we may have circled this
    }

    if (Is_Error(OUT))  // don't pass thru errors if not ^ sigil
        panic (Cell_Error(OUT));

    require (
      Decay_If_Unstable(OUT)
    );

    if (Is_Space(var))
        goto circled_check;

    if (Is_Word(var) or Is_Tuple(var) or Is_Pinned_Form_Of(WORD, var)) {
        heeded (Corrupt_Cell_If_Needful(SPARE));
        require (
          Set_Var_In_Scratch_To_Out(LEVEL, GROUPS_OK)
        );
    }
    else
        assert(false);

    goto circled_check;

} circled_check: { ///////////////////////////////////////////////////////////

  // Note: no circling passes through the original PACK!

    if (circled == stackindex_var)
        Copy_Cell(TOP_ATOM, OUT);

} skip_circled_check: { //////////////////////////////////////////////////////

    ++stackindex_var;
    if (pack_at_lifted != pack_tail)
        ++pack_at_lifted;
    goto next_pack_item;

} set_block_finalize_and_drop_stack: {

  // 1. At the start of the process we pushed the meta-value of whatever the
  //    right side of the SET_BLOCK! was (as long as it wasn't an ERROR!).
  //    OUT gets overwritten each time we write a variable, so we have to
  //    restore it to make the overall SET-BLOCK! process match the right hand
  //    side.  (This value is overwritten by a circled value, so it may not
  //    actually be the original right hand side.)

    if (pack_array)
        Drop_Lifeguard(pack_array);

    Move_Value(OUT, TOP_ATOM);  // restore OUT (or circled) from stack [1]

}} set_block_drop_stack_and_continue: {

  // We've just changed the values of variables, and these variables might be
  // coming up next.  Consider:
  //
  //     304 = [a]: test 1020
  //     a = 304
  //
  // The `a` was fetched and found to not be infix, and in the process
  // its value was known.  But then we assigned that a with a new value
  // in the implementation of SET-BLOCK! here, so, it's incorrect.

    Invalidate_Gotten(&L->feed->gotten);  // L_next_gotten_raw

    Drop_Data_Stack_To(STACK_BASE);  // drop writeback variables

    Corrupt_Cell_If_Needful(SPARE);  // we trashed it

    return none;
}}


//
//  set: native [
//
//  "Sets a variable to specified value (for dual band states, see TWEAK)"
//
//      return: [
//          any-value?   "Same value as input (not decayed)"
//          <null>       "If VALUE is NULL, or if <opt-out> of target "
//          error!       "Passed thru from input if not a meta-assign"
//      ]
//      target "Word or tuple, or calculated sequence steps (from GET)"
//          [
//              <opt-out>
//              _ word! tuple! "Decayed assignment"
//              ^ ^word! ^tuple! "Undecayed assignment"
//              group! "If :GROUPS, retrigger SET based on evaluated value"
//              block! "Use SET-BLOCK dialect, same as ([...]: ...)"
//              @block!
//          ]
//      ^value "Will be decayed if TARGET not BLOCK! or metavariables"
//          [any-value? pack! error!]
//      :groups "Allow GROUP! Evaluations"
//      :steps "Return evaluation steps for reproducible access"
//  ]
//
DECLARE_NATIVE(SET)
//
// SET is really just a version of TWEAK that passes a lifted argument, but
// also wants to make its return value match the assignment value.  This means
// it has to unlift value.
{
    INCLUDE_PARAMS_OF_TWEAK;  // !!! must have compatible frame

    Element* target = Element_ARG(TARGET);

    Value* v = Atom_ARG(DUAL);  // not a dual yet (we have to lift it...)

    bool groups_ok = Bool_ARG(GROUPS);

    USED(ARG(STEPS));  // TWEAK heeds this

    if (Is_Group(target)) {  // Group before error passthru [A]
        if (not groups_ok)
            return fail ("SET of GROUP! target without :GROUPS not allowed");

        require (
          bool vanished = Recalculate_Group_Arg_Vanishes(LEVEL, SYM_SET)
        );
        if (vanished)
            return NULLED;
    }

    if (Is_Error(v) and not Is_Metaform(target))
        return COPY(v);  // error passthru [B]

    if (Is_Block(target)) {
        Copy_Cell(SCRATCH, target);
        Copy_Cell(OUT, v);

        STATE = ST_STEPPER_SET_BLOCK;

        require (
          Push_Set_Block_Instructions_To_Stack_Throws(
            LEVEL,
            SPECIFIED
          )
        );
        require (
          Set_Block_From_Instructions_On_Stack_To_Out(LEVEL)
        );
        return OUT;
    }

    if (Is_Space(target)) {
        require (
            Decay_If_Unstable(v)
        );
        return COPY(v);
    }

    if (Is_Metaform_Space(target))
        return COPY(v);

  delegate_to_tweak: {

  // 1. We don't want to decay the value if we're going to pass it to TWEAK
  //    because (for instance) unstable ACTION-PACK! antiforms need to be
  //    passed to tweak so it knows an action assignment is "unsurprising".
  //    Also, GHOST! and VOID are used to unset variables even if they
  //    are not metaforms.

    if (Is_Word(target) or Is_Tuple(target))
        DONT(require(Decay_If_Unstable(v)));  // TWEAK needs undecayed [1]
    else {
        assert(
            Is_Meta_Form_Of(WORD, target)
            or Is_Meta_Form_Of(TUPLE, target)
        );
    }

    Value* dual = Liftify(v);  // make dual for TWEAK [2]

    Option(Bounce) b = Irreducible_Bounce(
        LEVEL,
        Apply_Cfunc(NATIVE_CFUNC(TWEAK), LEVEL)
    );
    if (b)
        return unwrap b;  // keep bouncing while we couldn't get OUT as answer

    Element* lifted = Known_Element(dual);
    assert(Any_Lifted(lifted));

    return UNLIFT(lifted);  // unlift TWEAK dual result to normal [2]
}}
