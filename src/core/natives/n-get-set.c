//
//  file: %n-get-set.c
//  summary: "Native functions to GET (Paths, Chains, Tuples, Words...)"
//  section: natives
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
// Getting and Setting in Ren-C are far more nuanced than the "lookup word to
// direct Cell value" technique of historical Redbol.  Things like OBJECT!
// store lifted representations of their fields, which makes room for storing
// special states in the unlifted range.  These allow for things like ACTION!
// to represent a "getter" or "setter" for a field, while lifted ACTION! means
// an actual action is stored there.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. The generalized GET of an arbitrary variable may return an ERROR!
//    antiform as the value in OUT (vs. returning an Option(Error*) for the
//    Trap_XXX()).  This happens if you are doing an ordinary GET of a
//    TUPLE! and the last "step" in the path is not in an object:
//
//         >> obj: make object! [a: 1020]
//
//         >> obj.b
//         ** Error: b is not a field of the OBJECT!
//
//         >> try obj.b
//         == ~null~  ; antiform
//
//    However, the rules change with meta-representation, to where the only
//    way to get an ERROR! back in that case is if the field exists and holds
//    a lifted representation of an ERROR!.
//
//    (!!! It's not clear if the convenience of the raised error on a normal
//    TUPLE!-type assignment is a good idea or not.  This depends on how
//    often generalized variable fetching is performed where you don't know
//    if the variable is meta-represented or not, and might have different
//    meanings for unlifting an ERROR! vs. a missing field.  The convenience
//    of allowing TRY existed before meta-representation unlifting, so this
//    is an open question that arose.)
//
//    In the case of an assignment, the only way to get it to return a
//    raised ERROR! will be if the value being assigned was an ERROR!.  In
//    the case of a regular assignment the assignment itself will not be
//    performed and the error just passed through.  In a meta-assignment,
//    the assignment will be performed and the ERROR! passed through in its
//    unlifted form.
//
//  B. For convenience, assignments via WORD!: and TUPLE!: will pass thru
//     ERROR!, and skip the assign.  You only get the assignment of the error
//     antiform if you use ^WORD!: or ^TUPLE!: to indicate meta-assignment.
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


//
//  Set_Var_In_Scratch_To_Out: C
//
Result(None) Set_Var_In_Scratch_To_Out(
    Level* level_,  // OUT may be ERROR! antiform, see [A]
    Option(Element*) steps_out  // no GROUP!s if nulled
){
    Liftify(OUT);  // must be lifted to be taken literally in dual protocol
    Option(Error*) e = Trap_Tweak_Var_In_Scratch_With_Dual_Out(
        level_,
        steps_out
    );
    require (
      Unliftify_Undecayed(OUT)
    );
    if (e)
        return fail (unwrap e);

    return none;
}


//
//  Get_Var_In_Scratch_To_Out: C
//
Result(None) Get_Var_In_Scratch_To_Out(
    Level* level_,  // OUT may be ERROR! antiform, see [A]
    Option(Element*) steps_out  // no GROUP!s if nulled
){
    heeded (Init_Dual_Nulled_Pick_Signal(OUT));

    Option(Error*) e = Trap_Tweak_Var_In_Scratch_With_Dual_Out(
        level_,
        steps_out
    );
    if (e)
        return fail (unwrap e);

    if (Is_Error(OUT))  // !!! weird can't pick case
        return none;

    require (
      Unliftify_Undecayed(OUT)  // not unstable if wasn't ^META [1]
    );
    return none;
}


//
//  Trap_Get_Tuple_Maybe_Trash: C
//
// Convenience wrapper for getting tuples that errors on trash.
//
Result(None) Get_Tuple_Maybe_Trash(
    Sink(Stable) out,
    Option(Element*) steps_out,  // if NULL, then GROUP!s not legal
    const Element* tuple,
    Context* context
){
    require (
      Level* level_ = Make_End_Level(
        &Stepper_Executor,
        LEVEL_MASK_NONE | FLAG_STATE_BYTE(1) // rule for trampoline
    ));

    Sink(Value) atom_out = u_cast(Value*, out);
    Push_Level_Erase_Out_If_State_0(atom_out, level_);

    heeded (Derelativize(SCRATCH, tuple, context));
    heeded (Corrupt_Cell_If_Needful(SPARE));

    Option(Error*) e;
    Get_Var_In_Scratch_To_Out(level_, steps_out) except (e) {
        // need to drop level before returning
    }

    Drop_Level(level_);

    if (e)
        return fail (unwrap e);

    require (
      Decay_If_Unstable(atom_out)
    );
    return none;
}


