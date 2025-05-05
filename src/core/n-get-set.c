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
            fail (".field access only in running functions");  // nullptr?
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
// called on "wordlike" sequences (like `.a` or `a/`).  But it should really
// only be called by things like Trap_Get_Tuple(), because there are no
// special adjustments for sequences like `.a`
//
static Option(Error*) Trap_Get_Wordlike_Cell_Maybe_Vacant(
    Sink(Value) out,
    const Element* word,  // sigils ignored (META-WORD! doesn't "meta-get")
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
//  Trap_Get_Tuple_Maybe_Vacant: C
//
// 1. Using a leading dot in a tuple is a cue to look up variables in the
//    object from which a function was dispatched, so `var` and `.var` can
//    look up differently inside a function's body.
//
Option(Error*) Trap_Get_Tuple_Maybe_Vacant(
    Sink(Value) out,
    Option(Value*) steps_out,  // if NULL, then GROUP!s not legal
    const Element* tuple,
    Context* context
){
    assert(Is_Tuple(tuple));

    if (not Sequence_Has_Node(tuple))  // byte compressed
        return Error_User("Cannot GET a numeric tuple");

    bool dot_at_head;  // dot at head means look in coupled context
    DECLARE_ELEMENT (detect);
    Copy_Sequence_At(detect, tuple, 0);
    if (Is_Blank(detect))
        dot_at_head = true;
    else
        dot_at_head = false;

    if (dot_at_head and context)  // avoid adjust if tuple has non-cache binding?
        context = Adjust_Context_For_Coupling(context);

  //=//// HANDLE SIMPLE "WORDLIKE" CASE (.a or a.) ////////////////////////=//

    const Node* node1 = CELL_NODE1(tuple);
    if (Is_Node_A_Cell(node1)) { // pair compressed
        // is considered "Listlike", can answer Cell_List_At()
    }
    else switch (Stub_Flavor(x_cast(Flex*, node1))) {
      case FLAVOR_SYMBOL: {
        Option(Error*) error = Trap_Get_Wordlike_Cell_Maybe_Vacant(
            out,
            tuple,  // optimized "wordlike" representation, like a. or .a
            context
        );
        if (error)
            return error;
        if (steps_out and steps_out != GROUPS_OK) {
            Source* a = Alloc_Singular(FLEX_MASK_MANAGED_SOURCE);
            Derelativize(Stub_Cell(a), tuple, context);
            Init_Any_List(unwrap steps_out, TYPE_THE_BLOCK, a);
        }
        if (dot_at_head and Is_Action(out)) {  // need the coupling
            if (Cell_Frame_Coupling(out) == UNCOUPLED) {
                if (IS_WORD_BOUND(tuple))
                    Tweak_Cell_Frame_Coupling(out, cast(VarList*, Cell_Binding(tuple)));
                else
                    Tweak_Cell_Frame_Coupling(out, cast(VarList*, context));
            }
        }
        return SUCCESS; }

      case FLAVOR_SOURCE:
        break;

      default:
        panic (tuple);
    }

  //=//// PUSH PROCESSED TUPLE ELEMENTS TO STACK //////////////////////////=//

    // The tuple may contain GROUP!s that we evaluate.  Rather than process
    // tuple elements directly, we push their possibly-evaluated elements to
    // the stack.  This way we can share code with the "sequence of steps"
    // formulation of tuple processing.
    //
    // 1. By convention, picker steps quote the first item if it was a GROUP!.
    //    It has to be somehow different because `('a).b` is trying to pick B
    //    out of the WORD! a...not out of what's fetched from A.  So if the
    //    first item of a "steps" block needs to be "fetched" we ^META it.

    StackIndex base = TOP_INDEX;

    const Element* tail;
    const Element* head = Cell_List_At(&tail, tuple);
    const Element* at;
    Context* at_binding = Derive_Binding(context, tuple);
    for (at = head; at != tail; ++at) {
        if (Is_Group(at)) {
            if (not steps_out)
                return Error_User("GET:GROUPS must be used to eval in GET");

            if (Eval_Any_List_At_Throws(cast(Atom*, out), at, at_binding)) {
                Drop_Data_Stack_To(base);
                return Error_No_Catch_For_Throw(TOP_LEVEL);
            }
            Decay_If_Unstable(cast(Atom*, out));

            Move_Cell(PUSH(), out);
            if (at == head)
                Quotify(TOP_ELEMENT);  // signify not literal
        }
        else  // Note: must keep words at head as-is for writeback!
            Derelativize(PUSH(), at, at_binding);
    }

  //=//// CALL COMMON CODE TO RUN CHAIN OF PICKS //////////////////////////=//

    // The behavior of getting a TUPLE! is generalized, and based on PICK.  So
    // in theory, as types in the system are extended, they only need to
    // implement PICK in order to have tuples work with them.

    Option(Error*) error = Trap_Get_From_Steps_On_Stack_Maybe_Vacant(
        out, base
    );
    if (error) {
        Drop_Data_Stack_To(base);
        return error;
    }

    if (steps_out and steps_out != GROUPS_OK) {
        Source* a = Pop_Source_From_Stack(base);
        Init_Any_List(unwrap steps_out, TYPE_THE_BLOCK, a);
    }
    else
        Drop_Data_Stack_To(base);

    return SUCCESS;
}


//
//  Trap_Get_Tuple: C
//
// Convenience wrapper for getting tuples that errors on nothing and tripwires.
//
Option(Error*) Trap_Get_Tuple(
    Sink(Value) out,
    Option(Value*) steps_out,  // if NULL, then GROUP!s not legal
    const Element* tuple,
    Context* context
){
    Option(Error*) error = Trap_Get_Tuple_Maybe_Vacant(
        out, steps_out, tuple, context
    );
    if (error)
        return error;

    if (Any_Vacancy(out))
        return Error_Bad_Word_Get(tuple, out);

    return SUCCESS;
}


//
//  Trap_Get_Var_Maybe_Vacant: C
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
Option(Error*) Trap_Get_Var_Maybe_Vacant(
    Sink(Value) out,
    Option(Value*) steps_out,  // if NULL, then GROUP!s not legal
    const Element* var,
    Context* context
){
    assert(var != cast(Cell*, out));
    assert(steps_out != out);  // Legal for SET, not for GET

    if (Any_Word(var)) {
        Option(Error*) error = Trap_Get_Wordlike_Cell_Maybe_Vacant(
            out, var, context
        );
        if (error)
            return error;

        if (steps_out and steps_out != GROUPS_OK) {
            Derelativize(unwrap steps_out, var, context);
            HEART_BYTE(unwrap steps_out) = TYPE_THE_WORD;
        }
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

            Option(Element*) def = nullptr;  // !!! EMPTY_BLOCK doesn't work?
            bool threw = Specialize_Action_Throws(  // costly, try to avoid [1]
                out, action, def, base
            );
            assert(not threw);  // can only throw if `def`
            UNUSED(threw);
        }

        if (steps_out and steps_out != GROUPS_OK)
            Init_Trash(unwrap steps_out);  // !!! What to return?

        return SUCCESS;
    }

    if (Is_Tuple(var))
        return Trap_Get_Tuple_Maybe_Vacant(
            out, steps_out, var, context
        );

    if (Is_The_Block(var)) {  // "steps"
        StackIndex base = TOP_INDEX;

        Context* at_binding = Derive_Binding(context, var);
        const Element* tail;
        const Element* head = Cell_List_At(&tail, var);
        const Element* at;
        for (at = head; at != tail; ++at)
            Derelativize(PUSH(), at, at_binding);

        Option(Error*) error = Trap_Get_From_Steps_On_Stack_Maybe_Vacant(
            out, base
        );
        Drop_Data_Stack_To(base);

        if (error)
            return error;

        if (steps_out and steps_out != GROUPS_OK)
            Copy_Cell(unwrap steps_out, var);

        return SUCCESS;
    }

    fail (var);
}


