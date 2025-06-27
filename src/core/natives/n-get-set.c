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
//     However, the rules change with meta-representation, to where the only
//     way to get an ERROR! back in that case is if the field exists and holds
//     a lifted representation of an ERROR!.
//
//     (!!! It's not clear if the convenience of the raised error on a normal
//     TUPLE!-type assignment is a good idea or not.  This depends on how
//     often generalized variable fetching is performed where you don't know
//     if the variable is meta-represented or not, and might have different
//     meanings for unlifting an ERROR! vs. a missing field.  The convenience
//     of allowing TRY existed before meta-representation unlifting, so this
//     is an open question that arose.)
//
//     In the case of an assignment, the only way to get it to return a
//     raised ERROR! will be if the value being assigned was an ERROR!.  In
//     the case of a regular assignment the assignment itself will not be
//     performed and the error just passed through.  In a meta-assignment,
//     the assignment will be performed and the ERROR! passed through in its
//     unlifted form.
//

#include "sys-core.h"


//
//  Adjust_Context_For_Coupling: C
//
// Ren-C injects the object from which a function was dispatched in a path
// into the function call, as something called a "coupling".  This coupling is
// tied in with the FRAME! for the function call, and can be used as a context
// to do special lookups in.
//
Context* Adjust_Context_For_Coupling(Context* c) {
    for (; c != nullptr; c = maybe Link_Inherit_Bind(c)) {
        VarList* frame_varlist;
        if (Is_Stub_Varlist(c)) {  // ordinary FUNC frame context
            frame_varlist = cast(VarList*, c);
            if (CTX_TYPE(frame_varlist) != TYPE_FRAME)
                continue;
        }
        else if (Is_Stub_Use(c)) {  // e.g. LAMBDA or DOES uses this
            if (not Is_Frame(Known_Stable(Stub_Cell(c))))
                continue;
            frame_varlist = Cell_Varlist(Stub_Cell(c));
        }
        else
            continue;

        Level* level = Level_Of_Varlist_If_Running(frame_varlist);
        if (not level)
            panic (".field access only in running functions");  // nullptr?
        VarList* coupling = maybe Level_Coupling(level);
        if (not coupling)
            continue;  // skip NULL couplings (default for FUNC, DOES, etc.)
        if (coupling == UNCOUPLED)
            return nullptr;  // uncoupled frame (method, just not coupled)
        return coupling;
    }
    return nullptr;
}



//
//  Trap_Get_Tuple_Maybe_Trash: C
//
// Convenience wrapper for getting tuples that errors on trash.
//
Option(Error*) Trap_Get_Tuple_Maybe_Trash(
    Sink(Value) out,
    Option(Element*) steps_out,  // if NULL, then GROUP!s not legal
    const Element* tuple,
    Context* context
){
    Level* level_ = Make_End_Level(
        &Stepper_Executor,
        LEVEL_MASK_NONE | FLAG_STATE_BYTE(1) // rule for trampoline
    );

    Sink(Atom) atom_out = u_cast(Atom*, out);
    Push_Level_Erase_Out_If_State_0(atom_out, level_);

    heeded(Derelativize(SCRATCH, tuple, context));
    heeded(Corrupt_Cell_If_Debug(SPARE));

    Option(Error*) e = Trap_Get_Var_In_Scratch_To_Out(level_, steps_out);

    Drop_Level(level_);

    if (e)
        return e;

    Decay_If_Unstable(atom_out);

    return SUCCESS;
}


