/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2014 Atronix Engineering, Inc.
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  t-routine.c
**  Summary: External Routine Support
**  Section: datatypes
**  Author:  Shixin Zeng
**  Notes:
**      When Rebol3 was open-sourced in 12-Dec-2012, that version had lost
**      support for the ROUTINE! type from Rebol2.  It was later
**      reimplemented by Atronix in their fork via the cross-platform (and
**      popularly used) Foreign Function Interface library "libffi":
**
**          https://en.wikipedia.org/wiki/Libffi
**
**      Yet Rebol is very conservative about library dependencies that
**      introduce their "own build step", due to the complexity introduced.
**      If one is to build libffi for a particular platform, that requires
**      having the rather messy GNU autotools installed.  Notice the
**      `Makefile.am`, `acinclude.m4`, `autogen.sh`, `configure.ac`,
**      `configure.host`, etc:
**
**          https://github.com/atgreen/libffi
**
**      Suddenly, you need more than just a C compiler (and a rebol.exe) to
**      build Rebol.  You now need to have everything to configure and
**      build libffi.  -OR- it would mean a dependency on a built library
**      you had to find or get somewhere that was not part of the OS
**      naturally, which can be a wild goose chase with version
**      incompatibility.  If you `sudo apt-get libffi`, now you need apt-get
**      *and* you pull down any dependencies as well!
**
**      (Note: Rebol's "just say no" attitude is the heart of the Rebellion:
**
**          http://www.rebol.com/cgi-bin/blog.r?view=0497
**
**      ...so keeping the core true to this principle is critical.  If this
**      principle is compromised, the whole point of the project is lost.)
**
**      Yet Rebol2 had ROUTINE!.  Red also has ROUTINE!, and is hinging its
**      story for rapid interoperability on it (you should not have to
**      wrap and recompile a DLL of C functions just to call them).  Users
**      want the feature and always ask...and Atronix needs it enough to have
**      had @ShixinZeng write it!
**
**      Regarding the choice of libffi in particular, it's a strong sign to
**      notice how many other language projects are using it.  Short list
**      taken from 2015 Wikipedia:
**
**          Python, Haskell, Dalvik, F-Script, PyPy, PyObjC, RubyCocoa,
**          JRuby, Rubinius, MacRuby, gcj, GNU Smalltalk, IcedTea, Cycript,
**          Pawn, Squeak, Java Native Access, Common Lisp, Racket,
**          Embeddable Common Lisp and Mozilla.
**
**      Rebol could roll its own implementation.  But that takes time and
**      maintenance, and it's hard to imagine how much better a job could
**      be done for a C-based foreign function interface on these platforms;
**      it's light and quite small once built.  So it makes sense to
**      "extract" libffi's code out of its repo to form one .h and .c file.
**      They'd live in the Rebol sources and build with the existing process,
**      with no need for GNU Autotools (which are *particularly* crufty!!!)
**
**      Doing such extractions by hand is how Rebol was originally done;
**      that made it hard to merge updates.  As a more future-proof method,
**      @HostileFork wrote a make-zlib.r extractor that can take a copy of
**      the zlib repository and do the work (mostly) automatically.  Going
**      forward it seems prudent to do the same with libffi and any other
**      libraries that Rebol co-opts into its turnkey build process.
**
**      Until that happens for libffi, not definining HAVE_LIBFFI_AVAILABLE,
**      will give you a short list of non-functional "stubs".  These can
**      allow t-routine.c to compile anyway.  That assists with maintenance
**      of the code and keeping it on the radar, even among those doing core
**      maintenance who are not building against the FFI.
**
**      (Note: Longer term there may be a story by which a feature like
**      ROUTINE! could be implemented as a third party extension.  There is
**      short-term thinking trying to facilitate this for GOB! in Ren/C, to
**      try and open the doors to more type extensions.  That's a hard
**      problem in itself...and the needs of ROUTINE! are hooked a bit more
**      tightly into the evaluation loop.  So possibly not happening.)
**
***********************************************************************/

#include "sys-core.h"

#include "mem-pools.h" // low-level memory pool access

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

    ffi_type ffi_type_void = { 0, 0, FFI_TYPE_VOID, NULL };
    ffi_type ffi_type_uint8 = { 0, 0, FFI_TYPE_UINT8, NULL };
    ffi_type ffi_type_sint8 = { 0, 0, FFI_TYPE_SINT8, NULL };
    ffi_type ffi_type_uint16 = { 0, 0, FFI_TYPE_UINT16, NULL };
    ffi_type ffi_type_sint16 = { 0, 0, FFI_TYPE_SINT16, NULL };
    ffi_type ffi_type_uint32 = { 0, 0, FFI_TYPE_UINT32, NULL };
    ffi_type ffi_type_sint32 = { 0, 0, FFI_TYPE_SINT32, NULL };
    ffi_type ffi_type_uint64 = { 0, 0, FFI_TYPE_UINT64, NULL };
    ffi_type ffi_type_sint64 = { 0, 0, FFI_TYPE_SINT64, NULL };
    ffi_type ffi_type_float = { 0, 0, FFI_TYPE_FLOAT, NULL };
    ffi_type ffi_type_double = { 0, 0, FFI_TYPE_DOUBLE, NULL };
    ffi_type ffi_type_pointer = { 0, 0, FFI_TYPE_POINTER, NULL };

    // Switched from an enum to allow Panic w/o complaint
    typedef int ffi_status;
    const int FFI_OK = 0;
    const int FFI_BAD_TYPEDEF = 1;
    const int FFI_BAD_ABI = 2;

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

    ffi_status ffi_prep_cif(
        ffi_cif *cif,
        ffi_abi abi,
        unsigned int nargs,
        ffi_type *rtype,
        ffi_type **atypes
    ) {
        fail (Error(RE_NOT_FFI_BUILD));
    }

    ffi_status ffi_prep_cif_var(
        ffi_cif *cif,
        ffi_abi abi,
        unsigned int nfixedargs,
        unsigned int ntotalargs,
        ffi_type *rtype,
        ffi_type **atypes
    ) {
        fail (Error(RE_NOT_FFI_BUILD));
    }

    void ffi_call(
        ffi_cif *cif,
        void (*fn)(void),
        void *rvalue,
        void **avalue
    ) {
        fail (Error(RE_NOT_FFI_BUILD));
    }

    // The closure is a "black box" but client code takes the sizeof() to
    // pass into the alloc routine...

    typedef struct {
        int stub;
    } ffi_closure;

    void *ffi_closure_alloc(size_t size, void **code) {
        fail (Error(RE_NOT_FFI_BUILD));
    }

    ffi_status ffi_prep_closure_loc(
        ffi_closure *closure,
        ffi_cif *cif,
        void (*fun)(ffi_cif *, void *, void **, void *),
        void *user_data,
        void *codeloc
    ) {
        panic (Error(RE_NOT_FFI_BUILD));
    }

    void ffi_closure_free (void *closure) {
        panic (Error(RE_NOT_FFI_BUILD));
    }
#endif // HAVE_LIBFFI_AVAILABLE

#define QUEUE_EXTRA_MEM(v,p) \
    do { \
        *SER_AT(void*, (v)->extra_mem, SER_LEN((v)->extra_mem)) = (p); \
        EXPAND_SERIES_TAIL((v)->extra_mem, 1);\
    } while (0)