//
//  Trap_Get_Var: C
//
// May generate specializations for paths.  See Trap_Get_Var_Maybe_Vacant()
//
Option(Error*) Trap_Get_Var(
    Sink(Value) out,
    Option(Value*) steps_out,  // if nullptr, then GROUP!s not legal
    const Element* var,
    Context* context
){
    Option(Error*) error = Trap_Get_Var_Maybe_Vacant(
        out, steps_out, var, context
    );
    if (error)
        return error;

    if (Any_Vacancy(out))
        return Error_Bad_Word_Get(var, out);

    return SUCCESS;
}


//
//  Get_Var_May_Fail: C
//
// Simplest interface.  Gets a variable, doesn't process groups, and will
// fail if the variable is vacant (holding nothing or a tripwire).  Use the
// appropriate Trap_Get_XXXX() interface if this is too simplistic.
//
Value* Get_Var_May_Fail(
    Sink(Value) out,  // variables never store unstable Atom* values
    const Element* var,
    Context* context
){
    Value* steps_out = nullptr;  // signal groups not allowed to run

    Option(Error*) error = Trap_Get_Var(  // vacant will give error
        out, steps_out, var, context
    );
    if (error)
        fail (unwrap error);

    assert(not Any_Vacancy(out));  // shouldn't have returned it
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
        DECLARE_VALUE (steps);
        Option(Error*) error = Trap_Get_Tuple(  // vacant is error
            out, steps, head, derived
        );
        if (error)
            fail (unwrap error);  // must be abrupt
    }
    else if (Is_Word(head)) {
        Option(Error*) error = Trap_Get_Any_Word(out, head, derived);
        if (error)
            fail (unwrap error);  // must be abrupt
    }
    else
        fail (head);  // what else could it have been?

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
    const Value* at = tail - 1;

    for (; at != head - 1; --at) {
        assert(not Is_Blank(at));  // no internal blanks

        const Value* item = at;
        if (Is_Group(at)) {
            if (Eval_Value_Throws(
                cast(Atom*, spare),
                c_cast(Element*, at),
                Derive_Binding(derived, at)
            )){
                return Error_No_Catch_For_Throw(TOP_LEVEL);
            }
            if (Is_Nihil(cast(Atom*, spare)))
                continue;  // just skip it (voids are ignored, NULLs error)

            item = Decay_If_Unstable(cast(Atom*, spare));

            if (Is_Antiform(item))
                return Error_Bad_Antiform(item);
        }

        if (Is_Word(item)) {
            Init_Pushed_Refinement(PUSH(), Cell_Word_Symbol(item));
        }
        else
            fail (item);
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
        goto ensure_out_is_action;  // will fail, it's not an action

      ensure_out_is_action: { ////////////////////////////////////////////////

        if (Is_Action(out))
            return SUCCESS;
        if (Is_Frame(out)) {
            Actionify(out);
            return SUCCESS;
        }
        fail ("PATH! must retrieve an action or frame");
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
        panic (path);
    }

    const Element* tail;
    const Element* at = Cell_List_At(&tail, path);

    Context* derived = Derive_Binding(context, path);

    if (Is_Blank(at)) {  // leading slash means execute (but we're GET-ing)
        ++at;
        assert(not Is_Blank(at));  // two blanks would be `/` as WORD!
    }

    if (Is_Group(at)) {
        if (Eval_Value_Throws(out, at, derived))
            return Error_No_Catch_For_Throw(TOP_LEVEL);
    }
    else if (Is_Tuple(at)) {
        DECLARE_VALUE (steps);
        Option(Error*) error = Trap_Get_Tuple(  // vacant is error
            out, steps, at, derived
        );
        if (error)
            fail (unwrap error);  // must be abrupt
    }
    else if (Is_Word(at)) {
        Option(Error*) error = Trap_Get_Any_Word(out, at, derived);
        if (error)
            fail (unwrap error);  // must be abrupt
    }
    else if (Is_Chain(at)) {
        if ((at + 1 != tail) and not Is_Blank(at + 1))
            fail ("CHAIN! can only be last item in a path right now");
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
        fail (at);  // what else could it have been?

    ++at;

    if (at == tail or Is_Blank(at))
        goto ensure_out_is_action;

    if (at + 1 != tail and not Is_Blank(at + 1))
        fail ("PATH! can only be two items max at this time");

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
        fail (path);

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
    const Element* word,  // sigils ignored (META-WORD! doesn't "meta-get")
    Context* context
){
    Option(Error*) error = Trap_Get_Wordlike_Cell_Maybe_Vacant(
        out, word, context
    );
    if (error)
        return error;

    if (Any_Vacancy(out))
        return Error_Bad_Word_Get(word, out);

    return SUCCESS;
}


//
//  Trap_Get_Any_Word_Maybe_Vacant: C
//
// High-level: see notes on Trap_Get_Any_Word().  This version just gives back
// "trash" (antiform blank) or "tripwire" (antiform tag) vs. give an error.
//
Option(Error*) Trap_Get_Any_Word_Maybe_Vacant(
    Sink(Value) out,
    const Element* word,  // sigils ignored (META-WORD! doesn't "meta-get")
    Context* context
){
    assert(Any_Word(word));
    return Trap_Get_Wordlike_Cell_Maybe_Vacant(out, word, context);
}


//
//  Trap_Get_From_Steps_On_Stack_Maybe_Vacant: C
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
Option(Error*) Trap_Get_From_Steps_On_Stack_Maybe_Vacant(
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
            fail (unwrap error);
        Copy_Cell(out, slot);
    }
    else
        fail (Copy_Cell(out, at));
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
                | LEVEL_FLAG_RAISED_RESULT_OK,
            CANON(PICK), temp, ins
        )){
            Drop_Data_Stack_To(base);
            Drop_Lifeguard(temp);
            return Error_No_Catch_For_Throw(TOP_LEVEL);
        }

        if (Is_Raised(cast(Atom*, out))) {
            Error* error = Cell_Error(out);  // extract error
            bool last_step = (stackindex == TOP_INDEX);

            Drop_Data_Stack_To(base);  // Note: changes TOP_INDEX
            Drop_Lifeguard(temp);
            if (last_step)
                return error;  // last step, interceptible error
            fail (error);  // intermediate step, must abrupt fail
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
//      return: [any-value? ~[[word! tuple! the-block!] any-value?]~]
//      source "Word or tuple to get, or block of PICK steps (see RESOLVE)"
//          [<maybe> any-word? any-sequence? any-group? the-block!]
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

    Value* steps;
    if (Bool_ARG(STEPS))
        steps = ARG(STEPS);
    else if (Bool_ARG(GROUPS))
        steps = GROUPS_OK;
    else
        steps = nullptr;  // no GROUP! evals

    if (Any_Group(source)) {  // !!! GET-GROUP! makes sense, but SET-GROUP!?
        if (not Bool_ARG(GROUPS))
            return FAIL(Error_Bad_Get_Group_Raw(source));

        if (steps != GROUPS_OK)
            return FAIL("GET on GROUP! with steps doesn't have answer ATM");

        if (Eval_Any_List_At_Throws(SPARE, source, SPECIFIED))
            return FAIL(Error_No_Catch_For_Throw(LEVEL));

        if (Is_Nihil(SPARE))
            return nullptr;  // !!! Is this a good idea, or should it error?

        Decay_If_Unstable(SPARE);

        if (not (
            Any_Word(SPARE) or Any_Sequence(SPARE) or Is_The_Block(SPARE))
        ){
            return FAIL(SPARE);
        }

        source = cast(Element*, SPARE);
    }

    Option(Error*) error = Trap_Get_Var_Maybe_Vacant(
        OUT, steps, source, SPECIFIED
    );
    if (error)
        return RAISE(unwrap error);

    if (not Bool_ARG(ANY))
        if (Any_Vacancy(stable_OUT))
            return RAISE(Error_Bad_Word_Get(source, stable_OUT));

    if (steps and steps != GROUPS_OK) {
        Source* pack = Make_Source_Managed(2);
        Set_Flex_Len(pack, 2);
        Copy_Meta_Cell(Array_At(pack, 0), steps);
        Copy_Meta_Cell(Array_At(pack, 1), stable_OUT);
        return Init_Pack(OUT, pack);
    }

    return OUT;
}




