//
//  file: %n-function.c
//  summary: "Generator for an ACTION! whose body is a block of user code"
//  section: natives
//  project: "Ren-C Language Interpreter and Run-time Environment"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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
// FUNC is a common means for creating an action from a BLOCK! of code, with
// another block serving as the "spec" for parameters and HELP:
//
//     >> print-sum-twice: func [
//            "Prints the sum of two integers, and return the sum"
//            return: "The sum" [integer!]
//            x "First Value" [integer!]
//            y "Second Value" [integer!]
//            <local> sum
//        ][
//            sum: x + y
//            repeat 2 [print ["The sum is" sum]]
//            return sum
//        ]
//
//     >> print-sum-twice 10 20
//     The sum is 30
//     The sum is 30
//
// Ren-C brings new abilities not present in historical Rebol:
//
// * Return-type checking via `return: [...]` in the spec
//
// * Definitional RETURN, so that each FUNC has a local definition of its
//   own version of return specially bound to its invocation.
//
// * Specific binding of arguments, so that each instance of a recursion
//   can discern WORD!s from each recursion.  (In R3-Alpha, this was only
//   possible using CLOSURE which made a costly deep copy of the function's
//   body on every invocation.  Ren-C's method does not require a copy.)
//
// * Functions that (return: [ghost!]) vanish completely, leaving whatever
//   result was in the evaluation previous to the function call as-is.
//
// * Refinements-as-their-own-arguments--which streamlines the evaluator,
//   saves memory, simplifies naming, and simplifies the FRAME! mechanics.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * See LAMBDA for a variant that doesn't include definitional return.
//   (An aspirational goal is that FUNC with definitional return could be
//   built in usermode with LAMBDA.)
//
// * R3-Alpha defined FUNC in terms of MAKE ACTION! on a block.  There was
//   no particular advantage to having an entry point to making functions
//   from a spec and body that put them both in the same block, so FUNC
//   serves as a more logical native entry point for that functionality.
//

#include "sys-core.h"

enum {
    IDX_FUNC_BODY = IDX_INTERPRETED_BODY,
    MAX_IDX_FUNC = IDX_FUNC_BODY
};


