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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
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
//          [block!]
//  ]
//
DECLARE_NATIVE(func)
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
//  Make_Thrown_Unwind_Value: C
//
// This routine will generate a THROWN() value that can be used to indicate
// a desire to jump to a particular level in the stack with a value (or void)
//
// It is used in the implementation of the UNWIND native.
//
void Make_Thrown_Unwind_Value(
    Value* out,
    const Value* level, // FRAME!, ACTION! (or INTEGER! relative to frame)
    const Value* value,
    REBFRM *frame // required if level is INTEGER! or ACTION!
) {
    Move_Value(out, NAT_VALUE(unwind));

    if (IS_FRAME(level)) {
        INIT_BINDING(out, VAL_CONTEXT(level));
    }
    else if (IS_INTEGER(level)) {
        REBLEN count = VAL_INT32(level);
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
                INIT_BINDING(out, f->varlist);
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
                INIT_BINDING(out, f->varlist);
                break;
            }
        }
    }

    CONVERT_NAME_TO_THROWN(out, value);
}


//
//  unwind: native [
//
//  {Jump up the stack to return from a specific frame or call.}
//
//      level "Frame, action, or index to exit from"
//          [frame! action! integer!]
//      /with "Result for enclosing state (default is void)"
//      value [any-value!]
//  ]
//
DECLARE_NATIVE(unwind)
//
// UNWIND is implemented via a THROWN() value that bubbles through the stack.
// Using UNWIND's action cell with a target `binding` field is the
// protocol understood by Eval_Core to catch a throw itself.
//
// !!! Allowing to pass an INTEGER! to jump from a function based on its
// BACKTRACE number is a bit low-level, and perhaps should be restricted to
// a debugging mode (though it is a useful tool in "code golf").
{
    INCLUDE_PARAMS_OF_UNWIND;

    UNUSED(REF(with)); // implied by non-null value

    Make_Thrown_Unwind_Value(D_OUT, ARG(level), ARG(value), frame_);
    return R_THROWN;
}