//
//  Set_Var_Core_Updater_Throws: C
//
// This is centralized code for setting variables.  If it returns `true`, the
// out cell will contain the thrown value.  If it returns `false`, the out
// cell will have steps with any GROUP!s evaluated.
//
// It tries to improve efficiency by handling cases that don't need methodized
// calling of POKE up front.  If a frame is needed, then it leverages that a
// frame with pushed cells is available to avoid needing more temporaries.
//
// **Almost all parts of the system should go through this code for assignment,
// even when they know they have just a WORD! in their hand and don't need path
// dispatch.**  Only a few places bypass this code for reasons of optimization,
// but they must do so carefully.
//
// It is legal to have `target == out`.  It means the target may be overwritten
// in the course of the assignment.
//
bool Set_Var_Core_Updater_Throws(
    Sink(Value) spare,  // temp GC-safe location, not used for output
    Option(Value*) steps_out,  // no GROUP!s if nulled
    const Element* var,
    Context* context,
    Atom* poke,  // e.g. L->out (in evaluator, right hand side)
    const Value* updater
){
    possibly(spare == steps_out or var == steps_out);
    assert(spare != poke and var != poke);

    Option(const Value*) setval;
    if (Is_Nihil(poke))
        setval = nullptr;
    else if (Is_Raised(poke))  // for now, skip assign
        return false;
    else
        setval = Decay_If_Unstable(poke);

    DECLARE_ATOM (temp);

    Heart var_heart = Heart_Of_Builtin(var);

    if (Any_Word_Type(var_heart)) {

      set_target:

        if (updater == Mutable_Lib_Var(SYM_POKE_P)) {  // unset poke ok for boot
            //
            // Shortcut past POKE for WORD! (though this subverts hijacking,
            // review that case.)
            //
            if (not setval)
                fail ("Can't poke a plain WORD! with NIHIL at this time");
            Copy_Cell(
                Sink_Word_May_Fail(var, context),
                unwrap setval
            );
        }
        else {
            // !!! This is a hack to try and get things working for PROTECT*.
            // Things are in roughly the right place, but very shaky.  Revisit
            // as BINDING OF is reviewed in terms of answers for LET.
            //
            Derelativize(temp, var, context);
            QUOTE_BYTE(temp) = ONEQUOTE_NONQUASI_3;
            Push_Lifeguard(temp);
            if (rebRunThrows(
                spare,  // <-- output cell
                rebRUN(updater), "binding of", temp, temp,
                    CANON(EITHER), rebL(did setval), rebQ(unwrap setval), "~[]~"
            )){
                Drop_Lifeguard(temp);
                fail (Error_No_Catch_For_Throw(TOP_LEVEL));
            }
            Drop_Lifeguard(temp);
        }

        if (steps_out and steps_out != GROUPS_OK) {
            if (steps_out != var)  // could be true if GROUP eval
                Derelativize(unwrap steps_out, var, context);

            // If the variable is a compressed path form like `a.` then turn
            // it into a plain word.
            //
            HEART_BYTE(unwrap steps_out) = TYPE_WORD;
        }
        return false;  // did not throw
    }

    StackIndex base = TOP_INDEX;

    // If we have a sequence, then GROUP!s must be evaluated.  (If we're given
    // a steps array as input, then a GROUP! is literally meant as a
    // GROUP! by value).  These evaluations should only be allowed if the
    // caller has asked us to return steps.

    if (Any_Sequence_Type(var_heart)) {
        if (not Sequence_Has_Node(var))  // compressed byte form
            fail (var);

        const Node* node1 = CELL_NODE1(var);
        if (Is_Node_A_Cell(node1)) {  // pair optimization
            // pairings considered "Listlike", handled by Cell_List_At()
        }
        else switch (Stub_Flavor(c_cast(Flex*, node1))) {
          case FLAVOR_SYMBOL: {
            if (Get_Cell_Flag(var, LEADING_BLANK)) {  // `/a` or `.a`
                if (var_heart == TYPE_TUPLE)
                    context = Adjust_Context_For_Coupling(context);
                goto set_target;
            }

            // `a/` or `a.`
            //
            // !!! If this is a PATH!, it should error if it's not an action...
            // and if it's a TUPLE! it should error if it is an action.  Review.
            //
            goto set_target; }

          case FLAVOR_SOURCE:
            break;  // fall through

          default:
            panic (var);
        }

        const Element* tail;
        const Element* head = Cell_List_At(&tail, var);
        const Element* at;
        Context* at_binding = Derive_Binding(context, var);
        for (at = head; at != tail; ++at) {
            if (Is_Group(at)) {
                if (not steps_out)
                    fail (Error_Bad_Get_Group_Raw(var));

                if (Eval_Any_List_At_Throws(temp, at, at_binding)) {
                    Drop_Data_Stack_To(base);
                    return true;
                }
                Decay_If_Unstable(temp);
                if (Is_Antiform(temp))
                    fail (Error_Bad_Antiform(temp));

                Move_Cell(PUSH(), cast(Element*, temp));
                if (at == head)
                    Quotify(TOP_ELEMENT);  // signal not literally the head
            }
            else  // Note: must keep WORD!s at head as-is for writeback
                Derelativize(PUSH(), at, at_binding);
        }
    }
    else if (Is_The_Block(var)) {
        const Element* tail;
        const Element* head = Cell_List_At(&tail, var);
        const Element* at;
        Context* at_binding = Derive_Binding(context, var);
        for (at = head; at != tail; ++at)
            Derelativize(PUSH(), at, at_binding);
    }
    else
        fail (var);

    assert(Is_Action(updater));  // we will use rebM() on it

    DECLARE_VALUE (writeback);
    Push_Lifeguard(writeback);

    Init_Unreadable(temp);
    Push_Lifeguard(temp);

    StackIndex stackindex_top = TOP_INDEX;

  poke_again:
  blockscope {
    StackIndex stackindex = base + 1;

  blockscope {
    OnStack(Element*) at = Data_Stack_At(Element, stackindex);
    if (Is_Quoted(at)) {
        Unquotify(Copy_Cell(spare, at));
    }
    else if (Is_Word(at)) {
        const Value* slot;
        Option(Error*) error = Trap_Lookup_Word(
            &slot, cast(Element*, at), SPECIFIED
        );
        if (error)
            fail (unwrap error);
        Copy_Cell(spare, slot);
    }
    else
        fail (Copy_Cell(spare, at));
  }

    ++stackindex;

    // Keep PICK-ing until you come to the last step.

    while (stackindex != stackindex_top) {
        Move_Cell(temp, spare);
        Quotify(Known_Element(temp));
        const Node* ins = rebQ(cast(Value*, Data_Stack_Cell_At(stackindex)));
        if (rebRunThrows(
            spare,  // <-- output cell
            CANON(PICK), temp, ins
        )){
            Drop_Lifeguard(temp);
            Drop_Lifeguard(writeback);
            fail (Error_No_Catch_For_Throw(TOP_LEVEL));  // don't let PICKs throw
        }
        ++stackindex;
    }

    // Now do a the final step, an update (often a poke)

    Move_Cell(temp, spare);
    QuoteByte quote_byte = QUOTE_BYTE(temp);
    QUOTE_BYTE(temp) = ONEQUOTE_NONQUASI_3;
    const Node* ins = rebQ(cast(Value*, Data_Stack_Cell_At(stackindex)));
    assert(Is_Action(updater));
    if (rebRunThrows(
        spare,  // <-- output cell
        rebRUN(updater), temp, ins,
            CANON(EITHER), rebL(did setval), rebQ(unwrap setval), "~[]~"
    )){
        Drop_Lifeguard(temp);
        Drop_Lifeguard(writeback);
        fail (Error_No_Catch_For_Throw(TOP_LEVEL));  // don't let POKEs throw
    }

    // Subsequent updates become pokes, regardless of initial updater function

    updater = LIB(POKE_P);

    if (not Is_Nulled(spare)) {
        Move_Cell(writeback, spare);
        QUOTE_BYTE(writeback) = quote_byte;
        setval = writeback;

        --stackindex_top;

        if (stackindex_top != base + 1)
            goto poke_again;

        // can't use POKE, need to use SET
        if (not Is_Word(Data_Stack_At(Element, base + 1)))
            fail ("Can't POKE back immediate value unless it's to a WORD!");

        if (not setval)
            fail ("Can't writeback POKE immediate with NIHIL at this time");

        Copy_Cell(
            Sink_Word_May_Fail(
                Data_Stack_At(Element, base + 1),
                SPECIFIED
            ),
            unwrap setval
        );
    }
  }

    Drop_Lifeguard(temp);
    Drop_Lifeguard(writeback);

    if (steps_out and steps_out != GROUPS_OK)
        Init_Block(unwrap steps_out, Pop_Source_From_Stack(base));
    else
        Drop_Data_Stack_To(base);

    return false;
}


