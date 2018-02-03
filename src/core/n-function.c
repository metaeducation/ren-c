//
//  File: %n-function.c
//  Summary: "native functions for creating and interacting with functions"
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Ren-C follows a concept of a single FUNCTION! type, instead of the
// subcategories from Rebol2 and R3-Alpha.  This simplifies matters from the
// user's point of view, and also moves to the idea of a different C native
// "dispatcher" functions which are attached to the function's definition
// itself.  Not only does this allow a variety of performant customized
// native dispatchers, but having the dispatcher accessed through an indirect
// pointer instead of in the function REBVALs themselves lets them be
// dynamically changed.  This is used by HIJACK and by user natives.
//

#include "sys-core.h"

//
//  func: native [
//
//  "Defines a user function with given spec and body."
//
//      return: [function!]
//      spec [block!]
//          {Help string (opt) followed by arg words (and opt type + string)}
//      body [block!]
//          "The body block of the function"
//  ]
//
REBNATIVE(func)
//
// Native optimized implementation of a "definitional return" function
// generator.  See comments on Make_Function_May_Fail for full notes.
{
    INCLUDE_PARAMS_OF_FUNC;

    REBFUN *fun = Make_Interpreted_Function_May_Fail(
        ARG(spec), ARG(body), MKF_RETURN | MKF_KEYWORDS
    );

    Move_Value(D_OUT, FUNC_VALUE(fun));
    return R_OUT;
}


//
//  proc: native [
//
//  "Defines a user function with given spec and body and no return result."
//
//      return: [function!]
//      spec [block!]
//          {Help string (opt) followed by arg words (and opt type + string)}
//      body [block!]
//          "The body block of the function, use LEAVE to exit"
//  ]
//
REBNATIVE(proc)
//
// Short for "PROCedure"; inspired by the Pascal language's discernment in
// terminology of a routine that returns a value vs. one that does not.
// Provides convenient interface similar to FUNC that will not accidentally
// leak values to the caller.
{
    INCLUDE_PARAMS_OF_PROC;

    REBFUN *fun = Make_Interpreted_Function_May_Fail(
        ARG(spec), ARG(body), MKF_LEAVE | MKF_KEYWORDS
    );

    Move_Value(D_OUT, FUNC_VALUE(fun));
    return R_OUT;
}


//
//  Make_Thrown_Exit_Value: C
//
// This routine will generate a THROWN() value that can be used to indicate
// a desire to exit from a particular level in the stack with a value (or void)
//
// It is used in the implementation of the EXIT native.
//
void Make_Thrown_Exit_Value(
    REBVAL *out,
    const REBVAL *level, // FRAME!, FUNCTION! (or INTEGER! relative to frame)
    const REBVAL *value,
    REBFRM *frame // required if level is INTEGER! or FUNCTION!
) {
    Move_Value(out, NAT_VALUE(exit));

    if (IS_FRAME(level)) {
        INIT_BINDING(out, VAL_CONTEXT(level));
    }
    else if (IS_INTEGER(level)) {
        REBCNT count = VAL_INT32(level);
        if (count <= 0)
            fail (Error_Invalid_Exit_Raw());

        REBFRM *f = frame->prior;
        for (; TRUE; f = f->prior) {
            if (f == NULL)
                fail (Error_Invalid_Exit_Raw());

            if (NOT(Is_Function_Frame(f)))
                continue; // only exit functions

            if (Is_Function_Frame_Fulfilling(f))
                continue; // not ready to exit

            --count;
            if (count == 0) {
                INIT_BINDING(out, f);
                break;
            }
        }
    }
    else {
        assert(IS_FUNCTION(level));

        REBFRM *f = frame->prior;
        for (; TRUE; f = f->prior) {
            if (f == NULL)
                fail (Error_Invalid_Exit_Raw());

            if (NOT(Is_Function_Frame(f)))
                continue; // only exit functions

            if (Is_Function_Frame_Fulfilling(f))
                continue; // not ready to exit

            if (VAL_FUNC(level) == f->original) {
                INIT_BINDING(out, f);
                break;
            }
        }
    }

    CONVERT_NAME_TO_THROWN(out, value);
}


