//
//  file: %n-get.c
//  summary: "Native functions to GET (Paths, Chains, Tuples, Words...)"
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

#include "sys-core.h"


//
//  Save_Level_Scratch_Spare: C
//
StateByte Save_Level_Scratch_Spare(Level* level_) {
    StateByte saved_state = STATE;

    assert(not Is_Cell_Poisoned(SCRATCH));
    assert(not Is_Cell_Poisoned(SPARE));

    Blit_Cell(PUSH(), SCRATCH);
    Blit_Cell(PUSH(), SPARE);

    return saved_state;
}


//
//  Restore_Level_Scratch_Spare: C
//
void Restore_Level_Scratch_Spare(
    Level* level_,
    StateByte saved_state
){
    Force_Blit_Cell(SPARE, TOP);
    Force_Erase_Cell(TOP);  // allows DROP() of protected cell
    DROP();

    Force_Blit_Cell(SCRATCH, TOP);
    Force_Erase_Cell(TOP);  // allows DROP() of protected cell
    DROP();

    STATE = saved_state;
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
//  Get_Word_Or_Tuple: C
//
// Uses TOP_LEVEL to do its work; has to save fields it corrupts.
//
Result(None) Get_Word_Or_Tuple(
    Sink(Stable) out,
    const Element* v,
    Context* context
){
    Level* const L = TOP_LEVEL;

    USE_LEVEL_SHORTHANDS (L);

    assert(out != SCRATCH and out != SPARE);
    possibly(out == OUT);
    possibly(v == SPARE);
    assert(v != SCRATCH);  // need to put bound word in scratch

    assert(Is_Word(v) or Is_Tuple(v));  // no sigil, can't give back unstable

    StateByte saved_state = Save_Level_Scratch_Spare(L);

    Force_Erase_Cell(SCRATCH);  // clears protection bit

    heeded (Derelativize(  // have to do after SCRATCH erase, in case protected
        SCRATCH,
        v,  // have to do before SPARE erase, in case (v = SPARE)
        context
    ));

    Force_Erase_Cell(SPARE);  // clears protection bit

    if (out != OUT) {
        Blit_Cell(PUSH(), OUT);
        Assert_Cell_Initable(OUT);  // don't need to erase
    }

    Value* saved_out = OUT;
    L->out = out;

    heeded (Corrupt_Cell_If_Needful(SPARE));

    Error* e;

    STATE = 1;
    Get_Var_In_Scratch_To_Out(L, GROUPS_OK) except (e) {
        // still need to restore state and scratch
    }

    if (not e) {
        Decay_If_Unstable(OUT) except (e) {  // L->out is `out`
            // still need to restore state and scratch
        }
    }

    L->out = saved_out;

    if (OUT != out) {
        Force_Blit_Cell(OUT, TOP);
        DROP();
    }

    Restore_Level_Scratch_Spare(L, saved_state);

    if (e)
        return fail (e);

    return none;
}


//
//  Get_Word: C
//
// Uses TOP_LEVEL to do its work; has to save fields it corrupts.
//
Result(None) Get_Word(
    Sink(Stable) out,
    const Element* word,
    Context* context
){
    assert(Is_Word(word));
    return Get_Word_Or_Tuple(out, word, context);
}


//
//  Get_Chain_Push_Refinements: C
//
Result(Stable*) Get_Chain_Push_Refinements(
    Sink(Stable) out,
    const Element* chain,
    Context* context
){
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
    else if (Is_Word(head) or Is_Tuple(head)) {  // .member:refinement is legal
        require (  // must panic on error
          Get_Word_Or_Tuple(
            out, head, derived
        ));
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

        Get_Word(OUT, spare, SPECIFIED) except (e) {
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
    else if (Is_Word(at) or Is_Tuple(at)) {
        Get_Word_Or_Tuple(
            OUT, at, binding
        ) except (e) {
            goto return_error;
        }
        Copy_Cell(spare_left, Known_Stable(OUT));
    }
    else if (Is_Chain(at)) {
        if ((at + 1 != tail) and not Is_Space(at + 1)) {
            e = Error_User("CHAIN! can only be last item in a path right now");
            goto return_error;
        }
        Get_Chain_Push_Refinements(
            u_cast(Init(Stable), OUT),
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
//  Get_Var: C
//
// May generate specializations for paths.  See Get_Var_Maybe_Trash()
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
// * The code behind Get_Var should be merged with GET so they are the same.
//
Result(Stable*) Get_Var(
    Sink(Stable) out,
    Option(Element*) steps_out,  // if nullptr, then GROUP!s not legal
    const Element* var,
    Context* context
){
    Value* atom_out = u_cast(Value*, out);

    assert(var != atom_out);
    assert(steps_out != out);  // Legal for SET, not for GET

    if (Is_Chain(var) or Is_Path(var)) {
        StackIndex base = TOP_INDEX;

        Option(Error*) error;
        if (Is_Chain(var)) {
            Get_Chain_Push_Refinements(
                out, var, context
            ) except (error) {
                // need to drop level before returning
            }
        } else {
            require (
              Level* level_ = Make_End_Level(
                &Stepper_Executor,
                LEVEL_MASK_NONE | FLAG_STATE_BYTE(1)  // rule for trampoline
            ));

            Push_Level_Erase_Out_If_State_0(atom_out, level_);

            heeded (Derelativize(SCRATCH, var, context));
            heeded (Corrupt_Cell_If_Needful(SPARE));

            Get_Path_Push_Refinements(level_) except (error) {
                // need to drop level before returning
            }

            Drop_Level(level_);
        }

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
    }
    else {
        assert(Is_Word(var) or Is_Tuple(var));

        trap (
            Get_Word_Or_Tuple(out, var, context)
        );
    }

    trap (
        Decay_If_Unstable(atom_out)
    );

    return out;
}


//
//  Recalculate_Group_Arg_Vanishes: C
//
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
Result(bool) Recalculate_Group_Arg_Vanishes(Level* level_, SymId id)
{
    INCLUDE_PARAMS_OF_GET;  // TARGET types must be compatible with SET

    Element* target = Element_ARG(TARGET);
    assert(Is_Group(target));

    USED(ARG(GROUPS));  // accounted for in caller (since we're running this!)

   // !!! At the moment, the generic Set_Var() mechanics aren't written to
   // handle GROUP!s.  But it probably should, since it handles groups that
   // are nested under TUPLE! and such.  Review.

    if (Eval_Any_List_At_Throws(OUT, target, SPECIFIED))
        panic (Error_No_Catch_For_Throw(LEVEL));

    if (Any_Void(OUT))
        return true;

    require (
      Stable* out = Decay_If_Unstable(OUT)
    );

    if (Is_Group(out))
        return fail ("GROUP! result from SET/GET of GROUP! target not legal");

    const Stable* action = Lib_Stable(id);  // different TARGETS [1]
    ParamList* paramlist = Phase_Paramlist(Frame_Phase(action));
    Param* param = Phase_Param(paramlist, PARAM_INDEX(TARGET));

    require (
      bool check = Typecheck_Coerce_Uses_Spare_And_Scratch(LEVEL, param, out)
    );

    if (not check)
        return fail (out);

    Copy_Cell(target, Known_Element(out));  // update ARG(TARGET)
    Corrupt_Cell_If_Needful(OUT);

    return false;
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
//              quoted! quasiform!  "Get unlifted version of item"
//              block!  "Recursively GET items into a PACK!"
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

    assert(Is_Light_Null(LOCAL(DUAL)));  // "value" (SET uses, GET doesn't) [2]
    USED(ARG(DUAL));  // NULL is signal for TWEAK to GET

    USED(ARG(STEPS));  // TWEAK heeds this
    USED(ARG(GROUPS));  // TWEAK heeds this too (but so do we)

    if (Is_Group(target)) {
        if (not ARG(GROUPS))
            return fail ("GET of GROUP! target without :GROUPS not allowed");

        require (
          bool vanished = Recalculate_Group_Arg_Vanishes(LEVEL, SYM_GET)
        );
        if (vanished)
            return NULLED;
    }

    if (Any_Lifted(target))
        return UNLIFT(target);

    if (Is_Block(target)) {
        Source* a = Make_Source(Series_Len_At(target));

        Context* binding = List_Binding(target);

        const Element* tail;
        const Element* at = List_At(&tail, target);

        for (; at != tail; ++at) {
            Derelativize(target, at, binding);
            Bounce b = Apply_Cfunc(NATIVE_CFUNC(GET), LEVEL);
            assert (b == OUT);
            UNUSED(b);
            require (
              Sink(Element) elem = Alloc_Tail_Array(a)
            );
            Copy_Cell(elem, Liftify(OUT));
        }

        return Init_Pack(OUT, a);
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

    heeded (Copy_Cell(SCRATCH, Element_ARG(TARGET)));
    heeded (Corrupt_Cell_If_Needful(SPARE));

    STATE = 1;

    Get_Var_In_Scratch_To_Out(
        LEVEL, NO_STEPS
    ) except (Error* e) {
        UNUSED(e);
        return LOGIC(false);
    }

    possibly(Is_Error(OUT));  // (get meta $obj.field) can be defined as ERROR!
    return LOGIC(true);
}
