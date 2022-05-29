//
//  File: %c-reframer.c
//  Summary: "Function that can transform arbitrary callsite functions"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2021 Ren-C Open Source Contributors
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
// REFRAMER allows one to define a function that does generalized transforms
// on the input (and output) of other functions.  Unlike ENCLOSE, it does not
// specify an exact function it does surgery on the frame of ahead of time.
// Instead, each invocation of the reframing action interacts with the
// instance that follows it at the callsite.
//
// A simple example is a function which removes quotes from the first
// parameter to a function, and adds them back for the result:
//
//     requote: reframer func [f [frame!]] [
//         p: first parameters of f
//         num-quotes: quotes of f/(p)
//
//         f/(p): noquote f/(p)
//
//         return quote/depth do f num-quotes
//     ]
//
//     >> item: just '''[a b c]
//     == '''[a b c]
//
//     >> requote append item <d>  ; append doesn't accept QUOTED! items
//     == '''[a b c <d>]   ; munging frame and result makes it seem to
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Enfix handling is not yet implemented, e.g. `requote '''1 + 2`
//
// * Because reframers need to know the function they are operating on, they
//   are unable to "see through" a GROUP! to get it, as a group could contain
//   multiple expressions.  So `requote (append item <d>)` cannot work.
//
// * If you "reframe a reframer" at the moment, you will not likely get what
//   you want...as the arguments you want to inspect will be compacted into
//   a frame argument.  It may be possible to make a "compound frame" that
//   captures the user-perceived combination of a reframer and what it's
//   reframing, but that would be technically difficult.
//

#include "sys-core.h"

enum {
    IDX_REFRAMER_SHIM = 1,  // action that can manipulate the reframed frame
    IDX_REFRAMER_PARAM_INDEX,  // index in shim that receives FRAME!
    IDX_REFRAMER_MAX
};


