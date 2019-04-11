//
//  File: %n-function.c
//  Summary: "Natives for creating and interacting with ACTION!s"
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
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
//=////////////////////////////////////////////////////////////////////////=//
//
// Ren-C implements a concept of a single ACTION! type, instead of the many
// subcategories of ANY-FUNCTION! from Rebol2 and R3-Alpha.  The categories
// unified under the name "ACTION!" instead of "FUNCTION!" for good reasons:
//
// https://forum.rebol.info/t/taking-action-on-function-vs-action/596
//

#include "sys-core.h"

//
//  func: native [
//
//  "Defines an ACTION! with given spec and body"
//
//      return: [action!]
//      spec "Help string (opt) followed by arg words (and opt type + string)"
//          [block!]
//      body "Code implementing the function--use RETURN to yield a result"
//          [<const> block!]
//  ]
//
REBNATIVE(func)
{
    INCLUDE_PARAMS_OF_FUNC;

    REBACT *func = Make_Interpreted_Action_May_Fail(
        ARG(spec),
        ARG(body),
        MKF_RETURN | MKF_KEYWORDS
    );

    return Init_Action_Unbound(D_OUT, func);
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
REB_R Init_Thrown_Unwind_Value(
    REBVAL *out,
    const REBVAL *level, // FRAME!, ACTION! (or INTEGER! relative to frame)
    const REBVAL *value,
    REBFRM *frame // required if level is INTEGER! or ACTION!
) {
    Move_Value(out, NAT_VALUE(unwind));

    if (IS_FRAME(level)) {
        INIT_BINDING(out, VAL_CONTEXT(level));
    }
    else if (IS_INTEGER(level)) {
        REBCNT count = VAL_INT32(level);
        if (count <= 0)
            fail (Error_Invalid_Exit_Raw());

        REBFRM *f = frame->prior;
        for (; true; f = f->prior) {
            if (f == FS_BOTTOM)
                fail (Error_Invalid_Exit_Raw());

            if (not Is_Action_Frame(f))
                continue; // only exit functions

            if (Is_Action_Frame_Fulfilling(f))
                continue; // not ready to exit

            --count;
            if (count == 0) {
                INIT_BINDING_MAY_MANAGE(out, SPC(f->varlist));
                break;
            }
        }
    }
    else {
        assert(IS_ACTION(level));

        REBFRM *f = frame->prior;
        for (; true; f = f->prior) {
            if (f == FS_BOTTOM)
                fail (Error_Invalid_Exit_Raw());

            if (not Is_Action_Frame(f))
                continue; // only exit functions

            if (Is_Action_Frame_Fulfilling(f))
                continue; // not ready to exit

            if (VAL_ACTION(level) == f->original) {
                INIT_BINDING_MAY_MANAGE(out, SPC(f->varlist));
                break;
            }
        }
    }

    return Init_Thrown_With_Label(out, value, out);
}


//
//  unwind: native [
//
//  {Jump up the stack to return from a specific frame or call.}
//
//      level "Frame, action, or index to exit from"
//          [frame! action! integer!]
//      result "Result for enclosing state"
//          [<opt> <end> any-value!]
//  ]
//
REBNATIVE(unwind)
//
// UNWIND is implemented via a throw that bubbles through the stack.  Using
// UNWIND's action REBVAL with a target `binding` field is the protocol
// understood by Eval_Core to catch a throw itself.
//
// !!! Allowing to pass an INTEGER! to jump from a function based on its
// BACKTRACE number is a bit low-level, and perhaps should be restricted to
// a debugging mode (though it is a useful tool in "code golf").
{
    INCLUDE_PARAMS_OF_UNWIND;

    return Init_Thrown_Unwind_Value(
        D_OUT,
        ARG(level),
        IS_ENDISH_NULLED(ARG(result)) ? VOID_VALUE : ARG(result),
        frame_
    );
}


//
//  return: native [
//
//  {RETURN, giving a result to the caller}
//
//      value "If no argument is given, result will be a VOID!"
//          [<end> <opt> any-value!]
//  ]
//
REBNATIVE(return)
{
    INCLUDE_PARAMS_OF_RETURN;

    REBFRM *f = frame_; // implicit parameter to REBNATIVE()

    // The frame this RETURN is being called from may well not be the target
    // function of the return (that's why it's a "definitional return").  The
    // binding field of the frame contains a copy of whatever the binding was
    // in the specific ACTION! value that was invoked.
    //
    REBFRM *target_frame;
    REBNOD *f_binding = FRM_BINDING(f);
    if (not f_binding)
        fail (Error_Return_Archetype_Raw()); // must have binding to jump to

    assert(f_binding->header.bits & ARRAY_FLAG_IS_VARLIST);
    target_frame = CTX_FRAME_MAY_FAIL(CTX(f_binding));

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
    REBACT *target_fun = FRM_UNDERLYING(target_frame);

    REBVAL *v = ARG(value);

    // Defininitional returns are "locals"--there's no argument type check.
    // So TYPESET! bits in the RETURN param are used for legal return types.
    //
    REBVAL *typeset = ACT_PARAM(target_fun, ACT_NUM_PARAMS(target_fun));
    assert(VAL_PARAM_CLASS(typeset) == REB_P_RETURN);
    assert(VAL_PARAM_SYM(typeset) == SYM_RETURN);

    if (GET_ACTION_FLAG(target_fun, IS_INVISIBLE) and IS_ENDISH_NULLED(v)) {
        //
        // The only legal way invisibles can use RETURN is with no argument.
    }
    else {
        if (IS_ENDISH_NULLED(v))
            Init_Void(v); // `do [return]` acts as `return void`

        // Check type NOW instead of waiting and letting Eval_Core()
        // check it.  Reasoning is that the error can indicate the callsite,
        // e.g. the point where `return badly-typed-value` happened.
        //
        // !!! In the userspace formulation of this abstraction, it indicates
        // it's not RETURN's type signature that is constrained, as if it were
        // then RETURN would be implicated in the error.  Instead, RETURN must
        // take [<opt> any-value!] as its argument, and then report the error
        // itself...implicating the frame (in a way parallel to this native).
        //
        if (not TYPE_CHECK(typeset, VAL_TYPE(v)))
            fail (Error_Bad_Return_Type(target_frame, VAL_TYPE(v)));
    }

    assert(f_binding->header.bits & ARRAY_FLAG_IS_VARLIST);

    Move_Value(D_OUT, NAT_VALUE(unwind)); // see also Make_Thrown_Unwind_Value
    INIT_BINDING_MAY_MANAGE(D_OUT, f_binding);

    return Init_Thrown_With_Label(D_OUT, v, D_OUT);
}


//
//  typechecker: native [
//
//  {Generator for an optimized typechecking ACTION!}
//
//      return: [action!]
//      type [datatype! typeset!]
//  ]
//
REBNATIVE(typechecker)
{
    INCLUDE_PARAMS_OF_TYPECHECKER;

    REBVAL *type = ARG(type);

    REBARR *paramlist = Make_Array_Core(
        2,
        SERIES_MASK_PARAMLIST | NODE_FLAG_MANAGED
    );

    REBVAL *archetype = RESET_CELL(
        Alloc_Tail_Array(paramlist),
        REB_ACTION,
        CELL_MASK_ACTION
    );
    VAL_ACT_PARAMLIST_NODE(archetype) = NOD(paramlist);
    INIT_BINDING(archetype, UNBOUND);

    Init_Param(
        Alloc_Tail_Array(paramlist),
        REB_P_NORMAL,
        Canon(SYM_VALUE),
        TS_OPT_VALUE // Allow null (e.g. <opt>), returns false
    );

    MISC_META_NODE(paramlist) = nullptr;  // !!! auto-generate info for HELP?

    REBACT *typechecker = Make_Action(
        paramlist,
        IS_DATATYPE(type)
            ? &Datatype_Checker_Dispatcher
            : &Typeset_Checker_Dispatcher,
        nullptr, // no underlying action (use paramlist)
        nullptr, // no specialization exemplar (or inherited exemplar)
        1 // details array capacity
    );
    Move_Value(ARR_HEAD(ACT_DETAILS(typechecker)), type);

    return Init_Action_Unbound(D_OUT, typechecker);
}


//
//  chain: native [
//
//  {Create a processing pipeline of actions, each consuming the last result}
//
//      return: [action!]
//      pipeline [block!]
//          {List of actions to apply.  Reduced by default.}
//      /quote
//          {Do not reduce the pipeline--use the values as-is.}
//  ]
//
REBNATIVE(chain)
{
    INCLUDE_PARAMS_OF_CHAIN;

    REBVAL *out = D_OUT; // plan ahead for factoring into Chain_Action(out..

    REBVAL *pipeline = ARG(pipeline);
    REBARR *chainees;
    if (REF(quote))
        chainees = COPY_ANY_ARRAY_AT_DEEP_MANAGED(pipeline);
    else {
        REBDSP dsp_orig = DSP;
        if (Reduce_To_Stack_Throws(out, pipeline, VAL_SPECIFIER(pipeline)))
            return out;

        // No more evaluations *should* run before putting this array in a
        // GC-safe spot, but leave unmanaged anyway.
        //
        chainees = Pop_Stack_Values(dsp_orig); // no NODE_FLAG_MANAGED
    }

    REBVAL *first = KNOWN(ARR_HEAD(chainees));

    // !!! Current validation is that all are functions.  Should there be other
    // checks?  (That inputs match outputs in the chain?)  Should it be
    // a dialect and allow things other than functions?
    //
    REBVAL *check = first;
    while (NOT_END(check)) {
        if (not IS_ACTION(check))
            fail (check);
        ++check;
    }

    // Paramlist needs to be unique to identify the new function, but will be
    // a compatible interface with the first function in the chain.
    //
    REBARR *paramlist = Copy_Array_Shallow_Flags(
        VAL_ACT_PARAMLIST(ARR_HEAD(chainees)),
        SPECIFIED,
        SERIES_MASK_PARAMLIST | NODE_FLAG_MANAGED // flags not auto-copied
    );
    VAL_ACT_PARAMLIST_NODE(ARR_HEAD(paramlist)) = NOD(paramlist);

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
    REBCTX *meta = Copy_Context_Shallow_Managed(VAL_CONTEXT(std_meta));
    Init_Nulled(CTX_VAR(meta, STD_CHAINED_META_DESCRIPTION)); // default
    Init_Block(CTX_VAR(meta, STD_CHAINED_META_CHAINEES), chainees);
    Init_Nulled(CTX_VAR(meta, STD_CHAINED_META_CHAINEE_NAMES));
    MISC_META_NODE(paramlist) = NOD(meta);  // must init before Make_Action

    REBACT *chain = Make_Action(
        paramlist,
        &Chainer_Dispatcher,
        ACT_UNDERLYING(VAL_ACTION(first)),  // same underlying as first action
        ACT_EXEMPLAR(VAL_ACTION(first)),  // same exemplar as first action
        1  // details array capacity
    );
    Init_Block(ARR_HEAD(ACT_DETAILS(chain)), chainees);

    return Init_Action_Unbound(out, chain);
}


//
//  adapt: native [
//
//  {Create a variant of an ACTION! that preprocesses its arguments}
//
//      return: [action!]
//      adaptee [action! word! path!]
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
    const bool push_refinements = false;
    if (Get_If_Word_Or_Path_Throws(
        D_OUT,
        &opt_adaptee_name,
        adaptee,
        SPECIFIED,
        push_refinements
    )){
        return R_THROWN;
    }

    if (not IS_ACTION(D_OUT))
        fail (PAR(adaptee));
    Move_Value(adaptee, D_OUT); // Frees D_OUT, and GC safe (in ARG slot)

    // The paramlist needs to be unique to designate this function, but
    // will be identical typesets to the original.  It's [0] element must
    // identify the function we're creating vs the original, however.
    //
    REBARR *paramlist = Copy_Array_Shallow_Flags(
        VAL_ACT_PARAMLIST(adaptee),
        SPECIFIED,
        SERIES_MASK_PARAMLIST
            | (SER(VAL_ACTION(adaptee))->header.bits & PARAMLIST_MASK_INHERIT)
            | NODE_FLAG_MANAGED
    );
    VAL_ACT_PARAMLIST_NODE(ARR_HEAD(paramlist)) = NOD(paramlist);

    // See %sysobj.r for `adapted-meta:` object template

    REBVAL *example = Get_System(SYS_STANDARD, STD_ADAPTED_META);

    REBCTX *meta = Copy_Context_Shallow_Managed(VAL_CONTEXT(example));
    Init_Nulled(CTX_VAR(meta, STD_ADAPTED_META_DESCRIPTION)); // default
    Move_Value(CTX_VAR(meta, STD_ADAPTED_META_ADAPTEE), adaptee);
    if (opt_adaptee_name == NULL)
        Init_Nulled(CTX_VAR(meta, STD_ADAPTED_META_ADAPTEE_NAME));
    else
        Init_Word(
            CTX_VAR(meta, STD_ADAPTED_META_ADAPTEE_NAME),
            opt_adaptee_name
        );

    MISC_META_NODE(paramlist) = NOD(meta);

    REBACT *underlying = ACT_UNDERLYING(VAL_ACTION(adaptee));

    REBACT *adaptation = Make_Action(
        paramlist,
        &Adapter_Dispatcher,
        underlying,  // same underlying as adaptee
        ACT_EXEMPLAR(VAL_ACTION(adaptee)),  // same exemplar as adaptee
        2  // details array capacity => [prelude, adaptee]
    );

    // !!! In a future branch it may be possible that specific binding allows
    // a read-only input to be "viewed" with a relative binding, and no copy
    // would need be made if input was R/O.  For now, we copy to relativize.
    //
    REBARR *prelude = Copy_And_Bind_Relative_Deep_Managed(
        ARG(prelude),
        ACT_PARAMLIST(underlying), // relative bindings ALWAYS use underlying
        TS_WORD
    );

    REBARR *details = ACT_DETAILS(adaptation);

    REBVAL *block = RESET_CELL(
        ARR_AT(details, 0),
        REB_BLOCK,
        CELL_FLAG_FIRST_IS_NODE
    );
    INIT_VAL_NODE(block, prelude);
    VAL_INDEX(block) = 0;
    INIT_BINDING(block, underlying); // relative binding

    Move_Value(ARR_AT(details, 1), adaptee);

    return Init_Action_Unbound(D_OUT, adaptation);
}


