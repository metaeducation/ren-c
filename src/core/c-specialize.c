//
//  File: %c-specialize.c
//  Summary: "function related datatypes"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015-2018 Rebol Open Source Contributors
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
// A specialization is an ACTION! which has some of its parameters fixed.
// e.g. `ap10: specialize 'append [value: 5 + 5]` makes ap10 have all the same
// refinements available as APPEND, but otherwise just takes one series arg,
// as it will always be appending 10.
//
// The method used is to store a FRAME! in the specialization's ACT_BODY.
// It contains non-null values for any arguments that have been specialized.
// Eval_Core_Throws() heeds these when walking parameters (see `f->special`),
// and processes slots with nulls in them normally.
//
// Code is shared between the SPECIALIZE native and specialization of a
// GET-PATH! via refinements, such as `adp: :append/dup/part`.  However,
// specifying a refinement without all its arguments is made complicated
// because ordering matters:
//
//     foo: func [/ref1 arg1 /ref2 arg2 /ref3 arg3] [...]
//
//     foo23: :foo/ref2/ref3
//     foo32: :foo/ref3/ref2
//
//     foo23 A B ;-- should give A to arg2 and B to arg3
//     foo32 A B ;-- should give B to arg2 and A to arg3
//
// Merely filling in the slots for the refinements specified with TRUE will
// not provide enough information for a call to be able to tell the difference
// between the intents.  Also, a call to `foo23/ref1 A B C` does not want to
// make arg1 A, because it should act like `foo/ref2/ref3/ref1 A B C`.
//
// The current trick for solving this efficiently involves exploiting the
// fact that refinements in exemplar frames are nominally only unspecialized
// (null), in use (LOGIC! true) or disabled (LOGIC! false).  So a REFINEMENT!
// is put in refinement slots that aren't fully specialized, to give a partial
// that should be pushed to the top of the list of refinements in use.
//
// Mechanically it's "simple", but may look a little counterintuitive.  These
// words are appearing in refinement slots that they don't have any real
// correspondence to.  It's just that they want to be able to pre-empt those
// refinements from fulfillment, while pushing to the in-use-refinements stack
// in reverse order given in the specialization.
//
// More concretely, the exemplar frame slots for `foo23: :foo/ref2/ref3` are:
//
// * REF1's slot would contain the REFINEMENT! ref3.  As Eval_Core_Throws()
//   traverses arguments it pushes ref3 as the current first-in-line to take
//   arguments at the callsite.  Yet REF1 has not been "specialized out", so
//   a call like `foo23/ref1` is legal...it's just that pushing ref3 from the
//   ref1 slot means ref1 defers gathering arguments at the callsite.
//
// * REF2's slot would contain the REFINEMENT! ref2.  This will push ref2 to
//   now be first in line in fulfillment.
//
// * REF3's slot would hold a null, having the typical appearance of not
//   being specialized.
//

#include "sys-core.h"