//
//  Get_Var_Maybe_Trash: C
//
// This is a generalized service routine for getting variables--including
// PATH! and CHAIN!.
//
// 1. Refinements will be specialized.  So if you know you have a path in
//    your hand--and all you plan to do with the result after getting it is
//    to execute it--then use Trap_Get_Path_Push_Refinements() instead of
//    this function, and then let the Action_Executor() use the refinements
//    on the stack directly.  That avoids making an intermediate action.
//
Result(None) Get_Var_Maybe_Trash(
    Sink(Value) out,
    Option(Element*) steps_out,  // if NULL, then GROUP!s not legal
    const Element* var,
    Context* context
){
    assert(var != cast(Cell*, out));
    assert(steps_out != out);  // Legal for SET, not for GET

    if (Is_Chain(var) or Is_Path(var)) {
        StackIndex base = TOP_INDEX;

        DECLARE_VALUE (safe);
        Push_Lifeguard(safe);

        Option(Error*) error;
        if (Is_Chain(var)) {
            Get_Chain_Push_Refinements(
                out, safe, var, context
            ) except (error) {
                // need to drop level before returning
            }
        } else {
            require (
              Level* level_ = Make_End_Level(
                &Stepper_Executor,
                LEVEL_MASK_NONE | FLAG_STATE_BYTE(1)  // rule for trampoline
            ));

            Push_Level_Erase_Out_If_State_0(out, level_);

            heeded (Derelativize(SCRATCH, var, context));
            heeded (Corrupt_Cell_If_Needful(SPARE));

            Get_Path_Push_Refinements(level_) except (error) {
                // need to drop level before returning
            }

            Drop_Level(level_);
        }
        Drop_Lifeguard(safe);

        if (error)
            return fail (unwrap error);

        assert(Is_Action(Known_Stable(out)));

        if (TOP_INDEX != base) {
            DECLARE_STABLE (action);
            Move_Cell(action, Known_Stable(out));
            Deactivate_If_Action(action);

            Option(Element*) def = nullptr;  // !!! g_empty_block doesn't work?
            bool threw = Specialize_Action_Throws(  // costly, try to avoid [1]
                out, action, def, base
            );
            assert(not threw);  // can only throw if `def`
            UNUSED(threw);
        }

        if (steps_out and steps_out != GROUPS_OK)
            Init_Quasar(unwrap steps_out);  // !!! What to return?

        return none;
    }

    require (
      Level* level_ = Make_End_Level(
        &Stepper_Executor,
        LEVEL_MASK_NONE | FLAG_STATE_BYTE(1)  // rule for trampoline
    ));

    Push_Level_Erase_Out_If_State_0(out, level_);  // flushes corruption

    heeded (Derelativize(SCRATCH, var, context));
    heeded (Corrupt_Cell_If_Needful(SPARE));

    Option(Error*) error;
    Get_Var_In_Scratch_To_Out(level_, steps_out) except (error) {
        // need to drop level before returning
    }

    Drop_Level(level_);

    if (error)
        return fail (unwrap error);

    return none;
}


