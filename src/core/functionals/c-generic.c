//
//  File: %c-generic.c
//  Summary: "Function that dispatches implementation based on argument types"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2020 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
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
// A "generic" is what R3-Alpha/Rebol2 had called "ACTION!" (until Ren-C took
// that as the umbrella term for all "invokables").  This kind of dispatch is
// based on the first argument's type, with the idea being a single C function
// for the type has a switch() statement in it and can handle many different
// such actions for that type.
//
// (e.g. APPEND [a b c] [d] would look at the type of the first argument,
// notice it was a BLOCK!, and call the common C function for lists with an
// append instruction--where that instruction also handles insert, length,
// etc. for BLOCK!s.)
//
// !!! This mechanism is a very primitive kind of "multiple dispatch".  Rebol
// will certainly need to borrow from other languages to develop a more
// flexible idea for user-defined types, vs. this very limited concept.
//
// https://en.wikipedia.org/wiki/Multiple_dispatch
// https://en.wikipedia.org/wiki/Generic_function
// https://stackoverflow.com/q/53574843/
//

#include "sys-core.h"

enum {
    IDX_GENERIC_VERB = 1,  // Word whose symbol is being dispatched
    IDX_GENERIC_MAX
};


//
//  Generic_Dispatcher: C
//
Bounce Generic_Dispatcher(Level* L)
{
    Phase* phase = Level_Phase(L);
    Details* details = Phase_Details(phase);
    const Symbol* verb = Cell_Word_Symbol(Details_At(details, IDX_GENERIC_VERB));

    // !!! It's technically possible to throw in locals or refinements at
    // any point in the sequence.  D_ARG() accounts for this...hackily.
    //
    Value* first_arg = D_ARG_Core(L, 1);

    return Run_Generic_Dispatch_Core(first_arg, L, verb);
}


//
//  /generic: infix native [
//
//  "Creates datatype action (currently for internal use only)"
//
//      return: [~]
//      @verb [set-run-word?]
//      spec [block!]
//  ]
//
DECLARE_NATIVE(generic)
//
// The `generic` native is designed to be an infix function that takes its
// first argument literally, so when you write (/foo: generic [...]), the
// /FOO: gets quoted to be passed in as the "verb".
{
    INCLUDE_PARAMS_OF_GENERIC;

    Element* verb = cast(Element*, ARG(verb));
    Unpath(verb);
    Unchain(verb);
    assert(Is_Word(verb));

    Element* spec = cast(Element*, ARG(spec));

    VarList* meta;
    Flags flags = MKF_RETURN;
    Array* paramlist = Make_Paramlist_Managed_May_Fail(
        &meta,
        spec,
        &flags  // return type checked only in debug build
    );

    Phase* generic = Make_Action(
        paramlist,
        nullptr,  // no partials
        &Generic_Dispatcher,  // return type is only checked in debug build
        IDX_NATIVE_MAX  // details array capacity
    );

    assert(ACT_ADJUNCT(generic) == nullptr);
    mutable_ACT_ADJUNCT(generic) = meta;

    Set_Action_Flag(generic, IS_NATIVE);

    Details* details = Phase_Details(generic);

    Init_Word(Details_At(details, IDX_NATIVE_BODY), Cell_Word_Symbol(verb));
    Copy_Cell(Details_At(details, IDX_NATIVE_CONTEXT), Lib_Module);

    Value* verb_var = Sink_Word_May_Fail(verb, SPECIFIED);
    Init_Action(verb_var, generic, Cell_Word_Symbol(verb), UNBOUND);

    return NOTHING;
}


//
//  Startup_Generics: C
//
// Returns an array of words bound to generics for SYSTEM/CATALOG/ACTIONS
//
// 1. The Startup_Natives() used Wrap_Extend_Core() to add all the natives
//    as variables in the LIB module so it could assign them.  We now do
//    the same thing for the generics so they can be assigned.
//
//    !!! Review if combining all the definitions into one pass could be
//    better...or if the boot process could be special-cased to create the
//    top level variables as it transcodes.
//
Array* Startup_Generics(const Element* boot_generics)
{
    assert(VAL_INDEX(boot_generics) == 0);  // should be at head, sanity check

    Context* context = Lib_Context;

    CollectFlags flags = COLLECT_ONLY_SET_WORDS;
    Wrap_Extend_Core(context, boot_generics, flags);  // top-level decls [1]

    DECLARE_ATOM (discarded);
    if (Eval_Any_List_At_Throws(discarded, boot_generics, context))
        panic (discarded);
    if (not Is_Quasi_Word_With_Id(Decay_If_Unstable(discarded), SYM_DONE))
        panic (discarded);

    if (0 != strcmp("open", String_UTF8(Canon(OPEN))))  // sanity check
        panic (Canon(OPEN));

    StackIndex base = TOP_INDEX;

    const Element* tail;
    Element* at = Cell_List_At_Known_Mutable(&tail, boot_generics);

    for (; at != tail; ++at)
        if (Try_Get_Settable_Word_Symbol(at)) {  // all generics as /foo:
            Derelativize(PUSH(), at, context);
            Unpath(TOP_ELEMENT);  // change /foo: -> foo:
            Unchain(TOP_ELEMENT);  // change foo: -> foo
        }

    return Pop_Stack_Values(base);  // catalog of generics
}