//
//  Make_Context_For_Action_Int_Partials: C
//
// This creates a FRAME! context with "Nulled" in all the unspecialized slots
// that are available to be filled.  For partial refinement specializations
// in the action, it will push the refinement to the stack and fill the arg
// slot in the new context with an INTEGER! indicating the data stack
// position of the partial.  In this way it retains the ordering information
// implicit in the refinements of an action's existing specialization.
//
// It is able to take in more specialized refinements on the stack.  These
// will be ordered *after* partial specializations in the function already.
// The caller passes in the stack pointer of the lowest priority refinement,
// which goes up to DSP for the highest of those added specializations.
//
// Since this is walking the parameters to make the frame already--and since
// we don't want to bind to anything specialized out (including the ad-hoc
// refinements added on the stack) we go ahead and collect bindings from the
// frame if needed.
//
// Note: For added refinements, as with any other parameter specialized out,
// the bindings are not added at all, vs. some kind of error...
//
//     specialize 'append/dup [dup: false] ; Note DUP: isn't frame /DUP
//
REBCTX *Make_Context_For_Action_Int_Partials(
    const REBVAL *action, // need ->binding, so can't just be a REBACT*
    REBDSP lowest_ordered_dsp, // caller can add refinement specializations
    struct Reb_Binder *opt_binder,
    REBFLGS prep // cell formatting mask bits, result managed if non-stack
){
    REBDSP highest_ordered_dsp = DSP;

    REBACT *act = VAL_ACTION(action);

    REBCNT num_slots = ACT_NUM_PARAMS(act) + 1;
    REBARR *varlist = Make_Arr_Core(
        num_slots, // includes +1 for the CTX_ARCHETYPE() at [0]
        SERIES_MASK_CONTEXT
    );

    REBVAL *rootvar = RESET_CELL(ARR_HEAD(varlist), REB_FRAME);
    rootvar->payload.any_context.varlist = varlist;
    rootvar->payload.any_context.phase = VAL_ACTION(action);
    INIT_BINDING(rootvar, VAL_BINDING(action));

    // Copy values from any prior specializations, transforming REFINEMENT!
    // used for partial specializations into INTEGER! or null, depending
    // on whether that slot was actually specialized out.

    const REBVAL *param = ACT_PARAMS_HEAD(act);
    REBVAL *arg = rootvar + 1;
    const REBVAL *special = ACT_SPECIALTY_HEAD(act); // of exemplar/paramlist

    REBCNT index = 1; // used to bind REFINEMENT! values to parameter slots

    REBCTX *exemplar = ACT_EXEMPLAR(act); // may be null
    if (exemplar)
        assert(special == CTX_VARS_HEAD(exemplar));
    else
        assert(special == ACT_PARAMS_HEAD(act));

    for (; NOT_END(param); ++param, ++arg, ++special, ++index) {
        arg->header.bits = prep;

        REBSTR *canon = VAL_PARAM_CANON(param);

        assert(special != param or NOT_VAL_FLAG(arg, ARG_MARKED_CHECKED));

    //=//// NON-REFINEMENT SLOT HANDLING //////////////////////////////////=//

        if (VAL_PARAM_CLASS(param) != PARAM_CLASS_REFINEMENT) {
            if (Is_Param_Hidden(param)) {
                assert(GET_VAL_FLAG(special, ARG_MARKED_CHECKED));
                Move_Value(arg, special); // !!! copy the flag?
                SET_VAL_FLAG(arg, ARG_MARKED_CHECKED); // !!! not copied
                goto continue_specialized; // Eval_Core_Throws() checks type
            }
            goto continue_unspecialized;
        }

    //=//// REFINEMENT PARAMETER HANDLING /////////////////////////////////=//

        if (IS_BLANK(special)) { // specialized BLANK! => "disabled"
            Init_Blank(arg);
            SET_VAL_FLAG(arg, ARG_MARKED_CHECKED);
            goto continue_specialized;
        }

        if (IS_REFINEMENT(special)) { // specialized REFINEMENT! => "in use"
            Init_Refinement(arg, VAL_PARAM_SPELLING(param));
            SET_VAL_FLAG(arg, ARG_MARKED_CHECKED);
            goto continue_specialized;
        }

        // Refinement argument slots are tricky--they can be unspecialized,
        // -but- have an ISSUE! in them we need to push to the stack.
        // (they're in *reverse* order of use).  Or they may be specialized
        // and have a NULL in them pushed by an earlier slot.  Refinements
        // in use must be turned into INTEGER! partials, to point to the DSP
        // of their stack order.

        if (IS_ISSUE(special)) {
            REBCNT partial_index = VAL_WORD_INDEX(special);
            DS_PUSH_TRASH;
            Init_Any_Word_Bound( // push an ISSUE! to data stack
                DS_TOP,
                REB_ISSUE,
                VAL_STORED_CANON(special),
                exemplar,
                partial_index
            );

            if (partial_index <= index) {
                //
                // We've already passed the slot we need to mark partial.
                // Go back and fill it in, and consider the stack item
                // to be completed/bound
                //
                REBVAL *passed = rootvar + partial_index;
                assert(passed->header.bits == prep);

                assert(
                    VAL_STORED_CANON(special) ==
                    VAL_PARAM_CANON(
                        CTX_KEYS_HEAD(exemplar) + partial_index - 1
                    )
                );

                Init_Integer(passed, DSP);
                SET_VAL_FLAG(passed, ARG_MARKED_CHECKED); // passed, not arg

                if (partial_index == index)
                    goto continue_specialized; // just filled in *this* slot
            }

            // We know this is partial (and should be set to an INTEGER!)
            // but it may have been pushed to the stack already, or it may
            // be coming along later.  Search only the higher priority
            // pushes since the call began.
            //
            canon = VAL_PARAM_CANON(param);
            REBDSP dsp = DSP;
            for (; dsp != highest_ordered_dsp; --dsp) {
                REBVAL *ordered = DS_AT(dsp);
                assert(IS_WORD_BOUND(ordered));
                if (VAL_WORD_INDEX(ordered) == index) { // prescient push
                    assert(canon == VAL_STORED_CANON(ordered));
                    Init_Integer(arg, dsp);
                    SET_VAL_FLAG(arg, ARG_MARKED_CHECKED);
                    goto continue_specialized;
                }
            }

            assert(arg->header.bits == prep); // skip slot for now
            continue;
        }

        assert(
            special == param
            or IS_NULLED(special)
            or (
                IS_VOID(special)
                and GET_VAL_FLAG(special, ARG_MARKED_CHECKED)
            )
        );

        // If we get here, then the refinement is unspecified in the
        // exemplar (or there is no exemplar and special == param).
        // *but* the passed in refinements may wish to override that in
        // a "virtual" sense...and remove it from binding consideration
        // for a specialization, e.g.
        //
        //     specialize 'append/only [only: false] ; won't disable only
        {
            REBDSP dsp = highest_ordered_dsp;
            for (; dsp != lowest_ordered_dsp; --dsp) {
                REBVAL *ordered = DS_AT(dsp);
                if (VAL_STORED_CANON(ordered) != canon)
                    continue; // just continuing this loop

                assert(not IS_WORD_BOUND(ordered)); // we bind only one
                INIT_BINDING(ordered, varlist);
                ordered->payload.any_word.index = index;

                // Wasn't hidden in the incoming paramlist, but it should be
                // hidden from the user when they are running their code
                // bound into this frame--even before the specialization
                // based on the outcome of that code has been calculated.
                //
                Init_Integer(arg, dsp);
                SET_VAL_FLAG(arg, ARG_MARKED_CHECKED);
                goto continue_specialized;
            }
        }

        goto continue_unspecialized;

      continue_unspecialized:;

        assert(arg->header.bits == prep);
        Init_Nulled(arg);
        if (opt_binder) {
            if (not Is_Param_Unbindable(param))
                Add_Binder_Index(opt_binder, canon, index);
        }
        continue;

      continue_specialized:;

        assert(not IS_NULLED(arg));
        assert(GET_VAL_FLAG(arg, ARG_MARKED_CHECKED));
        continue;
    }

    TERM_ARRAY_LEN(varlist, num_slots);
    MISC(varlist).meta = NULL; // GC sees this, we must initialize

    // !!! Can't currently pass SERIES_FLAG_STACK into Make_Arr_Core(),
    // because TERM_ARRAY_LEN won't let it set stack array lengths.
    //
    if (prep & CELL_FLAG_STACK)
        SET_SER_FLAG(varlist, SERIES_FLAG_STACK);

    INIT_CTX_KEYLIST_SHARED(CTX(varlist), ACT_PARAMLIST(act));
    return CTX(varlist);
}


//
//  Make_Context_For_Action: C
//
// !!! The ultimate concept is that it would be possible for a FRAME! to
// preserve ordering information such that an ACTION! could be made from it.
// Right now the information is the stack ordering numbers of the refinements
// which to make it usable should be relative to the lowest ordered DSP and
// not absolute.
//
REBCTX *Make_Context_For_Action(
    const REBVAL *action, // need ->binding, so can't just be a REBACT*
    REBDSP lowest_ordered_dsp,
    struct Reb_Binder *opt_binder
){
    REBCTX *exemplar = Make_Context_For_Action_Int_Partials(
        action,
        lowest_ordered_dsp,
        opt_binder,
        CELL_MASK_NON_STACK
    );

    MANAGE_ARRAY(CTX_VARLIST(exemplar)); // !!! was needed before, review
    DS_DROP_TO(lowest_ordered_dsp);
    return exemplar;
}