//
//  enclose: native [
//
//  {Wrap code around an ACTION! with access to its FRAME! and return value}
//
//      return: [action!]
//      inner [action! word! path!]
//          {Action that a FRAME! will be built for, then passed to OUTER}
//      outer [action! word! path!]
//          {Gets a FRAME! for INNER before invocation, can DO it (or not)}
//  ]
//
REBNATIVE(enclose)
{
    INCLUDE_PARAMS_OF_ENCLOSE;

    REBVAL *inner = ARG(inner);
    REBSTR *opt_inner_name;
    const bool push_refinements = false;
    if (Get_If_Word_Or_Path_Throws(
        D_OUT,
        &opt_inner_name,
        inner,
        SPECIFIED,
        push_refinements
    )){
        return R_THROWN;
    }

    if (not IS_ACTION(D_OUT))
        fail (PAR(inner));
    Move_Value(inner, D_OUT); // Frees D_OUT, and GC safe (in ARG slot)

    REBVAL *outer = ARG(outer);
    REBSTR *opt_outer_name;
    if (Get_If_Word_Or_Path_Throws(
        D_OUT,
        &opt_outer_name,
        outer,
        SPECIFIED,
        push_refinements
    )){
        return R_THROWN;
    }

    if (not IS_ACTION(D_OUT))
        fail (PAR(outer));
    Move_Value(outer, D_OUT); // Frees D_OUT, and GC safe (in ARG slot)

    // The paramlist needs to be unique to designate this function, but
    // will be identical typesets to the inner.  It's [0] element must
    // identify the function we're creating vs the original, however.
    //
    REBARR *paramlist = Copy_Array_Shallow_Flags(
        VAL_ACT_PARAMLIST(inner),
        SPECIFIED,
        SERIES_MASK_PARAMLIST | NODE_FLAG_MANAGED
    );
    REBVAL *rootparam = KNOWN(ARR_HEAD(paramlist));
    VAL_ACT_PARAMLIST_NODE(rootparam) = NOD(paramlist);

    // See %sysobj.r for `enclosed-meta:` object template

    REBVAL *example = Get_System(SYS_STANDARD, STD_ENCLOSED_META);

    REBCTX *meta = Copy_Context_Shallow_Managed(VAL_CONTEXT(example));
    Init_Nulled(CTX_VAR(meta, STD_ENCLOSED_META_DESCRIPTION)); // default
    Move_Value(CTX_VAR(meta, STD_ENCLOSED_META_INNER), inner);
    if (opt_inner_name == NULL)
        Init_Nulled(CTX_VAR(meta, STD_ENCLOSED_META_INNER_NAME));
    else
        Init_Word(
            CTX_VAR(meta, STD_ENCLOSED_META_INNER_NAME),
            opt_inner_name
        );
    Move_Value(CTX_VAR(meta, STD_ENCLOSED_META_OUTER), outer);
    if (opt_outer_name == NULL)
        Init_Nulled(CTX_VAR(meta, STD_ENCLOSED_META_OUTER_NAME));
    else
        Init_Word(
            CTX_VAR(meta, STD_ENCLOSED_META_OUTER_NAME),
            opt_outer_name
        );

    MISC_META_NODE(paramlist) = NOD(meta);

    REBACT *enclosure = Make_Action(
        paramlist,
        &Encloser_Dispatcher,
        ACT_UNDERLYING(VAL_ACTION(inner)),  // same underlying as inner
        ACT_EXEMPLAR(VAL_ACTION(inner)),  // same exemplar as inner
        2  // details array capacity => [inner, outer]
    );

    REBARR *details = ACT_DETAILS(enclosure);
    Move_Value(ARR_AT(details, 0), inner);
    Move_Value(ARR_AT(details, 1), outer);

    return Init_Action_Unbound(D_OUT, enclosure);
}


