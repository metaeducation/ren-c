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
// Eval_Core_Throws() heeds these when walking parameters (see `L->special`),
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
// which goes up to TOP_INDEX for the highest of those added specializations.
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
    const Value* action,   // need ->binding, so can't just be a REBACT*
    StackIndex lowest_stackindex, // caller can add refinement specializations
    struct Reb_Binder *opt_binder
){
    StackIndex highest_stackindex = TOP_INDEX;  // highest ordered refinement

    REBACT *act = VAL_ACTION(action);

    REBLEN num_slots = ACT_NUM_PARAMS(act) + 1;
    Array* varlist = Make_Array_Core(
        num_slots, // includes +1 for the CTX_ARCHETYPE() at [0]
        SERIES_MASK_CONTEXT
    );

    Value* rootvar = RESET_CELL(Array_Head(varlist), REB_FRAME);
    rootvar->payload.any_context.varlist = varlist;
    rootvar->payload.any_context.phase = VAL_ACTION(action);
    INIT_BINDING(rootvar, VAL_BINDING(action));

    // Copy values from any prior specializations, transforming REFINEMENT!
    // used for partial specializations into INTEGER! or null, depending
    // on whether that slot was actually specialized out.

    const Value* param = ACT_PARAMS_HEAD(act);
    Value* arg = rootvar + 1;
    const Value* special = ACT_SPECIALTY_HEAD(act); // of exemplar/paramlist

    REBLEN index = 1; // used to bind REFINEMENT! values to parameter slots

    REBCTX *exemplar = ACT_EXEMPLAR(act); // may be null
    if (exemplar)
        assert(special == CTX_VARS_HEAD(exemplar));
    else
        assert(special == ACT_PARAMS_HEAD(act));

    for (; NOT_END(param); ++param, ++arg, ++special, ++index) {
        arg->header.bits = CELL_MASK_ERASE;

        Symbol* canon = Cell_Param_Canon(param);

        assert(special != param or NOT_VAL_FLAG(arg, ARG_MARKED_CHECKED));

    //=//// NON-REFINEMENT SLOT HANDLING //////////////////////////////////=//

        if (VAL_PARAM_CLASS(param) != PARAM_CLASS_REFINEMENT) {
            if (Is_Param_Hidden(param)) {
                assert(GET_VAL_FLAG(special, ARG_MARKED_CHECKED));
                Copy_Cell(arg, special); // !!! copy the flag?
                SET_VAL_FLAG(arg, ARG_MARKED_CHECKED); // !!! not copied
                goto continue_specialized; // Eval_Core_Throws() checks type
            }
            goto continue_unspecialized;
        }

    //=//// REFINEMENT PARAMETER HANDLING /////////////////////////////////=//

        if (Is_Blank(special)) { // specialized BLANK! => "disabled"
            Init_Blank(arg);
            SET_VAL_FLAG(arg, ARG_MARKED_CHECKED);
            goto continue_specialized;
        }

        if (Is_Refinement(special)) { // specialized REFINEMENT! => "in use"
            Init_Refinement(arg, Cell_Parameter_Symbol(param));
            SET_VAL_FLAG(arg, ARG_MARKED_CHECKED);
            goto continue_specialized;
        }

        // Refinement argument slots are tricky--they can be unspecialized,
        // -but- have an ISSUE! in them we need to push to the stack.
        // (they're in *reverse* order of use).  Or they may be specialized
        // and have a NULL in them pushed by an earlier slot.  Refinements
        // in use must be turned into INTEGER! partials, to point to the
        // StackIndex of their stack order.

        if (Is_Issue(special)) {
            REBLEN partial_index = VAL_WORD_INDEX(special);
            Init_Any_Word_Bound( // push an ISSUE! to data stack
                PUSH(),
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
                Value* passed = rootvar + partial_index;
                assert(passed->header.bits == CELL_MASK_ERASE);

                assert(
                    VAL_STORED_CANON(special) ==
                    Cell_Param_Canon(
                        CTX_KEYS_HEAD(exemplar) + partial_index - 1
                    )
                );

                Init_Integer(passed, TOP_INDEX);
                SET_VAL_FLAG(passed, ARG_MARKED_CHECKED); // passed, not arg

                if (partial_index == index)
                    goto continue_specialized; // just filled in *this* slot
            }

            // We know this is partial (and should be set to an INTEGER!)
            // but it may have been pushed to the stack already, or it may
            // be coming along later.  Search only the higher priority
            // pushes since the call began.
            //
            canon = Cell_Param_Canon(param);
            StackIndex stackindex = TOP_INDEX;
            for (; stackindex != highest_stackindex; --stackindex) {
                Value* ordered = Data_Stack_At(stackindex);
                assert(IS_WORD_BOUND(ordered));
                if (VAL_WORD_INDEX(ordered) == index) { // prescient push
                    assert(canon == VAL_STORED_CANON(ordered));
                    Init_Integer(arg, stackindex);
                    SET_VAL_FLAG(arg, ARG_MARKED_CHECKED);
                    goto continue_specialized;
                }
            }

            assert(arg->header.bits == CELL_MASK_ERASE);  // skip slot for now
            continue;
        }

        assert(
            special == param
            or Is_Nulled(special)
            or (
                Is_Nothing(special)
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
            StackIndex stackindex = highest_stackindex;
            for (; stackindex != lowest_stackindex; --stackindex) {
                Value* ordered = Data_Stack_At(stackindex);
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
                Init_Integer(arg, stackindex);
                SET_VAL_FLAG(arg, ARG_MARKED_CHECKED);
                goto continue_specialized;
            }
        }

        goto continue_unspecialized;

      continue_unspecialized:;

        assert(arg->header.bits == CELL_MASK_ERASE);
        Init_Nulled(arg);
        if (opt_binder) {
            if (not Is_Param_Unbindable(param))
                Add_Binder_Index(opt_binder, canon, index);
        }
        continue;

      continue_specialized:;

        assert(not Is_Nulled(arg));
        assert(GET_VAL_FLAG(arg, ARG_MARKED_CHECKED));
        continue;
    }

    Term_Array_Len(varlist, num_slots);
    MISC(varlist).meta = nullptr;  // GC sees this, we must initialize

    INIT_CTX_KEYLIST_SHARED(CTX(varlist), ACT_PARAMLIST(act));
    return CTX(varlist);
}


//
//  Make_Context_For_Action: C
//
// !!! The ultimate concept is that it would be possible for a FRAME! to
// preserve ordering information such that an ACTION! could be made from it.
// Right now the information is the stack ordering numbers of the refinements
// which to make it usable should be relative to the lowest ordered stackindex
// and not absolute.
//
REBCTX *Make_Context_For_Action(
    const Value* action, // need ->binding, so can't just be a REBACT*
    StackIndex lowest_stackindex,
    struct Reb_Binder *opt_binder
){
    REBCTX *exemplar = Make_Context_For_Action_Int_Partials(
        action,
        lowest_stackindex,
        opt_binder
    );

    Manage_Flex(CTX_VARLIST(exemplar)); // !!! was needed before, review
    Drop_Data_Stack_To(lowest_stackindex);
    return exemplar;
}


// Each time we transition the refine field we need to check to see if a
// partial became fulfilled, and if so transition it to not being put into
// the partials.  Better to do it with a macro than repeat the code.  :-/
//
#define FINALIZE_REFINE_IF_FULFILLED \
    assert(evoked != refine or evoked->payload.partial.stackindex == 0); \
    if (VAL_TYPE_RAW(refine) == REB_X_PARTIAL) { \
        if (not GET_VAL_FLAG(refine, PARTIAL_FLAG_SAW_NULL_ARG)) { \
            if (refine->payload.partial.stackindex != 0) \
    /* full! */ Init_Blank(Data_Stack_At(refine->payload.partial.stackindex)); \
            else if (refine == evoked) \
                evoked = nullptr;  /* allow other evoke to be last partial! */ \
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
// has /DUP at TOP, and /PART under it.  List stops at lowest_stackindex.
//
bool Specialize_Action_Throws(
    Value* out,
    Value* specializee,
    Symbol* opt_specializee_name,
    Value* opt_def, // !!! REVIEW: binding modified directly (not copied)
    StackIndex lowest_stackindex
){
    assert(out != specializee);

    struct Reb_Binder binder;
    if (opt_def)
        INIT_BINDER(&binder, nullptr);

    REBACT *unspecialized = VAL_ACTION(specializee);

    // This produces a context where partially specialized refinement slots
    // will be INTEGER! pointing into the stack at the partial order
    // position. (This takes into account any we are adding "virtually", from
    // the current TOP_INDEX down to the lowest_stackindex).
    //
    // Note that REB_X_PARTIAL can't be used in slots yet, because the GC
    // will be able to see this frame (code runs bound into it).
    //
    REBCTX *exemplar = Make_Context_For_Action_Int_Partials(
        specializee,
        lowest_stackindex,
        opt_def ? &binder : nullptr
    );
    Manage_Flex(CTX_VARLIST(exemplar)); // destined to be managed, guarded

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
            Cell_List_At(opt_def),
            exemplar,
            FLAGIT_KIND(REB_SET_WORD), // types to bind (just set-word!)
            0, // types to "add midstream" to binding as we go (nothing)
            BIND_DEEP
        );

        // !!! Only one binder can be in effect, and we're calling arbitrary
        // code.  Must clean up now vs. in loop we do at the end.  :-(
        //
        Cell* key = CTX_KEYS_HEAD(exemplar);
        Value* var = CTX_VARS_HEAD(exemplar);
        for (; NOT_END(key); ++key, ++var) {
            if (Is_Param_Unbindable(key))
                continue; // !!! is this flag still relevant?
            if (Is_Param_Hidden(key)) {
                assert(GET_VAL_FLAG(var, ARG_MARKED_CHECKED));
                continue;
            }
            if (GET_VAL_FLAG(var, ARG_MARKED_CHECKED))
                continue; // may be refinement from stack, now specialized out
            Remove_Binder_Index(&binder, Key_Canon(key));
        }
        SHUTDOWN_BINDER(&binder);

        // Run block and ignore result (unless it is thrown)
        //
        Push_GC_Guard(exemplar);
        bool threw = Do_At_Throws(out, opt_def);
        Drop_GC_Guard(exemplar);

        if (threw) {
            Drop_Data_Stack_To(lowest_stackindex);
            return true;
        }
    }

    Value* rootkey = CTX_ROOTKEY(exemplar);

    // Build up the paramlist for the specialized function on the stack.
    // The same walk used for that is used to link and process REB_X_PARTIAL
    // arguments for whether they become fully specialized or not.

    StackIndex paramlist_base = TOP_INDEX;
    Copy_Cell(PUSH(), ACT_ARCHETYPE(unspecialized));

    Value* param = rootkey + 1;
    Value* arg = CTX_VARS_HEAD(exemplar);
    Value* refine = ORDINARY_ARG; // parallels states in Eval_Core_Throw()
    REBLEN index = 1;

    Value* first_partial = nullptr;
    Value* last_partial = nullptr;

    Value* evoked = nullptr;

    for (; NOT_END(param); ++param, ++arg, ++index) {
        switch (VAL_PARAM_CLASS(param)) {
        case PARAM_CLASS_REFINEMENT: {
            FINALIZE_REFINE_IF_FULFILLED; // see macro
            refine = arg;

            if (
                Is_Nulled(refine)
                or (
                    Is_Integer(refine)
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

                StackIndex partial_stackindex =
                    Is_Nulled(refine) ? 0 : VAL_INT32(refine);

                if (not first_partial)
                    first_partial = refine;
                else
                    last_partial->extra.next_partial = refine;

                RESET_CELL(refine, REB_X_PARTIAL);
                refine->payload.partial.stackindex = partial_stackindex;
                refine->payload.partial.index = index;
                Corrupt_Pointer_If_Debug(refine->extra.next_partial);

                last_partial = refine;

                if (partial_stackindex == 0)
                    goto unspecialized_arg_but_may_evoke;

                // Though Make_Level_For_Specialization() knew this slot was
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
                    Is_Refinement(refine)
                    and (
                        Cell_Word_Symbol(refine)
                        == Cell_Parameter_Symbol(param)
                    )
                )
            );

            if (IS_TRUTHY(refine))
                Init_Refinement(refine, Cell_Parameter_Symbol(param));
            else
                Init_Blank(arg);

            SET_VAL_FLAG(arg, ARG_MARKED_CHECKED);
            goto specialized_arg_no_typecheck; }

        case PARAM_CLASS_RETURN:
        case PARAM_CLASS_LOCAL:
            assert(Is_Nulled(arg)); // no bindings, you can't set these
            goto unspecialized_arg;

        default:
            break;
        }

        // It's an argument, either a normal one or a refinement arg.

        if (refine == ORDINARY_ARG) {
            if (Is_Nulled(arg))
                goto unspecialized_arg;

            goto specialized_arg;
        }

        if (VAL_TYPE_RAW(refine) == REB_X_PARTIAL) {
            if (Is_Nulled(arg)) { // we *know* it's not completely fulfilled
                SET_VAL_FLAG(refine, PARTIAL_FLAG_SAW_NULL_ARG);
                goto unspecialized_arg;
            }

            if (refine->payload.partial.stackindex != 0)  // started true
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
            TYPE_SET(TOP, REB_TS_HIDDEN);

            evoked = refine;  // gets reset to nullptr if ends up fulfilled
            SET_VAL_FLAG(refine, PARTIAL_FLAG_IN_USE);
            goto specialized_arg;
        }

        assert(Is_Blank(refine) or Is_Refinement(refine));

        if (Is_Blank(refine)) {
            //
            // `specialize 'append [dup: false count: 10]` is not legal.
            //
            if (not Is_Nulled(arg))
                fail (Error_Bad_Refine_Revoke(param, arg));
            goto specialized_arg_no_typecheck;
        }

        if (not Is_Nulled(arg))
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
        refine->payload.partial.stackindex = 0;  // no ordered stack position
        refine->payload.partial.index = index - (arg - refine);
        Corrupt_Pointer_If_Debug(refine->extra.next_partial);

        last_partial = refine;

        SET_VAL_FLAG(refine, PARTIAL_FLAG_SAW_NULL_ARG); // this is a null arg
        evoked = refine;  // ...we won't ever set this back to nulltpr later
        goto unspecialized_arg;

    unspecialized_arg_but_may_evoke:;

        assert(refine->payload.partial.stackindex == 0);

    unspecialized_arg:;

        assert(NOT_VAL_FLAG(arg, ARG_MARKED_CHECKED));
        Copy_Cell(PUSH(), param);  // if evoked, DROP'd from the paramlist
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

        Copy_Cell(PUSH(), param);
        TYPE_SET(TOP, REB_TS_HIDDEN);
        continue;
    }

    if (first_partial) {
        FINALIZE_REFINE_IF_FULFILLED; // last chance (no more refinements)
        last_partial->extra.next_partial = nullptr; // not needed until now
    }

    Array* paramlist = Pop_Stack_Values_Core(
        paramlist_base,
        SERIES_MASK_ACTION
    );
    Manage_Flex(paramlist);
    Cell* rootparam = Array_Head(paramlist);
    rootparam->payload.action.paramlist = paramlist;

    // PARAM_CLASS_REFINEMENT slots which started partially specialized (or
    // unspecialized) in the exemplar now all contain REB_X_PARTIAL, but we
    // must now convert these transitional placeholders to...
    //
    // * NOTHING -- Unspecialized, BUT in traversal order before a partial
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
    Value* ordered = Data_Stack_At(lowest_stackindex);
    while (ordered != TOP) {
        if (Is_Blank(ordered + 1)) // blanked when seen no longer partial
            ++ordered;
        else
            break;
    }

    Value* partial = first_partial;
    while (partial) {
        assert(VAL_TYPE_RAW(partial) == REB_X_PARTIAL);
        Value* next_partial = partial->extra.next_partial; // overwritten

        if (NOT_VAL_FLAG(partial, PARTIAL_FLAG_IN_USE)) {
            if (ordered == TOP)
                Init_Nulled(partial); // no more partials coming
            else {
                Init_Nothing(partial);  // still partials to go, signal pre-empt
                SET_VAL_FLAG(partial, ARG_MARKED_CHECKED);
            }
            goto continue_loop;
        }

        if (NOT_VAL_FLAG(partial, PARTIAL_FLAG_SAW_NULL_ARG)) { // filled
            Init_Refinement(
                partial,
                Cell_Parameter_Symbol(rootkey + partial->payload.partial.index)
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
            REBLEN evoked_index = evoked->payload.partial.index;
            Init_Any_Word_Bound(
                partial,
                REB_ISSUE,
                Cell_Param_Canon(rootkey + evoked_index),
                exemplar,
                evoked_index
            );
            SET_VAL_FLAG(partial, ARG_MARKED_CHECKED);

            evoked = nullptr;
            goto continue_loop;
        }

        if (ordered == TOP) { // some partials fully specialized
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

        while (ordered != TOP) {
            if (Is_Blank(ordered + 1))
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
    if (ordered != TOP)
        fail (Error_Bad_Refine_Raw(ordered)); // specialize 'print/asdf
    Drop_Data_Stack_To(lowest_stackindex);

    // See %sysobj.r for `specialized-meta:` object template

    Value* example = Get_System(SYS_STANDARD, STD_SPECIALIZED_META);

    REBCTX *meta = Copy_Context_Shallow_Managed(VAL_CONTEXT(example));

    Init_Nulled(CTX_VAR(meta, STD_SPECIALIZED_META_DESCRIPTION)); // default
    Copy_Cell(
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
    Cell* body = Array_Head(ACT_DETAILS(specialized));
    Copy_Cell(body, CTX_ARCHETYPE(exemplar));
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
// of when Eval_Core_Throws() used L->special to fill from the exemplar.  So
// all this does is change the phase and binding to match the function this
// layer was specializing.
//
Bounce Specializer_Dispatcher(Level* L)
{
    Array* details = ACT_DETAILS(Level_Phase(L));

    Value* exemplar = KNOWN(Array_Head(details));
    assert(Is_Frame(exemplar));

    Level_Phase(L) = exemplar->payload.any_context.phase;
    LVL_BINDING(L) = VAL_BINDING(exemplar);

    return BOUNCE_REDO_UNCHECKED; // redo uses the updated phase and binding
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
DECLARE_NATIVE(specialize)
{
    INCLUDE_PARAMS_OF_SPECIALIZE;

    Value* specializee = ARG(specializee);

    StackIndex lowest_stackindex = TOP_INDEX;

    // Any partial refinement specializations are pushed to the stack, and
    // gives ordering information that TRUE assigned in a code block can't.
    //
    Symbol* opt_name;
    if (Get_If_Word_Or_Path_Throws(
        OUT,
        &opt_name,
        specializee,
        SPECIFIED,
        true // push_refines = true (don't generate temp specialization)
    )){
        // e.g. `specialize 'append/(throw 10 'dup) [value: 20]`
        //
        return BOUNCE_THROWN;
    }

    // Note: Even if there was a PATH! doesn't mean there were refinements
    // used, e.g. `specialize 'lib/append [...]`.

    if (not Is_Action(OUT))
        fail (Error_Invalid(specializee));
    Copy_Cell(specializee, OUT); // Frees OUT, and GC safe (in ARG slot)

    if (Specialize_Action_Throws(
        OUT,
        specializee,
        opt_name,
        ARG(def),
        lowest_stackindex
    )){
        // e.g. `specialize 'append/dup [value: throw 10]`
        //
        return BOUNCE_THROWN;
    }

    return OUT;
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
Bounce Block_Dispatcher(Level* L)
{
    Array* details = ACT_DETAILS(Level_Phase(L));
    Cell* block = Array_Head(details);
    assert(Is_Block(block));

    if (IS_SPECIFIC(block)) {
        if (LVL_BINDING(L) == UNBOUND) {
            if (Do_At_Throws(L->out, KNOWN(block)))
                return BOUNCE_THROWN;
            return L->out;
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

        Array* body_array = Copy_And_Bind_Relative_Deep_Managed(
            KNOWN(block),
            ACT_PARAMLIST(Level_Phase(L)),
            TS_WORD
        );

        // Preserve file and line information from the original, if present.
        //
        if (Get_Array_Flag(Cell_Array(block), HAS_FILE_LINE)) {
            LINK(body_array).file = LINK(Cell_Array(block)).file;
            MISC(body_array).line = MISC(Cell_Array(block)).line;
            Set_Array_Flag(body_array, HAS_FILE_LINE);
        }

        // Need to do a raw initialization of this block Cell because it is
        // relative to a function.  (Init_Block assumes all specific values.)
        //
        INIT_VAL_ARRAY(block, body_array);
        VAL_INDEX(block) = 0;
        INIT_BINDING(block, Level_Phase(L)); // relative binding

        // Block is now a relativized copy; we won't do this again.
    }

    assert(IS_RELATIVE(block));

    if (Do_At_Throws(
        L->out,
        Cell_Array(block),
        VAL_INDEX(block),
        SPC(L->varlist)
    )){
        return BOUNCE_THROWN;
    }

    return L->out;
}


//
//  does: native [
//
//  {Create an arity-0 function that runs a block}
//
//      return: [action!]
//      value [block!]
//  ]
//
DECLARE_NATIVE(does)
{
    INCLUDE_PARAMS_OF_DOES;

    Value* value = ARG(value);

    Array* paramlist = Make_Array_Core(
        1, // archetype only...DOES always makes action with no arguments
        SERIES_MASK_ACTION
    );

    Value* archetype = RESET_CELL(Alloc_Tail_Array(paramlist), REB_ACTION);
    archetype->payload.action.paramlist = paramlist;
    INIT_BINDING(archetype, UNBOUND);
    Term_Array_Len(paramlist, 1);

    MISC(paramlist).meta = nullptr; // REDESCRIBE can be used to add help

    //
    // `does [...]` and `does do [...]` are not exactly the same.  The
    // generated ACTION! of the first form uses Block_Dispatcher() and
    // does on-demand relativization, so it's "kind of like" a `func []`
    // in forwarding references to members of derived objects.  Also, it
    // is optimized to not run the block with the DO native...hence a
    // HIJACK of DO won't be triggered by invocations of the first form.
    //
    Manage_Flex(paramlist);
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
    Cell* body = Array_Head(ACT_DETAILS(doer));
    Flex* locker = nullptr;
    Force_Value_Frozen_Deep(value, locker);
    Copy_Cell(body, value);

    return Init_Action_Unbound(OUT, doer);
}
