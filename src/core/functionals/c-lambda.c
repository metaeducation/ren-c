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
//     >> if ok [10] then x -> [print ["The branch produced" x]]
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
//   and a keylist (which may be optimizable to just a String* in the single
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
Bounce Lambda_Dispatcher(Level* const L)
//
// 1. We have to use Make_Use_Core() here, because it could be the case
//    that a higher level wrapper used the frame and virtually bound it.
{
    USE_LEVEL_SHORTHANDS (L);

    Details* details = Phase_Details(PHASE);
    assert(Array_Len(details) == IDX_LAMBDA_MAX);

    const Value* block = Details_At(details, IDX_LAMBDA_BLOCK);
    assert(Is_Block(block));

    Force_Level_Varlist_Managed(L);

    Context* parent = Cell_List_Binding(block);
    Use* use = Make_Use_Core(  // have to USE here
        Varlist_Archetype(Varlist_Of_Level_Force_Managed(L)),
        parent,
        REB_WORD
    );

    Copy_Cell(SPARE, block);
    BINDING(SPARE) = use;

    return DELEGATE_CORE(
        OUT,
        LEVEL_MASK_NONE,
        SPECIFIED,
        stable_SPARE
    );
}


//
//  Lambda_Unoptimized_Dispatcher: C
//
// Used by LAMBDA when it can't use the optimized form.  This runs very much
// like function dispatch, except there's no RETURN to catch.  So it can
// execute directly into the output cell.
//
Bounce Lambda_Unoptimized_Dispatcher(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    Details* details = Phase_Details(PHASE);
    Value* body = Details_At(details, IDX_DETAILS_1);  // code to run
    assert(Is_Block(body) and VAL_INDEX(body) == 0);

    Force_Level_Varlist_Managed(L);

    Copy_Cell(SPARE, body);
    node_LINK(NextVirtual, L->varlist) = Cell_List_Binding(body);
    BINDING(SPARE) = L->varlist;

    return DELEGATE_CORE(
        OUT,  // output
        LEVEL_MASK_NONE,  // flags
        SPECIFIED,  // branch binding
        stable_SPARE  // branch
    );
}


//
//  lambda: native [
//
//  "Makes an anonymous function that evaluates to its body, and has no RETURN"
//
//      return: [action?]
//      spec "Names of arguments (will not be type checked)"
//          [blank! word! lit-word? meta-word! refinement? block!]
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

    Value* spec = ARG(spec);
    Element* body = cast(Element*, ARG(body));

    bool optimizable = true;

    const Element* item_tail;
    const Element* item;
    if (Is_Block(spec)) {
        item = Cell_List_At(&item_tail, spec);
    }
    else if (
        Is_Word(spec)
        or Is_Get_Word(spec)
        or Is_Meta_Word(spec)
        or Is_Quoted(spec)
        or (Is_Path(spec) and Is_Refinement(spec))
    ){
        item = cast(Element*, spec);
        item_tail = item + 1;
    }
    else {
        assert(Is_Blank(spec));
        item = nullptr;
        item_tail = nullptr;
    }

    for (; item != item_tail; ++item) {
        Flags param_flags = 0;
        ParamClass pclass;
        const Symbol* symbol;
        if (Is_Word(item)) {
            pclass = PARAMCLASS_NORMAL;
            symbol = Cell_Word_Symbol(item);
        }
        else if (Is_Meta_Word(item)) {
            pclass = PARAMCLASS_META;
            symbol = Cell_Word_Symbol(item);
        }
        else if (Is_Get_Word(item)) {
            fail ("SOFT parameter quoting no longer necessary");
        }
        else if (Is_Quoted(item)) {
            if (Cell_Num_Quotes(item) != 1)
                fail (item);
            if (Cell_Heart(item) == REB_WORD)
                pclass = PARAMCLASS_JUST;
            else if (Cell_Heart(item) == REB_GET_WORD)
                pclass = PARAMCLASS_SOFT;
            else
                fail (item);
            symbol = Cell_Word_Symbol(item);
        }
        else if (Is_The_Word(item)) {
            pclass = PARAMCLASS_THE;
            symbol = Cell_Word_Symbol(item);
        }
        else if (Is_Path(item) and Is_Refinement(item)) {
            pclass = PARAMCLASS_NORMAL;
            symbol = VAL_REFINEMENT_SYMBOL(item);
            param_flags |= PARAMETER_FLAG_REFINEMENT;
            param_flags |= PARAMETER_FLAG_NULL_DEFINITELY_OK;
        }
        else if (Is_Set_Word(item) and Cell_Word_Id(item) == SYM_RETURN) {
            fail ("LAMBDA (->) does not offer RETURN facilities, use FUNCTION");
        }
        else {
            if (not Is_Block(spec))
                fail ("Invalid LAMBDA specification");

            optimizable = false;
            Drop_Data_Stack_To(STACK_BASE);
            break;
        }

        Init_Word(PUSH(), symbol);
        Init_Unconstrained_Parameter(
            PUSH(),
            FLAG_PARAMCLASS_BYTE(pclass) | param_flags
        );
    }

    if (not optimizable) {
        Phase* lambda = Make_Interpreted_Action_May_Fail(
            cast(Element*, spec),
            body,
            MKF_MASK_NONE,  // no MKF_RETURN
            &Lambda_Unoptimized_Dispatcher,
            1 + IDX_DETAILS_1  // archetype and one array slot (will be filled)
        );

        return Init_Action(OUT, lambda, ANONYMOUS, UNBOUND);
    }

    VarList* adjunct;  // reuses Pop_Paramlist() [1]
    Array* paramlist = Pop_Paramlist_With_Adjunct_May_Fail(
        &adjunct,
        STACK_BASE,
        MKF_MASK_NONE,
        0  // no return_stackindex
    );

    Phase* lambda = Make_Action(
        paramlist,
        nullptr,  // no partials
        &Lambda_Dispatcher,
        IDX_LAMBDA_MAX  // same as specialization, just 1 (for archetype)
    );

    assert(ACT_ADJUNCT(lambda) == nullptr);

    Details* details = Phase_Details(lambda);
    Copy_Cell(Array_At(details, IDX_LAMBDA_BLOCK), body);

    return Init_Action(OUT, lambda, ANONYMOUS, UNBOUND);
}
