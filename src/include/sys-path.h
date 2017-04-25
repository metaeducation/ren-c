//
//  File: %sys-path.h
//  Summary: "Definition of Structures for Path Processing"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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
// When a path like `a/(b + c)/d` is evaluated, it moves in steps.  The
// evaluative result of chaining the prior steps is offered as input to
// the next step.  The path evaluator `Do_Path_Throws` delegates steps to
// type-specific "(P)ath (D)ispatchers" with names like PD_Context,
// PD_Array, etc.
//
// R3-Alpha left several open questions about the handling of paths.  One
// of the trickiest regards the mechanics of how to use a SET-PATH! to
// write data into native structures when more than one path step is
// required.  For instance:
//
//     >> gob/size
//     == 10x20
//
//     >> gob/size/x: 304
//     >> gob/size
//     == 10x304
//
// Because GOB! stores its size as packed bits that are not a full PAIR!,
// the `gob/size` path dispatch can't give back a pointer to a REBVAL* to
// which later writes will update the GOB!.  It can only give back a
// temporary value built from its internal bits.  So workarounds are needed,
// as they are for a similar situation in trying to set values inside of
// C arrays in STRUCT!.
//
// The way the workaround works involves allowing a SET-PATH! to run forward
// and write into a temporary value.  Then in these cases the temporary
// REBVAL is observed and used to write back into the native bits before the
// SET-PATH! evaluation finishes.  This means that it's not currently
// prohibited for the effect of a SET-PATH! to be writing into a temporary.
//
// Further, the `value` slot is writable...even when it is inside of the path
// that is being dispatched:
//
//     >> code: compose [(make set-path! [12-Dec-2012 day]) 1]
//     == [12-Dec-2012/day: 1]
//
//     >> do code
//
//     >> probe code
//     [1-Dec-2012/day: 1]
//
// Ren-C has largely punted on resolving these particular questions in order
// to look at "more interesting" ones.  However, names and functions have
// been updated during investigation of what was being done.
//


//=////////////////////////////////////////////////////////////////////////=//
//
//  PATH VALUE STATE "PVS"
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The path value state structure is used by `Do_Path_Throws()` and passed
// to the dispatch routines.  See additional comments in %c-path.c.
//

struct Reb_Path_Value_State {
    //
    // `item` is the current element within the path that is being processed.
    // It is advanced as the path is consumed.
    //
    const RELVAL *item;

    // A specifier is needed because the PATH! is processed by incrementing
    // through values, which may be resident in an array that was part of
    // the cloning of a function body.  The specifier allows the path
    // evaluation to disambiguate which variable a word's relative binding
    // would match.
    //
    REBSPC *item_specifier;

    // `picker` is the result of evaluating the current path item if
    // necessary.  So if the path is `a/(1 + 2)` and processing the second
    // `item`, then the picker would be the computed value `3`.
    //
    // (This is what the individual path dispatchers should use.)
    //
    const REBVAL *picker;
    REBVAL picker_cell; // picker = &picker_cell (GC guarded value)

    // `value` holds the path value that should be chained from.  (It is the
    // type of `value` that dictates which dispatcher is given the `selector`
    // to get the next step.)  This has to be a relative value in order to
    // use the SET_IF_END option which writes into arrays.
    //
    RELVAL *value;

    // `value_specifier` has to be updated whenever value is updated
    //
    REBSPC *value_specifier;

    // `store` is the storage for constructed values, and also where any
    // thrown value will be written.
    //
    REBVAL *store;

    // `setval` is non-NULL if this is a SET-PATH!, and it is the value to
    // ultimately set the path to.  The set should only occur at the end
    // of the path, so most setters should check `IS_END(pvs->item + 1)`
    // before setting.
    //
    // !!! See notes at top of file about why the path dispatch is more
    // complicated than simply being able to only pass the setval to the last
    // item being dispatched (which would be cleaner, but some cases must
    // look ahead with alternate handling).
    //
    const REBVAL *opt_setval;

    // `orig` original path input, saved for error messages
    //
    const RELVAL *orig;

    // `label` is a concept that `obj/fun/refinement` would come back with
    // the symbol FUN to identify a function, for the stack trace.  This
    // idea throws away information and is a little sketchy, not to mention
    // that anonymous functions throw a wrench into it.  But it is roughly
    // what R3-Alpha did.
    //
    // !!! A better idea is probably to just temporarily lock the executing
    // path until the function is done running, and use the path itself as
    // the label.  This provides more information and doesn't require the
    // sketchy extraction logic.
    //
    REBSTR **label_out;
};


enum Path_Eval_Result {
    PE_OK, // pvs->value points to the element to take the next selector
    PE_SET_IF_END, // only sets if end of path
    PE_USE_STORE, // set pvs->value to be pvs->store
    PE_NONE // set pvs->store to NONE and then pvs->value to pvs->store
};

