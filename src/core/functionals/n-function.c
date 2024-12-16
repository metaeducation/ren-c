//
//  File: %n-function.c
//  Summary: "Generator for an ACTION! whose body is a block of user code"
//  Section: natives
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
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
//     >> /print-sum-twice: func [
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
// * Invisible functions (return: [~[]~]) that vanish completely,
//   leaving whatever result was in the evaluation previous to the function
//   call as-is.
//
// * Refinements-as-their-own-arguments--which streamlines the evaluator,
//   saves memory, simplifies naming, and simplifies the FRAME! mechanics.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * R3-Alpha defined FUNC in terms of MAKE ACTION! on a block.  There was
//   no particular advantage to having an entry point to making functions
//   from a spec and body that put them both in the same block, so FUNC
//   serves as a more logical native entry point for that functionality.
//
// * While FUNC is intended to be an optimized native due to its commonality,
//   the belief is still that it should be possible to build an equivalent
//   (albeit slower) version in usermode out of other primitives.  The current
//   plan is that those primitives would be RUNS of a FRAME!, and being able
//   to ADAPT a block of code into that frame.  This makes ADAPT the more
//   foundational operation for fusing interfaces with block bodies.
//

#include "sys-core.h"

enum {
    IDX_FUNC_BODY = IDX_INTERPRETED_BODY,
    IDX_FUNC_MAX
};


//
//  Func_Dispatcher: C
//
// Puts a definitional return ACTION! in the RETURN slot of the frame, and
// runs the body block associated with this function.
//
Bounce Func_Dispatcher(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    enum {
        ST_FUNC_INITIAL_ENTRY = STATE_0,
        ST_FUNC_BODY_EXECUTING
    };

    switch (STATE) {
      case ST_FUNC_INITIAL_ENTRY: goto initial_entry;
      case ST_FUNC_BODY_EXECUTING: goto body_finished_without_returning;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    // 1. One way of handling RETURN would be if this dispatcher asked to
    //    receive throws.  But for one thing, we wouldn't want to do type
    //    checking of the return result in this dispatcher... RETURN needs
    //    to do it so it can deliver the error at the source location where
    //    the return is called, prior ot the throw.
    //
    //    So really all this function would be doing at that point would be
    //    to catch the result.  The Trampoline has a generic UNWIND that
    //    deals with that already.  So long as that exists, then this
    //    dispatcher merely catching a "teleport" would be redundant.

    Details* details = Ensure_Level_Details(L);
    Value* body = Details_At(details, IDX_DETAILS_1);  // code to run
    assert(Is_Block(body) and VAL_INDEX(body) == 0);

    assert(Details_Has_Return(details));  // all FUNC have RETURN
    assert(Key_Id(Phase_Keys_Head(details)) == SYM_RETURN);

    Value* cell = Level_Arg(L, 1);
    assert(Is_Parameter(cell));
    Force_Level_Varlist_Managed(L);
    Init_Action(
        cell,
        Cell_Frame_Phase(LIB(DEFINITIONAL_RETURN)),
        CANON(RETURN),  // relabel (the RETURN in lib is a dummy action)
        cast(VarList*, L->varlist)  // so RETURN knows where to return from
    );

    STATE = ST_FUNC_BODY_EXECUTING;

    Copy_Cell(SPARE, body);
    node_LINK(NextVirtual, L->varlist) = Cell_List_Binding(body);
    BINDING(SPARE) = L->varlist;

    unnecessary(Enable_Dispatcher_Catching_Of_Throws(L));  // RETURN unwind [1]

    return CONTINUE(OUT, stable_SPARE);  // body result is discarded

} body_finished_without_returning: {  ////////////////////////////////////////

    // 1. If no RETURN is used, the result is NOTHING, and typechecking is
    //    performed to make sure NOTHING? was a legitimate return.  This has a
    //    little bit of a negative side that if someone is to hook the RETURN
    //    function, it won't be called in these "fallout" cases.  It's deemed
    //    too ugly to slip in a "hidden" call to RETURN for this case, and too
    //    big a hassle to force people to put RETURN ~ or RETURN at the end.
    //    So this is the compromise chosen...at the moment.

    Init_Nothing(OUT);  // NOTHING, regardless of body result [1]

    Details* details = Ensure_Level_Details(L);

    if (Details_Has_Return(details)) {
        assert(Key_Id(Phase_Keys_Head(details)) == SYM_RETURN);
        const Param* param = Phase_Params_Head(details);

        if (not Typecheck_Coerce_Return_Uses_Spare_And_Scratch(L, param, OUT))
            return FAIL(
                "End of function without a RETURN, but ~ not in RETURN: spec"
            );
    }

    return OUT;
}}


