//
//  File: %reb-struct.h
//  Summary: "Struct to C function"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2014 Atronix Engineering, Inc.
// Copyright 2014-2016 Rebol Open Source Contributors
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


#ifdef HAVE_LIBFFI_AVAILABLE
    #include <ffi.h>
#else
    // Non-functional stubs, see notes at top of t-routine.c

    typedef struct _ffi_type
    {
        size_t size;
        unsigned short alignment;
        unsigned short type;
        struct _ffi_type **elements;
    } ffi_type;

    #define FFI_TYPE_VOID       0
    #define FFI_TYPE_INT        1
    #define FFI_TYPE_FLOAT      2
    #define FFI_TYPE_DOUBLE     3
    #define FFI_TYPE_LONGDOUBLE 4
    #define FFI_TYPE_UINT8      5
    #define FFI_TYPE_SINT8      6
    #define FFI_TYPE_UINT16     7
    #define FFI_TYPE_SINT16     8
    #define FFI_TYPE_UINT32     9
    #define FFI_TYPE_SINT32     10
    #define FFI_TYPE_UINT64     11
    #define FFI_TYPE_SINT64     12
    #define FFI_TYPE_STRUCT     13
    #define FFI_TYPE_POINTER    14
    #define FFI_TYPE_COMPLEX    15

    // !!! Heads-up to FFI lib authors: these aren't const definitions.  :-/
    // Stray modifications could ruin these "constants".  Being const-correct
    // in the parameter structs for the type arrays would have been nice...

    extern ffi_type ffi_type_void;
    extern ffi_type ffi_type_uint8;
    extern ffi_type ffi_type_sint8;
    extern ffi_type ffi_type_uint16;
    extern ffi_type ffi_type_sint16;
    extern ffi_type ffi_type_uint32;
    extern ffi_type ffi_type_sint32;
    extern ffi_type ffi_type_uint64;
    extern ffi_type ffi_type_sint64;
    extern ffi_type ffi_type_float;
    extern ffi_type ffi_type_double;
    extern ffi_type ffi_type_pointer;

    // Switched from an enum to allow Panic w/o complaint
    typedef int ffi_status;
    #define FFI_OK 0
    #define FFI_BAD_TYPEDEF 1
    #define FFI_BAD_ABI 2

    typedef enum ffi_abi
    {
        // !!! The real ffi_abi constants will be different per-platform,
        // you would not have the full list.  Interestingly, a subsetting
        // script *might* choose to alter libffi to produce a larger list
        // vs being full of #ifdefs (though that's rather invasive change
        // to the libffi code to be maintaining!)

        FFI_FIRST_ABI = 0x0BAD,
        FFI_WIN64,
        FFI_STDCALL,
        FFI_SYSV,
        FFI_THISCALL,
        FFI_FASTCALL,
        FFI_MS_CDECL,
        FFI_UNIX64,
        FFI_VFP,
        FFI_O32,
        FFI_N32,
        FFI_N64,
        FFI_O32_SOFT_FLOAT,
        FFI_N32_SOFT_FLOAT,
        FFI_N64_SOFT_FLOAT,
        FFI_LAST_ABI,
        FFI_DEFAULT_ABI = FFI_FIRST_ABI
    } ffi_abi;

    typedef struct {
        ffi_abi abi;
        unsigned nargs;
        ffi_type **arg_types;
        ffi_type *rtype;
        unsigned bytes;
        unsigned flags;
    } ffi_cif;

    // The closure is a "black box" but client code takes the sizeof() to
    // pass into the alloc routine...

    typedef struct {
        int stub;
    } ffi_closure;

#endif // HAVE_LIBFFI_AVAILABLE


struct Struct_Field {
    REBARR* spec; /* for nested struct */
    REBSER* fields; /* for nested struct */
    REBSTR *name;

    unsigned short type; // e.g. FFI_TYPE_XXX constants

    REBSER *fftype; // single-element series, one `ffi_type`
    REBSER *fields_fftype_ptrs; // multiple-element series of `ffi_type*`

    /* size is limited by struct->offset, so only 16-bit */
    REBCNT offset;
    REBCNT dimension; /* for arrays */
    REBCNT size; /* size of element, in bytes */

    /* Note: C89 bitfields may be 'int', 'unsigned int', or 'signed int' */
    unsigned int is_array:1;

