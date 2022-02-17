//
//  File: %c-hijack.c
//  Summary: "Method for intercepting one function invocation with another"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2021 Ren-C Open Source Contributors
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
// HIJACK is a tricky-but-useful mechanism for replacing calls to one function
// with another function, based on identity.  This is distinct from overwriting
// a variable, because all references are affected:
//
//     >> victim: func [] [print "This gets hijacked."]
//
//     >> reference: :victim  ; both words point to the same function identity
//
//     >> victim
//     This gets hijacked.
//
//     >> reference
//     This gets hijacked.
//
//     >> hijack :victim (func [] [print "HIJACK!"])
//
//     >> victim
//     HIJACK!
//
//     >> reference
//     HIJACK!
//
// Though it originated as a somewhat hacky experiment, it was solidified as
// it became increasingly leaned on for important demos.  HIJACK is now
// considered to be safe for mezzanine usages (where appropriate).
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// * Specializations, adaptations, enclosures, or other compositional tools
//   hold "references" to functions internally.  These references are also
//   affected by the hijacking, which means it's easy to get infinite loops:
//
//       >> hijack :load (adapt :load [print "LOADING!"])
//
//       >> load "<for example>"
//       LOADING!
//       LOADING!
//       LOADING!  ; ... infinite loop
//
//   The problem there is that the adaptation performs its printout and then
//   falls through to the original LOAD, which is now the hijacked version
//   that has the adaptation.  Working around this problem means remembering
//   to ADAPT a COPY:
//
//       >> hijack :load (adapt copy :load [print "LOADING!"])
//
//       >> load "<for example>"
//       LOADING!
//       == [<for example>]
//
// * Hijacking is only efficient when the frames of the functions match--e.g.
//   when the "hijacker" is an ADAPT or ENCLOSE of a copy of the "victim".  But
//   if the frames don't line up, there's an attempt to remap the parameters in
//   the frame based on their name.  This should be avoided if possible.
//

