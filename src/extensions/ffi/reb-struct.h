//
//  File: %reb-struct.h
//  Summary: "Struct to C function"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2014 Atronix Engineering, Inc.
// Copyright 2014-2017 Rebol Open Source Contributors
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


#include <ffi.h>


// Returns an ffi_type* (which contains a ->type field, that holds the
// FFI_TYPE_XXX enum).
//
// !!! In the original Atronix implementation this was done with a table
// indexed by FFI_TYPE_XXX constants.  But since those constants do not have
// guaranteed values or ordering, there was a parallel separate enum to use
// for indexing (STRUCT_TYPE_XXX).  Getting rid of the STRUCT_TYPE_XXX and
// just using a switch statement should effectively act as a table anyway
// if the SYM_XXX numbers are in sequence.  :-/
//
inline static ffi_type *Get_FFType_For_Sym(REBSYM sym) {
    switch (sym) {
    case SYM_UINT8:
        return &ffi_type_uint8;

    case SYM_INT8:
        return &ffi_type_sint8;

    case SYM_UINT16:
        return &ffi_type_uint16;

    case SYM_INT16:
        return &ffi_type_sint16;

    case SYM_UINT32:
        return &ffi_type_uint32;

    case SYM_INT32:
        return &ffi_type_sint32;

    case SYM_UINT64:
        return &ffi_type_uint64;

    case SYM_INT64:
        return &ffi_type_sint64;

    case SYM_FLOAT:
        return &ffi_type_float;

    case SYM_DOUBLE:
        return &ffi_type_double;

    case SYM_POINTER:
        return &ffi_type_pointer;

    case SYM_REBVAL:
        return &ffi_type_pointer;

    // !!! SYM_INTEGER, SYM_DECIMAL, SYM_STRUCT was "-1" in original table

    default:
        return NULL;
    }
}


//=////////////////////////////////////////////////////////////////////////=//
//
// FIELD (FLD) describing an FFI struct element
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A field is used by the FFI code to describe an element inside the layout
// of a C `struct`, so that Rebol data can be proxied to and from C.  It
// contains field type descriptions, dimensionality, and name of the field.
// It is implemented as a small BLOCK!, which should eventually be coupled
// with a keylist so it can be an easy-to-read OBJECT!
//

enum {
    // A WORD! name for the field (or BLANK! if anonymous ?)  What should
    // probably happen here is that structs should use a keylist for this;
    // though that would mean anonymous fields would not be legal.
    //
    IDX_FIELD_NAME = 0,

    // WORD! type symbol or a BLOCK! of fields if this is a struct.  Symbols
    // generally map to FFI_TYPE_XXX constant (e.g. UINT8) but may also
    // be a special extension, such as REBVAL.
    //
    IDX_FIELD_TYPE = 1,

    // An INTEGER! of the array dimensionality, or BLANK! if not an array.
    //
    IDX_FIELD_DIMENSION = 2,

    // HANDLE! to the ffi_type* representing this entire field.  If it's a
    // premade ffi_type then it's a simple HANDLE! with no GC participation.
    // If it's a struct then it will use the shared form of HANDLE!, which
    // will GC the memory pointed to when the last reference goes away.
    //
    IDX_FIELD_FFTYPE = 3,

    // An INTEGER! of the offset this field is relative to the beginning
    // of its entire containing structure.  Will be BLANK! if the structure
    // is actually the root structure itself.
    //
    // !!! Comment said "size is limited by struct->offset, so only 16-bit"?
    //
    IDX_FIELD_OFFSET = 4,

    // An INTEGER! size of an individual field element ("wide"), in bytes.
    //
    IDX_FIELD_WIDE = 5,

    IDX_FIELD_MAX
};

#define FLD_AT(a, n) \
    SER_AT(REBVAL, SER(a), (n)) // locate index access

