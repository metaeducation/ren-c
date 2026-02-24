//
//  file: %c-arrow.c
//  summary: "Lambda Variation That Doesn't Deep Copy Body, Can Unpack Args "
//  section: datatypes
//  project: "Ren-C Language Interpreter and Run-time Environment"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2021-2026 Ren-C Open Source Contributors
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

#include "sys-core.h"


//
//  /arrow: native [
//
//  "Makes an anonymous function that doesn't copy its body, can unpack args"
//
//      return: [action!]
//      @(spec) "Ordinary spec, single arg spec, or ~(args to unpack)~"
//           [<hole> _ word! 'word! ^word! :word! block! ~group!~]
//      @(body) "Code to execute (will not be deep copied)"
//           [block! fence!]
//  ]
//
DECLARE_NATIVE(ARROW)
{
    INCLUDE_PARAMS_OF_ARROW;

    if (STATE != STATE_0)
        goto dispatch_to_lambda;

    Element* spec;

  initial_entry: {

    Param* param = ARG(SPEC);

    if (Is_Cell_A_Bedrock_Hole(param)) {
        spec = Init_Word(LOCAL(SPEC), CANON(QUESTION_1));
        goto wrap_spec_in_block;
    }

    spec = As_Element(param);

    if (Is_Block(spec))
        goto dispatch_to_lambda;

    if (Is_Space(spec)) {  // nameless fields not yet supported in LAMBDA
        Init_Word(spec, CANON(DUMMY2));  // DUMMY1 has use in LAMBDA
        goto wrap_spec_in_block;
    }

    if (Heart_Of(spec) == HEART_WORD)
        goto wrap_spec_in_block;

    if (not Is_Quasiform(spec))
        panic ("SPEC must be WORD!, BLOCK!, ~GROUP!~, or <hole> for ARROW");

  make_unpacking_arrow: {

  // This does a relatively inefficient implementation of an unpacking arrow.
  // It should have its own custom dispatcher, but it's not a priority
  // at this time.  For how this works, see:
  //
  // https://rebol.metaeducation.com/t/arrow-function-written-in-usermode/2651

    Clear_Cell_Quotes_And_Quasi(spec);

    Sink(Stable) spare_inner = SPARE;

    if (rebRunThrows(
        spare_inner,
        CANON(LAMBDA), ARG(SPEC), ARG(BODY)
    )){
        return THROWN;
    }
    assert(Is_Frame(spare_inner));  // decays by default ATM

    return rebUndecayed(
        CANON(LAMBDA), "[^pack [pack!] {f}] [",
            "f:", CANON(COPY), rebQ(spare_inner),
            CANON(SET), CANON(WORDS_OF), "f", "^pack",
            CANON(EVAL), "f",
        "]"
    );

}} wrap_spec_in_block: { /////////////////////////////////////////////////////

  // We could do an optimized ParamList generation here.  But the reuse of
  // the LAMBDA code is simply too convenient at this time, with so many
  // other things to work on besides that optimization...

    Source* a = Alloc_Singular(STUB_MASK_MANAGED_SOURCE);
    Copy_Cell(Stub_Cell(a), spec);
    Init_Block(spec, a);
    goto dispatch_to_lambda;

} dispatch_to_lambda: { //////////////////////////////////////////////////////

    return Apply_Cfunc(NATIVE_CFUNC(LAMBDA), LEVEL);
}}
