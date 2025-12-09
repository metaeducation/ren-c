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
    IDX_LAMBDA_RESULT_PARAM,  // specified in interface by `[]: [<typespec>]`
    MAX_IDX_LAMBDA = IDX_LAMBDA_RESULT_PARAM
};


//
//  Lambda_Dispatcher: C
//
// This runs very much like function dispatch, but there's no RETURN.  So
// the result of the call will just be whatever the body evaluates to.
//
// 1. We prime the result with GHOST!, because lambdas are willing to vanish
//    if their bodies fully vaporize with no non-ghost values seen:
//
//        test1: lambda [] []
//        test2: lambda [] [comment "no body"]
//
//        >> ghost? test1
//        == \~okay~\  ; antiform
//
//        >> ghost? test2
//        == \~okay~\  ; antiform
//
//    Do note that this will often appear as VOID in multi-step opeations if
//    you do not make the lambda GHOSTABLE.
//
//    https://rebol.metaeducation.com/t/comment-vanishes-but-not-if-eval/2563
//
// 2. PROCEDURE and DIVERGER are similar except for what happens when the
//    body finishes, so they share this dispatcher and just detect the mode
//    based on special signals where the PARAMETER! for a LAMBDA would be.
//
Bounce Lambda_Dispatcher(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    enum {
        ST_LAMBDA_INITIAL_ENTRY = STATE_0,
        ST_LAMBDA_LAMBDA_EXECUTING,
        ST_LAMBDA_PROCEDURE_EXECUTING,  // PROCEDURE shares this dispatcher [2]
        ST_LAMBDA_DIVERGER_EXECUTING  // DIVERGER shares this dispatcher [2]
    };

    Details* details = Ensure_Level_Details(L);

    Option(const Element*) result_param;

    if (Details_Max(details) == MAX_IDX_ARROW)
        result_param = nullptr;
    else {
        assert(Details_Max(details) == MAX_IDX_LAMBDA);
        result_param = cast(Element*,
            Details_At(details, IDX_LAMBDA_RESULT_PARAM)
        );
    }

    switch (STATE) {
      case ST_LAMBDA_INITIAL_ENTRY: goto initial_entry;
      case ST_LAMBDA_LAMBDA_EXECUTING: goto lambda_finished;
      case ST_LAMBDA_PROCEDURE_EXECUTING: goto procedure_finished;
      case ST_LAMBDA_DIVERGER_EXECUTING: goto diverger_finished;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    const Element* block = cast(Element*, Details_At(details, IDX_LAMBDA_BODY));
    assert(Is_Block(block));

    assert(Link_Inherit_Bind(L->varlist) == nullptr);
    Add_Link_Inherit_Bind(L->varlist, List_Binding(block));
    Force_Level_Varlist_Managed(L);

    Inject_Methodization_If_Any(L);

    Element* spare_rebound = Copy_Cell(SPARE, block);
    Tweak_Cell_Binding(spare_rebound, L->varlist);

    Flags flags = LEVEL_MASK_NONE
        | (not LEVEL_FLAG_AFRAID_OF_GHOSTS);  // allow vanish [1]

    require (
      Level* sub = Make_Level_At_Core(
        &Evaluator_Executor, spare_rebound, SPECIFIED, flags
    ));
    Init_Ghost(Evaluator_Primed_Cell(sub));  // allow vanish [1]

    Push_Level_Erase_Out_If_State_0(OUT, sub);

    if (not result_param)
        return BOUNCE_DELEGATE;  // no typecheck callback needed (ARROW)

    if (Is_Quasar(result_param)) {
        STATE = ST_LAMBDA_PROCEDURE_EXECUTING;
        return CONTINUE_SUBLEVEL(sub);
    }

    if (Is_Space(result_param)) {
        STATE = ST_LAMBDA_DIVERGER_EXECUTING;
        return CONTINUE_SUBLEVEL(sub);
    }

    STATE = ST_LAMBDA_LAMBDA_EXECUTING;

    if (Is_Parameter_Unconstrained(result_param))
        return BOUNCE_DELEGATE;  // also no typecheck needed

    return CONTINUE_SUBLEVEL(sub);

} lambda_finished: { /////////////////////////////////////////////////////////

    assert(not Is_Parameter_Unconstrained(unwrap result_param));

    heeded (Corrupt_Cell_If_Needful(SCRATCH));
    heeded (Corrupt_Cell_If_Needful(SPARE));

    require (
      bool check = Typecheck_Coerce_Return(LEVEL, unwrap result_param, OUT)
    );
    if (not check)  // do it now [2]
        panic (Error_Bad_Return_Type(L, OUT, unwrap result_param));

    return OUT;

} procedure_finished: { //////////////////////////////////////////////////////

    trap (
      Elide_Unless_Error_Including_In_Packs(OUT)  // turn ERROR! to PANIC
    );
    return Init_Trash_Named_From_Level(OUT, L);

} diverger_finished: { ///////////////////////////////////////////////////////

    panic ("DIVERGER reached end of body without THROW or PANIC");
}}