//
//  hijack: native [
//
//  {Cause all existing references to an ACTION! to invoke another ACTION!}
//
//      return: [<opt> action!]
//          {The hijacked action value, null if self-hijack (no-op)}
//      victim [action! word! path!]
//          {Action value whose references are to be affected.}
//      hijacker [action! word! path!]
//          {The action to run in its place}
//  ]
//
REBNATIVE(hijack)
//
// Hijacking an action does not change its interface--and cannot.  While
// it may seem tempting to use low-level tricks to keep the same paramlist
// but add or remove parameters, parameter lists can be referenced many
// places in the system (frames, specializations, adaptations) and can't
// be corrupted...or the places that rely on their properties (number and
// types of parameters) would get out of sync.
{
    INCLUDE_PARAMS_OF_HIJACK;

    REBSTR *opt_victim_name;
    const bool push_refinements = false;
    if (Get_If_Word_Or_Path_Throws(
        D_OUT,
        &opt_victim_name,
        ARG(victim),
        SPECIFIED,
        push_refinements
    )){
        return R_THROWN;
    }

    if (not IS_ACTION(D_OUT))
        fail ("Victim of HIJACK must be an ACTION!");
    Move_Value(ARG(victim), D_OUT); // Frees up D_OUT
    REBACT *victim = VAL_ACTION(ARG(victim)); // GC safe (in ARG slot)

    REBSTR *opt_hijacker_name;
    if (Get_If_Word_Or_Path_Throws(
        D_OUT,
        &opt_hijacker_name,
        ARG(hijacker),
        SPECIFIED,
        push_refinements
    )){
        return R_THROWN;
    }

    if (not IS_ACTION(D_OUT))
        fail ("Hijacker in HIJACK must be an ACTION!");
    Move_Value(ARG(hijacker), D_OUT); // Frees up D_OUT
    REBACT *hijacker = VAL_ACTION(ARG(hijacker)); // GC safe (in ARG slot)

    if (victim == hijacker)
        return nullptr; // permitting no-op hijack has some practical uses

    REBARR *victim_paramlist = ACT_PARAMLIST(victim);
    REBARR *victim_details = ACT_DETAILS(victim);
    REBARR *hijacker_paramlist = ACT_PARAMLIST(hijacker);
    REBARR *hijacker_details = ACT_DETAILS(hijacker);

    if (ACT_UNDERLYING(hijacker) == ACT_UNDERLYING(victim)) {
        //
        // Should the underliers of the hijacker and victim match, that means
        // any ADAPT or CHAIN or SPECIALIZE of the victim can work equally
        // well if we just use the hijacker's dispatcher directly.  This is a
        // reasonably common case, and especially common when putting the
        // originally hijacked function back.

        LINK_UNDERLYING_NODE(victim_paramlist)
            = LINK_UNDERLYING_NODE(hijacker_paramlist);
        if (LINK_SPECIALTY(hijacker_details) == hijacker_paramlist)
            LINK_SPECIALTY_NODE(victim_details) = NOD(victim_paramlist);
        else
            LINK_SPECIALTY_NODE(victim_details)
                = LINK_SPECIALTY_NODE(hijacker_details);

        MISC(victim_details).dispatcher = MISC(hijacker_details).dispatcher;

        // All function info arrays should live in cells with the same
        // underlying formatting.  Blit_Cell ensures that's the case.
        //
        // !!! It may be worth it to optimize some dispatchers to depend on
        // ARR_SINGLE(info) being correct.  That would mean hijack reversals
        // would need to restore the *exact* capacity.  Review.

        REBCNT details_len = ARR_LEN(hijacker_details);
        if (SER_REST(SER(victim_details)) < details_len + 1)
            EXPAND_SERIES_TAIL(
                SER(victim_details),
                details_len + 1 - SER_REST(SER(victim_details))
            );

        RELVAL *src = ARR_HEAD(hijacker_details);
        RELVAL *dest = ARR_HEAD(victim_details);
        for (; NOT_END(src); ++src, ++dest)
            Blit_Cell(dest, src);
        TERM_ARRAY_LEN(victim_details, details_len);
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
        MISC(victim_details).dispatcher = &Hijacker_Dispatcher;

        if (ARR_LEN(victim_details) < 1)
            Alloc_Tail_Array(victim_details);
        Move_Value(ARR_HEAD(victim_details), ARG(hijacker));
        TERM_ARRAY_LEN(victim_details, 1);
    }

    // !!! What should be done about MISC(victim_paramlist).meta?  Leave it
    // alone?  Add a note about the hijacking?  Also: how should binding and
    // hijacking interact?

    return Init_Action_Maybe_Bound(D_OUT, victim, VAL_BINDING(ARG(hijacker)));
}


