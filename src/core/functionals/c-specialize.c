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
// e.g. `ap10: specialize :append [value: 5 + 5]` makes ap10 have all the same
// refinements available as APPEND, but otherwise just takes one series arg,
// as it will always be appending 10.
//
// Specialization is done by means of making a new "exemplar" frame for the
// action.  Slots in that frame that would have held TYPESET! information for
// the parameter are replaced by the fixed value, which is type checked.
//
// Partial specialization uses a different mechanism.  `:file-to-local/pass`
// fulfills a frame slot value since /PASS has no arguments, but `:append/part`
// does not.  Distinctions between `:append/dup/part` and `:append/part/dup`
// require ordering information that has to be tracked outside of the
// exemplar frame.
//

#include "sys-core.h"

// The exemplar alone is sufficient information for the specialization frame.
// Hence a compact "singular" array of 1 cell can be used for the details.
//
enum {
    IDX_SPECIALIZER_MAX = 1  // has just ACT_DETAILS[0], the ACT_ARCHETYPE()
};


//
//  Specializer_Dispatcher: C
//
// The evaluator does not do any special "running" of a specialized frame.
// All of the contribution that the specialization had to make was taken care
// of when Eval_Core() used f->param to fill from the exemplar.  So all this
// does is change the phase and binding to match the function this layer was
// specializing.
//
Bounce Specializer_Dispatcher(Frame(*) f)
{
    Context(*) exemplar = ACT_EXEMPLAR(FRM_PHASE(f));

    INIT_FRM_PHASE(f, CTX_FRAME_ACTION(exemplar));
    INIT_FRM_BINDING(f, CTX_FRAME_BINDING(exemplar));

    return BOUNCE_REDO_UNCHECKED; // redo uses the updated phase and binding
}


