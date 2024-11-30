//
//  File: %c-augment.c
//  Summary: "Function generator for expanding the frame of an ACTION!"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2019-2021 Ren-C Open Source Contributors
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
// AUGMENT is designed to create a version of a function with an expanded
// frame, adding new parameters.  It does so without affecting the execution:
//
//     >> /foo-x: func [x [integer!]] [print ["x is" x]]
//     >> /foo-xy: augment foo-x/ [y [integer!]]
//
//     >> foo-x 10
//     x is 10
//
//     >> foo-xy 10
//     ** Error: foo-xy is missing its y argument
//
//     >> foo-xy 10 20
//     x is 10
//
// The original function doesn't know about the added parameters, so this is
// is only useful when combined with something like ADAPT or ENCLOSE... to
// inject in phases of code at a higher level that see these parameters:
//
//     >> /foo-xy: adapt (augment foo-x/ [y [integer!]]) [print ["y is" y]]
//
//     >> foo-xy 10 20
//     y is 20
//     x is 10
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * AUGMENT in historical Redbol would have been complicated by the idea
//   that refinements could span multiple arguments.  So if a function's spec
//   was `[arg1 /refine refarg1 refarg2]` then augmenting it would look like
//   `[arg1 /refine refarg1 refarg2 arg2]` and seem like it was adding an
//   argument to the refinement.  Since Ren-C refinements are the name of
//   the single argument they represent, this is not a problem.
//

#include "sys-core.h"

// See notes why the Augmenter gets away with reusing Specializer_Dispatcher
//
#define Augmenter_Dispatcher Specializer_Dispatcher
#define IDX_AUGMENTER_MAX 1


//
//  /augment: native [
//
//  "Create an action variant that acts the same, but has added parameters"
//
//      return: [action?]
//      original "Frame whose implementation is to be augmented"
//          [<unrun> frame!]
//      spec "Spec dialect for words to add to the derived function"
//          [block!]
//  ]
//
DECLARE_NATIVE(augment)
//
// 1. We reuse the process from Make_Paramlist_Managed_May_Fail(), which
//    pushes WORD! and PARAMETER! antiform pairs for each argument.
//
// 2. For any specialized (including local) parameters in the paramlist we are
//    copying, we want to "seal" them from view.  We wouldn't have access to
//    them if we were an ADAPT and not making a copy (since the action in the
//    exemplar would not match the phase).  So making a copy should not
//    suddenly subvert the access.
//
// 3. We don't need a new Phase.  AUGMENT itself doesn't add any new behavior,
//    so we can get away with patching the augmentee's action information
//    (phase and coupling) into the paramlist.
{
    INCLUDE_PARAMS_OF_AUGMENT;

    Element* spec = cast(Element*, ARG(spec));
    Element* original = cast(Element*, ARG(original));

    Option(const Symbol*) label = VAL_FRAME_LABEL(original);
    Action* augmentee = VAL_ACTION(original);

    Flags flags = MKF_MASK_NONE;  // if original had no return, we don't add

  blockscope {  // copying the augmentee's parameter names and values [1]
    const Key* key_tail;
    const Key* key = ACT_KEYS(&key_tail, augmentee);
    const Param* param = ACT_PARAMS_HEAD(augmentee);
    for (; key != key_tail; ++key, ++param) {
        Init_Word(PUSH(), *key);
        Copy_Cell(PUSH(), param);

        if (Is_Specialized(param))
            Set_Cell_Flag(TOP, STACK_NOTE_SEALED);  // seal parameters [2]
    }
  }

    VarList* adjunct = nullptr;

    Push_Keys_And_Holes_May_Fail(  // add spec parameters, may add duplicates
        &adjunct,
        spec,
        &flags
    );

    Array* paramlist = Pop_Paramlist_With_Adjunct_May_Fail(  // checks dups
        &adjunct, STACK_BASE, flags
    );

    assert(Not_Cell_Readable(Flex_Head(Value, paramlist)));
    Tweak_Frame_Varlist_Rootvar(  // no new phase needed, just use frame [3]
        paramlist,
        ACT_IDENTITY(VAL_ACTION(ARG(original))),
        Cell_Coupling(ARG(original))
    );

    Phase* augmentated = Make_Phase(
        paramlist,
        ACT_PARTIALS(augmentee),  // partials should still work
        &Augmenter_Dispatcher,
        IDX_AUGMENTER_MAX  // same as specialization, just 1 (for archetype)
    );

    assert(ACT_ADJUNCT(augmentated) == nullptr);
    mutable_ACT_ADJUNCT(augmentated) = adjunct;

    // Keep track that the derived keylist is related to the original, so
    // that it's possible to tell a frame built for the augmented function is
    // compatible with the original function (and its ancestors, too)
    //
    LINK(Ancestor, ACT_KEYLIST(augmentated)) = ACT_KEYLIST(augmentee);

    return Init_Action(OUT, augmentated, label, UNBOUND);
}
