//
//  File: %c-does.c
//  Summary: "Expedient generator for 0-argument function specializations"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018-2023 Ren-C Open Source Contributors
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
// DOES in historical Rebol was simply a specialization of FUNC which assumed
// an empty spec block as a convenience.  It was thus in most other respects
// like a FUNC... e.g. it would catch throws of a RETURN signal.
//
// In Ren-C, DOES with a BLOCK! instead acts as a LAMBDA with an empty spec.
// So RETURN will be inherited from the context and act as is.
//
// An experiment was added to push DOES a bit further.  Not only does it take
// blocks, but it can take any other data type that DO will accept...such as
// a FILE! or URL!:
//
//     >> d: does https://example.com/some-script.reb
//
//     >> d
//     ; Will act like `do https://example/some-script.reb`
//
//=//// NOTES ////////////////////////////////////////////////////////////=//
//
// * One experimental feature was removed, to allow specialization by example.
//   For instance `c: does catch [throw <like-this>]`.  This was inspired by
//   code golf.  However, it altered the interface (to quote its argument and
//   be variadic) and it also brought in distracting complexity that is better
//   kept in the implementations of REFRAMER and POINTFREE.
//

#include "sys-core.h"


//
//  does: native [
//
//  "Make action that will EVAL a BLOCK!"
//
//      return: [action?]
//      source [block!]  ; allow other types? [1]
//  ]
//
DECLARE_NATIVE(does)
//
// 1. There is an open question if you should be able to pass DOES anything
//    that you could pass and use as a branch, e.g.:
//
//       >> foo: does 'something
//       >> foo
//       == 'something
//
//       >> bar: 10
//       >> foo: does @bar
//       >> foo
//       == 10
//
//    This competes with an alternate meaning, that you could pass DOES
//    anything that DO would take (e.g. URL!s).  For the moment it is limited
//    to just taking BLOCK! while those questions are hashed out.
//
//      https://forum.rebol.info/t/does-vs-do/2271
{
    INCLUDE_PARAMS_OF_DOES;

    Value* source = ARG(source);

    if (Is_Block(source))
        return rebValue(Canon(LAMBDA), EMPTY_BLOCK, source);

    assert(!"DOES with types other than BLOCK! not currently active");  // [1]

    VarList* exemplar = Make_Varlist_For_Action(
        Lib(EVALUATE),
        TOP_INDEX,  // lower stackindex would be if wanting to add refinements
        nullptr  // don't set up a binder; just poke specializee in frame
    );
    assert(Is_Node_Managed(exemplar));

    assert(KEY_SYM(Varlist_Key(exemplar, 1)) == SYM_RETURN);
    Copy_Cell(Varlist_Slot(exemplar, 2), source);

    const Symbol* label = Canon(EVALUATE);  // !!! Better answer?
    Init_Frame(OUT, exemplar, label);
    return Actionify(OUT);
}
