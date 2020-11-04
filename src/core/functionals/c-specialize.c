//
//  File: %c-specialize.c
//  Summary: "Routines for Creating Function Variations with Fixed Parameters"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015-2020 Ren-C Open Source Contributors
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
// A specialization is an ACTION! which has some of its parameters fixed.
// e.g. `ap10: specialize 'append [value: 5 + 5]` makes ap10 have all the same
// refinements available as APPEND, but otherwise just takes one series arg,
// as it will always be appending 10.
//
// The method used is to store a FRAME! in the specialization's ACT_DETAILS().
// Any of those items that have ARG_MARKED_CHECKED are copied from that
// frame instead of gathered from the callsite.  Action_Executor() heeds this
// when walking parameters (see `f->special`).
//
// Code is shared between the SPECIALIZE native and specialization of a
// GET-PATH! via refinements, such as `adp: :append/dup/part`.  However,
// specifying a refinement that takes an argument *without* that argument is
// a "partial refinement specialization", made complicated by ordering:
//
//     foo: func [/A [integer!] /B [integer!] /C [integer!]] [...]
//
//     fooBC: :foo/B/C
//     fooCB: :foo/C/B
//
//     fooBC 1 2  ; /B = 1, /C = 2
//     fooCB 1 2  ; /B = 2, /C = 1
//
// Also, a call to `fooBC/A 1 2 3` does not want /A = 1, because it should act
// like `foo/B/C/A 1 2 3`.  Since the ordering matters, information encoding
// that order must be stored *somewhere*.
//
// It's solved with a simple mechanical trick--that may look counterintuitive
// at first.  Since unspecialized slots would usually only contain NULL,
// we sneak information into them without the ARG_MARKED_CHECKED bit.  This
// disrupts the default ordering by pushing refinements that have higher
// priority than fulfilling the unspecialized slot they are in.
//
// So when looking at `fooBC: :foo/B/C`
//
// * /A's slot would contain an instruction for /C.  As the evaluator visits
//   arguments in order it pushes /C as the current first-in-line to take
//   an argument at the callsite.  Yet /A has not been "specialized out", so
//   a call like `fooBC/A` is legal...it's just that pushing /C from the
//   /A slot means /A must wait to gather an argument at the callsite.
//
// * /B's slot would contain an instruction for /B.  This will push /B to now
//   be first in line in fulfillment.
//
// * /C's slot would hold a null, having the typical appearance of not
//   being specialized.
//

#include "sys-core.h"

enum {
    IDX_SPECIALIZER_FRAME = 0,  // Partially or fully filled FRAME! to run
    IDX_SPECIALIZER_MAX
};


//
//  Specializer_Dispatcher: C
//
// The evaluator does not do any special "running" of a specialized frame.
// All of the contribution that the specialization had to make was taken care
// of when Eval_Core() used f->special to fill from the exemplar.  So all this
// does is change the phase and binding to match the function this layer was
// specializing.
//
REB_R Specializer_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == IDX_SPECIALIZER_MAX);

    REBVAL *exemplar = SPECIFIC(ARR_AT(details, IDX_SPECIALIZER_FRAME));
    assert(IS_FRAME(exemplar));

    INIT_FRM_PHASE(f, VAL_PHASE(exemplar));
    FRM_BINDING(f) = VAL_BINDING(exemplar);

    return R_REDO_UNCHECKED; // redo uses the updated phase and binding
}