//
//  Make_Interpreted_Action_May_Fail: C
//
// This digests the spec block into a `paramlist` for parameter descriptions,
// along with an associated `keylist` of the names of the parameters and
// various locals.  A separate object that uses the same keylist is made
// which maps the parameters to any descriptions that were in the spec.
//
// Due to the fact that the typesets in paramlists are "lossy" of information
// in the source, another object is currently created as well that maps the
// parameters to the BLOCK! of type information as it appears in the source.
// Attempts are being made to close the gap between that and the paramlist, so
// that separate arrays aren't needed for this closely related information:
//
// https://forum.rebol.info/t/1459
//
// The C function dispatcher that is used for the resulting ACTION! varies.
// For instance, if the body is empty then it picks a dispatcher that does
// not bother running the code.  And if there's no return type specified,
// a dispatcher that doesn't check the type is used.
//
// There is also a "definitional return" MKF_RETURN option used by FUNC, so
// the body will introduce a RETURN specific to each action invocation, thus
// acting more like:
//
//     /return: lambda
//         [{Returns a value from a function.} ^value [any-atom?]]
//         [unwind:with (binding of $return) unmeta value]
//     ]
//     (body goes here)
//
// This pattern addresses "Definitional Return" in a way that does not need to
// build in RETURN as a language keyword in any specific form (in the sense
// that functions do not itself require it).  See the LAMBDA generator for
// an example...where UNWIND can be used to exit frames if you want to build
// something return-like.
//
// FUNC optimizes by not internally building or executing the equivalent body,
// but giving it back from BODY-OF.  This gives FUNC the edge to pretend to
// add containing code and simulate its effects, while really only holding
// onto the body the caller provided.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// 1. At one time there were many optimized dispatchers for cases like
//    `func [...] []` which would not bother running empty blocks, and which
//    did not write into a temporary cell and then copy over the result in
//    a later phase.  The introduction of LAMBDA as an alternative generator
//    made these optimizations give diminishing returns, so they were all
//    eliminated (though they set useful precedent for varying dispatchers).
//
Details* Make_Interpreted_Action_May_Fail(
    const Element* spec,
    const Element* body,
    Flags mkf_flags,  // MKF_RETURN, etc.
    Dispatcher* dispatcher,
    REBLEN details_capacity
){
    assert(Is_Block(spec) and Is_Block(body));
    assert(details_capacity >= 1);  // relativized body put in details[0]

    VarList* meta;
    ParamList* paramlist = Make_Paramlist_Managed_May_Fail(
        &meta,
        spec,
        mkf_flags
    );

    Flags details_flags = DETAILS_FLAG_OWNS_PARAMLIST;
    if (mkf_flags & MKF_RETURN)
        details_flags |= DETAILS_FLAG_PARAMLIST_HAS_RETURN;

    Details* details = Make_Dispatch_Details(
        details_flags,
        Phase_Archetype(paramlist),
        dispatcher,
        details_capacity  // we fill in details[0], caller fills any extra
    );

    assert(Phase_Adjunct(details) == nullptr);
    Tweak_Phase_Adjunct(details, meta);

    Source* copy = Copy_And_Bind_Relative_Deep_Managed(
        body,  // new copy has locals bound relatively to the new action
        details,
        LENS_MODE_ALL_UNSEALED // we created exemplar, see all!
    );

    // Favor the spec first, then the body, for file and line information.
    //
    if (Get_Source_Flag(Cell_Array(spec), HAS_FILE_LINE)) {
        LINK(Filename, copy) = LINK(Filename, Cell_Array(spec));
        copy->misc.line = Cell_Array(spec)->misc.line;
        Set_Source_Flag(copy, HAS_FILE_LINE);
    }
    else if (Get_Source_Flag(Cell_Array(body), HAS_FILE_LINE)) {
        LINK(Filename, copy) = LINK(Filename, Cell_Array(body));
        copy->misc.line = Cell_Array(body)->misc.line;
        Set_Source_Flag(copy, HAS_FILE_LINE);
    }
    else {
        // Ideally all source series should have a file and line numbering
        // At the moment, if a function is created in the body of another
        // function it doesn't work...trying to fix that.
    }

    // Save the relativized body in the action's details block.  Since it is
    // a Cell* and not a Value*, the dispatcher must combine it with a
    // running frame instance (the Level* received by the dispatcher) before
    // executing the interpreted code.
    //
    Cell* rebound = Init_Block(
        Details_At(details, IDX_INTERPRETED_BODY),
        copy
    );
    BINDING(rebound) = Cell_List_Binding(body);

    // Capture the mutability flag that was in effect when this action was
    // created.  This allows the following to work:
    //
    //    >> eval mutable [/f: func [] [b: [1 2 3] clear b]]
    //    >> f
    //    == []
    //
    // So even though the invocation is outside the mutable section, we have
    // a memory that it was created under those rules.  (It's better to do
    // this based on the frame in effect than by looking at the CONST flag of
    // the incoming body block, because otherwise ordinary Ren-C functions
    // whose bodies were created from dynamic code would have mutable bodies
    // by default--which is not a desirable consequence from merely building
    // the body dynamically.)
    //
    // Note: besides the general concerns about mutability-by-default, when
    // functions are allowed to modify their bodies with words relative to
    // their frame, the words would refer to that specific recursion...and not
    // get picked up by other recursions that see the common structure.  This
    // means compatibility would be with the behavior of R3-Alpha CLOSURE,
    // not with R3-Alpha FUNCTION.
    //
    if (Get_Cell_Flag(body, CONST))
        Set_Cell_Flag(rebound, CONST);  // Inherit_Const() would need Value*

    return details;
}