static ffi_type * struct_type_to_ffi [STRUCT_TYPE_MAX];

static void process_type_block(
    const REBVAL *out,
    REBVAL *blk,
    REBCNT n,
    REBOOL make
);

static void init_type_map()
{
    if (struct_type_to_ffi[0]) return;
    struct_type_to_ffi[STRUCT_TYPE_UINT8] = &ffi_type_uint8;
    struct_type_to_ffi[STRUCT_TYPE_INT8] = &ffi_type_sint8;
    struct_type_to_ffi[STRUCT_TYPE_UINT16] = &ffi_type_uint16;
    struct_type_to_ffi[STRUCT_TYPE_INT16] = &ffi_type_sint16;
    struct_type_to_ffi[STRUCT_TYPE_UINT32] = &ffi_type_uint32;
    struct_type_to_ffi[STRUCT_TYPE_INT32] = &ffi_type_sint32;
    struct_type_to_ffi[STRUCT_TYPE_UINT64] = &ffi_type_uint64;
    struct_type_to_ffi[STRUCT_TYPE_INT64] = &ffi_type_sint64;

    struct_type_to_ffi[STRUCT_TYPE_FLOAT] = &ffi_type_float;
    struct_type_to_ffi[STRUCT_TYPE_DOUBLE] = &ffi_type_double;

    struct_type_to_ffi[STRUCT_TYPE_POINTER] = &ffi_type_pointer;
}

//
//  CT_Routine: C
//
REBINT CT_Routine(const REBVAL *a, const REBVAL *b, REBINT mode)
{
    //RL_Print("%s, %d\n", __func__, __LINE__);
    if (mode >= 0) {
        return VAL_ROUTINE_INFO(a) == VAL_ROUTINE_INFO(b);
    }
    return -1;
}

//
//  CT_Callback: C
//
REBINT CT_Callback(const REBVAL *a, const REBVAL *b, REBINT mode)
{
    //RL_Print("%s, %d\n", __func__, __LINE__);
    return -1;
}

static REBCNT n_struct_fields (REBSER *fields)
{
    REBCNT n_fields = 0;
    REBCNT i = 0;
    for (i = 0; i < SER_LEN(fields); i ++) {
        struct Struct_Field *field = SER_AT(struct Struct_Field, fields, i);
        if (field->type != STRUCT_TYPE_STRUCT) {
            n_fields += field->dimension;
        } else {
            n_fields += n_struct_fields(field->fields);
        }
    }
    return n_fields;
}

static ffi_type* struct_to_ffi(const REBVAL *out, REBSER *fields, REBOOL make)
{
    REBCNT i = 0, j = 0;
    REBCNT n_basic_type = 0;

    ffi_type *stype = NULL;
    //printf("allocated stype at: %p\n", stype);
    if (make) {//called by Routine constructor
        stype = OS_ALLOC(ffi_type);
        QUEUE_EXTRA_MEM(VAL_ROUTINE_INFO(out), stype);
    } else {
        REBSER *ser = Make_Series(2, sizeof(ffi_type), MKS_NONE);
        SET_SER_FLAG(ser, SERIES_FLAG_FIXED_SIZE);
        stype = SER_HEAD(ffi_type, ser);
        PUSH_GUARD_SERIES(ser);
    }

    stype->size = stype->alignment = 0;
    stype->type = FFI_TYPE_STRUCT;

    /* one extra for NULL */
    if (make) {
        stype->elements = OS_ALLOC_N(ffi_type *, 1 + n_struct_fields(fields));
        //printf("allocated stype elements at: %p\n", stype->elements);
        QUEUE_EXTRA_MEM(VAL_ROUTINE_INFO(out), stype->elements);
    }
    else {
        REBSER *ser = Make_Series(
            2 + n_struct_fields(fields), sizeof(ffi_type *), MKS_NONE
        );
        SET_SER_FLAG(ser, SERIES_FLAG_FIXED_SIZE);
        stype->elements = SER_HEAD(ffi_type*, ser);
        PUSH_GUARD_SERIES(ser);
    }

    for (i = 0; i < SER_LEN(fields); i ++) {
        struct Struct_Field *field = SER_AT(struct Struct_Field, fields, i);
        if (field->type == STRUCT_TYPE_REBVAL) {
            /* don't see a point to pass a rebol value to external functions */
            fail (Error_Invalid_Arg(out));
        } else if (field->type != STRUCT_TYPE_STRUCT) {
            if (struct_type_to_ffi[field->type]) {
                REBCNT n = 0;
                for (n = 0; n < field->dimension; n ++) {
                    stype->elements[j++] = struct_type_to_ffi[field->type];
                }
            } else {
                return NULL;
            }
        } else {
            ffi_type *subtype = struct_to_ffi(out, field->fields, make);
            if (subtype) {
                REBCNT n = 0;
                for (n = 0; n < field->dimension; n ++) {
                    stype->elements[j++] = subtype;
                }
            } else {
                return NULL;
            }
        }
    }
    stype->elements[j] = NULL;

    return stype;
}

/* convert the type of "elem", and store it in "out" with index of "idx"
 */
static REBOOL rebol_type_to_ffi(const REBVAL *out, const REBVAL *elem, REBCNT idx, REBOOL make)
{
    ffi_type **args = SER_HEAD(ffi_type*, VAL_ROUTINE_FFI_ARG_TYPES(out));
    REBVAL *rebol_args = NULL;
    if (idx) {
        // when it's first call for return type, all_args has not been initialized yet
        if (ROUTINE_GET_FLAG(VAL_ROUTINE_INFO(out), ROUTINE_VARARGS)
            && idx > ARR_LEN(VAL_ROUTINE_FIXED_ARGS(out))) {
            rebol_args = ARR_HEAD(VAL_ROUTINE_ALL_ARGS(out));
        }
        else {
            rebol_args = ARR_HEAD(VAL_ROUTINE_PARAMLIST(out));
        }
    }

    if (IS_WORD(elem)) {
        REBVAL *temp;
        switch (VAL_WORD_CANON(elem)) {
            case SYM_VOID:
                args[idx] = &ffi_type_void;
                break;
            case SYM_UINT8:
                args[idx] = &ffi_type_uint8;
                if (idx) TYPE_SET(&rebol_args[idx], REB_INTEGER);
                break;
            case SYM_INT8:
                args[idx] = &ffi_type_sint8;
                if (idx) TYPE_SET(&rebol_args[idx], REB_INTEGER);
                break;
            case SYM_UINT16:
                args[idx] = &ffi_type_uint16;
                if (idx) TYPE_SET(&rebol_args[idx], REB_INTEGER);
                break;
            case SYM_INT16:
                args[idx] = &ffi_type_sint16;
                if (idx) TYPE_SET(&rebol_args[idx], REB_INTEGER);
                break;
            case SYM_UINT32:
                args[idx] = &ffi_type_uint32;
                if (idx) TYPE_SET(&rebol_args[idx], REB_INTEGER);
                break;
            case SYM_INT32:
                args[idx] = &ffi_type_sint32;
                if (idx) TYPE_SET(&rebol_args[idx], REB_INTEGER);
                break;
            case SYM_UINT64:
                args[idx] = &ffi_type_uint64;
                if (idx) TYPE_SET(&rebol_args[idx], REB_INTEGER);
                break;
            case SYM_INT64:
                args[idx] = &ffi_type_sint64;
                if (idx) TYPE_SET(&rebol_args[idx], REB_INTEGER);
                break;
            case SYM_FLOAT:
                args[idx] = &ffi_type_float;
                if (idx) TYPE_SET(&rebol_args[idx], REB_DECIMAL);
                break;
            case SYM_DOUBLE:
                args[idx] = &ffi_type_double;
                if (idx) TYPE_SET(&rebol_args[idx], REB_DECIMAL);
                break;
            case SYM_POINTER:
                args[idx] = &ffi_type_pointer;
                if (idx) {
                    TYPE_SET(&rebol_args[idx], REB_INTEGER);
                    TYPE_SET(&rebol_args[idx], REB_STRING);
                    TYPE_SET(&rebol_args[idx], REB_BINARY);
                    TYPE_SET(&rebol_args[idx], REB_VECTOR);
                    TYPE_SET(&rebol_args[idx], REB_CALLBACK);
                }
                break;
            default:
                return FALSE;
        }
        temp = Alloc_Tail_Array(VAL_ROUTINE_FFI_ARG_STRUCTS(out));
        SET_NONE(temp);
    }
    else if (IS_STRUCT(elem)) {
        ffi_type *ftype = struct_to_ffi(out, VAL_STRUCT_FIELDS(elem), make);
        REBVAL *to = NULL;
        if (ftype) {
            args[idx] = ftype;
            if (idx) {
                TYPE_SET(&rebol_args[idx], REB_STRUCT);
            }
        } else {
            return FALSE;
        }
        if (idx == 0) {
            to = ARR_HEAD(VAL_ROUTINE_FFI_ARG_STRUCTS(out));
        } else {
            to = Alloc_Tail_Array(VAL_ROUTINE_FFI_ARG_STRUCTS(out));
        }
        Copy_Struct_Val(elem, to); //for callback and return value
    } else {
        return FALSE;
    }
    return TRUE;
}