//
//  Make_Invokable_From_Feed_Throws: C
//
// This builds a frame from a feed *as if* it were going to be used to call
// an action, but doesn't actually make the call.  Instead it leaves the
// varlist available for other purposes.
//
// If the next item in the feed is not a WORD! or PATH! that look up to an
// action (nor an ACTION! literally) then the output will be set to a QUOTED!
// version of what would be evaluated to.  So in the case of NULL, it will be
// a single quote of nothing.
//
bool Make_Invokable_From_Feed_Throws(
    REBVAL *out,
    const RELVAL *first,  // if not END, override first value (vs. feed->value)
    REBFED *feed,
    bool error_on_deferred  // if not planning to keep running, can't ELSE/THEN
){
    assert(NOT_FEED_FLAG(feed, NEXT_ARG_FROM_OUT));  // not supported?

    const RELVAL *v = IS_END(first) ? feed->value : first;

    // !!! The case of `([x]: @)` wants to make something which when it
    // evaluates becomes invisible.  There's no QUOTED! value that can do
    // that, so if the feature is to be supported it needs to be VOID.
    //
    // Not all callers necessarily want to tolerate an end condition, so this
    // needs review.
    //
    if (IS_END(v)) {
        if (rebRunThrows(out, true, "make frame! :void"))
            return true;
        return false;
    }

    // Unfortunately, this means that `[x y]: ^(do f)` and `[x y]: ^ do f`
    // can't work.  The problem is that you don't know how many expressions
    // will be involved in these cases, and the multi-return is a syntax trick
    // that can only work when interacting with one function, and even plain
    // groups break that guarantee.  Do meta values with e.g. `[^x y]: do f`.
    //
    if (ANY_GROUP(v))  // `requote (append [a b c] #d, <can't-work>)`
        fail ("Actions made with REFRAMER cannot work with GROUP!s");

    DECLARE_FRAME (f, feed, EVAL_MASK_DEFAULT);  // FULFILL_ONLY added below
    Push_Frame(out, f);

    if (IS_WORD(v) or IS_TUPLE(v) or IS_PATH(v)) {
        DECLARE_LOCAL (steps);
        if (Get_Var_Push_Refinements_Throws(
            out,
            steps,
            v,
            FEED_SPECIFIER(feed)
        )){
            Drop_Frame(f);
            return true;
        }
    }
    else
        Derelativize(out, v, FEED_SPECIFIER(feed));

    if (not IS_ACTION(out)) {
        Quotify(out, 1);

        if (IS_END(first))
            Fetch_Next_Forget_Lookback(f);  // we've seen it now
        Drop_Frame(f);
        return false;
    }

    if (IS_END(first))
        Fetch_Next_Forget_Lookback(f);  // now, onto the arguments...

    option(const REBSTR*) label = VAL_ACTION_LABEL(f->out);

    // !!! Process_Action_Throws() calls Drop_Action() and loses the phase.
    // It probably shouldn't, but since it does we need the action afterward
    // to put the phase back.
    //
    DECLARE_LOCAL (action);
    Move_Cell(action, out);
    PUSH_GC_GUARD(action);

    // It is desired that any nulls encountered be processed as if they are
    // not specialized...and gather at the callsite if necessary.

    // The idea of creating a frame from an evaluative step which includes
    // infix as part of the step would ultimately have to make a composite
    // frame that captured the entire chain of the operation.  That's a heavy
    // concept, but for now we just try to get multiple returns to work which
    // are part of the evaluator and hence can do trickier things.
    //
    if (error_on_deferred)
        f->flags.bits |=
            EVAL_FLAG_ERROR_ON_DEFERRED_ENFIX;  // can't deal with ELSE/THEN

    Push_Action(f, VAL_ACTION(action), VAL_ACTION_BINDING(action));
    Begin_Prefix_Action(f, VAL_ACTION_LABEL(action));

    // Use this special mode where we ask the dispatcher not to run, just to
    // gather the args.  Push_Action() checks that it's not set, so we don't
    // set it until after that.
    //
    SET_EVAL_FLAG(f, FULFILL_ONLY);

    assert(FRM_BINDING(f) == VAL_ACTION_BINDING(action));  // no invocation

    assert(Is_Fresh(f->out));
    if (Process_Action_Maybe_Stale_Throws(f)) {
        DROP_GC_GUARD(action);
        Drop_Frame(f);
        return true;
    }
    assert(Is_Fresh(f->out));  // should only have gathered arguments

    // At the moment, Begin_Prefix_Action() marks the frame as having been
    // invoked...but since it didn't get managed it drops the flag in
    // Drop_Action().
    //
    // !!! The flag is new, as a gambit to try and avoid copying frames for
    // DO-ing just in order to expire the old identity.  Under development.
    //
    assert(NOT_SUBCLASS_FLAG(VARLIST, f->varlist, FRAME_HAS_BEEN_INVOKED));

    assert(NOT_EVAL_FLAG(f, FULFILL_ONLY));  // cleared by the evaluator

    // Drop_Action() clears out the phase and binding.  Put them back.
    // !!! Should it check EVAL_FLAG_FULFILL_ONLY?

    INIT_FRM_PHASE(f, VAL_ACTION(action));
    INIT_FRM_BINDING(f, VAL_ACTION_BINDING(action));

    // The function did not actually execute, so no SPC(f) was never handed
    // out...the varlist should never have gotten managed.  So this context
    // can theoretically just be put back into the reuse list, or managed
    // and handed out for other purposes by the caller.
    //
    REBARR *varlist = f->varlist;
    assert(NOT_SERIES_FLAG(varlist, MANAGED));  // not invoked yet
    f->varlist = nullptr;  // just let it GC, for now

    REBACT *act = VAL_ACTION(action);
    assert(FRM_BINDING(f) == VAL_ACTION_BINDING(action));

    INIT_BONUS_KEYSOURCE(varlist, ACT_KEYLIST(act));

    // May not be at end or thrown, e.g. (x: does+ just y x = 'y)
    //
    DROP_GC_GUARD(action);  // before drop to balance at right time
    Drop_Frame(f);

    // The exemplar may or may not be managed as of yet.  We want it
    // managed, but Push_Action() does not use ordinary series creation to
    // make its nodes, so manual ones don't wind up in the tracking list.
    //
    SET_SERIES_FLAG(varlist, MANAGED); // can't use Manage_Series

    Init_Frame(out, CTX(varlist), label);
    return false;
}


