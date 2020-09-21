//
//  File: %reb-struct.h
//  Summary: "Struct to C function"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2014 Atronix Engineering, Inc.
// Copyright 2014-2019 Ren-C Open Source Contributors
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
// STRUCT! is an extension value type that models a C `struct {}` value.
// The cell holds a pointer to a node containing the data: a singular REBARR
// series (a "REBSTU"), that typically holds just one BINARY! value with the
// memory of the instance.  Then, the LINK() field of this singular "REBSTU"
// points to a "REBFLD" schema that modles the names/types/sizes/offsets of
// the fields inside that memory block.
//
// A STRUCT!'s REBSTU can be seen as somewhat like an OBJECT!s REBCTX.  But
// instead of a LINK() to a "keylist", it links to a REBFLD array with indexed
// elements corresponding to descriptor properties for the FFI (one of which
// is a dynamically created `ffi_type` for the structure, as required by
// libffi to work with it).  As C structs can contain other structs, a REBFLD
// can model not just a struct but also an element of a struct...so the
// top-level schema contains an array of the constitution REBFLD items.
//
// As with OBJECT! keylists, once a REBFLD schema is created, it may be shared
// among multiple instances that share that schema.
//
// With this model of a C struct in place, Rebol can own the memory underlying
// a structure.  Then it can choose to fill that memory (or leave it
// uninitialized to be filled), and pass it through to a C function that is 
// expecting structs--either by pointer or by value.  It can access the
// structure with operations that do translated reads of the memory into Rebol
// values, or encode Rebol values as changing the right bytes at the right
// offset for a translated write.
//
///// NOTES ///////////////////////////////////////////////////////////////=//
//
// * See comments on ADDR-OF from the FFI about how the potential for memory
//   instability of content pointers may not be a match for the needs of an
//   FFI interface.  While calling into arbitrary C code with memory pointers
//   is fundamentally a dicey operation no matter what--there is a need for
//   some level of pointer locking if memory to mutable Rebol strings is
//   to be given out as raw UTF-8.
//
// * Atronix's initial implementation of the FFI used custom C structures to
//   describe things like the properties of a routine, or the schema of a
//   struct layout.  This required specialized hooks into the garbage
//   collector, that indicated locations in those C structs that pointers to
//   GC-managed elements lived.  Ren-C moved away from this, so that the
//   descriptors are ordinary Rebol arrays.  It's only a little bit less
//   efficient, and permitted the FFI to be migrated to an extension, so it
//   would not bring cost to builds that didn't use it (e.g. WASM build)
//
// * Because structs are not a built-in cell type, they are of kind
//   REB_CUSTOM, and hence must sacrifice one of their four platform-sized
//   pointer fields for their type information (so, the "extra" pointer is not
//   available for other uses).
//

#include <ffi.h>


// The REBLIB concept modeling a .DLL or .so file is no longer a built-in
// type.  The "Library Extension" provides it.  There is no particularly good
// system for making dependent extensions, so we trust that the build system
// has somehow gotten the include in the path for us and will handle it.  :-/

#include "sys-library.h"


// Returns an ffi_type* (which contains a ->type field, that holds the
// FFI_TYPE_XXX enum).
//
// Note: We avoid creating a "VOID" type in order to not give the illusion of
// void parameters being legal.  The VOID! return type is handled exclusively
// by the return value, to prevent potential mixups.
//
inline static ffi_type *Get_FFType_For_Sym(REBSYM sym) {
    switch (sym) {
      case SYM_UINT8: return &ffi_type_uint8;
      case SYM_INT8: return &ffi_type_sint8;
      case SYM_UINT16: return &ffi_type_uint16;
      case SYM_INT16: return &ffi_type_sint16;
      case SYM_UINT32: return &ffi_type_uint32;
      case SYM_INT32: return &ffi_type_sint32;
      case SYM_UINT64: return &ffi_type_uint64;
      case SYM_INT64: return &ffi_type_sint64;
      case SYM_FLOAT: return &ffi_type_float;
      case SYM_DOUBLE: return &ffi_type_double;
      case SYM_POINTER: return &ffi_type_pointer;
      case SYM_REBVAL: return &ffi_type_pointer;

    // !!! SYM_INTEGER, SYM_DECIMAL, SYM_STRUCT was "-1" in original table

      default:
        return nullptr;
    }
}