//
//  Get_Chain_Push_Refinements: C
//
Result(Stable*) Get_Chain_Push_Refinements(
    Sink(Stable) out,
    Sink(Stable) spare,
    const Element* chain,
    Context* context
){
    UNUSED(spare);  // !!! was used for GROUP!-in-CHAIN, feature removed

    assert(not Try_Get_Sequence_Singleheart(chain));  // don't use w/these

    const Element* tail;
    const Element* head = List_At(&tail, chain);

    Context* derived = Derive_Binding(context, chain);

    // The first item must resolve to an action.

    Value* atom_out = u_cast(Value*, out);

    if (Is_Group(head)) {  // historical Rebol didn't allow group at head
        if (Eval_Value_Throws(atom_out, head, derived))
            panic (Error_No_Catch_For_Throw(TOP_LEVEL));

        require (
          Decay_If_Unstable(atom_out)
        );
    }
    else if (Is_Tuple(head)) {  // .member-function:refinement is legal
        DECLARE_ELEMENT (steps);
        require (  // must panic on error
          Get_Tuple_Maybe_Trash(
            out, steps, head, derived
        ));
        if (Is_Trash(out))
            panic (Error_Bad_Word_Get(head, out));
    }
    else if (Is_Word(head)) {
        require (  // must panic on error
          Get_Word(out, head, derived));
    }
    else
        panic (head);  // what else could it have been?

    ++head;

    if (Is_Action(out))
        NOOP;  // it's good
    else if (Is_Antiform(out))
        return fail (Error_Bad_Antiform(out));
    else if (Is_Frame(out))
        Actionify(out);
    else
        panic ("Head of CHAIN! did not evaluate to an ACTION!");

    // We push the remainder of the chain in *reverse order* as words to act
    // as refinements to the function.  The action execution machinery will
    // decide if they are valid or not.
    //
    const Element* at = tail - 1;

    for (; at != head - 1; --at) {
        assert(not Is_Space(at));  // no internal blanks

        if (Is_Word(at)) {
            Init_Pushed_Refinement(PUSH(), Word_Symbol(at));
        }
        else
            panic (at);  // non-WORD! being considered for "dialected calls"
    }

    return out;
}