//
//  Trap_Get_Var_Maybe_Trash: C
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
Option(Error*) Trap_Get_Var_Maybe_Trash(
    Sink(Atom) out,
    Option(Element*) steps_out,  // if NULL, then GROUP!s not legal
    const Element* var,
    Context* context
){
    assert(var != cast(Cell*, out));
    assert(steps_out != out);  // Legal for SET, not for GET

    if (Is_Chain(var) or Is_Path(var)) {
        StackIndex base = TOP_INDEX;

        DECLARE_ATOM (safe);
        Push_Lifeguard(safe);

        Option(Error*) error;
        if (Is_Chain(var))
            error = Trap_Get_Chain_Push_Refinements(
                out, safe, var, context
            );
        else {
            Level* level_ = Make_End_Level(
                &Stepper_Executor,
                LEVEL_MASK_NONE | FLAG_STATE_BYTE(1)  // rule for trampoline
            );

            Push_Level_Erase_Out_If_State_0(out, level_);

            heeded(Derelativize(SCRATCH, var, context));
            heeded(Corrupt_Cell_If_Debug(SPARE));

            error = Trap_Get_Path_Push_Refinements(level_);

            Drop_Level(level_);
        }
        Drop_Lifeguard(safe);

        if (error)
            return error;

        assert(Is_Action(Known_Stable(out)));

        if (TOP_INDEX != base) {
            DECLARE_VALUE (action);
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

        return SUCCESS;
    }

    Level* level_ = Make_End_Level(
        &Stepper_Executor,
        LEVEL_MASK_NONE | FLAG_STATE_BYTE(1)  // rule for trampoline
    );

    Push_Level_Erase_Out_If_State_0(out, level_);  // flushes corruption

    heeded(Derelativize(SCRATCH, var, context));
    heeded(Corrupt_Cell_If_Debug(SPARE));

    Option(Error*) e = Trap_Get_Var_In_Scratch_To_Out(level_, steps_out);

    Drop_Level(level_);

    if (e)
        return e;

    return SUCCESS;
}


//
//  Trap_Get_Var: C
//
// May generate specializations for paths.  See Trap_Get_Var_Maybe_Trash()
//
Option(Error*) Trap_Get_Var(
    Sink(Value) out,
    Option(Element*) steps_out,  // if nullptr, then GROUP!s not legal
    const Element* var,
    Context* context
){
    Sink(Atom) atom_out = u_cast(Atom*, out);

    Option(Error*) error = Trap_Get_Var_Maybe_Trash(
        atom_out, steps_out, var, context
    );
    if (error)
        return error;

    Decay_If_Unstable(atom_out);

    if (Is_Trash(out))
        return Error_Bad_Word_Get(var, out);

    return SUCCESS;
}


//
//  Trap_Get_Chain_Push_Refinements: C
//
Option(Error*) Trap_Get_Chain_Push_Refinements(
    Sink(Value) out,
    Sink(Value) spare,
    const Element* chain,
    Context* context
){
    assert(not Try_Get_Sequence_Singleheart(chain));  // don't use w/these

    const Element* tail;
    const Element* head = Cell_List_At(&tail, chain);

    Context* derived = Derive_Binding(context, chain);

    // The first item must resolve to an action.

    if (Is_Group(head)) {  // historical Rebol didn't allow group at head
        if (Eval_Value_Throws(out, head, derived))
            return Error_No_Catch_For_Throw(TOP_LEVEL);
    }
    else if (Is_Tuple(head)) {  // .member-function:refinement is legal
        DECLARE_ELEMENT (steps);
        Option(Error*) error = Trap_Get_Tuple_Maybe_Trash(
            out, steps, head, derived
        );
        if (error)
            panic (unwrap error);  // must be abrupt
        if (Is_Trash(out))
            panic (Error_Bad_Word_Get(head, out));
    }
    else if (Is_Word(head)) {
        Option(Error*) error = Trap_Get_Word(out, head, derived);
        if (error)
            panic (unwrap error);  // must be abrupt
    }
    else
        panic (head);  // what else could it have been?

    ++head;

    if (Is_Action(out))
        NOOP;  // it's good
    else if (Is_Antiform(out))
        return Error_Bad_Antiform(out);
    else if (Is_Frame(out))
        Actionify(out);
    else
        return Error_User("Head of CHAIN! did not evaluate to an ACTION!");

    // We push the remainder of the chain in *reverse order* as words to act
    // as refinements to the function.  The action execution machinery will
    // decide if they are valid or not.
    //
    const Element* at = tail - 1;

    for (; at != head - 1; --at) {
        assert(not Is_Space(at));  // no internal blanks

        const Value* item = at;
        if (Is_Group(at)) {
            Sink(Atom) atom_spare = u_cast(Atom*, spare);
            if (Eval_Value_Throws(
                atom_spare,
                c_cast(Element*, at),
                Derive_Binding(derived, at)
            )){
                return Error_No_Catch_For_Throw(TOP_LEVEL);
            }
            if (Is_Void(atom_spare))
                continue;  // just skip it (voids are ignored, NULLs error)

            item = Decay_If_Unstable(atom_spare);

            if (Is_Antiform(item))
                return Error_Bad_Antiform(item);
        }

        if (Is_Word(item)) {
            Init_Pushed_Refinement(PUSH(), Cell_Word_Symbol(item));
        }
        else
            panic (item);
    }

    return SUCCESS;
}


//
//  Trap_Get_Path_Push_Refinements: C
//
// This is a high-level Get_Path() which only returns ACTION! in OUT.
//
// Long-term it should be able to do things like turn not/even/ into a CASCADE
// of functions.  That's not actually super hard to do, it just hasn't been
// implemented yet.  Right now a PATH! can only have two parts: a left side
// (a WORD! or a TUPLE!) and a right side (a WORD! or a CHAIN!)
//
Option(Error*) Trap_Get_Path_Push_Refinements(Level* level_)
{
  #if PERFORM_CORRUPTIONS  // confirm caller pre-corrupted spare [1]
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
        // pairing, but "Listlike", so Cell_List_At() will work on it
    }
    else switch (Stub_Flavor(c_cast(Flex*, payload1))) {
      case FLAVOR_SYMBOL: {  // `/a` or `a/`
        Element* spare = Copy_Cell(SPARE, path);
        KIND_BYTE(spare) = TYPE_WORD;
        e = Trap_Get_Any_Word_Maybe_Trash(OUT, spare, SPECIFIED);
        if (e)
            goto return_error;

        goto ensure_out_is_action; }

      case FLAVOR_SOURCE:
        break;

      default:
        crash (path);
    }

} handle_listlike_path: {

    const Element* tail;
    const Element* at = Cell_List_At(&tail, path);

    Context* binding = Cell_Sequence_Binding(path);

    if (Is_Space(at)) {  // leading slash means execute (but we're GET-ing)
        ++at;
        assert(not Is_Space(at));  // two blanks would be `/` as WORD!
    }

    Sink(Value) spare_left = SPARE;
    if (Is_Group(at)) {
        if (Eval_Value_Throws(spare_left, at, binding)) {
            e = Error_No_Catch_For_Throw(TOP_LEVEL);
            goto return_error;
        }
    }
    else if (Is_Tuple(at)) {
        DECLARE_ELEMENT (steps);
        e = Trap_Get_Tuple_Maybe_Trash(
            spare_left, steps, at, binding
        );
        if (e)
            goto return_error;
    }
    else if (Is_Word(at)) {
        e = Trap_Get_Word(spare_left, at, binding);
        if (e)
            goto return_error;
    }
    else if (Is_Chain(at)) {
        if ((at + 1 != tail) and not Is_Space(at + 1)) {
            e = Error_User("CHAIN! can only be last item in a path right now");
            goto return_error;
        }
        e = Trap_Get_Chain_Push_Refinements(
            u_cast(Init(Value), OUT),
            SPARE,
            c_cast(Element*, at),
            Derive_Binding(binding, at)
        );
        if (e)
            goto return_error;

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
        return Error_User("PATH! can only be two items max at this time");

    // When we see `lib/append` for instance, we want to pick APPEND out of
    // LIB and make sure it is an action.
    //
    if (not Any_Context(spare_left)) {
        e = Error_Bad_Value(path);
        goto return_error;
    }

  handle_context_on_left_of_at: {

    Sink(Value) out = OUT;

    if (Is_Chain(at)) {  // lib/append:dup
        e = Trap_Get_Chain_Push_Refinements(
            out,
            SPARE,  // scratch space (Cell_Context() extracts)
            at,
            Cell_Context(spare_left)  // need to find head of chain in object
        );
        if (e)
            goto return_error;

        goto return_success;  // chain must resolve to an action (?!)
    }

    possibly(Is_Frame(spare_left));
    Quotify(Known_Element(spare_left));  // frame runs if eval sees unquoted

    DECLARE_VALUE (temp);
    if (rebRunThrows(
        out,  // output cell
        CANON(PICK),
        spare_left,  // was quoted above
        rebQ(at)
    )){
        e = Error_No_Catch_For_Throw(TOP_LEVEL);
        goto return_error;
    }

    goto ensure_out_is_action;

}} ensure_out_is_action: { ///////////////////////////////////////////////////

    Value* out = Known_Stable(OUT);

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

    Corrupt_Cell_If_Debug(SPARE);

  #if RUNTIME_CHECKS
    Unprotect_Cell(SCRATCH);
  #endif

    return e;
}}