//
//  variadic?: native [
//
//  {Returns TRUE if an ACTION! may take a variable number of arguments.}
//
//      return: [logic!]
//      action [action!]
//  ]
//
REBNATIVE(variadic_q)
{
    INCLUDE_PARAMS_OF_VARIADIC_Q;

    REBVAL *param = VAL_ACT_PARAMS_HEAD(ARG(action));
    for (; NOT_END(param); ++param) {
        if (Is_Param_Variadic(param))
            return Init_True(D_OUT);
    }

    return Init_False(D_OUT);
}


//
//   skinner-return-helper: native [
//
//   {Internal function that pushes a deferred callback for return type check}
//
//       returned [<opt> any-value!]
//
//   ]
//
REBNATIVE(skinner_return_helper)
{
    INCLUDE_PARAMS_OF_SKINNER_RETURN_HELPER;

    REBFRM *f = frame_;
    REBVAL *v = ARG(returned);

    // !!! Same code as in Returner_Dispatcher()...should it be moved to a
    // shared inline location?

    REBACT *phase = ACT(FRM_BINDING(f));

    REBVAL *param = ACT_PARAM(phase, ACT_NUM_PARAMS(phase));
    assert(VAL_PARAM_SYM(param) == SYM_RETURN);

    // Typeset bits for locals in frames are usually ignored, but the RETURN:
    // local uses them for the return types of a function.
    //
    if (not Typecheck_Including_Quoteds(param, v))
        fail (Error_Bad_Return_Type(f, VAL_TYPE(v)));

    RETURN (v);
}