/* make a copy of the argument
 * arg referes to return value when idx = 0
 * function args start from idx = 1
 *
 * @ptrs is an array with a length of number of arguments of @rot
 *
 * For FFI_TYPE_POINTER, a temperary pointer could be needed
 * (whose address is returned). ptrs[idx] is the temperary pointer.
 * */
static void *arg_to_ffi(const REBVAL *rot, REBVAL *arg, REBCNT idx, void **ptrs)
{
    ffi_type **args = SER_HEAD(ffi_type*, VAL_ROUTINE_FFI_ARG_TYPES(rot));
    REBARR *rebol_args;

    struct Reb_Call *call_ = DSF; // So you can use the D_xxx macros

    if (ROUTINE_GET_FLAG(VAL_ROUTINE_INFO(rot), ROUTINE_VARARGS))
        rebol_args = VAL_ROUTINE_ALL_ARGS(rot);
    else
        rebol_args = VAL_ROUTINE_PARAMLIST(rot);

    switch (args[idx]->type) {
        case FFI_TYPE_UINT8:
            if (!IS_INTEGER(arg)) {
                fail (Error_Arg_Type(
                    D_LABEL_SYM, ARR_AT(rebol_args, idx), arg
                ));
            } else {
#ifdef BIG_ENDIAN
                u8 i = (u8) VAL_INT64(arg);
                memcpy(&VAL_INT64(arg), &i, sizeof(u8));
#endif
                return &VAL_INT64(arg);
            }

        case FFI_TYPE_SINT8:
            if (!IS_INTEGER(arg)) {
                fail (Error_Arg_Type(
                    D_LABEL_SYM, ARR_AT(rebol_args, idx), arg
                ));
            } else {
#ifdef BIG_ENDIAN
                i8 i = (i8) VAL_INT64(arg);
                memcpy(&VAL_INT64(arg), &i, sizeof(i8));
#endif
                return &VAL_INT64(arg);
            }

        case FFI_TYPE_UINT16:
            if (!IS_INTEGER(arg)) {
                fail (Error_Arg_Type(
                    D_LABEL_SYM, ARR_AT(rebol_args, idx), arg
                ));
            } else {
#ifdef BIG_ENDIAN
                u16 i = (u16) VAL_INT64(arg);
                memcpy(&VAL_INT64(arg), &i, sizeof(u16));
#endif
                return &VAL_INT64(arg);
            }

        case FFI_TYPE_SINT16:
            if (!IS_INTEGER(arg)) {
                fail (Error_Arg_Type(
                    D_LABEL_SYM, ARR_AT(rebol_args, idx), arg
                ));
            } else {
#ifdef BIG_ENDIAN
                i16 i = (i16) VAL_INT64(arg);
                memcpy(&VAL_INT64(arg), &i, sizeof(i16));
#endif
                return &VAL_INT64(arg);
            }

        case FFI_TYPE_UINT32:
            if (!IS_INTEGER(arg)) {
                fail (Error_Arg_Type(
                    D_LABEL_SYM, ARR_AT(rebol_args, idx), arg
                ));
            } else {
#ifdef BIG_ENDIAN
                u32 i = (u32) VAL_INT64(arg);
                memcpy(&VAL_INT64(arg), &i, sizeof(u32));
#endif
                return &VAL_INT64(arg);
            }

        case FFI_TYPE_SINT32:
            if (!IS_INTEGER(arg)) {
                fail (Error_Arg_Type(
                    D_LABEL_SYM, ARR_AT(rebol_args, idx), arg
                ));
            } else {
#ifdef BIG_ENDIAN
                i32 i = (i32) VAL_INT64(arg);
                memcpy(&VAL_INT64(arg), &i, sizeof(i32));
#endif
                return &VAL_INT64(arg);
            }

        case FFI_TYPE_UINT64:
        case FFI_TYPE_SINT64:
            if (!IS_INTEGER(arg))
                fail (Error_Arg_Type(
                    D_LABEL_SYM, ARR_AT(rebol_args, idx), arg
                ));
            return &VAL_INT64(arg);

        case FFI_TYPE_POINTER:
            switch (VAL_TYPE(arg)) {
                case REB_INTEGER:
                    return &VAL_INT64(arg);
                case REB_STRING:
                case REB_BINARY:
                case REB_VECTOR:
                    ptrs[idx] = VAL_RAW_DATA_AT(arg);
                    return &ptrs[idx];
                case REB_CALLBACK:
                    ptrs[idx] = VAL_ROUTINE_DISPATCHER(arg);
                    return &ptrs[idx];
                default:
                    fail (Error_Arg_Type(
                        D_LABEL_SYM, ARR_AT(rebol_args, idx), arg
                    ));
            }

        case FFI_TYPE_FLOAT:
            /* hackish, store the signle precision floating point number in a double precision variable */
            if (!IS_DECIMAL(arg)) {
                fail (Error_Arg_Type(
                    D_LABEL_SYM, ARR_AT(rebol_args, idx), arg
                ));
            } else {
                float a = (float)VAL_DECIMAL(arg);
                memcpy(&VAL_DECIMAL(arg), &a, sizeof(a));
                return &VAL_DECIMAL(arg);
            }

        case FFI_TYPE_DOUBLE:
            if (!IS_DECIMAL(arg))
                fail (Error_Arg_Type(
                    D_LABEL_SYM, ARR_AT(rebol_args, idx), arg
                ));
            return &VAL_DECIMAL(arg);

        case FFI_TYPE_STRUCT:
            if (idx == 0) {/* returning a struct */
                Copy_Struct(&VAL_ROUTINE_RVALUE(rot), &VAL_STRUCT(arg));
            } else {
                if (!IS_STRUCT(arg))
                    fail (Error_Arg_Type(
                        D_LABEL_SYM, ARR_AT(rebol_args, idx), arg
                    ));
            }
            return SER_AT(
                REBYTE,
                VAL_STRUCT_DATA_BIN(arg),
                VAL_STRUCT_OFFSET(arg)
            );

        case FFI_TYPE_VOID:
            if (!idx) {
                return NULL;
            } else {
                fail (Error_Arg_Type(
                    D_LABEL_SYM, ARR_AT(rebol_args, idx), arg
                ));
            }

        default:
            fail (Error_Invalid_Arg(arg));
    }

    return NULL;
}