//
//  exit: native [
//
//  {Leave enclosing function, or jump /FROM.}
//
//      /with
//          "Result for enclosing state (default is no value)"
//      value [any-value!]
//      /from
//          "Jump the stack to return from a specific frame or call"
//      level [frame! function! integer!]
//          "Frame, function, or stack index to exit from"
//  ]
//
REBNATIVE(exit)
//
// EXIT is implemented via a THROWN() value that bubbles up through the stack.
// Using EXIT's function REBVAL with a target `binding` field is the
// protocol understood by Do_Core to catch a throw itself.
//
// !!! Allowing to pass an INTEGER! to exit from a function based on its
// BACKTRACE number is a bit low-level, and perhaps should be restricted to
// a debugging mode (though it is a useful tool in "code golf").
{
    INCLUDE_PARAMS_OF_EXIT;

    UNUSED(REF(with)); // implied by non-void value

    if (NOT(REF(from)))
        Init_Integer(ARG(level), 1); // default--exit one function stack level

    Make_Thrown_Exit_Value(D_OUT, ARG(level), ARG(value), frame_);

    return R_OUT_IS_THROWN;
}


//
//  return: native [
//
//  "Returns a value from a function."
//
//      value [<opt> any-value!]
//  ]
//
REBNATIVE(return)
{
    INCLUDE_PARAMS_OF_RETURN;

    REBFRM *f = frame_; // implicit parameter to REBNATIVE()

    // The frame this RETURN is being called from may well not be the target
    // function of the return (that's why it's a "definitional return").  The
    // binding field of the frame contains a copy of whatever the binding was
    // in the specific FUNCTION! value that was invoked.
    //
    REBFRM *target_frame;
    if (IS_CELL(f->binding)) {
        target_frame = cast(REBFRM*, f->binding);
    }
    else if (f->binding->header.bits & ARRAY_FLAG_VARLIST) {
        target_frame = CTX_FRAME_MAY_FAIL(CTX(f->binding));
    }
    else {
        assert(f->binding == UNBOUND);
        fail (Error_Return_Archetype_Raw());
    }

    // !!! We only have a REBFRM via the binding.  We don't have distinct
    // knowledge about exactly which "phase" the original RETURN was
    // connected to.  As a practical matter, it can only return from the
    // current phase (what other option would it have, any other phase is
    // either not running yet or has already finished!).  But this means the
    // `target_frame->phase` may be somewhat incidental to which phase the
    // RETURN originated from...and if phases were allowed different return
    // typesets, then that means the typechecking could be somewhat random.
    //
    // Without creating a unique tracking entity for which phase was
    // intended for the return, it's not known which phase the return is
    // for.  So the return type checking is done on the basis of the
    // underlying function.  So compositions that share frames cannot expand
    // the return type set.  The unfortunate upshot of this is--for instance--
    // that an ENCLOSE'd function can't return any types the original function
    // could not.  :-(
    //
    REBFUN *target_fun = FRM_UNDERLYING(target_frame);

    // If it's a definitional return, the associated function's frame must
    // have a SYM_RETURN in it, which is also a local.  The trick used is
    // that the type bits in that local are used to store the legal types
    // for the return value.
    //
    REBVAL *typeset = FUNC_PARAM(target_fun, FUNC_NUM_PARAMS(target_fun));
    assert(VAL_PARAM_SYM(typeset) == SYM_RETURN);

    // Check the type *NOW* instead of waiting and letting Do_Core() check it.
    // The reasoning is that this way, the error will indicate the callsite,
    // e.g. the point where `return badly-typed-value` happened.
    //
    // !!! In the userspace formulation of this abstraction, it indicates that
    // it's not RETURN's type signature that is constrained, as if it were
    // then RETURN would be implicated in the error.  Instead, RETURN must
    // take [<opt> any-value!] as its argument, and then do the error report
    // itself...implicating the frame (in a way parallel to this native).
    //
    REBVAL *value = ARG(value);
    if (!TYPE_CHECK(typeset, VAL_TYPE(value)))
        fail (Error_Bad_Return_Type(target_frame, VAL_TYPE(value)));

    Move_Value(D_OUT, NAT_VALUE(exit)); // see also Make_Thrown_Exit_Value
    INIT_BINDING(D_OUT, f->binding);

    CONVERT_NAME_TO_THROWN(D_OUT, value);
    return R_OUT_IS_THROWN;
}


