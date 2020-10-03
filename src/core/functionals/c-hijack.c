//
//  File: %c-hijack.c
//  Summary: "Method for intercepting one function invocation with another"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2020 Ren-C Open Source Contributors
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the GNU Lesser General Public License (LGPL), Version 3.0.
// You may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.en.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// HIJACK is a speculative and somewhat risky mechanism for replacing calls
// to one function's identity--with another function.  This is most sensible
// (and most efficient) when the frames of the functions match--e.g. when the
// "hijacker" is an ADAPT or ENCLOSE of a copy of the "victim".  But there
// is an attempt to support the case when the functions are independent.
//
//     >> foo: func [x] [x + 1]
//     >> another-foo: :foo
//
//     >> old-foo: copy :foo
//
//     >> foo 10
//     == 11
//
//     >> another-foo
//     == 11
//
//     >> old-foo 10
//     == 11
//
//     >> hijack 'foo func [x] [(old-foo x) + 20]
//
//     >> foo 10
//     == 31  ; HIJACK'd!
//
//     >> another-foo 10
//     == 31  ; variable holds same ACTION! identity as foo, HIJACK effects
//
//     >> old-foo 10
//     == 11  ; was a COPY, so different identity--HIJACK does not effect
//
// !!! This feature is not well tested, and is difficult for users to apply
// correctly.  However, some important demos--like the Web REPL--lean on the
// feature to get their work done.  It should be revisited and vetted.
//

#include "sys-core.h"

enum {
    IDX_HIJACKER_HIJACKER = 0,  // Relativized block to run before Adaptee
    IDX_HIJACKER_MAX
};


//
//  Redo_Action_Throws: C
//
// This code takes a running call frame that has been built for one action
// and then tries to map its parameters to invoke another action.  The new
// action may have different orders and names of parameters.
//
// R3-Alpha had a rather brittle implementation, that had no error checking
// and repetition of logic in Eval_Core.  Ren-C more simply builds a PATH! of
// the target function and refinements.
//
// !!! This could be done more efficiently now by pushing the refinements to
// the stack and using an APPLY-like technique.
//
// !!! This still isn't perfect and needs reworking, as it won't stand up in
// the face of targets that are "adversarial" to the archetype:
//
//     foo: func [a /b c] [...]  =>  bar: func [/b d e] [...]
//                    foo/b 1 2  =>  bar/b 1 2
//
bool Redo_Action_Throws(REBVAL *out, REBFRM *f, REBACT *run)
{
    REBARR *code_arr = Make_Array(FRM_NUM_ARGS(f)); // max, e.g. no refines
    RELVAL *code = ARR_HEAD(code_arr);

    // !!! For the moment, if refinements are needed we generate a PATH! with
    // the ACTION! at the head, and have the evaluator rediscover the stack
    // of refinements.  This would be better if we left them on the stack
    // and called into the evaluator with Begin_Action() already in progress
    // on a new frame.  Improve when time permits.
    //
    REBDSP dsp_orig = DSP; // we push refinements as we find them

    // !!! Is_Valid_Sequence_Element() requires action to be in a GROUP!
    //
    REBARR *group = Alloc_Singular(NODE_FLAG_MANAGED);
    Move_Value(ARR_SINGLE(group), ACT_ARCHETYPE(run));  // Review: binding?
    Quotify(ARR_SINGLE(group), 1);  // suppress evaluation until pathing
    Init_Group(DS_PUSH(), group);

    assert(IS_END(f->param)); // okay to reuse, if it gets put back...
    f->param = ACT_PARAMS_HEAD(FRM_PHASE(f));
    f->arg = FRM_ARGS_HEAD(f);
    f->special = ACT_SPECIALTY_HEAD(FRM_PHASE(f));

    for (; NOT_END(f->param); ++f->param, ++f->arg, ++f->special) {
        if (Is_Param_Hidden(f->param)) {  // specialized-out parameter
            assert(GET_CELL_FLAG(f->special, ARG_MARKED_CHECKED));
            continue;
        }

        Reb_Param_Class pclass = VAL_PARAM_CLASS(f->param);

        if (pclass == REB_P_LOCAL)
             continue;  // don't add a callsite expression for it (can't)!

        if (TYPE_CHECK(f->param, REB_TS_SKIPPABLE) and IS_NULLED(f->arg))
            continue;  // don't throw in skippable args that are nulled out

        if (TYPE_CHECK(f->param, REB_TS_REFINEMENT)) {
            if (IS_NULLED(f->arg))  // don't add to PATH!
                continue;

            Init_Word(DS_PUSH(), VAL_PARAM_SPELLING(f->param));

            if (Is_Typeset_Invisible(f->param)) {
                assert(IS_REFINEMENT(f->arg));  // used but argless refinement
                continue;
            }
        }

        // The arguments were already evaluated to put them in the frame, do
        // not evaluate them again.
        //
        // !!! This tampers with the VALUE_FLAG_UNEVALUATED bit, which is
        // another good reason this should probably be done another way.  It
        // also loses information about the const bit.
        //
        Quotify(Move_Value(code, f->arg), 1);
        ++code;
    }

    TERM_ARRAY_LEN(code_arr, code - ARR_HEAD(code_arr));
    Manage_Array(code_arr);

    DECLARE_LOCAL (first);
    if (DSP == dsp_orig + 1) {  // no refinements, just use ACTION!
        DS_DROP_TO(dsp_orig);
        Move_Value(first, ACT_ARCHETYPE(run));
    }
    else {
        REBARR *a = Freeze_Array_Shallow(Pop_Stack_Values(dsp_orig));
        Force_Array_Managed(a);
        REBVAL *p = Try_Init_Path_Arraylike(first, a);
        assert(p);
        UNUSED(p);
    }

    bool threw = Do_At_Mutable_Maybe_Stale_Throws(
        out,  // invisibles allow for out to not be Init_Void()'d
        first,  // path not in array, will be "virtual" first element
        code_arr,
        0,  // index
        SPECIFIED  // reusing existing REBVAL arguments, no relative values
    );
    CLEAR_CELL_FLAG(out, OUT_MARKED_STALE);
    return threw;
}