static void prep_rvalue(REBRIN *rin, REBVAL *val)
{
    ffi_type *rtype = *SER_HEAD(ffi_type*, rin->arg_types);

    switch (rtype->type) {
        case FFI_TYPE_UINT8:
        case FFI_TYPE_SINT8:
        case FFI_TYPE_UINT16:
        case FFI_TYPE_SINT16:
        case FFI_TYPE_UINT32:
        case FFI_TYPE_SINT32:
        case FFI_TYPE_UINT64:
        case FFI_TYPE_SINT64:
        case FFI_TYPE_POINTER:
            SET_INTEGER(val, 0);
            break;
        case FFI_TYPE_FLOAT:
        case FFI_TYPE_DOUBLE:
            SET_DECIMAL(val, 0);
            break;
        case FFI_TYPE_STRUCT:
            VAL_RESET_HEADER(val, REB_STRUCT);
            break;
        case FFI_TYPE_VOID:
            SET_UNSET(val);
            break;
        default:
            fail (Error_Invalid_Arg(val));
    }
}

/* convert the return value to rebol
 */
static void ffi_to_rebol(REBRIN *rin,
                         ffi_type *ffi_rtype,
                         void *ffi_rvalue,
                         REBVAL *rebol_ret)
{
    switch (ffi_rtype->type) {
    case FFI_TYPE_UINT8:
        SET_INTEGER(rebol_ret, *cast(u8*, ffi_rvalue));
        break;

    case FFI_TYPE_SINT8:
        SET_INTEGER(rebol_ret, *cast(i8*, ffi_rvalue));
        break;

    case FFI_TYPE_UINT16:
        SET_INTEGER(rebol_ret, *cast(u16*, ffi_rvalue));
        break;

    case FFI_TYPE_SINT16:
        SET_INTEGER(rebol_ret, *cast(i16*, ffi_rvalue));
        break;

    case FFI_TYPE_UINT32:
        SET_INTEGER(rebol_ret, *cast(u32*, ffi_rvalue));
        break;

    case FFI_TYPE_SINT32:
        SET_INTEGER(rebol_ret, *cast(i32*, ffi_rvalue));
        break;

    case FFI_TYPE_UINT64:
        SET_INTEGER(rebol_ret, *cast(u64*, ffi_rvalue));
        break;

    case FFI_TYPE_SINT64:
        SET_INTEGER(rebol_ret, *cast(i64*, ffi_rvalue));
        break;

    case FFI_TYPE_POINTER:
        SET_INTEGER(rebol_ret, cast(REBUPT, *cast(void**, ffi_rvalue)));
        break;

    case FFI_TYPE_FLOAT:
        SET_DECIMAL(rebol_ret, *cast(float*, ffi_rvalue));
        break;

    case FFI_TYPE_DOUBLE:
        SET_DECIMAL(rebol_ret, *cast(double*, ffi_rvalue));
        break;

    case FFI_TYPE_STRUCT:
        VAL_RESET_HEADER(rebol_ret, REB_STRUCT);
        Copy_Struct(&RIN_RVALUE(rin), &VAL_STRUCT(rebol_ret));
        memcpy(
            SER_AT(
                REBYTE,
                VAL_STRUCT_DATA_BIN(rebol_ret),
                VAL_STRUCT_OFFSET(rebol_ret)
            ),
            ffi_rvalue,
            VAL_STRUCT_LEN(rebol_ret)
        );
        break;

    case FFI_TYPE_VOID:
        break;

    default:
        fail (Error_Invalid_Arg(rebol_ret));
    }
}