//
//  leave: native [
//
//  "Leaves a procedure, giving no result to the caller."
//
//  ]
//
REBNATIVE(leave)
//
// See notes on REBNATIVE(return)
{
    if (frame_->binding == UNBOUND) // raw native, not variant PROCEDURE made
        fail (Error_Return_Archetype_Raw());

    Move_Value(D_OUT, NAT_VALUE(exit)); // see also Make_Thrown_Exit_Value
    INIT_BINDING(D_OUT, frame_->binding);

    CONVERT_NAME_TO_THROWN(D_OUT, VOID_CELL);
    return R_OUT_IS_THROWN;
}


//
//  typechecker: native [
//
//  {Function generator for an optimized typechecking routine.}
//
//      return: [function!]
//      type [datatype! typeset!]
//  ]
//
REBNATIVE(typechecker)
{
    INCLUDE_PARAMS_OF_TYPECHECKER;

    REBVAL *type = ARG(type);

    REBARR *paramlist = Make_Array_Core(2, ARRAY_FLAG_PARAMLIST);

    REBVAL *archetype = Alloc_Tail_Array(paramlist);
    VAL_RESET_HEADER(archetype, REB_FUNCTION);
    archetype->payload.function.paramlist = paramlist;
    INIT_BINDING(archetype, UNBOUND);

    REBVAL *param = Alloc_Tail_Array(paramlist);
    Init_Typeset(param, ALL_64, Canon(SYM_VALUE));
    INIT_VAL_PARAM_CLASS(param, PARAM_CLASS_NORMAL);

    MANAGE_ARRAY(paramlist);

    LINK(paramlist).facade = paramlist;

    // for now, no help...use REDESCRIBE
    //
    MISC(paramlist).meta = NULL;

    REBFUN *fun = Make_Function(
        paramlist,
        IS_DATATYPE(type)
            ? &Datatype_Checker_Dispatcher
            : &Typeset_Checker_Dispatcher,
        NULL, // no facade (use paramlist)
        NULL // no specialization exemplar (or inherited exemplar)
    );

    Move_Value(FUNC_BODY(fun), type);

    Move_Value(D_OUT, FUNC_VALUE(fun));

    return R_OUT;
}


//
//  specialize: native [
//
//  {Create a new function through partial or full specialization of another}
//
//      return: [function!]
//      specializee [function! any-word! any-path!]
//          {Function or specifying word (preserves word name for debug info)}
//      def [block!]
//          {Definition for FRAME! fields for args and refinements}
//  ]
//
REBNATIVE(specialize)
{
    INCLUDE_PARAMS_OF_SPECIALIZE;

    REBVAL *specializee = ARG(specializee);

    REBSTR *opt_name;
    Get_If_Word_Or_Path_Arg(D_OUT, &opt_name, specializee);
    if (!IS_FUNCTION(D_OUT))
        fail (specializee);
    Move_Value(specializee, D_OUT);

    if (Specialize_Function_Throws(D_OUT, specializee, opt_name, ARG(def)))
        return R_OUT_IS_THROWN;

    return R_OUT;
}