//
//  Func_Dispatcher: C
//
// Puts a definitional return ACTION! in the RETURN slot of the frame, and
// runs the body block associated with this function.
//
// (At one time optimized dispatchers for cases like `func [...] []` were
// used, that avoided running empty blocks.  These were deemed to be more
// trouble than they were worth--at least for now--and so a common dispatcher
// is used even for cases that could be optimized.)
//
Bounce Func_Dispatcher(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    enum {
        ST_FUNC_INITIAL_ENTRY = STATE_0,
        ST_FUNC_BODY_EXECUTING
    };

    Details* details = Ensure_Level_Details(L);
    assert(Details_Max(details) == MAX_IDX_FUNC);

    if (THROWING) {  // might be a RETURN:RUN targeting this Level
        assert(STATE == ST_FUNC_BODY_EXECUTING);
        const Value* label = VAL_THROWN_LABEL(L);
        if (
            not Is_Frame(label)
            or Frame_Phase(label) != Frame_Phase(LIB(DEFINITIONAL_REDO))
            or Frame_Coupling(label) != L->varlist
        ){
            return BOUNCE_THROWN;  // wasn't a REDO thrown to this level
        }

        CATCH_THROWN(OUT, level_);

        if (Is_Light_Null(OUT))
            goto redo_with_current_frame_values;

        assert(Heart_Of(OUT) == TYPE_FRAME);
        goto reuse_level_to_run_frame_in_out;
    }

    switch (STATE) {
      case ST_FUNC_INITIAL_ENTRY: goto initial_entry;
      case ST_FUNC_BODY_EXECUTING: goto body_finished_without_returning;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    // 1. Originally, this Dispatcher did not ask to receive throws.  The
    //    DEFINITIONAL-RETURN function did all the work (it has to do type
    //    checking to deliver good errors) and then used a generic UNWIND
    //    supported by the Trampoline.
    //
    //    But realizations about redo mechanics meant that the Dispatchers
    //    had to be complicit.  This eliminated generic REDO, so now it's
    //    a specific aspect of RETURN:RUN's relationship to Func_Dispatcher()
    //    and needs to be caught here.  That could mean that it's better to
    //    give up the generic UNWIND and fold plain RETURN catching in with
    //    RETURN:RUN catching, but that would require multiplexing the signal
    //    of whether to :RUN or not into the thrown value.

    Element* body = Details_Element_At(details, IDX_DETAILS_1);  // code to run
    assert(Is_Block(body) and Series_Index(body) == 0);

    Add_Link_Inherit_Bind(L->varlist, List_Binding(body));
    Force_Level_Varlist_Managed(L);

    Inject_Definitional_Returner(L, LIB(DEFINITIONAL_RETURN), SYM_RETURN);

    STATE = ST_FUNC_BODY_EXECUTING;

    Element* spare = Copy_Cell(SPARE, body);
    Tweak_Cell_Binding(spare, L->varlist);

    Enable_Dispatcher_Catching_Of_Throws(L);  // for RETURN:RUN, not RETURN [1]

    return CONTINUE(OUT, spare);  // body result is discarded

} redo_with_current_frame_values: { //////////////////////////////////////////

    // This will trigger a call back to the evaluator with the same VarList.
    // We will re-enter this dispatcher in the ST_FUNC_INITIAL_ENTRY state.
    //
    // 1. Because tail calls might use existing arguments and locals when
    //    calculating the new call's locals and args, we can only avoid
    //    allocating new memory for the args and locals if we reuse the frame
    //    "as is"--assuming the values of the variables have been loaded with
    //    what the recursion expects.  We still have to reset specialized
    //    values back (including locals) to what a fresh call would have.

    possibly(Link_Inherit_Bind(L->varlist) == nullptr);  // maybe assigned null
    Tweak_Link_Inherit_Bind(L->varlist, nullptr);  // re-entry sets back

    const Key* key_tail;
    const Key* key = Phase_Keys(&key_tail, details);
    L->u.action.key = key;
    L->u.action.key_tail = key_tail;
    Param* param = Phase_Params_Head(details);
    L->u.action.param = param;
    Atom* arg = Level_Args_Head(L);
    L->u.action.arg = arg;
    for (; key != key_tail; ++key, ++arg, ++param) {
        if (Is_Specialized(param)) {  // must reset [1]
          #if DEBUG_POISON_UNINITIALIZED_CELLS
            Poison_Cell(arg);
          #endif
            Blit_Param_Drop_Mark(arg, param);
        }
        else {
            // assume arguments assigned to values desired for recursion
        }
    }

    assert(Get_Executor_Flag(ACTION, L, DISPATCHER_CATCHES));
    Clear_Executor_Flag(ACTION, L, DISPATCHER_CATCHES);

    return BOUNCE_REDO_CHECKED;  // !!! should it clear DISPATCHER_CATCHES?

} reuse_level_to_run_frame_in_out: { /////////////////////////////////////////

    // This form of REDO allocates a new VarList, but reuses the Level.  It
    // will gather new arguments from the callsite (the callsite's feed was
    // captured and reassigned as L's feed before the throw of the REDO).

    Drop_Action(L);

    Value* out = cast(Value*, OUT);

    Restart_Action_Level(L);
    require (
      Push_Action(L, out, PREFIX_0)
    );

    Erase_Cell(OUT);  // invariant for ST_ACTION_INITIAL_ENTRY

    assert(Get_Executor_Flag(ACTION, L, IN_DISPATCH));
    Clear_Executor_Flag(ACTION, L, IN_DISPATCH);

    assert(Get_Executor_Flag(ACTION, L, DISPATCHER_CATCHES));
    Clear_Executor_Flag(ACTION, L, DISPATCHER_CATCHES);

    STATE = ST_ACTION_INITIAL_ENTRY;
    return BOUNCE_CONTINUE;  // define a BOUNCE for this?

} body_finished_without_returning: {  ////////////////////////////////////////

    // 1. If no RETURN is used, the result is TRASH, and typechecking is
    //    performed to make sure TRASH? was a legitimate return.  This has a
    //    little bit of a negative side that if someone is to hook the RETURN
    //    function, it won't be called in these "fallout" cases.  It's deemed
    //    too ugly to slip in a "hidden" call to RETURN for this case, and too
    //    big a hassle to force people to put RETURN ~ or RETURN at the end.
    //    So this is the compromise chosen...at the moment.

    Init_Tripwire(OUT);  // TRASH, regardless of body result [1]

    const Element* param = Quoted_Returner_Of_Paramlist(
        Phase_Paramlist(details), SYM_RETURN
    );

    heeded (Corrupt_Cell_If_Needful(SPARE));
    heeded (Corrupt_Cell_If_Needful(SCRATCH));

    if (not Typecheck_Coerce_Return(L, param, OUT))
        panic (
            "End of function without a RETURN, but ~ not in RETURN: spec"
        );

    return OUT;
}}