//
//  Make_Frame_From_Feed_Throws: C
//
// Making an invokable from a feed might return a QUOTED!, because that is
// more efficient (and truthful) than creating a FRAME! for the identity
// function.  However, MAKE FRAME! of a VARARGS! was an experimental feature
// that has to follow the rules of MAKE FRAME!...e.g. returning a frame.
// This converts QUOTED!s into frames for the identity function.
//
bool Make_Frame_From_Feed_Throws(
    REBVAL *out,
    const RELVAL *first,
    REBFED *feed,
    bool error_on_deferred
){
    if (Make_Invokable_From_Feed_Throws(out, first, feed, error_on_deferred))
        return true;

    if (IS_FRAME(out))
        return false;

    assert(IS_QUOTED(out));
    REBCTX *exemplar = Make_Context_For_Action(Lib(IDENTITY), DSP, nullptr);

    Unquotify(Copy_Cell(CTX_VAR(exemplar, 2), out), 1);

    // Should we save the WORD! from a variable access to use as the name of
    // the identity alias?
    //
    option(const REBSYM*) label = nullptr;
    Init_Frame(out, exemplar, label);
    return false;
}


// The REFRAMER native specializes out the FRAME! argument of the function
// being modified when it builds the interface.
//
// So the next thing to do is to fulfill the next function's frame without
// running it, in order to build a frame to put into that specialized slot.
// Then we run the reframer.
//
// !!! As a first cut we build on top of specialize, and look for the
// parameter by means of a particular labeled void.
//
REB_R Reframer_Dispatcher(REBFRM *f)
{
    REBFRM *frame_ = f;  // for RETURN macros

    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == IDX_REFRAMER_MAX);

    REBVAL* shim = DETAILS_AT(details, IDX_REFRAMER_SHIM);
    assert(IS_ACTION(shim));

    REBVAL* param_index = DETAILS_AT(details, IDX_REFRAMER_PARAM_INDEX);
    assert(IS_INTEGER(param_index));

    // First run ahead and make the frame we want from the feed.
    //
    // Note: We can't write the value directly into the arg (as this frame
    // may have been built by a higher level ADAPT or other function that
    // still holds references, and those references could be reachable by
    // code that runs to fulfill parameters...which could see partially
    // filled values).  And we don't want to overwrite f->out in case of
    // invisibility.  So the frame's spare cell is used.
    //
    bool error_on_deferred = true;
    if (Make_Invokable_From_Feed_Throws(
        f_spare,
        END_CELL,
        f->feed,
        error_on_deferred
    )){
        return_thrown (f->out);
    }

    REBVAL *arg = FRM_ARG(f, VAL_INT32(param_index));
    Move_Cell(arg, f_spare);

    INIT_FRM_PHASE(f, VAL_ACTION(shim));
    INIT_FRM_BINDING(f, VAL_ACTION_BINDING(shim));

    return R_REDO_CHECKED;  // the redo will use the updated phase & binding
}


