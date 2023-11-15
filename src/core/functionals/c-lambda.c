//
//  File: %c-lambda.c
//  Summary: "Low-overhead virtual binding ACTION! with no Typecheck/RETURN"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2021-2022 Ren-C Open Source Contributors
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
// LAMBDA is an efficient form of ACTION! which has no RETURN, has no type
// checking, and which does not copy the block that serves as its body.  It
// is enfixed as `->` and is intended for uses like light branching.
//
//     >> if true [10] then x -> [print ["The branch produced" x]]
//     The branch produced 10
//
// The implementation is essentially like `does [use 'x [...]]`, but it's
// made as an optimized generator to drive the cost down in uses like the
// branch above.
//
//=//// NOTES ////////////////////////////////////////////////////////////=//
//
// * Though lighter-weight than a plain FUNC, lambdas still have to pay for
//   a DETAILS array (minimum length 2), a paramlist (also minimum length 2),
//   and a keylist (which may be optimizable to just a String(*) in the single
//   variable case...which is in the works).  Further optimizations would
//   need to weigh in the question of how AS FRAME! SOME-LAMBDA could work,
//   and if it would be worth it in the scheme of things.
//
// * Invisibility is allowed in lambda, so `x -> []` is void
//

#include "sys-core.h"

enum {
    IDX_LAMBDA_BLOCK = 1,
    IDX_LAMBDA_MAX
};


//
//  Lambda_Dispatcher: C
//
// Uses virtual binding (essentially like a USE)
//
// !!! Review if this should be unified with the Block_Dispatcher() of DOES.
// It could detect the case of no parameters to the frame, and thus avoid
// doing any virtual binding.  However, there's some difference w.r.t. the
// "derived binding" that need a going-over.
//
Bounce Lambda_Dispatcher(Level(*) const L)
//
// 1. We have to use Make_Or_Reuse_Use() here, because it could be the case
//    that a higher level wrapper used the frame and virtually bound it.
//
// 2. Currently, since we are evaluating the block with its own virtual
//    binding being taken into account, using that block's binding as the
//    `next` (VAL_SPECIFIER(block)) means it's redundant when creating the
//    feed, since it tries to apply this specifier on top of that *again*.
//    The merging notices the redundancy and doesn't create a new specifier
//    which is good...but this is still inefficient.  This all needs review.
{
    USE_LEVEL_SHORTHANDS (L);

    Details(*) details = Phase_Details(PHASE);
    assert(Array_Len(details) == IDX_LAMBDA_MAX);

    const REBVAL *block = DETAILS_AT(details, IDX_LAMBDA_BLOCK);
    assert(IS_BLOCK(block));

    Set_Series_Flag(L->varlist, MANAGED);  // not manually tracked...

    REBSPC *specifier = Make_Or_Reuse_Use(  // may reuse, see [1]
        CTX(L->varlist),
        VAL_SPECIFIER(block),  // redundant with feed, see [2]
        REB_WORD
    );

    return DELEGATE_CORE(
        OUT,
        LEVEL_MASK_NONE,
        specifier,  // block's specifier
        block
    );
}


//
//  Lambda_Unoptimized_Dispatcher: C
//
// Used by LAMBDA when it can't use the optimized form.  This runs very much
// like function dispatch, except there's no RETURN to catch.  So it can
// execute directly into the output cell.
//
Bounce Lambda_Unoptimized_Dispatcher(Level(*) level_)
{
    Details(*) details = Phase_Details(PHASE);
    Cell(*) body = Array_At(details, IDX_DETAILS_1);  // code to run
    assert(IS_BLOCK(body) and IS_RELATIVE(body) and VAL_INDEX(body) == 0);

    return DELEGATE_CORE(
        OUT,  // output
        LEVEL_MASK_NONE,  // flags
        SPC(LEVEL->varlist),  // branch specifier
        body  // branch
    );
}