//
//  /function: native [
//
//  "Defines an ACTION! with given spec and body"
//
//      return: [action?]
//      spec "Help string (opt) followed by arg words (and opt type + string)"
//          [block!]
//      body "Code implementing the function--use RETURN to yield a result"
//          [block!]
//  ]
//
DECLARE_NATIVE(function)
{
    INCLUDE_PARAMS_OF_FUNCTION;

    Element* spec = cast(Element*, ARG(spec));
    Element* body = cast(Element*, ARG(body));

    Details* details = Make_Interpreted_Action_May_Fail(
        spec,
        body,
        MKF_RETURN,
        &Func_Dispatcher,
        IDX_FUNC_MAX  // archetype and one array slot (will be filled)
    );

    return Init_Action(OUT, details, ANONYMOUS, UNBOUND);
}


//
//  Init_Thrown_Unwind_Value: C
//
// This routine generates a thrown signal that can be used to indicate a
// desire to jump to a particular level in the stack with a return value.
// It is used in the implementation of the UNWIND native.
//
// See notes is %sys-frame.h about how there is no actual REB_THROWN type.
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
                return FAIL(Error_Invalid_Exit_Raw());

            if (not Is_Action_Level(L))
                continue; // only exit functions

            if (Is_Level_Fulfilling(L))
                continue; // not ready to exit

            if (Cell_Frame_Phase(seek) == L->u.action.original) {
                g_ts.unwind_level = L;
                break;
            }
        }
    }
    else {
        assert(Is_Integer(seek));

        REBLEN count = VAL_INT32(seek);
        if (count <= 0)
            return FAIL(Error_Invalid_Exit_Raw());

        Level* L = target->prior;
        for (; true; L = L->prior) {
            if (L == BOTTOM_LEVEL)
                return FAIL(Error_Invalid_Exit_Raw());

            if (not Is_Action_Level(L))
                continue; // only exit functions

            if (Is_Level_Fulfilling(L))
                continue; // not ready to exit

            --count;
            if (count == 0) {
                g_ts.unwind_level = L;
                break;
            }
        }
    }

    return Init_Thrown_With_Label(level_, value, label);
}