inline static REBSTR *FLD_NAME(REBFLD *f) {
    if (IS_BLANK(FLD_AT(f, IDX_FIELD_NAME)))
        return NULL;
    return VAL_WORD_SPELLING(FLD_AT(f, IDX_FIELD_NAME));
}

inline static REBOOL FLD_IS_STRUCT(REBFLD *f) {
    if (IS_BLOCK(FLD_AT(f, IDX_FIELD_TYPE)))
        return TRUE;

    // Only top level struct schemas may have NULL names
    //
    assert(FLD_NAME(f) != NULL);
    return FALSE;
}

inline static REBSYM FLD_TYPE_SYM(REBFLD *f) {
    if (FLD_IS_STRUCT(f)) {
        //
        // We could return SYM_STRUCT_X for structs, but it's probably better
        // to have callers test FLD_IS_STRUCT() separately for clarity.
        //
        assert(FALSE);
        return SYM_STRUCT_X;
    }

    assert(IS_WORD(FLD_AT(f, IDX_FIELD_TYPE)));
    return VAL_WORD_SYM(FLD_AT(f, IDX_FIELD_TYPE));
}

inline static REBARR *FLD_FIELDLIST(REBFLD *f) {
    assert(FLD_IS_STRUCT(f));
    return VAL_ARRAY(FLD_AT(f, IDX_FIELD_TYPE));
}

inline static REBOOL FLD_IS_ARRAY(REBFLD *f) {
    if (IS_BLANK(FLD_AT(f, IDX_FIELD_DIMENSION)))
        return FALSE;
    assert(IS_INTEGER(FLD_AT(f, IDX_FIELD_DIMENSION)));
    return TRUE;
}

inline static REBCNT FLD_DIMENSION(REBFLD *f) {
    assert(FLD_IS_ARRAY(f));
    return VAL_UNT32(FLD_AT(f, IDX_FIELD_DIMENSION));
}

inline static ffi_type *FLD_FFTYPE(REBFLD *f)
    { return VAL_HANDLE_POINTER(ffi_type, FLD_AT(f, IDX_FIELD_FFTYPE)); }

inline static REBCNT FLD_OFFSET(REBFLD *f)
    { return VAL_UNT32(FLD_AT(f, IDX_FIELD_OFFSET)); }

inline static REBCNT FLD_WIDE(REBFLD *f)
    { return VAL_UNT32(FLD_AT(f, IDX_FIELD_WIDE)); }

inline static REBCNT FLD_LEN_BYTES_TOTAL(REBFLD *f) {
    if (FLD_IS_ARRAY(f))
        return FLD_WIDE(f) * FLD_DIMENSION(f);
    return FLD_WIDE(f);
}

inline static ffi_type* SCHEMA_FFTYPE(const RELVAL *schema) {
    if (IS_BLOCK(schema)) {
        REBFLD *field = VAL_ARRAY(schema);
        return FLD_FFTYPE(field);
    }

    // Avoid creating a "VOID" type in order to not give the illusion of
    // void parameters being legal.  The NONE! return type is handled
    // exclusively by the return value, to prevent potential mixups.
    //
    assert(IS_WORD(schema));
    return Get_FFType_For_Sym(VAL_WORD_SYM(schema));
}


#define VAL_STRUCT_LIMIT UINT32_MAX


//=////////////////////////////////////////////////////////////////////////=//
//
//  STRUCT! (`struct Reb_Struct`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Struct is a value type that is the the combination of a "schema" (field or
// list of fields) along with a blob of binary data described by that schema.
//

inline static REBVAL *STU_VALUE(REBSTU *stu) {
    assert(ARR_LEN(stu) == 1);
    return KNOWN(ARR_HEAD(stu));
}

#define STU_INACCESSIBLE(stu) \
    VAL_STRUCT_INACCESSIBLE(STU_VALUE(stu))

inline static REBFLD *STU_SCHEMA(REBSTU *stu) {
    REBFLD *schema = LINK(stu).schema;
    assert(FLD_IS_STRUCT(schema));
    return schema;
}

