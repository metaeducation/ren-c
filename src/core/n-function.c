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
// Copyright 2012-2016 Rebol Open Source Contributors
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
    PARAM(1, spec);
    PARAM(2, body);

    REBFUN *fun = Make_Interpreted_Function_May_Fail(
        ARG(spec), ARG(body), MKF_RETURN | MKF_KEYWORDS
    );

    *D_OUT = *FUNC_VALUE(fun);
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
    PARAM(1, spec);
    PARAM(2, body);

    REBFUN *fun = Make_Interpreted_Function_May_Fail(
        ARG(spec), ARG(body), MKF_LEAVE | MKF_KEYWORDS
    );

    *D_OUT = *FUNC_VALUE(fun);
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
    *out = *NAT_VALUE(exit);

    if (IS_INTEGER(level)) {
        REBCNT count = VAL_INT32(level);
        if (count <= 0)
            fail (Error(RE_INVALID_EXIT));

        REBFRM *f = frame->prior;
        for (; TRUE; f = f->prior) {
            if (f == NULL)
                fail (Error(RE_INVALID_EXIT));

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
//      value [<opt> any-value!]
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
    REFINE(1, with);
    PARAM(2, value);
    REFINE(3, from);
    PARAM(4, level);

    if (NOT(REF(from)))
        SET_INTEGER(ARG(level), 1); // default--exit one function stack level

    assert(REF(with) || IS_VOID(ARG(value)));

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
    PARAM(1, value);

    REBVAL *value = ARG(value);
    REBFRM *f = frame_; // implicit parameter to REBNATIVE()

    if (f->binding == NULL) // raw native, not a variant FUNCTION made
        fail (Error(RE_RETURN_ARCHETYPE));

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

    *D_OUT = *NAT_VALUE(exit); // see also Make_Thrown_Exit_Value
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
        fail (Error(RE_RETURN_ARCHETYPE));

    *D_OUT = *NAT_VALUE(exit); // see also Make_Thrown_Exit_Value
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
    PARAM(1, type);
    REFINE(2, opt);

    REBVAL *type = ARG(type);

    REBARR *paramlist = Make_Array(2);

    REBVAL *archetype = Alloc_Tail_Array(paramlist);
    VAL_RESET_HEADER(archetype, REB_FUNCTION);
    archetype->payload.function.paramlist = paramlist;
    archetype->extra.binding = NULL;

    REBVAL *param = Alloc_Tail_Array(paramlist);
    Val_Init_Typeset(param, ALL_64, Canon(SYM_VALUE));
    INIT_VAL_PARAM_CLASS(param, PARAM_CLASS_NORMAL);

    SET_ARR_FLAG(paramlist, ARRAY_FLAG_PARAMLIST);
    MANAGE_ARRAY(paramlist);

    REBFUN *fun = Make_Function(
        paramlist,
        IS_DATATYPE(type)
        ? &Datatype_Checker_Dispatcher
        : &Typeset_Checker_Dispatcher,
        NULL // this is fundamental (no distinct underlying function)
    );

    *FUNC_BODY(fun) = *type;

    // for now, no help...use REDESCRIBE

    ARR_SERIES(paramlist)->link.meta = NULL;

    *D_OUT = *FUNC_VALUE(fun);

    return R_OUT;
}


//
//  brancher: native/body [
//
//  {Create a function that selects between two values based on a LOGIC!}
//
//      return: [function!]
//      :true-branch [block!]
//      :false-branch [block!]
//  ][
//      specialize 'either [
//          true-branch: true-branch
//          false-branch: false-branch
//      ]
//  ]
//
REBNATIVE(brancher)
//
// !!! This is a slightly more optimized version of a brancher than could be
// accomplished in user mode code.  The "equivalent body" doesn't actually
// behave equivalently because there is no meta information suggesting
// the result is a specialization, so perhaps there should be a "remove
// meta" included (?)
//
// If this were taken to a next level of optimization for ELSE, it would have
// to not create series...but a special kind of REBVAL which would morph
// into a function on demand.  IF and UNLESS could recognize this special
// value type and treat it like a branch.
//
// !!! Currently it is limited to hard quoted BLOCK!s based on the
// limitations of left-handed enfixed functions w.r.t. quoting.  This is
// based on the assumption that in the long run, <tight> will not exist; and
// a function wanting to pull the trick ELSE is with its left hand side will
// have to use some kind of quoting.
{
    PARAM(1, true_branch);
    PARAM(2, false_branch);

    REBARR *paramlist = Make_Array(2);
    ARR_SERIES(paramlist)->link.meta = NULL;

    REBVAL *rootkey = SINK(ARR_AT(paramlist, 0));
    VAL_RESET_HEADER(rootkey, REB_FUNCTION);
    /* SET_VAL_FLAGS(rootkey, ???); */ // if flags ever needed...
    rootkey->payload.function.paramlist = paramlist;
    rootkey->extra.binding = NULL;

    REBVAL *param = SINK(ARR_AT(paramlist, 1));
    Val_Init_Typeset(param, FLAGIT_64(REB_LOGIC), Canon(SYM_CONDITION));
    INIT_VAL_PARAM_CLASS(param, PARAM_CLASS_NORMAL);
    TERM_ARRAY_LEN(paramlist, 2);

    SET_ARR_FLAG(paramlist, ARRAY_FLAG_PARAMLIST);
    MANAGE_ARRAY(paramlist);

    REBFUN *func = Make_Function(
        paramlist,
        &Brancher_Dispatcher,
        NULL // no underlying function, this is fundamental
    );

    RELVAL *body = FUNC_BODY(func);

    REBVAL *branches = Make_Pairing(NULL);
    *PAIRING_KEY(branches) = *ARG(true_branch);
    *branches = *ARG(false_branch);
    Manage_Pairing(branches);

    VAL_RESET_HEADER(body, REB_PAIR);
    body->payload.pair = branches;

    *D_OUT = *FUNC_VALUE(func);
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
    PARAM(1, value);
    PARAM(2, def);

    REBSTR *opt_name;

    // We don't limit to taking a FUNCTION! value directly, because that loses
    // the symbol (for debugging, errors, etc.)  If caller passes a WORD!
    // then we lookup the variable to get the function, but save the symbol.
    //
    REBVAL specializee;
    Get_If_Word_Or_Path_Arg(&specializee, &opt_name, ARG(value));

    if (!IS_FUNCTION(&specializee))
        fail (Error(RE_APPLY_NON_FUNCTION, ARG(value))); // for APPLY too

    if (Specialize_Function_Throws(D_OUT, &specializee, opt_name, ARG(def)))
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
    PARAM(1, pipeline);
    REFINE(2, quote);

    REBVAL *out = D_OUT; // plan ahead for factoring into Chain_Function(out..

    REBVAL *pipeline = ARG(pipeline);
    REBARR *chainees;
    if (REF(quote)) {
        chainees = COPY_ANY_ARRAY_AT_DEEP_MANAGED(pipeline);
    }
    else {
        if (Reduce_Any_Array_Throws(out, pipeline, FALSE))
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
    SET_ARR_FLAG(paramlist, ARRAY_FLAG_PARAMLIST);
    MANAGE_ARRAY(paramlist);

    REBFUN *specializer;
    REBFUN *underlying = Underlying_Function(&specializer, first);

    REBFUN *fun = Make_Function(
        paramlist,
        &Chainer_Dispatcher,
        specializer != NULL ? specializer : underlying // cache in paramlist
    );

    // "body" is the chainees array, available to the dispatcher when called
    //
    Val_Init_Block(FUNC_BODY(fun), chainees);

    // See %sysobj.r for `specialized-meta:` object template

    REBVAL *std_meta = Get_System(SYS_STANDARD, STD_CHAINED_META);
    REBCTX *meta = Copy_Context_Shallow(VAL_CONTEXT(std_meta));

    assert(IS_VOID(CTX_VAR(meta, SELFISH(1)))); // no description by default
    Val_Init_Block(CTX_VAR(meta, SELFISH(2)), chainees);
    //
    // !!! There could be a system for preserving names in the chain, by
    // accepting lit-words instead of functions--or even by reading the
    // GET-WORD!s in the block.  Consider for the future.
    //
    assert(IS_VOID(CTX_VAR(meta, SELFISH(3))));

    MANAGE_ARRAY(CTX_VARLIST(meta));
    ARR_SERIES(paramlist)->link.meta = meta;

    *D_OUT = *FUNC_VALUE(fun);
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
    PARAM(1, adaptee);
    PARAM(2, prelude);

    REBVAL *adaptee = ARG(adaptee);

    REBSTR *opt_adaptee_name;
    Get_If_Word_Or_Path_Arg(D_OUT, &opt_adaptee_name, adaptee);
    if (!IS_FUNCTION(D_OUT))
        fail (Error(RE_APPLY_NON_FUNCTION, adaptee));

    *adaptee = *D_OUT;

    // For the binding to be correct, the indices that the words use must be
    // the right ones for the frame pushed.  So if you adapt a specialization
    // that has one parameter, and the function that underlies that has
    // 10 parameters and the one parameter you're adapting to is it's 10th
    // and not its 1st...that has to be taken into account.
    //
    // Hence you must bind relative to that deeper function...e.g. the function
    // behind the frame of the specialization which gets pushed.
    //
    REBFUN *specializer;
    REBFUN *underlying = Underlying_Function(&specializer, adaptee);

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
    SET_ARR_FLAG(paramlist, ARRAY_FLAG_PARAMLIST);
    MANAGE_ARRAY(paramlist);

    REBFUN *fun = Make_Function(
        paramlist,
        &Adapter_Dispatcher,
        specializer != NULL ? specializer : underlying // cache in paramlist
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
    SET_VAL_FLAG(block, VALUE_FLAG_RELATIVE);
    INIT_RELATIVE(block, underlying);

    Append_Value(adaptation, adaptee);

    RELVAL *body = FUNC_BODY(fun);
    VAL_RESET_HEADER(body, REB_BLOCK);
    INIT_VAL_ARRAY(body, adaptation);
    VAL_INDEX(body) = 0;
    SET_VAL_FLAG(body, VALUE_FLAG_RELATIVE);
    INIT_RELATIVE(body, underlying);
    MANAGE_ARRAY(adaptation);

    // See %sysobj.r for `specialized-meta:` object template

    REBVAL *example = Get_System(SYS_STANDARD, STD_ADAPTED_META);

    REBCTX *meta = Copy_Context_Shallow(VAL_CONTEXT(example));
    assert(IS_VOID(CTX_VAR(meta, SELFISH(1)))); // no description by default
    *CTX_VAR(meta, SELFISH(2)) = *adaptee;
    if (opt_adaptee_name != NULL)
        Val_Init_Word(CTX_VAR(meta, SELFISH(3)), REB_WORD, opt_adaptee_name);

    MANAGE_ARRAY(CTX_VARLIST(meta));
    ARR_SERIES(paramlist)->link.meta = meta;

    *D_OUT = *FUNC_VALUE(fun);
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
    PARAM(1, victim);
    PARAM(2, hijacker);

    REBVAL victim_value;
    REBSTR *opt_victim_name;
    Get_If_Word_Or_Path_Arg(
        &victim_value, &opt_victim_name, ARG(victim)
    );
    REBVAL *victim = &victim_value;
    if (!IS_FUNCTION(victim))
        fail (Error(RE_MISC));

    REBVAL hijacker_value;
    REBSTR *opt_hijacker_name;
    Get_If_Word_Or_Path_Arg(
        &hijacker_value, &opt_hijacker_name, ARG(hijacker)
    );
    REBVAL *hijacker = &hijacker_value;
    if (!IS_FUNCTION(hijacker) && !IS_BLANK(hijacker))
        fail (Error(RE_MISC));

    // !!! Should hijacking a function with itself be a no-op?  One could make
    // an argument from semantics that the effect of replacing something with
    // itself is not to change anything, but erroring may give a sanity check.
    //
    if (!IS_BLANK(hijacker) && VAL_FUNC(victim) == VAL_FUNC(hijacker))
        fail (Error(RE_MISC));

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
            fail (Error(RE_MISC)); // !!! Allow re-blanking a blank?

        SET_BLANK(D_OUT);
    }
    else {
        // For non-blank victims, the return value will be a proxy for that
        // victim.  This proxy must have a different paramlist from the
        // original victim being hijacked (otherwise, calling it would call
        // the hijacker too).  So it's a copy.

        REBFUN *victim_underlying
            = ARR_SERIES(victim->payload.function.paramlist)->misc.underlying;

        REBARR *proxy_paramlist = Copy_Array_Deep_Managed(
            victim->payload.function.paramlist,
            SPECIFIED // !!! Note: not actually "deep", just typesets
        );
        ARR_HEAD(proxy_paramlist)->payload.function.paramlist
            = proxy_paramlist;
        ARR_SERIES(proxy_paramlist)->link.meta = VAL_FUNC_META(victim);
        SET_ARR_FLAG(proxy_paramlist, ARRAY_FLAG_PARAMLIST);

        // If the proxy had a body, then that body will be bound relative
        // to the original paramlist that's getting hijacked.  So when the
        // proxy is called, we want the frame pushed to be relative to
        // whatever underlied the function...even if it was foundational
        // so `victim_underlying = VAL_FUNC(victim)`

        REBFUN *proxy = Make_Function(
            proxy_paramlist,
            FUNC_DISPATCHER(VAL_FUNC(victim)),
            victim_underlying
        );

        // The victim's body is overwritten below to hold the hijacker.  Copy
        // the REBVAL bits first.

        *FUNC_BODY(proxy) = *VAL_FUNC_BODY(victim);

        *D_OUT = *FUNC_VALUE(proxy);
        D_OUT->extra.binding = VAL_BINDING(victim);

    #if !defined(NDEBUG)
        SET_VAL_FLAG(FUNC_VALUE(proxy), FUNC_FLAG_PROXY_DEBUG);

        REBFUN *specializer;
        Underlying_Function(&specializer, D_OUT); // double-check underlying
    #endif
    }

    // With the return value settled, do the actual hijacking.  The "body"
    // payload of a hijacker is the replacement function value itself.
    //
    // Note we don't want to disrupt the underlying function from whatever it
    // was before, because derived compositions cached that.  It will not
    // match the hijacker, so it won't be able to directly use the frame
    // which is built, and will have to build a new frame in the dispatcher.

    *VAL_FUNC_BODY(victim) = *hijacker;
    ARR_SERIES(victim->payload.function.body_holder)->misc.dispatcher
        = &Hijacker_Dispatcher;

    victim->extra.binding = NULL; // old exit binding extracted for proxy

    *ARR_HEAD(VAL_FUNC_PARAMLIST(victim)) = *victim; // update rootparam

    // Update the meta information on the function to indicate it's hijacked
    // See %sysobj.r for `hijacked-meta:` object template

    REBVAL *std_meta = Get_System(SYS_STANDARD, STD_HIJACKED_META);
    REBCTX *meta = Copy_Context_Shallow(VAL_CONTEXT(std_meta));

    assert(IS_VOID(CTX_VAR(meta, SELFISH(1)))); // no description by default
    *CTX_VAR(meta, SELFISH(2)) = *D_OUT;
    if (opt_victim_name != NULL)
        Val_Init_Word(CTX_VAR(meta, SELFISH(3)), REB_WORD, opt_victim_name);

    MANAGE_ARRAY(CTX_VARLIST(meta));
    ARR_SERIES(VAL_FUNC_PARAMLIST(victim))->link.meta = meta;

#if !defined(NDEBUG)
    REBFUN *specializer;
    Underlying_Function(&specializer, victim); // double-check underlying
#endif

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
    PARAM(1, func);

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
//  {Returns alias of a function whose args are gathered <tight>ly}
//
//      return: [function!]
//      action [function!]
//  ]
//
REBNATIVE(tighten)
//
// !!! The <tight> annotation was introduced while trying to define a bridge
// for compatibility with R3-Alpha's OP!.  That code made use of "lookahead
// suppression" on the right hand side of infix operators, in order to give
// a left-to-right evaluation ordering in pure infix expressions.  After some
// experimentation, Ren-C came up with a more uniform rule across both
// "enfixed" expressions (of arbitrary arity) and ordinary prefix expressions,
// which still gave a left-to-right effect for binary infix ops.
//
// Hence <tight> is likely to be phased out; but it exists for compatibility.
// This routine exists to avoid the overhead of a user-function stub where
// all the parameters are <tight>, e.g. the behavior of R3-Alpha's OP!s.
// So `+: enfix tighten :add` is a faster equivalent of:
//
//     +: enfix func [arg1 [<tight> any-value!] arg2 [<tight> any-value!] [
//         add :arg1 :arg2
//     ]
//
// But also, the parameter types and help notes are kept in sync.
{
    PARAM(1, action);

    REBFUN *original = VAL_FUNC(ARG(action));

    // !!! With specializations and chaining, functions can be a stack of
    // entities which must have consistent defintions and point to each other.
    // The identity comes from the top-most function, while the functionality
    // often comes from the lowest one...and the pointers must be consistent.
    // This means that tweaking function copies gets somewhat involved.  :-/
    //
    // For now this only supports tightening native or interpreted functions,
    // to avoid walking the function chain and adjusting each level.  The
    // option is still currently available to manually create a new user
    // function that explicitly calls chained/specialized/adapted functions
    // but has <tight> parameters...it will just not be as fast.
    //
    REBFUN *specializer;
    REBFUN *underlying = Underlying_Function(&specializer, ARG(action));
    if (underlying != original)
        fail (Error(RE_INVALID_TIGHTEN));

    // Copy the paramlist, which serves as the function's unique identity (the
    // body can be reused as-is)
    //
    REBARR *paramlist = Copy_Array_Shallow(
        FUNC_PARAMLIST(original),
        SPECIFIED // no relative values in parameter lists
    );
    SET_ARR_FLAG(paramlist, ARRAY_FLAG_PARAMLIST); // flags not auto-copied

    // Now set the tight flag on all the parameters.
    //
    RELVAL *param = ARR_AT(paramlist, 1); // first parameter (0 is FUNCTION!)
    for (; NOT_END(param); ++param)
        SET_VAL_FLAG(param, TYPESET_FLAG_TIGHT);

    // !!! This does not make a unique copy of the meta information context.
    // Hence updates to the title/parameter-descriptions/etc. of the tightened
    // function will affect the original, and vice-versa.  Because this is
    // a legacy support issue that's probably a feature and not a bug, but
    // other function-copying abstractions should do it.
    //    
    ARR_SERIES(paramlist)->link.meta = FUNC_META(original);

    // Update the underlying function (we should know it was equal to the
    // original function from the test at the start)
    //
    ARR_SERIES(paramlist)->misc.underlying = AS_FUNC(paramlist);

    assert(IS_FUNCTION(ARR_AT(paramlist, 0))); // canon value, must update
    ARR_AT(paramlist, 0)->payload.function.paramlist = paramlist;
    MANAGE_ARRAY(paramlist);

    // The function should not indicate any longer that it defers lookback
    // arguments (this flag is calculated from <tight> annotations in
    // Make_Function())
    //
    CLEAR_VAL_FLAG(ARR_AT(paramlist, 0), FUNC_FLAG_DEFERS_LOOKBACK_ARG);

    *D_OUT = *KNOWN(ARR_AT(paramlist, 0)); // canon value we just updated

    // Currently esoteric case if someone chose to tighten a definitional
    // return, so `return 1 + 2` would return 1 instead of 3.  Would need to
    // preserve the binding of the incoming value, which is never present in
    // the canon value of the function.
    //
    assert(D_OUT->extra.binding == NULL);
    D_OUT->extra.binding = ARG(action)->extra.binding;

    return R_OUT;
}
