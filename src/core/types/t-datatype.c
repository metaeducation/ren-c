//
//  file: %t-datatype.c
//  summary: "datatype datatype"
//  section: datatypes
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"


//
//  Startup_Datatypes: C
//
// Create library words for each type, (e.g. make INTEGER! correspond to
// the integer datatype value).  Returns an array of words for the added
// datatypes to use in SYSTEM/CATALOG/DATATYPES.  See %specs/types.r
//
// 1. Things like INTEGER! are defined to be ~{integer!}~ antiforms.
//
// 2. Right now the symbols in the spots where symbols for antiforms of
//    hearts that can't be antiforms would be are things like ANTIFORM-38!.
//    This could be reused for something else.  But we certainly don't want
//    to make datatypes for those.  Leave the Patch corresponding to it
//    empty as undefined.
//
// 3. Many places in the system want to be able to just off-the-cuff refer to
//    a built-in datatype, without allocating a cell to initialize.  This is
//    done with Datatype_From_Type(), that returns it from the lib context.
//
void Startup_Datatypes(void)
{
    SeaOfVars* datatypes = Alloc_Sea_Core(BASE_FLAG_MANAGED);

    SymId16 id16 = MIN_SYM_BUILTIN_TYPES;

    for (; id16 <= MAX_SYM_BUILTIN_TYPES; ++id16) {
        SymId id = cast(SymId, id16);
        Type type = Type_From_Symbol_Id(id);

        if (type > MAX_TYPE_ELEMENT) {  // antiform
            Heart heart = u_cast(
                Heart,
                u_cast(Byte, type) - u_cast(Byte, MAX_TYPE_ELEMENT)
            );
            if (not Any_Isotopic_Type(heart))
                continue;  // don't define the dummy antiform for this [2]
        }

        Patch* patch = &g_datatype_patches[cast(Byte, type)];
        assert(Is_Stub_Erased(patch));  // pre-boot state

        patch->leader.bits = STUB_MASK_PATCH;

        assert(INFO_PATCH_SEA(patch) == nullptr);
        assert(LINK_PATCH_RESERVED(patch) == nullptr);
        Tweak_Info_Patch_Sea(patch, datatypes);

        Symbol* symbol = &g_symbols.builtin_canons[id];
        assert(Misc_Hitch(symbol) == symbol);  // no module patches yet
        Tweak_Misc_Hitch(symbol, patch);  // ...but now it has one!
        Tweak_Misc_Hitch(patch, symbol);  // link back for singly-linked-list

        Sink(Value) datatype = Stub_Cell(patch);
        Source* a = Alloc_Singular(FLEX_MASK_MANAGED_SOURCE);
        Init_Word(Stub_Cell(a), Canon_Symbol(Symbol_Id_From_Type(type)));
        Freeze_Source_Deep(a);
        Init_Fence(datatype, a);
        LIFT_BYTE_RAW(datatype) = ANTIFORM_0;  // fences are isotopic
        Set_Cell_Flag(datatype, PROTECTED);

        assert(datatype == Datatype_From_Type(type));  // convenient [3]
        assert(Cell_Datatype_Type(datatype) == type);  // sanity check
    }

    ensure_nullptr(g_datatypes_context) = datatypes;
}


//
//  Shutdown_Datatypes: C
//
// Basically the same as issues as Shutdown_Lib(), see notes there!
//
// 1. See Shutdown_Lib()'s [1]
//
// 2. See Shutdown_Lib()'s [2]
//
void Shutdown_Datatypes(void)
{
    SymId16 id16 = MIN_SYM_BUILTIN_TYPES;

    assert(Is_Stub_Erased(&g_datatype_patches[cast(Byte, TYPE_0)]));  // skip

    for (; id16 <= MAX_SYM_BUILTIN_TYPES; ++id16) {
        SymId id = cast(SymId, id16);
        Type type = Type_From_Symbol_Id(id);
        Patch* patch = &g_datatype_patches[cast(Byte, type)];

        if (Is_Stub_Erased(patch))
            continue;  // isotope slot for non-isotopic type

        assert(INFO_PATCH_SEA(patch) == g_datatypes_context);  // freed [1]
        INFO_PATCH_SEA(patch) = nullptr;

        Force_Erase_Cell(Stub_Cell(patch));  // re-init to 0, overwrite PROTECT

        assert(LINK_PATCH_RESERVED(patch) == nullptr);

        Symbol* symbol = &g_symbols.builtin_canons[id16];

        assert(Misc_Hitch(patch) == symbol);  // assert no other patches [2]
        assert(Misc_Hitch(symbol) == patch);
        Tweak_Misc_Hitch(symbol, symbol);

        Erase_Stub(patch);
    }

    g_datatypes_context = nullptr;
}