//
//  chain: native [
//
//  {Create a processing pipeline of functions that consume the last's result}
//
//      return: [function!]
//      pipeline [block!]
//          {List of functions to apply.  Reduced by default.}
//      /quote
//          {Do not reduce the pipeline--use the values as-is.}
//  ]
//
REBNATIVE(chain)
{
    INCLUDE_PARAMS_OF_CHAIN;

    REBVAL *out = D_OUT; // plan ahead for factoring into Chain_Function(out..

    REBVAL *pipeline = ARG(pipeline);
    REBARR *chainees;
    if (REF(quote)) {
        chainees = COPY_ANY_ARRAY_AT_DEEP_MANAGED(pipeline);
    }
    else {
        if (Reduce_Any_Array_Throws(out, pipeline, REDUCE_FLAG_DROP_BARS))
            return R_OUT_IS_THROWN;

        chainees = VAL_ARRAY(out); // should be all specific values
        ASSERT_ARRAY_MANAGED(chainees);
    }

    REBVAL *first = KNOWN(ARR_HEAD(chainees));

    // !!! Current validation is that all are functions.  Should there be other
    // checks?  (That inputs match outputs in the chain?)  Should it be
    // a dialect and allow things other than functions?
    //
    REBVAL *check = first;
    while (NOT_END(check)) {
        if (!IS_FUNCTION(check))
            fail (check);
        ++check;
    }

    // The paramlist needs to be unique to designate this function, but
    // will be identical typesets to the first function in the chain.  It's
    // [0] element must identify the function we're creating vs the original,
    // however.
    //
    REBARR *paramlist = Copy_Array_Shallow(
        VAL_FUNC_PARAMLIST(ARR_HEAD(chainees)), SPECIFIED
    );
    ARR_HEAD(paramlist)->payload.function.paramlist = paramlist;
    SET_SER_FLAG(paramlist, ARRAY_FLAG_PARAMLIST);
    MANAGE_ARRAY(paramlist);

    // Initialize the "meta" information, which is used by HELP.  Because it
    // has a link to the "chainees", it is not necessary to copy parameter
    // descriptions...HELP can follow the link and find the information.
    //
    // See %sysobj.r for `chained-meta:` object template
    //
    // !!! There could be a system for preserving names in the chain, by
    // accepting lit-words instead of functions--or even by reading the
    // GET-WORD!s in the block.  Consider for the future.
    //
    REBVAL *std_meta = Get_System(SYS_STANDARD, STD_CHAINED_META);
    REBCTX *meta = Copy_Context_Shallow(VAL_CONTEXT(std_meta));
    Init_Void(CTX_VAR(meta, STD_CHAINED_META_DESCRIPTION)); // default
    Init_Block(CTX_VAR(meta, STD_CHAINED_META_CHAINEES), chainees);
    Init_Void(CTX_VAR(meta, STD_CHAINED_META_CHAINEE_NAMES));
    MANAGE_ARRAY(CTX_VARLIST(meta));
    MISC(paramlist).meta = meta; // must initialize before Make_Function

    REBFUN *fun = Make_Function(
        paramlist,
        &Chainer_Dispatcher,
        FUNC_FACADE(VAL_FUNC(first)), // same interface as first function
        FUNC_EXEMPLAR(VAL_FUNC(first)) // same exemplar as first function
    );

    // "body" is the chainees array, available to the dispatcher when called
    //
    Init_Block(FUNC_BODY(fun), chainees);

    Move_Value(D_OUT, FUNC_VALUE(fun));
    assert(VAL_BINDING(D_OUT) == UNBOUND);

    return R_OUT;
}


