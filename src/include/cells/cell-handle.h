//
//  file: %cell-handle.h
//  summary: "Definitions for GC-able and non-GC-able Handles"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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
// In R3-Alpha, a HANDLE! was just a cell that would hold an arbitrary C
// data pointer.  The pointer was not shared as the cell was copied around...
// so it could not be changed and reflected in other instances.
//
// Ren-C kept that "cheap" form, but also added a variant "managed" form of
// HANDLE that employs a shared stub.  This means that operations can change
// the data and have the change reflected in other references to that handle.
//
// Another feature of the managed form is that the base can hold a hook for
// a "cleanup" function.  The GC will call this when there are no references
// left to the handle.
//
////=// NOTES /////////////////////////////////////////////////////////////=//
//
// * The C language spec says that data pointers and function pointers on a
//   platform may not be the same size.  Many codebases ignore this and
//   assume that they are, but HANDLE! tries to stay on the right side of
//   the spec and has different forms for functions and data.
//

#define CELL_HANDLE_STUB(c)             CELL_PAYLOAD_1(c)

#define Extract_Cell_Handle_Stub(c)     cast(Stub*, CELL_HANDLE_STUB(c))

#define CELL_HANDLE_LENGTH_U(c)         (c)->extra.u

#define CELL_HANDLE_CDATA_P(c)          (c)->payload.split.two.p
#define CELL_HANDLE_CFUNC_P(c)          (c)->payload.split.two.cfunc
#define CELL_HANDLE_NODE_P(c)           (c)->payload.split.two.base


#define MISC_HANDLE_CLEANER(stub)       (stub)->misc.cfunc

INLINE Option(HandleCleaner*) Handle_Cleaner(Stub* handle_stub) {
    assert(Stub_Flavor(handle_stub) == FLAVOR_HANDLE);
    assert(Get_Stub_Flag(handle_stub, CLEANS_UP_BEFORE_GC_DECAY));
    return f_cast(HandleCleaner*, MISC_HANDLE_CLEANER(handle_stub));
}

INLINE void Tweak_Handle_Cleaner(
    Stub* handle_stub,
    Option(HandleCleaner*) cleaner
){
    assert(Stub_Flavor(handle_stub) == FLAVOR_HANDLE);
    assert(Get_Stub_Flag(handle_stub, CLEANS_UP_BEFORE_GC_DECAY));
    MISC_HANDLE_CLEANER(handle_stub) = f_cast(CFunction*, opt cleaner);
}


INLINE bool Is_Handle_Cfunc(const Stable* v) {
    assert(Unchecked_Heart_Of(v) == TYPE_HANDLE);
    return CELL_HANDLE_LENGTH_U(v) == 0;
}

MUTABLE_IF_C(Cell*, INLINE) Extract_Cell_Handle_Canon(CONST_IF_C(Cell*) cell)
{
    CONSTABLE(Cell*) c = m_cast(Cell*, cell);

    assert(Unchecked_Heart_Of(c) == TYPE_HANDLE);
    if (not Cell_Payload_1_Needs_Mark(c))
        return c;  // changing instance won't be seen by copies

    return Known_Stable(
        Stub_Cell(Extract_Cell_Handle_Stub(c))  // has shared base
    );
}

INLINE uintptr_t Cell_Handle_Len(const Stable* v) {
    assert(not Is_Handle_Cfunc(v));
    const Cell* canon = Extract_Cell_Handle_Canon(v);
    assert(Get_Cell_Flag(canon, DONT_MARK_PAYLOAD_2));
    return CELL_HANDLE_LENGTH_U(canon);
}

INLINE void* Cell_Handle_Void_Pointer(const Stable* v) {
    assert(not Is_Handle_Cfunc(v));
    const Cell* canon = Extract_Cell_Handle_Canon(v);
    assert(Get_Cell_Flag(canon, DONT_MARK_PAYLOAD_2));
    return CELL_HANDLE_CDATA_P(canon);
}

INLINE const Base* Cell_Handle_Base(const Stable* v) {
    assert(not Is_Handle_Cfunc(v));
    const Cell* canon = Extract_Cell_Handle_Canon(v);
    assert(Not_Cell_Flag(canon, DONT_MARK_PAYLOAD_2));
    return CELL_HANDLE_NODE_P(canon);
}

#define Cell_Handle_Pointer(T, v) \
    cast(T*, Cell_Handle_Void_Pointer(v))

INLINE CFunction* Cell_Handle_Cfunc(const Stable* v) {
    assert(Is_Handle_Cfunc(v));
    return CELL_HANDLE_CFUNC_P(Extract_Cell_Handle_Canon(v));
}

INLINE Option(HandleCleaner*) Cell_Handle_Cleaner(const Stable* v) {
    assert(Unchecked_Heart_Of(v) == TYPE_HANDLE);
    if (not Cell_Payload_1_Needs_Mark(v))
        return nullptr;
    Stub* stub = Extract_Cell_Handle_Stub(v);
    return Handle_Cleaner(stub);
}

INLINE void Tweak_Handle_Len(Stable* v, uintptr_t length)
  { CELL_HANDLE_LENGTH_U(Extract_Cell_Handle_Canon(v)) = length; }

INLINE void Tweak_Handle_Cdata(Stable* v, void *cdata) {
    Cell* canon = Extract_Cell_Handle_Canon(v);
    assert(CELL_HANDLE_LENGTH_U(canon) != 0);
    CELL_HANDLE_CDATA_P(canon) = cdata;
}