//
//  Func_Details_Querier: C
//
bool Func_Details_Querier(
    Sink(Value) out,
    Details* details,
    SymId property
){
    assert(Details_Dispatcher(details) == &Func_Dispatcher);
    assert(Details_Max(details) == MAX_IDX_FUNC);

    switch (property) {

  //=//// RETURN //////////////////////////////////////////////////////////=//

      case SYM_RETURN_OF: {
        Extract_Paramlist_Returner(out, Phase_Paramlist(details), SYM_RETURN);
        return true; }

  //=//// BODY ////////////////////////////////////////////////////////////=//

    // A longstanding idea about FUNC is it could be implemented equivalently
    // in userspace.  So there was an idea about "lying" about what the body
    // of an optimized FUNC is, in order to make it look like what a user
    // would have to write to get equivalent behavior.  To not have holes,
    // this means anything used in the boilerplate can't be hijacked and a
    // debugger would have to simulate it if stepping was requested.
    //
    // 1. See %sysobj.r for STANDARD.FUNC-BODY
    //
    // 2. Index 7 (or 6 in zero-based C) should be #BODY, a "real" body.  To
    //    give it the appearance of executing code in place, we use a GROUP!.

      case SYM_BODY_OF: {
        Element* body = cast(Element*, Details_At(details, IDX_DETAILS_1));

        Slot* std_func_body_slot = Get_System(SYS_STANDARD, STD_FUNC_BODY);

        DECLARE_VALUE (example);
        require (
          Read_Slot(example, std_func_body_slot)
        );

        REBLEN real_body_index = 6;

        Source* fake = cast(Source*, Copy_Array_Shallow_Flags(
            STUB_MASK_MANAGED_SOURCE,
            Cell_Array(example)
        ));

        Element* slot = Array_At(fake, real_body_index);
        assert(Is_Rune(slot));  // should be #BODY [2]

        assert(Series_Index(body) == 0);
        Init_Group(slot, Cell_Array(body));
        Set_Cell_Flag(slot, NEWLINE_BEFORE);

        Init_Block(out, fake);
        return true; }

      default:
        break;
    }

    return false;
}