//
//  adapt: native [
//
//  {Create a variant of a function that preprocesses its arguments}
//
//      return: [function!]
//      adaptee [function! any-word! any-path!]
//          {Function or specifying word (preserves word name for debug info)}
//      prelude [block!]
//          {Code to run in constructed frame before adapted function runs}
//  ]
//
REBNATIVE(adapt)
{
    INCLUDE_PARAMS_OF_ADAPT;

    REBVAL *adaptee = ARG(adaptee);

    REBSTR *opt_adaptee_name;
    Get_If_Word_Or_Path_Arg(D_OUT, &opt_adaptee_name, adaptee);
    if (!IS_FUNCTION(D_OUT))
        fail (adaptee);
    Move_Value(adaptee, D_OUT);

    // For the binding to be correct, the indices that the words use must be
    // the right ones for the frame pushed.  So if you adapt a specialization
    // that has one parameter, and the function that underlies that has
    // 10 parameters and the one parameter you're adapting to is it's 10th
    // and not its 1st...that has to be taken into account.
    //
    // Hence you must bind relative to that deeper function...e.g. the function
    // behind the frame of the specialization which gets pushed.
    //
    REBFUN *underlying = FUNC_UNDERLYING(VAL_FUNC(adaptee));

    // !!! In a future branch it may be possible that specific binding allows
    // a read-only input to be "viewed" with a relative binding, and no copy
    // would need be made if input was R/O.  For now, we copy to relativize.
    //
    REBARR *prelude = Copy_And_Bind_Relative_Deep_Managed(
        ARG(prelude),
        FUNC_PARAMLIST(underlying),
        TS_ANY_WORD
    );

    // The paramlist needs to be unique to designate this function, but
    // will be identical typesets to the original.  It's [0] element must
    // identify the function we're creating vs the original, however.
    //
    REBARR *paramlist = Copy_Array_Shallow(
        VAL_FUNC_PARAMLIST(adaptee), SPECIFIED
    );
    ARR_HEAD(paramlist)->payload.function.paramlist = paramlist;
    SET_SER_FLAG(paramlist, ARRAY_FLAG_PARAMLIST);
    MANAGE_ARRAY(paramlist);

    // See %sysobj.r for `adapted-meta:` object template

    REBVAL *example = Get_System(SYS_STANDARD, STD_ADAPTED_META);

    REBCTX *meta = Copy_Context_Shallow(VAL_CONTEXT(example));
    Init_Void(CTX_VAR(meta, STD_ADAPTED_META_DESCRIPTION)); // default
    Move_Value(CTX_VAR(meta, STD_ADAPTED_META_ADAPTEE), adaptee);
    if (opt_adaptee_name == NULL)
        Init_Void(CTX_VAR(meta, STD_ADAPTED_META_ADAPTEE_NAME));
    else
        Init_Word(
            CTX_VAR(meta, STD_ADAPTED_META_ADAPTEE_NAME),
            opt_adaptee_name
        );

    MANAGE_ARRAY(CTX_VARLIST(meta));
    MISC(paramlist).meta = meta;

    REBFUN *fun = Make_Function(
        paramlist,
        &Adapter_Dispatcher,
        FUNC_FACADE(VAL_FUNC(adaptee)), // same interface as adaptee
        FUNC_EXEMPLAR(VAL_FUNC(adaptee)) // same exemplar as adaptee
    );

    // We need to store the 2 values describing the adaptation so that the
    // dispatcher knows what to do when it gets called and inspects FUNC_BODY.
    //
    // [0] is the prelude BLOCK!, [1] is the FUNCTION! we've adapted.
    //
    REBARR *adaptation = Make_Array(2);

    REBVAL *block = Alloc_Tail_Array(adaptation);
    VAL_RESET_HEADER(block, REB_BLOCK);
    INIT_VAL_ARRAY(block, prelude);
    VAL_INDEX(block) = 0;
    INIT_BINDING(block, underlying); // relative binding

    Append_Value(adaptation, adaptee);

    RELVAL *body = FUNC_BODY(fun);
    VAL_RESET_HEADER(body, REB_BLOCK);
    INIT_VAL_ARRAY(body, adaptation);
    VAL_INDEX(body) = 0;
    INIT_BINDING(body, underlying); // relative binding
    MANAGE_ARRAY(adaptation);

    Move_Value(D_OUT, FUNC_VALUE(fun));
    assert(VAL_BINDING(D_OUT) == UNBOUND);

    return R_OUT;
}


