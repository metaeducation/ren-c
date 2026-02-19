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
// A. The generalized GET of an arbitrary variable may return a FAILURE!
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
//    way to get an FAILURE! back in that case is if the field exists and
//    holds a lifted representation of a FAILURE!.
//
//    (!!! It's not clear if the convenience of the raised error on a normal
//    TUPLE!-type assignment is a good idea or not.  This depends on how
//    often generalized variable fetching is performed where you don't know
//    if the variable is meta-represented or not, and might have different
//    meanings for unlifting an FAILURE! vs. a missing field.  The convenience
//    of allowing TRY existed before meta-representation unlifting, so this
//    is an open question that arose.)
//

#include "sys-core.h"


//
//  Get_Var_To_Out_Use_Toplevel: C
//
// Uses TOP_LEVEL.  OUT may be FAILURE! antiform, see [A]
//
Result(None) Get_Var_To_Out_Use_Toplevel(
    const Element* var,
    GroupEval group_eval  // no GROUP!s if nulled
){
    Level* level_ = TOP_LEVEL;
    assert(STATE == ST_TWEAK_GETTING);

    possibly(Get_Cell_Flag(var, SCRATCH_VAR_NOTE_ONLY_ACTION));

    heeded (Corrupt_Cell_If_Needful(SPARE));
    heeded (Init_Null_Signifying_Tweak_Is_Pick(SCRATCH));

    Option(Error*) e = Tweak_Var_With_Dual_Scratch_To_Spare_Use_Toplevel(
        var,
        group_eval == GROUP_EVAL_YES ? GROUPS_OK : NO_STEPS
    );

    if (e)
        return fail (unwrap e);

    Copy_Cell(OUT, SPARE);

    require (
      Unlift_Cell_No_Decay(OUT)  // not unstable if wasn't ^META [1]
    );

    if (Get_Cell_Flag(var, SCRATCH_VAR_NOTE_ONLY_ACTION)) {
        if (Is_Action(OUT)) {
            // okay
        }
        else if (Is_Possibly_Unstable_Value_Frame(OUT)) {
            Activate_Frame(OUT);  // !!! dodgy... should f/ get an action?
        }
        else
            panic ("GET of word/ or obj.field/ did not yield ACTION!");
    }

    if (Is_Word(var) or Is_Tuple(var)) {

        /* if (Is_Lifted_Unstable_Antiform(SPARE)) {
            if (
                Is_Lifted_Action(As_Stable(SPARE))  // e.g. asking APPEND.DUP
                and stackindex != limit - 1
            ){
                continue;  // allow it if NOT last step (picks PARAMETER!)
            }
            if (
                Is_Lifted_Hot_Potato(As_Stable(SPARE))
                and stackindex == limit - 1
            ){
                continue;  // last non-meta pick can be unstable if hot-potato
            }
            if (Is_Lifted_Void(As_Stable(SPARE))) {
                goto treat_like_pick_absent_signal;  // like before void pick
            }
            error = Error_Unstable_Non_Meta_Raw(
                Data_Stack_At(Element, stackindex)
            );
            goto return_error;
        }
        */

        if (
            Not_Cell_Stable(OUT)
            and not Is_Hot_Potato(OUT)  // !!! review exception, probably bad
        ){
            if (Is_Void(OUT))
                return fail (Error_Bad_Pick_Raw(var));

            panic ("GET of non-meta WORD!/TUPLE! should always be stable");
        }
    }
    else if (Is_Meta_Form_Of(WORD, var) or Is_Meta_Form_Of(TUPLE, var)) {
        //
        // Allow...
    }
    else if (Is_Tied_Form_Of(BLOCK, var)) {
        //
        // Allow STEPS (how do steps encode meta or not meta?  it could be
        // just a ^ at the beginning of the block if it's meta)
        //
    }
    else {  // this should grow out into more forms, like space
        panic ("GET target must be WORD! or TUPLE!, or their META-forms");
    }

    return none;
}


