//
//  file: %stub-context.h
//  summary: "Context definitions AFTER including %tmp-internals.h"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// All Context* subtypes use their Stub.link.base field to point to the next
// context in their inheritance chain.  So a Stub representing a Let might
// point to a VarList for a FRAME! which might in turn point to a SeaOfVars
// for a MODULE!.  This is how "Virtual Binding" works.
//

INLINE Option(Context*) Link_Inherit_Bind(Context* context)
  { return u_cast(Context*, context->link.base); }

INLINE Option(Context*) Link_Inherit_Bind_Raw(Stub* context) {
    assert(Stub_Flavor(context) == FLAVOR_VARLIST);
    return u_cast(Context*, context->link.base);
}

INLINE void Tweak_Link_Inherit_Bind_Raw(Stub* context, Option(Context*) next) {
    LINK_CONTEXT_INHERIT_BIND(context) = opt next;

    if (not next)
        Clear_Stub_Flag(context, LINK_NEEDS_MARK);
    else {
        Set_Stub_Flag(context, LINK_NEEDS_MARK);

        Flavor flavor = Stub_Flavor(unwrap next);
        assert(
            flavor == FLAVOR_LET
            or flavor == FLAVOR_USE
            or flavor == FLAVOR_VARLIST
            or flavor == FLAVOR_SEA
        );
        UNUSED(flavor);
        assert(Is_Base_Managed(unwrap next));
    }
}

#define Tweak_Link_Inherit_Bind(context, next) \
    Tweak_Link_Inherit_Bind_Raw(ensure(Context*, (context)), next)

INLINE void Add_Link_Inherit_Bind_Raw(Stub* context, Option(Context*) next) {
    assert(LINK_CONTEXT_INHERIT_BIND(context) == nullptr);
    Tweak_Link_Inherit_Bind_Raw(context, next);
}

#define Add_Link_Inherit_Bind(context,next) \
    Add_Link_Inherit_Bind_Raw(ensure(Context*, (context)), next)


// !!! Need better mechanism for getting context types.

INLINE const Element* Varlist_Archetype(VarList* c) {  // read-only form
    return Flex_Head_Dynamic(Element, c);
}

INLINE Heart CTX_TYPE(Context* c) {
    if (Is_Stub_Sea(c))
        return TYPE_MODULE;
    assert(Is_Stub_Varlist(c));
    return unwrap Heart_Of(Varlist_Archetype(cast(VarList*, c)));
}


INLINE const Symbol* Let_Symbol(const Let* let) {
    return cast(const Symbol*, INFO_LET_SYMBOL(let));
}

INLINE Slot* Let_Slot(Let* let) {
    return u_cast(Slot*, Stub_Cell(let));
}

INLINE Option(Slot*) Lookup_Let_Slot(
    Let* let,
    const Symbol* symbol,
    bool strict
){
    UNUSED(strict);  // TBD
    if (INFO_LET_SYMBOL(let) != symbol)
        return nullptr;

    return Let_Slot(let);
}