    // A REBVAL is passed as an FFI_TYPE_POINTER array of length 4.  But
    // for purposes of the GC marking, in the structs it has to be known
    // that they are REBVAL.
    //
    // !!! What is passing REBVALs for?
    //
    unsigned int is_rebval:1;

    /* field is initialized? */
    /* (used by GC to decide if the value needs to be marked) */
    unsigned int done:1;
};

#define VAL_STRUCT_LIMIT MAX_U32


//=////////////////////////////////////////////////////////////////////////=//
//
//  STRUCT! (`struct Reb_Struct`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Struct is used by the FFI code to describe the layout of a C `struct`,
// so that Rebol data can be proxied to a C function call.
//
// !!! Atronix added the struct type to get coverage in the FFI, and it is
// possible to make and extract structure data even if one is not calling
// a routine at all.  This might not be necessary, in that the struct
// description could be an ordinary OBJECT! which is used in FFI specs
// and tells how to map data into a Rebol object used as a target.
//

inline static REBVAL *STU_VALUE(REBSTU *stu) {
    assert(ARR_LEN(stu) == 1);
    return KNOWN(ARR_HEAD(stu));
}

#define STU_INACCESSIBLE(stu) \
    VAL_STRUCT_INACCESSIBLE(STU_VALUE(stu))

inline static struct Struct_Field *STU_SCHEMA(REBSTU *stu) {
    //
    // The new concept for structures is to make a singular structure
    // descriptor OBJECT!.  Previously structs didn't have a top level node,
    // but a series of them... so this has to extract the fieldlist from
    // the new-format top-level node.

    REBSER *schema = ARR_SERIES(stu)->link.schema;

#if !defined(NDEBUG)
    if (SER_LEN(schema) != 1)
        Panic_Series(schema);
    assert(SER_LEN(schema) == 1);
#endif

    struct Struct_Field *top = SER_HEAD(struct Struct_Field, schema);
    assert(top->type == FFI_TYPE_STRUCT);
    return top;
}

inline static REBSER *STU_FIELDLIST(REBSTU *stu) {
    return STU_SCHEMA(stu)->fields;
}

inline static REBCNT STU_SIZE(REBSTU *stu) {
    return STU_SCHEMA(stu)->size;
}

inline static REBSER *STU_DATA_BIN(REBSTU *stu) {
    return STU_VALUE(stu)->payload.structure.data;
}

inline static REBCNT STU_OFFSET(REBSTU *stu) {
    return STU_VALUE(stu)->extra.struct_offset;
}

#define STU_FFTYPE(stu) \
    SER_HEAD(ffi_type, STU_SCHEMA(stu)->fftype)

#define VAL_STRUCT(v) \
    ((v)->payload.structure.stu)

#define VAL_STRUCT_SPEC(v) \
    (STU_SCHEMA(VAL_STRUCT(v))->spec)

#define VAL_STRUCT_INACCESSIBLE(v) \
    SER_DATA_NOT_ACCESSIBLE(VAL_STRUCT_DATA_BIN(v))

#define VAL_STRUCT_SCHEMA(v) \
    STU_SCHEMA(VAL_STRUCT(v))

#define VAL_STRUCT_SIZE(v) \
    STU_SIZE(VAL_STRUCT(v))

#define VAL_STRUCT_DATA_BIN(v) \
    ((v)->payload.structure.data)

#define VAL_STRUCT_OFFSET(v) \
    ((v)->extra.struct_offset)

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
// A routine is an interface to calling a C function, which uses the libffi
// library.
//
// !!! Previously, ROUTINE! was an ANY-FUNCTION! category (like NATIVE!,
// COMMAND!, ACTION!, etc.)  Ren-C unified these under a single FUNCTION!
// type for the purposes of interface (included in the elimination of the
// CLOSURE! vs. FUNCTION! distinction).
//
// Since MAKE ROUTINE! wouldn't work (without some hacks in the compatibility
// layer), MAKE-ROUTINE was introduced as a native that returns a FUNCTION!.
// This opens the door to having MAKE-ROUTINE be a user function which then
// generates another user function which calls a simpler native to dispatch
// its work.  That review is pending.
//