//
//  Set_Var_Core_Throws: C
//
bool Set_Var_Core_Throws(
    Sink(Value) spare,  // temp GC-safe location, not used for output
    Option(Value*) steps_out,  // no GROUP!s if nulled
    const Element* var,
    Context* context,
    Atom* poke  // e.g. L->out (in evaluator, right hand side)
){
    return Set_Var_Core_Updater_Throws(
        spare,
        steps_out,
        var,
        context,
        poke,
        Mutable_Lib_Var(SYM_POKE_P)  // mutable means unset is okay
    );
}


//
//  Set_Var_May_Fail: C
//
// Simpler function, where GROUP! is not ok...and there's no interest in
// preserving the "steps" to reuse in multiple assignments.
//
void Set_Var_May_Fail(
    const Element* var,
    Context* context,
    Atom* poke
){
    Option(Value*) steps_out = nullptr;

    DECLARE_ATOM (dummy);
    if (Set_Var_Core_Throws(dummy, steps_out, var, context, poke))
        fail (Error_No_Catch_For_Throw(TOP_LEVEL));
}


//
//  set: native [
//
//  "Sets a word or path to specified value (see also: UNPACK)"
//
//      return: "Same value as input (error passthru even it skips the assign)"
//          [any-value?]
//      ^target "Word or tuple, or calculated sequence steps (from GET)"
//          [~[]~ any-word? tuple! any-group?
//          any-get-value? any-set-value? the-block!]  ; should take PACK! [1]
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

    Element* meta_setval = Element_ARG(VALUE);
    Element* meta_target = Element_ARG(TARGET);

    if (Is_Meta_Of_Nihil(meta_target))
        return UNMETA(meta_setval);   // same for SET as [10 = (void): 10]

    Element* target = Unquotify(meta_target);
    if (Is_Chain(target))  // GET-WORD, SET-WORD, SET-GROUP, etc.
        Unchain(target);

    if (not Any_Group(target))  // !!! maybe SET-GROUP!, but GET-GROUP!?
        goto call_generic_set_var;

  process_group_target: { ////////////////////////////////////////////////////

   // !!! At the moment, the generic Set_Var() mechanics aren't written to
   // handle GROUP!s.  But it probably should, since it handles groups that
   // are nested under TUPLE! and such.  Review.

    if (not Bool_ARG(GROUPS))
        return FAIL(Error_Bad_Get_Group_Raw(target));

    if (Eval_Any_List_At_Throws(SPARE, target, SPECIFIED))
        return FAIL(Error_No_Catch_For_Throw(LEVEL));

    if (Is_Nihil(SPARE))
        return UNMETA(meta_setval);

    Decay_If_Unstable(SPARE);

    if (not (
        Any_Word(SPARE) or Any_Sequence(SPARE) or Is_The_Block(SPARE)
    )){
        return FAIL(SPARE);
    }

    Copy_Cell(target, cast(Element*, SPARE));  // update ARG(TARGET)

} call_generic_set_var: { ////////////////////////////////////////////////////

    // 1. Plain POKE can't throw (e.g. from GROUP!) because it won't evaluate
    //    them.  However, we can get errors.  Confirm we only are raising
    //    errors unless steps_out were passed.
    //
    // 2. We want parity between (set $x expression) and (x: expression).  It's
    //    very useful that you can write (e: trap [x: expression]) and in the
    //    case of an error, have the assignment skipped and the error trapped.
    //
    //    Note that (set $ ^x raise "hi") will perform a meta-assignment of
    //    the quasiform error to X, but will still pass through the error
    //    antiform as the overall expression result.

    Value* steps;
    if (Bool_ARG(GROUPS))
        steps = GROUPS_OK;
    else
        steps = nullptr;  // no GROUP! evals

    if (not Bool_ARG(ANY)) {
        // !!! The only SET prohibitions will be on antiform actions, TBD
        // (more general filtering available via accessors)
    }

    Copy_Cell(OUT, meta_setval);
    Meta_Unquotify_Undecayed(OUT);

    if (Set_Var_Core_Throws(SPARE, steps, target, SPECIFIED, OUT)) {
        assert(steps or Is_Throwing_Failure(LEVEL));  // throws must eval [1]
        return THROWN;
    }

    return UNMETA(meta_setval);  // even if we don't assign, pass through [2]
}}


//
//  set-accessor: native [
//
//  "Put a function in charge of getting/setting a variable's value"
//
//      return: [~]
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

    Value* var = Lookup_Mutable_Word_May_Fail(word, SPECIFIED);
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
//      return: [~null~ object!]
//  ]
//
DECLARE_NATIVE(DOT_1)
{
    INCLUDE_PARAMS_OF_DOT_1;

    Context* coupling = Adjust_Context_For_Coupling(Level_Binding(LEVEL));
    if (not coupling)
        return RAISE("No current coupling in effect");

    return Init_Object(OUT, cast(VarList*, coupling));
}