//
//  Make_Context_For_Action_Push_Partials: C
//
// This creates a FRAME! context with NULLED cells in the unspecialized slots
// that are available to be filled.  For partial refinement specializations
// in the action, it will push the refinement to the stack.  In this way it
// retains the ordering information implicit in the partial refinements of an
// action's existing specialization.
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
REBCTX *Make_Context_For_Action_Push_Partials(
    const REBVAL *action,  // need ->binding, so can't just be a REBACT*
    REBDSP lowest_ordered_dsp,  // caller can add refinement specializations
    struct Reb_Binder *opt_binder
){
    REBDSP highest_ordered_dsp = DSP;

    REBACT *act = VAL_ACTION(action);

    REBLEN num_slots = ACT_NUM_PARAMS(act) + 1;  // +1 is for CTX_ARCHETYPE()
    REBARR *varlist = Make_Array_Core(num_slots, SERIES_MASK_VARLIST);

    REBVAL *rootvar = RESET_CELL(
        ARR_HEAD(varlist),
        REB_FRAME,
        CELL_MASK_CONTEXT
    );
    INIT_VAL_CONTEXT_VARLIST(rootvar, varlist);
    INIT_VAL_CONTEXT_PHASE(rootvar, VAL_ACTION(action));
    INIT_BINDING(rootvar, VAL_BINDING(action));

    const REBVAL *param = ACT_PARAMS_HEAD(act);
    REBVAL *arg = rootvar + 1;
    const REBVAL *special = ACT_SPECIALTY_HEAD(act);  // of exemplar/paramlist

    REBLEN index = 1; // used to bind REFINEMENT! values to parameter slots

    REBCTX *exemplar = ACT_EXEMPLAR(act); // may be null
    if (exemplar)
        assert(special == CTX_VARS_HEAD(exemplar));
    else
        assert(special == ACT_PARAMS_HEAD(act));

    for (; NOT_END(param); ++param, ++arg, ++special, ++index) {
        Prep_Cell(arg);

        if (Is_Param_Hidden(param)) {  // specialized out
            assert(GET_CELL_FLAG(special, ARG_MARKED_CHECKED));
            Move_Value(arg, special); // doesn't copy ARG_MARKED_CHECKED
            SET_CELL_FLAG(arg, ARG_MARKED_CHECKED);

          continue_specialized:

            // Note: used to `assert(not IS_NULLED(arg));`
            assert(GET_CELL_FLAG(arg, ARG_MARKED_CHECKED));
            continue;  // Eval_Core() double-checks type in debug build
        }

        assert(NOT_CELL_FLAG(special, ARG_MARKED_CHECKED));

        const REBSTR *canon = VAL_PARAM_CANON(param);  // to add to binding
        if (not TYPE_CHECK(param, REB_TS_REFINEMENT)) {  // nothing to push

          continue_unspecialized:

            Init_Nulled(arg);
            if (opt_binder) {
                if (not Is_Param_Unbindable(param))
                    Add_Binder_Index(opt_binder, canon, index);
            }
            continue;
        }

        // Unspecialized refinement slots may have an SYM-WORD! in them that
        // reflects a partial that needs to be pushed to the stack.  (They
        // are in *reverse* order of use.)

        assert(
            (special == param and IS_PARAM(special))
            or (IS_SYM_WORD(special) or IS_NULLED(special))
        );

        if (IS_SYM_WORD(special)) {
            REBLEN partial_index = VAL_WORD_INDEX(special);
            Init_Any_Word_Bound( // push a SYM-WORD! to data stack
                DS_PUSH(),
                REB_SYM_WORD,
                VAL_STORED_CANON(special),
                exemplar,
                partial_index
            );
        }

        // Unspecialized or partially specialized refinement.  Check the
        // passed-in refinements on the stack for usage.
        //
        REBDSP dsp = highest_ordered_dsp;
        for (; dsp != lowest_ordered_dsp; --dsp) {
            REBVAL *ordered = DS_AT(dsp);
            if (VAL_STORED_CANON(ordered) != canon)
                continue;  // just continuing this loop

            assert(not IS_WORD_BOUND(ordered));  // we bind only one
            INIT_BINDING(ordered, varlist);
            INIT_WORD_INDEX_UNCHECKED(ordered, index);

            if (not Is_Typeset_Empty(param))  // needs argument
                goto continue_unspecialized;

            // If refinement named on stack takes no arguments, then it can't
            // be partially specialized...only fully, and won't be bound:
            //
            //     specialize 'append/only [only: false]  ; only not bound
            //
            Init_Word(arg, VAL_STORED_CANON(ordered));
            Refinify(arg);
            SET_CELL_FLAG(arg, ARG_MARKED_CHECKED);
            goto continue_specialized;
        }

        goto continue_unspecialized;
    }

    TERM_ARRAY_LEN(varlist, num_slots);
    MISC_META_NODE(varlist) = nullptr;  // GC sees this, we must initialize

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
    REBCTX *exemplar = Make_Context_For_Action_Push_Partials(
        action,
        lowest_ordered_dsp,
        opt_binder
    );

    Manage_Array(CTX_VARLIST(exemplar));  // !!! was needed before, review
    DS_DROP_TO(lowest_ordered_dsp);
    return exemplar;
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
    REBVAL *opt_def,  // !!! REVIEW: binding modified directly (not copied)
    REBDSP lowest_ordered_dsp
){
    assert(out != specializee);

    struct Reb_Binder binder;
    if (opt_def)
        INIT_BINDER(&binder);

    REBACT *unspecialized = VAL_ACTION(specializee);

    // This produces a context where partially specialized refinement slots
    // will be on the stack (including any we are adding "virtually", from
    // the current DSP down to the lowest_ordered_dsp).
    //
    REBCTX *exemplar = Make_Context_For_Action_Push_Partials(
        specializee,
        lowest_ordered_dsp,
        opt_def ? &binder : nullptr
    );
    Manage_Array(CTX_VARLIST(exemplar)); // destined to be managed, guarded

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
            VAL_ARRAY_AT_MUTABLE_HACK(opt_def),
            exemplar,
            FLAGIT_KIND(REB_SET_WORD),  // types to bind (just set-word!)
            0,  // types to "add midstream" to binding as we go (nothing)
            BIND_DEEP
        );

        // !!! Only one binder can be in effect, and we're calling arbitrary
        // code.  Must clean up now vs. in loop we do at the end.  :-(
        //
        RELVAL *key = CTX_KEYS_HEAD(exemplar);
        REBVAL *var = CTX_VARS_HEAD(exemplar);
        for (; NOT_END(key); ++key, ++var) {
            if (Is_Param_Unbindable(key))
                continue;  // !!! is this flag still relevant?
            if (Is_Param_Hidden(key)) {
                assert(GET_CELL_FLAG(var, ARG_MARKED_CHECKED));
                continue;
            }
            if (GET_CELL_FLAG(var, ARG_MARKED_CHECKED))
                continue;  // maybe refinement from stack, now specialized out
            Remove_Binder_Index(&binder, VAL_KEY_CANON(key));
        }
        SHUTDOWN_BINDER(&binder);

        // Run block and ignore result (unless it is thrown)
        //
        PUSH_GC_GUARD(exemplar);
        bool threw = Do_Any_Array_At_Throws(out, opt_def, SPECIFIED);
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
    Move_Value(DS_PUSH(), ACT_ARCHETYPE(unspecialized));

    REBVAL *param = rootkey + 1;
    REBVAL *arg = CTX_VARS_HEAD(exemplar);

    REBDSP ordered_dsp = lowest_ordered_dsp;

    for (; NOT_END(param); ++param, ++arg) {
        if (TYPE_CHECK(param, REB_TS_REFINEMENT)) {
            if (IS_BLANK(arg)) {
                //
                // !!! Temporary compatibility solution... blank refinements
                // are considered to be unusable ones that have been
                // specialized out.
                //
                Init_Nulled(arg);
                SET_CELL_FLAG(arg, ARG_MARKED_CHECKED);
                goto specialized_arg_no_typecheck;
            }

            if (IS_NULLED(arg)) {
                //
                // A refinement that is nulled is a candidate for usage at the
                // callsite.  Hence it must be pre-empted by our ordered
                // overrides.  -but- the overrides only apply if their slot
                // wasn't filled by the user code.  Yet these values we are
                // putting in disrupt that detection (!), so use another
                // flag (PUSH_PARTIAL) to reflect this state.
                //
                while (ordered_dsp != dsp_paramlist) {
                    ++ordered_dsp;
                    REBVAL *ordered = DS_AT(ordered_dsp);

                    if (not IS_WORD_BOUND(ordered))  // specialize 'print/asdf
                        fail (Error_Bad_Refine_Raw(ordered));

                    REBVAL *slot = CTX_VAR(exemplar, VAL_WORD_INDEX(ordered));
                    if (
                        IS_NULLED(slot) or GET_CELL_FLAG(slot, PUSH_PARTIAL)
                    ){
                        // It's still partial, so set up the pre-empt.
                        //
                        Init_Any_Word_Bound(
                            arg,
                            REB_SYM_WORD,
                            VAL_STORED_CANON(ordered),
                            exemplar,
                            VAL_WORD_INDEX(ordered)
                        );
                        SET_CELL_FLAG(arg, PUSH_PARTIAL);
                        goto unspecialized_arg;
                    }
                    // Otherwise the user filled it in, so skip to next...
                }

                goto unspecialized_arg;  // ran out...no pre-empt needed
            }

            if (GET_CELL_FLAG(arg, ARG_MARKED_CHECKED)) {
                assert(
                    IS_NULLED(arg)
                    or (
                        IS_REFINEMENT(arg)
                        and (
                            VAL_REFINEMENT_SPELLING(arg)
                            == VAL_PARAM_SPELLING(param)
                        )
                    )
                );
            }
            else
                Typecheck_Refinement_And_Canonize(param, arg);

            goto specialized_arg_no_typecheck;
        }

        switch (VAL_PARAM_CLASS(param)) {
          case REB_P_LOCAL:
            assert(IS_NULLED(arg));  // no bindings, you can't set these
            goto unspecialized_arg;

          default:
            break;
        }

        // It's an argument, either a normal one or a refinement arg.

        if (not IS_NULLED(arg))
            goto specialized_arg_with_check;

      unspecialized_arg:

        assert(NOT_CELL_FLAG(arg, ARG_MARKED_CHECKED));
        assert(
            IS_NULLED(arg)
            or (IS_SYM_WORD(arg) and TYPE_CHECK(param, REB_TS_REFINEMENT))
        );
        Move_Value(DS_PUSH(), param);
        continue;

      specialized_arg_with_check:

        // !!! If argument was previously specialized, should have been type
        // checked already... don't type check again (?)
        //
        if (Is_Param_Variadic(param))
            fail ("Cannot currently SPECIALIZE variadic arguments.");

       SET_CELL_FLAG(arg, ARG_MARKED_CHECKED);

      specialized_arg_no_typecheck:

        // Specialized-out arguments must still be in the parameter list,
        // for enumeration in the evaluator to line up with the frame values
        // of the underlying function.

        assert(GET_CELL_FLAG(arg, ARG_MARKED_CHECKED));
        Move_Value(DS_PUSH(), param);
        TYPE_SET(DS_TOP, REB_TS_HIDDEN);
        continue;
    }

    REBARR *paramlist = Pop_Stack_Values_Core(
        dsp_paramlist,
        SERIES_MASK_PARAMLIST
            | (SER(unspecialized)->header.bits & PARAMLIST_MASK_INHERIT)
    );
    Manage_Array(paramlist);
    RELVAL *rootparam = ARR_HEAD(paramlist);
    VAL_ACT_PARAMLIST_NODE(rootparam) = NOD(paramlist);

    MISC_META_NODE(paramlist) = nullptr;

    // Everything should have balanced out for a valid specialization
    //
    while (ordered_dsp != DSP) {
        ++ordered_dsp;
        REBVAL *ordered = DS_AT(ordered_dsp);
        if (not IS_WORD_BOUND(ordered))  // specialize 'print/asdf
            fail (Error_Bad_Refine_Raw(ordered));

        REBVAL *slot = CTX_VAR(exemplar, VAL_WORD_INDEX(ordered));
        assert(not IS_NULLED(slot) and NOT_CELL_FLAG(slot, PUSH_PARTIAL));
        UNUSED(slot);
    }
    DS_DROP_TO(lowest_ordered_dsp);

    REBACT *specialized = Make_Action(
        paramlist,
        &Specializer_Dispatcher,
        ACT_UNDERLYING(unspecialized),  // same underlying action as this
        exemplar,  // also provide a context of specialization values
        IDX_SPECIALIZER_MAX  // details array capacity
    );
    assert(CTX_KEYLIST(exemplar) == ACT_PARAMLIST(unspecialized));

    // The "body" is the FRAME! value of the specialization.  It takes on the
    // binding we want to use (which we can't put in the exemplar archetype,
    // that binding has to be UNBOUND).  It also remembers the original
    // action in the phase, so Specializer_Dispatcher() knows what to call.
    //
    RELVAL *body = ARR_AT(ACT_DETAILS(specialized), IDX_SPECIALIZER_FRAME);
    Move_Value(body, CTX_ARCHETYPE(exemplar));
    INIT_BINDING(body, VAL_BINDING(specializee));
    INIT_VAL_CONTEXT_PHASE(body, unspecialized);

    Init_Action(out, specialized, VAL_ACTION_LABEL(specializee), UNBOUND);
    return false;  // code block did not throw
}


