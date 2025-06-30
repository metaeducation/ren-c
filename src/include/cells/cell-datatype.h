//
//  file: %cell-datatype.h
//  summary: "DATATYPE! Datatype Header"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2025 Ren-C Open Source Contributors
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
// Rebol2/Red/R3-Alpha have a notion of a distinct DATATYPE! type, which can
// appear in blocks.  However it never really had a reified lexical form, so
// they would default to looking like WORD!s
//
//    r3-alpha>> reduce [integer! block!]
//    == [integer! block!]
//
// You would have to use something like MOLD:ALL to reveal a LOAD-able syntax
// that would get you a DATATYPE! and not a WORD!:
//
//    r3-alpha>> mold:all reduce [integer! block!]
//    == "[#[datatype! integer!] #[datatype! block!]]"
//
// Ren-C's approach is to leverage antiform fences to act as datatypes:
//
//    >> integer!
//    == ~{integer!}~  ; anti
//
//    >> type of first ['''10]
//    == ~{quoted!}~  ; anti
//
//    >> heart of first ['''10]
//    == ~{integer!}~   ; anti
//
// They cannot be put in blocks, but their metaforms can.  Not being able to
// appear in blocks has advantages, such as disambiguating situations like
// this historical code:
//
//     rebol2>> find [a 1 b c] integer!
//     == [1 b c]
//
//     rebol2>> find compose [a (integer!) b c] integer!
//     == [integer! b c]  ; not a word!, should render as [#[integer!] b c]
//
// The TYPESET! datatype is replaced with the idea of type predicates, which
// are actions (antiform FRAME!)
//
//    >> match any-series?/ [a b c]
//    == [a b c]
//
//    >> match any-series?/ 10
//    == ~null~  ; anti
//
// Enhancements to the speed of type checking predicates are done using
// "intrinsics" as well as a new concept of "typesets" as a table built up
// from %types.r that mixes sparse and ranged byte checking for speed.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * %words.r is arranged so symbols for the fundamental types are at the
//   start of the enumeration.
//

// Datatypes cache a byte of their datatype in the array of the FENCE!.
// This is only available on antiforms, which are canonized from arbitrary
// FENCE!s created by the user to the ones made in Startup_Datatypes() which
// have the DATATYPE_BYTE() set.
//
#define DATATYPE_BYTE(source) \
    SECOND_BYTE(&FLEX_INFO(source))


// 1. This returns a RebolValue* to hold the datatype.  This paves the way
//    for the ability to GC datatypes if all references disappear.  (Right
//    now it doesn't work that way because the datatypes live in the
//    SYS.CONTEXTS.DATATYPES module, and are held alive by the module.  Could
//    we have "weak" variables that disappear when when all refs vanish?)
//
// 2. There are some open questions at the moment about how to handle the
//    issue of dependencies in native specs on extension types.  For instance,
//    the FFI extension wants to have parameters that take [library!], but
//    you might load the FFI extension first and then load the library
//    extension...so when the FFI native specs are loaded the parameter
//    generation might crash.  Hence allowing extensions to register the
//    datatypes they depend on before the actual extension providing it
//    is something that this is starting with.
//
INLINE RebolValue* Register_Datatype(const char* name)  // return "holder" [1]
{
    Size size = strsize(name);
    const Symbol* symbol = wont_fail (
        Intern_Utf8_Managed(cb_cast(name), size)
    );

    RebolValue* result = Alloc_Value();

    Option(Patch*) patch = Sea_Patch(g_datatypes_context, symbol, true);
    if (patch) {
        Value* datatype = c_cast(Value*, Stub_Cell(unwrap patch));
        assert(Is_Datatype(datatype));
        Copy_Cell(result, datatype);
        return rebUnmanage(result);  // "forward" registrations [2]
    }

    Source* a = Alloc_Singular(STUB_MASK_MANAGED_SOURCE);
    Init_Word(Stub_Cell(a), symbol);
    Freeze_Source_Deep(a);

    Init(Slot) slot = Append_Context(g_datatypes_context, symbol);
    Value* datatype = Init_Fence(slot, a);
    Stably_Antiformize_Unbound_Fundamental(datatype);
    assert(Is_Datatype(datatype));

    Copy_Cell(result, datatype);
    return rebUnmanage(result);
}

INLINE void Unregister_Datatype(RebolValue* datatype_holder)
{
    assert(Is_Datatype(datatype_holder));
    rebRelease(datatype_holder);
}


INLINE bool Is_Symbol_Id_Of_Builtin_Type(SymId id) {
    assert(id != SYM_0_constexpr);
    return (
        u_cast(SymId16, id) >= MIN_SYM_BUILTIN_TYPES
        and u_cast(SymId16, id) <= MAX_SYM_BUILTIN_TYPES
    );
}