//
//  Get_Word_Or_Tuple: C
//
// Makes a new Level... if you have a Level already, use it.
//
Result(None) Get_Word_Or_Tuple(Sink(Value) out, const Element* var)
{
    require (
      Level* sub = Make_End_Level(&Just_Use_Out_Executor, LEVEL_MASK_NONE)
    );
    DECLARE_VALUE (temp);  // !!! out may not be TRACK_FLAG_VALID_EVAL_TARGET
    Push_Level(temp, sub);

    heeded (Corrupt_Cell_If_Needful(Level_Spare(sub)));
    heeded (Corrupt_Cell_If_Needful(Level_Scratch(sub)));

    Option(Error*) error = SUCCESS;

    LEVEL_STATE_BYTE(sub) = ST_TWEAK_GETTING;

    Get_Var_To_Out_Use_Toplevel(
        var, GROUP_EVAL_YES
    ) except (Error* e) {
        error = e;
    }

    Drop_Level(sub);

    if (error) {
        Corrupt_Cell_If_Needful(out);
        return fail (unwrap error);
    }

    Copy_Cell(out, temp);
    return none;
}


//
//  Get_Word: C
//
// Uses TOP_LEVEL to do its work; has to save fields it corrupts.
//
Result(None) Get_Word(Sink(Stable) out, const Element* word)
{
    assert(Is_Word(word));

    trap (
      Get_Word_Or_Tuple(u_cast(Value*, out), word)
    );

    require (
      Decay_If_Unstable(u_cast(Value*, out))
    );

    return none;
}