//
//  Trap_Get_Any_Word_Maybe_Trash: C
//
Option(Error*) Trap_Get_Any_Word_Maybe_Trash(
    Sink(Atom) out,
    const Element* word,  // heeds Sigil (^WORD! will UNLIFT)
    Context* context
){
    assert(Any_Word(word));

    switch (Sigil_Of(word)) {
      case SIGIL_0:
        break;

      case SIGIL_META:
        break;

      case SIGIL_PIN:
      case SIGIL_TIE:
        return Error_User("Cannot GET a @PINNED or $TIED variable yet");
    }

    return Trap_Get_Var_Maybe_Trash(out, NO_STEPS, word, context);
}


//
//  Trap_Get_Any_Word: C
//
Option(Error*) Trap_Get_Word(
    Sink(Value) out,
    const Element* word,
    Context* context
){
    assert(Is_Word(word));  // no sigil, can't give back unstable form

    Sink(Atom) atom_out = u_cast(Atom*, out);

    Option(Error*) e = Trap_Get_Any_Word_Maybe_Trash(
        atom_out, word, context
    );
    if (e)
        return e;

    if (Is_Error(atom_out))  // !!! bad pick
        return Cell_Error(atom_out);

    if (Is_Trash(out))
        return Error_Bad_Word_Get(word, out);

    return SUCCESS;
}


// This breaks out the stylized code for calling TWEAK*, in a Level that
// can be reused across multiple TWEAK* calls.
//
// The stylization is to reduce the number of C-stack-based cells that need
// to be protected from GC.  Instead, cells are written directly into the
// locations they need to be, with careful orchestration.  (This also means
// less make-work of copying bits around from one location to another.)
//
// 1. SPARE indicates both the LOCATION used for the TWEAK*, and the output
//    of the TWEAK* call.  It's a "dual" because for normal values it is
//    a lifted representation--but if it's a non-lifted ACTION! then it is
//    a function to call to do the next TWEAK* with.  This prevents explosions
//    in cases like (some-struct.million-ints.10), where you don't want the
//    (some-struct.million-ints) pick to give back a BLOCK! of a million
//    INTEGER!s just so you can pick one of them out of it.
//
static Option(Error*) Trap_Call_Pick_Refresh_Dual_In_Spare(  // [1]
    Level* level_,
    Level* sub,  // will Push_Level() if not already pushed
    StackIndex picker_index
){
    if (Is_Quasiform(SPARE))
        return Error_User("TWEAK* cannot be used on antiforms");

    Push_Action(sub, LIB(TWEAK_P), PREFIX_0);
    Set_Executor_Flag(ACTION, sub, IN_DISPATCH);

    bool picker_was_meta;

    Element* location_arg;
    Value* picker_arg;
    Element* dual_arg;

  proxy_arguments_to_frame_dont_panic_in_this_scope: {

  // We carefully lay things out so the old SPARE gets moved into the frame,
  // to free it up to be used for the output.  But this is delicate, as we
  // cannot panic() while an allocated-but-not-pushed Level is extant.
  // So everything in this section must succeed.

    assert(Is_Quoted(SPARE));  // don't support ACTION!s in dual yet...
    location_arg = Copy_Cell(
        Force_Erase_Cell(Level_Arg(sub, 1)),
        Known_Element(SPARE)
    );
    Unquotify(location_arg);

    picker_arg = Copy_Cell(
        Force_Erase_Cell(Level_Arg(sub, 2)),
        Data_Stack_At(Value, picker_index)
    );

    dual_arg = Init_Dual_Nulled_Pick_Signal(
        Force_Erase_Cell(Level_Arg(sub, 3))
    );
    USED(dual_arg);

    if (sub == TOP_LEVEL)
        Erase_Cell(SPARE);
    else
        Push_Level_Erase_Out_If_State_0(SPARE, sub);

} adjust_frame_arguments_now_that_its_safe_to_panic: {

    if (Any_Lifted(picker_arg)) {  // literal x.'y or x.('y) => 'y
        Unliftify_Known_Stable(picker_arg);
        picker_was_meta = false;

        if (Is_Keyword(picker_arg) or Is_Trash(picker_arg))
            return Error_User(
                "PICK with keyword or trash picker never allowed"
            );
    }
    else {
        Element* pick_instruction = Known_Element(picker_arg);
        if (Is_Metaform(pick_instruction))
            picker_was_meta = true;  // assume pick product is meta, unlift
        else
            picker_was_meta = false;

        Plainify(pick_instruction);  // drop any sigil (frame cell, not stack)
    }

} call_pick_p: {

    // We actually call TWEAK*, the lower-level function that uses the dual
    // protocol--instead of PICK.  That is because if the pick is not the
    // last pick, it may return an out-of-band function value that we need
    // to use to do the next pick.

    bool threw = Trampoline_With_Top_As_Root_Throws();
    if (threw)  // don't want to return casual error you can TRY from
        return Error_No_Catch_For_Throw(sub);

    assert(sub == TOP_LEVEL);
    unnecessary(Drop_Action(sub));  // !! action is dropped, should it be?

    UNUSED(picker_was_meta);  // !!! caller checks picker on stack for metaform

    return SUCCESS;
}}