//
//  /unwind: native [
//
//  "Jump up the stack to return from a specific frame or call"
//
//      return: []
//      level "Frame or index to exit from"
//          [frame! integer!]
//      ^result "Result for enclosing state"
//          [any-atom?]
//  ]
//
DECLARE_NATIVE(unwind)
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

    Value* level = ARG(level);

    Copy_Cell(SPARE, ARG(result));  // SPARE can hold unstable isotopes
    Meta_Unquotify_Undecayed(SPARE);

    return Init_Thrown_Unwind_Value(LEVEL, level, SPARE, level_);
}


//
//  Typecheck_Coerce_Return_Uses_Spare_And_Scratch: C
//
bool Typecheck_Coerce_Return_Uses_Spare_And_Scratch(
    Level* L,  // Level whose spare/scratch used (not necessarily return level)
    const Param* param,  // parameter for the RETURN
    Atom* atom  // coercion needs mutability
){
    if (Is_Raised(atom))
        return true;  // For now, all functions return definitional errors

    if (Get_Parameter_Flag(param, NOTHING_DEFINITELY_OK) and Is_Nothing(atom))
        return true;  // common case, make fast

    if (Get_Parameter_Flag(param, NIHIL_DEFINITELY_OK) and Is_Nihil(atom))
        return true;  // kind of common... necessary?

    if (Typecheck_Coerce_Uses_Spare_And_Scratch(L, param, atom, true))
        return true;

    if (Is_Nihil(atom)) {  // RETURN NIHIL
        //
        // !!! Treating a return of NIHIL as a return of NOTHING helps some
        // scenarios, for instance piping UPARSE combinators which do not
        // want to propagate pure invisibility.  The idea should be reviewed
        // to see if VOID makes more sense...but start with a more "ornery"
        // value to see how it shapes up.
        //
        Init_Nothing(atom);
    }

    return Typecheck_Coerce_Uses_Spare_And_Scratch(L, param, atom, true);
}


