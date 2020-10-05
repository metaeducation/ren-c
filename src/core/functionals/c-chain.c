//
//  File: %c-chain.c
//  Summary: "Function generator for making a pipeline of post-processing"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2020 Ren-C Open Source Contributors
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
// CHAIN is used to create a function that matches the interface of a "first"
// function, and then pipes its results through to several subsequent
// post-processing actions:
//
//     >> negadd: chain [:add | :negate]
//
//     >> negadd 2 2
//     == -4
//
// Its design is relatively efficient, because it is supported by the action
// executor directly--which looks for ACTION!s pushed to the stack by
// dispatchers and assumes that means the calls should be chained.
//
// For more complex post-processing which may involve access to the original
// inputs to the first function (or other memory in the process), consider
// using ENCLOSE...which is less efficient, but more powerful.
//
// !!! CHAIN is one of the oldest function derivations, and has not been
// revisited much in its design--e.g. to support multiple return values.
//

#include "sys-core.h"

enum {
    IDX_CHAINER_PIPELINE = 0,  // Chain of functions to execute
    IDX_CHAINER_MAX
};


//
//  Chainer_Dispatcher: C
//
// When a function created with CHAIN is invoked, this dispatcher is run.
//
REB_R Chainer_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == IDX_CHAINER_MAX);

    const REBARR *pipeline = VAL_ARRAY(ARR_AT(details, IDX_CHAINER_PIPELINE));

    // The post-processing pipeline has to be "pushed" so it is not forgotten.
    // Go in reverse order, so the function to apply last is at the bottom of
    // the stack.
    //
    const RELVAL *chained = ARR_LAST(pipeline);
    for (; chained != ARR_HEAD(pipeline); --chained) {
        assert(IS_ACTION(chained));
        Move_Value(DS_PUSH(), SPECIFIC(chained));
    }

    // Extract the first function, itself which might be a chain.
    //
    INIT_FRM_PHASE(f, VAL_ACTION(chained));
    FRM_BINDING(f) = VAL_BINDING(chained);

    return R_REDO_UNCHECKED;  // signatures should match
}


//
//  chain*: native [
//
//  {Create a processing pipeline of actions, each consuming the last result}
//
//      return: [action!]
//      pipeline "Block of ACTION!s to apply (will be LOCKed)"
//          [block!]
//  ]
//
REBNATIVE(chain_p)  // see extended definition CHAIN in %base-defs.r
{
    INCLUDE_PARAMS_OF_CHAIN_P;

    REBVAL *out = D_OUT;  // plan ahead for factoring into Chain_Action(out..

    REBVAL *pipeline = ARG(pipeline);
    const RELVAL *first = VAL_ARRAY_AT(pipeline);

    const REBSTR *opt_label = VAL_ACTION_OPT_LABEL(first);  // inherit

    // !!! Current validation is that all are actions.  Should there be other
    // checks?  (That inputs match outputs in the chain?)  Should it be
    // a dialect and allow things other than functions?
    //
    const RELVAL *check = first;
    for (; NOT_END(check); ++check) {
        if (not IS_ACTION(check)) {
            DECLARE_LOCAL (specific);
            Derelativize(specific, check, VAL_SPECIFIER(pipeline));
            fail (specific);
        }
    }

    REBARR *paramlist = Copy_Array_Shallow_Flags(
        VAL_ACT_PARAMLIST(first),  // same interface as head of the chain
        SPECIFIED,
        SERIES_MASK_PARAMLIST | NODE_FLAG_MANAGED  // flags not auto-copied
    );
    Sync_Paramlist_Archetype(paramlist);  // [0] cell must hold copied pointer
    MISC_META_NODE(paramlist) = nullptr;  // defaults to being trash

    REBACT *chain = Make_Action(
        paramlist,
        &Chainer_Dispatcher,
        ACT_UNDERLYING(VAL_ACTION(first)),  // same underlying as first action
        ACT_EXEMPLAR(VAL_ACTION(first)),  // same exemplar as first action
        IDX_CHAINER_MAX  // details array capacity
    );
    Force_Value_Frozen_Deep(pipeline);
    Move_Value(ARR_AT(ACT_DETAILS(chain), IDX_CHAINER_PIPELINE), pipeline);

    Init_Action_Unbound(out, chain);
    if (opt_label)
        INIT_ACTION_LABEL(out, opt_label);

    return out;
}


