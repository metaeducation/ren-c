//
//  File: %c-arrow.c
//  Summary: "Lambda Variation That Doesn't Deep Copy Body, Can Unpack Args "
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2021-2024 Ren-C Open Source Contributors
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
// ARROW is a variant of LAMBDA that is optimized for light branching.
// It is infixed as `->`, where the argument is taken literally...permitting
// plain WORD! to be used as the argument:
//
//     >> if ok [10] then x -> [print ["The branch produced" x]]
//     The branch produced 10
//
// While a BLOCK! of arguments can be used to gather multiple arguments, you
// can also use a quasiform of block to unpack the arguments:
//
//     case [
//         true [pack [10 + 20, 3 + 4]]  ; makes antiform ~['30 '7]~
//         ...
//     ] then ~[a b]~ -> [
//         assert [a = 30, b = 7]
//     ]
//
// (NOTE: This feature is TBD, and the above example is a placeholder.)
//
// Due to branching being the primary application, it would be inefficient
// to do deep copies of the body block.  So the body block is not copied.
// This gives it different semantics from LAMBDA and FUNC:
//
//     >> block: [print ["Hello" x]]
//
//     >> one: x -> block
//
//     >> append block spread [print ["Goodbye" x]]
//
//     >> two: x -> block
//
//     >> one 1020
//     Hello 1020
//     Goodbye 1020
//
//     >> two 1020
//     Hello 1020
//     Goodbye 1020
//
//=//// NOTES ////////////////////////////////////////////////////////////=//
//
// * The specific details of how (if condition [...] then x -> [...]) works
//   are rather complex, and is one of the more nuanced points of evaluation:
//
//     https://forum.rebol.info/t/the-most-vexing-evaluation/1361
//
// * Though lighter-weight than a plain FUNC, lambdas still have to pay for
//   a DETAILS array (minimum length 2), a paramlist (also minimum length 2),
//   and a keylist (which may be optimizable to just a Symbol* in the single
//   variable case...which is in the works).  Further optimizations would
//   need to weigh in the question of how AS FRAME! SOME-LAMBDA could work,
//   and if it would be worth it in the scheme of things.
//
// * Invisibility is allowed in lambda, so `x -> []` is void
//

#include "sys-core.h"

enum {
    IDX_ARROW_BODY = 1,  // note this doesn't use IDX_INTERPRETED_BODY
    MAX_IDX_ARROW = IDX_ARROW_BODY
};


//
//  Arrow_Dispatcher: C
//
// Uses virtual binding, no prior relativized walk.
//
Bounce Arrow_Dispatcher(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    Details* details = Ensure_Level_Details(L);
    assert(Details_Max(details) == MAX_IDX_ARROW);

    const Element* block = cast(Element*, Details_At(details, IDX_ARROW_BODY));
    assert(Is_Block(block));

    assert(Link_Inherit_Bind(L->varlist) == nullptr);
    Add_Link_Inherit_Bind(L->varlist, Cell_List_Binding(block));
    Force_Level_Varlist_Managed(L);

    Element* block_rebound = Copy_Cell(SPARE, block);
    Tweak_Cell_Binding(SPARE, L->varlist);

    return DELEGATE_CORE(
        OUT,
        LEVEL_MASK_NONE,
        SPECIFIED,
        block_rebound
    );
}


//
//  Arrow_Details_Querier: C
//
bool Arrow_Details_Querier(
    Sink(Value) out,
    Details* details,
    SymId property
){
    assert(Details_Dispatcher(details) == &Arrow_Dispatcher);
    assert(Details_Max(details) == MAX_IDX_ARROW);

    switch (property) {
      case SYM_RETURN_OF:
        Init_Nulled(out);  // unconstrained parameter, instead?
        return true;

      case SYM_BODY_OF: {
        Copy_Cell(out, Details_At(details, IDX_ARROW_BODY));
        assert(Is_Block(out));  // not relativized...give back mutable?
        return true; }

      default:
        break;
    }

    return false;
}


//
//  arrow: native [
//
//  "Makes an anonymous function that doesn't copy its body, can unpack args"
//
//      return: [action!]
//      spec "Names of arguments"
//          [blank! word! lit-word? meta-word! refinement? block!]
//      body "Code to execute"
//          [<const> block!]
//  ]
//
DECLARE_NATIVE(ARROW)
{
    INCLUDE_PARAMS_OF_ARROW;

    Element* spec = Element_ARG(SPEC);
    Element* body = Element_ARG(BODY);

    bool optimizable = true;

  //=//// TRY TO OPTIMIZE FOR SIMPLE CASES ////////////////////////////////=//

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
        else if (Is_Quoted(item)) {
            if (Quotes_Of(item) != 1)
                return FAIL(item);
            if (Heart_Of(item) == TYPE_WORD)
                pclass = PARAMCLASS_JUST;
            else
                return FAIL(item);
            symbol = Cell_Word_Symbol(item);
        }
        else if (Is_The_Word(item)) {
            pclass = PARAMCLASS_THE;
            symbol = Cell_Word_Symbol(item);
        }
        else if (Is_Refinement(item)) {
            pclass = PARAMCLASS_NORMAL;
            symbol = Cell_Refinement_Symbol(item);
            param_flags |= PARAMETER_FLAG_REFINEMENT;
            param_flags |= PARAMETER_FLAG_NULL_DEFINITELY_OK;
        }
        else if (Is_Set_Word(item) and Cell_Word_Id(item) == SYM_RETURN) {
            return FAIL(
                "ARROW (->) does not offer RETURN facilities, use FUNCTION"
            );
        }
        else {
            if (not Is_Block(spec))
                return FAIL("Invalid ARROW specification");

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

    VarList* adjunct = nullptr;

  //=//// IF NOT OPTIMIZABLE, USE THE FULL PARAMLIST PROCESS //////////////=//

    if (not optimizable) {
        Option(Error*) e = Trap_Push_Keys_And_Params(
            &adjunct,
            spec,
            MKF_MASK_NONE,
            SYM_0  // no returner
        );
        if (e)
            return FAIL(unwrap e);
    }

  //=//// POP THE PARAMLIST AND MAKE THE DETAILS PHASE ////////////////////=//

    Option(Phase*) prior = nullptr;
    Option(VarList*) prior_coupling = nullptr;

    ParamList* paramlist;
    Option(Error*) e =  Trap_Pop_Paramlist(
        &paramlist, STACK_BASE, prior, prior_coupling
    );
    if (e)
        return FAIL(unwrap e);

    Details* details = Make_Dispatch_Details(
        DETAILS_FLAG_OWNS_PARAMLIST,
        Phase_Archetype(paramlist),
        &Lambda_Dispatcher,
        MAX_IDX_ARROW
    );

    assert(Misc_Phase_Adjunct(details) == nullptr);
    Tweak_Misc_Phase_Adjunct(details, adjunct);

    Copy_Cell(Array_At(details, IDX_ARROW_BODY), body);

    return Init_Action(OUT, details, ANONYMOUS, UNBOUND);
}