//
//  specialize*: native [
//
//  {Create a new action through partial or full specialization of another}
//
//      return: [action!]
//      specializee "Function or specifying word (keeps word for debug info)"
//          [action! word! path!]
//      def "Definition for FRAME! fields for args and refinements"
//          [block!]
//  ]
//
REBNATIVE(specialize_p)  // see extended definition SPECIALIZE in %base-defs.r
{
    INCLUDE_PARAMS_OF_SPECIALIZE_P;

    REBVAL *specializee = ARG(specializee);

    // Refinement specializations via path are pushed to the stack, giving
    // order information that can't be meaningfully gleaned from an arbitrary
    // code block (e.g. `specialize 'append [dup: x | if y [part: z]]`, we
    // shouldn't think that intends any ordering of /dup/part or /part/dup)
    //
    REBDSP lowest_ordered_dsp = DSP; // capture before any refinements pushed
    if (Get_If_Word_Or_Path_Throws(
        D_OUT,
        specializee,
        SPECIFIED,
        true  // push_refines = true (don't generate temp specialization)
    )){
        return R_THROWN;  // e.g. `specialize 'append/(throw 10 'dup) [...]`
    }

    // Note: Even if there was a PATH! doesn't mean there were refinements
    // used, e.g. `specialize 'lib/append [...]`.

    if (not IS_ACTION(D_OUT))
        fail (PAR(specializee));
    Move_Value(specializee, D_OUT);  // Frees up D_OUT, GC guards action

    if (Specialize_Action_Throws(
        D_OUT,
        specializee,
        ARG(def),
        lowest_ordered_dsp
    )){
        return R_THROWN;  // e.g. `specialize 'append/dup [value: throw 10]`
    }

    return D_OUT;
}