//
//  Make_Context_For_Action_Push_Partials: C
//
// For partial refinement specializations in the action, this will push the
// refinement to the stack.  In this way it retains the ordering information
// implicit in the partial refinements of an action's existing specialization.
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
Context(*) Make_Context_For_Action_Push_Partials(
    const REBVAL *action,  // need ->binding, so can't just be a Action(*)
    StackIndex lowest_ordered_stackindex,  // caller can add refinements
    option(struct Reb_Binder*) binder,
    const REBVAL *unspecialized  // what to put in unspecialized slots
){
    StackIndex highest_ordered_stackindex = TOP_INDEX;

    Action(*) act = VAL_ACTION(action);

    REBLEN num_slots = ACT_NUM_PARAMS(act) + 1;  // +1 is for CTX_ARCHETYPE()
    Array(*) varlist = Make_Array_Core(num_slots, SERIES_MASK_VARLIST);
    SET_SERIES_LEN(varlist, num_slots);

    INIT_CTX_KEYLIST_SHARED(CTX(varlist), ACT_KEYLIST(act));

    Cell(*) rootvar = ARR_HEAD(varlist);
    INIT_VAL_FRAME_ROOTVAR(
        rootvar,
        varlist,
        VAL_ACTION(action),
        VAL_ACTION_BINDING(action)
    );

    // If there is a PARTIALS list, then push its refinements.
    //
    Array(*) partials = try_unwrap(ACT_PARTIALS(act));
    if (partials) {
        Cell(const*) word_tail = ARR_TAIL(partials);
        Cell(const*) word = ARR_HEAD(partials);
        for (; word != word_tail; ++word) {
            Copy_Cell(PUSH(), SPECIFIC(word));
            assert(Is_Pushed_Refinement(TOP));
        }
    }

    const REBKEY *tail;
    const REBKEY *key = ACT_KEYS(&tail, act);
    const REBPAR *param = ACT_PARAMS_HEAD(act);

    REBVAL *arg = SPECIFIC(rootvar) + 1;

    REBLEN index = 1;  // used to bind REFINEMENT! values to parameter slots

    for (; key != tail; ++key, ++param, ++arg, ++index) {
        Erase_Cell(arg);

        if (Is_Specialized(param)) {  // includes locals
            Copy_Cell(arg, param);

          continue_specialized:

            continue;
        }

        Symbol(const*) symbol = KEY_SYMBOL(key);  // added to binding
        if (NOT_PARAM_FLAG(param, REFINEMENT)) {  // nothing to push

          continue_unspecialized:

            Copy_Cell(arg, unspecialized);
            if (binder)
                Add_Binder_Index(unwrap(binder), symbol, index);

            continue;
        }

        // Unspecialized refinement slot.  It may be partially specialized,
        // e.g. we may have pushed to the stack from the PARTIALS for it.
        //
        // !!! If partials were allowed to encompass things like /ONLY then
        // we would have to use that to fill the slot here.  For the moment,
        // a full new exemplar is generated for parameterless refinements
        // which seems expensive for the likes of :append/only, when we
        // can make :append/dup more compactly.  Rethink.

        // Check the passed-in refinements on the stack for usage.
        //
        StackIndex stackindex = highest_ordered_stackindex;
        for (; stackindex != lowest_ordered_stackindex; --stackindex) {
            StackValue(*) ordered = Data_Stack_At(stackindex);
            if (VAL_WORD_SYMBOL(ordered) != symbol)
                continue;  // just continuing this loop

            assert(not IS_WORD_BOUND(ordered));  // we bind only one
            INIT_VAL_WORD_BINDING(ordered, varlist);
            INIT_VAL_WORD_INDEX(ordered, index);

            if (not Is_Parameter_Unconstrained(param))  // needs argument
                goto continue_unspecialized;

            // If refinement named on stack takes no arguments, then it can't
            // be partially specialized...only fully, and won't be bound:
            //
            //     specialize :skip/unbounded [unbounded: #]  ; word not bound
            //
            Init_Blackhole(arg);
            goto continue_specialized;
        }

        goto continue_unspecialized;
    }

    mutable_MISC(VarlistAdjunct, varlist) = nullptr;
    mutable_LINK(Patches, varlist) = nullptr;

    return CTX(varlist);
}


