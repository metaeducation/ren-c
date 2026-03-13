//
//  file: %c-do.c
//  summary: "DO Evaluator Wrappers"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
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
// These are the "slightly more user-friendly" interfaces to the evaluator
// from %c-eval.c.  These routines will do the setup of the Reb_Frame state
// for you.
//
// Even "friendlier" interfaces are available as macros on top of these.
// See %sys-do.h for Eval_Any_List_At_Throws() and similar macros.
//

#include "sys-core.h"

//
//  Prep_Action_Level: C
//
// 1. If branch function argument isn't "meta" then we decay isotopes.
//    Do the decay test first to avoid needing to scan parameters unless it's
//    one of those cases.
//
//    (The theory here is that we're not throwing away any safety, as the
//     heavy branch process was usually just for the purposes of making the
//     branch trigger or not.  With that addressed, it's just inconvenient to
//     force functions to take ^ARG to get things like NULL.)
//
//         if ok [null] then (x -> [
//             ;
//             ; Why would we want to have to make it ^x, when we know any
//             ; nulls that triggered the branch would have been heavy forms?
//         ])
//
Result(None) Prep_Action_Level(
    Level* L,
    const Stable* action,
    Option(const Value*) with
){
    trap (
      Push_Action(L, action, PREFIX_0)
    );

  copy_exemplar_frame: {

    Mem_Copy(  // note: copies TRACK_FLAG_SHIELD_FROM_WRITES, cleared later
        L->u.action.arg,
        L->u.action.param,
        sizeof(Cell) * (L->u.action.key_tail - L->u.action.key)
    );

} fill_in_first_argument: {

    if (with) { attempt {
        const Param* param;
        Arg* arg = First_Unspecialized_Arg(&param, L);
        if (not arg) {
            require (
              Ensure_No_Failures_Including_In_Packs(unwrap with)
            );
            break;
        }

        Clear_Param_Shield_If_Tracking(arg);
        Value* copied = Copy_Cell(arg, unwrap with);  // do not decay [1]

        if (Parameter_Class(param) != PARAMCLASS_META) {
            require (
              Decay_If_Unstable(copied)
            );
        }
    }}

    return none;
}}


//
//  Push_Frame_Continuation: C
//
void Push_Frame_Continuation(
    Flags flags,
    const Stable* frame,  // may be antiform
    Option(const Value*) with
){
    possibly(with and Is_Failure(unwrap with));  // won't ignore failures

    require (
      Level* L = Make_End_Level(
        &Action_Executor,
        FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING) | flags
    ));
    Push_Level(nullptr, L);
    require (
      Prep_Action_Level(L, frame, with)
    );
}