//
//  For_Each_Unspecialized_Param: C
//
// We have to take into account specialization of refinements in order to know
// the correct order.  If someone has:
//
//     foo: func [a [integer!] /b [integer!] /c [integer!]] [...]
//
// They can partially specialize this as :foo/c/b.  This makes it seem to the
// caller a function originally written with spec:
//
//     [a [integer!] c [integer!] b [integer!]]
//
// But the frame order doesn't change; the information for knowing the order
// is encoded with instructions occupying the non-fully-specialized slots.
// (See %c-specialize.c for a description of the mechanic.)
//
// The true order could be cached when the function is generated, but to keep
// things "simple" we capture the behavior in this routine.
//
// Unspecialized parameters are visited in two passes: unsorted, then sorted.
//
void For_Each_Unspecialized_Param(
    REBACT *act,
    PARAM_HOOK hook,
    void *opaque
){
    REBDSP dsp_orig = DSP;

    // Do an initial scan to push the partial refinements in the reverse
    // order that they apply.  While walking the parameters in a potentially
    // "unsorted" fashion, offer them to the passed-in hook in case it has a
    // use for this first pass (e.g. just counting, to make an array big
    // enough to hold what's going to be given to it in the second pass.

    REBVAL *param = ACT_PARAMS_HEAD(act);
    REBVAL *special = ACT_SPECIALTY_HEAD(act);

    REBLEN index = 1;
    for (; NOT_END(param); ++param, ++special, ++index) {
        if (Is_Param_Hidden(param))
            continue;  // specialized out, not in interface

        Reb_Param_Class pclass = VAL_PARAM_CLASS(param);
        if (pclass == REB_P_LOCAL)
            continue;  // locals not in interface

        if (not hook(param, PHF_MASK_NONE, opaque)) {  // unsorted pass
            DS_DROP_TO(dsp_orig);
            return;
        }

        if (IS_SYM_WORD(special)) {
            assert(TYPE_CHECK(param, REB_TS_REFINEMENT));
            Move_Value(DS_PUSH(), special);
        }
    }

    // Refinements are now on stack such that topmost is first in-use
    // specialized refinement.

    // Now second loop, where we print out just the normal args.
    //
    param = ACT_PARAMS_HEAD(act);
    special = ACT_SPECIALTY_HEAD(act);
    for (; NOT_END(param); ++param, ++special) {
        if (Is_Param_Hidden(param))
            continue;

        if (TYPE_CHECK(param, REB_TS_REFINEMENT))
            continue;

        Reb_Param_Class pclass = VAL_PARAM_CLASS(param);
        if (pclass == REB_P_LOCAL)
            continue;

        // If the modal parameter has had its refinement specialized out, it
        // is no longer modal.
        //
        REBFLGS flags = PHF_SORTED_PASS;
        if (pclass == REB_P_MODAL) {
            if (NOT_END(param + 1)) {  // !!! Ideally checked at creation
                if (GET_CELL_FLAG(special + 1, ARG_MARKED_CHECKED)) {
                    if (TYPE_CHECK(param + 1, REB_TS_REFINEMENT))  // required
                        flags |= PHF_DEMODALIZED;  // !!! ^-- check at create!
                }
            }
        }

        if (not hook(param, flags, opaque)) {
            DS_DROP_TO(dsp_orig);
            return;
        }
    }

    // Now jump around and take care of the partial refinements.

    REBDSP dsp = DSP;  // highest priority are at *top* of stack, go downward
    while (dsp != dsp_orig) {
        param = ACT_PARAM(act, VAL_WORD_INDEX(DS_AT(dsp)));
        --dsp;

        bool cancel = not hook(
            param,
            PHF_SORTED_PASS | PHF_UNREFINED,
            opaque
        );

        if (cancel) {
            DS_DROP_TO(dsp_orig);
            return;
        }
    }

    // Finally, output any fully unspecialized refinements

    param = ACT_PARAMS_HEAD(act);
    UNUSED(special);  // stack is enough

    for (; NOT_END(param); ++param) {
        if (Is_Param_Hidden(param))
            continue;

        if (not TYPE_CHECK(param, REB_TS_REFINEMENT))
            continue;

        dsp = dsp_orig;
        while (dsp != DSP) {
            ++dsp;
            if (SAME_STR(
                VAL_WORD_SPELLING(DS_AT(dsp)),
                VAL_PARAM_SPELLING(param)
            )){
                goto continue_unspecialized_loop;
            }
        }

        if (not hook(param, PHF_SORTED_PASS, opaque)) {
            DS_DROP_TO(dsp_orig);
            return; // stack should be balanced here
        }

      continue_unspecialized_loop:
        NOOP;
    }

    DS_DROP_TO(dsp_orig);
}