#include "sys-core.h"


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
bool Redo_Action_Maybe_Stale_Throws(REBVAL *out, REBFRM *f, REBACT *run)
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
    Copy_Cell(ARR_SINGLE(group), ACT_ARCHETYPE(run));  // Review: binding?
    Quotify(ARR_SINGLE(group), 1);  // suppress evaluation until pathing
    Init_Group(DS_PUSH(), group);

    assert(not Is_Action_Frame_Fulfilling(f));  // okay to reuse
    f->key = ACT_KEYS(&f->key_tail, FRM_PHASE(f));
    f->arg = FRM_ARGS_HEAD(f);
    f->param = ACT_PARAMS_HEAD(FRM_PHASE(f));

    for (; f->key != f->key_tail; ++f->key, ++f->arg, ++f->param) {
        if (Is_Specialized(f->param))  // specialized or local
            continue;

        if (VAL_PARAM_CLASS(f->param) == PARAM_CLASS_RETURN)
            continue;  // !!! hack, has PARAM_FLAG_REFINEMENT, don't stack it

        if (GET_PARAM_FLAG(f->param, SKIPPABLE) and IS_NULLED(f->arg))
            continue;  // don't throw in skippable args that are nulled out

        if (GET_PARAM_FLAG(f->param, REFINEMENT)) {
            if (IS_NULLED(f->arg))  // don't add to PATH!
                continue;

            Init_Word(DS_PUSH(), KEY_SYMBOL(f->key));

            if (Is_Typeset_Empty(f->param)) {
                assert(Is_Blackhole(f->arg));  // used but argless refinement
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
        Quotify(Copy_Cell(code, f->arg), 1);
        ++code;
    }

    SET_SERIES_LEN(code_arr, code - ARR_HEAD(code_arr));
    Manage_Series(code_arr);

    DECLARE_LOCAL (first);
    if (DSP == dsp_orig + 1) {  // no refinements, just use ACTION!
        DS_DROP_TO(dsp_orig);
        Copy_Cell(first, ACT_ARCHETYPE(run));
    }
    else {
        REBARR *a = Freeze_Array_Shallow(Pop_Stack_Values(dsp_orig));
        Force_Series_Managed(a);
        REBVAL *p = Try_Init_Path_Arraylike(first, a);
        assert(p);
        UNUSED(p);
    }

    bool threw = Do_At_Mutable_Maybe_Stale_Throws(
        out,  // invisibles allow for out to not be Init_None()'d
        first,  // path not in array, will be "virtual" first element
        code_arr,
        0,  // index
        SPECIFIED  // reusing existing REBVAL arguments, no relative values
    );
    return threw;
}


//
//  Hijacker_Dispatcher: C
//
// A hijacker takes over another function's identity, replacing it with its
// own implementation.  It leaves the details array intact (in case it is
// being used by some other COPY of the action), but slips its own archetype
// into the [0] slot of that array.
//
// Sometimes the hijacking function has the same underlying function
// as the victim, in which case there's no need to insert a new dispatcher.
// The hijacker just takes over the identity.  But otherwise it cannot, and
// it's not legitimate to reshape the exemplar of the victim (as something like
// an ADAPT or SPECIALIZE or a MAKE FRAME! might depend on the existing
// paramlist shape of the identity.)  Those cases need this "shim" dispatcher.
//
REB_R Hijacker_Dispatcher(REBFRM *f)
{
    // The FRM_PHASE() here is the identity that the hijacker has taken over;
    // but the actual hijacker is in the archetype.

    REBACT *phase = FRM_PHASE(f);
    REBACT *hijacker = VAL_ACTION(ACT_ARCHETYPE(phase));

    // If the hijacked function was called directly -or- by an adaptation or
    // specalization etc. which was made *after* the hijack, the frame should
    // be compatible.  Check by seeing if the keylists are derived.
    //
    REBSER *exemplar_keylist = CTX_KEYLIST(ACT_EXEMPLAR(hijacker));
    REBSER *keylist = CTX_KEYLIST(CTX(f->varlist));
    while (true) {
        if (keylist == exemplar_keylist)
            return ACT_DISPATCHER(hijacker)(f);
        if (keylist == LINK(Ancestor, keylist))  // terminates with self ref.
            break;
        keylist = LINK(Ancestor, keylist);
    }

    // Otherwise, we assume the frame was built for the function prior to
    // the hijacking...and has to be remapped.
    //
    if (Redo_Action_Maybe_Stale_Throws(f->out, f, hijacker))
        return R_THROWN;

    return f->out;  // Note: may have OUT_NOTE_STALE, hence invisible
}


//
//  hijack: native [
//
//  {Cause all existing references to an ACTION! to invoke another ACTION!}
//
//      return: "The hijacked action value, null if self-hijack (no-op)"
//          [<opt> action!]
//      victim "Action whose references are to be affected"
//          [action!]
//      hijacker "The action to run in its place"
//          [action!]
//  ]
//
REBNATIVE(hijack)
{
    INCLUDE_PARAMS_OF_HIJACK;

    REBACT *victim = VAL_ACTION(ARG(victim));
    REBACT *hijacker = VAL_ACTION(ARG(hijacker));

    if (victim == hijacker)
        return nullptr;  // permitting no-op hijack has some practical uses

    REBARR *victim_identity = ACT_IDENTITY(victim);
    REBARR *hijacker_identity = ACT_IDENTITY(hijacker);

    if (Action_Is_Base_Of(victim, hijacker)) {
        //
        // Should the paramlists of the hijacker and victim match, that means
        // any ADAPT or CHAIN or SPECIALIZE of the victim can work equally
        // well if we just use the hijacker's dispatcher directly.  This is a
        // reasonably common case, and especially common when putting a copy
        // of the originally hijacked function back.

        mutable_LINK_DISPATCHER(victim_identity)
            = cast(CFUNC*, LINK_DISPATCHER(hijacker_identity));
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
        mutable_LINK_DISPATCHER(victim_identity)
            = cast(CFUNC*, &Hijacker_Dispatcher);
    }

    // The hijacker is no longer allowed to corrupt details arrays.
    // It may only move the archetype into the [0] slot of the identity.

    Copy_Cell(ACT_ARCHETYPE(victim), ACT_ARCHETYPE(hijacker));

    // !!! What should be done about MISC(victim_paramlist).meta?  Leave it
    // alone?  Add a note about the hijacking?  Also: how should binding and
    // hijacking interact?

    // We do not return a copy of the original function that can be used to
    // restore the behavior.  Because you can make such a copy yourself if
    // you intend to put the behavior back:
    //
    //     foo-saved: copy :foo
    //     hijack :foo :bar
    //     ...
    //     hijack :foo :foo-saved
    //
    // Making such a copy in this routine would be wasteful if it wasn't used.
    //
    return Init_Action(
        D_OUT,
        victim,
        VAL_ACTION_LABEL(ARG(victim)),
        VAL_ACTION_BINDING(ARG(hijacker))
    );
}
