//
//  file: %c-lambda.c
//  summary: "Lower-level generator than FUNC which has no RETURN"
//  section: datatypes
//  project: "Ren-C Language Interpreter and Run-time Environment"
//  homepage: https://github.com/metaeducation/ren-c/
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
// While FUNCTION has can have a RETURN: in its spec, LAMBDA cannot.  This
// means it's a lower-level generator, which just drops out whatever value
// happens to be in the evaluation cell when it's done.
//
// This means that RETURN can be a parameter or <local> of the lambda.  But
// more often, the meaning of RETURN will be whatever was in effect when the
// lambda was defined:
//
//      outer: func [x] [
//          let inner: lambda [y] [
//              return x + y  ; would return from OUTER, not INNER
//          ]
//          return inner/
//      ]
//
//=//// NOTES ////////////////////////////////////////////////////////////=//
//
// * The aspirational goal of the design of definitional returns was that you
//   could build FUNCTION from LAMBDA and get identical semantics, by having
//   a local variable called RETURN that was initialized with another LAMBDA
//   (that did not itself try to define a RETURN, thus avoiding an infinite
//   regress).  This would be a way to avoid having RETURN be a keyword in the
//   language, and instead be a feature some generators offered...with
//   FUNCTION being a low-level optimized native that implemented the feature
//   in a more efficient way.
//
//   Generally speaking this goal has been met (!)  But there are some issues
//   with how you would do things like expose the type constraint of RETURN
//   programmatically in the user created variation in the same way that
//   the FUNCTION generator is able to.
//
// * Because this code is very similar to FUNCTION, it may be that it should
//   be merged more...although the component operations have been factored
//   reasonably well such that this file is pretty short and doesn't have
//   a terrible amount of redundancy.
//

#include "sys-core.h"

enum {
    IDX_LAMBDA_BODY = IDX_INTERPRETED_BODY,
    MAX_IDX_LAMBDA = IDX_LAMBDA_BODY
};


//
//  Lambda_Dispatcher: C
//
// This runs very much like function dispatch, but there's no RETURN.  So
// the result of the call will just be whatever the body evaluates to.
// (Note that FUNCTION's result is forced to TRASH! if no RETURN is called
// before the end of the body block is reached.)
//
// 1. We prime the result with GHOST!, because lambdas are willing to vanish
//    if their bodies fully vaporize with no non-ghost values seen:
//
//        test1: lambda [] []
//        test2: lambda [] [comment "no body"]
//
//        >> 1 + 2 test1
//        == 3
//
//        >> 1 + 2 test2
//        == 3
//
//    It's an "unsurprising" ghost, hence you don't see ~,~ antiforms as
//    the result as a precaution.  The lambda will vanish if the code would
//    have vanished as a GROUP! in the evaluator.
//
Bounce Lambda_Dispatcher(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    Details* details = Ensure_Level_Details(L);
    assert(Details_Max(details) == MAX_IDX_LAMBDA);

    const Element* block = cast(Element*, Details_At(details, IDX_LAMBDA_BODY));
    assert(Is_Block(block));

    assert(Link_Inherit_Bind(L->varlist) == nullptr);
    Add_Link_Inherit_Bind(L->varlist, List_Binding(block));
    Force_Level_Varlist_Managed(L);

    Element* spare_rebound = Copy_Cell(SPARE, block);
    Tweak_Cell_Binding(spare_rebound, L->varlist);

    Flags flags = LEVEL_MASK_NONE;

    require (
      Level* sub = Make_Level_At_Core(
        &Evaluator_Executor, spare_rebound, SPECIFIED, flags
    ));
    Init_Unsurprising_Ghost(Evaluator_Primed_Cell(sub));  // allow vanish [1]

    Push_Level_Erase_Out_If_State_0(OUT, sub);

    return BOUNCE_DELEGATE;
}


//
//  Lambda_Details_Querier: C
//
bool Lambda_Details_Querier(
    Sink(Value) out,
    Details* details,
    SymId property
){
    assert(Details_Dispatcher(details) == &Lambda_Dispatcher);
    assert(Details_Max(details) == MAX_IDX_LAMBDA);

    switch (property) {
      case SYM_RETURN_OF:
        Init_Unconstrained_Parameter(
            out,
            FLAG_PARAMCLASS_BYTE(PARAMCLASS_NORMAL)
        );
        return true;

      case SYM_BODY_OF: {
        Copy_Cell(out, Details_At(details, IDX_LAMBDA_BODY));
        assert(Is_Block(out));  // !!! just return as-is, even if relativized?
        return true; }

      default:
        break;
    }

    return false;
}


//
//  lambda: native [
//
//  "Make an anonymous function that doesn't define a local RETURN"
//
//      return: [action!]
//      spec "Help string (opt) followed by arg words, RETURN is a legal arg"
//          [block!]
//      body "Code implementing the lambda"
//          [block!]
//  ]
//
DECLARE_NATIVE(LAMBDA)
{
    INCLUDE_PARAMS_OF_LAMBDA;

    Element* spec = Element_ARG(SPEC);
    Element* body = Element_ARG(BODY);

    require (
      Details* details = Make_Interpreted_Action(
        spec,
        body,
        SYM_0,  // no RETURN: in the paramlist
        &Lambda_Dispatcher,
        MAX_IDX_LAMBDA  // archetype and one array slot (will be filled)
    ));

    Init_Action(OUT, details, ANONYMOUS, NONMETHOD);
    return UNSURPRISING(OUT);
}
