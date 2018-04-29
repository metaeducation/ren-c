//
//  File: %mod-ffi.c
//  Summary: "Foreign function interface main C file"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 Atronix Engineering
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

#include "sys-core.h"
#include "sys-ext.h"

#include "tmp-mod-ffi-first.h"

#include "reb-struct.h"

// There is a platform-dependent list of legal ABIs which the MAKE-ROUTINE
// and MAKE-CALLBACK natives take as an option via refinement
//
static ffi_abi Abi_From_Word(const REBVAL *word) {
    switch (VAL_WORD_SYM(word)) {
    case SYM_DEFAULT:
        return FFI_DEFAULT_ABI;

#ifdef X86_WIN64
    case SYM_WIN64:
        return FFI_WIN64;

#elif defined(X86_WIN32) || defined(TO_LINUX_X86) || defined(TO_LINUX_X64)
    case SYM_STDCALL:
        return FFI_STDCALL;

    case SYM_SYSV:
        return FFI_SYSV;

    case SYM_THISCALL:
        return FFI_THISCALL;

    case SYM_FASTCALL:
        return FFI_FASTCALL;

#ifdef X86_WIN32
    case SYM_MS_CDECL:
        return FFI_MS_CDECL;
#else
    case SYM_UNIX64:
        return FFI_UNIX64;
#endif //X86_WIN32

#elif defined (TO_LINUX_ARM)
    case SYM_VFP:
        return FFI_VFP;

    case SYM_SYSV:
        return FFI_SYSV;

#elif defined (TO_LINUX_MIPS)
    case SYM_O32:
        return FFI_O32;

    case SYM_N32:
        return FFI_N32;

    case SYM_N64:
        return FFI_N64;

    case SYM_O32_SOFT_FLOAT:
        return FFI_O32_SOFT_FLOAT;

    case SYM_N32_SOFT_FLOAT:
        return FFI_N32_SOFT_FLOAT;

    case SYM_N64_SOFT_FLOAT:
        return FFI_N64_SOFT_FLOAT;
#endif //X86_WIN64

    default:
        break;
    }

    fail (Error_Invalid(word));
}


//
//  make-routine: native/export [
//
//  {Create a bridge for interfacing with arbitrary C code in a DLL}
//
//      return: [action!]
//      lib [library!]
//          {Library DLL that C function lives in (get with MAKE LIBRARY!)}
//      name [string!]
//          {Linker name of the C function in the DLL}
//      ffi-spec [block!]
//          {Description of what C argument types the C function takes}
//      /abi
//          {Specify the Application Binary Interface (vs. using default)}
//      abi-type [word!]
//          {'CDECL, 'FASTCALL, 'STDCALL, etc.}
//  ]
//
REBNATIVE(make_routine)
//
// !!! Would be nice if this could just take a filename and the lib management
// was automatic, e.g. no LIBRARY! type.
{
    FFI_INCLUDE_PARAMS_OF_MAKE_ROUTINE;

    ffi_abi abi;
    if (REF(abi))
        abi = Abi_From_Word(ARG(abi_type));
    else
        abi = FFI_DEFAULT_ABI;

    // Make sure library wasn't closed with CLOSE
    //
    REBLIB *lib = VAL_LIBRARY(ARG(lib));
    if (lib == NULL)
        fail (Error_Invalid(ARG(lib)));

    // OS_FIND_FUNCTION takes a char* on both Windows and Posix.
    //
    // !!! Should it error if any bytes aren't ASCII?
    //
    REBSIZ offset;
    REBSIZ size;
    REBSER *temp = Temp_UTF8_At_Managed(
        &offset, &size, ARG(name), VAL_LEN_AT(ARG(name))
    );

    CFUNC *cfunc = OS_FIND_FUNCTION(
        LIB_FD(lib),
        SER_AT(char, temp, offset) // name may not be at head index
    );
    if (cfunc == NULL)
        fail ("FFI: Couldn't find function in library");

    // Process the parameter types into a function, then fill it in

    REBACT *routine = Alloc_Ffi_Action_For_Spec(ARG(ffi_spec), abi);
    REBRIN *r = VAL_ARRAY(ACT_BODY(routine));

    Init_Handle_Cfunc(RIN_AT(r, IDX_ROUTINE_CFUNC), cfunc, 0);
    Move_Value(RIN_AT(r, IDX_ROUTINE_ORIGIN), ARG(lib));

    Move_Value(D_OUT, ACT_ARCHETYPE(routine));
    return R_OUT;
}