inline static REBARR *STU_FIELDLIST(REBSTU *stu) {
    return FLD_FIELDLIST(STU_SCHEMA(stu));
}

inline static REBCNT STU_SIZE(REBSTU *stu) {
    return FLD_WIDE(STU_SCHEMA(stu));
}

inline static REBCNT STU_OFFSET(REBSTU *stu) {
    return STU_VALUE(stu)->extra.struct_offset;
}

#define STU_FFTYPE(stu) \
    FLD_FFTYPE(STU_SCHEMA(stu))

#define VAL_STRUCT(v) \
    ((v)->payload.structure.stu)

#define VAL_STRUCT_SCHEMA(v) \
    STU_SCHEMA(VAL_STRUCT(v))

#define VAL_STRUCT_SIZE(v) \
    STU_SIZE(VAL_STRUCT(v))

inline static REBYTE *VAL_STRUCT_DATA_HEAD(const RELVAL *v) {
    REBSER *data = v->payload.structure.data;
    if (NOT_SER_FLAG(data, SERIES_FLAG_ARRAY))
        return BIN_HEAD(data);

    RELVAL *handle = ARR_HEAD(ARR(data));
    assert(VAL_HANDLE_LEN(handle) != 0);
    return VAL_HANDLE_POINTER(REBYTE, handle);
}

inline static REBYTE *STU_DATA_HEAD(REBSTU *stu) {
    return VAL_STRUCT_DATA_HEAD(STU_VALUE(stu));
}

#define VAL_STRUCT_OFFSET(v) \
    ((v)->extra.struct_offset)

inline static REBYTE *VAL_STRUCT_DATA_AT(const RELVAL *v) {
    return VAL_STRUCT_DATA_HEAD(v) + VAL_STRUCT_OFFSET(v);
}

inline static REBCNT VAL_STRUCT_DATA_LEN(const RELVAL *v) {
    REBSER *data = v->payload.structure.data;
    if (NOT_SER_FLAG(data, SERIES_FLAG_ARRAY))
        return BIN_LEN(data);

    RELVAL *handle = ARR_HEAD(ARR(data));
    assert(VAL_HANDLE_LEN(handle) != 0);
    return VAL_HANDLE_LEN(handle);
}

inline static REBCNT STU_DATA_LEN(REBSTU *stu) {
    return VAL_STRUCT_DATA_LEN(STU_VALUE(stu));
}

inline static REBOOL VAL_STRUCT_INACCESSIBLE(const RELVAL *v) {
    REBSER *data = v->payload.structure.data;
    if (NOT_SER_FLAG(data, SERIES_FLAG_ARRAY))
        return FALSE; // it's not "external", so never inaccessible

    RELVAL *handle = ARR_HEAD(ARR(data));
    if (VAL_HANDLE_LEN(handle) != 0)
        return FALSE; // !!! TBD: double check size is correct for mem block
    
    return TRUE;
}

#define VAL_STRUCT_FIELDLIST(v) \
    STU_FIELDLIST(VAL_STRUCT(v))

#define VAL_STRUCT_FFTYPE(v) \
    STU_FFTYPE(VAL_STRUCT(v))


//=////////////////////////////////////////////////////////////////////////=//
//
//  ROUTINE SUPPORT
//
//=////////////////////////////////////////////////////////////////////////=//
//
// "Routine info" used to be a specialized C structure, which referenced
// Rebol functions/values/series.  This meant there had to be specialized
// code in the garbage collector.  It actually went as far as to have a memory
// pool for objects that was sizeof(Reb_Routine_Info), which complicates the
// concerns further.
//
// That "invasive" approach is being gradually generalized to speak in the
// natural vocabulary of Rebol values.  What enables the transition is that
// arbitrary C allocations (such as an ffi_closure*) can use the new freeing
// handler feature of a GC'd HANDLE! value.  So now "routine info" is just
// a BLOCK! REBVAL*, which lives in the ACT_BODY of a routine, and has some
// HANDLE!s in it that array.
//
// !!! An additional benefit is that if the structures used internally
// are actual Rebol-manipulatable values, then that means more parts of the
// FFI extension itself could be written as Rebol.  e.g. the FFI spec analysis
// could be done with PARSE, as opposed to harder-to-edit-and-maintain
// internal API C code.
//
 