//
//  enclose: native [
//
//  {Wrap code around a FUNCTION! with access to its FRAME! and return value}
//
//      return: [function!]
//      inner [function! any-word! any-path!]
//          {Function that a FRAME! will be built for (and optionally called)}
//      outer [function! any-word! any-path!]
//          {Gets a FRAME! for INNER before invocation, can DO it (or not)}
//  ]
//
REBNATIVE(enclose)
{
    INCLUDE_PARAMS_OF_ENCLOSE;

    REBVAL *inner = ARG(inner);
    REBVAL *outer = ARG(outer);

    REBSTR *opt_inner_name;
    Get_If_Word_Or_Path_Arg(D_OUT, &opt_inner_name, inner);
    if (!IS_FUNCTION(D_OUT))
        fail (inner);
    Move_Value(inner, D_OUT);

    REBSTR *opt_outer_name;
    Get_If_Word_Or_Path_Arg(D_OUT, &opt_outer_name, outer);
    if (!IS_FUNCTION(D_OUT))
        fail (outer);
    Move_Value(outer, D_OUT);

    // The paramlist needs to be unique to designate this function, but
    // will be identical typesets to the inner.  It's [0] element must
    // identify the function we're creating vs the original, however.
    //
    REBARR *paramlist = Copy_Array_Shallow(
        VAL_FUNC_PARAMLIST(inner), SPECIFIED
    );
    ARR_HEAD(paramlist)->payload.function.paramlist = paramlist;
    SET_SER_FLAG(paramlist, ARRAY_FLAG_PARAMLIST);
    MANAGE_ARRAY(paramlist);

    // See %sysobj.r for `enclosed-meta:` object template

    REBVAL *example = Get_System(SYS_STANDARD, STD_ENCLOSED_META);

    REBCTX *meta = Copy_Context_Shallow(VAL_CONTEXT(example));
    Init_Void(CTX_VAR(meta, STD_ENCLOSED_META_DESCRIPTION)); // default
    Move_Value(CTX_VAR(meta, STD_ENCLOSED_META_INNER), inner);
    if (opt_inner_name == NULL)
        Init_Void(CTX_VAR(meta, STD_ENCLOSED_META_INNER_NAME));
    else
        Init_Word(
            CTX_VAR(meta, STD_ENCLOSED_META_INNER_NAME),
            opt_inner_name
        );
    Move_Value(CTX_VAR(meta, STD_ENCLOSED_META_OUTER), outer);
    if (opt_outer_name == NULL)
        Init_Void(CTX_VAR(meta, STD_ENCLOSED_META_OUTER_NAME));
    else
        Init_Word(
            CTX_VAR(meta, STD_ENCLOSED_META_OUTER_NAME),
            opt_outer_name
        );

    MANAGE_ARRAY(CTX_VARLIST(meta));
    MISC(paramlist).meta = meta;

    REBFUN *fun = Make_Function(
        paramlist,
        &Encloser_Dispatcher,
        FUNC_FACADE(VAL_FUNC(inner)), // same interface as inner
        FUNC_EXEMPLAR(VAL_FUNC(inner)) // same exemplar as inner
    );

    // We need to store the 2 values describing the enclosure so that the
    // dispatcher knows what to do when it gets called and inspects FUNC_BODY.
    //
    // [0] is the inner FUNCTION!, [2] is the outer FUNCTION!
    //
    REBARR *enclosure = Make_Array(2);
    Append_Value(enclosure, inner);
    Append_Value(enclosure, outer);

    Init_Block(FUNC_BODY(fun), enclosure);

    Move_Value(D_OUT, FUNC_VALUE(fun));
    assert(VAL_BINDING(D_OUT) == UNBOUND);

    return R_OUT;
}