//
//  Call_Routine: C
//
void Call_Routine(REBROT *rot, REBARR *args, REBVAL *ret)
{
    REBCNT i = 0;
    void *rvalue = NULL;
    REBSER *ser = NULL;
    void ** ffi_args = NULL;
    REBINT pop = 0;
    REBCNT n_fixed = 0; /* number of fixed arguments */
    REBSER *ffi_args_ptrs = NULL; /* a temprary series to hold pointer parameters */

    struct Reb_Call *call_ = DSF; // So you can use the D_xxx macros

    REBVAL out = *FUNC_VALUE(rot); // REVIEW: why is it done this way?

    // `is_vararg_routine` is optimized out, but hints static analyzer
    const REBOOL is_vararg_routine
        = ROUTINE_GET_FLAG(ROUTINE_INFO(rot), ROUTINE_VARARGS);

    REBVAL *varargs = NULL;

    /* save the saved series stack pointer
     *
     *  Temporary series could be allocated in process_type_block, recursively.
     *  Instead of remembering how many times SAVE_SERIES has called, it's easier to
     *  just remember the initial pointer and restore it later.
    **/
    REBCNT series_guard_tail = SER_LEN(GC_Series_Guard);

    if (ROUTINE_LIB(rot) != NULL) {
        // lib is NULL when routine is constructed from address directly
        if (IS_CLOSED_LIB(ROUTINE_LIB(rot)))
            fail (Error(RE_BAD_LIBRARY));
    }

    if (is_vararg_routine) {
        varargs = ARR_HEAD(args);
        if (!IS_BLOCK(varargs))
            fail (Error_Invalid_Arg(varargs));

        // Note: Must subtract 1 because the [0]th element is reserved in
        // paramlists for the REBVAL of the function itself.
        //
        n_fixed = ARR_LEN(ROUTINE_FIXED_ARGS(rot)) - 1;

        if ((VAL_LEN_AT(varargs) - n_fixed) % 2)
            fail (Error_Invalid_Arg(varargs));

        ser = Make_Series(
            n_fixed + (VAL_LEN_AT(varargs) - n_fixed) / 2,
            sizeof(void *),
            MKS_NONE
        );
    } else if ((SER_LEN(ROUTINE_FFI_ARG_TYPES(rot))) > 1) {
        ser = Make_Series(
            SER_LEN(ROUTINE_FFI_ARG_TYPES(rot)) - 1,
            sizeof(void *),
            MKS_NONE
        );
    }

    /* ser is NULL if the routine takes no arguments */
    if (ser)
        ffi_args = SER_HEAD(void*, ser);

    // must be big enough
    //
    ffi_args_ptrs = Make_Series(
        SER_LEN(ROUTINE_FFI_ARG_TYPES(rot)), sizeof(void *), MKS_NONE
    );

    if (is_vararg_routine) {
        REBCNT j = 1;
        ffi_type **arg_types = NULL;

        // reset length
        SET_SERIES_LEN(ROUTINE_FFI_ARG_TYPES(rot), n_fixed + 1);

        ROUTINE_ALL_ARGS(rot) = Copy_Array_Shallow(ROUTINE_FIXED_ARGS(rot));
        MANAGE_ARRAY(ROUTINE_ALL_ARGS(rot));

        for (i = 1, j = 1; i < VAL_LEN_HEAD(varargs) + 1; i ++, j ++) {
            REBVAL *reb_arg = VAL_ARRAY_AT_HEAD(varargs, i - 1);
            if (i <= n_fixed) { /* fix arguments */
                if (!TYPE_CHECK(
                    ARR_AT(ROUTINE_FIXED_ARGS(rot), i),
                    VAL_TYPE(reb_arg)
                )) {
                    fail (Error_Arg_Type(
                        D_LABEL_SYM,
                        ARR_AT(ROUTINE_FIXED_ARGS(rot), i),
                        reb_arg
                    ));
                }
            } else {
                /* initialize rin->args */
                REBVAL *reb_type = NULL;
                REBVAL *v = NULL;
                if (i == VAL_LEN_HEAD(varargs)) /* type is missing */
                    fail (Error_Invalid_Arg(reb_arg));

                reb_type = VAL_ARRAY_AT_HEAD(varargs, i);
                if (!IS_BLOCK(reb_type))
                    fail (Error_Invalid_Arg(reb_type));

                v = Alloc_Tail_Array(ROUTINE_ALL_ARGS(rot));
                Val_Init_Typeset(v, 0, SYM_ELLIPSIS); //FIXME, be clear
                EXPAND_SERIES_TAIL(ROUTINE_FFI_ARG_TYPES(rot), 1);

                process_type_block(&out, reb_type, j, FALSE);
                i ++;
            }
            ffi_args[j - 1] = arg_to_ffi(
                &out, reb_arg, j, SER_HEAD(void*, ffi_args_ptrs)
            );
        }
        if (ROUTINE_CIF(rot) == NULL) {
            ROUTINE_CIF(rot) = OS_ALLOC(ffi_cif);
            QUEUE_EXTRA_MEM(ROUTINE_INFO(rot), ROUTINE_CIF(rot));
        }

        /* series data could have moved */
        arg_types = SER_HEAD(ffi_type*, ROUTINE_FFI_ARG_TYPES(rot));

        assert(j == SER_LEN(ROUTINE_FFI_ARG_TYPES(rot)));

        if (FFI_OK != ffi_prep_cif_var(
                cast(ffi_cif*, ROUTINE_CIF(rot)),
                cast(ffi_abi, ROUTINE_ABI(rot)),
                n_fixed, /* number of fixed arguments */
                j - 1, /* number of all arguments */
                arg_types[0], /* return type */
                &arg_types[1]
        )) {
            //RL_Print("Couldn't prep CIF_VAR\n");
            fail (Error_Invalid_Arg(varargs));
        }
    }
    else {
        for (i = 1; i < SER_LEN(ROUTINE_FFI_ARG_TYPES(rot)); i ++) {
            ffi_args[i - 1] = arg_to_ffi(
                &out,
                ARR_AT(args, i - 1),
                i,
                SER_HEAD(void*, ffi_args_ptrs)
            );
        }
    }

    prep_rvalue(ROUTINE_INFO(rot), ret);
    rvalue = arg_to_ffi(
        &out, ret, 0, SER_HEAD(void*, ffi_args_ptrs)
    );

    SET_UNSET(&Callback_Error);

    ffi_call(
        cast(ffi_cif*, ROUTINE_CIF(rot)),
        ROUTINE_FUNCPTR(rot),
        rvalue,
        ffi_args
    );

    if (IS_ERROR(&Callback_Error))
        fail (VAL_CONTEXT(&Callback_Error));

    ffi_to_rebol(
        ROUTINE_INFO(rot),
        SER_HEAD(ffi_type*, ROUTINE_FFI_ARG_TYPES(rot))[0],
        rvalue,
        ret
    );

    Free_Series(ffi_args_ptrs);

    if (ser) Free_Series(ser);

    //restore the saved series stack pointer
    SET_SERIES_LEN(GC_Series_Guard, series_guard_tail);
}


//
//  Free_Routine: C
//
void Free_Routine(REBRIN *rin)
{
    REBCNT n = 0;
    for (n = 0; n < SER_LEN(rin->extra_mem); n ++) {
        void *addr = *SER_AT(void*, rin->extra_mem, n);
        //printf("freeing %p\n", addr);
        OS_FREE(addr);
    }

    ROUTINE_CLR_FLAG(rin, ROUTINE_MARK);
    if (IS_CALLBACK_ROUTINE(rin)) {
        ffi_closure_free(RIN_CLOSURE(rin));
    }
    Free_Node(RIN_POOL, (REBNOD*)rin);
}

static void process_type_block(const REBVAL *out, REBVAL *blk, REBCNT n, REBOOL make)
{
    if (IS_BLOCK(blk)) {
        REBVAL *t = VAL_ARRAY_AT(blk);
        if (IS_WORD(t) && VAL_WORD_CANON(t) == SYM_STRUCT_TYPE) {
            /* followed by struct definition */
            REBVAL tmp;
            VAL_INIT_WRITABLE_DEBUG(&tmp);

            SET_NONE(&tmp); // GC should not reach uninitialized values
            PUSH_GUARD_VALUE(&tmp);

            ++ t;
            if (!IS_BLOCK(t) || VAL_LEN_AT(blk) != 2)
                fail (Error_Invalid_Arg(blk));

            if (!MT_Struct(&tmp, t, REB_STRUCT))
                fail (Error_Invalid_Arg(blk));

            if (!rebol_type_to_ffi(out, &tmp, n, make))
                fail (Error_Invalid_Arg(blk));

            DROP_GUARD_VALUE(&tmp);
        }
        else {
            if (VAL_LEN_AT(blk) != 1)
                fail (Error_Invalid_Arg(blk));

            if (!rebol_type_to_ffi(out, t, n, make))
                fail (Error_Invalid_Arg(t));
        }
    }
    else
        fail (Error_Invalid_Arg(blk));
}


