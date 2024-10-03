//
//  File: %c-do.c
//  Summary: "DO Evaluator Wrappers"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
//     force functions to be meta to get things like NULL.)
//
//         if ok [null] then x -> [
//             ;
//             ; Why would we want to have to make it ^x, when we know any
//             ; nulls that triggered the branch would have been heavy forms?
//         ]
//
void Prep_Action_Level(
    Level* L,
    const Cell* action,
    Option(const Atom*) with
){
    Push_Action(L, VAL_ACTION(action), Cell_Frame_Coupling(action));
    Begin_Prefix_Action(L, VAL_FRAME_LABEL(action));

    const Key* key = L->u.action.key;
    const Param* param = L->u.action.param;
    Atom* arg = L->u.action.arg;
    for (; key != L->u.action.key_tail; ++key, ++param, ++arg) {
        Erase_Cell(arg);
        Copy_Cell(arg, param);
        assert(Is_Stable(arg));
    }

    if (with) do {
        arg = First_Unspecialized_Arg(&param, L);
        if (not arg)
            break;

        Copy_Cell(arg, unwrap with);  // do not decay [1]

        if (Cell_ParamClass(param) == PARAMCLASS_META)
            Meta_Quotify(arg);
        else
            Decay_If_Unstable(arg);
        break;
    } while (0);
}


//
//  Push_Frame_Continuation: C
//
void Push_Frame_Continuation(
    Atom* out,
    Flags flags,
    const Value* frame,  // may be antiform
    Option(const Atom*) with,
    bool copy_frame
){
    if (Is_Frame_Details(frame)) {
        UNUSED(copy_frame);  // !!! have to copy (details are locked)

        Level* L = Make_End_Level(
            &Action_Executor,
            FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING) | flags
        );
        Prep_Action_Level(L, frame, with);
        Push_Level(out, L);
        return;
    }

    UNUSED(with);  // !!! no handling at present

    if (IS_FRAME_PHASED(frame))  // see REDO for tail-call recursion
        fail ("Use REDO to restart a running FRAME! (not DO)");

    if (Get_Subclass_Flag(
        VARLIST,
        Varlist_Array(Cell_Varlist(frame)),
        FRAME_HAS_BEEN_INVOKED
    )){
        fail (Error_Stale_Frame_Raw());
    }

    // We want to run a COPY of the FRAME!, not the frame itself

    StackIndex lowest_stackindex = TOP_INDEX;  // for refinements

    VarList* varlist;
    if (copy_frame) {
        varlist = Make_Varlist_For_Action(
            frame,  // being used here as input (e.g. the ACTION!)
            lowest_stackindex,  // will weave in any refinements pushed
            nullptr  // no binder needed, not running any code
        );
    } else
        varlist = Cell_Varlist(frame);

    Level* L = Make_End_Level(
        &Action_Executor,
        FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING) | flags
    );

    L->varlist = Varlist_Array(varlist);
    L->rootvar = Rootvar_Of_Varlist(varlist);
    Tweak_Varlist_Keysource(varlist, L);

    assert(Level_Phase(L) == CTX_FRAME_PHASE(varlist));
    Tweak_Level_Coupling(L, Cell_Frame_Coupling(frame));

    L->u.action.original = Level_Phase(L);

    Begin_Prefix_Action(L, VAL_FRAME_LABEL(frame));

    Push_Level(out, L);
}