//
//  make-routine-raw: native/export [
//
//  {Create a bridge for interfacing with a C function, by pointer}
//
//      return: [action!]
//      pointer [integer!]
//          {Raw address of C function in memory}
//      ffi-spec [block!]
//          {Description of what C argument types the C function takes}
//      /abi
//          {Specify the Application Binary Interface (vs. using default)}
//      abi-type [word!]
//          {'CDECL, 'FASTCALL, 'STDCALL, etc.}
//  ]
//
REBNATIVE(make_routine_raw)
//
// !!! Would be nice if this could just take a filename and the lib management
// was automatic, e.g. no LIBRARY! type.
{
    FFI_INCLUDE_PARAMS_OF_MAKE_ROUTINE_RAW;

    ffi_abi abi;
    if (REF(abi))
        abi = Abi_From_Word(ARG(abi_type));
    else
        abi = FFI_DEFAULT_ABI;

    // Cannot cast directly to a function pointer from a 64-bit value
    // on 32-bit systems.
    //
    CFUNC *cfunc = cast(CFUNC*, cast(uintptr_t, VAL_INT64(ARG(pointer))));
    if (cfunc == NULL)
        fail ("FFI: NULL pointer not allowed for raw MAKE-ROUTINE");

    REBACT *routine = Alloc_Ffi_Action_For_Spec(ARG(ffi_spec), abi);
    REBRIN *r = VAL_ARRAY(ACT_BODY(routine));

    Init_Handle_Cfunc(RIN_AT(r, IDX_ROUTINE_CFUNC), cfunc, 0);
    Init_Blank(RIN_AT(r, IDX_ROUTINE_ORIGIN)); // no LIBRARY! in this case.

    Move_Value(D_OUT, ACT_ARCHETYPE(routine));
    return R_OUT;
}


//
//  wrap-callback: native/export [
//
//  {Wrap an ACTION! so it can be called by raw C code via a memory address.}
//
//      return: [action!]
//      action [action!]
//          {The existing Rebol action whose behavior is being wrapped}
//      ffi-spec [block!]
//          {Description of what C types each Rebol argument should map to}
//      /abi
//          {Specify the Application Binary Interface (vs. using default)}
//      abi-type [word!]
//          {'CDECL, 'FASTCALL, 'STDCALL, etc.}
//  ]
//
REBNATIVE(wrap_callback)
{
    FFI_INCLUDE_PARAMS_OF_WRAP_CALLBACK;

    ffi_abi abi;
    if (REF(abi))
        abi = Abi_From_Word(ARG(abi_type));
    else
        abi = FFI_DEFAULT_ABI;

    REBACT *callback = Alloc_Ffi_Action_For_Spec(ARG(ffi_spec), abi);
    REBRIN *r = VAL_ARRAY(ACT_BODY(callback));

    void *thunk; // actually CFUNC (FFI uses void*, may not be same size!)
    ffi_closure *closure = cast(ffi_closure*, ffi_closure_alloc(
        sizeof(ffi_closure), &thunk
    ));

    if (closure == NULL)
        fail ("FFI: Couldn't allocate closure");

    ffi_status status = ffi_prep_closure_loc(
        closure,
        RIN_CIF(r),
        callback_dispatcher, // when thunk is called it calls this function...
        r, // ...and this piece of data is passed to callback_dispatcher
        thunk
    );

    if (status != FFI_OK)
        fail ("FFI: Couldn't prep closure");

    REBOOL check = TRUE; // avoid "conditional expression is constant"
    if (check && sizeof(void*) != sizeof(CFUNC*))
        fail ("FFI does not work when void* size differs from CFUNC* size");

    // It's the FFI's fault for using the wrong type for the thunk.  Use a
    // memcpy in order to get around strict checks that absolutely refuse to
    // let you do a cast here.
    //
    CFUNC *cfunc_thunk;
    memcpy(&cfunc_thunk, &thunk, sizeof(cfunc_thunk));

    Init_Handle_Cfunc(RIN_AT(r, IDX_ROUTINE_CFUNC), cfunc_thunk, 0);
    Init_Handle_Managed(
        RIN_AT(r, IDX_ROUTINE_CLOSURE),
        closure,
        0,
        &cleanup_ffi_closure
    );
    Move_Value(RIN_AT(r, IDX_ROUTINE_ORIGIN), ARG(action));

    Move_Value(D_OUT, ACT_ARCHETYPE(callback));
    return R_OUT;
}