Option(Error*) Trap_Tweak_Spare_Is_Dual_To_Top_Put_Writeback_Dual_In_Spare(
    Level* level_,
    Level* sub,
    StackIndex picker_index
){
    if (Is_Quasiform(SPARE))
        return Error_User("TWEAK* cannot be used on antiforms");

    Atom* spare_location_dual = SPARE;

    Push_Action(sub, LIB(TWEAK_P), PREFIX_0);
    Set_Executor_Flag(ACTION, sub, IN_DISPATCH);

    Element* location_arg;
    Value* picker_arg;
    Atom* value_arg;

  proxy_arguments_to_frame_dont_panic_in_this_scope: {

  // We can't panic while there's an extant level that's not pushed.
  //
  // (See notes in Trap_Call_Pick_Refresh_Dual_In_Spare() for more details.)
  //
  // 1. GET:STEPS returns @var for steps of var.  But is (get @var) same as
  //    (get $var) ?

    assert(Is_Quoted(spare_location_dual));
    location_arg = Copy_Cell(
        Force_Erase_Cell(Level_Arg(sub, 1)),
        Known_Element(spare_location_dual)
    );
    Unquotify(location_arg);

    picker_arg = Copy_Cell(
        Force_Erase_Cell(Level_Arg(sub, 2)),
        Data_Stack_At(Element, picker_index)
    );

    value_arg = u_cast(Atom*, Force_Erase_Cell(Level_Arg(sub, 3)));

    Push_Level_Erase_Out_If_State_0(SPARE, sub);  // SPARE becomes writeback

} adjust_frame_arguments_now_that_its_safe_to_panic: {

    attempt {
        if (Any_Lifted(picker_arg)) {  // literal x.'y or x.('y) => 'y
            Unliftify_Known_Stable(picker_arg);

            if (Is_Keyword(picker_arg) or Is_Trash(picker_arg))
                return Error_User(
                    "PICK with keyword or trash picker never allowed"
                );

            Copy_Cell(value_arg, TOP_ELEMENT);
            Unliftify_Undecayed(value_arg);
            Decay_If_Unstable(value_arg);
            Liftify(value_arg);
            break;
        }

        Element* picker_instruction = Known_Element(picker_arg);
        Option(Sigil) picker_sigil = Sigil_Of(picker_instruction);
        if (picker_sigil == SIGIL_META) {
            Copy_Cell(value_arg, TOP_ELEMENT);  // don't decay
            continue;
        }

        // if not meta, needs to decay if unstable

        if (not Any_Lifted(TOP)) {
            Copy_Cell(value_arg, TOP);
            continue;  // dual signal, do not lift dual
        }

        if (Is_Lifted_Void(TOP)) {
            Init_Dual_Word_Remove_Signal(value_arg);
            continue;  // do not lift dual signal
        }

        Copy_Cell(value_arg, TOP_ELEMENT);
        Unliftify_Undecayed(value_arg);
        Decay_If_Unstable(value_arg);
        Liftify(value_arg);

        if (Is_Lifted_Action(Known_Stable(value_arg))) {
            if (Not_Cell_Flag(TOP, OUT_HINT_UNSURPRISING))
                return Error_Surprising_Action_Raw(picker_arg);

            if (Is_Word(picker_arg)) {
                Update_Frame_Cell_Label(  // !!! is this a good idea?
                    Known_Stable(value_arg), Cell_Word_Symbol(picker_arg)
                );
            }
        }
    }
    then {  // not quoted...
        Plainify(Known_Element(picker_arg));  // drop any sigils
    }

    Corrupt_Cell_If_Debug(TOP);  // shouldn't use past this point

} call_updater: {

    bool threw = Trampoline_With_Top_As_Root_Throws();

    if (threw)  // don't want to return casual error you can TRY from
        return Error_No_Catch_For_Throw(TOP_LEVEL);

    return SUCCESS;
}}