//
//  Make_Interpreted_Action: C
//
// This digests the spec block into a `paramlist` for parameter descriptions,
// along with an associated `keylist` of the names of the parameters and
// various locals.
//
// The C function dispatcher that is used for the resulting ACTION! varies.
// For instance, if the body is empty then it picks a dispatcher that does
// not bother running the code.  And if there's no return type specified,
// a dispatcher that doesn't check the type is used.
//
// 1. We capture the mutability flag that was in effect when this action was
//    created.  The default FUNC dispatcher takes the body as <const>, but
//    alternatives could be made which did not:
//
//        >> f: func:mutable [] [b: [1 2 3] clear b]]
//
//        >> f
//        == []
//
Result(Details*) Make_Interpreted_Action(
    const Element* spec,
    const Element* body,
    Option(SymId) returner,  // SYM_RETURN, SYM_YIELD, SYM_0 ...
    Dispatcher* dispatcher,
    REBLEN details_capacity
){
    assert(Is_Block(spec) and Is_Block(body));
    assert(details_capacity >= 1);  // relativized body put in details[0]

    VarList* adjunct;
    ParamList* paramlist = Make_Paramlist_Managed(
        &adjunct,
        spec,
        MKF_MASK_NONE,
        returner
    ) except (Error* e) {
        panic (e);
    }

    Details* details = Make_Dispatch_Details(
        BASE_FLAG_MANAGED | DETAILS_FLAG_OWNS_PARAMLIST,
        Phase_Archetype(paramlist),
        dispatcher,
        details_capacity  // we fill in details[0], caller fills any extra
    );

    assert(Misc_Phase_Adjunct(details) == nullptr);
    Tweak_Misc_Phase_Adjunct(details, adjunct);

    Source* copy = Copy_And_Bind_Relative_Deep_Managed(
        body,  // new copy has locals bound relatively to the new action
        details,
        LENS_MODE_ALL_UNSEALED // we created exemplar, see all!
    );

    Option(const Strand*) filename;
    if ((filename = Link_Filename(Cell_Array(spec)))) {  // favor spec
        Tweak_Link_Filename(copy, filename);
        MISC_SOURCE_LINE(copy) = MISC_SOURCE_LINE(Cell_Array(spec));
    }
    else if ((filename = Link_Filename(Cell_Array(body)))) {  // body fallback
        Tweak_Link_Filename(copy, filename);
        MISC_SOURCE_LINE(copy) = MISC_SOURCE_LINE(Cell_Array(body));
    }
    else {
        // Ideally Source arrays should be connected with *some* file and line
    }

    Element* rebound = Init_Block(
        Details_At(details, IDX_INTERPRETED_BODY),
        copy
    );
    Tweak_Cell_Binding(rebound, List_Binding(body));

    if (Get_Cell_Flag(body, CONST))  // capture mutability flag [2]
        Set_Cell_Flag(rebound, CONST);  // Inherit_Const() would need Value*

    return details;
}


//
//  function: native [
//
//  "Generates an ACTION! with RETURN capability"
//
//      return: [action!]
//      spec "Help string (opt) followed by arg words (and opt type + string)"
//          [block!]
//      body "Code implementing function (if no RETURN, evaluates to TRASH!)"
//          [block!]
//  ]
//
DECLARE_NATIVE(FUNCTION)
{
    INCLUDE_PARAMS_OF_FUNCTION;

    Element* spec = Element_ARG(SPEC);
    Element* body = Element_ARG(BODY);

    require (
      Details* details = Make_Interpreted_Action(
        spec,
        body,
        SYM_RETURN,  // has a RETURN: in the paramlist
        &Func_Dispatcher,
        MAX_IDX_FUNC  // archetype and one array slot (will be filled)
    ));

    Init_Action(OUT, details, ANONYMOUS, NONMETHOD);
    return UNSURPRISING(OUT);
}


