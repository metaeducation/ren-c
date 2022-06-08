//
//  File: %c-reorder.c
//  Summary: "Function Generator for Reordering Parameters"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2020-2021 Ren-C Open Source Contributors
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
// REORDER allows you to create a variation of a function that uses the same
// underlying frame format, but reorders the parameters.  For instance, APPEND
// usually expects the series to append to as the first item:
//
//     >> append [a b c] <item>
//     == [a b c <item>]
//
// But a REORDER takes a block of parameters in the order you wish:
//
//     >> append-value-first: reorder :append [value series]
//
//     >> append-value-first <item> [a b c]
//     == [a b c <item>]
//
// It's currently necessary to specify all the required parameters in a
// reordering.  However, optional parameters may be mentioned as well:
//
//     >> append-val-dup-ser: reorder :append [value dup series]
//
//     >> append-val-dup-ser <item> 3 [a b c]
//     == [a b c <item> <item> <item>]
//
// This feature effectively exposes a more ergonomic form of the reordering
// that is possible using refinements in path dispatch.  The same mechanism
// of applying a second pass over the frame (using indices accrued during the
// first pass) is used to achieve it:
//
//     >> append/series <item> [a b c]  ; use series parameter on 2nd pass
//     == [a b c <item>]
//
// But `:append/dup/series` is not very intuitive for getting the order
// of [value dup series] (and gets more counterintuitive the more normal
// parameters a function has).
//

#include "sys-core.h"

enum {
    IDX_REORDERER_REORDEREE = 1,  // saves the function being reordered
    IDX_REORDERER_MAX
};


//
//  Reorderer_Dispatcher: C
//
// The reordered function was saved in the details, and all we need to do
// is switch the phase to that function.
//
// Note that this function may not be the same one that the exemplar context
// was created for; exemplars can be reused by functions that don't need to
// tweak them (e.g. ADAPT).
//
REB_R Reorderer_Dispatcher(REBFRM *f) {
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == IDX_REORDERER_MAX);

    REBVAL *reorderee = DETAILS_AT(details, IDX_REORDERER_REORDEREE);

    INIT_FRM_PHASE(f, VAL_ACTION(reorderee));
    INIT_FRM_BINDING(f, VAL_ACTION_BINDING(reorderee));

    return R_REDO_UNCHECKED;  // exemplar unchanged; known to be valid
}