//
//  Lambda_Details_Querier: C
//
bool Lambda_Details_Querier(
    Sink(Stable) out,
    Details* details,
    SymId property
){
    assert(Details_Dispatcher(details) == &Lambda_Dispatcher);
    assert(Details_Max(details) == MAX_IDX_LAMBDA);

    switch (property) {
      case SYM_RETURN_OF: {
        Stable* param = Details_At(details, IDX_LAMBDA_RESULT_PARAM);
        if (Is_Quasar(param)) {  // [] PARAMETER!
            Api(Stable*) arbitrary = rebStable("return of @", LIB(RANDOMIZE));
            Copy_Cell(out, arbitrary);
            rebRelease(arbitrary);
        }
        else if (Is_Space(param)) {  // [<divergent>] PARAMETER!
            Api(Stable*) arbitrary = rebStable("return of @", LIB(CRASH));
            Copy_Cell(out, arbitrary);
            rebRelease(arbitrary);
        }
        else
            Copy_Cell(out, param);
        assert(Is_Parameter(out));
        return true; }

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
//      return: [~[action!]~]
//      spec "Help string (opt) followed by arg words, RETURN is a legal arg"
//          [block!]
//      body [block!]
//  ]
//
DECLARE_NATIVE(LAMBDA)
//
// 1. The idea of LAMBDA being able to have a type constraint in its spec was
//    held hostage for a while to not knowing what to call the word for it.
//    Functions do [return: [<typespsec>], yielders do [yield: [<typespec>],
//    but it would be deceptive to call it RETURN: in a lambda since it
//    doesn't actually define a RETURN.
//
//    Eventually using an empty SET-BLOCK as [[]: [<typespec>]] was chosen.
//    It's weird, but the empty block shows what's missing that you would
//    otherwise expect there, as a word for what the returnlike construct is.
//    But there isn't one...so the empty block stands in for it.
{
    INCLUDE_PARAMS_OF_LAMBDA;

    Element* spec = Element_ARG(SPEC);
    Element* body = Element_ARG(BODY);

    require (
      Details* details = Make_Interpreted_Action(
        spec,
        body,
        SYM_DUMMY1,  // cue look for []: in the paramlist for return spec [1]
        &Lambda_Dispatcher,
        MAX_IDX_LAMBDA  // archetype and one array slot (will be filled)
    ));

    Pop_Unpopped_Return(  // SYM_DUMMY1 parameter was not popped
        Details_At(details, IDX_LAMBDA_RESULT_PARAM),
        STACK_BASE
      );

    Init_Action(OUT, details, ANONYMOUS, UNCOUPLED);
    return Packify_Action(OUT);
}


//
//  diverger: native [
//
//  "Declares divergent function (will PANIC if it reaches the end of body)"
//
//      return: [~[action!]~]
//      spec "Help string (opt) followed by arg words, RETURN is a legal arg"
//          [block!]
//      body [block!]
//  ]
//
DECLARE_NATIVE(DIVERGER)
{
    INCLUDE_PARAMS_OF_DIVERGER;

    Element* spec = Element_ARG(SPEC);
    Element* body = Element_ARG(BODY);

    Option(SymId) no_returner = none;  // don't allow []: or RETURN:

    require (
      Details* details = Make_Interpreted_Action(
        spec,
        body,
        no_returner,
        &Lambda_Dispatcher,
        MAX_IDX_LAMBDA  // archetype and one array slot (will be filled)
    ));

    Init_Space(Details_At(details, IDX_LAMBDA_RESULT_PARAM));  // panic signal

    Init_Action(OUT, details, ANONYMOUS, UNCOUPLED);
    return Packify_Action(OUT);
}