//
//  Pushed_Continuation: C
//
//////////////////////////////////////////////////////////////////////////////
//
// 3. Things like CASE currently ask for a branch-based continuation on types
//    they haven't checked, but encounter via evaluation.  Hence we FAIL here
//    instead of crash()...but that suggests this should be narrowed to the
//    kinds of types branching permits.
//
bool Pushed_Continuation(
    Flags flags,  // FORCE_HEAVY_BRANCH, etc. for pushed levels
    Context* binding,  // before branch forces non-empty variadic call
    const Element* branch,  // *cannot* be the same as out
    Option(const Value*) with  // can be same as out or not GC-safe, may copy
){
    assert(
        not with
        or not Is_Api_Value(unwrap with)
    );

    if (Is_Frame(branch))  // antiform frames are legal
        goto handle_frame;

    if (Is_Group(branch)) {  // [2] for @(gr o up)
        assert(flags & LEVEL_FLAG_FORCE_HEAVY_BRANCH);  // branches
        require (
          Level* grouper = Make_Level_At_Core(
            &Group_Branch_Executor,  // evaluates to synthesize branch
            branch,
            binding,
            LEVEL_MASK_NONE  // Group_Branch_Executor() assumes heaviness
        ));
        if (with == nullptr)  // spare will hold the value
            assert(Is_Cell_Erased(Level_Spare(grouper)));
        else
            Copy_Cell(Level_Spare(grouper), unwrap with);
        Push_Level(nullptr, grouper);
        goto pushed_continuation;
    }

  panic_if_ignoring_with_is_dangerous: {

  // A GROUP! or a FRAME! branch *may* wind up being ^META-parameterized, so
  // that it can process a FAILURE! (or a pack with errors in it):
  //
  //     fail "test" else (^e -> [print "This doesn't panic..."])
  //
  //     fail "test" else (e -> [print "...but this panics"])
  //
  // However, if we get here there's no way there's parameterization.  In
  // these cases, we want to elevate any failures to a panic so they're not
  // dropped on the floor:
  //
  //     fail "test" else [print "This panics"]
  //
  //     fail "test" else 'this-panics-too

    if (with) {
      require (
        Ensure_No_Failures_Including_In_Packs(unwrap with)
      );
    }

} switch_on_sigil: {

    Type type = opt Type_Of(branch);

    switch (type) {
      case TYPE_BLOCK:  // plain execution
        binding = Derive_Binding(binding, branch);
        goto handle_list_with_adjusted_binding;

      handle_list_with_adjusted_binding: {
          Option(Element*) first = nullptr;
          require (
            Result(Feed*) feed = Prep_Array_Feed(
              Alloc_Feed(),
              first,  // no injection
              Cell_Array(branch),
              Series_Index(branch),
              binding,
              FEED_MASK_DEFAULT | (branch->header.bits & FEED_FLAG_CONST)
            )
        );

        require (
          Level* L = Make_Level(
            &Evaluator_Executor, feed, flags
        ));
        Push_Level(nullptr, L);

        goto pushed_continuation; }

      case TYPE_QUASIFORM: {
        require (
          Level* dummy = Make_End_Level(
            &Just_Use_Out_Executor,
            FLAG_STATE_BYTE(1)
          )
        );
        Push_Level(nullptr, dummy);

        Copy_Cell(Level_Out(dummy), branch);
        require (
          Unlift_Cell_No_Decay(Level_Out(dummy))
        );
        if (flags & LEVEL_FLAG_FORCE_HEAVY_BRANCH)
            Force_Cell_Heavy(Level_Out(dummy));
        goto pushed_continuation; }  /* should be "just use out" */

      case TYPE_METAFORM:
        break;  // define behavior!

      case TYPE_PINNED:
        break;

      case TYPE_TIED: {  // note: bound (use 'quoted to avoid binding)
        require (
          Level* dummy = Make_End_Level(
            &Just_Use_Out_Executor,
            FLAG_STATE_BYTE(1)
          )
        );
        Push_Level(nullptr, dummy);

        Clear_Cell_Sigil(
          Copy_Cell_May_Bind(Level_Out(dummy), branch, binding)
        );
        goto pushed_continuation; }  /* should be "just use out" */

      case TYPE_FENCE: {  // WRAP, then execute
        const Element* tail;
        const Element* at = List_At(&tail, branch);
        VarList* parent = nullptr;

        VarList* varlist = Make_Varlist_Detect_Managed(
            COLLECT_ONLY_SET_WORDS,
            HEART_OBJECT,  // !!! Presume object?
            at,
            tail,
            parent
        );
        Tweak_Link_Inherit_Bind(
            varlist,
            Derive_Binding(binding, branch)
        );
        binding = varlist;  // update binding
        goto handle_list_with_adjusted_binding; }

      case TYPE_CHAIN: {  // effectively REDUCE
        if (not Is_Get_Block(branch))
            panic ("GET-BLOCK! is only CHAIN branch currently working");

        require (
          Level* L = Make_End_Level(
            &Action_Executor,
            FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING)
        ));

        require (
          Push_Action(L, LIB(REDUCE), PREFIX_0)
        );

    copy_exemplar_frame: {

        Mem_Copy(  // note: copies TRACK_FLAG_SHIELD_FROM_WRITES, cleared later
            L->u.action.arg,
            L->u.action.param,
            sizeof(Cell) * (L->u.action.key_tail - L->u.action.key)
        );

    } fill_in_first_argument: {

        const Param* param;
        Arg* arg = First_Unspecialized_Arg(&param, L);
        Copy_Cell_May_Bind(arg, branch, binding);
        Tweak_Cell_Type_Matching_Heart(arg, HEART_BLOCK);  // :[1 + 2] => [3], not :[3]

        Push_Level(nullptr, L);
        goto pushed_continuation; }}

      case TYPE_PATH: {
        DECLARE_VALUE (temp);
        Length len = Sequence_Len(branch);
        Element *last = Copy_Sequence_At(temp, branch, len - 1);
        if (not Is_Space(last))
            panic ("Only terminal-slash PATH! can act as BRANCH!");

        require (
          Stable* got = Get_Var(temp, NO_STEPS, branch, binding)
        );
        if (not Is_Frame(got))
            panic ("Only FRAME! result from PATH! BRANCH allowed");

        branch = As_Element(got);
        goto handle_frame; }

      default:
        break;
    }

    if (Is_Quoted_Type(type)) {  // not bound (use $tied to get a binding)
        require (
          Level* dummy = Make_End_Level(
            &Just_Use_Out_Executor, FLAG_STATE_BYTE(1)
          )
        );
        Push_Level(nullptr, dummy);

        Unquote_Quoted_Cell(Copy_Cell(Level_Out(dummy), branch));
        goto pushed_continuation;  /* should be "just use out" */
    }

    panic (Error_Bad_Branch_Type_Raw());  // narrow input types? [3]

} handle_frame: { ///////////////////////////////////////////////////////////

    Push_Frame_Continuation(flags, branch, with);
    goto pushed_continuation;

} pushed_continuation: { /////////////////////////////////////////////////////

    return true;

/*} just_use_out_disabled: { //////////////////////////////////////////////////

  // !!! Temporarily disabled; it should always be possible to disable in
  // order to get notification in the Trampoline.

    assert(!"Temporarily disabled");
    return false;*/
}}