//
//  Skinner_Dispatcher: C
//
// Reskinned functions may expand what types the original function took, in
// which case the typechecking the skinned function did may not be enough for
// any parameters that appear to be ARG_MARKED_CHECKED in the frame...they
// were checked against the expanded criteria, not that of the original
// function.  So it has to clear the ARG_MARKED_CHECKED off any of those
// parameters it finds...so if they wind up left in the frame the evaluator
// still knows it has to recheck them.
//
REB_R Skinner_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    REBVAL *skinned = KNOWN(ARR_HEAD(details));

    REBVAL *param = ACT_PARAMS_HEAD(FRM_PHASE(f));
    REBVAL *arg = FRM_ARGS_HEAD(f);
    for (; NOT_END(param); ++param, ++arg) {
        if (TYPE_CHECK(param, REB_TS_SKIN_EXPANDED))
            CLEAR_CELL_FLAG(arg, ARG_MARKED_CHECKED);
    }

    // If the return type has been expanded, then the only way we're going to
    // get a chance to check it is by pushing some kind of handler here for
    // it.  It has to be a 1-argument function, and it needs enough of an
    // identity to know which return type it's checking.  :-/  We cheat and
    // use the binding to find the paramlist we wish to check.
    //
    // !!! This is kind of an ugly hack, because this action is now a
    // "relative value"...and no actions are supposed to be relative to
    // parameter lists.  But we couldn't use the frame even if we wanted to,
    // the phase is getting overwritten so we couldn't find the return.  So
    // just hope that it stays on the stack and doesn't do much besides
    // get dropped by that processing, which can account for it.
    //
    Init_Action_Maybe_Bound(
        DS_PUSH(),
        NAT_ACTION(skinner_return_helper),
        NOD(FRM_PHASE(f))
    );

    INIT_FRM_PHASE(f, VAL_ACTION(skinned));

    // We captured the binding for the skin when the action was made; if the
    // user rebound the action, then don't overwrite with the one in the
    // initial skin--assume they meant to change it.

    // If we frame checked now, we'd fail, because we just put the new phase
    // into place with more restricted types.  Let the *next* check kick in,
    // and it will now react to the cleared ARG_MARKED_CHECKED flags.
    //
    return R_REDO_UNCHECKED;
}