// Each time we transition the refine field we need to check to see if a
// partial became fulfilled, and if so transition it to not being put into
// the partials.  Better to do it with a macro than repeat the code.  :-/
//
#define FINALIZE_REFINE_IF_FULFILLED \
    assert(evoked != refine or evoked->payload.partial.dsp == 0); \
    if (VAL_TYPE_RAW(refine) == REB_X_PARTIAL) { \
        if (not GET_VAL_FLAG(refine, PARTIAL_FLAG_SAW_NULL_ARG)) { \
            if (refine->payload.partial.dsp != 0) \
                Init_Blank(DS_AT(refine->payload.partial.dsp)); /* full! */ \
            else if (refine == evoked) \
                evoked = NULL; /* allow another evoke to be last partial! */ \
        } \
    }


//
//  Specialize_Action_Throws: C
//
// Create a new ACTION! value that uses the same implementation as another,
// but just takes fewer arguments or refinements.  It does this by storing a
// heap-based "exemplar" FRAME! in the specialized action; this stores the
// values to preload in the stack frame cells when it is invoked.
//
// The caller may provide information on the order in which refinements are
// to be specialized, using the data stack.  These refinements should be
// pushed in the *reverse* order of their invocation, so append/dup/part
// has /DUP at DS_TOP, and /PART under it.  List stops at lowest_ordered_dsp.
//
bool Specialize_Action_Throws(
    REBVAL *out,
    REBVAL *specializee,
    REBSTR *opt_specializee_name,
    REBVAL *opt_def, // !!! REVIEW: binding modified directly (not copied)
    REBDSP lowest_ordered_dsp
){
    assert(out != specializee);

    struct Reb_Binder binder;
    if (opt_def)
        INIT_BINDER(&binder);

    REBACT *unspecialized = VAL_ACTION(specializee);

    // This produces a context where partially specialized refinement slots
    // will be INTEGER! pointing into the stack at the partial order
    // position. (This takes into account any we are adding "virtually", from
    // the current DSP down to the lowest_ordered_dsp).
    //
    // Note that REB_X_PARTIAL can't be used in slots yet, because the GC
    // will be able to see this frame (code runs bound into it).
    //
    REBCTX *exemplar = Make_Context_For_Action_Int_Partials(
        specializee,
        lowest_ordered_dsp,
        opt_def ? &binder : nullptr,
        CELL_MASK_NON_STACK
    );
    MANAGE_ARRAY(CTX_VARLIST(exemplar)); // destined to be managed, guarded

    if (opt_def) { // code that fills the frame...fully or partially
        //
        // Bind all the SET-WORD! in the body that match params in the frame
        // into the frame.  This means `value: value` can very likely have
        // `value:` bound for assignments into the frame while `value` refers
        // to whatever value was in the context the specialization is running
        // in, but this is likely the more useful behavior.
        //
        // !!! This binds the actual arg data, not a copy of it--following
        // OBJECT!'s lead.  However, ordinary functions make a copy of the
        // body they are passed before rebinding.  Rethink.

        // See Bind_Values_Core() for explanations of how the binding works.

        Bind_Values_Inner_Loop(
            &binder,
            VAL_ARRAY_AT(opt_def),
            exemplar,
            FLAGIT_KIND(REB_SET_WORD), // types to bind (just set-word!)
            0, // types to "add midstream" to binding as we go (nothing)
            BIND_DEEP
        );

        // !!! Only one binder can be in effect, and we're calling arbitrary
        // code.  Must clean up now vs. in loop we do at the end.  :-(
        //
        RELVAL *key = CTX_KEYS_HEAD(exemplar);
        REBVAL *var = CTX_VARS_HEAD(exemplar);
        for (; NOT_END(key); ++key, ++var) {
            if (Is_Param_Unbindable(key))
                continue; // !!! is this flag still relevant?
            if (Is_Param_Hidden(key)) {
                assert(GET_VAL_FLAG(var, ARG_MARKED_CHECKED));
                continue;
            }
            if (GET_VAL_FLAG(var, ARG_MARKED_CHECKED))
                continue; // may be refinement from stack, now specialized out
            Remove_Binder_Index(&binder, VAL_KEY_CANON(key));
        }
        SHUTDOWN_BINDER(&binder);

        // Run block and ignore result (unless it is thrown)
        //
        PUSH_GC_GUARD(exemplar);
        bool threw = Do_Any_Array_At_Throws(out, opt_def);
        DROP_GC_GUARD(exemplar);

        if (threw) {
            DS_DROP_TO(lowest_ordered_dsp);
            return true;
        }
    }

    REBVAL *rootkey = CTX_ROOTKEY(exemplar);

    // Build up the paramlist for the specialized function on the stack.
    // The same walk used for that is used to link and process REB_X_PARTIAL
    // arguments for whether they become fully specialized or not.

    REBDSP dsp_paramlist = DSP;
    DS_PUSH(ACT_ARCHETYPE(unspecialized));

    REBVAL *param = rootkey + 1;
    REBVAL *arg = CTX_VARS_HEAD(exemplar);
    REBVAL *refine = ORDINARY_ARG; // parallels states in Eval_Core_Throw()
    REBCNT index = 1;

    REBVAL *first_partial = nullptr;
    REBVAL *last_partial = nullptr;

    REBVAL *evoked = nullptr;

    for (; NOT_END(param); ++param, ++arg, ++index) {
        switch (VAL_PARAM_CLASS(param)) {
        case PARAM_CLASS_REFINEMENT: {
            FINALIZE_REFINE_IF_FULFILLED; // see macro
            refine = arg;

            if (
                IS_NULLED(refine)
                or (
                    IS_INTEGER(refine)
                    and GET_VAL_FLAG(refine, ARG_MARKED_CHECKED)
                )
            ){
                // /DUP is implicitly "evoked" to be true in the following
                // case, despite being void, since an argument is supplied:
                //
                //     specialize 'append [count: 10]
                //
                // But refinements with one argument that get evoked might
                // cause partial refinement specialization.  Since known
                // partials are checked to see if they become complete anyway,
                // use the same mechanic for voids.

                REBDSP partial_dsp = IS_NULLED(refine) ? 0 : VAL_INT32(refine);

                if (not first_partial)
                    first_partial = refine;
                else
                    last_partial->extra.next_partial = refine;

                RESET_CELL(refine, REB_X_PARTIAL);
                refine->payload.partial.dsp = partial_dsp;
                refine->payload.partial.index = index;
                TRASH_POINTER_IF_DEBUG(refine->extra.next_partial);

                last_partial = refine;

                if (partial_dsp == 0)
                    goto unspecialized_arg_but_may_evoke;

                // Though Make_Frame_For_Specialization() knew this slot was
                // partial when it ran, user code might have run to fill in
                // all the null arguments.  We need to know the stack position
                // of the ordering, to BLANK! it from the partial stack if so.
                //
                SET_VAL_FLAG(refine, PARTIAL_FLAG_IN_USE);
                goto specialized_arg_no_typecheck;
            }

            assert(
                NOT_VAL_FLAG(refine, ARG_MARKED_CHECKED)
                or (
                    IS_REFINEMENT(refine)
                    and (
                        VAL_WORD_SPELLING(refine)
                        == VAL_PARAM_SPELLING(param)
                    )
                )
            );

            if (IS_TRUTHY(refine))
                Init_Refinement(refine, VAL_PARAM_SPELLING(param));
            else
                Init_Blank(arg);

            SET_VAL_FLAG(arg, ARG_MARKED_CHECKED);
            goto specialized_arg_no_typecheck; }

        case PARAM_CLASS_RETURN:
        case PARAM_CLASS_LOCAL:
            assert(IS_NULLED(arg)); // no bindings, you can't set these
            goto unspecialized_arg;

        default:
            break;
        }

        // It's an argument, either a normal one or a refinement arg.

        if (refine == ORDINARY_ARG) {
            if (IS_NULLED(arg))
                goto unspecialized_arg;

            goto specialized_arg;
        }

        if (VAL_TYPE_RAW(refine) == REB_X_PARTIAL) {
            if (IS_NULLED(arg)) { // we *know* it's not completely fulfilled
                SET_VAL_FLAG(refine, PARTIAL_FLAG_SAW_NULL_ARG);
                goto unspecialized_arg;
            }

            if (refine->payload.partial.dsp != 0) // started true
                goto specialized_arg;

            if (evoked == refine)
                goto specialized_arg; // already evoking this refinement

            // If we started out with a null refinement this arg "evokes" it.
            // (Opposite of void "revocation" at callsites).
            // An "evoked" refinement from the code block has no order,
            // so only one such partial is allowed, unless it turns out to
            // be completely fulfilled.
            //
            if (evoked)
                fail (Error_Ambiguous_Partial_Raw());

            // added at `unspecialized_but_may_evoke` unhidden, now hide it
            TYPE_SET(DS_TOP, REB_TS_HIDDEN);

            evoked = refine; // gets reset to NULL if ends up fulfilled
            SET_VAL_FLAG(refine, PARTIAL_FLAG_IN_USE);
            goto specialized_arg;
        }

        assert(IS_BLANK(refine) or IS_REFINEMENT(refine));

        if (IS_BLANK(refine)) {
            //
            // `specialize 'append [dup: false count: 10]` is not legal.
            //
            if (not IS_NULLED(arg))
                fail (Error_Bad_Refine_Revoke(param, arg));
            goto specialized_arg_no_typecheck;
        }

        if (not IS_NULLED(arg))
            goto specialized_arg;

        // A previously *fully* specialized TRUE should not have null args.
        // But code run for the specialization may have set the refinement
        // to true without setting all its arguments.
        //
        // Unlike with the REB_X_PARTIAL cases, we have no ordering info
        // besides "after all of those", we can only do that *once*.

        if (evoked)
            fail (Error_Ambiguous_Partial_Raw());

        // Link into partials list (some repetition with code above)

        if (not first_partial)
            first_partial = refine;
        else
            last_partial->extra.next_partial = refine;

        RESET_CELL_EXTRA(refine, REB_X_PARTIAL, PARTIAL_FLAG_IN_USE);
        refine->payload.partial.dsp = 0; // no ordered position on stack
        refine->payload.partial.index = index - (arg - refine);
        TRASH_POINTER_IF_DEBUG(refine->extra.next_partial);

        last_partial = refine;

        SET_VAL_FLAG(refine, PARTIAL_FLAG_SAW_NULL_ARG); // this is a null arg
        evoked = refine; // ...we won't ever set this back to NULL later
        goto unspecialized_arg;

    unspecialized_arg_but_may_evoke:;

        assert(refine->payload.partial.dsp == 0);

    unspecialized_arg:;

        assert(NOT_VAL_FLAG(arg, ARG_MARKED_CHECKED));
        DS_PUSH(param); // if evoked, will get DS_DROP'd from the paramlist
        continue;

    specialized_arg:;

        assert(VAL_PARAM_CLASS(param) != PARAM_CLASS_REFINEMENT);

        // !!! If argument was previously specialized, should have been type
        // checked already... don't type check again (?)
        //
        if (Is_Param_Variadic(param))
            fail ("Cannot currently SPECIALIZE variadic arguments.");

        if (not TYPE_CHECK(param, VAL_TYPE(arg)))
            fail (Error_Invalid(arg)); // !!! merge w/Error_Invalid_Arg()

       SET_VAL_FLAG(arg, ARG_MARKED_CHECKED);

    specialized_arg_no_typecheck:;

        // Specialized-out arguments must still be in the parameter list,
        // for enumeration in the evaluator to line up with the frame values
        // of the underlying function.

        DS_PUSH(param);
        TYPE_SET(DS_TOP, REB_TS_HIDDEN);
        continue;
    }

    if (first_partial) {
        FINALIZE_REFINE_IF_FULFILLED; // last chance (no more refinements)
        last_partial->extra.next_partial = nullptr; // not needed until now
    }

    REBARR *paramlist = Pop_Stack_Values_Core(
        dsp_paramlist,
        SERIES_MASK_ACTION
    );
    MANAGE_ARRAY(paramlist);
    RELVAL *rootparam = ARR_HEAD(paramlist);
    rootparam->payload.action.paramlist = paramlist;

    // PARAM_CLASS_REFINEMENT slots which started partially specialized (or
    // unspecialized) in the exemplar now all contain REB_X_PARTIAL, but we
    // must now convert these transitional placeholders to...
    //
    // * VOID! -- Unspecialized, BUT in traversal order before a partial
    //   refinement.  That partial must pre-empt Eval_Core_Throws() fulfilling
    //   a use of this unspecialized refinement from a PATH! at the callsite.
    //
    // * NULL -- Unspecialized with no outranking partials later in traversal.
    //   So Eval_Core_Throws() is free to fulfill a use of this refinement
    //   from a PATH! at the callsite when it first comes across it.
    //
    // * REFINEMENT! (with symbol of the parameter) -- All arguments were
    //   filled in, it's no longer partial.
    //
    // * ISSUE! -- Partially specialized.  Note the symbol of the issue
    //   is probably different from the slot it's in...this is how the
    //   priority order of usage of partial refinements is encoded.

    // We start filling in slots with the lowest priority ordered refinements
    // and move on to the higher ones, so that when those refinements are
    // pushed the end result will be a stack with the highest priority
    // refinements at the top.
    //
    REBVAL *ordered = DS_AT(lowest_ordered_dsp);
    while (ordered != DS_TOP) {
        if (IS_BLANK(ordered + 1)) // blanked when seen no longer partial
            ++ordered;
        else
            break;
    }

    REBVAL *partial = first_partial;
    while (partial) {
        assert(VAL_TYPE_RAW(partial) == REB_X_PARTIAL);
        REBVAL *next_partial = partial->extra.next_partial; // overwritten

        if (NOT_VAL_FLAG(partial, PARTIAL_FLAG_IN_USE)) {
            if (ordered == DS_TOP)
                Init_Nulled(partial); // no more partials coming
            else {
                Init_Void(partial); // still partials to go, signal pre-empt
                SET_VAL_FLAG(partial, ARG_MARKED_CHECKED);
            }
            goto continue_loop;
        }

        if (NOT_VAL_FLAG(partial, PARTIAL_FLAG_SAW_NULL_ARG)) { // filled
            Init_Refinement(
                partial,
                VAL_PARAM_SPELLING(rootkey + partial->payload.partial.index)
            );
            SET_VAL_FLAG(partial, ARG_MARKED_CHECKED);
            goto continue_loop;
        }

        if (evoked) {
            //
            // A non-position-bearing refinement use coming from running the
            // code block will come after all the refinements in the path,
            // making it *first* in the exemplar partial/unspecialized slots.
            //
            REBCNT evoked_index = evoked->payload.partial.index;
            Init_Any_Word_Bound(
                partial,
                REB_ISSUE,
                VAL_PARAM_CANON(rootkey + evoked_index),
                exemplar,
                evoked_index
            );
            SET_VAL_FLAG(partial, ARG_MARKED_CHECKED);

            evoked = nullptr;
            goto continue_loop;
        }

        if (ordered == DS_TOP) { // some partials fully specialized
            Init_Nulled(partial);
            goto continue_loop;
        }

        ++ordered;
        if (IS_WORD_UNBOUND(ordered)) // not in paramlist, or a duplicate
            fail (Error_Bad_Refine_Raw(ordered));

        Init_Any_Word_Bound(
            partial,
            REB_ISSUE,
            VAL_STORED_CANON(ordered),
            exemplar,
            VAL_WORD_INDEX(ordered)
        );
        SET_VAL_FLAG(partial, ARG_MARKED_CHECKED);

        while (ordered != DS_TOP) {
            if (IS_BLANK(ordered + 1))
                ++ordered; // loop invariant, no BLANK! in next stack
            else
                break;
        }

        goto continue_loop;

    continue_loop:;

        partial = next_partial;
    }

    // Everything should have balanced out for a valid specialization
    //
    assert(not evoked);
    if (ordered != DS_TOP)
        fail (Error_Bad_Refine_Raw(ordered)); // specialize 'print/asdf
    DS_DROP_TO(lowest_ordered_dsp);

    // See %sysobj.r for `specialized-meta:` object template

    REBVAL *example = Get_System(SYS_STANDARD, STD_SPECIALIZED_META);

    REBCTX *meta = Copy_Context_Shallow_Managed(VAL_CONTEXT(example));

    Init_Nulled(CTX_VAR(meta, STD_SPECIALIZED_META_DESCRIPTION)); // default
    Move_Value(
        CTX_VAR(meta, STD_SPECIALIZED_META_SPECIALIZEE),
        specializee
    );
    if (not opt_specializee_name)
        Init_Nulled(CTX_VAR(meta, STD_SPECIALIZED_META_SPECIALIZEE_NAME));
    else
        Init_Word(
            CTX_VAR(meta, STD_SPECIALIZED_META_SPECIALIZEE_NAME),
            opt_specializee_name
        );

    MISC(paramlist).meta = meta;

    REBACT *specialized = Make_Action(
        paramlist,
        &Specializer_Dispatcher,
        ACT_UNDERLYING(unspecialized), // same underlying action as this
        exemplar, // also provide a context of specialization values
        1 // details array capacity
    );
    assert(CTX_KEYLIST(exemplar) == ACT_PARAMLIST(unspecialized));

    // The "body" is the FRAME! value of the specialization.  It takes on the
    // binding we want to use (which we can't put in the exemplar archetype,
    // that binding has to be UNBOUND).  It also remembers the original
    // action in the phase, so Specializer_Dispatcher() knows what to call.
    //
    RELVAL *body = ARR_HEAD(ACT_DETAILS(specialized));
    Move_Value(body, CTX_ARCHETYPE(exemplar));
    INIT_BINDING(body, VAL_BINDING(specializee));
    body->payload.any_context.phase = unspecialized;

    Init_Action_Unbound(out, specialized);
    return false; // code block did not throw
}