//
//  Trap_Tweak_Var_In_Scratch_With_Dual_Out_Push_Steps: C
//
// This is centralized code for setting or "tweaking" variables.
//
// **Almost all parts of the system should go through this code for assignment,
// even when they know they have just a WORD! in their hand and don't need path
// dispatch.**  Only a few places bypass this code for reasons of optimization,
// but they must do so carefully, because that would skip things like
// accessors (which implement type checking, etc.)
//
// 1. The calling function should do `heeded(Corrupt_Cell_If_Debug(SPARE))`.
//    This helps be sure they're not expecting SPARE to be untouched.  (It's
//    better than trying to work "Corrupts_Spare()" into the already quite-long
//    name of the function.)
//
Option(Error*) Trap_Tweak_Var_In_Scratch_With_Dual_Out_Push_Steps(
    Level* level_,  // OUT may be ERROR! antiform, see [A]
    bool groups_ok
){
    Value* out = Known_Stable(OUT);

    assert(LEVEL == TOP_LEVEL);
    possibly(Get_Cell_Flag(SCRATCH, SCRATCH_VAR_NOTE_ONLY_ACTION));

  #if PERFORM_CORRUPTIONS  // confirm caller pre-corrupted spare [1]
    assert(Not_Cell_Readable(SPARE));
  #endif

    Flags flags = LEVEL_MASK_NONE;  // reused, top level, no keepalive needed

    Sink(Atom) spare_location_dual = SPARE;

    StackIndex base = TOP_INDEX;
    StackIndex stackindex_top;

    Option(Error*) e = SUCCESS;  // for common exit path on error

  #if RUNTIME_CHECKS
    Protect_Cell(SCRATCH);  // (common exit path undoes this protect)
    if (not Is_Dual_Nulled_Pick_Signal(out))
        Protect_Cell(OUT);
  #endif

  dispatch_based_on_scratch_var_type: {

    Element* scratch_var = Known_Element(SCRATCH);

    if (Any_Word(scratch_var))
        goto handle_scratch_var_as_wordlike;

    if (Any_Sequence(scratch_var))
        goto handle_scratch_var_as_sequence;

    if (Is_Pinned_Form_Of(BLOCK, scratch_var))
        goto handle_scratch_var_as_pinned_steps_block;

    e = Error_Bad_Value(scratch_var);
    goto return_error;

  handle_scratch_var_as_wordlike: {

    if (not Try_Get_Binding_Of(SPARE, scratch_var)) {
        e = Error_No_Binding_Raw(scratch_var);
        goto return_error;
    }

    Copy_Cell(PUSH(), Known_Element(SPARE));
    Liftify(TOP);  // dual protocol, lift (?)

    Copy_Cell(PUSH(), scratch_var);  // save var for steps + error messages
    unnecessary(Liftify(TOP));  // if ^x, not literally ^x ... meta-variable

    goto set_from_steps_on_stack;

} handle_scratch_var_as_sequence: {

    // If we have a sequence, then GROUP!s must be evaluated.  (If we're given
    // a steps array as input, then a GROUP! is literally meant as a
    // GROUP! by value).  These evaluations should only be allowed if the
    // caller has asked us to return steps.

    if (not Sequence_Has_Pointer(scratch_var)) {  // compressed byte form
        e = Error_Bad_Value(scratch_var);
        goto return_error;
    }

    const Base* payload1 = CELL_PAYLOAD_1(scratch_var);
    if (Is_Base_A_Cell(payload1)) {  // pair optimization
        // pairings considered "Listlike", handled by Cell_List_At()
    }
    else switch (Stub_Flavor(c_cast(Flex*, payload1))) {
      case FLAVOR_SYMBOL: {
        if (Get_Cell_Flag(scratch_var, LEADING_SPACE)) {  // `/a` or `.a`
            panic ("Leading dot selection is being redesigned.");
            /*if (Heart_Of(scratch_var) == TYPE_TUPLE) {
                Context* context = Cell_Binding(scratch_var);
                context = Adjust_Context_For_Coupling(context);
                Tweak_Cell_Binding(scratch_var, context);
            }
            goto handle_scratch_var_as_wordlike;*/
        }

        // `a/` or `a.`
        //
        // !!! If this is a PATH!, it should error if it's not an action...
        // and if it's a TUPLE! it should error if it is an action.  Review.
        //
        goto handle_scratch_var_as_wordlike; }

      case FLAVOR_SOURCE:
        break;  // fall through

      default:
        crash (scratch_var);
    }

    const Element* tail;
    const Element* head = Cell_List_At(&tail, scratch_var);
    const Element* at;
    Context* at_binding = Cell_Binding(scratch_var);

    if (Any_Word(head)) {  // add binding at head
        if (not Try_Get_Binding_Of(
            PUSH(), Derelativize(SPARE, head, at_binding)
        )){
            DROP();
            e = Error_No_Binding_Raw(Known_Element(SPARE));
            goto return_error;
        }

        Liftify(TOP);  // dual protocol, lift (?)
    }

    for (at = head; at != tail; ++at) {
        if (not Is_Group(at)) {  // must keep WORD!s at head as-is for writeback
            possibly(Is_Quoted(at));  // will be interpreted "literally"
            Derelativize(PUSH(), at, at_binding);
            continue;
        }

        if (not groups_ok) {
            e = Error_Bad_Get_Group_Raw(scratch_var);
            goto return_error;
        }

        if (Eval_Any_List_At_Throws(SPARE, at, at_binding)) {
            Drop_Data_Stack_To(base);
            e = Error_No_Catch_For_Throw(TOP_LEVEL);
            goto finalize_and_return;
        }

        Value* spare_picker = Decay_If_Unstable(SPARE);
        possibly(Is_Antiform(spare_picker));  // e.g. PICK DATATYPE! from MAP!
        Liftify(spare_picker);  // signal literal pick
        Move_Cell(PUSH(), spare_picker);
    }

    goto set_from_steps_on_stack;

} handle_scratch_var_as_pinned_steps_block: {

    const Element* tail;
    const Element* head = Cell_List_At(&tail, scratch_var);
    const Element* at;
    Context* at_binding = Cell_Binding(scratch_var);
    for (at = head; at != tail; ++at)
        Derelativize(PUSH(), at, at_binding);

    goto set_from_steps_on_stack;

}} set_from_steps_on_stack: { ////////////////////////////////////////////////

    // We always poke from the top of the stack, not from OUT.  This is
    // because we may have to decay it, and we don't want to modify OUT.
    // It also simplifies the bookkeeping because we don't have to remember
    // if we're looking to poke from the stack or not.

    stackindex_top = TOP_INDEX;  // capture "top of stack" before push

    Copy_Cell_Core(PUSH(), Known_Stable(OUT), CELL_MASK_THROW);

  poke_again: { //////////////////////////////////////////////////////////////

    StackIndex stackindex = base + 1;

  do_stack_thing: {

    OnStack(Element*) at = Data_Stack_At(Element, stackindex);
    Copy_Cell(spare_location_dual, at);  // dual protocol, leave lifted
    if (not Any_Lifted(spare_location_dual)) {
        e = Error_User("First Element in STEPS must be lifted");
        goto return_error;
    }

    ++stackindex;

} calculate_pick_stack_limit: {

    StackIndex limit = stackindex_top;
    if (Is_Dual_Nulled_Pick_Signal(out))
        limit = stackindex_top + 1;

    if (stackindex == limit)
        goto check_for_updater;

  keep_picking_until_last_step: {

    Level* sub = Make_End_Level(&Action_Executor, flags);

    for (; stackindex != limit; ++stackindex, Restart_Action_Level(sub)) {
        e = Trap_Call_Pick_Refresh_Dual_In_Spare(
            level_, sub, stackindex
        );
        if (e) {
            if (sub->varlist)
                Drop_Action(sub);  // drop any varlist, if it exists
            Drop_Level(sub);
            goto return_error;
        }

        if (Any_Lifted(SPARE)) {  // most common answer--successful pick

            if (not Is_Metaform(Data_Stack_At(Element, stackindex))) {
                Unliftify_Undecayed(SPARE);  // review unlift + lift
                Decay_If_Unstable(SPARE);
                Liftify(SPARE);  // need lifted for dual protocol (review)
            }

            continue;
        }

        if (Is_Dual_Nulled_Absent_Signal(Known_Stable(SPARE))) {
            Copy_Cell(SPARE, Data_Stack_At(Element, stackindex));
            e = Error_Bad_Pick_Raw(Known_Element(SPARE));
            if (
                stackindex == limit - 1
                and not Is_Metaform(Data_Stack_At(Element, stackindex))
            ){
                Init_Warning(OUT, unwrap e);
                Failify(OUT);  // signal bad pick distinct from panics

                e = SUCCESS;
                Drop_Level(sub);
                goto return_success;  // last step can be tolerant, see [A]
            }
            Drop_Level(sub);
            goto return_error;
        }

        if (Is_Dual_Word_Unset_Signal(Known_Stable(SPARE))) {
            if (
                stackindex == limit - 1
                and Is_Dual_Nulled_Pick_Signal(out)
            ){
                break;  // let tweak return the unset signal
            }

            Drop_Level(sub);
            return Error_User("Unset variable");
        }

        e = Error_User("TWEAK* (dual protocol) gave unknown state for PICK");
        Drop_Level(sub);
        goto return_error;
    }

    Drop_Level(sub);

}} check_for_updater: {

    // 1. SPARE was picked via dual protocol.  At the moment of the PICK,
    //    the picker may have been ^META, in which case we wouldn't want to
    //    decay... but otherwise we would.  But that decay was already done
    //    (it just re-lifted it) so the undecayed won't make an unstable
    //    value here if the picker wasn't ^META.

    if (Is_Dual_Nulled_Pick_Signal(out)) {
        assert(Is_Nulled(TOP));
        Copy_Cell(OUT, spare_location_dual);
        goto return_success;
    }

    if (Get_Cell_Flag(SCRATCH, SCRATCH_VAR_NOTE_ONLY_ACTION)) {
        Clear_Cell_Flag(SCRATCH, SCRATCH_VAR_NOTE_ONLY_ACTION);  // consider *once*

        if (not Is_Lifted_Action(TOP)) {
            e = Error_User("/word: and /obj.field: assignments need ACTION!");
            goto return_error;
        }
        Set_Cell_Flag(TOP, OUT_HINT_UNSURPRISING);
    }

    // This may be the first time we do an update, or it may be a writeback
    // as we go back through the list of steps to update any bits that are
    // required to update in the referencing cells.

    Level* sub = Make_End_Level(&Action_Executor, flags);

    e = Trap_Tweak_Spare_Is_Dual_To_Top_Put_Writeback_Dual_In_Spare(
        level_,
        sub,
        stackindex  // picker_index
    );
    if (sub != TOP_LEVEL) {
        assert(e);  // ack, fix!
        Push_Level_Erase_Out_If_State_0(SPARE, sub);
    }
    if (sub->varlist)
        Drop_Action(sub);
    Drop_Level(sub);

    if (e)
        goto return_error;

    Value* spare_writeback_dual = Known_Stable(SPARE);

    // Subsequent updates become pokes, regardless of initial updater function

    if (Is_Dual_Nulled_No_Writeback_Signal(spare_writeback_dual))
        goto return_success;

    if (stackindex_top == base + 1) {
        e = Error_User(
            "Last TWEAK* step in POKE gave non-null writeback instruction"
        );
        goto return_error;
    }

    Assert_Cell_Stable(spare_writeback_dual);
    Copy_Cell(Data_Stack_At(Atom, TOP_INDEX), spare_writeback_dual);

    --stackindex_top;

    goto poke_again;

}}} return_error: { ///////////////////////////////////////////////////////////

    assert(e);
    Drop_Data_Stack_To(base);
    goto finalize_and_return;

} return_success: { //////////////////////////////////////////////////////////

    possibly(Is_Error(OUT));  // success may be ERROR! antiform, see [A]

    assert(not e);

    DROP();  // drop pushed cell for decaying OUT/etc.

    goto finalize_and_return;

} finalize_and_return: { /////////////////////////////////////////////////////

    assert(LEVEL == TOP_LEVEL);

    Corrupt_Cell_If_Debug(SPARE);

  #if RUNTIME_CHECKS
    Unprotect_Cell(SCRATCH);
    if (Get_Cell_Flag(OUT, PROTECTED))
        Unprotect_Cell(OUT);
  #endif

    return e;
}}