//
//  /definitional-return: native [
//
//  "RETURN, giving a result to the caller"
//
//      return: []
//      ^atom [any-atom?]
//      :run "Reuse stack level for another call (<redo> uses locals/args too)"
//      ;   [<variadic> any-value?]  ; would force this frame managed
//  ]
//
DECLARE_NATIVE(definitional_return)
//
// Returns in Ren-C are functions that are aware of the function they return
// to.  So the dispatchers for functions that provide return e.g. FUNC will
// actually use an instance of this native, and poke a binding into it to
// identify the action.
//
// This means the RETURN that is in LIB is actually just a dummy function
// which you will bind to and run if there is no definitional return in effect.
//
// 1. The cached name for values holding this native is set to RETURN by the
//    dispatchers that use it, which might seem confusing debugging this.
//
// 2. Check type NOW instead of waiting and having the dispatcher check it.
//    Reasoning is that that lets the error indicate the callsite, e.g. the
//    point where `return badly-typed-value` happened.
//
//    !!! In the userspace formulation of this abstraction, it indicates
//    it's not RETURN's type signature that is constrained, as if it were
//    then RETURN would be implicated in the error.  Instead, RETURN must
//    take [any-atom?] as its argument, and then report the error itself...
//    implicating the frame (in a way parallel to this native).
{
    INCLUDE_PARAMS_OF_DEFINITIONAL_RETURN;  // cached name usually RETURN [1]

    Atom* atom = Copy_Cell(OUT, ARG(atom));  // ARG can't be unstable
    Meta_Unquotify_Undecayed(atom);

    Level* return_level = LEVEL;  // Level of this RETURN call

    Option(VarList*) coupling = Level_Coupling(return_level);
    if (not coupling)
        return FAIL(Error_Archetype_Invoked_Raw());

    Level* target_level = Level_Of_Varlist_May_Fail(unwrap coupling);
    Details* target_phase = Ensure_Level_Details(target_level);
    assert(Details_Has_Return(target_phase));  // continuations can RETURN [1]
    assert(Key_Id(Phase_Keys_Head(target_phase)) == SYM_RETURN);
    const Param* return_param = Phase_Params_Head(target_phase);

    if (not REF(run)) {  // plain simple RETURN (not weird tail-call)
        if (not Typecheck_Coerce_Return_Uses_Spare_And_Scratch(  // do now [2]
            LEVEL, return_param, OUT
        )){
            return FAIL(Error_Bad_Return_Type(target_level, OUT));
        }

        DECLARE_VALUE (label);
        Copy_Cell(label, LIB(UNWIND)); // see Make_Thrown_Unwind_Value
        g_ts.unwind_level = target_level;

        return Init_Thrown_With_Label(LEVEL, OUT, label);
    }

  //=//// TAIL-CALL HANDLING //////////////////////////////////////////////=//

    // Tail calls are a semi-obscure feature that are included more "just to
    // show we can" vs. actually mattering that much.  They have the negative
    // property of obscuring the actual call stack, which is a reasoning that
    // kept them from being included in Python:
    //
    //   https://en.wikipedia.org/wiki/Tail_call
    //
    // 1. The function we are returning from is in the dispatching state, and
    //    the level's state byte can be used by the dispatcher function when
    //    that is the case.  We're pushing the level back to either the
    //    argument-gathering phase (INITIAL_ENTRY) or typechecking phase.
    //    Other flags pertinent to the dispatcher need to be cleared too.
    //
    // 2. Because tail calls might use existing arguments and locals when
    //    calculating the new call's locals and args, we can only avoid
    //    allocating new memory for the args and locals if we reuse the frame
    //    "as is"--assuming the values of the variables have been loaded with
    //    what the recursion expects.  We still have to reset specialized
    //    values back (including locals) to what a fresh call would have.

    const Value* gather_args;

    if (
        Is_Tag(atom)
        and strcmp(c_cast(char*, Cell_Utf8_At(atom)), "redo") == 0
    ){
        Phase* redo_action = target_level->u.action.original;
        const Key* key_tail;
        const Key* key = Phase_Keys(&key_tail, redo_action);
        target_level->u.action.key = key;
        target_level->u.action.key_tail = key_tail;
        Param* param = cast(Param*, Varlist_Slots_Head(Phase_Paramlist(redo_action)));
        target_level->u.action.param = Phase_Params_Head(redo_action);
        Value* arg = Level_Args_Head(target_level);
        target_level->u.action.arg = arg;
        for (; key != key_tail; ++key, ++arg, ++param) {
            if (Is_Specialized(param)) {  // must reset [2]
              #if DEBUG_POISON_UNINITIALIZED_CELLS
                Poison_Cell(arg);
              #endif
                Blit_Param_Drop_Mark(arg, param);
            }
            else {
                // assume arguments assigned to values desired for recursion
            }
        }

        // leave phase as-is... we redo the phase we were in
        // (also if we redid original, note there's no original_binding :-/)

        gather_args = LIB(NULL);
    }
    else if (Is_Action(atom) or Is_Frame(atom)) {  // just reuse Level
        Drop_Action(target_level);

        Restart_Action_Level(target_level);
        Push_Action(target_level, atom);
        Begin_Action(target_level, Cell_Frame_Label(atom), PREFIX_0);

        Release_Feed(target_level->feed);
        target_level->feed = return_level->feed;
        Add_Feed_Reference(return_level->feed);

        Set_Node_Managed_Bit(target_level->varlist);

        gather_args = LIB(OKAY);
    }
    else
        return FAIL("RETURN:RUN requires action, frame, or <redo> as argument");

    // We need to cooperatively throw a restart instruction up to the level
    // of the frame.  Use REDO as the throw label that Eval_Core() will
    // identify for that behavior.
    //
    Copy_Cell(SPARE, LIB(REDO));
    Tweak_Cell_Frame_Coupling(  // comment said "may have changed"?
        SPARE,
        Varlist_Of_Level_Force_Managed(target_level)
    );

    return Init_Thrown_With_Label(LEVEL, gather_args, stable_SPARE);
}