//
//  Pushed_Continuation: C
//
//////////////////////////////////////////////////////////////////////////////
//
// 3. Things like CASE currently ask for a branch-based continuation on types
//    they haven't checked, but encounter via evaluation.  Hence we FAIL here
//    instead of panic()...but that suggests this should be narrowed to the
//    kinds of types branching permits.
//
bool Pushed_Continuation(
    Atom* out,
    Flags flags,  // LEVEL_FLAG_BRANCH, etc. for pushed levels
    Context* binding,  // before branch forces non-empty variadic call
    const Value* branch,
    Option(const Atom*) with  // can be same as out or not GC-safe, may copy
){
    assert(branch != out);  // it's legal for `with` to be the same as out
    assert(not with or (unwrap with) == out or not Is_Api_Value(unwrap with));

    if (Is_Action(branch))  // antiform frames are legal
        goto handle_action;

    if (Is_Void(branch)) {
        if (with) {
            Copy_Cell(out, unwrap with);
            goto just_use_out;
        }
        fail ("Branch has no default value to give with void");
    }

    if (Is_Antiform(branch))  // no other antiforms can be branches
        fail (Error_Bad_Antiform(branch));

    if (Is_The_Group(branch)) {  // [2] for GET-GROUP!
        assert(flags & LEVEL_FLAG_BRANCH);  // needed for trick
        Level* grouper = Make_Level_At_Core(
            &The_Group_Branch_Executor,  // evaluates to synthesize branch
            branch,
            binding,
            (flags & (~ LEVEL_FLAG_BRANCH))
                | FLAG_STATE_BYTE(ST_GROUP_BRANCH_ENTRY_DONT_ERASE_OUT)
        );
        if (with == nullptr)
            Freshen_Cell(out);
        else
            Copy_Cell(out, unwrap with);  // need lifetime preserved
        Push_Level(out, grouper);
        goto pushed_continuation;
    }

    switch (VAL_TYPE(branch)) {
      case REB_BLANK:
        if (flags & LEVEL_FLAG_BRANCH)
            Init_Heavy_Null(out);
        else
            Init_Nulled(out);
        goto just_use_out;

      case REB_QUOTED:
        Derelativize(out, c_cast(Element*, branch), binding);
        Unquotify(out, 1);
        if (Is_Nulled(out) and (flags & LEVEL_FLAG_BRANCH))
            Init_Heavy_Null(out);
        goto just_use_out;

      case REB_META_BLOCK:
      case REB_BLOCK: {
        Init_Void(Alloc_Evaluator_Primed_Result());
        Level* L = Make_Level_At_Core(
            &Evaluator_Executor, branch, binding, flags
        );
        if (Cell_Heart_Unchecked(branch) == REB_META_BLOCK) {
            Set_Level_Flag(L, META_RESULT);
            Set_Level_Flag(L, RAISED_RESULT_OK);
        }

        Push_Level(out, L);
        goto pushed_continuation; }  // trampoline handles LEVEL_FLAG_BRANCH

      case REB_CHAIN: {  // effectively REDUCE
        if (not Is_Get_Block(branch))
            fail ("GET-BLOCK! is only CHAIN branch currently working");

        Level* L = Make_End_Level(
            &Action_Executor,
            FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING)
        );

        const Value* action = Lib(REDUCE);
        Push_Action(L, VAL_ACTION(action), Cell_Frame_Coupling(action));
        Begin_Prefix_Action(L, VAL_FRAME_LABEL(action));

        const Key* key = L->u.action.key;
        const Param* param = L->u.action.param;
        Atom* arg = L->u.action.arg;
        for (; key != L->u.action.key_tail; ++key, ++param, ++arg) {
            Erase_Cell(arg);
            Copy_Cell(arg, param);
        }

        arg = First_Unspecialized_Arg(&param, L);
        Derelativize(arg, c_cast(Element*, branch), binding);
        HEART_BYTE(arg) = REB_BLOCK;  // :[1 + 2] => [3], not :[3]

        Push_Level(out, L);
        goto pushed_continuation; }

      handle_action:
      case REB_FRAME: {
        bool copy_frame = true;  // !!! dicate based on freeze status?
        Push_Frame_Continuation(out, flags, branch, with, copy_frame);
        goto pushed_continuation; }

      default:
        break;
    }

    fail (Error_Bad_Branch_Type_Raw());  // narrow input types? [3]

  pushed_continuation:
    return true;

  just_use_out:
    return false;
}