//
//  Init_Thrown_Unwind_Value: C
//
// This routine generates a thrown signal that can be used to indicate a
// desire to jump to a particular level in the stack with a return value.
// It is used in the implementation of the UNWIND native.
//
// See notes is %sys-frame.h about how there is no actual TYPE_THROWN type.
//
Bounce Init_Thrown_Unwind_Value(
    Level* level_,
    const Value* seek, // FRAME!, ACTION! (or INTEGER! relative to frame)
    const Atom* value,
    Level* target // required if level is INTEGER! or ACTION!
) {
    DECLARE_VALUE (label);
    Copy_Cell(label, LIB(UNWIND));

    if (Is_Frame(seek) and Is_Frame_On_Stack(Cell_Varlist(seek))) {
        g_ts.unwind_level = Level_Of_Varlist_If_Running(Cell_Varlist(seek));
    }
    else if (Is_Frame(seek)) {
        Level* L = target->prior;
        for (; true; L = L->prior) {
            if (L == BOTTOM_LEVEL)
                panic (Error_Invalid_Exit_Raw());

            if (not Is_Action_Level(L))
                continue; // only exit functions

            if (Is_Level_Fulfilling_Or_Typechecking(L))
                continue; // not ready to exit

            if (Frame_Phase(seek) == L->u.action.original) {
                g_ts.unwind_level = L;
                break;
            }
        }
    }
    else {
        assert(Is_Integer(seek));

        REBLEN count = VAL_INT32(seek);
        if (count <= 0)
            panic (Error_Invalid_Exit_Raw());

        Level* L = target->prior;
        for (; true; L = L->prior) {
            if (L == BOTTOM_LEVEL)
                panic (Error_Invalid_Exit_Raw());

            if (not Is_Action_Level(L))
                continue; // only exit functions

            if (Is_Level_Fulfilling_Or_Typechecking(L))
                continue; // not ready to exit

            --count;
            if (count == 0) {
                g_ts.unwind_level = L;
                break;
            }
        }
    }

    Init_Thrown_With_Label(level_, value, label);
    return BOUNCE_THROWN;
}


//
//  unwind: native [
//
//  "Jump up the stack to return from a specific frame or call"
//
//      return: [<divergent>]
//      level "Frame or index to exit from"
//          [frame! integer!]
//      ^result "Result for enclosing state"
//          [any-value?]
//  ]
//
DECLARE_NATIVE(UNWIND)
//
// UNWIND is implemented via a throw that bubbles through the stack.  Using
// UNWIND's action Value with a target `binding` field is the protocol
// understood by Eval_Core to catch a throw itself.
//
// !!! Allowing to pass an INTEGER! to jump from a function based on its
// BACKTRACE number is a bit low-level, and perhaps should be restricted to
// a debugging mode (though it is a useful tool in "code golf").
//
// !!! This might be a little more natural if the label of the throw was a
// FRAME! value.  But that also would mean throws named by frames couldn't be
// taken advantage by the user for other features, while this only takes one
// function away.  (Or, perhaps antiform frames could be used?)
{
    INCLUDE_PARAMS_OF_UNWIND;

    Value* level = ARG(LEVEL);
    Atom* result = Atom_ARG(RESULT);

    return Init_Thrown_Unwind_Value(LEVEL, level, result, level_);
}


