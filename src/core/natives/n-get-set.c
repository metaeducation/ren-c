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
            if (not Is_Frame(Stub_Cell(c)))
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
    Level* level_ = Make_End_Level(&Stepper_Executor, LEVEL_MASK_NONE);
    STATE = 1;  // rule for trampoline (we're setting out to non-erased)

    Sink(Atom) atom = cast(Atom*, out);
    Push_Level_Erase_Out_If_State_0(atom, level_);

    Derelativize(SCRATCH, tuple, context);
    heeded(Corrupt_Cell_If_Debug(SPARE));

    Option(Error*) e = Trap_Get_Var_In_Scratch_To_Out(level_, steps_out);
    if (e)
        return e;

    Drop_Level(level_);

    Decay_If_Unstable(atom);

    return SUCCESS;
}


//
//  Trap_Get_Var_Maybe_Trash: C
//
// This is a generalized service routine for getting variables that will
// specialize paths into concrete actions.
//
// 1. This specialization process has cost.  So if you know you have a path in
//    your hand--and all you plan to do with the result after getting it is
//    to execute it--then use Trap_Get_Path_Push_Refinements() instead of
//    this function, and then let the Action_Executor() use the refinements
//    on the stack directly.  That avoids making an intermediate action.
//
Option(Error*) Trap_Get_Var_Maybe_Trash(
    Sink(Value) out,
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
        else
            error = Trap_Get_Path_Push_Refinements(
                out, safe, var, context
            );
        Drop_Lifeguard(safe);

        if (error)
            return error;

        assert(Is_Action(out));

        if (TOP_INDEX != base) {
            DECLARE_VALUE (action);
            Move_Cell(action, out);
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

    Level* level_ = Make_End_Level(&Stepper_Executor, LEVEL_MASK_NONE);
    STATE = 1;  // rule for trampoline (we're setting out to non-erased)

    Sink(Atom) out_atom = cast(Atom*, out);
    Push_Level_Erase_Out_If_State_0(out_atom, level_);

    heeded(Derelativize(SCRATCH, var, context));
    heeded(Corrupt_Cell_If_Debug(SPARE));

    Option(Error*) e = Trap_Get_Var_In_Scratch_To_Out(level_, steps_out);
    if (e)
        return e;

    Drop_Level(level_);

    Decay_If_Unstable(out_atom);

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
    Option(Error*) error = Trap_Get_Var_Maybe_Trash(
        out, steps_out, var, context
    );
    if (error)
        return error;

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
        Option(Error*) error = Trap_Get_Any_Word(out, head, derived);
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
            if (Eval_Value_Throws(
                cast(Atom*, spare),
                c_cast(Element*, at),
                Derive_Binding(derived, at)
            )){
                return Error_No_Catch_For_Throw(TOP_LEVEL);
            }
            if (Is_Void(cast(Atom*, spare)))
                continue;  // just skip it (voids are ignored, NULLs error)

            item = Decay_If_Unstable(cast(Atom*, spare));

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
// This form of Get_Path() is low-level, and may return a non-ACTION! value
// if the path is inert (e.g. `/abc` or `.a.b/c/d`).
//
Option(Error*) Trap_Get_Path_Push_Refinements(
    Sink(Value) out,
    Sink(Value) safe,
    const Element* path,
    Context* context
){
    if (not Sequence_Has_Node(path)) {  // byte compressed
        Copy_Cell(out, path);
        goto ensure_out_is_action;  // will panic, it's not an action

      ensure_out_is_action: { ////////////////////////////////////////////////

        if (Is_Action(out))
            return SUCCESS;
        if (Is_Frame(out)) {
            Actionify(out);
            return SUCCESS;
        }
        panic ("PATH! must retrieve an action or frame");
    }}

    const Node* node1 = CELL_NODE1(path);
    if (Is_Node_A_Cell(node1)) {
        // pairing, but "Listlike", so Cell_List_At() will work on it
    }
    else switch (Stub_Flavor(c_cast(Flex*, node1))) {
      case FLAVOR_SYMBOL : {  // `/a` or `a/`
        Option(Error*) error = Trap_Get_Any_Word(out, path, context);
        if (error)
            return error;

        goto ensure_out_is_action; }

      case FLAVOR_SOURCE : {}
        break;

      default :
        crash (path);
    }

    const Element* tail;
    const Element* at = Cell_List_At(&tail, path);

    Context* derived = Derive_Binding(context, path);

    if (Is_Space(at)) {  // leading slash means execute (but we're GET-ing)
        ++at;
        assert(not Is_Space(at));  // two blanks would be `/` as WORD!
    }

    if (Is_Group(at)) {
        if (Eval_Value_Throws(out, at, derived))
            return Error_No_Catch_For_Throw(TOP_LEVEL);
    }
    else if (Is_Tuple(at)) {
        DECLARE_ELEMENT (steps);
        Option(Error*) error = Trap_Get_Tuple_Maybe_Trash(
            out, steps, at, derived
        );
        if (error)
            panic (unwrap error);  // must be abrupt
        if (Is_Trash(out))
            return Error_Bad_Word_Get(at, out);
    }
    else if (Is_Word(at)) {
        Option(Error*) error = Trap_Get_Any_Word(out, at, derived);
        if (error)
            panic (unwrap error);  // must be abrupt
    }
    else if (Is_Chain(at)) {
        if ((at + 1 != tail) and not Is_Space(at + 1))
            panic ("CHAIN! can only be last item in a path right now");
        Option(Error*) error = Trap_Get_Chain_Push_Refinements(
            out,
            safe,
            c_cast(Element*, at),
            Derive_Binding(derived, at)
        );
        if (error)
            return error;
        return SUCCESS;
    }
    else
        panic (at);  // what else could it have been?

    ++at;

    if (at == tail or Is_Space(at))
        goto ensure_out_is_action;

    if (at + 1 != tail and not Is_Space(at + 1))
        panic ("PATH! can only be two items max at this time");

    // When we see `lib/append` for instance, we want to pick APPEND out of
    // LIB and make sure it is an action.
    //
    if (Any_Context(out)) {
        if (Is_Chain(at)) {  // lib/append:dup
            Option(Error*) error = Trap_Get_Chain_Push_Refinements(
                out,
                safe,
                c_cast(Element*, at),
                Cell_Context(out)  // need to find head of chain in object
            );
            if (error)
                return error;
            return SUCCESS;
        }

        possibly(Is_Frame(out));
        Quotify(Known_Element(out));  // frame would run if eval sees unquoted

        DECLARE_ATOM (temp);
        if (rebRunThrows(
            cast(RebolValue*, temp),
            CANON(PICK),
            cast(const RebolValue*, out),  // was quoted above
            rebQ(cast(const RebolValue*, at)))  // Cell, but is Element*
        ){
            return Error_No_Catch_For_Throw(TOP_LEVEL);
        }
        Copy_Cell(out, Decay_If_Unstable(temp));
    }
    else
        panic (path);

    goto ensure_out_is_action;
}


//
//  Trap_Get_Any_Word: C
//
// This is the "high-level" chokepoint for looking up a word and getting a
// value from it.  If the word is bound to a "getter" slot, then this will
// actually run a function to retrieve the value.  For that reason, almost
// all code should be going through this layer (or higher) when fetching an
// ANY-WORD! variable.
//
Option(Error*) Trap_Get_Any_Word(
    Sink(Value) out,
    const Element* word,  // should heed sigil? (^WORD! should UNMETA?)
    Context* context
){
    assert(Sigil_Of(word) == SIGIL_0);

    const Value* slot;
    Option(Error*) error = Trap_Lookup_Word(&slot, word, context);
    if (error)
        return error;

    Copy_Cell(out, slot);

    if (Is_Trash(out))
        return Error_Bad_Word_Get(word, out);

    return SUCCESS;
}


//
//  Trap_Get_Any_Word_Maybe_Trash: C
//
// High-level: see notes on Trap_Get_Any_Word().  This version just gives back
// TRASH! vs. give an error.
//
Option(Error*) Trap_Get_Any_Word_Maybe_Trash(
    Sink(Atom) out,
    const Element* word,  // heeds Sigil (^WORD! will UNLIFT)
    Context* context
){
    assert(Any_Word(word));
    const Value* slot;
    Option(Error*) e = Trap_Lookup_Word(&slot, word, context);
    if (e)
        return e;

    Copy_Cell(out, slot);

    switch (Sigil_Of(word)) {
      case SIGIL_0:
        break;

      case SIGIL_META:
        if (not Any_Lifted(out))
            return Error_User("^WORD! can only UNLIFT quoted/quasiform");
        Unliftify_Undecayed(out);
        break;

      case SIGIL_PIN:
      case SIGIL_TIE:
        return Error_User("Cannot GET a @PINNED or $TIED variable yet");
    }

    return SUCCESS;
}


//
//  get: native [
//
//  "Gets the value of a word or path, or block of words/paths"
//
//      return: [any-value? ~[[word! tuple! @block!] any-value?]~]
//      source "Word or tuple to get, or block of PICK steps (see RESOLVE)"
//          [<opt-out> any-word? any-sequence? group! @block!]
//      :any "Do not error on unset words"
//      :groups "Allow GROUP! Evaluations"
//      :steps "Provide invariant way to get this variable again"
//  ]
//
DECLARE_NATIVE(GET)
{
    INCLUDE_PARAMS_OF_GET;

    Element* source = Element_ARG(SOURCE);

    if (Is_Chain(source)) {  // GET-WORD, SET-WORD, SET-GROUP, etc.
        if (Try_Get_Sequence_Singleheart(source))
            Unchain(source);  // want to GET or SET normally
    }

    Option(Element*) steps;
    if (Bool_ARG(STEPS)) {
        Init_Space(ARG(STEPS));
        steps = Element_ARG(STEPS);  // we write into the STEPS slot directly
    }
    else if (Bool_ARG(GROUPS))
        steps = GROUPS_OK;
    else
        steps = nullptr;  // no GROUP! evals

    if (Is_Group(source)) {
        if (not Bool_ARG(GROUPS))
            return PANIC(Error_Bad_Get_Group_Raw(source));

        if (steps != GROUPS_OK)
            return PANIC("GET on GROUP! with steps doesn't have answer ATM");

        if (Eval_Any_List_At_Throws(SPARE, source, SPECIFIED))
            return PANIC(Error_No_Catch_For_Throw(LEVEL));

        if (Is_Void(SPARE))
            return nullptr;  // !!! Is this a good idea, or should it warning?

        Value* spare = Decay_If_Unstable(SPARE);

        if (not (
            Any_Word(spare)
            or Any_Sequence(spare)
            or Is_Pinned(BLOCK, spare))
        ){
            return PANIC(spare);
        }

        source = Known_Element(spare);
    }

    Sink(Value) out = OUT;
    Option(Error*) error = Trap_Get_Var_Maybe_Trash(
        out, steps, source, SPECIFIED
    );
    if (error)
        return FAIL(unwrap error);

    if (not Bool_ARG(ANY))
        if (Is_Trash(out))
            return FAIL(Error_Bad_Word_Get(source, out));

    if (steps and steps != GROUPS_OK) {
        Source* pack = Make_Source_Managed(2);
        Set_Flex_Len(pack, 2);
        Copy_Lifted_Cell(Array_At(pack, 0), unwrap steps);
        Copy_Lifted_Cell(Array_At(pack, 1), out);
        return Init_Pack(OUT, pack);
    }

    return OUT;
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

    Push_Action(sub, LIB(TWEAK_P));
    Begin_Action(sub, CANON(TWEAK_P), PREFIX_0);
    Set_Executor_Flag(ACTION, sub, IN_DISPATCH);

    bool picker_was_meta;

    Element* location_arg;
    Element* picker_arg;
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
        Data_Stack_At(Element, picker_index)
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

    if (Is_Quoted(picker_arg)) {  // literal x.'y or x.('y) => 'y
        Unquotify(picker_arg);
        picker_was_meta = false;
    }
    else {
        if (Any_Metaform(picker_arg))
            picker_was_meta = true;  // assume pick product is meta, unlift
        else
            picker_was_meta = false;

        Plainify(picker_arg);  // drop any sigil (on cell in frame, not stack)
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

    if (not Any_Lifted(SPARE)) {
        if (Is_Nulled(SPARE)) {  // bad pick on final step should be trappable
            Copy_Cell(OUT, Data_Stack_At(Element, picker_index));
            Drop_Level(sub);

            Init_Warning(SPARE, Error_Bad_Pick_Raw(Known_Element(OUT)));
            Failify(SPARE);  // signal bad pick distinct from other panics
            return SUCCESS;
        }

        return Error_User(
            "TWEAK* (dual protocol) didn't return a lifted value"
        );
    }

    Unliftify_Undecayed(SPARE);  // review efficiency of unlift + lift here

    if (picker_was_meta) {
        Unliftify_Undecayed(SPARE);
        Decay_If_Unstable(SPARE);
    }

    Liftify(SPARE);  // need lifted for dual protocol (review)

    return SUCCESS;
}}


Option(Error*) Trap_Tweak_Spare_Is_Dual_Put_Writeback_Dual_In_Spare(
    Level* level_,
    Level* sub,
    StackIndex picker_index,
    Option(Value*) dual_poke_if_not_on_stack
){
    if (Is_Quasiform(SPARE))
        return Error_User("TWEAK* cannot be used on antiforms");

    Atom* spare_location_dual = SPARE;

    Push_Action(sub, LIB(TWEAK_P));
    Begin_Action(sub, CANON(TWEAK_P), PREFIX_0);
    Set_Executor_Flag(ACTION, sub, IN_DISPATCH);

    Element* location_arg;
    Element* picker_arg;
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
        if (Is_Quoted(picker_arg)) {  // literal x.'y or x.('y) => 'y
            Unquotify(picker_arg);

            if (dual_poke_if_not_on_stack)
                Copy_Cell(value_arg, unwrap dual_poke_if_not_on_stack);
            else {
                Copy_Cell(value_arg, TOP);
                DROP();
            }

            break;
        }

        Option(Sigil) picker_sigil = Sigil_Of(picker_arg);
        if (picker_sigil == SIGIL_META) {
            if (dual_poke_if_not_on_stack)
                Copy_Cell(value_arg, unwrap dual_poke_if_not_on_stack);
            else {
                Copy_Cell(value_arg, TOP_ELEMENT);
                DROP();
            }
            Liftify(value_arg);  // lift it again to be ^META argument
            continue;
        }

        // if not meta, needs to decay if unstable
        Value* stable_poke;
        if (dual_poke_if_not_on_stack) {
            if (not Any_Lifted(unwrap dual_poke_if_not_on_stack)) {
                Copy_Cell(value_arg, unwrap dual_poke_if_not_on_stack);
                continue;  // dual signal, do not lift dual
            }

            if (Is_Lifted_Void(unwrap dual_poke_if_not_on_stack)) {
                assert(OUT == unwrap dual_poke_if_not_on_stack);
                Init_Dual_Tripwire_Remove_Signal(value_arg);
                continue;  // do not lift dual null
            }

            Atom* atom = unwrap dual_poke_if_not_on_stack;  // !!! FIX
            Unliftify_Undecayed(atom);
            Decay_If_Unstable(atom);
            Liftify(atom);
            stable_poke = unwrap dual_poke_if_not_on_stack;
        }
        else {
            stable_poke = TOP;
        }

        if (Is_Action(stable_poke)) {  // not lifted now...
            if (Not_Cell_Flag(stable_poke, OUT_HINT_UNSURPRISING))
                panic ("Surprising ACTION! assignment, use ^ to APPROVE");

            if (Is_Word(picker_arg)) {
                Update_Frame_Cell_Label(
                    stable_poke, Cell_Word_Symbol(picker_arg)
                );
            }
        }
        Copy_Cell(value_arg, stable_poke);  // lift it to be ^META arg
    }
    then {  // not quoted...
        Plainify(picker_arg);  // drop any sigils
    }

} call_updater: {

    bool threw = Trampoline_With_Top_As_Root_Throws();

    if (threw)  // don't want to return casual error you can TRY from
        return Error_No_Catch_For_Throw(TOP_LEVEL);

    return SUCCESS;
}}