//
//  hijack: native [
//
//  {Cause all existing references to a function to invoke another function.}
//
//      return: [function! blank!]
//          {The hijacked function value, blank if self-hijack (no-op).}
//      victim [function! any-word! any-path!]
//          {Function value whose references are to be affected.}
//      hijacker [function! any-word! any-path!]
//          {The function to run in its place.}
//  ]
//
REBNATIVE(hijack)
//
// The HIJACK operation replaces one function completely with another, such
// that references to the old function value will now call a new one.
//
// Hijacking a function does not change its interface--and cannot.  While
// it may seem tempting to use low-level tricks to keep the same paramlist
// but add or remove parameters, parameter lists can be referenced many
// places in the system (frames, specializations, adaptations) and can't
// be corrupted...or the places that rely on their properties (number and
// types of parameters) would get out of sync.
//
{
    INCLUDE_PARAMS_OF_HIJACK;

    DECLARE_LOCAL (victim);
    REBSTR *opt_victim_name;
    Get_If_Word_Or_Path_Arg(victim, &opt_victim_name, ARG(victim));
    if (!IS_FUNCTION(victim))
        fail ("Victim of HIJACK must be a FUNCTION!");

    DECLARE_LOCAL (hijacker);
    REBSTR *opt_hijacker_name;
    Get_If_Word_Or_Path_Arg(hijacker, &opt_hijacker_name, ARG(hijacker));
    if (!IS_FUNCTION(hijacker))
        fail ("Hijacker in HIJACK must be a FUNCTION!");

    if (VAL_FUNC(victim) == VAL_FUNC(hijacker)) {
        //
        // Permitting a no-op hijack has some applications...but offer a
        // distinguished result for those who want to detect the condition.
        //
        return R_BLANK;
    }

    REBARR *victim_paramlist = VAL_FUNC_PARAMLIST(victim);
    REBARR *hijacker_paramlist = VAL_FUNC_PARAMLIST(hijacker);

    if (
        LOGICAL(
            FUNC_UNDERLYING(VAL_FUNC(hijacker))
            == FUNC_UNDERLYING(VAL_FUNC(victim))
        )
    ){
        // Should the underlying functions of the hijacker and victim match,
        // that means any ADAPT or CHAIN or SPECIALIZE of the victim can
        // work equally well if we just use the hijacker's dispatcher
        // directly.  This is a reasonably common case, and especially
        // common when putting the originally hijacked function back.

        LINK(victim_paramlist).facade = LINK(hijacker_paramlist).facade;
        LINK(victim->payload.function.body_holder).exemplar =
            LINK(hijacker->payload.function.body_holder).exemplar;

        // All function bodies should live in cells with the same underlying
        // formatting.  Blit_Cell ensures that's the case.
        //
        Blit_Cell(VAL_FUNC_BODY(victim), VAL_FUNC_BODY(hijacker));

        MISC(victim->payload.function.body_holder).dispatcher =
            MISC(hijacker->payload.function.body_holder).dispatcher;
    }
    else {
        // A mismatch means there could be someone out there pointing at this
        // function who expects it to have a different frame than it does.
        // In case that someone needs to run the function with that frame,
        // a proxy "shim" is needed.
        //
        // !!! It could be possible to do things here like test to see if
        // frames were compatible in some way that could accelerate the
        // process of building a new frame.  But in general one basically
        // needs to do a new function call.
        //
        Move_Value(VAL_FUNC_BODY(victim), hijacker);
        MISC(victim->payload.function.body_holder).dispatcher =
            &Hijacker_Dispatcher;
    }

    // !!! What should be done about MISC(victim_paramlist).meta?  Leave it
    // alone?  Add a note about the hijacking?

    Move_Value(D_OUT, victim);
    INIT_BINDING(D_OUT, VAL_BINDING(hijacker));

    return R_OUT;
}


//
//  variadic?: native [
//
//  {Returns TRUE if a function may take a variable number of arguments.}
//
//      func [function!]
//  ]
//
REBNATIVE(variadic_q)
{
    INCLUDE_PARAMS_OF_VARIADIC_Q;

    REBVAL *param = VAL_FUNC_PARAMS_HEAD(ARG(func));
    for (; NOT_END(param); ++param) {
        if (GET_VAL_FLAG(param, TYPESET_FLAG_VARIADIC))
            return R_TRUE;
    }

    return R_FALSE;
}