//
//  reorder*: native [
//
//  {Create variation of a function with its arguments reordered}
//
//      return: [action!]
//      action [action!]
//      ordering "Parameter WORD!s, all required parameters must be mentioned"
//          [block!]
//  ]
//
REBNATIVE(reorder_p)  // see REORDER in %base-defs.r, for inheriting meta
{
    INCLUDE_PARAMS_OF_REORDER_P;

    REBACT *reorderee = VAL_ACTION(ARG(action));
    option(const REBSYM*) label  = VAL_ACTION_LABEL(ARG(action));

    // Working with just the exemplar means we will lose the partials ordering
    // information from the interface.  But that's what we want, as the
    // caller is to specify a complete ordering.
    //
    REBCTX *exemplar = ACT_EXEMPLAR(reorderee);

    // We need a binder to efficiently map arguments to their position in
    // the parameters array, and track which parameters are mentioned.

    struct Reb_Binder binder;
    INIT_BINDER(&binder);

  blockscope {
    const REBKEY *tail;
    const REBKEY *key = ACT_KEYS(&tail, reorderee);
    const REBPAR *param = ACT_PARAMS_HEAD(reorderee);
    REBLEN index = 1;
    for (; key != tail; ++key, ++param, ++index) {
        if (Is_Specialized(param))
            continue;
        Add_Binder_Index(&binder, KEY_SYMBOL(key), index);
    }
  }

    // IMPORTANT: Binders use global state and code is not allowed to fail()
    // without cleaning the binder up first, balancing it all out to zeros.
    // Errors must be stored and reported after the cleanup.
    //
    option(REBCTX*) error = nullptr;

    REBDSP dsp_orig = DSP;

    // We proceed through the array, and remove the binder indices as we go.
    // This lets us check for double uses or use of words that aren't in the
    // spec, and a final pass can check to make sure all mandatory parameters
    // have been spoken for in the order.
    //
    // We iterate backwards, because that's the stack order that needs to
    // be pushed.
    //
    const Cell *item;  // starts as tail
    const Cell *at = VAL_ARRAY_AT(&item, ARG(ordering));
    for (; at != item--; ) {
        const REBSYM *symbol = VAL_WORD_SYMBOL(item);

        // !!! As a bit of a weird demo of a potential future direction, we
        // don't just allow WORD!s but allow you to do things like pass the
        // full `parameters of`, e.g. reversed.
        //
        bool ignore = false;
        if (ANY_WORD(item)) {  // on the record, we only just allow WORD!...
            symbol = VAL_WORD_SYMBOL(item);
        }
        else if (IS_REFINEMENT(item)) {
            symbol = VAL_REFINEMENT_SYMBOL(item);
            ignore = true;  // to use a refinement, don't /refine it
        }
        else if (IS_QUOTED(item)) {
            if (
                VAL_QUOTED_DEPTH(item) != 1
                or not ANY_WORD_KIND(CELL_HEART(item))
            ) {
                error = Error_User("REORDER allows single quoted ANY-WORD!");
                goto cleanup_binder;
            }
            symbol = VAL_WORD_SYMBOL(VAL_UNESCAPED(item));
        }
        else {
            error = Error_User("Unknown REORDER element");
            goto cleanup_binder;
        }

        REBLEN index = Remove_Binder_Index_Else_0(&binder, symbol);
        if (index == 0) {
            error = Error_Bad_Parameter_Raw(item);
            goto cleanup_binder;
        }

        if (ignore)
            continue;

        const REBVAL *param = ACT_PARAM(reorderee, index);
        if (GET_PARAM_FLAG(param, REFINEMENT) and Is_Typeset_Empty(param)) {
            error = Error_User("Can't reorder refinements with no argument");
            goto cleanup_binder;
        }

        Init_Any_Word_Bound(DS_PUSH(), REB_WORD, exemplar, symbol, index);
    }

    // Make sure that all parameters that were mandatory got a place in the
    // ordering list.

  cleanup_binder: {
    const REBKEY *tail;
    const REBKEY *key = ACT_KEYS(&tail, reorderee);
    const REBPAR *param = ACT_PARAMS_HEAD(reorderee);
    REBLEN index = 1;
    for (; key != tail; ++key, ++param, ++index) {
        if (Is_Specialized(param))
            continue;

        const REBSYM *symbol = KEY_SYMBOL(key);

        // If we saw the parameter, we removed its index from the binder.
        //
        bool mentioned = (0 == Remove_Binder_Index_Else_0(&binder, symbol));

        if (
            not error  // don't report an error here if one is pending
            and not mentioned
            and NOT_PARAM_FLAG(param, REFINEMENT)  // okay to leave out
        ){
            error = Error_No_Arg(label, KEY_SYMBOL(key));
        }
    }
  }

    SHUTDOWN_BINDER(&binder);

    if (error)  // *now* it's safe to fail...
        fail (unwrap(error));

    REBARR *partials = Pop_Stack_Values_Core(
        dsp_orig,
        SERIES_FLAG_MANAGED | SERIES_MASK_PARTIALS
    );

    REBACT *reordered = Make_Action(
        CTX_VARLIST(exemplar),
        partials,
        &Reorderer_Dispatcher,
        IDX_REORDERER_MAX
    );

    REBARR *details = ACT_DETAILS(reordered);
    Copy_Cell(DETAILS_AT(details, IDX_REORDERER_REORDEREE), ARG(action));

    return Init_Action(OUT, reordered, label, UNBOUND);
}