//
//  Trap_Tweak_Var_In_Scratch_With_Dual_Out: C
//
Option(Error*) Trap_Tweak_Var_In_Scratch_With_Dual_Out(
    Level* level_,  // OUT may be ERROR! antiform, see [A]
    Option(Element*) steps_out  // no GROUP!s if nulled
){
    possibly(SPARE == steps_out or SCRATCH == steps_out);

    assert(STATE != STATE_0);  // trampoline rule: OUT only erased if STATE_0

    dont(assert(TOP_INDEX == STACK_BASE));  // Hmmm, why not?
    StackIndex base = TOP_INDEX;

    Option(Error*) e;
    e = Trap_Tweak_Var_In_Scratch_With_Dual_Out_Push_Steps(
        level_,
        steps_out != NO_STEPS
    );
    if (e)
        return e;

    if (not steps_out or steps_out == GROUPS_OK) {
        Drop_Data_Stack_To(base);
        return SUCCESS;
    }

    if (TOP_INDEX == base + 1 and Is_Word(TOP_ELEMENT)) {
        Copy_Cell(unwrap steps_out, TOP_ELEMENT);
        DROP();
    }
    else
        Init_Block(unwrap steps_out, Pop_Source_From_Stack(base));

    Pinify(unwrap steps_out);  // steps are @[bl o ck] or @word

    return SUCCESS;
}