//
//  Make_Context_For_Action: C
//
// This creates a FRAME! context with `~` isotopes in all of the unspecialized
// slots.  The reason this is chosen instead of NULL is that specialization
// with NULL is frequent, while frame values may not be isotopes when a
// function is called.  The only way to take isotopes is to use a ^meta
// argument, and they are represented as non-isotopes in the frame.
//
// !!! The ultimate concept is that it would be possible for a FRAME! to
// preserve ordering information such that an ACTION! could be made from it.
// Right now the information is the stack ordering numbers of the refinements
// which to make it usable should be relative to the lowest ordered StackIndex
// and not absolute.
//
Context(*) Make_Context_For_Action(
    const REBVAL *action, // need ->binding, so can't just be a Action(*)
    StackIndex lowest_ordered_stackindex,
    option(struct Reb_Binder*) binder
){
    Context(*) exemplar = Make_Context_For_Action_Push_Partials(
        action,
        lowest_ordered_stackindex,
        binder,
        NONE_CELL
    );

    Manage_Series(CTX_VARLIST(exemplar));  // !!! was needed before, review
    Drop_Data_Stack_To(lowest_ordered_stackindex);
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
// has /DUP at TOP, and /PART under it.  List stops at lowest_ordered_dsp.
//
bool Specialize_Action_Throws(
    REBVAL *out,
    REBVAL *specializee,
    option(REBVAL*) def,  // !!! REVIEW: binding modified directly, not copied
    StackIndex lowest_ordered_stackindex
){
    assert(out != specializee);

    struct Reb_Binder binder;
    if (def)
        INIT_BINDER(&binder);

    Action(*) unspecialized = VAL_ACTION(specializee);

    // This produces a context where partially specialized refinement slots
    // will be on the stack (including any we are adding "virtually", from
    // the current TOP_INDEX down to the lowest_ordered_dsp).
    //
    Context(*) exemplar = Make_Context_For_Action_Push_Partials(
        specializee,
        lowest_ordered_stackindex,
        def ?
            &binder
            : cast(struct Reb_Binder*, nullptr),  // C++98 ambiguous w/o cast
        NONE_CELL
    );
    Manage_Series(CTX_VARLIST(exemplar)); // destined to be managed, guarded

    if (def) { // code that fills the frame...fully or partially
        //
        // Bind all the SET-WORD! in the body that match params in the frame
        // into the frame.  This means `value: value` can very likely have
        // `value:` bound for assignments into the frame while `value` refers
        // to whatever value was in the context the specialization is running
        // in, but this is likely the more useful behavior.
        //
        Virtual_Bind_Deep_To_Existing_Context(
            unwrap(def),
            exemplar,
            &binder,
            REB_SET_WORD
        );

        // !!! Only one binder can be in effect, and we're calling arbitrary
        // code.  Must clean up now vs. in loop we do at the end.  :-(
        //
        const REBKEY *tail;
        const REBKEY *key = ACT_KEYS(&tail, unspecialized);
        const REBPAR *param = ACT_PARAMS_HEAD(unspecialized);
        for (; key != tail; ++key, ++param) {
            if (Is_Specialized(param))
                continue;  // maybe refinement from stack, now specialized out

            Remove_Binder_Index(&binder, KEY_SYMBOL(key));
        }
        SHUTDOWN_BINDER(&binder);

        // Run block and ignore result (unless it is thrown)
        //
        PUSH_GC_GUARD(exemplar);
        bool threw = Do_Any_Array_At_Throws(out, unwrap(def), SPECIFIED);
        DROP_GC_GUARD(exemplar);

        if (threw) {
            Drop_Data_Stack_To(lowest_ordered_stackindex);
            return true;
        }

        FRESHEN(out);
    }

    const REBKEY *tail;
    const REBKEY *key = ACT_KEYS(&tail, unspecialized);
    const REBPAR *param = ACT_PARAMS_HEAD(unspecialized);

    REBVAL *arg = CTX_VARS_HEAD(exemplar);

    StackIndex ordered_stackindex = lowest_ordered_stackindex;

    for (; key != tail; ++key, ++param, ++arg) {
        if (Is_Specialized(param))
            continue;

        assert(IS_PARAMETER(param));

        // You can't specialize with ~ isotopes ("none"), these indicate
        // unspecialized arguments.  Only ^META parameters can take meta-none
        // (e.g. a plain quasi-void, or ~)
        //
        if (Is_None(arg)) {  // unspecialized argument
            Copy_Cell(arg, param);
            continue;
        }

        // !!! If argument was previously specialized, should have been type
        // checked already... don't type check again (?)
        //
        if (GET_PARAM_FLAG(param, VARIADIC))
            fail ("Cannot currently SPECIALIZE variadic arguments.");

        if (not Typecheck_Coerce_Argument(param, arg)) {
            option(Symbol(const*)) label = VAL_ACTION_LABEL(specializee);
            fail (Error_Arg_Type(label, key, param, arg));
        }
    }

    // Everything should have balanced out for a valid specialization.
    // Turn partial refinements into an array of things to push.
    //
    Array(*) partials;
    if (ordered_stackindex == TOP_INDEX)
        partials = nullptr;
    else {
        // The list of ordered refinements may contain some cases like /ONLY
        // which aren't considered partial because they have no argument.
        // If that's the only kind of partial we hvae, we'll free this array.
        //
        // !!! This array will be allocated too big in cases like /dup/only,
        // review how to pick the exact size efficiently.  There's also the
        // case that duplicate refinements or non-existent ones create waste,
        // but since we error and throw those arrays away it doesn't matter.
        //
        partials = Make_Array_Core(
            TOP_INDEX - ordered_stackindex,  // maximum partial count possible
            SERIES_MASK_PARTIALS  // don't manage, yet... may free
        );

        while (ordered_stackindex != TOP_INDEX) {
            ++ordered_stackindex;
            StackValue(*) ordered = Data_Stack_At(ordered_stackindex);
            if (not IS_WORD_BOUND(ordered)) {  // specialize :print/asdf
                Refinify_Pushed_Refinement(ordered);
                fail (Error_Bad_Parameter_Raw(ordered));
            }

            REBVAL *slot = CTX_VAR(exemplar, VAL_WORD_INDEX(ordered));
            if (not Is_Specialized(cast(REBPAR*, slot))) {
                //
                // It's still partial...
                //
                assert(VAL_WORD_INDEX(ordered) != 0);
                Init_Pushable_Refinement_Bound(
                    Alloc_Tail_Array(partials),
                    KEY_SYMBOL(CTX_KEY(exemplar, VAL_WORD_INDEX(ordered))),
                    exemplar,
                    VAL_WORD_INDEX(ordered)
                );
            }
        }
        Drop_Data_Stack_To(lowest_ordered_stackindex);

        if (ARR_LEN(partials) == 0) {
            Free_Unmanaged_Series(partials);
            partials = nullptr;
        }
        else {
            Manage_Series(partials);
        }
    }

    Action(*) specialized = Make_Action(
        CTX_VARLIST(exemplar),
        partials,
        &Specializer_Dispatcher,
        IDX_SPECIALIZER_MAX  // details array capacity
    );
    assert(CTX_KEYLIST(exemplar) == ACT_KEYLIST(unspecialized));

    if (Get_Action_Flag(VAL_ACTION(specializee), ENFIXED))
        Set_Action_Flag(specialized, ENFIXED);  // if incoming was, outgoing is

    Init_Activation(out, specialized, VAL_ACTION_LABEL(specializee), UNBOUND);

    return false;  // code block did not throw
}


//
//  specialize*: native [
//
//  {Create a new action through partial or full specialization of another}
//
//      return: [activation!]
//      action "Function whose parameters will be set to fixed values"
//          [<unrun> action!]
//      def "Definition for FRAME! fields for args and refinements"
//          [block!]
//  ]
//
DECLARE_NATIVE(specialize_p)  // see extended SPECIALIZE in %base-defs.r
//
// 1. Refinement specializations via path are pushed to the stack, giving
//    order information that can't be meaningfully gleaned from an arbitrary
//    code block (e.g. `specialize :append [dup: x | if y [part: z]]`, we
//    shouldn't think that intends any ordering of /dup/part or /part/dup)
{
    INCLUDE_PARAMS_OF_SPECIALIZE_P;

    Value(*) specializee = ARG(action);
    Value(*) def = ARG(def);

    if (Specialize_Action_Throws(
        OUT,
        specializee,
        def,
        STACK_BASE  // lowest ordered stackindex, see [1]
    )){
        return THROWN;  // e.g. `specialize :append/dup [value: throw 10]`
    }

    return OUT;
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
// is encoded in a "partials" array.  See remarks on ACT_PARTIALS().
//
// The true order could be cached when the function is generated, but to keep
// things "simple" we capture the behavior in this routine.
//
// Unspecialized parameters are visited in two passes: unsorted, then sorted.
//
void For_Each_Unspecialized_Param(
    Action(*) act,
    PARAM_HOOK hook,
    void *opaque
){
    option(Array(*)) partials = ACT_PARTIALS(act);

    // Walking the parameters in a potentially "unsorted" fashion.  Offer them
    // to the passed-in hook in case it has a use for this first pass (e.g.
    // just counting, to make an array big enough to hold what's going to be
    // given to it in the second pass.
    //
  blockscope {
    const REBKEY *tail;
    const REBKEY *key = ACT_KEYS(&tail, act);
    const REBPAR *param = ACT_PARAMS_HEAD(act);

    // Loop through and pass just the normal args.
    //
    for (; key != tail; ++key, ++param) {
        if (Is_Specialized(param))
            continue;

        if (GET_PARAM_FLAG(param, REFINEMENT))
            continue;

        if (VAL_PARAM_CLASS(param) == PARAM_CLASS_RETURN)
            continue;

        Flags flags = 0;

        if (partials) {  // even normal parameters can appear in partials
            Cell(const*) partial_tail = ARR_TAIL(unwrap(partials));
            Cell(const*) partial = ARR_HEAD(unwrap(partials));
            for (; partial != partial_tail; ++partial) {
                if (Are_Synonyms(
                    VAL_WORD_SYMBOL(partial),
                    KEY_SYMBOL(key)
                )){
                    goto skip_in_first_pass;
                }
            }
        }

        if (not hook(key, param, flags, opaque))
            return;

      skip_in_first_pass: {}
    }
  }

    // Now jump around and take care of the partial refinements.

    if (partials) {
        assert(ARR_LEN(unwrap(partials)) > 0);  // no partials means no array

        // the highest priority are at *top* of stack, so we have to go
        // "downward" in the push order...e.g. the reverse of the array.

        Cell(*) partial = ARR_TAIL(unwrap(partials));
        Cell(*) head = ARR_HEAD(unwrap(partials));
        for (; partial-- != head; ) {
            const REBKEY *key = ACT_KEY(act, VAL_WORD_INDEX(partial));
            const REBPAR *param = ACT_PARAM(act, VAL_WORD_INDEX(partial));

            if (not hook(key, param, PHF_UNREFINED, opaque))
                return;
        }
    }

    // Finally, output any fully unspecialized refinements

  blockscope {
    const REBKEY *key_tail;
    const REBKEY *key = ACT_KEYS(&key_tail, act);
    const REBPAR *param = ACT_PARAMS_HEAD(act);

    for (; key != key_tail; ++key, ++param) {
        if (Is_Specialized(param))
            continue;

        if (
            NOT_PARAM_FLAG(param, REFINEMENT)
            or VAL_PARAM_CLASS(param) == PARAM_CLASS_RETURN
        ){
            continue;
        }

        if (partials) {
            Cell(const*) partial_tail = ARR_TAIL(unwrap(partials));
            Cell(const*) partial = ARR_HEAD(unwrap(partials));
            for (; partial != partial_tail; ++partial) {
                if (Are_Synonyms(
                    VAL_WORD_SYMBOL(partial),
                    KEY_SYMBOL(key)
                )){
                    goto continue_unspecialized_loop;
                }
            }
        }

        if (not hook(key, param, 0, opaque))
            return;

      continue_unspecialized_loop:
        NOOP;
    }
  }
}


struct Find_Param_State {
    const REBKEY *key;
    const REBPAR *param;
};

static bool First_Param_Hook(
    const REBKEY *key,
    const REBPAR *param,
    Flags flags,
    void *opaque
){
    struct Find_Param_State *s = cast(struct Find_Param_State*, opaque);
    assert(not s->key);  // should stop enumerating if found

    if (not (flags & PHF_UNREFINED) and GET_PARAM_FLAG(param, REFINEMENT))
        return false;  // we know WORD!-based invocations will be 0 arity

    s->key = key;
    s->param = param;
    return false;  // found first unspecialized, no need to look more
}

static bool Last_Param_Hook(
    const REBKEY *key,
    const REBPAR *param,
    Flags flags,
    void *opaque
){
    struct Find_Param_State *s = cast(struct Find_Param_State*, opaque);

    if (not (flags & PHF_UNREFINED) and GET_PARAM_FLAG(param, REFINEMENT))
        return false;  // we know WORD!-based invocations will be 0 arity

    s->key = key;
    s->param = param;
    return true;  // keep looking and be left with the last
}

//
//  First_Unspecialized_Param: C
//
// This can be somewhat complex in the worst case:
//
//     >> foo: func [/a [block!] /b [block!] /c [block!] /d [block!]] [...]
//     >> foo-d: runs :foo/d
//
// This means that the last parameter (D) is actually the first of FOO-D.
//
const REBPAR *First_Unspecialized_Param(const REBKEY ** key, Action(*) act)
{
    struct Find_Param_State s;
    s.key = nullptr;
    s.param = nullptr;

    For_Each_Unspecialized_Param(act, &First_Param_Hook, &s);

    if (key)
        *key = s.key;
    return s.param;  // may be nullptr
}


//
//  Last_Unspecialized_Param: C
//
// See notes on First_Unspecialized_Param() regarding complexity
//
const REBPAR *Last_Unspecialized_Param(const REBKEY ** key, Action(*) act)
{
    struct Find_Param_State s;
    s.key = nullptr;
    s.param = nullptr;

    For_Each_Unspecialized_Param(act, &Last_Param_Hook, &s);

    if (key)
        *key = s.key;
    return s.param;  // may be nullptr
}

//
//  First_Unspecialized_Arg: C
//
// Helper built on First_Unspecialized_Param(), can also give you the param.
//
REBVAL *First_Unspecialized_Arg(option(const REBPAR **) param_out, Frame(*) f)
{
    Action(*) phase = FRM_PHASE(f);
    const REBPAR *param = First_Unspecialized_Param(nullptr, phase);
    if (param_out)
        *unwrap(param_out) = param;

    if (param == nullptr)
        return nullptr;

    REBLEN index = param - ACT_PARAMS_HEAD(phase);
    return FRM_ARGS_HEAD(f) + index;
}


//
//  Alloc_Action_From_Exemplar: C
//
// Leaves details blank, and lets you specify the dispatcher.
//
Action(*) Alloc_Action_From_Exemplar(
    Context(*) exemplar,
    option(Symbol(const*)) label,
    Dispatcher* dispatcher,
    REBLEN details_capacity
){
    Action(*) unspecialized = CTX_FRAME_ACTION(exemplar);

    const REBKEY *tail;
    const REBKEY *key = ACT_KEYS(&tail, unspecialized);
    const REBPAR *param = ACT_PARAMS_HEAD(unspecialized);
    REBVAL *arg = CTX_VARS_HEAD(exemplar);
    for (; key != tail; ++key, ++arg, ++param) {
        if (Is_Specialized(param))
            continue;

        // We leave non-hidden `~` isotopes to be handled by the evaluator as
        // unspecialized (which means putting it back to the parameter
        // description info, e.g. the typeset for now):
        //
        // https://forum.rebol.info/t/default-values-and-make-frame/1412
        // https://forum.rebol.info/t/1413
        //
        if (Is_None(arg)) {
            assert(IS_PARAMETER(param));
            Copy_Cell(arg, param);
            continue;
        }

        if (not Typecheck_Coerce_Argument(param, arg))
            fail (Error_Arg_Type(label, key, param, arg));
    }

    // This code parallels Specialize_Action_Throws(), see comments there

    Action(*) action = Make_Action(
        CTX_VARLIST(exemplar),
        nullptr,  // no partials
        dispatcher,
        details_capacity
    );

    return action;
}


//
//  Make_Action_From_Exemplar: C
//
// Assumes you want a Specializer_Dispatcher with the exemplar in details.
//
Action(*) Make_Action_From_Exemplar(
    Context(*) exemplar,
    option(Symbol(const*)) label
){
    Action(*) action = Alloc_Action_From_Exemplar(
        exemplar,
        label,
        &Specializer_Dispatcher,
        IDX_SPECIALIZER_MAX  // details capacity
    );
    return action;
}