//
//  Specializer_Dispatcher: C
//
// The evaluator does not do any special "running" of a specialized frame.
// All of the contribution that the specialization had to make was taken care
// of when Eval_Core_Throws() used f->special to fill from the exemplar.  So
// all this does is change the phase and binding to match the function this
// layer was specializing.
//
const REBVAL *Specializer_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));

    REBVAL *exemplar = KNOWN(ARR_HEAD(details));
    assert(IS_FRAME(exemplar));

    FRM_PHASE(f) = exemplar->payload.any_context.phase;
    FRM_BINDING(f) = VAL_BINDING(exemplar);

    return R_REDO_UNCHECKED; // redo uses the updated phase and binding
}


//
//  specialize: native [
//
//  {Create a new action through partial or full specialization of another}
//
//      return: [action!]
//      specializee [action! word! path!]
//          {Function or specifying word (preserves word name for debug info)}
//      def [block!]
//          {Definition for FRAME! fields for args and refinements}
//  ]
//
REBNATIVE(specialize)
{
    INCLUDE_PARAMS_OF_SPECIALIZE;

    REBVAL *specializee = ARG(specializee);

    REBDSP lowest_ordered_dsp = DSP;

    // Any partial refinement specializations are pushed to the stack, and
    // gives ordering information that TRUE assigned in a code block can't.
    //
    REBSTR *opt_name;
    if (Get_If_Word_Or_Path_Throws(
        D_OUT,
        &opt_name,
        specializee,
        SPECIFIED,
        true // push_refines = true (don't generate temp specialization)
    )){
        // e.g. `specialize 'append/(throw 10 'dup) [value: 20]`
        //
        return R_THROWN;
    }

    // Note: Even if there was a PATH! doesn't mean there were refinements
    // used, e.g. `specialize 'lib/append [...]`.

    if (not IS_ACTION(D_OUT))
        fail (Error_Invalid(specializee));
    Move_Value(specializee, D_OUT); // Frees D_OUT, and GC safe (in ARG slot)

    if (Specialize_Action_Throws(
        D_OUT,
        specializee,
        opt_name,
        ARG(def),
        lowest_ordered_dsp
    )){
        // e.g. `specialize 'append/dup [value: throw 10]`
        //
        return R_THROWN;
    }

    return D_OUT;
}