inline static REBRIN *VAL_ACT_ROUTINE(const RELVAL *v) {
    return VAL_ARRAY(VAL_ACT_BODY(v));
}

enum {
    // The HANDLE! of a CFUNC*, obeying the interface of the C-format call.
    // If it's a routine, then it's the pointer to a pre-existing function
    // in the DLL that the routine intends to wrap.  If a callback, then
    // it's a fabricated function pointer returned by ffi_closure_alloc,
    // which presents the "thunk"...a C function that other C functions can
    // call which will then delegate to Rebol to call the wrapped ACTION!.
    //
    // Additionally, callbacks poke a data pointer into the HANDLE! with
    // ffi_closure*.  (The closure allocation routine gives back a void* and
    // not an ffi_closure* for some reason.  Perhaps because it takes a
    // size that might be bigger than the size of a closure?)
    //
    IDX_ROUTINE_CFUNC = 0,

    // An INTEGER! indicating which ABI is used by the CFUNC (enum ffi_abi)
    //
    // !!! It would be better to change this to use a WORD!, especially if
    // the routine descriptions will ever become user visible objects.
    //
    IDX_ROUTINE_ABI = 1,

    // The LIBRARY! the CFUNC* lives in if a routine, or the ACTION! to
    // be called if this is a callback.
    //
    IDX_ROUTINE_ORIGIN = 2,

    // The "schema" of the return type.  This is either a WORD! (which
    // is a symbol corresponding to the FFI_TYPE constant of the return) or
    // a BLOCK! representing a field (this REBFLD will hopefully become
    // OBJECT! at some point).  If it is BLANK! then there is no return type.
    //
    IDX_ROUTINE_RET_SCHEMA = 3,

    // An ARRAY! of the argument schemas; each also WORD! or ARRAY!, following
    // the same pattern as the return value...but not allowed to be blank
    // (no such thing as a void argument)
    //
    IDX_ROUTINE_ARG_SCHEMAS = 4,

    // A HANDLE! containing one ffi_cif*, or BLANK! if variadic.  The Call
    // InterFace (CIF) for a C function with fixed arguments can be created
    // once and then used many times.  For a variadic routine, it must be
    // created on each call to match the number and types of arguments.
    //
    IDX_ROUTINE_CIF = 5,

    // A HANDLE! which is actually an array of ffi_type*, so a C array of
    // pointers.  This array was passed into the CIF at its creation time,
    // and it holds references to them as long as you use that CIF...so this
    // array must survive as long as the CIF does.  BLANK! if variadic.
    //
    IDX_ROUTINE_ARG_FFTYPES = 6,

    // A LOGIC! of whether this routine is variadic.  Since variadic-ness is
    // something that gets exposed in the ACTION! interface itself, this
    // may become redundant as an internal property of the implementation.
    //
    IDX_ROUTINE_IS_VARIADIC = 7,

    // An ffi_closure which for a callback stores the place where the CFUNC*
    // lives, or BLANK! otherwise.
    //
    IDX_ROUTINE_CLOSURE = 8,

    IDX_ROUTINE_MAX
};

#define RIN_AT(a, n) \
    SER_AT(REBVAL, SER(a), (n)) // locate index access

inline static CFUNC *RIN_CFUNC(REBRIN *r)
    { return VAL_HANDLE_CFUNC(RIN_AT(r, IDX_ROUTINE_CFUNC)); }

inline static ffi_abi RIN_ABI(REBRIN *r)
    { return cast(ffi_abi, VAL_INT32(RIN_AT(r, IDX_ROUTINE_ABI))); }