//
//  reframer*: native [
//
//  {Make a function that manipulates an invocation at the callsite}
//
//      return: [action!]
//      shim "The action that has a FRAME! (or QUOTED!) argument to supply"
//          [action!]
//      /parameter "Shim parameter receiving the frame--defaults to last"
//          [word!]
//  ]
//
REBNATIVE(reframer_p)
{
    INCLUDE_PARAMS_OF_REFRAMER_P;

    REBACT *shim = VAL_ACTION(ARG(shim));
    option(const REBSYM*) label = VAL_ACTION_LABEL(ARG(shim));

    REBDSP dsp_orig = DSP;

    struct Reb_Binder binder;
    INIT_BINDER(&binder);
    REBCTX *exemplar = Make_Context_For_Action_Push_Partials(
        ARG(shim),
        dsp_orig,
        &binder,
        UNSET_ISOTOPE
    );

    option(REBCTX*) error = nullptr;  // can't fail() with binder in effect

    REBLEN param_index = 0;

    if (DSP != dsp_orig) {
        error = Error_User("REFRAMER can't use partial specializions ATM");
        goto cleanup_binder;
    }

  blockscope {
    const REBKEY *key;
    const REBPAR *param;

    if (REF(parameter)) {
        const REBSYM *symbol = VAL_WORD_SYMBOL(ARG(parameter));
        param_index = Get_Binder_Index_Else_0(&binder, symbol);
        if (param_index == 0) {
            error = Error_No_Arg(label, symbol);
            goto cleanup_binder;
        }
        key = CTX_KEY(exemplar, param_index);
        param = cast_PAR(CTX_VAR(exemplar, param_index));
    }
    else {
        param = Last_Unspecialized_Param(&key, shim);
        param_index = param - ACT_PARAMS_HEAD(shim) + 1;
    }

    // Make sure the parameter is able to accept FRAME! arguments (the type
    // checking will ultimately use the same slot we overwrite here!)
    //
    if (not TYPE_CHECK(param, REB_FRAME)) {
        DECLARE_LOCAL (label_word);
        if (label)
            Init_Word(label_word, unwrap(label));
        else
            Init_Blank(label_word);

        DECLARE_LOCAL (param_word);
        Init_Word(param_word, KEY_SYMBOL(key));

        error = Error_Expect_Arg_Raw(
            label_word,
            Datatype_From_Kind(REB_FRAME),
            param_word
        );
        goto cleanup_binder;
    }
  }

  cleanup_binder: {
    const REBKEY *tail;
    const REBKEY *key = ACT_KEYS(&tail, shim);
    const REBPAR *param = ACT_PARAMS_HEAD(shim);
    for (; key != tail; ++key, ++param) {
        if (Is_Specialized(param))
            continue;

        const REBSYM *symbol = KEY_SYMBOL(key);
        REBLEN index = Remove_Binder_Index_Else_0(&binder, symbol);
        assert(index != 0);
        UNUSED(index);
    }

    SHUTDOWN_BINDER(&binder);

    if (error)  // once binder is cleaned up, safe to raise errors
        fail (unwrap(error));
  }

    // We need the dispatcher to be willing to start the reframing step even
    // though the frame to be processed isn't ready yet.  So we have to
    // specialize the argument with something that type checks.  It wants a
    // FRAME!, so temporarily fill it with the exemplar frame itself.
    //
    // !!! An expired frame would be better, or tweaking the argument so it
    // takes a void and giving it ~pending~; would make bugs more obvious.
    //
    REBVAL *var = CTX_VAR(exemplar, param_index);
    assert(Is_Unset_Isotope(var));
    Copy_Cell(var, CTX_ARCHETYPE(exemplar));

    // Make action with enough space to store the implementation phase and
    // which parameter to fill with the *real* frame instance.
    //
    Manage_Series(CTX_VARLIST(exemplar));
    REBACT *reframer = Alloc_Action_From_Exemplar(
        exemplar,  // shim minus the frame argument
        &Reframer_Dispatcher,
        IDX_REFRAMER_MAX  // details array capacity => [shim, param_index]
    );

    REBARR *details = ACT_DETAILS(reframer);
    Copy_Cell(ARR_AT(details, IDX_REFRAMER_SHIM), ARG(shim));
    Init_Integer(ARR_AT(details, IDX_REFRAMER_PARAM_INDEX), param_index);

    return Init_Action(D_OUT, reframer, label, UNBOUND);
}