//
//  return: native [
//
//  {RETURN, giving a result to the caller}
//
//      value "If no argument is given, result will be a trash"
//          [<end> <opt> any-value!]
//  ]
//
DECLARE_NATIVE(return)
{
    INCLUDE_PARAMS_OF_RETURN;

    REBFRM *f = frame_; // implicit parameter to DECLARE_NATIVE()

    // The frame this RETURN is being called from may well not be the target
    // function of the return (that's why it's a "definitional return").  The
    // binding field of the frame contains a copy of whatever the binding was
    // in the specific ACTION! value that was invoked.
    //
    REBFRM *target_frame;
    REBNOD *f_binding = FRM_BINDING(f);
    if (not f_binding)
        fail (Error_Return_Archetype_Raw()); // must have binding to jump to

    assert(f_binding->header.bits & ARRAY_FLAG_VARLIST);
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

    Value* v = ARG(value);

    // Defininitional returns are "locals"--there's no argument type check.
    // So TYPESET! bits in the RETURN param are used for legal return types.
    //
    Value* typeset = ACT_PARAM(target_fun, ACT_NUM_PARAMS(target_fun));
    assert(VAL_PARAM_CLASS(typeset) == PARAM_CLASS_RETURN);
    assert(Cell_Parameter_Id(typeset) == SYM_RETURN);

    if (
        GET_ACT_FLAG(target_fun, ACTION_FLAG_INVISIBLE)
        and IS_ENDISH_NULLED(v)
    ){
        // The only legal way invisibles can use RETURN is with no argument.
    }
    else {
        if (IS_ENDISH_NULLED(v))
            Init_Trash(v);  // `do [return]` acts as `return trash`

        // Check type NOW instead of waiting and letting Eval_Core_Throws()
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

    assert(f_binding->header.bits & ARRAY_FLAG_VARLIST);

    Move_Value(D_OUT, NAT_VALUE(unwind)); // see also Make_Thrown_Unwind_Value
    INIT_BINDING_MAY_MANAGE(D_OUT, f_binding);

    CONVERT_NAME_TO_THROWN(D_OUT, v);
    return R_THROWN;
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
DECLARE_NATIVE(typechecker)
{
    INCLUDE_PARAMS_OF_TYPECHECKER;

    Value* type = ARG(type);

    Array* paramlist = Make_Arr_Core(
        2,
        SERIES_MASK_ACTION | NODE_FLAG_MANAGED
    );

    Value* archetype = RESET_CELL(Alloc_Tail_Array(paramlist), REB_ACTION);
    archetype->payload.action.paramlist = paramlist;
    INIT_BINDING(archetype, UNBOUND);

    Value* param = Init_Typeset(
        Alloc_Tail_Array(paramlist),
        TS_OPT_VALUE, // Allow null (e.g. <opt>), returns false
        Canon(SYM_VALUE)
    );
    INIT_VAL_PARAM_CLASS(param, PARAM_CLASS_NORMAL);
    assert(not Is_Param_Endable(param));

    MISC(paramlist).meta = nullptr;  // !!! auto-generate info for HELP?

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
DECLARE_NATIVE(chain)
{
    INCLUDE_PARAMS_OF_CHAIN;

    Value* out = D_OUT; // plan ahead for factoring into Chain_Action(out..

    Value* pipeline = ARG(pipeline);
    Array* chainees;
    if (REF(quote))
        chainees = COPY_ANY_ARRAY_AT_DEEP_MANAGED(pipeline);
    else {
        REBDSP dsp_orig = DSP;
        if (Reduce_To_Stack_Throws(out, pipeline, REDUCE_MASK_NONE))
            return out;

        // No more evaluations *should* run before putting this array in a
        // GC-safe spot, but leave unmanaged anyway.
        //
        chainees = Pop_Stack_Values(dsp_orig); // no NODE_FLAG_MANAGED
    }

    Value* first = KNOWN(ARR_HEAD(chainees));

    // !!! Current validation is that all are functions.  Should there be other
    // checks?  (That inputs match outputs in the chain?)  Should it be
    // a dialect and allow things other than functions?
    //
    Value* check = first;
    while (NOT_END(check)) {
        if (not IS_ACTION(check))
            fail (Error_Invalid(check));
        ++check;
    }

    // Paramlist needs to be unique to identify the new function, but will be
    // a compatible interface with the first function in the chain.
    //
    Array* paramlist = Copy_Array_Shallow_Flags(
        VAL_ACT_PARAMLIST(ARR_HEAD(chainees)),
        SPECIFIED,
        SERIES_MASK_ACTION | NODE_FLAG_MANAGED // flags not auto-copied
    );
    ARR_HEAD(paramlist)->payload.action.paramlist = paramlist;

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
    Value* std_meta = Get_System(SYS_STANDARD, STD_CHAINED_META);
    REBCTX *meta = Copy_Context_Shallow_Managed(VAL_CONTEXT(std_meta));
    Init_Nulled(CTX_VAR(meta, STD_CHAINED_META_DESCRIPTION)); // default
    Init_Block(CTX_VAR(meta, STD_CHAINED_META_CHAINEES), chainees);
    Init_Nulled(CTX_VAR(meta, STD_CHAINED_META_CHAINEE_NAMES));
    MISC(paramlist).meta = meta; // must initialize before Make_Action

    REBACT *chain = Make_Action(
        paramlist,
        &Chainer_Dispatcher,
        ACT_UNDERLYING(VAL_ACTION(first)), // same underlying as first action
        ACT_EXEMPLAR(VAL_ACTION(first)), // same exemplar as first action
        1 // details array capacity
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
DECLARE_NATIVE(adapt)
{
    INCLUDE_PARAMS_OF_ADAPT;

    Value* adaptee = ARG(adaptee);

    Symbol* opt_adaptee_name;
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
        fail (Error_Invalid(adaptee));
    Move_Value(adaptee, D_OUT); // Frees D_OUT, and GC safe (in ARG slot)

    // The paramlist needs to be unique to designate this function, but
    // will be identical typesets to the original.  It's [0] element must
    // identify the function we're creating vs the original, however.
    //
    Array* paramlist = Copy_Array_Shallow_Flags(
        VAL_ACT_PARAMLIST(adaptee),
        SPECIFIED,
        SERIES_MASK_ACTION | NODE_FLAG_MANAGED
    );
    ARR_HEAD(paramlist)->payload.action.paramlist = paramlist;

    // See %sysobj.r for `adapted-meta:` object template

    Value* example = Get_System(SYS_STANDARD, STD_ADAPTED_META);

    REBCTX *meta = Copy_Context_Shallow_Managed(VAL_CONTEXT(example));
    Init_Nulled(CTX_VAR(meta, STD_ADAPTED_META_DESCRIPTION)); // default
    Move_Value(CTX_VAR(meta, STD_ADAPTED_META_ADAPTEE), adaptee);
    if (opt_adaptee_name == nullptr)
        Init_Nulled(CTX_VAR(meta, STD_ADAPTED_META_ADAPTEE_NAME));
    else
        Init_Word(
            CTX_VAR(meta, STD_ADAPTED_META_ADAPTEE_NAME),
            opt_adaptee_name
        );

    MISC(paramlist).meta = meta;

    REBACT *underlying = ACT_UNDERLYING(VAL_ACTION(adaptee));

    REBACT *adaptation = Make_Action(
        paramlist,
        &Adapter_Dispatcher,
        underlying, // same underlying as adaptee
        ACT_EXEMPLAR(VAL_ACTION(adaptee)), // same exemplar as adaptee
        2 // details array capacity => [prelude, adaptee]
    );

    // !!! In a future branch it may be possible that specific binding allows
    // a read-only input to be "viewed" with a relative binding, and no copy
    // would need be made if input was R/O.  For now, we copy to relativize.
    //
    Array* prelude = Copy_And_Bind_Relative_Deep_Managed(
        ARG(prelude),
        ACT_PARAMLIST(underlying), // relative bindings ALWAYS use underlying
        TS_WORD
    );

    Array* details = ACT_DETAILS(adaptation);

    Value* block = RESET_CELL(Array_At(details, 0), REB_BLOCK);
    INIT_VAL_ARRAY(block, prelude);
    VAL_INDEX(block) = 0;
    INIT_BINDING(block, underlying); // relative binding

    Move_Value(Array_At(details, 1), adaptee);

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
DECLARE_NATIVE(enclose)
{
    INCLUDE_PARAMS_OF_ENCLOSE;

    Value* inner = ARG(inner);
    Symbol* opt_inner_name;
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
        fail (Error_Invalid(inner));
    Move_Value(inner, D_OUT); // Frees D_OUT, and GC safe (in ARG slot)

    Value* outer = ARG(outer);
    Symbol* opt_outer_name;
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
        fail (Error_Invalid(outer));
    Move_Value(outer, D_OUT); // Frees D_OUT, and GC safe (in ARG slot)

    // The paramlist needs to be unique to designate this function, but
    // will be identical typesets to the inner.  It's [0] element must
    // identify the function we're creating vs the original, however.
    //
    Array* paramlist = Copy_Array_Shallow_Flags(
        VAL_ACT_PARAMLIST(inner),
        SPECIFIED,
        SERIES_MASK_ACTION | NODE_FLAG_MANAGED
    );
    Value* rootparam = KNOWN(ARR_HEAD(paramlist));
    rootparam->payload.action.paramlist = paramlist;

    // !!! We don't want to inherit the flags of the original action, such
    // as ACTION_FLAG_NATIVE.  For now just clear out all the type-specific
    // bits and let Make_Action() cache the flags it needs.
    //
    CUSTOM_BYTE(rootparam) = 0;

    // See %sysobj.r for `enclosed-meta:` object template

    Value* example = Get_System(SYS_STANDARD, STD_ENCLOSED_META);

    REBCTX *meta = Copy_Context_Shallow_Managed(VAL_CONTEXT(example));
    Init_Nulled(CTX_VAR(meta, STD_ENCLOSED_META_DESCRIPTION)); // default
    Move_Value(CTX_VAR(meta, STD_ENCLOSED_META_INNER), inner);
    if (opt_inner_name == nullptr)
        Init_Nulled(CTX_VAR(meta, STD_ENCLOSED_META_INNER_NAME));
    else
        Init_Word(
            CTX_VAR(meta, STD_ENCLOSED_META_INNER_NAME),
            opt_inner_name
        );
    Move_Value(CTX_VAR(meta, STD_ENCLOSED_META_OUTER), outer);
    if (opt_outer_name == nullptr)
        Init_Nulled(CTX_VAR(meta, STD_ENCLOSED_META_OUTER_NAME));
    else
        Init_Word(
            CTX_VAR(meta, STD_ENCLOSED_META_OUTER_NAME),
            opt_outer_name
        );

    MISC(paramlist).meta = meta;

    REBACT *enclosure = Make_Action(
        paramlist,
        &Encloser_Dispatcher,
        ACT_UNDERLYING(VAL_ACTION(inner)), // same underlying as inner
        ACT_EXEMPLAR(VAL_ACTION(inner)), // same exemplar as inner
        2 // details array capacity => [inner, outer]
    );

    Array* details = ACT_DETAILS(enclosure);
    Move_Value(Array_At(details, 0), inner);
    Move_Value(Array_At(details, 1), outer);

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
DECLARE_NATIVE(hijack)
//
// Hijacking an action does not change its interface--and cannot.  While
// it may seem tempting to use low-level tricks to keep the same paramlist
// but add or remove parameters, parameter lists can be referenced many
// places in the system (frames, specializations, adaptations) and can't
// be corrupted...or the places that rely on their properties (number and
// types of parameters) would get out of sync.
{
    INCLUDE_PARAMS_OF_HIJACK;

    Symbol* opt_victim_name;
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

    Symbol* opt_hijacker_name;
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

    Array* victim_paramlist = ACT_PARAMLIST(victim);
    Array* victim_details = ACT_DETAILS(victim);
    Array* hijacker_paramlist = ACT_PARAMLIST(hijacker);
    Array* hijacker_details = ACT_DETAILS(hijacker);

    if (ACT_UNDERLYING(hijacker) == ACT_UNDERLYING(victim)) {
        //
        // Should the underliers of the hijacker and victim match, that means
        // any ADAPT or CHAIN or SPECIALIZE of the victim can work equally
        // well if we just use the hijacker's dispatcher directly.  This is a
        // reasonably common case, and especially common when putting the
        // originally hijacked function back.

        LINK(victim_paramlist).underlying = LINK(hijacker_paramlist).underlying;
        if (LINK(hijacker_details).specialty == hijacker_paramlist)
            LINK(victim_details).specialty = victim_paramlist;
        else
            LINK(victim_details).specialty = LINK(hijacker_details).specialty;

        MISC(victim_details).dispatcher = MISC(hijacker_details).dispatcher;

        // All function info arrays should live in cells with the same
        // underlying formatting.  Blit_Cell ensures that's the case.
        //
        // !!! It may be worth it to optimize some dispatchers to depend on
        // ARR_SINGLE(info) being correct.  That would mean hijack reversals
        // would need to restore the *exact* capacity.  Review.

        REBLEN details_len = ARR_LEN(hijacker_details);
        if (SER_REST(SER(victim_details)) < details_len + 1)
            EXPAND_SERIES_TAIL(
                SER(victim_details),
                details_len + 1 - SER_REST(SER(victim_details))
            );

        Cell* src = ARR_HEAD(hijacker_details);
        Cell* dest = ARR_HEAD(victim_details);
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
DECLARE_NATIVE(variadic_q)
{
    INCLUDE_PARAMS_OF_VARIADIC_Q;

    Value* param = VAL_ACT_PARAMS_HEAD(ARG(action));
    for (; NOT_END(param); ++param) {
        if (Is_Param_Variadic(param))
            return Init_True(D_OUT);
    }

    return Init_False(D_OUT);
}


//
//  tighten: native [
//
//  {Returns alias of an ACTION! whose "normal" args are gathered "tightly"}
//
//      return: [action!]
//      action [action!]
//  ]
//
DECLARE_NATIVE(tighten)
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

    REBACT *original = VAL_ACTION(ARG(action));

    // Copy the paramlist, which serves as the function's unique identity,
    // and set the tight flag on all the parameters.

    Array* paramlist = Copy_Array_Shallow_Flags(
        ACT_PARAMLIST(original),
        SPECIFIED, // no relative values in parameter lists
        SERIES_MASK_ACTION | NODE_FLAG_MANAGED // flags not auto-copied
    );

    Cell* param = Array_At(paramlist, 1); // first parameter (0 is ACTION!)
    for (; NOT_END(param); ++param) {
        enum Reb_Param_Class pclass = VAL_PARAM_CLASS(param);
        if (pclass == PARAM_CLASS_NORMAL)
            INIT_VAL_PARAM_CLASS(param, PARAM_CLASS_TIGHT);
    }

    Cell* rootparam = ARR_HEAD(paramlist);
    Clear_Action_Cached_Flags(rootparam);
    rootparam->payload.action.paramlist = paramlist;
    INIT_BINDING(rootparam, UNBOUND);

    // !!! This does not make a unique copy of the meta information context.
    // Hence updates to the title/parameter-descriptions/etc. of the tightened
    // function will affect the original, and vice-versa.
    //
    MISC(paramlist).meta = ACT_META(original);

    // Our function has a new identity, but we don't want to be using that
    // identity for the pushed frame.  If we did that, then if the underlying
    // function were interpreted, we would have to make a copy of its body
    // and rebind it to the new paramlist.  HOWEVER we want the new tightened
    // parameter specification to take effect--and that's not reflected in
    // the original paramlist, e.g. the one to which that block is bound.
    //
    // This is why we pass the original in as the "underlying" function,
    // which is used when the frame is being pushed.
    //
    REBLEN details_len = ARR_LEN(ACT_DETAILS(original));
    REBACT *tightened = Make_Action(
        paramlist,
        ACT_DISPATCHER(original),
        ACT_UNDERLYING(original), // !!! ^-- notes above may be outdated
        ACT_EXEMPLAR(original), // don't add to the original's specialization
        details_len // details array capacity
    );

    // We're reusing the original dispatcher, so we also reuse the original
    // function body.  Note that Blit_Cell ensures that the cell formatting
    // on the source and target are the same, and it preserves relative
    // value information (rarely what you meant, but it's meant here).
    //
    Cell* src = ARR_HEAD(ACT_DETAILS(original));
    Cell* dest = ARR_HEAD(ACT_DETAILS(tightened));
    for (; NOT_END(src); ++src, ++dest)
        Blit_Cell(dest, src);
    TERM_ARRAY_LEN(ACT_DETAILS(tightened), details_len);

    return Init_Action_Maybe_Bound(
        D_OUT,
        tightened, // REBACT* archetype doesn't contain a binding
        VAL_BINDING(ARG(action)) // e.g. keep binding for `tighten 'return`
    );
}



REB_R N_Shot_Dispatcher(REBFRM *f)
{
    Array* details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == 1);

    Cell* n = ARR_HEAD(details);
    if (VAL_INT64(n) == 0)
        return nullptr; // always return null once 0 is reached
    --VAL_INT64(n);

    Value* code = FRM_ARG(f, 1);
    if (Do_Branch_Throws(f->out, code))
        return R_THROWN;

    return Trashify_If_Nulled(f->out);
}


REB_R N_Upshot_Dispatcher(REBFRM *f)
{
    Array* details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == 1);

    Cell* n = ARR_HEAD(details);
    if (VAL_INT64(n) < 0) {
        ++VAL_INT64(ARR_HEAD(details));
        return nullptr; // return null until 0 is reached
    }

    Value* code = FRM_ARG(f, 1);
    if (Do_Branch_Throws(f->out, code))
        return R_THROWN;

    return Trashify_If_Nulled(f->out);
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
DECLARE_NATIVE(n_shot)
{
    INCLUDE_PARAMS_OF_N_SHOT;

    REBI64 n = VAL_INT64(ARG(n));

    Array* paramlist = Make_Arr_Core(
        2,
        SERIES_MASK_ACTION | NODE_FLAG_MANAGED
    );

    Value* archetype = RESET_CELL(Alloc_Tail_Array(paramlist), REB_ACTION);
    archetype->payload.action.paramlist = paramlist;
    INIT_BINDING(archetype, UNBOUND);

    // !!! Should anything DO would accept be legal, as DOES would run?
    //
    Value* param = Init_Typeset(
        Alloc_Tail_Array(paramlist),
        FLAGIT_KIND(REB_BLOCK) | FLAGIT_KIND(REB_ACTION),
        Canon(SYM_VALUE) // SYM_CODE ?
    );
    INIT_VAL_PARAM_CLASS(param, PARAM_CLASS_NORMAL);
    assert(not Is_Param_Endable(param));

    MISC(paramlist).meta = nullptr;  // !!! auto-generate info for HELP?

    REBACT *n_shot = Make_Action(
        paramlist,
        n >= 0 ? &N_Shot_Dispatcher : &N_Upshot_Dispatcher,
        nullptr, // no underlying action (use paramlist)
        nullptr, // no specialization exemplar (or inherited exemplar)
        1 // details array capacity
    );
    Init_Integer(ARR_HEAD(ACT_DETAILS(n_shot)), n);

    return Init_Action_Unbound(D_OUT, n_shot);
}
