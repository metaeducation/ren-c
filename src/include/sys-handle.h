//
//  File: %sys-handle.h
//  Summary: "Definitions for GC-able and non-GC-able Handles"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// In Rebol terminology, a HANDLE! is a pointer to a function or data that
// represents an arbitrary external resource.  While such data could also
// be encoded as a BINARY! "blob" (as it might be in XML), the HANDLE! type
// is intentionally "opaque" to user code so that it is a black box.
//
// Additionally, Ren-C added the idea of a garbage collector callback for
// "Managed" handles.  This is implemented by means of making the handle cost
// a single Stub node shared among its instances, which is a "singular"
// Array containing a canon value of the handle itself.  When there are no
// references left to the handle and the GC runs, it will run a hook stored
// in the ->misc field of the singular array.
//
// As an added benefit of the Managed form, the code and data pointers in the
// value itself are not used; instead preferring the data held in the Array.
// This allows one instance of a managed handle to have its code or data
// pointer changed and be reflected in all instances.  The simple form of
// handle however is such that each cell copied instance is independent,
// and changing one won't change the others.
//

// Note: In the C language, sizeof(void*) may not be the same size as a
// function pointer; hence they can't necessarily be cast between each other.
// In practice, a void* is generally big enough to hold a CFUNC*, and many
// APIs do assume this.
//
#define CELL_FLAG_HANDLE_CFUNC FLAG_TYPE_SPECIFIC_BIT(0)


INLINE uintptr_t VAL_HANDLE_LEN(const Cell* v) {
    assert(Is_Handle(v));
    if (v->extra.singular)
        return Array_Head(v->extra.singular)->payload.handle.length;
    else
        return v->payload.handle.length;
}

INLINE void *VAL_HANDLE_VOID_POINTER(const Cell* v) {
    assert(Is_Handle(v));
    assert(Not_Cell_Flag(v, HANDLE_CFUNC));
    if (v->extra.singular)
        return Array_Head(v->extra.singular)->payload.handle.data.pointer;
    else
        return v->payload.handle.data.pointer;
}

#define VAL_HANDLE_POINTER(t, v) \
    cast(t *, VAL_HANDLE_VOID_POINTER(v))

INLINE CFUNC *VAL_HANDLE_CFUNC(const Cell* v) {
    assert(Is_Handle(v));
    assert(Get_Cell_Flag(v, HANDLE_CFUNC));
    if (v->extra.singular)
        return Array_Head(v->extra.singular)->payload.handle.data.cfunc;
    else
        return v->payload.handle.data.cfunc;
}

INLINE CLEANUP_CFUNC *VAL_HANDLE_CLEANER(const Cell* v) {
    assert(Is_Handle(v));
    Array* singular = v->extra.singular;
    return singular != nullptr ? MISC(singular).cleaner : nullptr;
}

INLINE void SET_HANDLE_LEN(Cell* v, uintptr_t length) {
    assert(Is_Handle(v));
    if (v->extra.singular)
        Array_Head(v->extra.singular)->payload.handle.length = length;
    else
        v->payload.handle.length = length;
}

INLINE void SET_HANDLE_POINTER(Cell* v, void *pointer) {
    assert(Is_Handle(v));
    assert(Not_Cell_Flag(v, HANDLE_CFUNC));
    if (v->extra.singular)
        Array_Head(v->extra.singular)->payload.handle.data.pointer = pointer;
    else
        v->payload.handle.data.pointer = pointer;
}

INLINE void SET_HANDLE_CFUNC(Cell* v, CFUNC *cfunc) {
    assert(Is_Handle(v));
    assert(Get_Cell_Flag(v, HANDLE_CFUNC));
    if (v->extra.singular)
        Array_Head(v->extra.singular)->payload.handle.data.cfunc = cfunc;
    else
        v->payload.handle.data.cfunc = cfunc;
}

INLINE Value* Init_Handle_Simple(
    Cell* out,
    void *pointer,
    uintptr_t length
){
    RESET_CELL(out, TYPE_HANDLE);
    out->extra.singular = nullptr;
    out->payload.handle.data.pointer = pointer;
    out->payload.handle.length = length;
    return KNOWN(out);
}

INLINE Value* Init_Handle_Cfunc(
    Cell* out,
    CFUNC *cfunc,
    uintptr_t length
){
    Reset_Cell_Header(out, TYPE_HANDLE, CELL_FLAG_HANDLE_CFUNC);
    out->extra.singular = nullptr;
    out->payload.handle.data.cfunc = cfunc;
    out->payload.handle.length = length;
    return KNOWN(out);
}

INLINE void Init_Handle_Managed_Common(
    Cell* out,
    uintptr_t length,
    CLEANUP_CFUNC *cleaner
){
    Array* singular = Alloc_Singular(NODE_FLAG_MANAGED);
    MISC(singular).cleaner = cleaner;

    Cell* v = Array_Head(singular);
    v->extra.singular = singular;
    v->payload.handle.length = length;

    // Caller will fill in whichever field is needed.  Note these are both
    // the same union member, so trashing them both is semi-superfluous, but
    // serves a commentary purpose here.
    //
    Corrupt_Pointer_If_Debug(v->payload.handle.data.pointer);
    Corrupt_CFunction_If_Debug(v->payload.handle.data.cfunc);

    // Don't fill the handle properties in the instance if it's the managed
    // form.  This way, you can set the properties in the canon value and
    // effectively update all instances...since the bits live in the shared
    // series component.
    //
    RESET_CELL(out, TYPE_HANDLE);
    out->extra.singular = singular;
    Corrupt_Pointer_If_Debug(out->payload.handle.data.pointer);
}

INLINE Value* Init_Handle_Managed(
    Cell* out,
    void *pointer,
    uintptr_t length,
    CLEANUP_CFUNC *cleaner
){
    Init_Handle_Managed_Common(out, length, cleaner);

    // Leave the non-singular cfunc as corrupt; clients should not be using
    //
    Reset_Cell_Header(out, TYPE_HANDLE, 0);

    RESET_CELL(Array_Head(out->extra.singular), TYPE_HANDLE);
    Array_Head(out->extra.singular)->payload.handle.data.pointer = pointer;
    return KNOWN(out);
}

INLINE Value* Init_Handle_Managed_Cfunc(
    Cell* out,
    CFUNC *cfunc,
    uintptr_t length,
    CLEANUP_CFUNC *cleaner
){
    Init_Handle_Managed_Common(out, length, cleaner);

    // Leave the non-singular cfunc as corrupt; clients should not be using
    //
    Reset_Cell_Header(out, TYPE_HANDLE, CELL_FLAG_HANDLE_CFUNC);

    Reset_Cell_Header(
        Array_Head(out->extra.singular),
        TYPE_HANDLE,
        CELL_FLAG_HANDLE_CFUNC
    );
    Array_Head(out->extra.singular)->payload.handle.data.cfunc = cfunc;
    return KNOWN(out);
}