//
//  reskinned: native [
//
//  {Returns alias of an ACTION! with modified typing for the given parameter}
//
//      return: "A new action value with the modified parameter conventions"
//          [action!]
//      skin "Mutation spec, e.g. [param1 @add [integer!] 'param2 [tag!]]"
//          [block!]
//      action [action!]
//  ]
//
REBNATIVE(reskinned)
//
// This avoids having to create a usermode function stub for something where
// the only difference is a parameter convention (e.g. an identical function
// that quotes its third argument doesn't actually need a new body).
//
// Care should be taken not to allow the expansion of parameter types accepted
// to allow passing unexpected types to a native, because it could crash.  At
// least for natives, accepted types should only be able to be narrowed.
//
// Keeps the parameter types and help notes in sync, also.
{
    INCLUDE_PARAMS_OF_RESKINNED;

    REBACT *original = VAL_ACTION(ARG(action));

    // We make a copy of the ACTION's paramlist vs. trying to fiddle the
    // action in place.  One reason to do this is that there'd have to be code
    // written to account for the caching done by Make_Action() based on the
    // parameters and their conventions (e.g. PARAMLIST_QUOTES_FIRST),
    // and we don't want to try and update all that here and get it wrong.
    //
    // Another good reason is that if something messes up halfway through
    // the transformation process, the partially built new action gets thrown
    // out.  It would not be atomic if we were fiddling bits directly in
    // something the user already has pointers to.
    //
    // Another reason is to give the skin its own dispatcher, so it can take
    // responsibility for any performance hit incurred by extra type checking
    // that has to be done due to its meddling.  Typically if you ADAPT a
    // function and the frame is fulfilled, with ARG_MARKED_CHECKED on an
    // argument, it's known that there's no point in checking it again if
    // the arg doesn't get freshly overwritten.  Reskinning changes that.
    //
    // !!! Note: Typechecking today is nearly as cheap as the check to avoid
    // it, but the attempt to avoid typechecking is based on a future belief
    // of a system in which the checks are more expensive...which it will be
    // if it has to search hierarchies or lists of quoted forms/etc.
    //
    REBARR *paramlist = Copy_Array_Shallow_Flags(
        ACT_PARAMLIST(original),
        SPECIFIED, // no relative values in parameter lists
        SERIES_MASK_PARAMLIST
            | (SER(original)->header.bits & PARAMLIST_MASK_INHERIT)
    );

    bool need_skin_phase = false; // only needed if types were broadened

    RELVAL *param = ARR_AT(paramlist, 1); // first param (0 is ACT_ARCHETYPE)
    RELVAL *item = VAL_ARRAY_AT(ARG(skin));
    Reb_Param_Class pclass;
    while (NOT_END(item)) {
        bool change;
        if (
            KIND_BYTE(item) != REB_SYM_WORD
            or VAL_WORD_SYM(item) != SYM_CHANGE
        ){
            change = false;
        }
        else {
            change = true;
            ++item;
        }

        if (IS_WORD(item))
            pclass = REB_P_NORMAL;
        else if (IS_SET_WORD(item))
            pclass = REB_P_RETURN;
        else if (IS_GET_WORD(item))
            pclass = REB_P_HARD_QUOTE;
        else if (
            IS_QUOTED(item)
            and VAL_NUM_QUOTES(item) == 1
            and CELL_KIND(VAL_UNESCAPED(item)) == REB_WORD
        ){
            pclass = REB_P_SOFT_QUOTE;
        }
        else
            fail (Error_Bad_Value_Core(item, VAL_SPECIFIER(ARG(skin))));

        REBSTR *canon = VAL_WORD_CANON(VAL_UNESCAPED(item));

        // We assume user gives us parameters in order, but if they don't we
        // cycle around to the beginning again.  So it's most efficient if
        // in order, but still works if not.

        bool wrapped_around = false;
        while (true) {
            if (IS_END(param)) {
                if (wrapped_around) {
                    DECLARE_LOCAL (word);
                    Init_Word(word, canon);
                    fail (word);
                }

                param = ARR_AT(paramlist, 1);
                wrapped_around = true;
            }

            if (VAL_PARAM_CANON(param) == canon)
                break;
            ++param;
        }

        // Got a match and a potential new parameter class.  Don't let the
        // class be changed on accident just because they forgot to use the
        // right marking, require an instruction.  (Better names needed, these
        // were just already in %words.r)

        if (pclass != KIND_BYTE(param)) {
            assert(MIRROR_BYTE(param) == REB_TYPESET);
            if (change)
                mutable_KIND_BYTE(param) = pclass;
            else if (pclass != REB_P_NORMAL) // assume plain word is no change
                fail ("If parameter convention is reskinned, use #change");
        }

        ++item;

        // The next thing is either a BLOCK! (in which case we take its type
        // bits verbatim), or @add or @remove, so you can tweak w.r.t. just
        // some bits.

        REBSYM sym = SYM_0;
        if (REB_SYM_WORD == KIND_BYTE(item)) {
            sym = VAL_WORD_SYM(item);
            if (sym != SYM_REMOVE and sym != SYM_ADD)
                fail ("RESKIN only supports @add and @remove instructions");
            ++item;
        }

        if (REB_BLOCK != KIND_BYTE(item)) {
            if (change) // [@change 'arg] is okay w/no block
                continue;
            fail ("Expected BLOCK! after instruction");
        }

        REBSPC *specifier = VAL_SPECIFIER(item);

        switch (sym) {
          case SYM_0: // completely override type bits
            VAL_TYPESET_LOW_BITS(param) = 0;
            VAL_TYPESET_HIGH_BITS(param) = 0;
            Add_Typeset_Bits_Core(param, VAL_ARRAY_AT(item), specifier);
            TYPE_SET(param, REB_TS_SKIN_EXPANDED);
            need_skin_phase = true; // !!! Worth it to check for expansion?
            break;

          case SYM_ADD: // leave existing bits, add new ones
            Add_Typeset_Bits_Core(param, VAL_ARRAY_AT(item), specifier);
            TYPE_SET(param, REB_TS_SKIN_EXPANDED);
            need_skin_phase = true;
            break;

          case SYM_REMOVE: {
            DECLARE_LOCAL (temp); // make temporary typeset, remove its bits
            Init_Typeset(temp, 0);
            Add_Typeset_Bits_Core(temp, VAL_ARRAY_AT(item), specifier);

            VAL_TYPESET_LOW_BITS(param) &= ~VAL_TYPESET_LOW_BITS(temp);
            VAL_TYPESET_HIGH_BITS(param) &= ~VAL_TYPESET_HIGH_BITS(temp);

            // ENCLOSE doesn't type check the return result by default.  So
            // if you constrain the return types, there will have to be a
            // phase to throw a check into the stack.  Otherwise, constraining
            // types is no big deal...any type that passed the narrower check
            // will pass the broader one.
            //
            if (VAL_PARAM_SYM(param) == SYM_RETURN)
                need_skin_phase = true;
            break; }

          default:
            assert(false);
        }

        ++item;
    }

    // The most sensible case for a type-expanding reskin is if there is some
    // amount of injected usermode code to narrow the type back to something
    // the original function can deal with.  It might be argued that usermode
    // code would have worked on more types than it annotated, and you may
    // know that and be willing to risk an error if you're wrong.  But with
    // a native--if you give it types it doesn't expect--it can crash.
    //
    // Hence we abide by the type contract, and need a phase to check that
    // we are honoring it.  The only way to guarantee we get that phase is if
    // we're using something that already does the checks...e.g. an Adapter
    // or an Encloser.
    //
    // (Type-narrowing and quoting convention changing things are fine, there
    // is no risk posed to the underlying action call.)
    //
    if (ACT_DISPATCHER(original) == &Skinner_Dispatcher)
        need_skin_phase = false; // already taken care of, reuse it
    else if (
        need_skin_phase and (
            ACT_DISPATCHER(original) != &Adapter_Dispatcher
            and ACT_DISPATCHER(original) != &Encloser_Dispatcher
        )
    ){
        fail ("Type-expanding RESKIN only works on ADAPT/ENCLOSE actions");
    }

    if (not need_skin_phase) // inherit the native flag if no phase change
        SER(paramlist)->header.bits
            |= SER(original)->header.bits & PARAMLIST_FLAG_IS_NATIVE;

    RELVAL *rootparam = ARR_HEAD(paramlist);
    SER(paramlist)->header.bits &= ~PARAMLIST_MASK_CACHED;
    VAL_ACT_PARAMLIST_NODE(rootparam) = NOD(paramlist);
    INIT_BINDING(rootparam, UNBOUND);

    // !!! This does not make a unique copy of the meta information context.
    // Hence updates to the title/parameter-descriptions/etc. of the tightened
    // function will affect the original, and vice-versa.
    //
    MISC_META_NODE(paramlist) = NOD(ACT_META(original));

    Manage_Array(paramlist);

    // If we only *narrowed* the type conventions, then we don't need to put
    // in a new dispatcher.  But if we *expanded* them, the type checking
    // done by the skinned version for ARG_MARKED_CHECKED may not be enough.
    //
    REBCNT details_len = need_skin_phase ? 1 :ARR_LEN(ACT_DETAILS(original));
    REBACT *defers = Make_Action(
        paramlist,
        need_skin_phase ? &Skinner_Dispatcher : ACT_DISPATCHER(original),
        ACT_UNDERLYING(original), // !!! ^-- notes above may be outdated
        ACT_EXEMPLAR(original), // don't add to the original's specialization
        details_len // details array capacity
    );

    if (need_skin_phase)
        Move_Value(ARR_HEAD(ACT_DETAILS(defers)), ARG(action));
    else {
        // We're reusing the original dispatcher, so also reuse the original
        // function body.  Note Blit_Cell() ensures that the cell formatting
        // on the source and target are the same, and it preserves relative
        // value information (rarely what you meant, but it's meant here).
        //
        RELVAL *src = ARR_HEAD(ACT_DETAILS(original));
        RELVAL *dest = ARR_HEAD(ACT_DETAILS(defers));
        for (; NOT_END(src); ++src, ++dest)
            Blit_Cell(dest, src);
    }

    TERM_ARRAY_LEN(ACT_DETAILS(defers), details_len);

    return Init_Action_Maybe_Bound(
        D_OUT,
        defers, // REBACT* archetype doesn't contain a binding
        VAL_BINDING(ARG(action)) // inherit binding (user can rebind)
    );
}