//
//  Cache_Predicate_Throws: C
//
// The concept of a "predicate" is like a CHAIN, where the functions are
// in a TUPLE! and applied in order (so the reverse of the BLOCK! formulation
// that was initially implemented).  The TUPLE! is headed with a BLANK!, so
// it appears to start with a dot.
//
// Predicates may be simple -- an already-existing ACTION!, like `.try` -- or
// they may be compositions, like `.not.equal?` -- and since GROUP!s are legal
// in TUPLE!, you get a generic escape with `.(func [x] [...])`
//
// When a native starts up, it doesn't want to recalculate the predicate
// each time it is called...so it caches it.
//
// !!! User functions may want predicates to have this processing easily
// also, so `<predicate>` may be an argument tag in the future.
//
bool Cache_Predicate_Throws(
    REBVAL *out,  // if a throw, it is written here
    REBVAL *predicate  // updated to be the ACTION! (or WORD!-invoking action)
){
    if (IS_NULLED(predicate))  // function uses default (IS_TRUTHY(), .equal?)
        return false;

    if (IS_ACTION(predicate))  // already an action (e.g. passed via APPLY)
        return false;

    assert(IS_TUPLE(predicate));

    REBARR *items;

    // TUPLE!s are ANY-SEQUENCE! and have compressed implementation forms.
    // See %sys-sequence.h for more information.  We want optimized handling
    // of the `.foo` form (with "heart" as a GET-WORD!, not a BLOCK!).
    //
    switch (MIRROR_BYTE(predicate)) {
      case REB_GET_WORD: {  // representation of a `.foo` single-WORD! form
        const REBSTR *label = VAL_WORD_SPELLING(predicate);
        Move_Value(predicate, Lookup_Word_May_Fail(predicate, SPECIFIED));
        if (not IS_ACTION(predicate))
            fail ("PREDICATE must look up to an ACTION!");
        INIT_ACTION_LABEL(predicate, label);
        return false; }

      case REB_BLOCK:
        items = ARR(VAL_NODE(predicate));
        if (not IS_BLANK(ARR_HEAD(items)))
            goto error_not_blank_head;
        break;  // fall through to array handling

      default:
      error_not_blank_head:
        fail ("PREDICATE! must start with `.` (BLANK! in first position)");
    }

    // To turn this into input that CHAIN* understands, every step needs to
    // be an ACTION!.  We avoid re-implementing REDUCE here, and instead
    // just make a reversed array of GET-items to pass to it.

    REBLEN len = ARR_LEN(items);
    REBSPC *specifier = VAL_SEQUENCE_SPECIFIER(predicate);

    REBARR *reversed = Make_Array(len - 1);

    RELVAL *dst = ARR_HEAD(reversed);
    RELVAL *src = ARR_AT(items, len - 1);
    REBLEN i;
    for (i = 1; i < len; ++i, --src, ++dst) {
        if (not IS_WORD(src) and not IS_GROUP(src))
            fail ("Predicate elements can only be WORD! or GROUP!");
        Getify(Derelativize(dst, src, specifier));
    }

    TERM_ARRAY_LEN(reversed, len - 1);
    Init_Block(out, reversed);

    REBVAL *chain = rebValue(
        NATIVE_VAL(chain_p), NATIVE_VAL(reduce), out,
    rebEND);

    Move_Value(predicate, chain);
    rebRelease(chain);

    UNUSED(out);
    return false;
}
