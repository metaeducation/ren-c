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
Bounce Reorderer_Dispatcher(Level* L) {
    Details* details = Phase_Details(Level_Phase(L));
    assert(Array_Len(details) == IDX_REORDERER_MAX);

    Value* reorderee = Details_At(details, IDX_REORDERER_REORDEREE);

    INIT_LVL_PHASE(L, ACT_IDENTITY(VAL_ACTION(reorderee)));
    INIT_LVL_BINDING(L, VAL_FRAME_BINDING(reorderee));

    return BOUNCE_REDO_UNCHECKED;  // exemplar unchanged; known to be valid
}


//
//  reorder: native [
//
//  "Create variation of a frame with its parameters reordered"
//
//      return: [action?]
//      original [<unrun> frame!]
//      ordering "Parameter WORD!s, all required parameters must be mentioned"
//          [block!]
//  ]
//
DECLARE_NATIVE(reorder)
{
    INCLUDE_PARAMS_OF_REORDER;

    Action* reorderee = VAL_ACTION(ARG(original));
    Option(const Symbol*) label  = VAL_FRAME_LABEL(ARG(original));

    // Working with just the exemplar means we will lose the partials ordering
    // information from the interface.  But that's what we want, as the
    // caller is to specify a complete ordering.
    //
    Context* exemplar = ACT_EXEMPLAR(reorderee);

    // We need a binder to efficiently map arguments to their position in
    // the parameters array, and track which parameters are mentioned.

    struct Reb_Binder binder;
    INIT_BINDER(&binder);

  blockscope {
    const Key* tail;
    const Key* key = ACT_KEYS(&tail, reorderee);
    const Param* param = ACT_PARAMS_HEAD(reorderee);
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
    Option(Context*) error = nullptr;

    StackIndex base = TOP_INDEX;

    // We proceed through the array, and remove the binder indices as we go.
    // This lets us check for double uses or use of words that aren't in the
    // spec, and a final pass can check to make sure all mandatory parameters
    // have been spoken for in the order.
    //
    // We iterate backwards, because that's the stack order that needs to
    // be pushed.
    //
    const Element* item;  // starts as tail
    const Element* at = Cell_Array_At(&item, ARG(ordering));
    for (; at != item--; ) {
        const Symbol* symbol = Cell_Word_Symbol(item);

        // !!! As a bit of a weird demo of a potential future direction, we
        // don't just allow WORD!s but allow you to do things like pass the
        // full `parameters of`, e.g. reversed.
        //
        bool ignore = false;
        if (Any_Word(item)) {  // on the record, we only just allow WORD!...
            symbol = Cell_Word_Symbol(item);
        }
        else if (Is_Refinement(item)) {
            symbol = VAL_REFINEMENT_SYMBOL(item);
            ignore = true;  // to use a refinement, don't /refine it
        }
        else if (Is_Quoted(item)) {
            if (
                Cell_Num_Quotes(item) != 1
                or not Any_Word_Kind(Cell_Heart(item))
            ) {
                error = Error_User("REORDER allows single quoted ANY-WORD?");
                goto cleanup_binder;
            }
            symbol = Cell_Word_Symbol(item);
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

        const Value* param = ACT_PARAM(reorderee, index);
        if (Get_Parameter_Flag(param, REFINEMENT) and Is_Parameter_Unconstrained(param)) {
            error = Error_User("Can't reorder refinements with no argument");
            goto cleanup_binder;
        }

        Init_Any_Word_Bound(PUSH(), REB_WORD, symbol, exemplar, index);
    }

    // Make sure that all parameters that were mandatory got a place in the
    // ordering list.

  cleanup_binder: {
    const Key* tail;
    const Key* key = ACT_KEYS(&tail, reorderee);
    const Param* param = ACT_PARAMS_HEAD(reorderee);
    REBLEN index = 1;
    for (; key != tail; ++key, ++param, ++index) {
        if (Is_Specialized(param))
            continue;

        const Symbol* symbol = KEY_SYMBOL(key);

        // If we saw the parameter, we removed its index from the binder.
        //
        bool mentioned = (0 == Remove_Binder_Index_Else_0(&binder, symbol));

        if (
            not error  // don't report an error here if one is pending
            and not mentioned
            and Not_Parameter_Flag(param, REFINEMENT)  // okay to leave out
        ){
            error = Error_No_Arg(label, KEY_SYMBOL(key));
        }
    }
  }

    SHUTDOWN_BINDER(&binder);

    if (error)  // *now* it's safe to fail...
        fail (unwrap(error));

    Array* partials = Pop_Stack_Values_Core(
        base,
        NODE_FLAG_MANAGED | SERIES_MASK_PARTIALS
    );

    Phase* reordered = Make_Action(
        CTX_VARLIST(exemplar),
        partials,
        &Reorderer_Dispatcher,
        IDX_REORDERER_MAX
    );

    Details* details = Phase_Details(reordered);
    Copy_Cell(Details_At(details, IDX_REORDERER_REORDEREE), ARG(original));

    return Init_Action(OUT, reordered, label, UNBOUND);
}