INLINE Type Type_From_Symbol_Id(SymId id) {
    assert(Is_Symbol_Id_Of_Builtin_Type(id));
    return u_cast(TypeEnum, id - MIN_SYM_BUILTIN_TYPES + 1);
}

INLINE SymId Symbol_Id_From_Type(Type type) {
    assert(type != TYPE_0_constexpr);
    return cast(SymId,
        u_cast(SymId16, u_cast(Byte, type) + MIN_SYM_BUILTIN_TYPES - 1)
    );
}


INLINE Option(SymId) Cell_Datatype_Id(const Value* v) {
    assert(Is_Datatype(v));
    if (Series_Len_At(v) != 1)
        abrupt_panic ("Type blocks only allowed one element for now");
    const Element* item = List_At(nullptr, v);
    if (not Is_Word(item))
        abrupt_panic ("Type blocks only allowed WORD! items for now");
    return Word_Id(item);
}

// 1. When a user writes (type: anti '{integer!}) then converting to an
//    antiform is what canonizes the fence's array to one that has the
//    DATATYPE_BYTE() set.  So you can only ask this of antiforms.
//
INLINE Option(Type) Cell_Datatype_Type(const Value* v) {
    assert(Is_Datatype(v));  // only works on antiform [1]
    return u_cast(Option(Type), DATATYPE_BYTE(Cell_Array(v)));
}

#if RUNTIME_CHECKS
    INLINE Option(Type) Cell_Datatype_Type_Slow_Debug(const Value* v) {
        Option(SymId) id = Cell_Datatype_Id(v);
        if (id and Is_Symbol_Id_Of_Builtin_Type(unwrap id))
            return Type_From_Symbol_Id(unwrap id);
        return TYPE_0;
    }
#endif

INLINE Option(Heart) Cell_Datatype_Heart(const Value* v) {
    Byte type_byte_or_0 = u_cast(Byte, Cell_Datatype_Type(v));
    assert(type_byte_or_0 <= u_cast(Byte, MAX_HEART));  // no QUOTE/QUASI/ANTI
    return u_cast(Option(Heart), type_byte_or_0);
}

INLINE Heart Cell_Datatype_Builtin_Heart(const Value* v) {
    Option(Type) type = Cell_Datatype_Type(v);
    assert(type);
    Byte type_byte = u_cast(Byte, type);
    assert(type_byte <= u_cast(Byte, MAX_HEART));  // not QUOTED/QUASI/ANTI
    return u_cast(HeartEnum, type_byte);
}

INLINE const ExtraHeart* Cell_Datatype_Extra_Heart(const Value* v) {
    assert(Is_Datatype(v));

    const Symbol* s = Word_Symbol(List_Item_At(v));
    Option(Patch*) patch = Sea_Patch(g_datatypes_context, s, true);
    assert(patch);
    return unwrap patch;
}


INLINE const ExtraHeart* Cell_Extra_Heart(const Cell* v) {
    assert(Heart_Of_Is_0(v));
    return c_cast(ExtraHeart*, v->extra.base);
}


INLINE Value* Init_Extended_Datatype_Untracked(
    Init(Value) out,
    const ExtraHeart* ext_heart
){
    assert(Is_Stub_Patch(ext_heart));
    const Value* datatype = c_cast(Value*, Stub_Cell(ext_heart));
    assert(Is_Datatype(datatype));
    return Copy_Cell(out, datatype);
}

#define Init_Extended_Datatype(out,ext_heart) \
    TRACK(Init_Extended_Datatype_Untracked((out), (ext_heart)))


// Used by the Typechecker intrinsic, but also Generic dispatch and PARAMETER!
// typechecking optimization.
//
// 1. The built-in typeset checks can only really match extension types with
//    ANY-ELEMENT? and ANY-FUNDAMENTAL?.  So this should only be checked on
//    extension types *after* the hooks for their ExtraHeart have been done.
//
INLINE bool Builtin_Typeset_Check(
    TypesetByte typeset_byte,
    Option(Type) type  // includes extension types for ANY-ELEMENT?, etc. [1]
){
    TypesetFlags typeset = g_typesets[typeset_byte];

    if (typeset & TYPESET_FLAG_0_RANGE) {  // trivial ranges ok (one datatype)
        Byte start = THIRD_BYTE(&typeset);
        Byte end = FOURTH_BYTE(&typeset);
        return start <= u_cast(Byte, type) and u_cast(Byte, type) <= end;
    }

    if (u_cast(Byte, type) > MAX_TYPE_BYTE_ELEMENT)
        return false;  // antiform, no sparse_memberships (only ranged)

    return did (g_sparse_memberships[u_cast(Byte, type)] & typeset);
}