//
//  addr-of: native/export [
//
//  {Get the memory address of an FFI STRUCT! or routine/callback}
//
//      return: [integer!]
//          {Memory address expressed as an up-to-64-bit integer}
//      value [action! struct!]
//          {Fixed address structure or routine to get the address of}
//  ]
//
REBNATIVE(addr_of) {
    FFI_INCLUDE_PARAMS_OF_ADDR_OF;

    REBVAL *v = ARG(value);

    if (IS_ACTION(v)) {
        if (!IS_ACTION_RIN(v))
            fail ("Can only take address of ACTION!s created though FFI");

        // The CFUNC is fabricated by the FFI if it's a callback, or
        // just the wrapped DLL function if it's an ordinary routine
        //
        Init_Integer(
            D_OUT, cast(intptr_t, RIN_CFUNC(VAL_ACT_ROUTINE(v)))
        );
        return R_OUT;
    }

    assert(IS_STRUCT(v));

    // !!! If a structure wasn't mapped onto "raw-memory" from the C,
    // then currently the data for that struct is a BINARY!, not a handle to
    // something which was malloc'd.  Much of the system is designed to be
    // able to handle memory relocations of a series data, but if a pointer is
    // given to code it may expect that address to be permanent.  Data
    // pointers currently do not move (e.g. no GC compaction) unless there is
    // a modification to the series, but this may change...in which case a
    // "do not move in memory" bit would be needed for the BINARY! or a
    // HANDLE! to a non-moving malloc would need to be used instead.
    //
    Init_Integer(D_OUT, cast(intptr_t, VAL_STRUCT_DATA_AT(v)));
    return R_OUT;
}


//
//  make-similar-struct: native/export [
//
//  "Create a STRUCT! that reuses the underlying spec of another STRUCT!"
//
//      return: [struct!]
//      spec [struct!]
//          "Struct with interface to copy"
//      body [block! any-context! blank!]
//          "keys and values defining instance contents (bindings modified)"
//  ]
//
REBNATIVE(make_similar_struct)
//
// !!! Compatibility for `MAKE some-struct [...]` from Atronix R3.  There
// isn't any real "inheritance management" for structs, but it allows the
// re-use of the structure's field definitions, so it is a means of saving on
// memory (?)  Code retained for examination.
{
    FFI_INCLUDE_PARAMS_OF_MAKE_SIMILAR_STRUCT;

    REBVAL *spec = ARG(spec);
    REBVAL *body = ARG(body);

    REBSTU *stu = Copy_Struct_Managed(VAL_STRUCT(spec));

    Move_Value(D_OUT, STU_VALUE(stu));

    // !!! Comment said "only accept value initialization"
    //
    Init_Struct_Fields(D_OUT, body);
    return R_OUT;
}


//
//  destroy-struct-storage: native [
//
//  {Destroy the external memory associated the struct}
//
//      struct [struct!]
//      /free
//          {Specify the function to free the memory}
//      free-func [action!]
//  ]
//
REBNATIVE(destroy_struct_storage)
{
    FFI_INCLUDE_PARAMS_OF_DESTROY_STRUCT_STORAGE;

    REBSER *data = ARG(struct)->payload.structure.data;
    if (NOT_SER_FLAG(data, SERIES_FLAG_ARRAY))
        fail (Error_No_External_Storage_Raw());

    RELVAL *handle = ARR_HEAD(ARR(data));

    DECLARE_LOCAL (pointer);
    Init_Integer(pointer, cast(intptr_t, VAL_HANDLE_POINTER(void, handle)));

    if (VAL_HANDLE_LEN(handle) == 0)
        fail (Error_Already_Destroyed_Raw(pointer));

    // TBD: assert handle length was correct for memory block size

    SET_HANDLE_LEN(handle, 0);

    if (REF(free)) {
        if (!IS_ACTION_RIN(ARG(free_func)))
            fail (Error_Free_Needs_Routine_Raw());

        if (Do_Va_Throws(D_OUT, ARG(free_func), pointer, END))
            return R_OUT_IS_THROWN;
    }

    return R_VOID;
}


