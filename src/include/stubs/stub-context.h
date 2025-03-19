//
//  File: %stub-context.h
//  Summary: "Context definitions AFTER including %tmp-internals.h"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//

//=//// INHERITED BINDING LINK ////////////////////////////////////////////=//
//
// All Context* subtypes use their Stub.link.node field to point to the next
// context in their inheritance chain.  So a Stub representing a Let might
// point to a VarList for a FRAME! which might in turn point to a SeaOfVars
// for a MODULE!.  This is how "Virtual Binding" works.
//

INLINE Option(Context*) Link_Inherit_Bind(Context* context)
  { return u_cast(Context*, context->link.node); }

INLINE void Tweak_Link_Inherit_Bind(Context* context, Option(Context*) next) {
    if (next) {
        Flavor flavor = Stub_Flavor(unwrap next);
        assert(
            flavor == FLAVOR_LET
            or flavor == FLAVOR_USE
            or flavor == FLAVOR_VARLIST
            or flavor == FLAVOR_SEA
        );
        UNUSED(flavor);
        assert(Is_Node_Managed(unwrap next));
    }
    context->link.node = maybe next;
}

INLINE void Add_Link_Inherit_Bind(Context* context, Option(Context*) next) {
    assert(LINK_CONTEXT_INHERIT_BIND(context) == nullptr);
    LINK_CONTEXT_INHERIT_BIND(context) = maybe next;
}


// !!! Need better mechanism for getting context types.

INLINE const Element* Varlist_Archetype(VarList* c) {  // read-only form
    return Flex_Head_Dynamic(Element, c);
}

INLINE Heart CTX_TYPE(Context* c) {
    if (Is_Stub_Sea(c))
        return TYPE_MODULE;
    assert(Is_Stub_Varlist(c));
    return Cell_Heart(Varlist_Archetype(cast(VarList*, c)));
}


INLINE const Symbol* Info_Let_Symbol(const Stub* stub) {
    assert(Is_Stub_Let(stub));
    return cast(const Symbol*, INFO_LET_SYMBOL(stub));
}
