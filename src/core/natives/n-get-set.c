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


// This is the core implementation of Trap_Get_Any_Word(), that allows being
// called on "wordlike" sequences (like `.a` or `a/`).  But there are no
// special adjustments for sequences like `.a`
//
static Option(Error*) Trap_Get_Wordlike_Cell_Maybe_Trash(
    Sink(Value) out,
    const Element* word,  // sigils ignored (^WORD! doesn't "meta-get")
    Context* context  // context for `.xxx` tuples not adjusted
){
    assert(Wordlike_Cell(word));

    const Value* lookup;
    Option(Error*) error = Trap_Lookup_Word(&lookup, word, context);
    if (error)
        return error;

    if (not (lookup->header.bits & CELL_FLAG_VAR_IS_ACCESSOR)) {
        Copy_Cell(out, lookup);  // non-accessor variable, just plain value
        return SUCCESS;
    }

    assert(Heart_Of(lookup) == TYPE_FRAME);  // alias accessors as WORD! ?
    assert(QUOTE_BYTE(lookup) == ANTIFORM_0);

    DECLARE_ELEMENT (accessor);
    Push_Lifeguard(accessor);
    accessor->header.bits |= (
        NODE_FLAG_NODE | NODE_FLAG_CELL  // ensure NODE+CELL
        | (lookup->header.bits & CELL_MASK_COPY & (~ NODE_FLAG_UNREADABLE))
    );
    accessor->extra = lookup->extra;
    accessor->payload = lookup->payload;
    QUOTE_BYTE(accessor) = NOQUOTE_1;

    bool threw = rebRunThrows(out, accessor);  // run accessor as GET
    Drop_Lifeguard(accessor);
    if (threw)
        return Error_No_Catch_For_Throw(TOP_LEVEL);
    return SUCCESS;
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

    Option(Error*) error = Trap_Get_Var_In_Scratch_To_Out_Uses_Spare(
        level_, steps_out
    );
    if (error)
        return error;

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

    if (Any_Word(var)) {
        Option(Error*) error = Trap_Get_Wordlike_Cell_Maybe_Trash(
            out, var, context
        );
        if (error)
            return error;

        if (steps_out and steps_out != GROUPS_OK)
            Pinify(Derelativize(unwrap steps_out, var, context));

        return SUCCESS;
    }

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

    if (Is_Tuple(var))
        return Trap_Get_Tuple_Maybe_Trash(out, steps_out, var, context);

    if (Is_Pinned(BLOCK, var)) {  // "steps"
        StackIndex base = TOP_INDEX;

        Context* at_binding = Derive_Binding(context, var);
        const Element* tail;
        const Element* head = Cell_List_At(&tail, var);
        const Element* at;
        for (at = head; at != tail; ++at)
            Derelativize(PUSH(), at, at_binding);

        Option(Error*) error = Trap_Get_From_Steps_On_Stack_Maybe_Trash(
            out, base
        );
        Drop_Data_Stack_To(base);

        if (error)
            return error;

        if (steps_out and steps_out != GROUPS_OK)
            Copy_Cell(unwrap steps_out, var);

        return SUCCESS;
    }

    panic (var);
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
//  Get_Var_May_Panic: C
//
// Simplest interface.  Gets a variable, doesn't process groups, and will
// panic if the variable is trash.  Use the appropriate Trap_Get_XXXX()
// interface if this is too simplistic.
//
Value* Get_Var_May_Panic(
    Sink(Value) out,  // variables never store unstable Atom* values
    const Element* var,
    Context* context
){
    Option(Element*) steps_out = nullptr;  // signal groups not allowed to run

    Option(Error*) error = Trap_Get_Var(  // vacant will give error
        out, steps_out, var, context
    );
    if (error)
        panic (unwrap error);

    assert(not Is_Trash(out));  // shouldn't have returned it
    return out;
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

    Sink(Atom) atom = cast(Atom*, out);
    Option(Error*) error = Trap_Get_Wordlike_Cell_Maybe_Trash(
        atom, word, context
    );
    if (error)
        return error;

    assert(Is_Stable(atom));

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
    Option(Error*) e = Trap_Get_Wordlike_Cell_Maybe_Trash(out, word, context);
    if (e)
        return e;

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
//  Trap_Get_From_Steps_On_Stack_Maybe_Trash: C
//
// The GET and SET operations are able to tolerate :GROUPS, whereby you can
// run somewhat-arbitrary code that appears in groups in tuples.  This can
// mean that running GET on something and then SET on it could run that code
// twice.  If you want to avoid that, a sequence of :STEPS can be requested
// that can be used to find the same location after initially calculating
// the groups, without doubly evaluating.
//
// This is a common service routine used for both tuples and "step lists",
// which uses the stack (to avoid needing to generate an intermediate array
// in the case evaluations were performed).
//
Option(Error*) Trap_Get_From_Steps_On_Stack_Maybe_Trash(
    Sink(Value) out,
    StackIndex base
){
    StackIndex stackindex = base + 1;

  blockscope {
    OnStack(Element*) at = Data_Stack_At(Element, stackindex);
    if (Is_Quoted(at)) {
        Copy_Cell(out, at);
        Unquotify(Known_Element(out));
    }
    else if (Is_Word(at)) {
        const Value* slot;
        Option(Error*) error = Trap_Lookup_Word(
            &slot, cast(Element*, at), SPECIFIED
        );
        if (error)
            panic (unwrap error);
        Copy_Cell(out, slot);
    }
    else
        panic (Copy_Cell(out, at));
  }

    ++stackindex;

    DECLARE_ATOM (temp);
    Push_Lifeguard(temp);

    while (stackindex != TOP_INDEX + 1) {
        Move_Cell(temp, out);
        QUOTE_BYTE(temp) = ONEQUOTE_NONQUASI_3;
        const Node* ins = rebQ(cast(Value*, Data_Stack_Cell_At(stackindex)));
        if (rebRunCoreThrows_internal(
            out,  // <-- output cell
            EVAL_EXECUTOR_FLAG_NO_RESIDUE
                | LEVEL_FLAG_UNINTERRUPTIBLE
                | LEVEL_FLAG_ERROR_RESULT_OK,
            CANON(PICK), temp, ins
        )){
            Drop_Data_Stack_To(base);
            Drop_Lifeguard(temp);
            return Error_No_Catch_For_Throw(TOP_LEVEL);
        }

        if (Is_Error(cast(Atom*, out))) {
            Error* error = Cell_Error(out);  // extract error
            bool last_step = (stackindex == TOP_INDEX);

            Drop_Data_Stack_To(base);  // Note: changes TOP_INDEX
            Drop_Lifeguard(temp);
            if (last_step)
                return error;  // last step, interceptible error
            panic (error);  // intermediate step, must abrupt panic
        }

        if (Is_Antiform(cast(Atom*, out)))
            assert(not Is_Antiform_Unstable(cast(Atom*, out)));

        ++stackindex;
    }

    Drop_Lifeguard(temp);
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

    if (source == TYPE_GROUP) {
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



// This breaks out the stylized code for calling PICK*, in a Level that
// can be reused across multiple PICK* calls.
//
// The stylization is to reduce the number of C-stack-based cells that need
// to be protected from GC.  Instead, cells are written directly into the
// locations they need to be, with careful orchestration.  (This also means
// less make-work of copying bits around from one location to another.)
//
// 1. SPARE indicates both the LOCATION used for the PICK*, and the output
//    of the PICK* call.  It's a "dual" because for normal values it is
//    a lifted representation--but if it's a non-lifted ACTION! then it is
//    a function to call to do the next PICK* with.  This prevents explosions
//    in cases like (some-struct.million-ints.10), where you don't want the
//    (some-struct.million-ints) pick to give back a BLOCK! of a million
//    INTEGER!s just so you can pick one of them out of it.
//
static Option(Error*) Trap_Call_Pick_Refresh_Dual_In_Spare(  // [1]
    Level* level_,
    Level* sub,  // will Push_Level() if not already pushed
    StackIndex picker_index
){
    Push_Action(sub, LIB(PICK_P));
    Begin_Action(sub, CANON(PICK_P), PREFIX_0);
    Set_Executor_Flag(ACTION, sub, IN_DISPATCH);

    bool picker_was_meta;

    Element* location_arg;
    Element* picker_arg;

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

    // We actually call PICK*, the lower-level function that uses the dual
    // protocol--instead of PICK.  That is because if the pick is not the
    // last pick, it may return an out-of-band function value that we need
    // to use to do the next pick.

    bool threw = Trampoline_With_Top_As_Root_Throws();
    if (threw)  // don't want to return casual error you can TRY from
        panic (Error_No_Catch_For_Throw(sub));

    assert(sub == TOP_LEVEL);
    unnecessary(Drop_Action(sub));  // !! action is dropped, should it be?

    if (not Any_Lifted(SPARE)) {
        if (Is_Nulled(SPARE)) {  // bad pick on final step should be trappable
            Copy_Cell(SPARE, Data_Stack_At(Element, picker_index));
            Drop_Level(sub);

            return Error_Bad_Pick_Raw(Known_Element(SPARE));
        }

        panic ("PICK* (dual protocol) didn't return a lifted value");
    }

    Unliftify_Undecayed(SPARE);  // review efficiency of unlift + lift here

    if (picker_was_meta) {
        Unliftify_Undecayed(SPARE);
        Decay_If_Unstable(SPARE);
    }

    Liftify(SPARE);  // need lifted for dual protocol (review)

    return SUCCESS;
}}


Option(Error*) Trap_Updater_Spare_Is_Dual_Put_Writeback_Dual_In_Spare(
    Level* level_,
    Level* sub,
    StackIndex picker_index,
    Option(Atom*) atom_poke_if_not_on_stack,
    const Value* updater  // possibly POKE_P, or compatible function
){
    Atom* spare_location_dual = SPARE;

    Push_Action(sub, updater);
    Begin_Action(sub, Cell_Frame_Label_Deep(updater), PREFIX_0);
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

            if (atom_poke_if_not_on_stack)
                Copy_Lifted_Cell(value_arg, unwrap atom_poke_if_not_on_stack);
            else {
                Copy_Cell(value_arg, TOP_ELEMENT);
                DROP();
            }

            break;
        }

        Option(Sigil) picker_sigil = Sigil_Of(picker_arg);
        if (picker_sigil == SIGIL_META) {
            if (atom_poke_if_not_on_stack)
                Copy_Lifted_Cell(value_arg, unwrap atom_poke_if_not_on_stack);
            else {
                Copy_Cell(value_arg, TOP_ELEMENT);
                DROP();
            }
            Liftify(value_arg);  // lift it again to be ^META argument
            continue;
        }

        // if not meta, needs to decay if unstable
        Value* stable_poke;
        if (atom_poke_if_not_on_stack) {
            if (Is_Void(unwrap atom_poke_if_not_on_stack)) {
                assert(OUT == unwrap atom_poke_if_not_on_stack);
                Init_Nulled(value_arg);
                continue;  // do not lift dual null
            }

            stable_poke = Decay_If_Unstable(
                unwrap atom_poke_if_not_on_stack
            );
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
        Copy_Lifted_Cell(value_arg, stable_poke);  // lift it to be ^META arg
    }
    then {  // not quoted...
        Plainify(picker_arg);  // drop any sigils
    }

} call_updater: {

    bool threw = Trampoline_With_Top_As_Root_Throws();

    if (threw)  // don't want to return casual error you can TRY from
        panic (Error_No_Catch_For_Throw(TOP_LEVEL));

    return SUCCESS;
}}


//
//  Trap_Update_Var_In_Scratch_With_Out_Uses_Spare_Push_Steps: C
//
// This is centralized code for setting variables.
//
// **Almost all parts of the system should go through this code for assignment,
// even when they know they have just a WORD! in their hand and don't need path
// dispatch.**  Only a few places bypass this code for reasons of optimization,
// but they must do so carefully, because that would skip things like
// accessors (which implement type checking, etc.)
//
Option(Error*) Trap_Update_Var_In_Scratch_With_Out_Uses_Spare_Push_Steps(
    Level* level_,  // SPARE will be overwritten, OUT might be decayed
    bool groups_ok,
    Option(const Value*) updater  //  writes the final step (e.g. POKE*)
){
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

    panic (scratch_var);

  handle_scratch_var_as_wordlike: {

    if (not Try_Get_Binding_Of(spare_location_dual, scratch_var))
        panic ("Couldn't get binding...");

    Liftify(spare_location_dual);  // dual protocol, lift

    Copy_Cell(PUSH(), scratch_var);  // save var for steps + error messages

    Level* sub = Make_End_Level(&Action_Executor, flags);

    if (updater) {
        Atom* atom_poke = OUT;
        possibly(Not_Stable(atom_poke));

        e = Trap_Updater_Spare_Is_Dual_Put_Writeback_Dual_In_Spare(
            level_,
            sub,
            TOP_INDEX,  // picker_index
            atom_poke,
            unwrap updater
        );
        if (e) {
            unnecessary(Drop_Level(sub));  // Call_Poke_P() drops on error
            goto return_error;
        }

        Value* spare_writeback_dual = Known_Stable(SPARE);

        if (not Is_Nulled(spare_writeback_dual))  // only one unit of POKE* !
            panic ("Last POKE* step gave non-null cell writeback bits");
    }
    else {
        e = Trap_Call_Pick_Refresh_Dual_In_Spare(
            level_,
            sub,
            TOP_INDEX  // picker_index
        );
        if (e) {
            unnecessary(Drop_Level(sub));  // Call_Poke_P() drops on error
            goto return_error;
        }
        Copy_Cell(OUT, spare_location_dual);
        Unliftify_Undecayed(OUT);  // already decayed if it was non-meta
    }

    Drop_Level(sub);

    goto return_success;

} handle_scratch_var_as_sequence: {

    // If we have a sequence, then GROUP!s must be evaluated.  (If we're given
    // a steps array as input, then a GROUP! is literally meant as a
    // GROUP! by value).  These evaluations should only be allowed if the
    // caller has asked us to return steps.

    if (not Sequence_Has_Node(scratch_var))  // compressed byte form
        panic (scratch_var);

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
    for (at = head; at != tail; ++at) {
        if (not Is_Group(at)) {  // must keep WORD!s at head as-is for writeback
            possibly(Is_Quoted(at));  // will be interpreted "literally"
            Derelativize(PUSH(), at, at_binding);
            continue;
        }

        if (not groups_ok)
            panic (Error_Bad_Get_Group_Raw(scratch_var));

        if (Eval_Any_List_At_Throws(SPARE, at, at_binding)) {
            Drop_Data_Stack_To(base);
            e = Error_No_Catch_For_Throw(TOP_LEVEL);
            goto finalize_and_return;
        }
        Decay_If_Unstable(SPARE);
        if (Is_Antiform(SPARE))
            panic (Error_Bad_Antiform(SPARE));

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

    possibly(Not_Stable(OUT));

    Option(Atom*) atom_poke_if_not_on_stack = OUT;  // writeback becomes null

    stackindex_top = TOP_INDEX;

  poke_again: { //////////////////////////////////////////////////////////////

    StackIndex stackindex = base + 1;

  do_stack_thing: {

    OnStack(Element*) at = Data_Stack_At(Element, stackindex);
    if (Is_Quoted(at)) {  // don't dereference
        Copy_Cell(spare_location_dual, at);  // dual protocol, leave lifted
    }
    else if (Is_Word(at)) {
        const Value* slot;
        Option(Error*) error = Trap_Lookup_Word(&slot, at, SPECIFIED);
        if (error)
            panic (unwrap error);
        if (Any_Lifted(slot))
            panic ("Can't PICK from a lifted LOCATION");
        if (Is_Antiform(slot)) {
            if (Is_Action(slot))
                QUOTE_BYTE(slot) = NOQUOTE_1;  // (append.series) -> parameter!
            else
                panic (Error_Bad_Antiform(slot));
        }
        Copy_Cell(spare_location_dual, Known_Element(slot));
        Liftify(spare_location_dual);  // dual protocol, lift
    }
    else
        panic (Copy_Cell(SPARE, at));

    ++stackindex;

} calculate_pick_stack_limit:

    StackIndex limit = stackindex_top;
    if (not updater)
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
            unnecessary(Drop_Level(sub));  // Call_Pick_P() drops on error

            if (stackindex == limit - 1)
                goto return_error;  // last step, interceptible error

            panic (unwrap e);  // intermediate step, must abrupt panic
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

    if (not updater) {
        Copy_Cell(OUT, spare_location_dual);
        Unliftify_Undecayed(OUT);  // won't make unstable if wasn't ^META [1]
        goto return_success;
    }

    // This may be the first time we do an update, or it may be a writeback
    // as we go back through the list of steps to update any bits that are
    // required to update in the referencing cells.

    Level* sub = Make_End_Level(&Action_Executor, flags);

    e = Trap_Updater_Spare_Is_Dual_Put_Writeback_Dual_In_Spare(
        level_,
        sub,
        stackindex,  // picker_index
        atom_poke_if_not_on_stack,
        unwrap updater
    );
    if (e) {
        unnecessary(Drop_Level(sub));  // Call_Poke_P() drops on error
        goto return_error;
    }

    Value* spare_writeback_dual = Known_Stable(SPARE);

    Drop_Level(sub);

    // Subsequent updates become pokes, regardless of initial updater function

    if (Is_Nulled(spare_writeback_dual))
        goto return_success;

    if (stackindex_top == base + 1)
        panic ("Last POKE* step in POKE gave non-null writeback instruction");

    assert(Any_Lifted(spare_writeback_dual));  // TBD: writeback actions?
    Copy_Cell(Data_Stack_At(Atom, TOP_INDEX), spare_writeback_dual);
    Unliftify_Known_Stable(TOP);  // must be stable

    possibly(atom_poke_if_not_on_stack == nullptr);
    atom_poke_if_not_on_stack = nullptr;  // signal it's on stack now

    --stackindex_top;

    possibly(updater == LIB(POKE_P));
    updater = LIB(POKE_P);

    goto poke_again;

}} return_error: { ///////////////////////////////////////////////////////////

    assert(e);
    Drop_Data_Stack_To(base);
    goto finalize_and_return;

}} return_success: { //////////////////////////////////////////////////////////

    assert(not e);
    goto finalize_and_return;

} finalize_and_return: { /////////////////////////////////////////////////////

  #if RUNTIME_CHECKS
    Init_Unreadable(SPARE);
    Unprotect_Cell(SCRATCH);
  #endif

    return e;
}}


//
//  Trap_Update_Var_In_Scratch_With_Out_Uses_Spare: C
//
Option(Error*) Trap_Update_Var_In_Scratch_With_Out_Uses_Spare(
    Level* level_,
    Option(Element*) steps_out,  // no GROUP!s if nulled
    Option(const Value*) updater  // function to write last step (e.g. POKE*)
){
    possibly(SPARE == steps_out or SCRATCH == steps_out);

    assert(STATE != STATE_0);  // trampoline rule: OUT only erased if STATE_0

    dont(assert(TOP_INDEX == STACK_BASE));  // Hmmm, why not?
    StackIndex base = TOP_INDEX;

    Option(Error*) e;
    e = Trap_Update_Var_In_Scratch_With_Out_Uses_Spare_Push_Steps(
        level_,
        steps_out != NO_STEPS,
        updater
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
//  Trap_Set_Var_In_Scratch_To_Out_Uses_Spare: C
//
Option(Error*) Trap_Set_Var_In_Scratch_To_Out_Uses_Spare(
    Level* level_,
    Option(Element*) steps_out  // no GROUP!s if nulled
){
    return Trap_Update_Var_In_Scratch_With_Out_Uses_Spare(
        level_,
        steps_out,
        LIB(POKE_P)  // typical "update" step is a complete overwite (a POKE)
    );
}


//
//  Trap_Get_Var_In_Scratch_To_Out_Uses_Spare: C
//
Option(Error*) Trap_Get_Var_In_Scratch_To_Out_Uses_Spare(
    Level* level_,
    Option(Element*) steps_out  // no GROUP!s if nulled
){
  #if RUNTIME_CHECKS
    Init_Unreadable(OUT);  // written, but shouldn't be read
  #endif

    return Trap_Update_Var_In_Scratch_With_Out_Uses_Spare(
        level_,
        steps_out,
        nullptr  // if no updater, then it's a GET
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

    if (target != TYPE_GROUP)  // !!! maybe SET-GROUP!, but GET-GROUP!?
        goto call_generic_set_var;

  process_group_target: { ////////////////////////////////////////////////////

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

    // 1. Plain POKE can't throw (e.g. from GROUP!) because it won't evaluate
    //    them.  However, we can get errors.  Confirm we only are raising
    //    errors unless steps_out were passed.
    //
    // 2. We want parity between (set $x expression) and (x: expression).  It's
    //    very useful that you can write (e: trap [x: expression]) and in the
    //    case of an error, have the assignment skipped and the error trapped.
    //
    //    Note that (set $ ^x fail "hi") will perform a meta-assignment of
    //    the quasiform error to X, but will still pass through the error
    //    antiform as the overall expression result.

    Option(Element*) steps;
    if (Bool_ARG(GROUPS))
        steps = GROUPS_OK;
    else
        steps = nullptr;  // no GROUP! evals

    if (not Bool_ARG(ANY)) {
        // !!! The only SET prohibitions will be on antiform actions, TBD
        // (more general filtering available via accessors)
    }

    Copy_Cell_Core(OUT, lifted_setval, CELL_MASK_THROW);
    Unliftify_Undecayed(OUT);

    Copy_Cell(SCRATCH, target);

    STATE = ST_SET_SETTING;  // we'll be setting out to something not erased

    Option(Error*) e = Trap_Set_Var_In_Scratch_To_Out_Uses_Spare(
        LEVEL, steps
    );
    if (e) {
        assert(steps or Is_Throwing_Panic(LEVEL));  // throws must eval [1]
        return PANIC(unwrap e);
    }

    return OUT;  // even if we don't assign, pass through [2]
}}


//
//  set-accessor: native [
//
//  "Put a function in charge of getting/setting a variable's value"
//
//      return: []
//      var [word!]
//      action [action!]
//  ]
//
DECLARE_NATIVE(SET_ACCESSOR)
//
// 1. While Get_Var()/Set_Var() and their variants are specially written to
//    know about accessors, lower level code is not.  Only code that is
//    sensitive to the fact that the cell contains an accessor should be
//    dealing with the raw cell.  We use the read and write protection
//    abilities to catch violators.
{
    INCLUDE_PARAMS_OF_SET_ACCESSOR;

    Element* word = Element_ARG(VAR);
    Value* action = ARG(ACTION);

    Value* var = Lookup_Mutable_Word_May_Panic(word, SPECIFIED);
    Copy_Cell(var, action);
    Set_Cell_Flag(var, VAR_IS_ACCESSOR);

    Set_Cell_Flag(var, PROTECTED);  // help trap unintentional writes [1]
    Set_Node_Unreadable_Bit(var);  // help trap unintentional reads [1]

    return TRASH;
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