//
//  Typecheck_Coerce_Return: C
//
bool Typecheck_Coerce_Return(
    Level* L,  // Level whose spare/scratch used (not necessarily return level)
    const Element* param,  // parameter for the RETURN (may be quoted)
    Atom* atom  // coercion needs mutability
){
  #if NEEDFUL_DOES_CORRUPTIONS
    assert(Not_Cell_Readable(Level_Scratch(L)));
    assert(Not_Cell_Readable(Level_Spare(L)));
  #endif

    assert(  // to be in specialized slot, RETURN can't be a plain PARAMETER!
        Heart_Of(param) == TYPE_PARAMETER
        and (
            LIFT_BYTE(param) == NOQUOTE_2
            or LIFT_BYTE(param) == ONEQUOTE_NONQUASI_4
        )
    );

    if (Is_Error(atom))
        return true;  // For now, all functions return definitional errors

    if (
        Get_Parameter_Flag(param, TRASH_DEFINITELY_OK)
        and Is_Possibly_Unstable_Atom_Trash(atom)
    ){
        return true;  // common case, make fast
    }

    if (Get_Parameter_Flag(param, VOID_DEFINITELY_OK) and Is_Void(atom))
        return true;  // kind of common... necessary?

    if (not Typecheck_Coerce(L, param, atom, true))
        return false;

  determine_if_result_is_surprising: { ///////////////////////////////////////

    // If a function returns a value but doesn't always return a value of
    // that type, we consider it "surprising"...in particular we are concerned
    // with "surprising ghosts" or "surprising functions":
    //
    //   https://rebol.metaeducation.com/t/leaky-ghosts/2437
    //
    // But it's actually faster to just determine if any type is surprising.
    // The information might come in handy somewhere.

    const Array* spec = maybe Parameter_Spec(param);
    if (not spec)
        return true;

    const TypesetByte* optimized = spec->misc.at_least_4;

    if (
        optimized[0] != 0 and optimized[1] == 0  // > 1 type "surprising"
        and Not_Parameter_Flag(param, INCOMPLETE_OPTIMIZATION)  // more in spec
        and optimized[0] == u_cast(Byte, Type_Of(atom))
        and Type_Of(atom) != TYPE_PACK  // all PACK! are potentially surprising
    ){
      #if RUNTIME_CHECKS
        Phase* phase = Level_Phase(L);
        assert(Is_Stub_Details(phase));
        Details* details = u_cast(Details*, phase);
        if (
            Get_Details_Flag(details, RAW_NATIVE)
            and Not_Cell_Flag(atom, OUT_HINT_UNSURPRISING)
            and (Is_Possibly_Unstable_Atom_Action(atom) or Is_Ghost(atom))
            and (phase != Frame_Phase(LIB(DEFINITIONAL_RETURN)))
            and (phase != Frame_Phase(LIB(DEFINITIONAL_YIELD)))
            and (phase != Frame_Phase(LIB(LET)))  // review
            and (phase != Frame_Phase(LIB(SET)))  // review
        ){
            assert(!"NATIVE relies on typechecking for UNSURPRISING flag");
        }
      #endif

        Set_Cell_Flag(atom, OUT_HINT_UNSURPRISING);
    }
    else {
      #if RUNTIME_CHECKS
        Phase* phase = Level_Phase(L);
        assert(Is_Stub_Details(phase));
        Details* details = u_cast(Details*, phase);
        if (
            Get_Details_Flag(details, RAW_NATIVE)
            and Get_Cell_Flag(atom, OUT_HINT_UNSURPRISING)
            and (Is_Possibly_Unstable_Atom_Action(atom) or Is_Ghost(atom))
            and (phase != Frame_Phase(LIB(DEFINITIONAL_RETURN)))
            and (phase != Frame_Phase(LIB(DEFINITIONAL_YIELD)))
            and (phase != Frame_Phase(LIB(LET)))  // review
            and (phase != Frame_Phase(LIB(SET)))  // review
        ){
            assert(!"NATIVE relies on typechecking for SURPRISING flag");
        }
      #endif

        Clear_Cell_Flag(atom, OUT_HINT_UNSURPRISING);
    }

    return true;
}}