//
//  tighten: native [
//
//  {Returns alias of a function whose "normal" args are gathered "tightly"}
//
//      return: [function!]
//      action [function!]
//  ]
//
REBNATIVE(tighten)
//
// This routine exists to avoid the overhead of a user-function stub where
// all the parameters are #tight, e.g. the behavior of R3-Alpha's OP!s.
// So `+: enfix tighten :add` is a faster equivalent of:
//
//     +: enfix func [#arg1 [any-value!] #arg2 [any-value!] [
//         add :arg1 :arg2
//     ]
//
// But also, the parameter types and help notes are kept in sync.
//
{
    INCLUDE_PARAMS_OF_TIGHTEN;

    REBFUN *original = VAL_FUNC(ARG(action));

    // Copy the paramlist, which serves as the function's unique identity,
    // and set the tight flag on all the parameters.

    REBARR *paramlist = Copy_Array_Shallow(
        FUNC_PARAMLIST(original),
        SPECIFIED // no relative values in parameter lists
    );
    SET_SER_FLAG(paramlist, ARRAY_FLAG_PARAMLIST); // flags not auto-copied

    RELVAL *param = ARR_AT(paramlist, 1); // first parameter (0 is FUNCTION!)
    for (; NOT_END(param); ++param) {
        enum Reb_Param_Class pclass = VAL_PARAM_CLASS(param);
        if (pclass == PARAM_CLASS_NORMAL)
            INIT_VAL_PARAM_CLASS(param, PARAM_CLASS_TIGHT);
    }

    RELVAL *rootparam = ARR_HEAD(paramlist);
    CLEAR_VAL_FLAGS(rootparam, FUNC_FLAG_CACHED_MASK);
    rootparam->payload.function.paramlist = paramlist;
    INIT_BINDING(rootparam, UNBOUND);

    // !!! This does not make a unique copy of the meta information context.
    // Hence updates to the title/parameter-descriptions/etc. of the tightened
    // function will affect the original, and vice-versa.
    //
    MISC(paramlist).meta = FUNC_META(original);

    MANAGE_ARRAY(paramlist);

    // Our function has a new identity, but we don't want to be using that
    // identity for the pushed frame.  If we did that, then if the underlying
    // function were interpreted, we would have to make a copy of its body
    // and rebind it to the new paramlist.  HOWEVER we want the new tightened
    // parameter specification to take effect--and that's not reflected in
    // the original paramlist, e.g. the one to which that block is bound.
    //
    // So here's the clever part: functions allow you to offer a "facade"
    // which is an array compatible with the original underlying function,
    // but with stricter parameter types and different parameter classes.
    // So just as the paramlist got transformed, transform the facade.
    //
    // Note: Do NOT set the ARRAY_FLAG_PARAMLIST on this facade.  It holds
    // whatever function value in the [0] slot the original had, and that is
    // used for the identity of the "underlying function".  (In order to make
    // this a real FUNCTION!'s paramlist, the paramlist in the [0] slot would
    // have to be equal to the facade's pointer.)
    //
    REBARR *facade = Copy_Array_Shallow(
        FUNC_FACADE(original),
        SPECIFIED // no relative values in facades, either
    );
    RELVAL *facade_param = ARR_AT(facade, 1);
    for (; NOT_END(facade_param); ++facade_param) {
        //
        // !!! Technically we probably shouldn't be modifying the parameter
        // classes of any arguments that were specialized out or otherwise
        // not present in the original; but it shouldn't really matter.
        // Once this function's layer has finished, the lower levels will
        // refer to their own facades.
        //
        enum Reb_Param_Class pclass = VAL_PARAM_CLASS(facade_param);
        if (pclass == PARAM_CLASS_NORMAL)
            INIT_VAL_PARAM_CLASS(facade_param, PARAM_CLASS_TIGHT);
    }

    MANAGE_ARRAY(facade);

    REBFUN *fun = Make_Function(
        paramlist,
        FUNC_DISPATCHER(original),
        facade, // use the new, tightened facade
        FUNC_EXEMPLAR(original) // don't add to the original's specialization
    );

    // We're reusing the original dispatcher, so we also reuse the original
    // function body.  Note that Blit_Cell ensures that the cell formatting
    // on the source and target are the same, and it preserves relative
    // value information (rarely what you meant, but it's meant here).
    //
    Blit_Cell(FUNC_BODY(fun), FUNC_BODY(original));

    Move_Value(D_OUT, FUNC_VALUE(fun));

    // Currently esoteric case if someone chose to tighten a definitional
    // return, so `return 1 + 2` would return 1 instead of 3.  Would need to
    // preserve the binding of the incoming value, which is never present in
    // the canon value of the function.
    //
    INIT_BINDING(D_OUT, VAL_BINDING(ARG(action)));

    return R_OUT;
}