static void callback_dispatcher(
    ffi_cif *cif,
    void *ret,
    void **args,
    void *user_data
) {
    REBRIN *rin = (REBRIN*)user_data;
    REBCNT i = 0;
    REBARR *array;
    REBVAL *elem;

    struct Reb_State state;
    REBCTX *error;

    REBVAL safe;
    VAL_INIT_WRITABLE_DEBUG(&safe);

    if (IS_ERROR(&Callback_Error)) return;

    PUSH_TRAP(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

    if (error) {
        Val_Init_Error(&Callback_Error, error);
        return;
    }

    array = Make_Array(1 + cif->nargs);

    // !!! Currently an array must be managed in order to use it with DO,
    // because the series could be put into a block of a backtrace.  That
    // constraint may need to change (for Ren/C++) so this code is set up
    // so these lines (and the DROP_GUARD) can be deleted if that happens.
    //
    MANAGE_ARRAY(array);
    PUSH_GUARD_ARRAY(array);

    elem = Alloc_Tail_Array(array);
    *elem = *FUNC_VALUE(RIN_FUNC(rin));

    for (i = 0; i < cif->nargs; i ++) {
        elem = Alloc_Tail_Array(array);
        switch (cif->arg_types[i]->type) {
            case FFI_TYPE_UINT8:
                SET_INTEGER(elem, *(u8*)args[i]);
                break;
            case FFI_TYPE_SINT8:
                SET_INTEGER(elem, *(i8*)args[i]);
                break;
            case FFI_TYPE_UINT16:
                SET_INTEGER(elem, *(u16*)args[i]);
                break;
            case FFI_TYPE_SINT16:
                SET_INTEGER(elem, *(i16*)args[i]);
                break;
            case FFI_TYPE_UINT32:
                SET_INTEGER(elem, *(u32*)args[i]);
                break;
            case FFI_TYPE_SINT32:
                SET_INTEGER(elem, *(i32*)args[i]);
                break;
            case FFI_TYPE_UINT64:
            case FFI_TYPE_POINTER:
                SET_INTEGER(elem, *(u64*)args[i]);
                break;
            case FFI_TYPE_SINT64:
                SET_INTEGER(elem, *(i64*)args[i]);
                break;
            case FFI_TYPE_STRUCT:
                if (!IS_STRUCT(ARR_AT(RIN_ARGS_STRUCTS(rin), i + 1)))
                    fail (Error_Invalid_Arg(ARR_AT(RIN_ARGS_STRUCTS(rin), i + 1)));

                Copy_Struct_Val(ARR_AT(RIN_ARGS_STRUCTS(rin), i + 1), elem);
                memcpy(
                    SER_AT(
                        REBYTE,
                        VAL_STRUCT_DATA_BIN(elem),
                        VAL_STRUCT_OFFSET(elem)
                    ),
                    args[i],
                    VAL_STRUCT_LEN(elem)
                );
                break;
            default:
                // !!! was fail (Error_Invalid_Arg(elem)), but elem is uninitizalized here
                fail (Error(RE_MISC));
        }
    }

    if (Do_At_Throws(&safe, array, 0)) {
        // !!! Does not check for thrown cases...what should this
        // do in case of THROW, BREAK, QUIT?
        fail (Error_No_Catch_For_Throw(&safe));
    }

    elem = &safe;
    switch (cif->rtype->type) {
        case FFI_TYPE_VOID:
            break;
        case FFI_TYPE_UINT8:
            *((u8*)ret) = (u8)VAL_INT64(elem);
            break;
        case FFI_TYPE_SINT8:
            *((i8*)ret) = (i8)VAL_INT64(elem);
            break;
        case FFI_TYPE_UINT16:
            *((u16*)ret) = (u16)VAL_INT64(elem);
            break;
        case FFI_TYPE_SINT16:
            *((i16*)ret) = (i16)VAL_INT64(elem);
            break;
        case FFI_TYPE_UINT32:
            *((u32*)ret) = (u32)VAL_INT64(elem);
            break;
        case FFI_TYPE_SINT32:
            *((i32*)ret) = (i32)VAL_INT64(elem);
            break;
        case FFI_TYPE_UINT64:
        case FFI_TYPE_POINTER:
            *((u64*)ret) = (u64)VAL_INT64(elem);
            break;
        case FFI_TYPE_SINT64:
            *((i64*)ret) = (i64)VAL_INT64(elem);
            break;
        case FFI_TYPE_STRUCT:
            memcpy(
                ret,
                SER_AT(
                    REBYTE,
                    VAL_STRUCT_DATA_BIN(elem),
                    VAL_STRUCT_OFFSET(elem)
                ),
                VAL_STRUCT_LEN(elem)
            );
            break;
        default:
            fail (Error_Invalid_Arg(elem));
    }

    // !!! Could be a Free_Series if not managed/saved to use with DO
    DROP_GUARD_ARRAY(array);

    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);
}