//
//  Hijacker_Dispatcher: C
//
// A hijacker takes over another function's identity, replacing it with its
// own implementation, injecting directly into the paramlist and body_holder
// nodes held onto by all the victim's references.
//
// Sometimes the hijacking function has the same underlying function
// as the victim, in which case there's no need to insert a new dispatcher.
// The hijacker just takes over the identity.  But otherwise it cannot,
// and a "shim" is needed...since something like an ADAPT or SPECIALIZE
// or a MAKE FRAME! might depend on the existing paramlist shape.
//
REB_R Hijacker_Dispatcher(REBFRM *f)
{
    REBACT *phase = FRM_PHASE(f);
    REBARR *details = ACT_DETAILS(phase);
    RELVAL *hijacker = ARR_HEAD(details);

    // We need to build a new frame compatible with the hijacker, and
    // transform the parameters we've gathered to be compatible with it.
    //
    if (Redo_Action_Throws(f->out, f, VAL_ACTION(hijacker)))
        return R_THROWN;

    if (GET_ACTION_FLAG(phase, IS_INVISIBLE))
        return R_INVISIBLE;

    return f->out;
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

    const bool push_refinements = false;
    if (Get_If_Word_Or_Path_Throws(
        D_OUT,
        ARG(victim),
        SPECIFIED,
        push_refinements
    )){
        return R_THROWN;
    }

    if (not IS_ACTION(D_OUT))
        fail ("Victim of HIJACK must be an ACTION!");
    Move_Value(ARG(victim), D_OUT);  // Frees up D_OUT
    REBACT *victim = VAL_ACTION(ARG(victim));  // GC safe (in ARG slot)

    if (Get_If_Word_Or_Path_Throws(
        D_OUT,
        ARG(hijacker),
        SPECIFIED,
        push_refinements
    )){
        return R_THROWN;
    }

    if (not IS_ACTION(D_OUT))
        fail ("Hijacker in HIJACK must be an ACTION!");
    Move_Value(ARG(hijacker), D_OUT);  // Frees up D_OUT
    REBACT *hijacker = VAL_ACTION(ARG(hijacker));  // GC safe (in ARG slot)

    if (victim == hijacker)
        return nullptr;  // permitting no-op hijack has some practical uses

    REBARR *victim_paramlist = ACT_PARAMLIST(victim);
    REBARR *victim_details = ACT_DETAILS(victim);
    REBARR *hijacker_paramlist = ACT_PARAMLIST(hijacker);
    REBARR *hijacker_details = ACT_DETAILS(hijacker);

    if (
        ACT_UNDERLYING(hijacker) == ACT_UNDERLYING(victim)
        and (ACT_NUM_PARAMS(hijacker) == ACT_NUM_PARAMS(victim))
    ){
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

        REBLEN details_len = ARR_LEN(hijacker_details);
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
        Move_Value(
            ARR_AT(victim_details, IDX_HIJACKER_HIJACKER),
            ARG(hijacker)
        );
        TERM_ARRAY_LEN(victim_details, 1);
    }

    // !!! What should be done about MISC(victim_paramlist).meta?  Leave it
    // alone?  Add a note about the hijacking?  Also: how should binding and
    // hijacking interact?

    return Init_Action_Maybe_Bound(D_OUT, victim, VAL_BINDING(ARG(hijacker)));
}