//
//  tweak: native [
//
//  {Modify a special property (currently only for ACTION!)}
//
//      return: "Same action identity as input"
//          [action!]
//      action "(modified) Action to modify property of"
//          [action!]
//      property "Currently must be [defer postpone]"
//          [word!]
//      enable [logic!]
//  ]
//
REBNATIVE(tweak)
{
    INCLUDE_PARAMS_OF_TWEAK;

    REBACT *act = VAL_ACTION(ARG(action));
    REBVAL *first = First_Unspecialized_Param(act);
    if (not first)
        fail ("Cannot TWEAK action enfix behavior unless it has >= 1 params");

    Reb_Param_Class pclass = VAL_PARAM_CLASS(first);
    REBFLGS flag;

    switch (VAL_WORD_SYM(ARG(property))) {
      case SYM_DEFER: // Special enfix behavior used by THEN, ELSE, ALSO...
        if (pclass != REB_P_NORMAL)
            fail ("TWEAK defer only actions with evaluative 1st params");
        flag = PARAMLIST_FLAG_DEFERS_LOOKBACK;
        break;

      case SYM_POSTPONE: // Wait as long as it can to run w/o changing order
        if (pclass != REB_P_NORMAL and pclass != REB_P_SOFT_QUOTE)
            fail ("TWEAK postpone only actions with evaluative 1st params");
        flag = PARAMLIST_FLAG_POSTPONES_ENTIRELY;
        break;

      default:
        fail ("TWEAK currently only supports [defer postpone]");
    }

    if (VAL_LOGIC(ARG(enable)))
        SER(act)->header.bits |= flag;
    else
        SER(act)->header.bits &= ~flag;

    RETURN (ARG(action));
}