//
//  Trap_Set_Var_In_Scratch_To_Out: C
//
Option(Error*) Trap_Set_Var_In_Scratch_To_Out(
    Level* level_,  // OUT may be ERROR! antiform, see [A]
    Option(Element*) steps_out  // no GROUP!s if nulled
){
    Liftify(OUT);  // must be lifted to be taken literally in dual protocol
    Option(Error*) e = Trap_Tweak_Var_In_Scratch_With_Dual_Out(
        level_,
        steps_out
    );
    Unliftify_Undecayed(OUT);
    return e;
}


//
//  Trap_Get_Var_In_Scratch_To_Out: C
//
Option(Error*) Trap_Get_Var_In_Scratch_To_Out(
    Level* level_,  // OUT may be ERROR! antiform, see [A]
    Option(Element*) steps_out  // no GROUP!s if nulled
){
    heeded(Init_Dual_Nulled_Pick_Signal(OUT));

    Option(Error*) e = Trap_Tweak_Var_In_Scratch_With_Dual_Out(
        level_,
        steps_out
    );
    if (e)
        return e;

    if (Is_Error(OUT))  // !!! weird can't pick case
        return SUCCESS;

    if (Is_Dual_Word_Unset_Signal(Known_Stable(OUT)))
        return Error_User("UNSET variable");

    Unliftify_Undecayed(OUT);  // won't make unstable if wasn't ^META [1]
    return SUCCESS;
}