//
//  alloc-value-pointer: native/export [
//
//  {Persistently allocate a cell that can be referenced from FFI routines}
//
//      return: [integer!]
//      value [<opt> any-value!]
//          {Initial value for the cell}
//  ]
//
REBNATIVE(alloc_value_pointer)
//
// !!! Would it be better to not bother with the initial value parameter and
// just start the cell out void?
{
    FFI_INCLUDE_PARAMS_OF_ALLOC_VALUE_POINTER;

    REBVAL *paired = Alloc_Pairing(); // no owning frame
    Move_Value(paired, ARG(value));

    // We didn't put a FRAME! in the pairing's key, so instead put a blank.
    // Also, it is not managed...but we want the GC to mark the pairing,
    // so add NODE_FLAG_ROOT.
    //
    // (If it were managed, then any non-END, non-expired-FRAME! value here
    // would prevent GC.  For now, go with unmanaged in order to make any
    // leaks be "noisy".)
    //
    REBVAL *key = PAIRING_KEY(paired);
    Init_Blank(key);
    SET_VAL_FLAG(key, NODE_FLAG_ROOT);

    Init_Integer(D_OUT, cast(intptr_t, paired));
    return R_OUT;
}


//
//  free-value-pointer: native/export [
//
//  {Free a cell that was allocated by ALLOC-VALUE-POINTER}
//
//      return: [<opt>]
//      pointer [integer!]
//  ]
//
REBNATIVE(free_value_pointer)
{
    FFI_INCLUDE_PARAMS_OF_FREE_VALUE_POINTER;

    REBVAL *paired = cast(REBVAL*, cast(intptr_t, VAL_INT64(ARG(pointer))));

    // Check some invariants that should be true if this is the kind of
    // value pointer that can be freed.
    //
    // !!! Should these be included in the release build and trigger an error
    // in order to make the system a bit more crashproof?  It wouldn't be
    // 100%, but it might save some headaches.
    //
#if !defined(NDEBUG)
    REBVAL *key = PAIRING_KEY(paired);
    assert(ALL_VAL_FLAGS(paired, NODE_FLAG_NODE | NODE_FLAG_CELL));
    assert(ALL_VAL_FLAGS(key, NODE_FLAG_NODE | NODE_FLAG_CELL));
    assert(IS_BLANK(key));
    assert(GET_VAL_FLAG(key, NODE_FLAG_ROOT));
#endif

    // Although currently unmanaged pairings are used, it would also be
    // possible to use a managed pairing.  Instead of calling Free_Pairing()
    // then the PAIRING_KEY() would need to be set to void, and the GC would
    // free it if there were no outstanding references.
    //
    // Currently there's no way to make GC-visible references to the returned
    // pointer.  So the only value of using a managed strategy would be to
    // have the GC clean up leaks on exit instead of complaining in the
    // debug build.  For now, assume complaining is better.
    //
    Free_Pairing(paired);
    return R_VOID;
}


//
//  get-at-pointer: native/export [
//
//  {Get the contents of a cell, e.g. one returned by ALLOC-VALUE-POINTER}
//
//      return: [<opt> any-value!]
//          {If the source looks up to a value, that value--else blank}
//      source [integer!]
//          {A pointer to a Rebol value}
//      /only
//          {Return void if no value instead of blank}
//  ]
//
REBNATIVE(get_at_pointer)
//
// !!! In an ideal future, the FFI would probably add a user-defined-type
// for a POINTER!, and then GET could be overloaded to work with it.  No
// such mechanisms have been designed yet.  In the meantime, the interface
// for GET-AT-POINTER should not deviate too far from GET.
{
    FFI_INCLUDE_PARAMS_OF_GET_AT_POINTER;

    REBVAL *paired = cast(REBVAL*, cast(intptr_t, VAL_INT64(ARG(source))));
    if (IS_VOID(paired) && !REF(only))
        Init_Blank(D_OUT);
    else
        Move_Value(D_OUT, paired);

    return R_OUT;
}


//
//  set-at-pointer: native/export [
//
//  {Set the contents of a cell, e.g. one returned by ALLOC-VALUE-POINTER}
//
//      return: [<opt> any-value!]
//          {Will be the value set to, or void if the set values is void}
//      target [integer!]
//          {A pointer to a Rebol value}
//      value [<opt> any-value!]
//          "Value to assign"
//      /only
//          {Treat void values as unsetting the target instead of an error}
//  ]
//
REBNATIVE(set_at_pointer)
//
// !!! See notes on GET-AT-POINTER about keeping interface roughly compatible
// with the SET native.
{
    FFI_INCLUDE_PARAMS_OF_SET_AT_POINTER;

    if (IS_VOID(ARG(value)) && !REF(only))
        fail (Error_No_Value(ARG(value)));

    REBVAL *paired = cast(REBVAL*, cast(intptr_t, VAL_INT64(ARG(target))));
    Move_Value(paired, ARG(value));

    Move_Value(D_OUT, ARG(value));
    return R_OUT;
}


#include "tmp-mod-ffi-last.h"