struct First_Param_State {
    REBACT *act;
    REBVAL *first_unspecialized;
};

static bool First_Param_Hook(REBVAL *param, REBFLGS flags, void *opaque)
{
    struct First_Param_State *s = cast(struct First_Param_State*, opaque);
    assert(not s->first_unspecialized);  // should stop enumerating if found

    if (not (flags & PHF_SORTED_PASS))
        return true;  // can't learn anything until second pass

    if (not (flags & PHF_UNREFINED) and TYPE_CHECK(param, REB_TS_REFINEMENT))
        return false;  // we know WORD!-based invocations will be 0 arity

    s->first_unspecialized = param;
    return false;  // found first_unspecialized, no need to look more
}

//
//  First_Unspecialized_Param: C
//
// This can be somewhat complex in the worst case:
//
//     >> foo: func [/a [block!] /b [block!] /c [block!] /d [block!]] [...]
//     >> foo-d: :foo/d
//
// This means that the last parameter (D) is actually the first of FOO-D.
//
REBVAL *First_Unspecialized_Param(REBACT *act)
{
    struct First_Param_State s;
    s.act = act;
    s.first_unspecialized = nullptr;

    For_Each_Unspecialized_Param(act, &First_Param_Hook, &s);

    return s.first_unspecialized;  // may be nullptr
}