// Currently, setting the output cell to trash is how you signal a Tweak
// operation should act as a GET, not a SET.  This can't overlap with signals
// that are valid for dual.  It should be something fast to check...
//
#define Is_Tweak_A_Get(L)  Is_Atom_Trash((L)->out)
#define Mark_Tweak_As_Get(L)  Init_Tripwire((L)->out)


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
// 1. The calling function should do `heeded(Corrupt_Cell_If_Debug(SPARE))`.  This
//    helps us know they are not expecting SPARE to be maintained across the
//    evaluation.  (It's better than trying to work "Corrupts_Spare()" into
//    the already quite-long name of the function.)
//
Option(Error*) Trap_Tweak_Var_In_Scratch_With_Dual_Out_Push_Steps(
    Level* level_,  // OUT may be ERROR! antiform, see [A]
    bool groups_ok
){
  #if PERFORM_CORRUPTIONS  // caller pre-corrupts spare [1]
    assert(Not_Cell_Readable(SPARE));
  #endif

    Flags flags = LEVEL_MASK_NONE;  // reused, top level, no keepalive needed

    Sink(Atom) spare_location_dual = SPARE;

    StackIndex base = TOP_INDEX;
    StackIndex stackindex_top;

    Option(Error*) e = SUCCESS;  // for common exit path on error

  #if RUNTIME_CHECKS
    Protect_Cell(SCRATCH);  // (common exit path undoes this protect)
  #endif

  dispatch_based_on_scratch_var_type: {

    Element* scratch_var = Known_Element(SCRATCH);

    if (Any_Word(scratch_var))
        goto handle_scratch_var_as_wordlike;

    if (Any_Sequence(scratch_var))
        goto handle_scratch_var_as_sequence;

    if (Is_Pinned(BLOCK, scratch_var))
        goto handle_scratch_var_as_pinned_steps_block;

    e = Error_Bad_Value(scratch_var);
    goto return_error;

  handle_scratch_var_as_wordlike: {

    if (not Try_Get_Binding_Of(SPARE, scratch_var)) {
        e = Error_User("Couldn't get binding...");
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

    if (not Sequence_Has_Node(scratch_var)) {  // compressed byte form
        e = Error_Bad_Value(scratch_var);
        goto return_error;
    }

    const Node* node1 = CELL_NODE1(scratch_var);
    if (Is_Node_A_Cell(node1)) {  // pair optimization
        // pairings considered "Listlike", handled by Cell_List_At()
    }
    else switch (Stub_Flavor(c_cast(Flex*, node1))) {
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
            e = Error_User("Couldn't get binding...");
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
        Decay_If_Unstable(SPARE);
        if (Is_Antiform(SPARE)) {
            e = Error_Bad_Antiform(SPARE);
            goto return_error;
        }

        Move_Cell(PUSH(), Known_Element(SPARE));  // PICKER for PICKPOKE
        Quotify(TOP_ELEMENT);  // signal literal pick
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

    Option(Value*) dual_poke_if_not_on_stack = Known_Stable(OUT);

    stackindex_top = TOP_INDEX;

  poke_again: { //////////////////////////////////////////////////////////////

    StackIndex stackindex = base + 1;

  do_stack_thing: {

    OnStack(Element*) at = Data_Stack_At(Element, stackindex);
    if (Any_Lifted(at)) {  // don't dereference
        Copy_Cell(spare_location_dual, at);  // dual protocol, leave lifted
    }
    else if (Is_Word(at)) {
        const Value* slot;
        Option(Error*) error = Trap_Lookup_Word(&slot, at, SPECIFIED);
        if (error)
            panic (unwrap error);
        if (Any_Lifted(slot)) {
            e = Error_User("Can't PICK from a lifted LOCATION");
            goto return_error;
        }
        if (Is_Antiform(slot)) {
            if (Is_Action(slot))
                LIFT_BYTE(slot) = NOQUOTE_1;  // (append.series) -> parameter!
            else {
                e = Error_Bad_Antiform(slot);
                goto return_error;
            }
        }
        Copy_Cell(spare_location_dual, Known_Element(slot));
        Liftify(spare_location_dual);  // dual protocol, lift
    }
    else {
        e = Error_Bad_Value(Copy_Cell(SPARE, at));
        goto return_error;
    }

    ++stackindex;

} calculate_pick_stack_limit:

    StackIndex limit = stackindex_top;
    if (Is_Tweak_A_Get(LEVEL))
        limit = stackindex_top + 1;

    if (stackindex == limit)
        goto check_for_updater;

  keep_picking_until_last_step: {

    Level* sub = Make_End_Level(&Action_Executor, flags);

    for (; stackindex != limit; Restart_Action_Level(sub)) {
        e = Trap_Call_Pick_Refresh_Dual_In_Spare(
            level_, sub, stackindex
        );
        if (e) {
            Drop_Level(sub);
            goto return_error;
        }

        if (Is_Error(spare_location_dual)) {  // PICK failed
            if (
                stackindex == limit - 1
                and not Any_Metaform(Data_Stack_At(Element, stackindex))
            ){
                Move_Atom(OUT, SPARE);
                goto return_success;  // last step can be tolerant, see [A]
            }
            e = Cell_Error(spare_location_dual);
            goto return_error;
        }

        ++stackindex;
    }

    Drop_Level(sub);

} check_for_updater: {

    // 1. SPARE was picked via dual protocol.  At the moment of the PICK,
    //    the picker may have been ^META, in which case we wouldn't want to
    //    decay... but otherwise we would.  But that decay was already done
    //    (it just re-lifted it) so the undecayed won't make an unstable
    //    value here if the picker wasn't ^META.

    if (Is_Tweak_A_Get(LEVEL)) {
        Copy_Cell(OUT, spare_location_dual);
        Unliftify_Undecayed(OUT);  // won't make unstable if wasn't ^META [1]
        goto return_success;
    }

    // This may be the first time we do an update, or it may be a writeback
    // as we go back through the list of steps to update any bits that are
    // required to update in the referencing cells.

    Level* sub = Make_End_Level(&Action_Executor, flags);

    e = Trap_Tweak_Spare_Is_Dual_Put_Writeback_Dual_In_Spare(
        level_,
        sub,
        stackindex,  // picker_index
        dual_poke_if_not_on_stack
    );
    if (sub != TOP_LEVEL) {
        assert(e);  // ack, fix!
        Push_Level_Erase_Out_If_State_0(SPARE, sub);
    }
    Drop_Level(sub);

    if (e)
        goto return_error;

    Value* spare_writeback_dual = Known_Stable(SPARE);

    // Subsequent updates become pokes, regardless of initial updater function

    if (Is_Nulled(spare_writeback_dual))
        goto return_success;

    if (stackindex_top == base + 1) {
        e = Error_User(
            "Last TWEAK* step in POKE gave non-null writeback instruction"
        );
        goto return_error;
    }

    Assert_Cell_Stable(spare_writeback_dual);
    Copy_Cell(Data_Stack_At(Atom, TOP_INDEX), spare_writeback_dual);

    possibly(dual_poke_if_not_on_stack == nullptr);
    dual_poke_if_not_on_stack = nullptr;  // signal it's on stack now

    --stackindex_top;

    goto poke_again;

}}} return_error: { ///////////////////////////////////////////////////////////

    assert(e);
    Drop_Data_Stack_To(base);
    goto finalize_and_return;

} return_success: { //////////////////////////////////////////////////////////

    possibly(Is_Error(OUT));  // success may be ERROR! antiform, see [A]

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
    heeded(Mark_Tweak_As_Get(level_));  // mark OUT to signal a GET, not a SET

    return Trap_Tweak_Var_In_Scratch_With_Dual_Out(
        level_,
        steps_out
    );
}


//
//  set: native [
//
//  "Sets a word or path to specified value (see also: UNPACK)"
//
//      return: "Same value as input (error passthru even it skips the assign)"
//          [any-value?]
//      target "Word or tuple, or calculated sequence steps (from GET)"
//          [<undo-opt> any-word? tuple! group!
//          any-get-value? any-set-value? @block!]  ; should take PACK! [1]
//      ^value "Will be decayed if not assigned to metavariables"
//          [any-atom?]
//      :any "Do not error on unset words"
//      :groups "Allow GROUP! Evaluations"
//  ]
//
DECLARE_NATIVE(SET)
//
// 1. SET of a BLOCK! should expose the implementation of the multi-return
//    mechanics used by SET-BLOCK!.  That will take some refactoring... not
//    an urgent priority, but it needs to be done.
{
    INCLUDE_PARAMS_OF_SET;

    enum {
        ST_SET_INITIAL_ENTRY = STATE_0,
        ST_SET_SETTING  // trampoline rule: OUT must be erased if STATE_0
    };

    Element* lifted_setval = Element_ARG(VALUE);

    if (Is_Nulled(ARG(TARGET)))
        return UNLIFT(lifted_setval);   // same for SET as [10 = (void): 10]

    Element* target = Element_ARG(TARGET);

    if (Is_Chain(target))  // GET-WORD, SET-WORD, SET-GROUP, etc.
        Unchain(target);

    if (not Is_Group(target))  // !!! maybe SET-GROUP!, but GET-GROUP!?
        goto call_generic_set_var;

  process_group_target: {

   // !!! At the moment, the generic Set_Var() mechanics aren't written to
   // handle GROUP!s.  But it probably should, since it handles groups that
   // are nested under TUPLE! and such.  Review.

    if (not Bool_ARG(GROUPS))
        return PANIC(Error_Bad_Get_Group_Raw(target));

    if (Eval_Any_List_At_Throws(SPARE, target, SPECIFIED))
        return PANIC(Error_No_Catch_For_Throw(LEVEL));

    if (Is_Void(SPARE))
        return UNLIFT(lifted_setval);

    Value* spare = Decay_If_Unstable(SPARE);

    if (not (
        Any_Word(spare)
        or Any_Sequence(spare)
        or Is_Pinned(BLOCK, spare)
    )){
        return PANIC(spare);
    }

    Copy_Cell(target, Known_Element(spare));  // update ARG(TARGET)

} call_generic_set_var: { ////////////////////////////////////////////////////

    Option(Element*) steps;
    if (Bool_ARG(GROUPS))
        steps = GROUPS_OK;
    else
        steps = NO_STEPS;  // no GROUP! evals

    if (not Bool_ARG(ANY)) {
        // !!! The only SET prohibitions will be on antiform actions, TBD
        // (more general filtering available via accessors)
    }

    Copy_Cell_Core(OUT, lifted_setval, CELL_MASK_THROW);
    Unliftify_Undecayed(OUT);

    STATE = ST_SET_SETTING;  // we'll be setting out to something not erased

    heeded(Copy_Cell(SCRATCH, target));
    heeded(Corrupt_Cell_If_Debug(SPARE));

    Option(Error*) e = Trap_Set_Var_In_Scratch_To_Out(LEVEL, steps);
    if (e)
        return PANIC(unwrap e);

} return_value_even_if_we_dont_assign: {

  // We want parity between (set $x expression) and (x: expression).  It's
  // very useful that you can write (e: trap [x: expression]) and in the case
  // of an error, have the assignment skipped and the error trapped.
  //
  // Note that (set $ ^x fail "hi") will perform a lifted-assignment of the
  // quasiform error to X, but will still pass through the ERROR! antiform as
  // the overall expression result.

    return OUT;
}}


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