//
//  Block_Dispatcher: C
//
// There are no arguments or locals to worry about in a DOES, nor does it
// heed any definitional RETURN.  This means that in many common cases we
// don't need to do anything special to a BLOCK! passed to DO...no copying
// or otherwise.  Just run it when the function gets called.
//
// Yet `does [...]` isn't *quite* like `specialize 'do [source: [...]]`.  The
// difference is subtle, but important when interacting with bindings to
// fields in derived objects.  That interaction cannot currently resolve such
// bindings without a copy, so it is made on demand.
//
// (Luckily these copies are often not needed, such as when the DOES is not
// used in a method... -AND- it only needs to be made once.)
//
const REBVAL *Block_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    RELVAL *block = ARR_HEAD(details);
    assert(IS_BLOCK(block));

    if (IS_SPECIFIC(block)) {
        if (FRM_BINDING(f) == UNBOUND) {
            if (Do_Any_Array_At_Throws(f->out, KNOWN(block)))
                return R_THROWN;
            return f->out;
        }

        // Until "virtual binding" is implemented, we would lose f->binding's
        // ability to influence any variable lookups in the block if we did
        // not relativize it to this frame.  This is the only current way to
        // "beam down" influence of the binding for cases like:
        //
        // What forces us to copy the block are cases like this:
        //
        //     o1: make object! [a: 10 b: does [if true [a]]]
        //     o2: make o1 [a: 20]
        //     o2/b = 20
        //
        // While o2/b's ACTION! has a ->binding to o2, the only way for the
        // [a] block to get the memo is if it is relative to o2/b.  It won't
        // be relative to o2/b if it didn't have its existing relativism
        // Derelativize()'d out to make it specific, and then re-relativized
        // through a copy on behalf of o2/b.

        REBARR *body_array = Copy_And_Bind_Relative_Deep_Managed(
            KNOWN(block),
            ACT_PARAMLIST(FRM_PHASE(f)),
            TS_WORD
        );

        // Preserve file and line information from the original, if present.
        //
        if (GET_SER_FLAG(VAL_ARRAY(block), ARRAY_FLAG_FILE_LINE)) {
            LINK(body_array).file = LINK(VAL_ARRAY(block)).file;
            MISC(body_array).line = MISC(VAL_ARRAY(block)).line;
            SET_SER_FLAG(body_array, ARRAY_FLAG_FILE_LINE);
        }

        // Need to do a raw initialization of this block RELVAL because it is
        // relative to a function.  (Init_Block assumes all specific values.)
        //
        INIT_VAL_ARRAY(block, body_array);
        VAL_INDEX(block) = 0;
        INIT_BINDING(block, FRM_PHASE(f)); // relative binding

        // Block is now a relativized copy; we won't do this again.
    }

    assert(IS_RELATIVE(block));

    if (Do_At_Throws(
        f->out,
        VAL_ARRAY(block),
        VAL_INDEX(block),
        SPC(f->varlist)
    )){
        return R_THROWN;
    }

    return f->out;
}


