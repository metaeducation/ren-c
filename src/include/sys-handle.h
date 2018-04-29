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
// a single REBSER node shared among its instances, which is a "singular"
// Array containing a canon value of the handle itself.  When there are no
// references left to the handle and the GC runs, it will run a hook stored
// in the ->misc field of the singular array.
//
// As an added benefit of the Managed form, the code and data pointers in the
// value itself are not used; instead preferring the data held in the REBARR.
// This allows one instance of a managed handle to have its code or data
// pointer changed and be reflected in all instances.  The simple form of
// handle however is such that each REBVAL copied instance is independent,
// and changing one won't change the others.
//

#ifdef NDEBUG
    #define HANDLE_FLAG(n) \
        FLAGIT_LEFT(TYPE_SPECIFIC_BIT + (n))
#else
    #define HANDLE_FLAG(n) \
        (FLAGIT_LEFT(TYPE_SPECIFIC_BIT + (n)) | HEADERIZE_KIND(REB_HANDLE))
#endif

// Note: In the C language, sizeof(void*) may not be the same size as a
// function pointer; hence they can't necessarily be cast between each other.
// In practice, a void* is generally big enough to hold a CFUNC*, and many
// APIs do assume this.
//
#define HANDLE_FLAG_CFUNC HANDLE_FLAG(0)


inline static uintptr_t VAL_HANDLE_LEN(const RELVAL *v) {
    assert(IS_HANDLE(v));
    if (v->extra.singular)
        return ARR_HEAD(v->extra.singular)->payload.handle.length;
    else
        return v->payload.handle.length;
}

inline static void *VAL_HANDLE_VOID_POINTER(const RELVAL *v) {
    assert(IS_HANDLE(v));
    assert(NOT_VAL_FLAG(v, HANDLE_FLAG_CFUNC));
    if (v->extra.singular)
        return ARR_HEAD(v->extra.singular)->payload.handle.data.pointer;
    else
        return v->payload.handle.data.pointer;
}

#define VAL_HANDLE_POINTER(t, v) \
    cast(t *, VAL_HANDLE_VOID_POINTER(v))

inline static CFUNC *VAL_HANDLE_CFUNC(const RELVAL *v) {
    assert(IS_HANDLE(v));
    assert(GET_VAL_FLAG(v, HANDLE_FLAG_CFUNC));
    if (v->extra.singular)
        return ARR_HEAD(v->extra.singular)->payload.handle.data.cfunc;
    else
        return v->payload.handle.data.cfunc;
}

inline static CLEANUP_CFUNC VAL_HANDLE_CLEANER(const RELVAL *v) {
    assert(IS_HANDLE(v));
    REBARR *singular = v->extra.singular;
    return singular != NULL ? MISC(singular).cleaner : NULL;
}

inline static void SET_HANDLE_LEN(RELVAL *v, uintptr_t length) {
    assert(IS_HANDLE(v));
    if (v->extra.singular)
        ARR_HEAD(v->extra.singular)->payload.handle.length = length;
    else
        v->payload.handle.length = length;
}

inline static void SET_HANDLE_POINTER(RELVAL *v, void *pointer) {
    assert(IS_HANDLE(v));
    assert(NOT_VAL_FLAG(v, HANDLE_FLAG_CFUNC));
    if (v->extra.singular)
        ARR_HEAD(v->extra.singular)->payload.handle.data.pointer = pointer;
    else
        v->payload.handle.data.pointer = pointer;
}

inline static void SET_HANDLE_CFUNC(RELVAL *v, CFUNC *cfunc) {
    assert(IS_HANDLE(v));
    assert(GET_VAL_FLAG(v, HANDLE_FLAG_CFUNC));
    if (v->extra.singular)
        ARR_HEAD(v->extra.singular)->payload.handle.data.cfunc = cfunc;
    else
        v->payload.handle.data.cfunc = cfunc;
}

inline static REBVAL *Init_Handle_Simple(
    RELVAL *out,
    void *pointer,
    uintptr_t length
){
    RESET_VAL_HEADER(out, REB_HANDLE);
    out->extra.singular = NULL;
    out->payload.handle.data.pointer = pointer;
    out->payload.handle.length = length;
    return KNOWN(out);
}

inline static REBVAL *Init_Handle_Cfunc(
    RELVAL *out,
    CFUNC *cfunc,
    uintptr_t length
){
    RESET_VAL_HEADER_EXTRA(out, REB_HANDLE, HANDLE_FLAG_CFUNC);
    out->extra.singular = NULL;
    out->payload.handle.data.cfunc = cfunc;
    out->payload.handle.length = length;
    return KNOWN(out);
}

inline static void Init_Handle_Managed_Common(
    RELVAL *out,
    uintptr_t length,
    CLEANUP_CFUNC cleaner
){
    REBARR *singular = Alloc_Singular_Array();
    MISC(singular).cleaner = cleaner;

    RELVAL *v = ARR_HEAD(singular);
    v->extra.singular = singular; 
    v->payload.handle.length = length;

    // Caller will fill in whichever field is needed.  Note these are both
    // the same union member, so trashing them both is semi-superfluous, but
    // serves a commentary purpose here.
    //
    TRASH_POINTER_IF_DEBUG(v->payload.handle.data.pointer);
    TRASH_CFUNC_IF_DEBUG(v->payload.handle.data.cfunc);

    MANAGE_ARRAY(singular);

    // Don't fill the handle properties in the instance if it's the managed
    // form.  This way, you can set the properties in the canon value and
    // effectively update all instances...since the bits live in the shared
    // series component.
    //
    TRASH_CELL_IF_DEBUG(out);
    RESET_VAL_HEADER(out, REB_HANDLE);
    out->extra.singular = singular;
    TRASH_POINTER_IF_DEBUG(out->payload.handle.data.pointer);
}

inline static REBVAL *Init_Handle_Managed(
    RELVAL *out,
    void *pointer,
    uintptr_t length,
    CLEANUP_CFUNC cleaner
){
    Init_Handle_Managed_Common(out, length, cleaner);

    // Leave the non-singular cfunc as trash; clients should not be using
    //
    RESET_VAL_HEADER(out, REB_HANDLE);

    RESET_VAL_HEADER(ARR_HEAD(out->extra.singular), REB_HANDLE);
    ARR_HEAD(out->extra.singular)->payload.handle.data.pointer = pointer;
    return KNOWN(out);
}

inline static REBVAL *Init_Handle_Managed_Cfunc(
    RELVAL *out,
    CFUNC *cfunc,
    uintptr_t length,
    CLEANUP_CFUNC cleaner
){
    Init_Handle_Managed_Common(out, length, cleaner);

    // Leave the non-singular cfunc as trash; clients should not be using
    //
    RESET_VAL_HEADER_EXTRA(out, REB_HANDLE, HANDLE_FLAG_CFUNC);
    
    RESET_VAL_HEADER_EXTRA(
        ARR_HEAD(out->extra.singular),
        REB_HANDLE,
        HANDLE_FLAG_CFUNC
    );
    ARR_HEAD(out->extra.singular)->payload.handle.data.cfunc = cfunc;
    return KNOWN(out);
}