struct Reb_Routine_Info {
    union {
        struct {
            REBLIB *lib;
            CFUNC *cfunc;
        } routine;
        struct {
            //
            // The closure allocation routine gives back a void* and not
            // an ffi_closure* for some reason.  (Perhaps because it takes
            // a sizeof() that is >= size of closure, so may be bigger.)
            //
            ffi_closure *closure;
            REBFUN *func;
            void *dispatcher;
        } callback;
    } code;

    // Here the "schema" is either an INTEGER! (which is the FFI_TYPE constant
    // of the argument) or a HANDLE! containing a REBSER* of length 1 that
    // contains a `Struct_Field` (it's held in a series to allow it to be
    // referenced multiple places and participate in GC).
    //
    // !!! REBVALs are used here to simplify a GC-participating typed
    // struct, with an eye to a future where the schemas are done with OBJECT!
    // so that special GC behavior and C struct definitions is not necessary.
    //
    REBVAL ret_schema;
    REBARR *args_schemas;

    // The Call InterFace (CIF) for a C function with fixed arguments can
    // be created once and then used many times.  For a variadic routine,
    // it must be created to match the variadic arguments.  Hence this will
    // be NULL for variadics.
    //
    REBSER *cif; // one ffi_cif long (for GC participation, fail()...)
    REBSER *args_fftypes; // list of ffi_type*, must live as long as CIF does

    REBCNT flags; // !!! 32-bit...should it use REBFLGS for 64-bit on 64-bit?
    ffi_abi abi; // an enum

    //REBUPT padding; // sizeof(Reb_Routine_Info) % 8 must be 0 for Make_Node()
};


enum {
    ROUTINE_FLAG_MARK = 1 << 0, // routine was found during GC mark scan.
    ROUTINE_FLAG_USED = 1 << 1,
    ROUTINE_FLAG_CALLBACK = 1 << 2, // is a callback
    ROUTINE_FLAG_VARIADIC = 1 << 3 // has FFI va_list interface
};

#define SET_RIN_FLAG(s,f) \
    ((s)->flags |= (f))

#define CLEAR_RIN_FLAG(s,f) \
    ((s)->flags &= ~(f))

#define GET_RIN_FLAG(s, f) \
    LOGICAL((s)->flags & (f))

// Routine Field Accessors

inline static CFUNC *RIN_CFUNC(REBRIN *r)
    { return r->code.routine.cfunc; }

inline static REBLIB *RIN_LIB(REBRIN *r)
    { return r->code.routine.lib; }

#define RIN_NUM_FIXED_ARGS(r) \
    ARR_LEN((r)->args_schemas)

// !!! Should this be 1-based to be consistent with ARG() and PARAM() (or
// should the D_ARG(N) 1-basedness legacy be changed to a C 0-based one?)
//
#define RIN_ARG_SCHEMA(r,n) \
    KNOWN(ARR_AT((r)->args_schemas, (n)))

#define Get_FFType_Enum_Info(sym_out,kind_out,type) \
    cast(ffi_type*, Get_FFType_Enum_Info_Core((sym_out), (kind_out), (type)))

inline static void* SCHEMA_FFTYPE_CORE(const RELVAL *schema) {
    if (IS_HANDLE(schema)) {
        struct Struct_Field *field
            = SER_HEAD(
                struct Struct_Field,
                cast(REBSER*, VAL_HANDLE_DATA(schema))
            );
        return SER_HEAD(ffi_type, field->fftype);
    }

    // Avoid creating a "VOID" type in order to not give the illusion of
    // void parameters being legal.  The NONE! return type is handled
    // exclusively by the return value, to prevent potential mixups.
    //
    assert(IS_INTEGER(schema));

    enum Reb_Kind kind; // dummy
    REBSTR *name; // dummy
    return Get_FFType_Enum_Info(&name, &kind, VAL_INT32(schema));
}

#define SCHEMA_FFTYPE(schema) \
    cast(ffi_type*, SCHEMA_FFTYPE_CORE(schema))

#define RIN_RET_SCHEMA(r) \
    (&(r)->ret_schema)

#define RIN_DISPATCHER(r) \
    ((r)->code.callback.dispatcher)

#define RIN_CALLBACK_FUNC(r) \
    ((r)->code.callback.func)

#define RIN_CLOSURE(r) \
    ((r)->code.callback.closure)

#define RIN_ABI(r) \
    cast(ffi_abi, (r)->abi)