//
//  MT_Routine: C
// 
// format:
// make routine! [[
//     "document"
//     arg1 [type1 type2] "note"
//     arg2 [type3] "note"
//     ...
//     argn [typen] "note"
//     return: [type] "note"
//     abi: word "note"
// ] lib "name"]
//
REBOOL MT_Routine(REBVAL *out, REBVAL *data, enum Reb_Kind type)
{
    //RL_Print("%s, %d\n", __func__, __LINE__);
    ffi_type ** args = NULL;
    REBVAL *blk = NULL;
    REBCNT eval_idx = 0; /* for spec block evaluation */
    REBSER *extra_mem = NULL;
    REBOOL ret = TRUE;
    CFUNC *func = NULL;
    REBCNT n = 1; /* arguments start with the index 1 (return type has a index of 0) */
    REBCNT has_return = 0;
    REBCNT has_abi = 0;
    REBVAL *temp;

    if (!IS_BLOCK(data)) {
        return FALSE;
    }

    VAL_RESET_HEADER(out, type);

    VAL_ROUTINE_INFO(out) = cast(REBRIN*, Make_Node(RIN_POOL));
    memset(VAL_ROUTINE_INFO(out), 0, sizeof(REBRIN));
    ROUTINE_SET_FLAG(VAL_ROUTINE_INFO(out), ROUTINE_USED);

    if (type == REB_CALLBACK) {
        ROUTINE_SET_FLAG(VAL_ROUTINE_INFO(out), ROUTINE_CALLBACK);
    }

#define N_ARGS 8

    VAL_ROUTINE_SPEC(out) = Copy_Array_Shallow(VAL_ARRAY(data));
    VAL_ROUTINE_FFI_ARG_TYPES(out) =
        Make_Series(N_ARGS, sizeof(ffi_type*), MKS_NONE);
    out->payload.any_function.func = AS_FUNC(Make_Array(N_ARGS));

    // first slot is reserved for the "self", see `struct Reb_Func`
    //
    temp = Alloc_Tail_Array(FUNC_PARAMLIST(out->payload.any_function.func));
    *temp = *out;

    VAL_ROUTINE_FFI_ARG_STRUCTS(out) = Make_Array(N_ARGS);
    // reserve for returning struct
    temp = Alloc_Tail_Array(VAL_ROUTINE_FFI_ARG_STRUCTS(out));

    // !!! should this be VAL_INIT_WRITABLE_DEBUG(), e.g. write-only location?
    //
    SET_NONE(temp);

    VAL_ROUTINE_ABI(out) = FFI_DEFAULT_ABI;
    VAL_ROUTINE_LIB(out) = NULL;

    extra_mem = Make_Series(N_ARGS, sizeof(void*), MKS_NONE);
    VAL_ROUTINE_EXTRA_MEM(out) = extra_mem;

    args = SER_HEAD(ffi_type*, VAL_ROUTINE_FFI_ARG_TYPES(out));
    EXPAND_SERIES_TAIL(VAL_ROUTINE_FFI_ARG_TYPES(out), 1); //reserved for return type
    args[0] = &ffi_type_void; //default return type

    init_type_map();

    blk = VAL_ARRAY_AT(data);

    // For all series we created, we must either free them or hand them over
    // to be managed by the garbage collector.  (They will be invisible to
    // the GC prior to giving them over via Manage_Series.)  On the plus
    // side of making them managed up-front, the GC is responsible for
    // freeing them if there is an error.  On the downside: if any DO
    // operation were to run, the series would be candidates for GC if
    // they are not linked somehow into the transitive closure of the roots.
    //
    ENSURE_ARRAY_MANAGED(VAL_ROUTINE_SPEC(out)); // probably already managed
    MANAGE_SERIES(VAL_ROUTINE_FFI_ARG_TYPES(out));
    MANAGE_ARRAY(VAL_ROUTINE_PARAMLIST(out));
    MANAGE_ARRAY(VAL_ROUTINE_FFI_ARG_STRUCTS(out));
    MANAGE_SERIES(VAL_ROUTINE_EXTRA_MEM(out));

    if (type == REB_ROUTINE) {
        REBIXO indexor = 0;

        REBVAL lib;
        VAL_INIT_WRITABLE_DEBUG(&lib);

        if (!IS_BLOCK(&blk[0]))
            fail (Error_Unexpected_Type(REB_BLOCK, VAL_TYPE(&blk[0])));

        DO_NEXT_MAY_THROW(indexor, &lib, VAL_ARRAY(data), 1);
        if (indexor == THROWN_FLAG)
            fail (Error_No_Catch_For_Throw(&lib));

        if (IS_INTEGER(&lib)) {
            if (indexor != END_FLAG)
                fail (Error_Invalid_Arg(&blk[cast(REBCNT, indexor)]));

            //treated as a pointer to the function
            if (VAL_INT64(&lib) == 0)
                fail (Error_Invalid_Arg(&lib));

            // Cannot cast directly to a function pointer from a 64-bit value
            // on 32-bit systems; first cast to int that holds Unsigned PoinTer
            VAL_ROUTINE_FUNCPTR(out) = cast(CFUNC*,
                cast(REBUPT, VAL_INT64(&lib))
            );
        } else {
            REBSER *byte_sized;
            REBCNT b_index;
            REBCNT b_len;
            REBCNT fn_idx = cast(REBCNT, indexor);

            if (!IS_LIBRARY(&lib))
                fail (Error_Invalid_Arg(&lib));

            if (!IS_STRING(&blk[fn_idx]))
                fail (Error_Invalid_Arg(&blk[fn_idx]));

            if (NOT_END(&blk[fn_idx + 1]))
                fail (Error_Invalid_Arg(&blk[fn_idx + 1]));

            VAL_ROUTINE_LIB(out) = VAL_LIB_HANDLE(&lib);
            if (!VAL_ROUTINE_LIB(out)) {
                fail (Error_Invalid_Arg(&lib));
                //RL_Print("lib is not open\n");
            }
            TERM_SEQUENCE(VAL_SERIES(&blk[fn_idx]));

            // OS_FIND_FUNCTION takes a char* on both Windows and Posix.  The
            // string that gets here could be REBUNI wide or BYTE_SIZE(), so
            // make sure it's turned into a char* before passing.
            //
            // !!! Should it error if any bytes need to be UTF8 encoded?
            //
            b_index = VAL_INDEX(&blk[fn_idx]);
            b_len = VAL_LEN_AT(&blk[fn_idx]);
            byte_sized = Temp_Bin_Str_Managed(&blk[fn_idx], &b_index, &b_len);

            func = OS_FIND_FUNCTION(
                LIB_FD(VAL_ROUTINE_LIB(out)),
                SER_HEAD(char, byte_sized)
            );

            if (!func) {
                fail (Error_Invalid_Arg(&blk[fn_idx]));
                //printf("Couldn't find function: %s\n", VAL_DATA_AT(&blk[2]));
            } else {
                VAL_ROUTINE_FUNCPTR(out) = func;
            }
        }
    } else if (type == REB_CALLBACK) {
        REBIXO indexor = 0;

        REBVAL fun;
        VAL_INIT_WRITABLE_DEBUG(&fun);

        if (!IS_BLOCK(&blk[0]))
            fail (Error_Invalid_Arg(&blk[0]));

        DO_NEXT_MAY_THROW(indexor, &fun, VAL_ARRAY(data), 1);
        if (indexor == THROWN_FLAG)
            fail (Error_No_Catch_For_Throw(&fun));

        if (!IS_FUNCTION(&fun))
            fail (Error_Invalid_Arg(&fun));
        VAL_CALLBACK_FUNC(out) = VAL_FUNC(&fun);

        if (indexor != END_FLAG)
            fail (Error_Invalid_Arg(&blk[cast(REBCNT, indexor)]));

        //printf("RIN: %p, func: %p\n", VAL_ROUTINE_INFO(out), &blk[1]);
    }

    blk = VAL_ARRAY_AT(&blk[0]);
    for (; NOT_END(blk); blk++) {
        if (IS_STRING(blk)) {
            // Notes in the spec, ignore them
            continue;
        }

        switch (VAL_TYPE(blk)) {
            case REB_WORD:
                {
                    REBVAL *v = NULL;
                    if (VAL_WORD_CANON(blk) == SYM_ELLIPSIS) {
                        if (ROUTINE_GET_FLAG(VAL_ROUTINE_INFO(out), ROUTINE_VARARGS)) {
                            fail (Error_Invalid_Arg(blk)); /* duplicate ellipsis */
                        }
                        ROUTINE_SET_FLAG(VAL_ROUTINE_INFO(out), ROUTINE_VARARGS);
                        //Change the argument list to be a block
                        VAL_ROUTINE_FIXED_ARGS(out) = Copy_Array_Shallow(VAL_ROUTINE_PARAMLIST(out));
                        MANAGE_ARRAY(VAL_ROUTINE_FIXED_ARGS(out));
                        Remove_Series(
                            ARR_SERIES(VAL_ROUTINE_PARAMLIST(out)),
                            1,
                            ARR_LEN(VAL_ROUTINE_PARAMLIST(out))
                        );
                        v = Alloc_Tail_Array(VAL_ROUTINE_PARAMLIST(out));
                        Val_Init_Typeset(
                            v, FLAGIT_KIND(REB_BLOCK), SYM_VARARGS
                        );
                    }
                    else {
                        if (ROUTINE_GET_FLAG(VAL_ROUTINE_INFO(out), ROUTINE_VARARGS)) {
                            //... has to be the last argument
                            fail (Error_Invalid_Arg(blk));
                        }
                        v = Alloc_Tail_Array(VAL_ROUTINE_PARAMLIST(out));
                        Val_Init_Typeset(v, 0, VAL_WORD_SYM(blk));
                        EXPAND_SERIES_TAIL(VAL_ROUTINE_FFI_ARG_TYPES(out), 1);

                        ++ blk;
                        process_type_block(out, blk, n, TRUE);
                    }

                    // Function dispatch needs to know whether parameters are
                    // to be hard quoted, soft quoted, refinements, or
                    // evaluated.  This is signaled with bits on the typeset.
                    //
                    SET_VAL_FLAG(v, TYPESET_FLAG_EVALUATE);
                }
                n ++;
                break;
            case REB_SET_WORD:
                switch (VAL_WORD_CANON(blk)) {
                    case SYM_ABI:
                        ++ blk;
                        if (!IS_WORD(blk) || has_abi > 1)
                            fail (Error_Invalid_Arg(blk));

                        switch (VAL_WORD_CANON(blk)) {
                            case SYM_DEFAULT:
                                VAL_ROUTINE_ABI(out) = FFI_DEFAULT_ABI;
                                break;
#ifdef X86_WIN64
                            case SYM_WIN64:
                                VAL_ROUTINE_ABI(out) = FFI_WIN64;
                                break;
#elif defined(X86_WIN32) || defined(TO_LINUX_X86) || defined(TO_LINUX_X64)
                            case SYM_STDCALL:
                                VAL_ROUTINE_ABI(out) = FFI_STDCALL;
                                break;
                            case SYM_SYSV:
                                VAL_ROUTINE_ABI(out) = FFI_SYSV;
                                break;
                            case SYM_THISCALL:
                                VAL_ROUTINE_ABI(out) = FFI_THISCALL;
                                break;
                            case SYM_FASTCALL:
                                VAL_ROUTINE_ABI(out) = FFI_FASTCALL;
                                break;
#ifdef X86_WIN32
                            case SYM_MS_CDECL:
                                VAL_ROUTINE_ABI(out) = FFI_MS_CDECL;
                                break;
#else
                            case SYM_UNIX64:
                                VAL_ROUTINE_ABI(out) = FFI_UNIX64;
                                break;
#endif //X86_WIN32
#elif defined (TO_LINUX_ARM)
                            case SYM_VFP:
                                VAL_ROUTINE_ABI(out) = FFI_VFP;
                            case SYM_SYSV:
                                VAL_ROUTINE_ABI(out) = FFI_SYSV;
                                break;
#elif defined (TO_LINUX_MIPS)
                            case SYM_O32:
                                VAL_ROUTINE_ABI(out) = FFI_O32;
                                break;
                            case SYM_N32:
                                VAL_RNUTINE_ABI(out) = FFI_N32;
                                break;
                            case SYM_N64:
                                VAL_RNUTINE_ABI(out) = FFI_N64;
                                break;
                            case SYM_O32_SOFT_FLOAT:
                                VAL_ROUTINE_ABI(out) = FFI_O32_SOFT_FLOAT;
                                break;
                            case SYM_N32_SOFT_FLOAT:
                                VAL_RNUTINE_ABI(out) = FFI_N32_SOFT_FLOAT;
                                break;
                            case SYM_N64_SOFT_FLOAT:
                                VAL_RNUTINE_ABI(out) = FFI_N64_SOFT_FLOAT;
                                break;
#endif //X86_WIN64
                            default:
                                fail (Error_Invalid_Arg(blk));
                        }
                        has_abi ++;
                        break;
                    case SYM_RETURN:
                        if (has_return > 1) {
                            fail (Error_Invalid_Arg(blk));
                        }
                        has_return ++;
                        ++ blk;
                        process_type_block(out, blk, 0, TRUE);
                        break;
                    default:
                        fail (Error_Invalid_Arg(blk));
                }
                break;
            default:
                fail (Error_Invalid_Arg(blk));
        }
    }

    if (!ROUTINE_GET_FLAG(VAL_ROUTINE_INFO(out), ROUTINE_VARARGS)) {
        VAL_ROUTINE_CIF(out) = OS_ALLOC(ffi_cif);
        //printf("allocated cif at: %p\n", VAL_ROUTINE_CIF(out));
        QUEUE_EXTRA_MEM(VAL_ROUTINE_INFO(out), VAL_ROUTINE_CIF(out));

        /* series data could have moved */
        args = SER_HEAD(ffi_type*, VAL_ROUTINE_FFI_ARG_TYPES(out));
        if (
            FFI_OK != ffi_prep_cif(
                cast(ffi_cif*, VAL_ROUTINE_CIF(out)),
                cast(ffi_abi, VAL_ROUTINE_ABI(out)),
                SER_LEN(VAL_ROUTINE_FFI_ARG_TYPES(out)) - 1,
                args[0],
                &args[1]
            )
        ) {
            //RL_Print("Couldn't prep CIF\n");
            ret = FALSE;
        }
    }

    if (type == REB_CALLBACK) {
        VAL_ROUTINE_CLOSURE(out) = ffi_closure_alloc(sizeof(ffi_closure), &VAL_ROUTINE_DISPATCHER(out));
        if (VAL_ROUTINE_CLOSURE(out) == NULL) {
            //printf("No memory\n");
            ret = FALSE;
        } else {
            ffi_status status;

            status = ffi_prep_closure_loc(
                cast(ffi_closure*, VAL_ROUTINE_CLOSURE(out)),
                cast(ffi_cif*, VAL_ROUTINE_CIF(out)),
                callback_dispatcher,
                VAL_ROUTINE_INFO(out),
                VAL_ROUTINE_DISPATCHER(out)
            );

            if (status != FFI_OK) {
                //RL_Print("Couldn't prep closure\n");
                ret = FALSE;
            }
        }
    }

    //RL_Print("%s, %d, ret = %d\n", __func__, __LINE__, ret);
    return ret;
}