//
//  First_Unspecialized_Arg: C
//
// Helper built on First_Unspecialized_Param(), can also give you the param.
//
REBVAL *First_Unspecialized_Arg(REBVAL **opt_param_out, REBFRM *f)
{
    REBACT *phase = FRM_PHASE(f);
    REBVAL *param = First_Unspecialized_Param(phase);
    if (opt_param_out)
        *opt_param_out = param;

    if (param == nullptr)
        return nullptr;

    REBLEN index = param - ACT_PARAMS_HEAD(phase);
    return FRM_ARGS_HEAD(f) + index;
}


//
//  Make_Invocation_Frame_Throws: C
//
// Logic shared currently by DOES and MATCH to build a single executable
// frame from feeding forward a VARARGS! parameter.  A bit like being able to
// call EVALUATE via Eval_Core() yet introspect the evaluator step.
//
bool Make_Invocation_Frame_Throws(
    REBFRM *f,
    REBVAL **first_arg_ptr,  // returned so that MATCH can steal it
    const REBVAL *action
){
    assert(IS_ACTION(action));
    assert(f == FS_TOP);

    // It is desired that any nulls encountered be processed as if they are
    // not specialized...and gather at the callsite if necessary.
    //
    f->flags.bits |=
        EVAL_FLAG_ERROR_ON_DEFERRED_ENFIX;  // can't deal with ELSE/THEN/etc.

    // === END FIRST PART OF CODE FROM DO_SUBFRAME ===

    REBSTR *opt_label = nullptr; // !!! for now
    Push_Action(f, VAL_ACTION(action), VAL_BINDING(action));
    Begin_Prefix_Action(f, opt_label);

    // Use this special mode where we ask the dispatcher not to run, just to
    // gather the args.  Push_Action() checks that it's not set, so we don't
    // set it until after that.
    //
    SET_EVAL_FLAG(f, FULFILL_ONLY);

    assert(FRM_BINDING(f) == VAL_BINDING(action));  // no invoke to change it

    bool threw = Process_Action_Throws(f);

    assert(NOT_EVAL_FLAG(f, FULFILL_ONLY));  // cleared by the evaluator

    // Drop_Action() clears out the phase and binding.  Put them back.
    // !!! Should it check EVAL_FLAG_FULFILL_ONLY?

    INIT_FRM_PHASE(f, VAL_ACTION(action));
    FRM_BINDING(f) = VAL_BINDING(action);

    // The function did not actually execute, so no SPC(f) was never handed
    // out...the varlist should never have gotten managed.  So this context
    // can theoretically just be put back into the reuse list, or managed
    // and handed out for other purposes by the caller.
    //
    assert(NOT_SERIES_FLAG(f->varlist, MANAGED));

    if (threw)
        return true;

    assert(IS_NULLED(f->out)); // guaranteed by dummy, for the skipped action

    // === END SECOND PART OF CODE FROM DO_SUBFRAME ===

    *first_arg_ptr = nullptr;

    REBVAL *refine = nullptr;
    REBVAL *param = CTX_KEYS_HEAD(CTX(f->varlist));
    REBVAL *arg = CTX_VARS_HEAD(CTX(f->varlist));
    for (; NOT_END(param); ++param, ++arg) {
        Reb_Param_Class pclass = VAL_PARAM_CLASS(param);
        if (TYPE_CHECK(param, REB_TS_REFINEMENT)) {
            refine = param;
        }
        else switch (pclass) {
          case REB_P_NORMAL:
          case REB_P_HARD_QUOTE:
          case REB_P_MODAL:
          case REB_P_SOFT_QUOTE:
            if (not refine or VAL_LOGIC(refine)) {
                *first_arg_ptr = arg;
                goto found_first_arg_ptr;
            }
            break;

          case REB_P_LOCAL:
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
//  Make_Frame_From_Varargs_Throws: C
//
// Routines like MATCH or DOES are willing to do impromptu specializations
// from a feed of instructions, so that a frame for an ACTION! can be made
// without actually running it yet.  This is also exposed by MAKE ACTION!.
//
// This pre-manages the exemplar, because it has to be done specially (it gets
// "stolen" out from under an evaluator's REBFRM*, and was manually tracked
// but never in the manual series list.)
//
bool Make_Frame_From_Varargs_Throws(
    REBVAL *out,
    const REBVAL *specializee,
    const REBVAL *varargs
){
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

    // REBFRM whose built FRAME! context we will steal

    DECLARE_FRAME (f, parent->feed, EVAL_MASK_DEFAULT);
    Push_Frame(out, f);

    if (Get_If_Word_Or_Path_Throws(
        out,
        specializee,
        SPECIFIED,
        true  // push_refinements = true (DECLARE_FRAME captured original DSP)
    )){
        Drop_Frame(f);
        return true;
    }

    if (not IS_ACTION(out))
        fail (specializee);

    const REBSTR *label = VAL_ACTION_LABEL(out);

    DECLARE_LOCAL (action);
    Move_Value(action, out);
    PUSH_GC_GUARD(action);

    // We interpret phrasings like `x: does all [...]` to mean something
    // like `x: specialize 'all [block: [...]]`.  While this originated
    // from the Rebmu code golfing language to eliminate a pair of bracket
    // characters from `x: does [all [...]]`, it actually has different
    // semantics...which can be useful in their own right, plus the
    // resulting function will run faster.

    REBVAL *first_arg;
    if (Make_Invocation_Frame_Throws(f, &first_arg, action)) {
        DROP_GC_GUARD(action);
        return true;
    }

    UNUSED(first_arg); // MATCH uses to get its answer faster, we don't need

    REBACT *act = VAL_ACTION(action);

    assert(NOT_SERIES_FLAG(f->varlist, MANAGED)); // not invoked yet
    assert(FRM_BINDING(f) == VAL_BINDING(action));

    REBCTX *exemplar = Steal_Context_Vars(CTX(f->varlist), NOD(act));
    assert(ACT_NUM_PARAMS(act) == CTX_LEN(exemplar));

    INIT_LINK_KEYSOURCE(exemplar, NOD(act));

    SET_SERIES_FLAG(f->varlist, MANAGED); // is inaccessible
    f->varlist = nullptr; // just let it GC, for now

    // May not be at end or thrown, e.g. (x: does lit y x = 'y)
    //
    DROP_GC_GUARD(action);  // before drop to balance at right time
    Drop_Frame(f);

    // The exemplar may or may not be managed as of yet.  We want it
    // managed, but Push_Action() does not use ordinary series creation to
    // make its nodes, so manual ones don't wind up in the tracking list.
    //
    SET_SERIES_FLAG(exemplar, MANAGED); // can't use Manage_Series

    Init_Frame(out, exemplar, label);
    return false;
}


//
//  Alloc_Action_From_Exemplar: C
//
// Leaves details blank, and lets you specify the dispatcher.
//
REBACT *Alloc_Action_From_Exemplar(
    REBCTX *exemplar,
    REBNAT dispatcher,
    REBLEN details_capacity
){
    REBACT *unspecialized = ACT(CTX_KEYLIST(exemplar));

    REBLEN num_slots = ACT_NUM_PARAMS(unspecialized) + 1;
    REBARR *paramlist = Make_Array_Core(num_slots, SERIES_MASK_PARAMLIST);

    RELVAL *archetype = RESET_CELL(
        ARR_HEAD(paramlist),
        REB_ACTION,
        CELL_MASK_ACTION
    );
    VAL_ACT_PARAMLIST_NODE(archetype) = NOD(paramlist);
    INIT_BINDING(archetype, UNBOUND);
    TERM_ARRAY_LEN(paramlist, 1);

    MISC_META_NODE(paramlist) = nullptr;  // REDESCRIBE can add help

    REBVAL *param = ACT_PARAMS_HEAD(unspecialized);
    REBVAL *arg = CTX_VARS_HEAD(exemplar);
    RELVAL *alias = archetype + 1;
    for (; NOT_END(param); ++param, ++arg, ++alias) {
        Move_Value(alias, param);
        if (not IS_NULLED(arg)) {
            //
            // Don't show argument in the parameter list.
            //
            TYPE_SET(alias, REB_TS_HIDDEN);
            TYPE_SET(alias, REB_TS_UNBINDABLE);

            // Indicate that argument is specialized out.
            //
            SET_CELL_FLAG(arg, ARG_MARKED_CHECKED);
        }
    }

    TERM_ARRAY_LEN(paramlist, num_slots);
    Manage_Array(paramlist);

    // This code parallels Specialize_Action_Throws(), see comments there

    REBACT *action = Make_Action(
        paramlist,
        dispatcher,
        ACT_UNDERLYING(unspecialized),  // common underlying action
        exemplar,  // also provide a context of specialization values
        details_capacity
    );

    return action;
}


//
//  Make_Action_From_Exemplar: C
//
// Assumes you want a Specializer_Dispatcher with the exemplar in details.
//
REBACT *Make_Action_From_Exemplar(REBCTX *exemplar)
{
    REBACT *action = Alloc_Action_From_Exemplar(
        exemplar,
        &Specializer_Dispatcher,
        IDX_SPECIALIZER_MAX  // details capacity
    );
    Init_Frame(
        ARR_AT(ACT_DETAILS(action), IDX_SPECIALIZER_FRAME),
        exemplar,
        ANONYMOUS
    );
    return action;
}