//
//  tweak: native [
//
//  "Low-level variable setter, that can assign within the dual band"
//
//      return: "Same value as input (error passthru even it skips the assign)"
//          [any-value?]
//      target "Word or tuple, or calculated sequence steps (from GET)"
//          [<opt> any-word? tuple! group!
//          any-get-value? any-set-value? @block!]
//      dual "Ordinary GET or SET with lifted value (unlifts), else dual"
//          [null? tripwire? space? quasiform! quoted!]
//      :any "Do not error on unset words"
//      :groups "Allow GROUP! Evaluations"
//  ]
//
DECLARE_NATIVE(TWEAK)
{
    INCLUDE_PARAMS_OF_TWEAK;

    enum {
        ST_TWEAK_INITIAL_ENTRY = STATE_0,
        ST_TWEAK_TWEAKING  // trampoline rule: OUT must be erased if STATE_0
    };

    Value* dual = ARG(DUAL);

    Copy_Cell_Core(OUT, dual, CELL_MASK_THROW);

    if (Is_Nulled(ARG(TARGET)))
        return OUT;   // same for SET as [10 = (void): 10]

    Element* target = Element_ARG(TARGET);

    if (Is_Chain(target))  // GET-WORD, SET-WORD, SET-GROUP, etc.
        Unchain(target);

    if (not Is_Group(target))  // !!! maybe SET-GROUP!, but GET-GROUP!?
        goto call_generic_tweak;

  process_group_target: {

   // !!! At the moment, the generic Set_Var() mechanics aren't written to
   // handle GROUP!s.  But it probably should, since it handles groups that
   // are nested under TUPLE! and such.  Review.

    if (not Bool_ARG(GROUPS))
        return PANIC(Error_Bad_Get_Group_Raw(target));

    if (Eval_Any_List_At_Throws(SPARE, target, SPECIFIED))
        return PANIC(Error_No_Catch_For_Throw(LEVEL));

    if (Is_Void(SPARE))
        return OUT;

    Value* spare = Decay_If_Unstable(SPARE);

    if (not (
        Any_Word(spare)
        or Any_Sequence(spare)
        or Is_Pinned_Form_Of(BLOCK, spare)
    )){
        return PANIC(spare);
    }

    Copy_Cell(target, Known_Element(spare));  // update ARG(TARGET)

} call_generic_tweak: { //////////////////////////////////////////////////////

    Option(Element*) steps;
    if (Bool_ARG(GROUPS))
        steps = GROUPS_OK;
    else
        steps = NO_STEPS;  // no GROUP! evals

    if (not Bool_ARG(ANY)) {
        // !!! The only SET prohibitions will be on antiform actions, TBD
        // (more general filtering available via accessors)
    }

    STATE = ST_TWEAK_TWEAKING;  // we'll be setting out to something not erased

    heeded(Copy_Cell(SCRATCH, target));
    heeded(Corrupt_Cell_If_Debug(SPARE));

    Option(Error*) e = Trap_Tweak_Var_In_Scratch_With_Dual_Out(LEVEL, steps);
    if (e)
        return PANIC(unwrap e);

} return_value_even_if_we_dont_assign: {

  // We want parity between (set $x expression) and (x: expression).  It's
  // very useful that you can write (e: trap [x: expression]) and in the case
  // of an error, have the assignment skipped and the error trapped.
  //
  // Note that (set $ '^x fail "hi") will assign the error! to X, but will
  // still pass through the ERROR! antiform as the overall expression result.

    return OUT;
}}


//
//  set: native [
//
//  "Sets a variable to specified value (for dual band states, see TWEAK)"
//
//      return: "Same value as input (error passthru even it skips the assign)"
//          [any-value?]
//      target "Word or tuple, or calculated sequence steps (from GET)"
//          [<opt> any-word? tuple! group!
//          any-get-value? any-set-value? @block!]
//      ^value "Will be decayed if not assigned to metavariables"
//          [any-atom?]  ; should take PACK! [1]
//      :any "Do not error on unset words"
//      :groups "Allow GROUP! Evaluations"
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

    USED(ARG(TARGET));
    // is actually ARG(DUAL) in TWEAK, need to lift it
    USED(ARG(ANY));
    USED(ARG(GROUPS));

    Atom* dual = Atom_ARG(DUAL);
    Liftify(dual);

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
//      return: "Same value as input (error passthru even it skips the assign)"
//          [any-value?]
//      target "Word or tuple, or calculated sequence steps (from GET)"
//          [<opt> any-word? tuple! group!
//          any-get-value? any-set-value? @block!]
//      :dual-ignore "!!! Just for frame compatibility !!!"  ; dummy [1]
//      :any "Do not error on unset words"
//      :groups "Allow GROUP! Evaluations"
//  ]
//
DECLARE_NATIVE(GET)
//
// GET is really just a version of TWEAK that passes null, and unlifts the
// return result.
//
// 1. Something has to be picked for placeholder slots or locals in the
//    frame, so you can make dummy slots but not show them on the interface
//    of the function.  Once upon a time this would be like `.dual` but that
//    was removed.  Several instances of this exist and need an answer.
{
    INCLUDE_PARAMS_OF_TWEAK;  // !!! must have compatible frame

    USED(ARG(TARGET));
    assert(Is_Nulled(ARG(DUAL)));  // "value" slot
    USED(ARG(DUAL));
    USED(ARG(ANY));
    USED(ARG(GROUPS));

    Option(Bounce) b = Irreducible_Bounce(
        LEVEL,
        Apply_Cfunc(NATIVE_CFUNC(TWEAK), LEVEL)
    );
    if (b)
        return unwrap b;  // keep bouncing while we couldn't get OUT as answer

    if (Is_Error(OUT))
        return OUT;  // weird can't pick case, see [A]

    if (not Any_Lifted(OUT))
        return PANIC("GET of UNSET or other weird state (see TWEAK)");

    return Unliftify_Undecayed(OUT);
}


//
//  .: native [
//
//  "Get the current coupling from the binding environment"
//
//      return: [null? object!]
//  ]
//
DECLARE_NATIVE(DOT_1)
{
    INCLUDE_PARAMS_OF_DOT_1;

    Context* coupling = Adjust_Context_For_Coupling(Level_Binding(LEVEL));
    if (not coupling)
        return FAIL("No current coupling in effect");

    return Init_Object(OUT, cast(VarList*, coupling));
}