//
//  definitional-return: native [
//
//  "RETURN, giving a result to the caller"
//
//      return: [<divergent>]
//      ^value [any-value?]
//      :run "Reuse stack level for another call (<redo> uses locals/args too)"
//      ;   [<variadic> any-stable?]  ; would force this frame managed
//  ]
//
DECLARE_NATIVE(DEFINITIONAL_RETURN)
//
// Returns in Ren-C are functions that are aware of the function they return
// to.  So the dispatchers for functions that provide return e.g. FUNC will
// actually use an instance of this native, and poke a binding into it to
// identify the action.
//
// This means the RETURN that is in LIB is actually a labeled TRASH! to inform
// you that no definitional return is in effect.
//
// 1. The cached name for values holding this native is set to RETURN by the
//    dispatchers that use it, overriding DEFINITIONAL-RETURN, which might
//    seem confusing debugging this.
//
// 2. Check type NOW instead of waiting and having the dispatcher check it.
//    Reasoning is that that lets the error indicate the callsite, e.g. the
//    point where `return badly-typed-value` happened.
//
//    !!! In the userspace formulation of this abstraction, it indicates
//    it's not RETURN's type signature that is constrained, as if it were
//    then RETURN would be implicated in the error.  Instead, RETURN must
//    take [any-value?] as its argument, and then report the error itself...
//    implicating the frame (in a way parallel to this native).
{
    INCLUDE_PARAMS_OF_DEFINITIONAL_RETURN;  // cached name usually RETURN [1]

    Atom* atom = Atom_ARG(VALUE);

    Level* return_level = LEVEL;  // Level of this RETURN call

    Option(VarList*) coupling = Level_Coupling(return_level);
    if (not coupling)
        panic (Error_Archetype_Invoked_Raw());

    possibly(Level_Label(LEVEL) == CANON(RETURN));  // common renaming [1]

    Level* target_level = Level_Of_Varlist_May_Panic(unwrap coupling);
    Details* target_details = Ensure_Level_Details(target_level);

    const Element* param = Quoted_Returner_Of_Paramlist(
        Phase_Paramlist(target_details), SYM_RETURN
    );

    if (not Bool_ARG(RUN)) {  // plain simple RETURN (not weird tail-call)
        heeded (Corrupt_Cell_If_Needful(SPARE));
        heeded (Corrupt_Cell_If_Needful(SCRATCH));

        if (not Typecheck_Coerce_Return(LEVEL, param, atom))  // do it now [2]
            panic (Error_Bad_Return_Type(target_level, atom, param));

        DECLARE_VALUE (label);
        Copy_Cell(label, LIB(UNWIND)); // see Make_Thrown_Unwind_Value
        g_ts.unwind_level = target_level;

        Init_Thrown_With_Label(LEVEL, atom, label);
        return BOUNCE_THROWN;
    }

  //=//// TAIL-CALL HANDLING //////////////////////////////////////////////=//

    // Tail calls are a semi-obscure feature that are included more "just to
    // show we can" vs. actually mattering that much.  They have the negative
    // property of obscuring the actual call stack, which is a reasoning that
    // kept them from being included in Python:
    //
    //   https://en.wikipedia.org/wiki/Tail_call
    //

    const Value* gather_args;

    require (
      Value* v = Decay_If_Unstable(atom)
    );
    if (
        Is_Tag(v)
        and strcmp(cast(char*, Cell_Utf8_At(v)), "redo") == 0
    ){
        gather_args = LIB(NULL);
    }
    else if (Is_Action(v) or Is_Frame(v)) {  // just reuse Level
        gather_args = v;
        Release_Feed(target_level->feed);
        target_level->feed = return_level->feed;
        Add_Feed_Reference(return_level->feed);
    }
    else
        panic ("RETURN:RUN requires action, frame, or <redo> as argument");

    // We need to cooperatively throw a restart instruction up to the level
    // of the frame.  Use DEFINITIONAL-REDO as the throw label that Eval_Core()
    // will identify for that behavior.
    //
    Value* spare = Copy_Cell(SPARE, LIB(DEFINITIONAL_REDO));
    Tweak_Frame_Coupling(  // comment said "may have changed"?
        spare,
        Varlist_Of_Level_Force_Managed(target_level)
    );

    Init_Thrown_With_Label(LEVEL, gather_args, spare);
    return BOUNCE_THROWN;
}


//
//  definitional-redo: native [
//
//  "Internal throw signal used by RETURN:RUN"
//
//      return: [<divergent>]
//  ]
//
DECLARE_NATIVE(DEFINITIONAL_REDO)
//
// It would be possible to multiplex RETURN:RUN's functionality onto the throw
// signal of DEFINITIONAL-RETURN.  It could use CELL_FLAG_NOTE on the thrown
// value (which would be sneaky and lost if the throw mechanics didn't know
// about the flag and copied cells, losing it).  Or it could be part of the
// meta-representational mechanics, where any value that isn't a quoted or
// quasiform was presumed to be a redo signal...and plain RETURN would just
// unlift the result.
//
// However, RETURN has been using a generic UNWIND facility provided by the
// trampoline, which keeps that tested.  It also underscores the fact that
// RETURN has to do the type checking while DEFINITIONAL-RETURN is still on
// the stack, to deliver a good error message.
//
// What to do here can be revisited, but it was necessary to get rid of the
// old signal that was a generic REDO command, because generic REDO no longer
// makes sense.
{
    INCLUDE_PARAMS_OF_DEFINITIONAL_REDO;

    panic ("DEFINITIONAL-REDO should not be called directly");
}
