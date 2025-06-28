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
//         if ok [null] then x -> [
//             ;
//             ; Why would we want to have to make it ^x, when we know any
//             ; nulls that triggered the branch would have been heavy forms?
//         ]
//
void Prep_Action_Level(
    Level* L,
    const Value* action,
    Option(const Atom*) with
){
    Push_Action(L, action, PREFIX_0);

    const Key* key = L->u.action.key;
    const Param* param = L->u.action.param;
    Atom* arg = L->u.action.arg;
    for (; key != L->u.action.key_tail; ++key, ++param, ++arg) {
        if (Is_Specialized(param))
            Blit_Param_Drop_Mark(arg, param);
        else {
            Erase_Cell(arg);
            if (Get_Parameter_Flag(param, REFINEMENT))
                Init_Nulled(arg);
            else
                Init_Tripwire(arg);
        }
    }

    if (with) do {
        arg = First_Unspecialized_Arg(&param, L);
        if (not arg)
            break;

        Copy_Cell(arg, unwrap with);  // do not decay [1]

        if (Parameter_Class(param) != PARAMCLASS_META)
            Decay_If_Unstable(arg);
        break;
    } while (0);
}


//
//  Push_Frame_Continuation: C
//
void Push_Frame_Continuation(
    Init(Atom) out,
    Flags flags,
    const Value* frame,  // may be antiform
    Option(const Atom*) with
){
    Level* L = Make_End_Level(
        &Action_Executor,
        FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING) | flags
    );
    Prep_Action_Level(L, frame, with);
    Push_Level_Erase_Out_If_State_0(out, L);
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
    Need(Atom*) out,  // not Sink (would corrupt, but with can be same as out)
    Flags flags,  // LEVEL_FLAG_FORCE_HEAVY_NULLS, etc. for pushed levels
    Context* binding,  // before branch forces non-empty variadic call
    const Value* branch,  // *cannot* be the same as out
    Option(const Atom*) with  // can be same as out or not GC-safe, may copy
){
    assert(u_cast(const Atom*, branch) != out);
    assert(
        not with
        or (unwrap with) == out
        or not Is_Atom_Api_Value(unwrap with)
    );

    if (Is_Action(branch))  // antiform frames are legal
        goto handle_action;

    if (false) {  // !!! checked for VOID, but that's unstable antiform now
        if (with) {
            Copy_Cell(out, unwrap with);
            goto just_use_out;
        }
        panic ("Branch has no default value to give with void");
    }

    if (Is_Antiform(branch))  // no other antiforms can be branches
        panic (Error_Bad_Antiform(branch));

    if (Is_Pinned_Form_Of(GROUP, branch)) {  // [2] for GET-GROUP!
        assert(flags & LEVEL_FLAG_FORCE_HEAVY_NULLS);  // needed for trick
        Level* grouper = Make_Level_At_Core(
            &The_Group_Branch_Executor,  // evaluates to synthesize branch
            c_cast(Element*, branch),
            binding,
            (flags & (~ LEVEL_FLAG_FORCE_HEAVY_NULLS))
        );
        if (with == nullptr)  // spare will hold the value
            assert(Is_Cell_Erased(Level_Spare(grouper)));
        else
            Copy_Cell(Level_Spare(grouper), unwrap with);
        Push_Level_Erase_Out_If_State_0(out, grouper);
        goto pushed_continuation;
    }

  switch_on_sigil: {

    Option(Sigil) sigil = Sigil_Of(c_cast(Element*, branch));
    if (sigil) {
        switch (unwrap sigil) {
          case SIGIL_META:
            break;  // define behavior!

          case SIGIL_PIN:
            break;

          case SIGIL_TIE:
            Plainify(Derelativize(out, c_cast(Element*, branch), binding));
            goto just_use_out;

          default:
            assert(false);
        }
    }
    else switch (Type_Of(branch)) {
      case TYPE_QUOTED:
        Unquotify(Derelativize(out, c_cast(Element*, branch), binding));
        goto just_use_out;

      case TYPE_QUASIFORM:
        if (
            Is_Lifted_Null(c_cast(Element*, branch))
            and (flags & LEVEL_FLAG_FORCE_HEAVY_NULLS)
        ){
            Init_Heavy_Null(out);
        }
        else {
            Derelativize(out, c_cast(Element*, branch), binding);
            Unliftify_Undecayed(out);
        }
        goto just_use_out;

      case TYPE_BLOCK: {
        Level* L = Make_Level_At_Core(
            &Evaluator_Executor, c_cast(Element*, branch), binding, flags
        );
        Init_Void(Evaluator_Primed_Cell(L));

        Push_Level_Erase_Out_If_State_0(out, L);
        goto pushed_continuation; }

      case TYPE_CHAIN: {  // effectively REDUCE
        if (not Is_Get_Block(branch))
            panic ("GET-BLOCK! is only CHAIN branch currently working");

        Level* L = Make_End_Level(
            &Action_Executor,
            FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING)
        );

        Push_Action(L, LIB(REDUCE), PREFIX_0);

        const Key* key = L->u.action.key;
        const Param* param = L->u.action.param;
        Atom* arg = L->u.action.arg;
        for (; key != L->u.action.key_tail; ++key, ++param, ++arg) {
            if (Is_Specialized(param))
                Blit_Param_Drop_Mark(arg, param);
            else {
                Erase_Cell(arg);
                Init_Tripwire(arg);
            }
        }

        arg = First_Unspecialized_Arg(&param, L);
        Derelativize(arg, c_cast(Element*, branch), binding);
        KIND_BYTE(arg) = TYPE_BLOCK;  // :[1 + 2] => [3], not :[3]

        Push_Level_Erase_Out_If_State_0(out, L);
        goto pushed_continuation; }

      case TYPE_FRAME:
        goto handle_action;

      default:
        break;
    }

    panic (Error_Bad_Branch_Type_Raw());  // narrow input types? [3]

} handle_action: { ///////////////////////////////////////////////////////////

    Push_Frame_Continuation(out, flags, branch, with);
    goto pushed_continuation;

} pushed_continuation: { /////////////////////////////////////////////////////

    return true;

} just_use_out: { ////////////////////////////////////////////////////////////

    return false;
}}