//
//  lambda: native [
//
//  {Makes an anonymous function that evaluates to its body, and has no RETURN}
//
//      return: [activation?]
//      spec "Names of arguments (will not be type checked)"
//          [<opt> word! lit-word! meta-word! refinement! block!]
//      body "Code to execute"
//          [<const> block!]
//  ]
//
DECLARE_NATIVE(lambda)
//
// 1. For the moment, this lazily reuses Pop_Paramlist(), just because that
//    code is a vetted path.  It could be done much more efficiently, but at
//    the risk of getting the incantation wrong.  Optimize later if needed.
{
    INCLUDE_PARAMS_OF_LAMBDA;

    REBVAL *spec = ARG(spec);
    REBVAL *body = ARG(body);

    bool optimizable = true;

    REBSPC *item_specifier;
    Cell(const*) item_tail;
    Cell(const*) item;
    if (IS_BLOCK(spec)) {
        item = VAL_ARRAY_AT(&item_tail, spec);
        item_specifier = VAL_SPECIFIER(spec);
    }
    else if (
        IS_WORD(spec)
        or IS_GET_WORD(spec)
        or IS_META_WORD(spec)
        or Is_Quoted(spec)
        or (IS_PATH(spec) and IS_REFINEMENT(spec))
    ){
        item = spec;
        item_specifier = SPECIFIED;
        item_tail = item + 1;
    }
    else {
        assert(Is_Nulled(spec));
        item_specifier = SPECIFIED;
        item = nullptr;
        item_tail = nullptr;
    }

    PUSH_SLOTS();

    Init_Word_Isotope(KEY_SLOT(TOP_INDEX), Canon(KEY));
    Init_Trash(PARAM_SLOT(TOP_INDEX));  // unused
    Init_Trash(TYPES_SLOT(TOP_INDEX));  // unused
    Init_Nulled(NOTES_SLOT(TOP_INDEX));  // overwritten if description

    for (; item != item_tail; ++item) {
        PUSH_SLOTS();

        Value(*) key_slot = KEY_SLOT(TOP_INDEX);
        Derelativize(key_slot, item, item_specifier);

        // First in quad needs to be a WORD!, after pclass extracted...
        //
        Flags param_flags = 0;
        enum Reb_Param_Class pclass;
        if (IS_WORD(key_slot))
            pclass = PARAM_CLASS_NORMAL;
        else if (IS_META_WORD(key_slot)) {
            pclass = PARAM_CLASS_META;
            mutable_HEART_BYTE(key_slot) = REB_WORD;
        }
        else if (IS_GET_WORD(key_slot)) {
            pclass = PARAM_CLASS_SOFT;
            mutable_HEART_BYTE(key_slot) = REB_WORD;
        }
        else if (Is_Quoted(key_slot)) {
            Unquotify(key_slot, 1);
            if (not IS_WORD(key_slot))
                fail (item);
            pclass = PARAM_CLASS_HARD;
        }
        else if (IS_PATH(key_slot) and IS_REFINEMENT(key_slot)) {
            pclass = PARAM_CLASS_NORMAL;
            Symbol(const*) symbol = VAL_REFINEMENT_SYMBOL(key_slot);
            Init_Word(key_slot, symbol);
            param_flags |= PARAM_FLAG_REFINEMENT;
        }
        else if (IS_SET_WORD(item) and VAL_WORD_ID(item) == SYM_RETURN) {
            fail ("LAMBDA (->) does not offer RETURN facilities, use FUNCTION");
        }
        else {
            if (not IS_BLOCK(spec))
                fail ("Invalid LAMBDA specification");

            optimizable = false;
            continue;
        }

        Init_Param(
            PARAM_SLOT(TOP_INDEX),
            pclass | param_flags,
            nullptr
        );

        Init_Nulled(TYPES_SLOT(TOP_INDEX));  // types (not supported)
        Init_Nulled(NOTES_SLOT(TOP_INDEX));  // notes (not supported)
    }

    if (not optimizable) {
        Drop_Data_Stack_To(STACK_BASE);

        Phase(*) lambda = Make_Interpreted_Action_May_Fail(
            spec,
            body,
            MKF_KEYWORDS,  // no MKF_RETURN
            &Lambda_Unoptimized_Dispatcher,
            1 + IDX_DETAILS_1  // archetype and one array slot (will be filled)
        );

        return Init_Activation(OUT, lambda, ANONYMOUS, UNBOUND);
    }

    Context(*) adjunct;  // reuses Pop_Paramlist(), see [1]
    Array(*) paramlist = Pop_Paramlist_With_Adjunct_May_Fail(
        &adjunct,
        STACK_BASE,
        MKF_KEYWORDS,
        0  // no return_stackindex
    );

    Phase(*) lambda = Make_Action(
        paramlist,
        nullptr,  // no partials
        &Lambda_Dispatcher,
        IDX_LAMBDA_MAX  // same as specialization, just 1 (for archetype)
    );

    assert(ACT_ADJUNCT(lambda) == nullptr);

    Details(*) details = Phase_Details(lambda);
    Copy_Cell(Array_At(details, IDX_LAMBDA_BLOCK), body);

    return Init_Activation(OUT, lambda, ANONYMOUS, UNBOUND);
}