inline static REBOOL RIN_IS_CALLBACK(REBRIN *r) {
    if (IS_ACTION(RIN_AT(r, IDX_ROUTINE_ORIGIN)))
        return TRUE;
    assert(
        IS_LIBRARY(RIN_AT(r, IDX_ROUTINE_ORIGIN))
        || IS_BLANK(RIN_AT(r, IDX_ROUTINE_ORIGIN))
    );
    return FALSE;
}

inline static ffi_closure* RIN_CLOSURE(REBRIN *r) {
    assert(RIN_IS_CALLBACK(r)); // only callbacks have ffi_closure
    return VAL_HANDLE_POINTER(ffi_closure, RIN_AT(r, IDX_ROUTINE_CLOSURE));
}

inline static REBLIB *RIN_LIB(REBRIN *r) {
    assert(!RIN_IS_CALLBACK(r));
    if (IS_BLANK(RIN_AT(r, IDX_ROUTINE_ORIGIN)))
        return NULL;
    return VAL_LIBRARY(RIN_AT(r, IDX_ROUTINE_ORIGIN));
}

inline static REBACT *RIN_CALLBACK_ACTION(REBRIN *r) {
    assert(RIN_IS_CALLBACK(r));
    return VAL_ACTION(RIN_AT(r, IDX_ROUTINE_ORIGIN));
}

inline static REBVAL *RIN_RET_SCHEMA(REBRIN *r)
    { return KNOWN(RIN_AT(r, IDX_ROUTINE_RET_SCHEMA)); }

inline static REBCNT RIN_NUM_FIXED_ARGS(REBRIN *r)
    { return VAL_LEN_HEAD(RIN_AT(r, IDX_ROUTINE_ARG_SCHEMAS)); }

inline static REBVAL *RIN_ARG_SCHEMA(REBRIN *r, REBCNT n) { // 0-based index
    return KNOWN(VAL_ARRAY_AT_HEAD(RIN_AT(r, IDX_ROUTINE_ARG_SCHEMAS), n));
}

inline static ffi_cif *RIN_CIF(REBRIN *r)
    { return VAL_HANDLE_POINTER(ffi_cif, RIN_AT(r, IDX_ROUTINE_CIF)); }

inline static ffi_type** RIN_ARG_FFTYPES(REBRIN *r) {
    return VAL_HANDLE_POINTER(ffi_type*, RIN_AT(r, IDX_ROUTINE_ARG_FFTYPES));
}

inline static REBOOL RIN_IS_VARIADIC(REBRIN *r)
    { return VAL_LOGIC(RIN_AT(r, IDX_ROUTINE_IS_VARIADIC)); }


// !!! FORWARD DECLARATIONS
//
// Currently there is no auto-processing of the files in extensions to look
// for C functions and extract their prototypes to be used within that
// extension.  Maintain manually for the moment.
//

extern REBSTU *Copy_Struct_Managed(REBSTU *src);
extern void Init_Struct_Fields(REBVAL *ret, REBVAL *spec);
extern REBACT *Alloc_Ffi_Action_For_Spec(REBVAL *ffi_spec, ffi_abi abi);
extern void callback_dispatcher(
    ffi_cif *cif,
    void *ret,
    void **args,
    void *user_data
);
extern void cleanup_ffi_closure(const REBVAL *v);

extern REB_R T_Struct(REBFRM *frame_, REBSYM action);
extern REB_R PD_Struct(REBPVS *pvs, const REBVAL *picker, const REBVAL *opt_setval);
extern REBINT CT_Struct(const RELVAL *a, const RELVAL *b, REBINT mode);
extern void MAKE_Struct(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg);
extern void TO_Struct(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg);
extern void MF_Struct(REB_MOLD *mo, const RELVAL *v, REBOOL form);

extern REB_R Routine_Dispatcher(REBFRM *f);

inline static REBOOL IS_ACTION_RIN(const RELVAL *v)
    { return VAL_ACT_DISPATCHER(v) == &Routine_Dispatcher; }