//
//  Make_Invocation_Frame_Throws: C
//
// Logic shared currently by DOES and MATCH to build a single executable
// frame from feeding forward a VARARGS! parameter.  A bit like being able to
// call EVALUATE via Eval_Core_Throws() yet introspect the evaluator step.
//
bool Make_Invocation_Frame_Throws(
    REBVAL *out, // in case there is a throw
    REBFRM *f,
    REBVAL **first_arg_ptr, // returned so that MATCH can steal it
    const REBVAL *action,
    REBVAL *varargs,
    REBDSP lowest_ordered_dsp
){
    assert(IS_ACTION(action));
    assert(IS_VARARGS(varargs));

    // !!! The vararg's frame is not really a parent, but try to stay
    // consistent with the naming in subframe code copy/pasted for now...
    //
    REBFRM *parent;
    if (not Is_Frame_Style_Varargs_May_Fail(&parent, varargs))
        fail (
            "Currently MAKE FRAME! on a VARARGS! only works with a varargs"
            " which is tied to an existing, running frame--not one that is"
            " being simulated from a BLOCK! (e.g. MAKE VARARGS! [...])"
        );

    assert(Is_Action_Frame(parent));

    // Slip the REBFRM a dsp_orig which may be lower than the DSP captured by
    // DECLARE_FRAME().  This way, it will see any pushes done during a
    // path resolution as ordered refinements to use.
    //
    f->dsp_orig = lowest_ordered_dsp;

    // === FIRST PART OF CODE FROM DO_SUBFRAME ===
    f->out = out;

    f->source = parent->source;
    f->value = parent->value;
    f->gotten = parent->gotten;
    f->specifier = parent->specifier;
    TRASH_POINTER_IF_DEBUG(parent->gotten);

    // Just do one step of the evaluator, so no DO_FLAG_TO_END.  Specifically,
    // it is desired that any voids encountered be processed as if they are
    // not specialized...and gather at the callsite if necessary.
    //
    f->flags = Endlike_Header(DO_MASK_NONE | DO_FLAG_PROCESS_ACTION);

    Push_Frame_Core(f);
    Reuse_Varlist_If_Available(f);

    // === END FIRST PART OF CODE FROM DO_SUBFRAME ===

    REBSTR *opt_label = nullptr; // !!! for now
    Push_Action(f, VAL_ACTION(action), VAL_BINDING(action));
    Begin_Action(f, opt_label, ORDINARY_ARG);

    // !!! A hack here is needed to slip in a lie to make the dispatcher not
    // run the action, but rather to throw back to us.
    //
    assert(FRM_BINDING(f) == VAL_BINDING(action));
    assert(FRM_PHASE(f) == VAL_ACTION(action));
    FRM_PHASE_OR_DUMMY(f) = PG_Dummy_Action;
    bool threw = (*PG_Eval_Throws)(f);
    FRM_PHASE_OR_DUMMY(f) = VAL_ACTION(action);
    FRM_BINDING(f) = VAL_BINDING(action); // can change during invoke

    // The function did not actually execute, so no SPC(f) was never handed
    // out...the varlist should never have gotten managed.  So this context
    // can theoretically just be put back into the reuse list, or managed
    // and handed out for other purposes by the caller.
    //
    assert(NOT_SER_FLAG(f->varlist, NODE_FLAG_MANAGED));

    parent->value = f->value;
    parent->gotten = f->gotten;
    assert(parent->specifier == f->specifier); // !!! can't change?

    if (f->flags.bits & DO_FLAG_BARRIER_HIT)
        parent->flags.bits |= DO_FLAG_BARRIER_HIT;

    if (threw)
        return true;

    assert(IS_NULLED(f->out)); // guaranteed by dummy, for the skipped action

    // === END SECOND PART OF CODE FROM DO_SUBFRAME ===

    *first_arg_ptr = nullptr;

    REBVAL *refine = nullptr;
    REBVAL *param = CTX_KEYS_HEAD(CTX(f->varlist));
    REBVAL *arg = CTX_VARS_HEAD(CTX(f->varlist));
    for (; NOT_END(param); ++param, ++arg) {
        enum Reb_Param_Class pclass = VAL_PARAM_CLASS(param);
        switch (pclass) {
        case PARAM_CLASS_REFINEMENT:
            refine = param;
            break;

        case PARAM_CLASS_NORMAL:
        case PARAM_CLASS_TIGHT:
        case PARAM_CLASS_HARD_QUOTE:
        case PARAM_CLASS_SOFT_QUOTE:
            if (not refine or VAL_LOGIC(refine)) {
                *first_arg_ptr = arg;
                goto found_first_arg_ptr;
            }
            break;

        case PARAM_CLASS_LOCAL:
        case PARAM_CLASS_RETURN:
            break;

        default:
            panic ("Unknown PARAM_CLASS");
        }
    }

    fail ("ACTION! has no args to MAKE FRAME! from...");

found_first_arg_ptr:

    // DS_DROP_TO(lowest_ordered_dsp);

    return false;
}