//
//  Get_Path_Push_Refinements: C
//
// This is a high-level Get_Path() which only returns ACTION! in OUT.
//
// Long-term it should be able to do things like turn not/even/ into a CASCADE
// of functions.  That's not actually super hard to do, it just hasn't been
// implemented yet.  Right now a PATH! can only have two parts: a left side
// (a WORD! or a TUPLE!) and a right side (a WORD! or a CHAIN!)
//
Result(None) Get_Path_Push_Refinements(Level* level_)
{
  #if NEEDFUL_DOES_CORRUPTIONS  // confirm caller pre-corrupted spare [1]
    assert(Not_Cell_Readable(SPARE));
  #endif

    StackIndex base = TOP_INDEX;

    Option(Error*) e = SUCCESS;

  #if RUNTIME_CHECKS
    Protect_Cell(SCRATCH);  // (common exit path undoes this protect)
  #endif

    const Element* path = Known_Element(SCRATCH);
    assert(Is_Path(path));

    if (not Sequence_Has_Pointer(path)) {  // byte compressed
        e = Error_Bad_Value(path);  // no meaning to 1.2.3/ or /1.2.3 etc.
        goto return_error;
    }

 detect_path_compression: {

    const Base* payload1 = CELL_PAYLOAD_1(path);
    if (Is_Base_A_Cell(payload1)) {
        // pairing, but "Listlike", so List_At() will work on it
    }
    else switch (Stub_Flavor(cast(Flex*, payload1))) {
      case FLAVOR_SYMBOL: {  // `/a` or `a/`
        Element* spare = Copy_Cell(SPARE, path);
        KIND_BYTE(spare) = TYPE_WORD;

        Get_Any_Word_Maybe_Trash(OUT, spare, SPECIFIED) except (e) {
            goto return_error;
        }

        goto ensure_out_is_action; }

      case FLAVOR_SOURCE:
        break;

      default:
        crash (path);
    }

} handle_listlike_path: {

    const Element* tail;
    const Element* at = List_At(&tail, path);

    Context* binding = Sequence_Binding(path);

    if (Is_Space(at)) {  // leading slash means execute (but we're GET-ing)
        ++at;
        assert(not Is_Space(at));  // two blanks would be `/` as WORD!
    }

    Sink(Stable) spare_left = SPARE;
    if (Is_Group(at)) {
        if (Eval_Value_Throws(SPARE, at, binding)) {
            e = Error_No_Catch_For_Throw(TOP_LEVEL);
            goto return_error;
        }
        require (
          Decay_If_Unstable(SPARE)
        );
    }
    else if (Is_Tuple(at)) {
        DECLARE_ELEMENT (steps);
        Get_Tuple_Maybe_Trash(
            spare_left, steps, at, binding
        ) except (e) {
            goto return_error;
        }
    }
    else if (Is_Word(at)) {
        Get_Word(spare_left, at, binding) except (e) {
            goto return_error;
        }
    }
    else if (Is_Chain(at)) {
        if ((at + 1 != tail) and not Is_Space(at + 1)) {
            e = Error_User("CHAIN! can only be last item in a path right now");
            goto return_error;
        }
        Get_Chain_Push_Refinements(
            u_cast(Init(Stable), OUT),
            SPARE,
            cast(Element*, at),
            Derive_Binding(binding, at)
        )
        except (e) {
            goto return_error;
        }

        goto return_success;  // chain must resolve to an action (?!)
    }
    else {
        e = Error_Bad_Value(at);  // what else could it have been?
        goto return_error;
    }

    ++at;

    if (at == tail or Is_Space(at)) {
        Copy_Cell(OUT, spare_left);
        goto ensure_out_is_action;
    }

    if (at + 1 != tail and not Is_Space(at + 1))
        return fail ("PATH! can only be two items max at this time");

    // When we see `lib/append` for instance, we want to pick APPEND out of
    // LIB and make sure it is an action.
    //
    if (not Any_Context(spare_left)) {
        e = Error_Bad_Value(path);
        goto return_error;
    }

  handle_context_on_left_of_at: {

    if (Is_Chain(at)) {  // lib/append:dup
        Sink(Stable) out = OUT;

        Get_Chain_Push_Refinements(
            out,
            SPARE,  // scratch space (Cell_Context() extracts)
            at,
            Cell_Context(spare_left)  // need to find head of chain in object
        )
        except (e) {
            goto return_error;
        }

        goto return_success;  // chain must resolve to an action (?!)
    }

    possibly(Is_Frame(spare_left));
    Quotify(Known_Element(spare_left));  // lifted protocol

    Copy_Cell(PUSH(), at);

    require (
      Level* sub = Make_End_Level(&Action_Executor, LEVEL_MASK_NONE)
    );

    e = Trap_Call_Pick_Refresh_Dual_In_Spare(TOP_LEVEL, sub, TOP_INDEX);
    if (e)
        goto return_error;

    Drop_Level(sub);

    DROP();

    Copy_Cell(OUT, SPARE);
    require (
      Unliftify_Undecayed(OUT)
    );

    goto ensure_out_is_action;

}} ensure_out_is_action: { ///////////////////////////////////////////////////

    Stable* out = Known_Stable(OUT);

    if (Is_Action(out))
        goto return_success;

    if (Is_Frame(out)) {
        Actionify(out);
        goto return_success;
    }

    e = Error_User("PATH! must retrieve an action or frame");
    goto return_error;

} return_error: { ////////////////////////////////////////////////////////////

    assert(e);
    Drop_Data_Stack_To(base);
    goto finalize_and_return;

} return_success: { //////////////////////////////////////////////////////////

  // Currently there are no success modes that return ERROR! antiforms (as
  // described by [A] at top of file.)  Would you ever TRY a PATH! and not
  // mean "try the result of the function invoked by the path"?  e.g. TRY
  // on a PATH! that ends in slash?

    assert(Is_Action(Known_Stable(OUT)));

    assert(not e);
    goto finalize_and_return;

} finalize_and_return: { /////////////////////////////////////////////////////

    assert(LEVEL == TOP_LEVEL);

    Corrupt_Cell_If_Needful(SPARE);

  #if RUNTIME_CHECKS
    Unprotect_Cell(SCRATCH);
  #endif

    if (e)
        return fail (unwrap e);

    return none;
}}


//
//  Get_Any_Word_Maybe_Trash: C
//
Result(None) Get_Any_Word_Maybe_Trash(
    Sink(Value) out,
    const Element* word,  // heeds Sigil (^WORD! will UNLIFT)
    Context* context
){
    assert(Any_Word(word));

    switch (opt Sigil_Of(word)) {
      case SIGIL_0:
        break;

      case SIGIL_META:
        break;

      case SIGIL_PIN:
      case SIGIL_TIE:
        return fail ("Cannot GET a @PINNED or $TIED variable yet");
    }

    return Get_Var_Maybe_Trash(out, NO_STEPS, word, context);
}