INLINE void Tweak_Handle_Cfunc(Stable* v, CFunction* cfunc) {
    assert(Is_Handle_Cfunc(v));
    Cell* canon = Extract_Cell_Handle_Canon(v);
    assert(CELL_HANDLE_LENGTH_U(canon) == 0);
    CELL_HANDLE_CFUNC_P(canon) = cfunc;
}

INLINE Element* Init_Handle_Cdata(
    Init(Element) out,
    void *cdata,
    uintptr_t length
){
    assert(length != 0);  // can't be 0 unless cfunc (see also malloc(0))

    Reset_Cell_Header_Noquote(
        out,
        FLAG_HEART(TYPE_HANDLE) | CELL_MASK_NO_MARKING
    );
    Corrupt_Unused_Field(out->payload.split.one.corrupt);
    CELL_HANDLE_CDATA_P(out) = cdata;
    CELL_HANDLE_LENGTH_U(out) = length;  // non-zero signals cdata

    return out;
}

INLINE Element* Init_Handle_Cfunc(
    Init(Element) out,
    CFunction* cfunc
){
    Reset_Cell_Header_Noquote(
        out,
        FLAG_HEART(TYPE_HANDLE) | CELL_MASK_NO_MARKING
    );
    Corrupt_Unused_Field(out->payload.split.one.corrupt);
    CELL_HANDLE_CFUNC_P(out) = cfunc;
    CELL_HANDLE_LENGTH_U(out) = 0;  // signals cfunc
    return out;
}

INLINE Element* Init_Handle_Base(
    Init(Element) out,
    const Base* base
){
    Reset_Cell_Header_Noquote(
        out,
        FLAG_HEART(TYPE_HANDLE) | CELL_FLAG_DONT_MARK_PAYLOAD_1
    );
    Corrupt_Unused_Field(out->payload.split.one.corrupt);
    CELL_HANDLE_NODE_P(out) = m_cast(Base*, base);  // extracted as const
    CELL_HANDLE_LENGTH_U(out) = 1;
    return out;
}

INLINE void Init_Handle_Managed_Common(
    Init(Element) out,
    uintptr_t length,
    Option(HandleCleaner*) cleaner
){
    require (
      Stub* stub = Make_Untracked_Stub(
        FLAG_FLAVOR(FLAVOR_HANDLE)
            | STUB_FLAG_CLEANS_UP_BEFORE_GC_DECAY  // calls the HandleCleaner
            | BASE_FLAG_MANAGED
    ));
    Tweak_Handle_Cleaner(stub, cleaner);  // FLAVOR_HANDLE in Diminish_Stub()

    Sink(Element) single = Stub_Cell(stub);
    Reset_Cell_Header_Noquote(
        single,
        FLAG_HEART(TYPE_HANDLE)
            | (not CELL_FLAG_DONT_MARK_PAYLOAD_1)  // points to singular
            | CELL_FLAG_DONT_MARK_PAYLOAD_2
    );
    CELL_HANDLE_STUB(single) = stub;
    CELL_HANDLE_LENGTH_U(single) = length;
    // caller fills in CELL_HANDLE_CDATA_P or CELL_HANDLE_CFUNC_P

    // Don't fill the handle properties in the instance if it's the managed
    // form.  This way, you can set the properties in the canon value and
    // effectively update all instances...since the bits live in the shared
    // Flex component.
    //
    Reset_Cell_Header_Noquote(
        out,
        FLAG_HEART(TYPE_HANDLE)
            | (not CELL_FLAG_DONT_MARK_PAYLOAD_1)  // points to stub
            | CELL_FLAG_DONT_MARK_PAYLOAD_2
    );
    CELL_HANDLE_STUB(out) = stub;

    CELL_HANDLE_LENGTH_U(out) = 0xDECAFBAD;  // corrupt avoids compiler warning
    CELL_HANDLE_CDATA_P(out) = nullptr;  // or complains about not initializing
}

INLINE Element* Init_Handle_Cdata_Managed(
    Init(Element) out,
    void *cdata,
    uintptr_t length,
    Option(HandleCleaner*) cleaner
){
    Init_Handle_Managed_Common(out, length, cleaner);

    // Leave the non-singular cdata corrupt; clients should not be using

    Stub* stub = Extract_Cell_Handle_Stub(out);
    CELL_HANDLE_CDATA_P(Stub_Cell(stub)) = cdata;
    return out;
}

INLINE Element* Init_Handle_Cfunc_Managed(
    Init(Element) out,
    CFunction* cfunc,
    Option(HandleCleaner*) cleaner
){
    Init_Handle_Managed_Common(out, 0, cleaner);

    // Leave the non-singular cfunc corrupt; clients should not be using

    Stub* stub = Extract_Cell_Handle_Stub(out);
    CELL_HANDLE_CFUNC_P(Stub_Cell(stub)) = cfunc;
    return out;
}

INLINE Element* Init_Handle_Base_Managed(
    Init(Element) out,
    const Base* base,
    Option(HandleCleaner*) cleaner
){
    Init_Handle_Managed_Common(out, 1, cleaner);

    // Leave the non-singular cdata corrupt; clients should not be using

    Cell* cell = Stub_Cell(Extract_Cell_Handle_Stub(out));
    Clear_Cell_Flag(cell, DONT_MARK_PAYLOAD_2);
    CELL_HANDLE_NODE_P(cell) = m_cast(Base*, base);  // extracted as const
    return out;
}

#define Handle_Holds_Base(c) \
    Cell_Payload_2_Needs_Mark(Stub_Cell(Extract_Cell_Handle_Stub(c)))
