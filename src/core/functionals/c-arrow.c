//
//  file: %c-arrow.c
//  summary: "Lambda Variation That Doesn't Deep Copy Body, Can Unpack Args "
//  section: datatypes
//  project: "Ren-C Language Interpreter and Run-time Environment"
//  homepage: https://github.com/metaeducation/ren-c/
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
//     >> if ok [10] then (x -> [print ["The branch produced" x]])
//     The branch produced 10
//
// While a BLOCK! of arguments can be used to gather multiple arguments, you
// can also use a quasiform of group to unpack the arguments:
//
//     case [
//         true [pack [10 + 20, 3 + 4]]  ; makes antiform ~('30 '7)~
//         ...
//     ] then (~(a b)~ -> [
//         assert [a = 30, b = 7]
//     ])
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

    const Element* body = Known_Element(Details_At(details, IDX_ARROW_BODY));
    assert(Is_Block(body) or Is_Fence(body));

    assert(Link_Inherit_Bind(L->varlist) == nullptr);
    Add_Link_Inherit_Bind(L->varlist, List_Binding(body));
    Force_Level_Varlist_Managed(L);

    Element* spare_rebound = Copy_Cell(SPARE, body);
    Tweak_Cell_Binding(spare_rebound, L->varlist);

    return DELEGATE_CORE(
        OUT,
        LEVEL_MASK_NONE,
        SPECIFIED,
        spare_rebound
    );
}


//
//  Arrow_Details_Querier: C
//
bool Arrow_Details_Querier(
    Sink(Stable) out,
    Details* details,
    SymId property
){
    assert(Details_Dispatcher(details) == &Arrow_Dispatcher);
    assert(Details_Max(details) == MAX_IDX_ARROW);

    switch (property) {
      case SYM_RETURN_OF:
        Init_Unconstrained_Parameter(
            out,
            FLAG_PARAMCLASS_BYTE(PARAMCLASS_META)
        );
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
//      return: [~(action!)~]
//      spec "Names of arguments"
//          [_ word! 'word! ^word! :word! block!]
//      @(body) [<const> block! fence!]
//  ]
//
DECLARE_NATIVE(ARROW)
{
    INCLUDE_PARAMS_OF_ARROW;

    Element* spec = Element_ARG(SPEC);
    Element* body = Element_ARG(BODY);

    bool optimizable = true;

    Element* methodization = Init_Quasar(SCRATCH);

  try_optimizing_simple_paramlist: {

    const Element* tail;
    const Element* at;
    if (Is_Block(spec)) {
        at = List_At(&tail, spec);
    }
    else if (
        Is_Word(spec)
        or Is_Get_Word(spec)
        or Is_Meta_Form_Of(WORD, spec)
        or Is_Quoted(spec)
        or (Is_Path(spec) and Is_Refinement(spec))
    ){
        at = spec;
        tail = at + 1;
    }
    else {
        assert(Is_Space(spec));
        at = nullptr;
        tail = nullptr;
    }

    for (; at != tail; ++at) {
        Flags param_flags = 0;
        ParamClass pclass;
        const Symbol* symbol;
        if (Is_Word(at)) {
            pclass = PARAMCLASS_NORMAL;
            symbol = Word_Symbol(at);
        }
        else if (Is_Meta_Form_Of(WORD, at)) {
            pclass = PARAMCLASS_META;
            symbol = Word_Symbol(at);
        }
        else if (Is_Quoted(at)) {
            if (Quotes_Of(at) != 1)
                panic (at);
            if (Heart_Of(at) == TYPE_WORD)
                pclass = PARAMCLASS_JUST;
            else
                panic (at);
            symbol = Word_Symbol(at);
        }
        else if (Is_Pinned_Form_Of(WORD, at)) {
            pclass = PARAMCLASS_THE;
            symbol = Word_Symbol(at);
        }
        else if (Is_Refinement(at)) {
            pclass = PARAMCLASS_NORMAL;
            symbol = Cell_Refinement_Symbol(at);
            param_flags |= PARAMETER_FLAG_REFINEMENT;
            param_flags |= PARAMETER_FLAG_NULL_DEFINITELY_OK;
        }
        else if (Is_Set_Word(at) and Word_Id(at) == SYM_RETURN) {
            panic (
                "ARROW (->) does not offer RETURN facilities, use FUNCTION"
            );
        }
        else {
            if (not Is_Block(spec))
                panic ("Invalid ARROW specification");

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

} process_paramlist_if_not_optimizable: {

    if (not optimizable) {
        require (
          Push_Keys_And_Params(
            methodization,
            spec,
            MKF_MASK_NONE,
            SYM_0  // no returner
        ));
    }

} pop_paramlist_and_make_details_phase: {

    Option(Phase*) prior = nullptr;
    Option(VarList*) prior_coupling = nullptr;
    Option(Element*) gather = nullptr;

    require (
      ParamList* paramlist = Pop_Paramlist(
          STACK_BASE,
          Is_Quasar(methodization) ? nullptr : methodization,
          prior,
          prior_coupling,
          gather
      )
    );

    Details* details = Make_Dispatch_Details(
        BASE_FLAG_MANAGED | DETAILS_FLAG_OWNS_PARAMLIST,
        Phase_Archetype(paramlist),
        &Lambda_Dispatcher,
        MAX_IDX_ARROW
    );

    Copy_Cell(Details_At(details, IDX_ARROW_BODY), body);

    Init_Action(OUT, details, ANONYMOUS, UNCOUPLED);
    return Packify_Action(OUT);
}}