//
//  does: native [
//
//  {Specializes DO for a value (or for args of another named function)}
//
//      return: [action!]
//      'specializee [any-value!]
//          {WORD! or PATH! names function to specialize, else arg to DO}
//      :args [any-value! <...>]
//          {arguments which will be consumed to fulfill a named function}
//  ]
//
REBNATIVE(does)
{
    INCLUDE_PARAMS_OF_DOES;

    REBVAL *specializee = ARG(specializee);

    if (IS_BLOCK(specializee)) {
        REBARR *paramlist = Make_Arr_Core(
            1, // archetype only...DOES always makes action with no arguments
            SERIES_MASK_ACTION
        );

        REBVAL *archetype = RESET_CELL(Alloc_Tail_Array(paramlist), REB_ACTION);
        archetype->payload.action.paramlist = paramlist;
        INIT_BINDING(archetype, UNBOUND);
        TERM_ARRAY_LEN(paramlist, 1);

        MISC(paramlist).meta = nullptr; // REDESCRIBE can be used to add help

        //
        // `does [...]` and `does do [...]` are not exactly the same.  The
        // generated ACTION! of the first form uses Block_Dispatcher() and
        // does on-demand relativization, so it's "kind of like" a `func []`
        // in forwarding references to members of derived objects.  Also, it
        // is optimized to not run the block with the DO native...hence a
        // HIJACK of DO won't be triggered by invocations of the first form.
        //
        MANAGE_ARRAY(paramlist);
        REBACT *doer = Make_Action(
            paramlist,
            &Block_Dispatcher, // **SEE COMMENTS**, not quite like plain DO!
            nullptr, // no underlying action (use paramlist)
            nullptr, // no specialization exemplar (or inherited exemplar)
            1 // details array capacity
        );

        // Block_Dispatcher() *may* copy at an indeterminate time, so to keep
        // things invariant we have to lock it.
        //
        RELVAL *body = ARR_HEAD(ACT_DETAILS(doer));
        REBSER *locker = NULL;
        Ensure_Value_Immutable(specializee, locker);
        Move_Value(body, specializee);

        return Init_Action_Unbound(D_OUT, doer);
    }

    REBCTX *exemplar;
    if (
        GET_VAL_FLAG(specializee, VALUE_FLAG_UNEVALUATED)
        and (IS_WORD(specializee) or IS_PATH(specializee))
    ){
        REBSTR *opt_label;
        REBDSP lowest_ordered_dsp = DSP;
        if (Get_If_Word_Or_Path_Throws(
            D_OUT,
            &opt_label,
            specializee,
            SPECIFIED,
            true // push_refinements = true
        )){
            return R_THROWN;
        }

        if (not IS_ACTION(D_OUT))
            fail (Error_Invalid(specializee));

        Move_Value(specializee, D_OUT);

        // We interpret phrasings like `x: does all [...]` to mean something
        // like `x: specialize 'all [block: [...]]`.  While this originated
        // from the Rebmu code golfing language to eliminate a pair of bracket
        // characters from `x: does [all [...]]`, it actually has different
        // semantics...which can be useful in their own right, plus the
        // resulting function will run faster.

        DECLARE_FRAME (f); // REBFRM whose built FRAME! context we will steal

        REBVAL *first_arg;
        if (Make_Invocation_Frame_Throws(
            D_OUT,
            f,
            &first_arg,
            specializee,
            ARG(args),
            lowest_ordered_dsp
        )){
            return R_THROWN;
        }
        assert(NOT_SER_FLAG(f->varlist, NODE_FLAG_MANAGED)); // not invoked yet
        assert(FRM_BINDING(f) == VAL_BINDING(specializee));
        exemplar = Steal_Context_Vars(
            CTX(f->varlist),
            NOD(VAL_ACTION(specializee))
        );
        LINK(exemplar).keysource = NOD(VAL_ACTION(specializee));
        assert(
            ACT_NUM_PARAMS(VAL_ACTION(specializee))
            == CTX_LEN(exemplar)
        );

        SET_SER_FLAG(f->varlist, NODE_FLAG_MANAGED); // is inaccessible
        f->varlist = nullptr; // just let it GC, for now

        // May not be at end or thrown, e.g. (x: does quote y x = 'y)
        //
        Drop_Frame(f);

        // The exemplar may or may not be managed as of yet.  We want it
        // managed, but Push_Action() does not use ordinary series creation to
        // make its nodes, so manual ones don't wind up in the tracking list.
        //
        SET_SER_FLAG(exemplar, NODE_FLAG_MANAGED); // can't use Manage_Series

        UNUSED(first_arg);
        UNUSED(opt_label);
    }
    else {
        // On all other types, we just make it act like a specialized call to
        // DO for that value.  But since we're manually specializing it, we
        // are responsible for type-checking...the evaluator expects any
        // specialization process to do so (otherwise it would have to pay
        // for type checking on each call).
        //
        // !!! The error reports that DOES doesn't accept the type for its
        // specializee argument, vs. that DO doesn't accept it.
        //
        REBVAL *typeset = ACT_PARAM(NAT_ACTION(do), 1);
        REBVAL *param = PAR(specializee);
        if (not TYPE_CHECK(typeset, VAL_TYPE(specializee)))
            fail (Error_Arg_Type(frame_, param, VAL_TYPE(specializee)));

        exemplar = Make_Context_For_Action(
            NAT_VALUE(do),
            DSP, // lower dsp would be if we wanted to add refinements
            nullptr // don't set up a binder; just poke specializee in frame
        );
        assert(GET_SER_FLAG(exemplar, NODE_FLAG_MANAGED));
        Move_Value(CTX_VAR(exemplar, 1), specializee);
        SET_VAL_FLAG(CTX_VAR(exemplar, 1), ARG_MARKED_CHECKED); // checked
        Move_Value(specializee, NAT_VALUE(do));
    }

    REBACT *unspecialized = VAL_ACTION(specializee);

    REBCNT num_slots = ACT_NUM_PARAMS(unspecialized) + 1;
    REBARR *paramlist = Make_Arr_Core(num_slots, SERIES_MASK_ACTION);

    RELVAL *archetype = RESET_CELL(ARR_HEAD(paramlist), REB_ACTION);
    archetype->payload.action.paramlist = paramlist;
    INIT_BINDING(archetype, UNBOUND);
    TERM_ARRAY_LEN(paramlist, 1);

    MISC(paramlist).meta = nullptr; // REDESCRIBE can be used to add help

    REBVAL *param = ACT_PARAMS_HEAD(unspecialized);
    RELVAL *alias = archetype + 1;
    for (; NOT_END(param); ++param, ++alias) {
        Move_Value(alias, param);
        TYPE_SET(alias, REB_TS_HIDDEN);
        TYPE_SET(alias, REB_TS_UNBINDABLE);
    }

    TERM_ARRAY_LEN(paramlist, num_slots);
    MANAGE_ARRAY(paramlist);

    // This code parallels Specialize_Action_Throws(), see comments there

    REBACT *doer = Make_Action(
        paramlist,
        &Specializer_Dispatcher,
        ACT_UNDERLYING(unspecialized), // common underlying action
        exemplar, // also provide a context of specialization values
        1 // details array capacity
    );

    Init_Frame(ARR_HEAD(ACT_DETAILS(doer)), exemplar);

    return Init_Action_Unbound(D_OUT, doer);
}
