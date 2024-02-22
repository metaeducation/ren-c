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

#define INIT_VAL_HANDLE_STUB            Init_Cell_Node1
#define VAL_HANDLE_STUB(v)              cast(Stub*, Cell_Node1(v))

#define VAL_HANDLE_LENGTH_U(v)          PAYLOAD(Any, (v)).second.u

#define VAL_HANDLE_CDATA_P(v)           EXTRA(Any, (v)).p
#define VAL_HANDLE_CFUNC_P(v)           EXTRA(Any, (v)).cfunc


INLINE bool Is_Handle_Cfunc(const Cell* v) {
    assert(Cell_Heart_Unchecked(v) == REB_HANDLE);
    return VAL_HANDLE_LENGTH_U(v) == 0;
}

INLINE const Cell* VAL_HANDLE_CANON(const Cell* v) {
    assert(Cell_Heart_Unchecked(v) == REB_HANDLE);
    if (Not_Cell_Flag_Unchecked(v, FIRST_IS_NODE))
        return v;  // changing handle instance won't be seen by copies
    return Stub_Cell(VAL_HANDLE_STUB(v));  // has shared node
}

INLINE Cell* mutable_VAL_HANDLE_CANON(Cell* v) {
    assert(Cell_Heart_Unchecked(v) == REB_HANDLE);
    if (Not_Cell_Flag_Unchecked(v, FIRST_IS_NODE))
        return v;  // changing handle instance won't be seen by copies
    return Stub_Cell(VAL_HANDLE_STUB(v));  // has shared node
}

INLINE uintptr_t VAL_HANDLE_LEN(const Cell* v) {
    assert(not Is_Handle_Cfunc(v));
    return VAL_HANDLE_LENGTH_U(VAL_HANDLE_CANON(v));
}

INLINE void *VAL_HANDLE_VOID_POINTER(const Cell* v) {
    assert(not Is_Handle_Cfunc(v));
    return VAL_HANDLE_CDATA_P(VAL_HANDLE_CANON(v));
}

#define VAL_HANDLE_POINTER(T, v) \
    cast(T*, VAL_HANDLE_VOID_POINTER(v))

INLINE CFunction* VAL_HANDLE_CFUNC(const Cell* v) {
    assert(Is_Handle_Cfunc(v));
    return VAL_HANDLE_CFUNC_P(VAL_HANDLE_CANON(v));
}

INLINE CLEANUP_CFUNC *VAL_HANDLE_CLEANER(const Cell* v) {
    assert(Cell_Heart_Unchecked(v) == REB_HANDLE);
    if (Not_Cell_Flag_Unchecked(v, FIRST_IS_NODE))
        return nullptr;
    return VAL_HANDLE_STUB(v)->misc.cleaner;
}

INLINE void SET_HANDLE_LEN(Cell* v, uintptr_t length)
  { VAL_HANDLE_LENGTH_U(mutable_VAL_HANDLE_CANON(v)) = length; }

INLINE void SET_HANDLE_CDATA(Cell* v, void *cdata) {
    Cell* canon = mutable_VAL_HANDLE_CANON(v);
    assert(VAL_HANDLE_LENGTH_U(canon) != 0);
    VAL_HANDLE_CDATA_P(canon) = cdata;
}

INLINE void SET_HANDLE_CFUNC(Cell* v, CFunction* cfunc) {
    assert(Is_Handle_Cfunc(v));
    Cell* canon = mutable_VAL_HANDLE_CANON(v);
    assert(VAL_HANDLE_LENGTH_U(canon) == 0);
    VAL_HANDLE_CFUNC_P(canon) = cfunc;
}

INLINE Value* Init_Handle_Cdata(
    Cell* out,
    void *cdata,
    uintptr_t length
){
    assert(length != 0);  // can't be 0 unless cfunc (see also malloc(0))
    Reset_Unquoted_Header_Untracked(
        out,
        FLAG_HEART_BYTE(REB_HANDLE) | CELL_MASK_NO_NODES
    );
  #ifdef ZERO_UNUSED_CELL_FIELDS
    PAYLOAD(Any, out).first.corrupt = CORRUPTZERO;
  #endif
    VAL_HANDLE_CDATA_P(out) = cdata;
    VAL_HANDLE_LENGTH_U(out) = length;  // non-zero signals cdata
    return cast(Value*, out);
}

INLINE Value* Init_Handle_Cfunc(
    Cell* out,
    CFunction* cfunc
){
    Reset_Unquoted_Header_Untracked(
        out,
        FLAG_HEART_BYTE(REB_HANDLE) | CELL_MASK_NO_NODES
    );
  #ifdef ZERO_UNUSED_CELL_FIELDS
    PAYLOAD(Any, out).first.corrupt = CORRUPTZERO;
  #endif
    VAL_HANDLE_CFUNC_P(out) = cfunc;
    VAL_HANDLE_LENGTH_U(out) = 0;  // signals cfunc
    return cast(Value*, out);
}

INLINE void Init_Handle_Managed_Common(
    Cell* out,
    uintptr_t length,
    CLEANUP_CFUNC *cleaner
){
    Array* singular = Alloc_Singular(FLAG_FLAVOR(HANDLE) | NODE_FLAG_MANAGED);
    singular->misc.cleaner = cleaner;

    Cell* single = Stub_Cell(singular);
    Reset_Unquoted_Header_Untracked(
        single,
        FLAG_HEART_BYTE(REB_HANDLE) | CELL_FLAG_FIRST_IS_NODE
    );
    INIT_VAL_HANDLE_STUB(single, singular);
    VAL_HANDLE_LENGTH_U(single) = length;
    // caller fills in VAL_HANDLE_CDATA_P or VAL_HANDLE_CFUNC_P

    // Don't fill the handle properties in the instance if it's the managed
    // form.  This way, you can set the properties in the canon value and
    // effectively update all instances...since the bits live in the shared
    // series component.
    //
    Reset_Unquoted_Header_Untracked(
        out,
        FLAG_HEART_BYTE(REB_HANDLE) | CELL_FLAG_FIRST_IS_NODE
    );
    INIT_VAL_HANDLE_STUB(out, singular);

    VAL_HANDLE_LENGTH_U(out) = 0xDECAFBAD;  // corrupt avoids compiler warning
    VAL_HANDLE_CDATA_P(out) = nullptr;  // or complains about not initializing
}

INLINE Value* Init_Handle_Cdata_Managed(
    Cell* out,
    void *cdata,
    uintptr_t length,
    CLEANUP_CFUNC *cleaner
){
    Init_Handle_Managed_Common(out, length, cleaner);

    // Leave the non-singular cfunc corrupt; clients should not be using

    Stub* stub = VAL_HANDLE_STUB(out);
    VAL_HANDLE_CDATA_P(Stub_Cell(stub)) = cdata;
    return cast(Value*, out);
}

INLINE Value* Init_Handle_Cdata_Managed_Cfunc(
    Cell* out,
    CFunction* cfunc,
    CLEANUP_CFUNC *cleaner
){
    Init_Handle_Managed_Common(out, 0, cleaner);

    // Leave the non-singular cfunc corrupt; clients should not be using

    Stub* stub = VAL_HANDLE_STUB(out);
    VAL_HANDLE_CFUNC_P(Stub_Cell(stub)) = cfunc;
    return cast(Value*, out);
}