//
//  REBTYPE: C
//
REBTYPE(Routine)
{
    REBVAL *val;
    REBVAL *arg;
    REBVAL *ret;

    arg = D_ARG(2);
    val = D_ARG(1);

    ret = D_OUT;
    // unary actions
    switch(action) {
        case A_MAKE:
            //RL_Print("%s, %d, Make routine action\n", __func__, __LINE__);
        case A_TO:
            if (IS_ROUTINE(val)) {
                fail (Error_Unexpected_Type(REB_ROUTINE, VAL_TYPE(arg)));
            }
            else if (!IS_BLOCK(arg) || !MT_Routine(ret, arg, REB_ROUTINE)) {
                fail (Error_Unexpected_Type(REB_BLOCK, VAL_TYPE(arg)));
            }
            break;
        case A_REFLECT:
            {
                REBINT n = VAL_WORD_CANON(arg); // zero on error
                switch (n) {
                    case SYM_SPEC:
                        Val_Init_Block(
                            ret, Copy_Array_Deep_Managed(VAL_ROUTINE_SPEC(val))
                        );
                        Unbind_Values_Deep(VAL_ARRAY_HEAD(val));
                        break;
                    case SYM_ADDR:
                        SET_INTEGER(ret, cast(REBUPT, VAL_ROUTINE_FUNCPTR(val)));
                        break;
                    default:
                        fail (Error_Cannot_Reflect(REB_STRUCT, arg));
                }
            }
            break;
        default:
            fail (Error_Illegal_Action(REB_ROUTINE, action));
    }
    return R_OUT;
}

//
//  REBTYPE: C
//
REBTYPE(Callback)
{
    REBVAL *val;
    REBVAL *arg;
    REBVAL *ret;

    arg = D_ARG(2);
    val = D_ARG(1);

    ret = D_OUT;
    // unary actions
    switch(action) {
        case A_MAKE:
            //RL_Print("%s, %d, Make routine action\n", __func__, __LINE__);
        case A_TO:
            if (IS_ROUTINE(val)) {
                fail (Error_Unexpected_Type(REB_ROUTINE, VAL_TYPE(arg)));
            }
            else if (!IS_BLOCK(arg) || !MT_Routine(ret, arg, REB_CALLBACK)) {
                fail (Error_Unexpected_Type(REB_BLOCK, VAL_TYPE(arg)));
            }
            break;
        case A_REFLECT:
            {
                REBINT n = VAL_WORD_CANON(arg); // zero on error
                switch (n) {
                    case SYM_SPEC:
                        Val_Init_Block(
                            ret, Copy_Array_Deep_Managed(VAL_ROUTINE_SPEC(val))
                        );
                        Unbind_Values_Deep(VAL_ARRAY_HEAD(val));
                        break;
                    case SYM_ADDR:
                        SET_INTEGER(ret, (REBUPT)VAL_ROUTINE_DISPATCHER(val));
                        break;
                    default:
                        fail (Error_Cannot_Reflect(REB_STRUCT, arg));
                }
            }
            break;
        default:
            fail (Error_Illegal_Action(REB_CALLBACK, action));
    }
    return R_OUT;
}