//
//  Get_Chain_Push_Refinements: C
//
Result(Value*) Get_Chain_Push_Refinements(
    Sink(Value) out,
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
    }
    else if (Is_Word(head) or Is_Tuple(head)) {  // .member:refinement is legal
        DECLARE_ELEMENT (word_or_tuple);
        Copy_Cell(word_or_tuple, head);
        Bind_Cell_If_Unbound(word_or_tuple, derived);
        Add_Cell_Sigil(word_or_tuple, SIGIL_META);  // want ACTION!

        require (  // must panic on error
          Get_Word_Or_Tuple(out, word_or_tuple)
        );
    }
    else
        panic (head);  // what else could it have been?

    ++head;

    if (Is_Action(out))
        NOOP;  // it's good
    else if (Is_Possibly_Unstable_Value_Frame(out))
        Activate_Frame(out);
    else
        panic ("Head of CHAIN! did not evaluate to an ACTION!");

    // We push the remainder of the chain in *reverse order* as words to act
    // as refinements to the function.  The action execution machinery will
    // decide if they are valid or not.
    //
    const Element* at = tail - 1;

    for (; at != head - 1; --at) {
        assert(not Is_Blank(at));  // no internal blanks

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

    Option(Error*) error = SUCCESS;

    Track_Shield_Cell(SCRATCH);  // (common exit path undoes this shield)

    const Element* path = As_Element(SCRATCH);
    assert(Is_Path(path));

    if (not Sequence_Has_Pointer(path)) {  // byte compressed
        error = Error_Bad_Value(path);  // no meaning to 1.2.3/ or /1.2.3 etc.
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
        KIND_BYTE(spare) = Kind_From_Sigil_And_Heart(SIGIL_META, TYPE_WORD);

        Get_Word_Or_Tuple(OUT, spare) except (Error* e) {
            error = e;
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

    if (Is_Blank(at)) {  // leading slash means execute (but we're GET-ing)
        ++at;
        assert(not Is_Blank(at));  // two blanks would be `/` as WORD!
    }

    Sink(Stable) spare_left = SPARE;
    if (Is_Group(at)) {
        if (Eval_Value_Throws(SPARE, at, binding)) {
            error = Error_No_Catch_For_Throw(TOP_LEVEL);
            goto return_error;
        }
        require (
          Decay_If_Unstable(SPARE)
        );
    }
    else if (Is_Word(at) or Is_Tuple(at)) {
        Element *word_or_tuple = Copy_Cell(SPARE, at);
        Bind_Cell_If_Unbound(word_or_tuple, binding);
        Add_Cell_Sigil(word_or_tuple, SIGIL_META);  // want ACTION!

        Get_Word_Or_Tuple(
            OUT, word_or_tuple
        ) except (Error* e) {
            error = e;
            goto return_error;
        }
        require (
          Decay_If_Unstable(OUT)
        );
        Copy_Cell(spare_left, As_Stable(OUT));
    }
    else if (Is_Chain(at)) {
        if ((at + 1 != tail) and not Is_Blank(at + 1)) {
            error = Error_User(
                "CHAIN! can only be last item in a path right now"
            );
            goto return_error;
        }
        Get_Chain_Push_Refinements(
            OUT,
            at,
            Derive_Binding(binding, at)
        )
        except (Error* e) {
            error = e;
            goto return_error;
        }

        goto return_success;  // chain must resolve to an action (?!)
    }
    else {
        error = Error_Bad_Value(at);  // what else could it have been?
        goto return_error;
    }

    ++at;

    if (at == tail or Is_Blank(at)) {
        Copy_Cell(OUT, spare_left);
        goto ensure_out_is_action;
    }

    if (at + 1 != tail and not Is_Blank(at + 1))
        return fail ("PATH! can only be two items max at this time");

    // When we see `lib/append` for instance, we want to pick APPEND out of
    // LIB and make sure it is an action.
    //
    if (not Any_Context(spare_left)) {
        error = Error_Bad_Value(path);
        goto return_error;
    }

  handle_context_on_left_of_at: {

    if (Is_Chain(at)) {  // lib/append:dup
        Get_Chain_Push_Refinements(
            OUT,
            at,
            Cell_Context(spare_left)  // need to find head of chain in object
        )
        except (Error* e) {
            error = e;
            goto return_error;
        }

        goto return_success;  // chain must resolve to an action (?!)
    }

    possibly(Is_Frame(spare_left));
    Quote_Cell(As_Element(spare_left));  // lifted protocol

    Copy_Cell(PUSH(), at);

    Level* L = TOP_LEVEL;
    require (
      Level* sub = Make_End_Level(
        &Action_Executor,
        LEVEL_FLAG_DEBUG_STATE_0_OUT_NOT_ERASED_OK
      )
    );
    dont(Erase_Cell(Level_Spare(L)));  // spare read before erase
    Push_Level(Level_Spare(L), sub);

    error = Trap_Call_Pick_Refresh_Dual_In_Spare(
        L,
        sub,
        TOP_INDEX,
        false  // not tweaking, so do indirection
    );
    if (error)
        goto return_error;

    Drop_Level(sub);

    DROP();

    Copy_Cell(OUT, SPARE);
    require (
      Unlift_Cell_No_Decay(OUT)
    );

    goto ensure_out_is_action;

}} ensure_out_is_action: { ///////////////////////////////////////////////////

    if (Is_Action(OUT))
        goto return_success;

    if (Is_Possibly_Unstable_Value_Frame(OUT)) {
        Activate_Frame(OUT);
        goto return_success;
    }

    error = Error_User("PATH! must retrieve an action or frame");
    goto return_error;

} return_error: { ////////////////////////////////////////////////////////////

    assert(error);
    Drop_Data_Stack_To(base);
    goto finalize_and_return;

} return_success: { //////////////////////////////////////////////////////////

  // Currently there are no success modes that return FAILURE! antiforms (as
  // described by [A] at top of file.)  Would you ever TRY a PATH! and not
  // mean "try the result of the function invoked by the path"?  e.g. TRY
  // on a PATH! that ends in slash?

    assert(Is_Action(OUT));

    assert(not error);
    goto finalize_and_return;

} finalize_and_return: { /////////////////////////////////////////////////////

    assert(LEVEL == TOP_LEVEL);

    Corrupt_Cell_If_Needful(SPARE);

    Track_Unshield_Cell(SCRATCH);

    if (error)
        return fail (unwrap error);

    return none;
}}


//
//  Meta_Get_Var: C
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
Result(Value*) Meta_Get_Var(
    Sink(Value) out,
    Option(Element*) steps_out,  // if nullptr, then GROUP!s not legal
    const Element* var,
    Context* context
){
    assert(u_cast(Value*, var) != out);
    assert(steps_out != out);  // Legal for SET, not for GET

    if (Is_Chain(var) or Is_Path(var)) {
        StackIndex base = TOP_INDEX;

        Option(Error*) error = SUCCESS;
        if (Is_Chain(var)) {
            Get_Chain_Push_Refinements(
                out, var, context
            ) except (Error* e) {
                // need to drop level before returning
                error = e;
            }
        } else {
            require (
              Level* level_ = Make_End_Level(
                &Stepper_Executor,
                LEVEL_MASK_NONE | FLAG_STATE_BYTE(1)  // rule for trampoline
            ));
            dont(Erase_Cell(out));  // ??? why is LEVEL_STATE_BYTE 1 ???
            Push_Level(out, level_);

            heeded (Copy_Cell_May_Bind(SCRATCH, var, context));
            heeded (Corrupt_Cell_If_Needful(SPARE));

            Get_Path_Push_Refinements(level_) except (Error* e) {
                // need to drop level before returning
                error = e;
            }

            Drop_Level(level_);
        }

        if (error)
            return fail (unwrap error);

        assert(Is_Action(out));

        if (TOP_INDEX != base) {
            DECLARE_VALUE (frame);
            Move_Cell(frame, Deactivate_Action(out));

            Option(Element*) def = nullptr;  // !!! why not LIB(EMPTY_BLOCK) ?
            bool threw = Specialize_Action_Throws(  // costly, try to avoid [1]
                out, frame, def, base
            );
            assert(not threw);  // can only throw if `def`
            UNUSED(threw);
        }

        if (steps_out and steps_out != GROUPS_OK)
            Init_Quasar(unwrap steps_out);  // !!! What to return?
    }
    else {
        DECLARE_ELEMENT (word_or_tuple);
        Copy_Cell(word_or_tuple, var);
        Bind_Cell_If_Unbound(word_or_tuple, context);
        Force_Cell_Sigil(word_or_tuple, SIGIL_META);  // want ACTION!

        trap (
          Get_Word_Or_Tuple(out, word_or_tuple)
        );
    }

    return out;
}


//
//  Get_Var: C
//
Result(Stable*) Get_Var(
    Sink(Stable) out,
    Option(Element*) steps_out,  // if nullptr, then GROUP!s not legal
    const Element* var,
    Context* context
){
    Value* unstable_out = u_cast(Value*, out);
    trap (
      Meta_Get_Var(unstable_out, steps_out, var, context)
    );
    trap (
      Decay_If_Unstable(unstable_out)
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

    const Value* action = Lib_Value(id);  // different TARGETS [1]
    ParamList* paramlist = Phase_Paramlist(Frame_Phase(action));
    const Slot* param = Known_Unspecialized(
        Phase_Param(paramlist, PARAM_INDEX(TARGET))
    );

    require (
      bool check = Typecheck_Coerce_Use_Toplevel(LEVEL, param, out)
    );

    if (not check)
        return fail (out);

    Copy_Cell(target, As_Element(out));  // update ARG(TARGET)
    Corrupt_Cell_If_Needful(OUT);

    return false;
}


//
//  /get: native [
//
//  "Gets a variable (for dual band states, see TWEAK)"
//
//      return: [
//          any-value?             "will be decayed if not ^META input"
//          ~($block! any-value?)~ "Give :STEPS as well as the result value"
//          failure!               "Passthru even it skips the assign"
//      ]
//      target "Word or tuple or path, or calculated sequence steps (from GET)"
//          [
//              word! tuple!   "Unstable fetches error"
//              ^word! ^tuple! "Do not decay unstable antiform results"
//              quoted! quasiform!  "Get unlifted version of item"
//              block!  "Recursively GET items into a PACK!"
//              path!   "Specialize action specified by path"
//              group!  "If :GROUPS, retrigger GET based on evaluated value"
//              $block! "Series of calculated GET:STEPS or SET:STEPS"
//          ]
//      {dual-ignore}  ; for frame compatibility with TWEAK [1]
//      :groups "Allow GROUP! Evaluations"
//      :steps "Return evaluation steps for reproducible access"
//      :dual "Get value as lifted, or unlifted if special 'bedrock' state"
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
    INCLUDE_PARAMS_OF_GET;  // !!! must have compatible frame with TWEAK [1]

    Element* target = Element_ARG(TARGET);

    assert(Is_Light_Null(LOCAL(DUAL_IGNORE)));  // (SET uses, GET doesn't) [2]
    USED(ARG(DUAL_IGNORE));  // NULL is signal for TWEAK to GET

    USED(ARG(STEPS));  // TWEAK heeds this
    USED(ARG(GROUPS));  // TWEAK heeds this too (but so do we)

    if (Is_Group(target)) {
        if (not ARG(GROUPS))
            return fail ("GET of GROUP! target without :GROUPS not allowed");

        require (
          bool vanished = Recalculate_Group_Arg_Vanishes(LEVEL, SYM_GET)
        );
        if (vanished)
            return NULL_OUT;
    }

    if (Any_Lifted(target))
        return UNLIFT_TO_OUT(target);

    if (Is_Block(target)) {
        Source* a = Make_Source(Series_Len_At(target));

        Context* binding = List_Binding(target);

        const Element* tail;
        const Element* at = List_At(&tail, target);

        for (; at != tail; ++at) {
            Copy_Cell_May_Bind(target, at, binding);
            Bounce b = Apply_Cfunc(NATIVE_CFUNC(GET), LEVEL);
            assert (b == OUT);
            UNUSED(b);
            require (
              Sink(Element) elem = Alloc_Tail_Array(a)
            );
            Copy_Cell(elem, Lift_Cell(OUT));
        }

        return Init_Pack(OUT, a);
    }

    STATE = ARG(DUAL) ? ST_TWEAK_TWEAKING : ST_TWEAK_GETTING;

    Option(Bounce) b = Irreducible_Bounce(
        LEVEL,
        Apply_Cfunc(NATIVE_CFUNC(TWEAK), LEVEL)
    );
    if (b)
        return unwrap b;  // keep bouncing while we couldn't get OUT as answer

    if (Is_Failure(OUT))
        return OUT;  // weird can't pick case, see [A]

    if (ARG(DUAL)) {
        assert(not Is_Antiform(OUT));
        return OUT;
    }

    assert(Any_Lifted(OUT));  // should not give back BEDROCK_0 states.

    require (
      Unlift_Cell_No_Decay(OUT)  // decay or not was guided by ^VAR marker
    );
    return OUT;
}


//
//  /defined?: native [
//
//  "Check to see if a variable is defined (unset is considered defined)"
//
//      return: [logic!]
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

    Element* target = Element_ARG(TARGET);
    Add_Cell_Sigil(target, SIGIL_META);

    heeded (Corrupt_Cell_If_Needful(SPARE));
    heeded (Corrupt_Cell_If_Needful(SCRATCH));

    STATE = ST_TWEAK_GETTING;

    Get_Var_To_Out_Use_Toplevel(
        target, GROUP_EVAL_NO
    ) except (Error* e) {
        UNUSED(e);
        return LOGIC_OUT(false);
    }

    possibly(Is_Failure(OUT));  // (get meta $obj.field) can be FAILURE!
    return LOGIC_OUT(true);
}