//=//// FFI STRUCT SCHEMA DESCRIPTOR (FLD) ////////////////////////////////=//
//
// A "field" is a small BLOCK! of properties that describe what is basically
// a single item in a C struct (e.g. `struct { ... int field[3]; ....}`).  It
// has primary information like the type (`int`), name ("field"), and
// dimensionality (3).  But it also caches derived information, like the
// offset within the struct or the total size.
//
// Since you can embed structs in structs, this same field type for "one
// element" is the same type used for a toplevel overall schema of a struct.
//
// Schemas are REBFLD arrays, which contain all the information about
// the structure's layout, regardless of what offset it would find itself at
// inside of a data blob.  This includes the total size, and arrays of
// field definitions...essentially, the validated spec.  It also contains
// a HANDLE! for the `ffi_type`, a structure that needs to be made that
// coalesces the information the FFI has to know to interpret the binary.
//

typedef REBARR REBFLD;  // alias to help find usages

enum {
    // A WORD! name for the field (or BLANK! if anonymous)
    //
    // https://gcc.gnu.org/onlinedocs/gcc-4.7.2/gcc/Unnamed-Fields.html
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
    SER_AT(REBVAL, SER(a), (n))  // locate index access

inline static REBSTR *FLD_NAME(REBFLD *f) {
    if (IS_BLANK(FLD_AT(f, IDX_FIELD_NAME)))
        return nullptr;
    return VAL_WORD_SPELLING(FLD_AT(f, IDX_FIELD_NAME));
}

inline static bool FLD_IS_STRUCT(REBFLD *f) {
    if (IS_BLOCK(FLD_AT(f, IDX_FIELD_TYPE)))
        return true;
    assert(FLD_NAME(f) != nullptr);  // only legal for toplevel struct schemas
    return false;
}

inline static REBSYM FLD_TYPE_SYM(REBFLD *f) {
    if (FLD_IS_STRUCT(f)) {
        //
        // We could return SYM_STRUCT_X for structs, but it's probably better
        // to have callers test FLD_IS_STRUCT() separately for clarity.
        //
        assert(false);
        return SYM_STRUCT_X;
    }
    assert(IS_WORD(FLD_AT(f, IDX_FIELD_TYPE)));
    return VAL_WORD_SYM(FLD_AT(f, IDX_FIELD_TYPE));
}

inline static REBARR *FLD_FIELDLIST(REBFLD *f) {
    assert(FLD_IS_STRUCT(f));
    return VAL_ARRAY_KNOWN_MUTABLE(FLD_AT(f, IDX_FIELD_TYPE));
}

inline static bool FLD_IS_ARRAY(REBFLD *f) {
    if (IS_BLANK(FLD_AT(f, IDX_FIELD_DIMENSION)))
        return false;
    assert(IS_INTEGER(FLD_AT(f, IDX_FIELD_DIMENSION)));
    return true;
}

inline static REBLEN FLD_DIMENSION(REBFLD *f) {
    assert(FLD_IS_ARRAY(f));
    return VAL_UINT32(FLD_AT(f, IDX_FIELD_DIMENSION));
}

inline static ffi_type *FLD_FFTYPE(REBFLD *f)
    { return VAL_HANDLE_POINTER(ffi_type, FLD_AT(f, IDX_FIELD_FFTYPE)); }

inline static REBLEN FLD_OFFSET(REBFLD *f)
    { return VAL_UINT32(FLD_AT(f, IDX_FIELD_OFFSET)); }

inline static REBLEN FLD_WIDE(REBFLD *f)
    { return VAL_UINT32(FLD_AT(f, IDX_FIELD_WIDE)); }

inline static REBLEN FLD_LEN_BYTES_TOTAL(REBFLD *f) {
    if (FLD_IS_ARRAY(f))
        return FLD_WIDE(f) * FLD_DIMENSION(f);
    return FLD_WIDE(f);
}

inline static ffi_type* SCHEMA_FFTYPE(const RELVAL *schema) {
    if (IS_BLOCK(schema)) {
        REBFLD *field = VAL_ARRAY_KNOWN_MUTABLE(schema);
        return FLD_FFTYPE(field);
    }
    return Get_FFType_For_Sym(VAL_WORD_SYM(schema));
}


#define VAL_STRUCT_LIMIT UINT32_MAX


//=//// STRUCTURE INSTANCE (STU) //////////////////////////////////////////=//
//
// A REBSTU is a singular array, typically holding a BINARY! value of bytes
// which represent the memory for the struct instance.  (If the structure is
// actually describing something at an absolute location in memory that Rebol
// does not control, it will be a HANDLE! with that pointer instead.)
//
// The LINK() field of this singular array points to a REBFLD* that describes
// the "schema" of the struct.
//

extern REBTYP *EG_Struct_Type;

inline static bool IS_STRUCT(const RELVAL *v)  // Note: QUOTED! doesn't count
  { return IS_CUSTOM(v) and CELL_CUSTOM_TYPE(v) == EG_Struct_Type; }

typedef REBARR REBSTU;

#define LINK_SCHEMA_NODE(stu)   LINK(stu).custom.node
#define LINK_SCHEMA(s)          ARR(LINK_SCHEMA_NODE(s))

#define MISC_STU_OFFSET(stu)    MISC(stu).custom.u32

inline static REBFLD *STU_SCHEMA(REBSTU *stu) {
    REBFLD *schema = cast(REBARR*, LINK(stu).custom.node);
    assert(FLD_IS_STRUCT(schema));
    return schema;
}

#define STU_DATA(stu) \
    SPECIFIC(ARR_SINGLE(stu))  // BINARY! or HANDLE!

#define STU_OFFSET(stu) \
    MISC(stu).custom.u32

inline static REBARR *STU_FIELDLIST(REBSTU *stu)
  { return FLD_FIELDLIST(STU_SCHEMA(stu)); }

inline static REBLEN STU_SIZE(REBSTU *stu)
  { return FLD_WIDE(STU_SCHEMA(stu)); }

#define STU_FFTYPE(stu) \
    FLD_FFTYPE(STU_SCHEMA(stu))

inline static REBYTE *STU_DATA_HEAD(REBSTU *stu) {
    REBVAL *data = STU_DATA(stu);
    if (IS_BINARY(data))
        return BIN_HEAD(VAL_BINARY_KNOWN_MUTABLE(data));

    assert(VAL_HANDLE_LEN(data) != 0);  // is HANDLE!
    return VAL_HANDLE_POINTER(REBYTE, data);
}

inline static REBLEN STU_DATA_LEN(REBSTU *stu) {
    REBVAL *data = STU_DATA(stu);
    if (IS_BINARY(data))
        return VAL_LEN_AT(data);

    assert(VAL_HANDLE_LEN(data) != 0);  // is HANDLE!
    return VAL_HANDLE_LEN(data);
}

inline static bool STU_INACCESSIBLE(REBSTU *stu) {
    REBVAL *data = STU_DATA(stu);
    if (IS_BINARY(data))
        return false;  // it's not "external", so never inaccessible

    if (VAL_HANDLE_LEN(data) != 0)
        return false;  // !!! TBD: double check size is correct for mem block
    
    return true;
}


// Just as with the varlist of an object, the struct's data is a node for the
// instance that points to the schema.
//
// !!! The series data may come from an outside pointer, hence VAL_STRUCT_DATA
// may be a handle instead of a BINARY!.

#define VAL_STRUCT(v) \
    cast(REBSTU*, VAL_NODE(v))

#define VAL_STRUCT_DATA(v) \
    STU_DATA(VAL_STRUCT(v))

#define VAL_STRUCT_OFFSET(v) \
    STU_OFFSET(VAL_STRUCT(v))

#define VAL_STRUCT_SCHEMA(v) \
    STU_SCHEMA(VAL_STRUCT(v))

#define VAL_STRUCT_SIZE(v) \
    STU_SIZE(VAL_STRUCT(v))

#define VAL_STRUCT_DATA_HEAD(v) \
    STU_DATA_HEAD(VAL_STRUCT(v))


inline static REBYTE *VAL_STRUCT_DATA_AT(const RELVAL *v) {
    return VAL_STRUCT_DATA_HEAD(v) + VAL_STRUCT_OFFSET(v);
}

inline static REBLEN VAL_STRUCT_DATA_LEN(const RELVAL *v) {
    return STU_DATA_LEN(VAL_STRUCT(v));
}

#define VAL_STRUCT_FIELDLIST(v) \
    STU_FIELDLIST(VAL_STRUCT(v))

#define VAL_STRUCT_FFTYPE(v) \
    STU_FFTYPE(VAL_STRUCT(v))

#define VAL_STRUCT_INACCESSIBLE(v) \
    STU_INACCESSIBLE(VAL_STRUCT(v))

inline static REBVAL *Init_Struct(RELVAL *out, REBSTU *stu) {
    assert(GET_SERIES_FLAG(stu, MANAGED));

    RESET_CUSTOM_CELL(out, EG_Struct_Type, CELL_FLAG_FIRST_IS_NODE);
    INIT_VAL_NODE(out, stu);
    VAL_STRUCT_OFFSET(out) = 0;
    return SPECIFIC(out);
}


//=//// FFI ROUTINE INFO DESCRIPTOR (RIN) /////////////////////////////////=//
//
// ...

typedef REBARR REBRIN;

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
    // lives, or BLANK! if the routine does not have a callback interface.
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

inline static bool RIN_IS_CALLBACK(REBRIN *r) {
    if (IS_ACTION(RIN_AT(r, IDX_ROUTINE_ORIGIN)))
        return true;
    assert(
        IS_LIBRARY(RIN_AT(r, IDX_ROUTINE_ORIGIN))
        or IS_BLANK(RIN_AT(r, IDX_ROUTINE_ORIGIN))
    );
    return false;
}

inline static ffi_closure* RIN_CLOSURE(REBRIN *r) {
    assert(RIN_IS_CALLBACK(r)); // only callbacks have ffi_closure
    return VAL_HANDLE_POINTER(ffi_closure, RIN_AT(r, IDX_ROUTINE_CLOSURE));
}

inline static REBLIB *RIN_LIB(REBRIN *r) {
    assert(not RIN_IS_CALLBACK(r));
    if (IS_BLANK(RIN_AT(r, IDX_ROUTINE_ORIGIN)))
        return NULL;
    return VAL_LIBRARY(RIN_AT(r, IDX_ROUTINE_ORIGIN));
}

inline static REBACT *RIN_CALLBACK_ACTION(REBRIN *r) {
    assert(RIN_IS_CALLBACK(r));
    return VAL_ACTION(RIN_AT(r, IDX_ROUTINE_ORIGIN));
}

inline static REBVAL *RIN_RET_SCHEMA(REBRIN *r)
    { return RIN_AT(r, IDX_ROUTINE_RET_SCHEMA); }

inline static REBLEN RIN_NUM_FIXED_ARGS(REBRIN *r)
    { return VAL_LEN_HEAD(RIN_AT(r, IDX_ROUTINE_ARG_SCHEMAS)); }

inline static REBVAL *RIN_ARG_SCHEMA(REBRIN *r, REBLEN n) { // 0-based index
    REBVAL *arg_schemas = RIN_AT(r, IDX_ROUTINE_ARG_SCHEMAS);
    return SPECIFIC(ARR_AT(VAL_ARRAY_KNOWN_MUTABLE(arg_schemas), n));
}

inline static ffi_cif *RIN_CIF(REBRIN *r)
    { return VAL_HANDLE_POINTER(ffi_cif, RIN_AT(r, IDX_ROUTINE_CIF)); }

inline static ffi_type** RIN_ARG_FFTYPES(REBRIN *r) {
    return VAL_HANDLE_POINTER(ffi_type*, RIN_AT(r, IDX_ROUTINE_ARG_FFTYPES));
}

inline static bool RIN_IS_VARIADIC(REBRIN *r)
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

extern REB_R T_Struct(REBFRM *frame_, const REBVAL *verb);
extern REB_R PD_Struct(REBPVS *pvs, const RELVAL* picker, const REBVAL *opt_setval);
extern REBINT CT_Struct(REBCEL(const*) a, REBCEL(const*) b, bool strict);
extern REB_R MAKE_Struct(REBVAL *out, enum Reb_Kind kind,const REBVAL *opt_parent, const REBVAL *arg);
extern REB_R TO_Struct(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg);
extern void MF_Struct(REB_MOLD *mo, REBCEL(const*) v, bool form);

extern REB_R Routine_Dispatcher(REBFRM *f);

inline static bool IS_ACTION_RIN(const RELVAL *v)
    { return ACT_DISPATCHER(VAL_ACTION(v)) == &Routine_Dispatcher; }