REB_R Downshot_Dispatcher(REBFRM *f) // runs until count is reached
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == 1);

    RELVAL *n = ARR_HEAD(details);
    if (VAL_INT64(n) == 0)
        return nullptr; // always return null once 0 is reached
    --VAL_INT64(n);

    REBVAL *code = FRM_ARG(f, 1);
    if (Do_Branch_Throws(f->out, code))
        return R_THROWN;

    return Voidify_If_Nulled(f->out);
}


REB_R Upshot_Dispatcher(REBFRM *f) // won't run until count is reached
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == 1);

    RELVAL *n = ARR_HEAD(details);
    if (VAL_INT64(n) < 0) {
        ++VAL_INT64(ARR_HEAD(details));
        return nullptr; // return null until 0 is reached
    }

    REBVAL *code = FRM_ARG(f, 1);
    if (Do_Branch_Throws(f->out, code))
        return R_THROWN;

    return Voidify_If_Nulled(f->out);
}


//
//  n-shot: native [
//
//  {Create a DO variant that executes what it's given for N times}
//
//      n "Number of times to execute before being a no-op"
//          [integer!]
//  ]
//
REBNATIVE(n_shot)
{
    INCLUDE_PARAMS_OF_N_SHOT;

    REBI64 n = VAL_INT64(ARG(n));

    REBARR *paramlist = Make_Array_Core(
        2,
        SERIES_MASK_PARAMLIST | NODE_FLAG_MANAGED
    );

    REBVAL *archetype = RESET_CELL(
        Alloc_Tail_Array(paramlist),
        REB_ACTION,
        CELL_MASK_ACTION
    );
    VAL_ACT_PARAMLIST_NODE(archetype) = NOD(paramlist);
    INIT_BINDING(archetype, UNBOUND);

    // !!! Should anything DO would accept be legal, as DOES would run?
    //
    Init_Param(
        Alloc_Tail_Array(paramlist),
        REB_P_NORMAL,
        Canon(SYM_VALUE), // !!! would SYM_CODE be better?
        FLAGIT_KIND(REB_BLOCK) | FLAGIT_KIND(REB_ACTION)
    );

    MISC_META_NODE(paramlist) = nullptr;  // !!! auto-generate info for HELP?

    REBACT *n_shot = Make_Action(
        paramlist,
        n >= 0 ? &Downshot_Dispatcher : &Upshot_Dispatcher,
        nullptr, // no underlying action (use paramlist)
        nullptr, // no specialization exemplar (or inherited exemplar)
        1 // details array capacity
    );
    Init_Integer(ARR_HEAD(ACT_DETAILS(n_shot)), n);

    return Init_Action_Unbound(D_OUT, n_shot);
}
