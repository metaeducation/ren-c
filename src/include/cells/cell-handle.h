//
//  File: %cell-handle.h
//  Summary: "Definitions for GC-able and non-GC-able Handles"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
// Another feature of the managed form is that the node can hold a hook for
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

#define Tweak_Cell_Handle_Stub          Tweak_Cell_Node1
#define Extract_Cell_Handle_Stub(c)     cast(Stub*, Cell_Node1(c))

#define CELL_HANDLE_LENGTH_U(c)         EXTRA(Any, (c)).u

#define CELL_HANDLE_CDATA_P(c)          PAYLOAD(Any, (c)).second.p
#define CELL_HANDLE_CFUNC_P(c)          PAYLOAD(Any, (c)).second.cfunc
#define CELL_HANDLE_NODE_P(c)           PAYLOAD(Any, (c)).second.node


INLINE bool Is_Handle_Cfunc(const Cell* v) {
    assert(Cell_Heart_Unchecked(v) == REB_HANDLE);
    return CELL_HANDLE_LENGTH_U(v) == 0;
}

INLINE Cell* Extract_Cell_Handle_Canon(const_if_c Cell* c) {
    assert(Cell_Heart_Unchecked(c) == REB_HANDLE);
    if (not Cell_Has_Node1(c))
        return m_cast(Cell*, c);  // changing instance won't be seen by copies
    return Stub_Cell(Extract_Cell_Handle_Stub(c));  // has shared node
}

#if CPLUSPLUS_11
    INLINE const Cell* Extract_Cell_Handle_Canon(const Cell* c) {
        assert(Cell_Heart_Unchecked(c) == REB_HANDLE);
        if (not Cell_Has_Node1(c))
            return c;  // changing handle instance won't be seen by copies
        return Stub_Cell(Extract_Cell_Handle_Stub(c));  // has shared node
    }
#endif

INLINE uintptr_t Cell_Handle_Len(const Cell* v) {
    assert(not Is_Handle_Cfunc(v));
    const Cell* canon = Extract_Cell_Handle_Canon(v);
    assert(Get_Cell_Flag(canon, DONT_MARK_NODE2));
    return CELL_HANDLE_LENGTH_U(canon);
}

INLINE void* Cell_Handle_Void_Pointer(const Cell* v) {
    assert(not Is_Handle_Cfunc(v));
    const Cell* canon = Extract_Cell_Handle_Canon(v);
    assert(Get_Cell_Flag(canon, DONT_MARK_NODE2));
    return CELL_HANDLE_CDATA_P(canon);
}

INLINE const Node* Cell_Handle_Node(const Cell* v) {
    assert(not Is_Handle_Cfunc(v));
    const Cell* canon = Extract_Cell_Handle_Canon(v);
    assert(Not_Cell_Flag(canon, DONT_MARK_NODE2));
    return CELL_HANDLE_NODE_P(canon);
}

#define Cell_Handle_Pointer(T, v) \
    cast(T*, Cell_Handle_Void_Pointer(v))

INLINE CFunction* Cell_Handle_Cfunc(const Cell* v) {
    assert(Is_Handle_Cfunc(v));
    return CELL_HANDLE_CFUNC_P(Extract_Cell_Handle_Canon(v));
}

INLINE Option(RebolHandleCleaner*) Cell_Handle_Cleaner(const Cell* v) {
    assert(Cell_Heart_Unchecked(v) == REB_HANDLE);
    if (not Cell_Has_Node1(v))
        return nullptr;
    return Extract_Cell_Handle_Stub(v)->misc.cleaner;
}

INLINE void Tweak_Cell_Handle_Len(Cell* v, uintptr_t length)
  { CELL_HANDLE_LENGTH_U(Extract_Cell_Handle_Canon(v)) = length; }

INLINE void Tweak_Cell_Handle_Cdata(Cell* v, void *cdata) {
    Cell* canon = Extract_Cell_Handle_Canon(v);
    assert(CELL_HANDLE_LENGTH_U(canon) != 0);
    CELL_HANDLE_CDATA_P(canon) = cdata;
}

