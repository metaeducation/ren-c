//
//  file: %c-reorder.c
//  summary: "Function Generator for Reordering Parameters"
//  section: datatypes
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
//     >> append:series <item> [a b c]  ; use series parameter on 2nd pass
//     == [a b c <item>]
//
// But `get $append:dup:series` is not very intuitive for getting the order
// of [value dup series] (and gets more counterintuitive the more normal
// parameters a function has).
//

#include "sys-core.h"

enum {
    IDX_REORDERER_REORDEREE = 1,  // saves the function being reordered
    MAX_IDX_REORDERER = IDX_REORDERER_REORDEREE
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
    Details* details = Ensure_Level_Details(L);
    assert(Details_Max(details) == MAX_IDX_REORDERER);

    Value* reorderee = Details_At(details, IDX_REORDERER_REORDEREE);

    Tweak_Level_Phase(L, Frame_Phase(reorderee));
    Tweak_Level_Coupling(L, Cell_Frame_Coupling(reorderee));

    return BOUNCE_REDO_UNCHECKED;  // exemplar unchanged; known to be valid
}


//
//  Reorderer_Details_Querier: C
//
// All questions are forwarded to the reorderee.
//
bool Reorderer_Details_Querier(
    Sink(Value) out,
    Details* details,
    SymId property
){
    assert(Details_Dispatcher(details) == &Reorderer_Dispatcher);
    assert(Details_Max(details) == MAX_IDX_REORDERER);

    Value* reorderee = Details_At(details, IDX_REORDERER_REORDEREE);

    Details* reorderee_details = Phase_Details(Frame_Phase(reorderee));
    DetailsQuerier* querier = Details_Querier(reorderee_details);
    return (*querier)(out, reorderee_details, property);
}


//
//  reorder: native [
//
//  "Create variation of a frame with its parameters reordered"
//
//      return: [action!]
//      original [<unrun> frame!]
//      ordering "Parameter WORD!s, all required parameters must be mentioned"
//          [block!]
//  ]
//
DECLARE_NATIVE(REORDER)
{
    INCLUDE_PARAMS_OF_REORDER;

    // IMPORTANT: Binders use global state and code is not allowed to panic()
    // without cleaning the binder up first, balancing it all out to zeros.
    // Errors must be stored and reported after the cleanup.
    //
    Option(Error*) error = SUCCESS;

    Element* original = Element_ARG(ORIGINAL);
    Phase* reorderee = Frame_Phase(ARG(ORIGINAL));
    Option(const Symbol*) label  = Cell_Frame_Label_Deep(ARG(ORIGINAL));

    // Working with just the exemplar means we will lose the partials ordering
    // information from the interface.  But that's what we want, as the
    // caller is to specify a complete ordering.
    //
    ParamList* paramlist = Phase_Paramlist(reorderee);

    // We need a binder to efficiently map arguments to their position in
    // the parameters array, and track which parameters are mentioned.

    DECLARE_BINDER (binder);
    Construct_Binder(binder);

  add_binder_indices: {

    const Key* tail;
    const Key* key = Phase_Keys(&tail, reorderee);
    const Param* param = Phase_Params_Head(reorderee);
    REBLEN index = 1;
    for (; key != tail; ++key, ++param, ++index) {
        if (Is_Specialized(param))
            continue;
        Add_Binder_Index(binder, Key_Symbol(key), index);
    }

} use_binder: {

    // We proceed through the list, and remove the binder indices as we go.
    // This lets us check for double uses or use of words that aren't in the
    // spec, and a final pass can check to make sure all mandatory parameters
    // have been spoken for in the order.
    //
    // We iterate backwards, because that's the stack order that needs to
    // be pushed.
    //
    const Element* item;  // starts as tail
    const Element* at = List_At(&item, ARG(ORDERING));
    for (; at != item--; ) {
        const Symbol* symbol;

        // !!! As a bit of a weird demo of a potential future direction, we
        // don't just allow WORD!s but allow you to do things like pass the
        // full `parameters of`, e.g. reversed.
        //
        bool ignore = false;
        if (Any_Word(item)) {  // on the record, we only just allow WORD!...
            symbol = Word_Symbol(item);
        }
        else if (Is_Refinement(item)) {
            symbol = Cell_Refinement_Symbol(item);
            ignore = true;  // to use a refinement, don't /refine it
        }
        else if (Is_Quoted(item)) {
            if (
                Quotes_Of(item) != 1
                or Heart_Of(item) != TYPE_WORD
            ) {
                error = Error_User("REORDER allows single quoted ANY-WORD?");
                goto cleanup_binder;
            }
            symbol = Word_Symbol(item);
        }
        else {
            error = Error_User("Unknown REORDER element");
            goto cleanup_binder;
        }

        REBINT index = maybe Try_Get_Binder_Index(binder, symbol);
        if (index <= 0) {
            error = Error_Bad_Parameter_Raw(item);
            goto cleanup_binder;
        }

        Update_Binder_Index(binder, symbol, -1);

        if (ignore)
            continue;

        const Value* param = Phase_Param(reorderee, index);
        if (Get_Parameter_Flag(param, REFINEMENT) and Is_Parameter_Unconstrained(param)) {
            error = Error_User("Can't reorder refinements with no argument");
            goto cleanup_binder;
        }

        Init_Word_Bound(PUSH(), symbol, paramlist);
        Tweak_Word_Index(TOP_ELEMENT, index);
    }

    // Make sure that all parameters that were mandatory got a place in the
    // ordering list.

} cleanup_binder: {

    const Key* tail;
    const Key* key = Phase_Keys(&tail, reorderee);
    const Param* param = Phase_Params_Head(reorderee);
    REBLEN index = 1;
    for (; key != tail; ++key, ++param, ++index) {
        if (Is_Specialized(param))
            continue;

        const Symbol* symbol = Key_Symbol(key);

        // If we saw the parameter, we set its index to -1.
        //
        bool mentioned = (-1 == Try_Get_Binder_Index(binder, symbol));

        if (
            not error  // don't report an error here if one is pending
            and not mentioned
            and Not_Parameter_Flag(param, REFINEMENT)  // okay to leave out
        ){
            error = Error_No_Arg(label, Key_Symbol(key));
        }
    }

} destruct_binder: {

    Destruct_Binder(binder);

    if (error)  // *now* it's safe to panic...
        return PANIC(unwrap error);

    Details* details = Make_Dispatch_Details(
        BASE_FLAG_MANAGED,
        Phase_Archetype(paramlist),
        &Reorderer_Dispatcher,
        MAX_IDX_REORDERER
    );

    Copy_Cell(Details_At(details, IDX_REORDERER_REORDEREE), original);

    Drop_Data_Stack_To(STACK_BASE);  // !!! None of this works ATM.

    Init_Action(OUT, details, label, NONMETHOD);
    return UNSURPRISING(OUT);
}}