//
//  Get_Word: C
//
Result(Stable*) Get_Word(
    Sink(Stable) out,
    const Element* word,
    Context* context
){
    assert(Is_Word(word));  // no sigil, can't give back unstable form

    Sink(Value) atom_out = u_cast(Value*, out);

    trap (
      Get_Any_Word_Maybe_Trash(atom_out, word, context)
    );
    if (Is_Error(atom_out))  // !!! bad pick
        return fail (Cell_Error(atom_out));

    if (Is_Trash(out))
        return fail (Error_Bad_Word_Get(word, out));

    return out;
}




//
//  Get_Var: C
//
// May generate specializations for paths.  See Get_Var_Maybe_Trash()
//
Result(Stable*) Get_Var(
    Sink(Stable) out,
    Option(Element*) steps_out,  // if nullptr, then GROUP!s not legal
    const Element* var,
    Context* context
){
    Sink(Value) atom_out = u_cast(Value*, out);

    trap (
      Get_Var_Maybe_Trash(atom_out, steps_out, var, context)
    );

    require (
      Decay_If_Unstable(atom_out)
    );
    if (Is_Trash(out))
        return fail (Error_Bad_Word_Get(var, out));

    return out;
}



// TWEAK handles GROUP!s inside of a TUPLE! if you ask it to.  But it doesn't
// work at the higher level of `set $(first [word1 word2]) value`...it's a
// narrower function for handling single WORD!/TUPLE! targets.  Higher-level
// behaviors like SET of a BLOCK! are layered on top of it, and that includes
// abstracting the operation to getting or setting of a GROUP! target.
//
// 1. We check what the GROUP! synthesized against the actual RETURN: [...]
//    parameterization of GET or SET.  So long as a GROUP! didn't synthesize
//    another GROUP!, we allow any other thing from that list.
//
static Result(bool) Recalculate_Group_Arg_Vanishes(Level* level_, SymId id)
{
    INCLUDE_PARAMS_OF_GET;  // TARGET types must be compatible with SET

    Element* target = Element_ARG(TARGET);
    assert(Is_Group(target));

    USED(ARG(GROUPS));
    USED(ARG(STEPS));

   // !!! At the moment, the generic Set_Var() mechanics aren't written to
   // handle GROUP!s.  But it probably should, since it handles groups that
   // are nested under TUPLE! and such.  Review.

    if (Eval_Any_List_At_Throws(OUT, target, SPECIFIED))
        panic (Error_No_Catch_For_Throw(LEVEL));

    if (Is_Ghost_Or_Void(OUT))
        return true;

    require (
      Stable* out = Decay_If_Unstable(OUT)
    );

    if (Is_Group(out))
        return fail ("GROUP! result from SET/GET of GROUP! target not legal");

    const Stable* action = Lib_Stable(id);  // different TARGETS for GET/SET
    ParamList* paramlist = Phase_Paramlist(Frame_Phase(action));
    Param* param = Phase_Param(paramlist, PARAM_INDEX(TARGET));

    heeded (Corrupt_Cell_If_Needful(SCRATCH));
    heeded (Corrupt_Cell_If_Needful(SPARE));

    require (
      bool check = Typecheck_Coerce(LEVEL, param, out, false)
    );

    if (not check)
        return fail (out);

    Copy_Cell(target, Known_Element(out));  // update ARG(TARGET)
    Corrupt_Cell_If_Needful(OUT);

    return false;
}


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
//              word! tuple!
//              ^word! ^tuple!
//              group! "If :GROUPS, retrigger SET based on evaluated value"
//              @block!
//          ]
//      ^value "Will be decayed if TARGET not BLOCK! or metavariables"
//          [any-value? error!]
//      :groups "Allow GROUP! Evaluations"
//      :steps "Return evaluation steps for reproducible access"
//  ]
//
DECLARE_NATIVE(SET)
//
// SET is really just a version of TWEAK that passes a lifted argument, but
// also wants to make its return value match the assignment value.  This means
// it has to unlift value.
//
// 1. SET of a BLOCK! should expose the implementation of the multi-return
//    mechanics used by SET-BLOCK!.  That will take some refactoring... not
//    an urgent priority, but it needs to be done.
{
    INCLUDE_PARAMS_OF_TWEAK;  // !!! must have compatible frame

    Element* target = Element_ARG(TARGET);

    Value* v = Atom_ARG(DUAL);  // not a dual yet (we have to lift it...)

    bool groups_ok = Bool_ARG(GROUPS);

    USED(ARG(STEPS));  // TWEAK heeds this

    if (Is_Group(target)) {  // Group before error passthru [B]
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

    Value* dual = Liftify(v);

    Option(Bounce) b = Irreducible_Bounce(
        LEVEL,
        Apply_Cfunc(NATIVE_CFUNC(TWEAK), LEVEL)
    );
    if (b)
        return unwrap b;  // keep bouncing while we couldn't get OUT as answer

    Element* lifted = Known_Element(dual);
    assert(Any_Lifted(lifted));

    return UNLIFT(lifted);
}


//
//  get: native [
//
//  "Gets a variable (for dual band states, see TWEAK)"
//
//      return: [
//          any-value?             "will be decayed if not ^META input"
//          ~[@block! any-value?]~ "Give :STEPS as well as the result value"
//          error!                 "Passthru even it skips the assign"
//      ]
//      target "Word or tuple or path, or calculated sequence steps (from GET)"
//          [
//              <opt-out>
//              word! tuple!   "Unstable fetches error"
//              ^word! ^tuple! "Do not decay unstable antiform results"
//              path!   "Specialize action specified by path"
//              group!  "If :GROUPS, retrigger GET based on evaluated value"
//              @block!
//          ]
//      {dual-ignore}  ; for frame compatibility with TWEAK [1]
//      :groups "Allow GROUP! Evaluations"
//      :steps "Return evaluation steps for reproducible access"
//  ]
//
DECLARE_NATIVE(GET)
//
// GET is really just a version of TWEAK that passes null, and unlifts the
// return result.
//
// 1. GET delegates to TWEAK which reuses the same Level; put locals wherever
//    TWEAK has parameters or locals that GET doesn't have.
//
// 2. Conveniently, FRAME! locals default to NULL, so the DUAL parameter is
//    the right signal for GET to pass to TWEAK to mean GET.
{
    INCLUDE_PARAMS_OF_TWEAK;  // !!! must have compatible frame [1]

    Element* target = Element_ARG(TARGET);

    assert(Is_Nulled(ARG(DUAL)));  // "value" slot (SET uses, GET does not) [2]
    USED(ARG(DUAL));

    bool groups_ok = Bool_ARG(GROUPS);

    USED(ARG(STEPS));  // TWEAK heeds this

    if (Is_Group(target)) {
        if (not groups_ok)
            return fail ("GET of GROUP! target without :GROUPS not allowed");

        require (
          bool vanished = Recalculate_Group_Arg_Vanishes(LEVEL, SYM_GET)
        );
        if (vanished)
            return NULLED;
    }

    Option(Bounce) b = Irreducible_Bounce(
        LEVEL,
        Apply_Cfunc(NATIVE_CFUNC(TWEAK), LEVEL)
    );
    if (b)
        return unwrap b;  // keep bouncing while we couldn't get OUT as answer

    if (Is_Error(OUT))
        return OUT;  // weird can't pick case, see [A]

    if (not Any_Lifted(OUT))
        panic ("GET of DUAL_0 state, code to resolve this not in GET yet");

    require (
      Unliftify_Undecayed(OUT)
    );
    return OUT;
}


//
//  defined?: native [
//
//  "Check to see if a variable is defined (unset is considered defined)"
//
//      return: [logic?]
//      target [word! tuple! path!]
//  ]
//
DECLARE_NATIVE(DEFINED_Q)
//
// !!! Exactly what the scope of "not defined" here is a bit unclear: should
// something like (defined? $(1).foo) panic, or should it quietly consider
// picking a field out of an INTEGER! to count as "undefined?"
{
    INCLUDE_PARAMS_OF_DEFINED_Q;

    Get_Var_Maybe_Trash(
        OUT, NO_STEPS, Element_ARG(TARGET), SPECIFIED
    ) except (Error* e) {
        UNUSED(e);
        return LOGIC(false);
    }

    possibly(Is_Error(OUT));  // (get meta $obj.field) can be defined as ERROR!
    return LOGIC(true);
}