INLINE void Tweak_Cell_Handle_Cfunc(Cell* v, CFunction* cfunc) {
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

    Reset_Cell_Header_Untracked(
        out,
        FLAG_HEART_BYTE(REB_HANDLE) | CELL_MASK_NO_NODES
    );
    Corrupt_Unused_Field(PAYLOAD(Any, out).first.corrupt);
    CELL_HANDLE_CDATA_P(out) = cdata;
    CELL_HANDLE_LENGTH_U(out) = length;  // non-zero signals cdata

    return out;
}

INLINE Element* Init_Handle_Cfunc(
    Init(Element) out,
    CFunction* cfunc
){
    Reset_Cell_Header_Untracked(
        out,
        FLAG_HEART_BYTE(REB_HANDLE) | CELL_MASK_NO_NODES
    );
    Corrupt_Unused_Field(PAYLOAD(Any, out).first.corrupt);
    CELL_HANDLE_CFUNC_P(out) = cfunc;
    CELL_HANDLE_LENGTH_U(out) = 0;  // signals cfunc
    return out;
}

INLINE Element* Init_Handle_Node(
    Init(Element) out,
    const Node* node
){
    Reset_Cell_Header_Untracked(
        out,
        FLAG_HEART_BYTE(REB_HANDLE) | CELL_FLAG_DONT_MARK_NODE1
    );
    Corrupt_Unused_Field(PAYLOAD(Any, out).first.corrupt);
    CELL_HANDLE_NODE_P(out) = node;
    CELL_HANDLE_LENGTH_U(out) = 1;
    return out;
}

INLINE void Init_Handle_Managed_Common(
    Init(Element) out,
    uintptr_t length,
    RebolHandleCleaner* cleaner
){
    Stub* stub = Make_Untracked_Stub(FLAG_FLAVOR(HANDLE) | NODE_FLAG_MANAGED);
    stub->misc.cleaner = cleaner;

    Cell* single = Stub_Cell(stub);
    Reset_Cell_Header_Untracked(
        single,
        FLAG_HEART_BYTE(REB_HANDLE)
            | (not CELL_FLAG_DONT_MARK_NODE1)  // points to singular
            | CELL_FLAG_DONT_MARK_NODE2
    );
    Tweak_Cell_Handle_Stub(single, stub);
    CELL_HANDLE_LENGTH_U(single) = length;
    // caller fills in CELL_HANDLE_CDATA_P or CELL_HANDLE_CFUNC_P

    // Don't fill the handle properties in the instance if it's the managed
    // form.  This way, you can set the properties in the canon value and
    // effectively update all instances...since the bits live in the shared
    // Flex component.
    //
    Reset_Cell_Header_Untracked(
        out,
        FLAG_HEART_BYTE(REB_HANDLE)
            | (not CELL_FLAG_DONT_MARK_NODE1)  // points to stub
            | CELL_FLAG_DONT_MARK_NODE2
    );
    Tweak_Cell_Handle_Stub(out, stub);

    CELL_HANDLE_LENGTH_U(out) = 0xDECAFBAD;  // corrupt avoids compiler warning
    CELL_HANDLE_CDATA_P(out) = nullptr;  // or complains about not initializing
}

INLINE Element* Init_Handle_Cdata_Managed(
    Init(Element) out,
    void *cdata,
    uintptr_t length,
    RebolHandleCleaner* cleaner
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
    RebolHandleCleaner* cleaner
){
    Init_Handle_Managed_Common(out, 0, cleaner);

    // Leave the non-singular cfunc corrupt; clients should not be using

    Stub* stub = Extract_Cell_Handle_Stub(out);
    CELL_HANDLE_CFUNC_P(Stub_Cell(stub)) = cfunc;
    return out;
}

INLINE Element* Init_Handle_Node_Managed(
    Init(Element) out,
    const Node* node,
    RebolHandleCleaner* cleaner
){
    Init_Handle_Managed_Common(out, 1, cleaner);

    // Leave the non-singular cdata corrupt; clients should not be using

    Cell* cell = Stub_Cell(Extract_Cell_Handle_Stub(out));
    Clear_Cell_Flag(cell, DONT_MARK_NODE2);
    CELL_HANDLE_NODE_P(cell) = node;
    return out;
}

#define Handle_Holds_Node(c) \
    Cell_Has_Node2(Stub_Cell(Extract_Cell_Handle_Stub(c)))
