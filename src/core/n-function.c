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
    REBFRM *frame // only required if level is INTEGER!
) {
    Move_Value(out, NAT_VALUE(exit));

    if (IS_INTEGER(level)) {
        REBCNT count = VAL_INT32(level);
        if (count <= 0)
            fail (Error_Invalid_Exit_Raw());

        REBFRM *f = frame->prior;
        for (; TRUE; f = f->prior) {
            if (f == NULL)
                fail (Error_Invalid_Exit_Raw());

            if (NOT(Is_Any_Function_Frame(f))) continue; // only exit functions

            if (Is_Function_Frame_Fulfilling(f)) continue; // not ready to exit

        #if !defined(NDEBUG)
            if (LEGACY(OPTIONS_DONT_EXIT_NATIVES))
                if (NOT(IS_FUNCTION_INTERPRETED(FUNC_VALUE(f->func))))
                    continue; // R3-Alpha would exit the first user function
        #endif

            --count;

            if (count == 0) {
                //
                // We want the integer-based exits to identify frames uniquely.
                // Without a context varlist, a frame can't be unique.
                //
                Context_For_Frame_May_Reify_Managed(f);
                assert(f->varlist);
                out->extra.binding = f->varlist;
                break;
            }
        }
    }
    else if (IS_FRAME(level)) {
        out->extra.binding = CTX_VARLIST(VAL_CONTEXT(level));
    }
    else {
        assert(IS_FUNCTION(level));
        out->extra.binding = VAL_FUNC_PARAMLIST(level);
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
        SET_INTEGER(ARG(level), 1); // default--exit one function stack level

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

    REBVAL *value = ARG(value);
    REBFRM *f = frame_; // implicit parameter to REBNATIVE()

    if (f->binding == NULL) // raw native, not a variant FUNCTION made
        fail (Error_Return_Archetype_Raw());

    // The frame this RETURN is being called from may well not be the target
    // function of the return (that's why it's a "definitional return").  So
    // examine the binding.  Currently it can be either a FRAME!'s varlist or
    // a FUNCTION! paramlist.

    REBFUN *target =
        IS_FUNCTION(ARR_HEAD(f->binding))
        ? AS_FUNC(f->binding)
        : AS_FUNC(CTX_KEYLIST(AS_CONTEXT(f->binding)));

    REBVAL *typeset = FUNC_PARAM(target, FUNC_NUM_PARAMS(target));
    assert(VAL_PARAM_SYM(typeset) == SYM_RETURN);

    // Check to make sure the types match.  If it were not done here, then
    // the error would not point out the bad call...just the function that
    // wound up catching it.
    //
    if (!TYPE_CHECK(typeset, VAL_TYPE(value)))
        fail (Error_Bad_Return_Type(
            f->label, // !!! Should climb stack to get real label?
            VAL_TYPE(value)
        ));

    Move_Value(D_OUT, NAT_VALUE(exit)); // see also Make_Thrown_Exit_Value
    D_OUT->extra.binding = f->binding;

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
    if (frame_->binding == NULL) // raw native, not a variant PROCEDURE made
        fail (Error_Return_Archetype_Raw());

    Move_Value(D_OUT, NAT_VALUE(exit)); // see also Make_Thrown_Exit_Value
    D_OUT->extra.binding = frame_->binding;

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

    REBARR *paramlist = Make_Array(2);

    REBVAL *archetype = Alloc_Tail_Array(paramlist);
    VAL_RESET_HEADER(archetype, REB_FUNCTION);
    archetype->payload.function.paramlist = paramlist;
    archetype->extra.binding = NULL;

    REBVAL *param = Alloc_Tail_Array(paramlist);
    Init_Typeset(param, ALL_64, Canon(SYM_VALUE));
    INIT_VAL_PARAM_CLASS(param, PARAM_CLASS_NORMAL);

    SET_SER_FLAG(paramlist, ARRAY_FLAG_PARAMLIST);
    MANAGE_ARRAY(paramlist);

    // for now, no help...use REDESCRIBE

    AS_SERIES(paramlist)->link.meta = NULL;

    REBFUN *fun = Make_Function(
        paramlist,
        IS_DATATYPE(type)
            ? &Datatype_Checker_Dispatcher
            : &Typeset_Checker_Dispatcher,
        NULL, // this is fundamental (no distinct underlying function)
        NULL // not providing a specialization
    );

    *FUNC_BODY(fun) = *type;

    Move_Value(D_OUT, FUNC_VALUE(fun));

    return R_OUT;
}


//
//  specialize: native [
//
//  {Create a new function through partial or full specialization of another}
//
//      return: [function!]
//      value [function! any-word! any-path!]
//          {Function or specifying word (preserves word name for debug info)}
//      def [block!]
//          {Definition for FRAME! fields for args and refinements}
//  ]
//
REBNATIVE(specialize)
{
    INCLUDE_PARAMS_OF_SPECIALIZE;

    REBSTR *opt_name;

    // We don't limit to taking a FUNCTION! value directly, because that loses
    // the symbol (for debugging, errors, etc.)  If caller passes a WORD!
    // then we lookup the variable to get the function, but save the symbol.
    //
    DECLARE_LOCAL (specializee);
    Get_If_Word_Or_Path_Arg(specializee, &opt_name, ARG(value));

    if (!IS_FUNCTION(specializee))
        fail (Error_Apply_Non_Function_Raw(ARG(value))); // for APPLY too

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
            fail (Error_Invalid_Arg(check));
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

    // See %sysobj.r for `chained-meta:` object template

    REBVAL *std_meta = Get_System(SYS_STANDARD, STD_CHAINED_META);
    REBCTX *meta = Copy_Context_Shallow(VAL_CONTEXT(std_meta));

    SET_VOID(CTX_VAR(meta, STD_CHAINED_META_DESCRIPTION)); // default
    Init_Block(CTX_VAR(meta, STD_CHAINED_META_CHAINEES), chainees);
    //
    // !!! There could be a system for preserving names in the chain, by
    // accepting lit-words instead of functions--or even by reading the
    // GET-WORD!s in the block.  Consider for the future.
    //
    SET_VOID(CTX_VAR(meta, STD_CHAINED_META_CHAINEE_NAMES));

    MANAGE_ARRAY(CTX_VARLIST(meta));
    AS_SERIES(paramlist)->link.meta = meta;

    REBFUN *fun = Make_Function(
        paramlist,
        &Chainer_Dispatcher,
        VAL_FUNC(first), // cache in paramlist
        NULL // not changing the specialization
    );

    // "body" is the chainees array, available to the dispatcher when called
    //
    Init_Block(FUNC_BODY(fun), chainees);

    Move_Value(D_OUT, FUNC_VALUE(fun));
    assert(VAL_BINDING(D_OUT) == NULL);

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
        fail (Error_Apply_Non_Function_Raw(adaptee));

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
    SET_VOID(CTX_VAR(meta, STD_ADAPTED_META_DESCRIPTION)); // default
    Move_Value(CTX_VAR(meta, STD_ADAPTED_META_ADAPTEE), adaptee);
    if (opt_adaptee_name == NULL)
        SET_VOID(CTX_VAR(meta, STD_ADAPTED_META_ADAPTEE_NAME));
    else
        Init_Word(
            CTX_VAR(meta, STD_ADAPTED_META_ADAPTEE_NAME),
            opt_adaptee_name
        );

    MANAGE_ARRAY(CTX_VARLIST(meta));
    AS_SERIES(paramlist)->link.meta = meta;

    REBFUN *fun = Make_Function(
        paramlist,
        &Adapter_Dispatcher,
        underlying, // cache in paramlist
        NULL // not changing the specialization
    );

    // We need to store the 2 values describing the adaptation so that the
    // dispatcher knows what to do when it gets called and inspects FUNC_BODY.
    //
    // [0] is the prelude BLOCK!, [1] is the FUNCTION! we've adapted.
    //
    REBARR *adaptation = Make_Array(2);

    REBVAL *block = Alloc_Tail_Array(adaptation);
    VAL_RESET_HEADER_EXTRA(block, REB_BLOCK, VALUE_FLAG_RELATIVE);
    INIT_VAL_ARRAY(block, prelude);
    VAL_INDEX(block) = 0;
    INIT_RELATIVE(block, underlying);

    Append_Value(adaptation, adaptee);

    RELVAL *body = FUNC_BODY(fun);
    VAL_RESET_HEADER_EXTRA(body, REB_BLOCK, VALUE_FLAG_RELATIVE);
    INIT_VAL_ARRAY(body, adaptation);
    VAL_INDEX(body) = 0;
    INIT_RELATIVE(body, underlying);
    MANAGE_ARRAY(adaptation);

    Move_Value(D_OUT, FUNC_VALUE(fun));
    assert(VAL_BINDING(D_OUT) == NULL);

    return R_OUT;
}


//
//  hijack: native [
//
//  {Cause all existing references to a function to invoke another function.}
//
//      return: [function! blank!]
//          {Proxy for the original function, BLANK! if hijacked with BLANK!}
//      victim [function! any-word! any-path!]
//          {Function value whose references are to be affected.}
//      hijacker [function! any-word! any-path! blank!]
//          {The function to run in its place or BLANK! to extract prior code.}
//  ]
//
REBNATIVE(hijack)
//
// !!! Should the parameters be checked for baseline compatibility, or just
// let all failures happen at the moment of trying to run the hijack?
// As it is, one might not require a perfectly compatible interface,
// and be tolerant if the refinements don't line up...just fail if any
// case of trying to use unaligned refinements happens.
//
{
    INCLUDE_PARAMS_OF_HIJACK;

    DECLARE_LOCAL (victim);
    REBSTR *opt_victim_name;
    Get_If_Word_Or_Path_Arg(victim, &opt_victim_name, ARG(victim));
    if (!IS_FUNCTION(victim))
        fail (Error_Misc_Raw());

    DECLARE_LOCAL (hijacker);
    REBSTR *opt_hijacker_name;
    Get_If_Word_Or_Path_Arg(hijacker, &opt_hijacker_name, ARG(hijacker));
    if (!IS_FUNCTION(hijacker) && !IS_BLANK(hijacker))
        fail (Error_Misc_Raw());

    // !!! Should hijacking a function with itself be a no-op?  One could make
    // an argument from semantics that the effect of replacing something with
    // itself is not to change anything, but erroring may give a sanity check.
    //
    if (!IS_BLANK(hijacker) && VAL_FUNC(victim) == VAL_FUNC(hijacker))
        fail (Error_Misc_Raw());

    if (IS_FUNCTION_HIJACKER(victim) && IS_BLANK(VAL_FUNC_BODY(victim))) {
        //
        // If the victim is a "blank hijackee", it was generated by a previous
        // hijack call.  This was likely for the purposes of getting a proxy
        // for the function to use in the hijacker's implementation itself.
        //
        // We don't bother copying the paramlist to proxy it again--just poke
        // the value into the paramlist directly, and return blank to signify
        // that no new proxy could be made.

        if (IS_BLANK(hijacker))
            fail (Error_Misc_Raw()); // !!! Allow re-blanking a blank?

        SET_BLANK(D_OUT);
    }
    else {
        // For non-blank victims, the return value will be a proxy for that
        // victim.  This proxy must have a different paramlist from the
        // original victim being hijacked (otherwise, calling it would call
        // the hijacker too).  So it's a copy.

        REBFUN *victim_underlying = FUNC_UNDERLYING(VAL_FUNC(victim));

        REBARR *proxy_paramlist = Copy_Array_Deep_Managed(
            victim->payload.function.paramlist,
            SPECIFIED // !!! Note: not actually "deep", just typesets
        );
        ARR_HEAD(proxy_paramlist)->payload.function.paramlist
            = proxy_paramlist;
        AS_SERIES(proxy_paramlist)->link.meta = VAL_FUNC_META(victim);
        SET_SER_FLAG(proxy_paramlist, ARRAY_FLAG_PARAMLIST);

        // If the proxy had a body, then that body will be bound relative
        // to the original paramlist that's getting hijacked.  So when the
        // proxy is called, we want the frame pushed to be relative to
        // whatever underlied the function...even if it was foundational
        // so `victim_underlying = VAL_FUNC(victim)`

        REBFUN *proxy = Make_Function(
            proxy_paramlist,
            FUNC_DISPATCHER(VAL_FUNC(victim)),
            victim_underlying,
            NULL // not changing the specialization
        );

        // The victim's body is overwritten below to hold the hijacker.  Copy
        // the REBVAL bits first.

        *FUNC_BODY(proxy) = *VAL_FUNC_BODY(victim);

        Move_Value(D_OUT, FUNC_VALUE(proxy));
        D_OUT->extra.binding = VAL_BINDING(victim);
    }

    // With the return value settled, do the actual hijacking.  The "body"
    // payload of a hijacker is the replacement function value itself.
    //
    // Note we don't want to disrupt the underlying function from whatever it
    // was before, because derived compositions cached that.  It will not
    // match the hijacker, so it won't be able to directly use the frame
    // which is built, and will have to build a new frame in the dispatcher.

    *VAL_FUNC_BODY(victim) = *hijacker;
    AS_SERIES(victim->payload.function.body_holder)->misc.dispatcher
        = &Hijacker_Dispatcher;

    victim->extra.binding = NULL; // old exit binding extracted for proxy

    *ARR_HEAD(VAL_FUNC_PARAMLIST(victim)) = *victim; // update rootparam

    // Update the meta information on the function to indicate it's hijacked
    // See %sysobj.r for `hijacked-meta:` object template

    REBVAL *std_meta = Get_System(SYS_STANDARD, STD_HIJACKED_META);
    REBCTX *meta = Copy_Context_Shallow(VAL_CONTEXT(std_meta));

    SET_VOID(CTX_VAR(meta, STD_HIJACKED_META_DESCRIPTION)); // default
    Move_Value(CTX_VAR(meta, STD_HIJACKED_META_HIJACKEE), D_OUT);
    if (opt_victim_name == NULL)
        SET_VOID(CTX_VAR(meta, STD_HIJACKED_META_HIJACKEE_NAME));
    else
        Init_Word(
            CTX_VAR(meta, STD_HIJACKED_META_HIJACKEE_NAME),
            opt_victim_name
        );

    MANAGE_ARRAY(CTX_VARLIST(meta));
    AS_SERIES(VAL_FUNC_PARAMLIST(victim))->link.meta = meta;

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
    rootparam->extra.binding = NULL;

    // !!! This does not make a unique copy of the meta information context.
    // Hence updates to the title/parameter-descriptions/etc. of the tightened
    // function will affect the original, and vice-versa.
    //
    AS_SERIES(paramlist)->link.meta = FUNC_META(original);

    MANAGE_ARRAY(paramlist);

    REBFUN *fun = Make_Function(
        paramlist,
        FUNC_DISPATCHER(original),
        original, // used to set the initial facade (overridden below)
        NULL // don't add any specialization beyond the original
    );

    // We're reusing the original dispatcher, so we also reuse the original
    // function body.
    //
    *FUNC_BODY(fun) = *FUNC_BODY(original);

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

    // Note: Do NOT set the ARRAY_FLAG_PARAMLIST on this facade.  It holds
    // whatever function value in the [0] slot the original had, and that is
    // used for the identity of the "underlying function".  (In order to make
    // this a real FUNCTION!'s paramlist, the paramlist in the [0] slot would
    // have to be equal to the facade's pointer.)
    //
    AS_SERIES(paramlist)->misc.facade = facade;

    Move_Value(D_OUT, FUNC_VALUE(fun));

    // Currently esoteric case if someone chose to tighten a definitional
    // return, so `return 1 + 2` would return 1 instead of 3.  Would need to
    // preserve the binding of the incoming value, which is never present in
    // the canon value of the function.
    //
    D_OUT->extra.binding = ARG(action)->extra.binding;

    return R_OUT;
}
