//
//  File: %t-routine.c
//  Summary: "Support for calling non-Rebol C functions in DLLs w/Rebol args)"
//  Section: datatypes
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
// This code was contributed by Atronix Engineering:
//
// http://www.atronixengineering.com/downloads/
//
// It will only work if your build (-D)efines "-DHAVE_LIBFFI_AVAILABLE".
//
// Not defining HAVE_LIBFFI_AVAILABLE will produce a short list of
// non-working "stubs" that match the interface of <libffi.h>.  These can
// allow t-routine.c to compile anyway.  That assists with maintenance
// of the code and keeping it on the radar--even among those doing core
// coding who are not building against the FFI.
//

#include "sys-core.h"

#include "mem-pools.h" // low-level memory pool access

#if !defined(HAVE_LIBFFI_AVAILABLE)

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

    ffi_status ffi_prep_cif(
        ffi_cif *cif,
        ffi_abi abi,
        unsigned int nargs,
        ffi_type *rtype,
        ffi_type **atypes
    ){
        UNUSED(cif);
        UNUSED(abi);
        UNUSED(nargs);
        UNUSED(rtype);
        UNUSED(atypes);

        fail (Error_Not_Ffi_Build_Raw());
    }

    ffi_status ffi_prep_cif_var(
        ffi_cif *cif,
        ffi_abi abi,
        unsigned int nfixedargs,
        unsigned int ntotalargs,
        ffi_type *rtype,
        ffi_type **atypes
    ){
        UNUSED(cif);
        UNUSED(abi);
        UNUSED(nfixedargs);
        UNUSED(ntotalargs);
        UNUSED(rtype);
        UNUSED(atypes);

        fail (Error_Not_Ffi_Build_Raw());
    }

    void ffi_call(
        ffi_cif *cif,
        void (*fn)(void),
        void *rvalue,
        void **avalue
    ){
        UNUSED(cif);
        UNUSED(fn);
        UNUSED(rvalue);
        UNUSED(avalue);

        fail (Error_Not_Ffi_Build_Raw());
    }

    void *ffi_closure_alloc(size_t size, void **code) {
        UNUSED(size);
        UNUSED(code);

        fail (Error_Not_Ffi_Build_Raw());
    }

    ffi_status ffi_prep_closure_loc(
        ffi_closure *closure,
        ffi_cif *cif,
        void (*fun)(ffi_cif *, void *, void **, void *),
        void *user_data,
        void *codeloc
    ){
        UNUSED(closure);
        UNUSED(cif);
        UNUSED(fun);
        UNUSED(user_data);
        UNUSED(codeloc);

        fail (Error_Not_Ffi_Build_Raw());
    }

    void ffi_closure_free(void *closure) {
        UNUSED(closure);

        fail (Error_Not_Ffi_Build_Raw());
    }
#endif


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

    fail (word);
}


//
// Writes into `out` a Rebol value representing the "schema", which describes
// either a basic FFI type or the layout of a STRUCT! (not including data).
//
static void Schema_From_Block_May_Fail(
    REBVAL *schema_out, // => INTEGER! or HANDLE! for struct
    REBVAL *param_out, // => TYPESET!
    const REBVAL *blk
){
    TRASH_CELL_IF_DEBUG(schema_out);
    TRASH_CELL_IF_DEBUG(param_out);

    assert(IS_BLOCK(blk));
    if (VAL_LEN_AT(blk) == 0)
        fail (blk);

    RELVAL *item = VAL_ARRAY_AT(blk);

    DECLARE_LOCAL (def);
    DECLARE_LOCAL (temp);

    if (IS_WORD(item) && VAL_WORD_SYM(item) == SYM_STRUCT_X) {
        //
        // [struct! [...struct definition...]]

        ++item;
        if (IS_END(item) || !IS_BLOCK(item))
            fail (blk);

        // Use the block spec to build a temporary structure through the same
        // machinery that implements `make struct! [...]`

        Derelativize(def, item, VAL_SPECIFIER(blk));

        MAKE_Struct(temp, REB_STRUCT, def); // may fail()
        assert(IS_STRUCT(temp));

        // !!! It should be made possible to create a schema without going
        // through a struct creation.  There are "raw" structs with no memory,
        // which would avoid the data series (not the REBSTU array, though)
        //
        Init_Block(schema_out, VAL_STRUCT_SCHEMA(temp));

        // !!! Saying any STRUCT! is legal here in the typeset suggests any
        // structure is legal to pass into a routine.  Yet structs in C
        // have different sizes (and static type checking so you can't pass
        // one structure in the place of another.  Actual struct compatibility
        // is not checked until runtime, when the call happens.
        //
        Init_Typeset(param_out, FLAGIT_KIND(REB_STRUCT), NULL);
        return;
    }

    if (VAL_LEN_AT(blk) != 1)
        fail (blk);

    if (IS_WORD(item)) {
        //
        // Drop the binding off word (then note SYM_VOID turns schema to blank)
        //
        Init_Word(schema_out, VAL_WORD_SPELLING(item));

        switch (VAL_WORD_SYM(item)) {
        case SYM_VOID:
            Init_Blank(schema_out); // only valid for return types
            Init_Typeset(param_out, FLAGIT_KIND(REB_MAX_VOID), NULL);
            break;

        case SYM_UINT8:
            Init_Typeset(param_out, FLAGIT_KIND(REB_INTEGER), NULL);
            break;

        case SYM_INT8:
            Init_Typeset(param_out, FLAGIT_KIND(REB_INTEGER), NULL);
            break;

        case SYM_UINT16:
            Init_Typeset(param_out, FLAGIT_KIND(REB_INTEGER), NULL);
            break;

        case SYM_INT16:
            Init_Typeset(param_out, FLAGIT_KIND(REB_INTEGER), NULL);
            break;

        case SYM_UINT32:
            Init_Typeset(param_out, FLAGIT_KIND(REB_INTEGER), NULL);
            break;

        case SYM_INT32:
            Init_Typeset(param_out, FLAGIT_KIND(REB_INTEGER), NULL);
            break;

        case SYM_UINT64:
            Init_Typeset(param_out, FLAGIT_KIND(REB_INTEGER), NULL);
            break;

        case SYM_INT64:
            Init_Typeset(param_out, FLAGIT_KIND(REB_INTEGER), NULL);
            break;

        case SYM_FLOAT:
            Init_Typeset(param_out, FLAGIT_KIND(REB_DECIMAL), NULL);
            break;

        case SYM_DOUBLE:
            Init_Typeset(param_out, FLAGIT_KIND(REB_DECIMAL), NULL);
            break;

        case SYM_POINTER:
            Init_Typeset(
                param_out,
                FLAGIT_KIND(REB_INTEGER)
                    | FLAGIT_KIND(REB_STRING)
                    | FLAGIT_KIND(REB_BINARY)
                    | FLAGIT_KIND(REB_VECTOR)
                    | FLAGIT_KIND(REB_FUNCTION), // legal if routine or callback
                NULL
            );
            break;

        case SYM_REBVAL:
            Init_Typeset(param_out, ALL_64, NULL);
            break;

        default:
            fail ("Invalid FFI type indicator");
        }
        return;
    }

    fail (blk);
}


//
// According to the libffi documentation, the arguments "must be suitably
// aligned; it is the caller's responsibility to ensure this".
//
// We assume the store's data pointer will have suitable alignment for any
// type (currently Make_Series() is expected to match malloc() in this way).
// This will round the offset positions to an alignment appropriate for the
// type size given.
//
// This means sequential arguments in the store may have padding between them.
//
inline static void *Expand_And_Align_Core(
    REBUPT *offset_out,
    REBCNT align,
    REBSER *store,
    REBCNT size
){
    REBCNT padding = SER_LEN(store) % align;
    if (padding != 0)
        padding = align - padding;

    *offset_out = SER_LEN(store) + padding;
    EXPAND_SERIES_TAIL(store, padding + size);
    return SER_DATA_RAW(store) + *offset_out;
}

inline static void *Expand_And_Align(
    REBUPT *offset_out,
    REBSER *store,
    REBCNT size // assumes align == size
){
    return Expand_And_Align_Core(offset_out, size, store, size);
}


//
// Convert a Rebol value into a bit pattern suitable for the expectations of
// the FFI for how a C argument would be represented.  (e.g. turn an
// INTEGER! into the appropriate representation of an `int` in memory.)
//
static REBUPT arg_to_ffi(
    REBSER *store,
    void *dest,
    const REBVAL *arg,
    const REBVAL *schema,
    const REBVAL *param
){
    // Only one of dest or store should be non-NULL.  This allows to write
    // either to a known pointer of sufficient size (dest) or to a series
    // that will expand enough to accommodate the data (store).
    //
    assert(store == NULL ? dest != NULL : dest == NULL);

#if !defined(NDEBUG)
    //
    // If the value being converted has a "name"--e.g. the FFI Routine
    // interface named it in the spec--then `param` contains that name, for
    // reporting any errors in the conversion.
    //
    // !!! Shouldn't the argument have already had its type checked by the
    // calling process?
    //
    if (param)
        assert(arg != NULL && IS_TYPESET(param));
    else
        assert(arg == NULL); // return value, so just make space (no arg data)
#endif

    REBFRM *frame_ = FS_TOP; // So you can use the D_xxx macros

    REBUPT offset;
    if (dest == NULL)
        offset = 0;
    else
        offset = 10200304; // shouldn't be used, but avoid warning

    if (IS_BLOCK(schema)) {
        REBFLD *top = VAL_ARRAY(schema);

        assert(FLD_IS_STRUCT(top));
        assert(NOT(FLD_IS_ARRAY(top))); // !!! wasn't supported--should be?

        // !!! In theory a struct has to be aligned to its maximal alignment
        // needed by a fundamental member.  We'll assume that the largest
        // is sizeof(void*) here...this may waste some space in the padding
        // between arguments, but that shouldn't have any semantic effect.
        //
        if (dest == NULL)
            dest = Expand_And_Align_Core(
                &offset,
                sizeof(void*),
                store,
                FLD_WIDE(top) // !!! What about FLD_LEN_BYTES_TOTAL ?
            );

        if (arg == NULL) {
            //
            // Return values don't have an incoming argument to fill into the
            // calling frame.
            //
            return offset;
        }

        // !!! There wasn't any compatibility checking here before (not even
        // that the arg was a struct.  :-/  It used a stored STRUCT! from
        // when the routine was specified to know what the size should be,
        // and didn't pay attention to the size of the passed-in struct.
        //
        // (One reason it didn't use the size of the passed-struct is
        // because it couldn't do so in the return case where arg was null)

        if (!IS_STRUCT(arg))
            fail (Error_Arg_Type(D_LABEL_SYM, param, VAL_TYPE(arg)));

        if (STU_SIZE(VAL_STRUCT(arg)) != FLD_WIDE(top))
            fail (Error_Arg_Type(D_LABEL_SYM, param, VAL_TYPE(arg)));

        memcpy(dest, VAL_STRUCT_DATA_AT(arg), STU_SIZE(VAL_STRUCT(arg)));

        return offset;
    }

    assert(IS_WORD(schema));

    switch (VAL_WORD_SYM(schema)) {
    case SYM_UINT8:{
        u8 u;
        if (!dest)
            dest = Expand_And_Align(&offset, store, sizeof(u));
        if (!arg) break;

        if (!IS_INTEGER(arg))
            fail (Error_Arg_Type(D_LABEL_SYM, param, VAL_TYPE(arg)));

        u = cast(u8, VAL_INT64(arg));
        memcpy(dest, &u, sizeof(u));
        break;}

    case SYM_INT8:{
        i8 i;
        if (!dest)
            dest = Expand_And_Align(&offset, store, sizeof(i));
        if (!arg) break;

        if (!IS_INTEGER(arg))
            fail (Error_Arg_Type(D_LABEL_SYM, param, VAL_TYPE(arg)));

        i = cast(i8, VAL_INT64(arg));
        memcpy(dest, &i, sizeof(i));
        break;}

    case SYM_UINT16:{
        u16 u;
        if (!dest)
            dest = Expand_And_Align(&offset, store, sizeof(u));
        if (!arg) break;

        if (!IS_INTEGER(arg))
            fail (Error_Arg_Type(D_LABEL_SYM, param, VAL_TYPE(arg)));

        u = cast(u16, VAL_INT64(arg));
        memcpy(dest, &u, sizeof(u));
        break;}

    case SYM_INT16:{
        i16 i;
        if (!dest)
            dest = Expand_And_Align(&offset, store, sizeof(i));
        if (!arg) break;

        if (!IS_INTEGER(arg))
            fail (Error_Arg_Type(D_LABEL_SYM, param, VAL_TYPE(arg)));

        i = cast(i16, VAL_INT64(arg));
        memcpy(dest, &i, sizeof(i));
        break;}

    case SYM_UINT32:{
        u32 u;
        if (!dest)
            dest = Expand_And_Align(&offset, store, sizeof(u));
        if (!arg) break;

        if (!IS_INTEGER(arg))
            fail (Error_Arg_Type(D_LABEL_SYM, param, VAL_TYPE(arg)));

        u = cast(u32, VAL_INT64(arg));
        memcpy(dest, &u, sizeof(u));
        break;}

    case SYM_INT32:{
        i32 i;
        if (!dest)
            dest = Expand_And_Align(&offset, store, sizeof(i));
        if (!arg) break;

        if (!IS_INTEGER(arg))
            fail (Error_Arg_Type(D_LABEL_SYM, param, VAL_TYPE(arg)));

        i = cast(i32, VAL_INT64(arg));
        memcpy(dest, &i, sizeof(i));
        break;}

    case SYM_UINT64:
    case SYM_INT64:{
        REBI64 i;
        if (!dest)
            dest = Expand_And_Align(&offset, store, sizeof(i));
        if (!arg) break;

        if (!IS_INTEGER(arg))
            fail (Error_Arg_Type(D_LABEL_SYM, param, VAL_TYPE(arg)));

        i = VAL_INT64(arg);
        memcpy(dest, &i, sizeof(REBI64));
        break;}

    case SYM_POINTER:{
        //
        // Note: Function pointers and data pointers may not be same size.
        //
        if (!dest)
            dest = Expand_And_Align(&offset, store, sizeof(void*));
        if (!arg) break;

        switch (VAL_TYPE(arg)) {
        case REB_INTEGER:{
            REBIPT ipt = VAL_INT64(arg); // REBIPT is like C99's intptr_t
            memcpy(dest, &ipt, sizeof(void*));
            break;}

        case REB_STRING:
        case REB_BINARY:
        case REB_VECTOR:{
            // !!! This is a questionable idea, giving out pointers directly
            // into Rebol series data.  One issue is that the recipient of
            // the data doesn't know whether to interpret it as REBYTE[] or as
            // REBUNI[]...because it's passing the raw data of strings which
            // can be wide or not based on things that have happened in the
            // lifetime of that string.  Another is that the data may be
            // relocated in memory if any modifications happen during a
            // callback...so the memory is not "stable".
            //
            REBYTE *raw_ptr = VAL_RAW_DATA_AT(arg);
            memcpy(dest, &raw_ptr, sizeof(raw_ptr)); // copies a *pointer*!
            break;}

        case REB_FUNCTION:{
            if (!IS_FUNCTION_RIN(arg))
                fail (Error_Only_Callback_Ptr_Raw()); // actually routines too

            CFUNC* cfunc = RIN_CFUNC(VAL_FUNC_ROUTINE(arg));
            size_t sizeof_cfunc = sizeof(cfunc); // avoid conditional const
            if (sizeof_cfunc != sizeof(void*)) // not necessarily true
                fail ("Void pointer size not equal to function pointer size");
            memcpy(dest, &cfunc, sizeof(void*));
            break;}

        default:
            fail (Error_Arg_Type(D_LABEL_SYM, param, VAL_TYPE(arg)));
        }
        break;} // end case FFI_TYPE_POINTER

    case SYM_REBVAL: {
        if (!dest)
            dest = Expand_And_Align(&offset, store, sizeof(REBVAL*));
        if (!arg) break;

        memcpy(dest, &arg, sizeof(REBVAL*)); // copies a *pointer*!
        break; }

    case SYM_FLOAT:{
        float f;
        if (!dest)
            dest = Expand_And_Align(&offset, store, sizeof(f));
        if (!arg) break;

        if (!IS_DECIMAL(arg))
            fail (Error_Arg_Type(D_LABEL_SYM, param, VAL_TYPE(arg)));

        f = cast(float, VAL_DECIMAL(arg));
        memcpy(dest, &f, sizeof(f));
        break;}

    case SYM_DOUBLE:{
        double d;
        if (!dest)
            dest = Expand_And_Align(&offset, store, sizeof(d));
        if (!arg) break;

        if (!IS_DECIMAL(arg))
            fail (Error_Arg_Type(D_LABEL_SYM, param, VAL_TYPE(arg)));

        d = VAL_DECIMAL(arg);
        memcpy(dest, &d, sizeof(double));
        break;}

    case SYM_STRUCT_X:
        //
        // structs should be processed above by the HANDLE! case, not WORD!
        //
        assert(FALSE);
    case SYM_VOID:
        //
        // can't return a meaningful offset for "void"--it's only valid for
        // return types, so caller should check and not try to pass it in.
        //
        assert(FALSE);
    default:
        fail (arg);
    }

    return offset;
}


/* convert the return value to rebol
 */
static void ffi_to_rebol(
    REBVAL *out,
    const REBVAL *schema,
    void *ffi_rvalue
) {
    if (IS_BLOCK(schema)) {
        REBFLD *top = VAL_ARRAY(schema);

        assert(FLD_IS_STRUCT(top));
        assert(NOT(FLD_IS_ARRAY(top))); // !!! wasn't supported, should be?

        REBSTU *stu = Alloc_Singular_Array();

        REBSER *data = Make_Series(
            FLD_WIDE(top), // !!! what about FLD_LEN_BYTES_TOTAL ?
            sizeof(REBYTE)
        );
        memcpy(SER_HEAD(REBYTE, data), ffi_rvalue, FLD_WIDE(top));
        MANAGE_SERIES(data);

        VAL_RESET_HEADER(out, REB_STRUCT);
        out->payload.structure.stu = stu;
        out->payload.structure.data = data;
        out->extra.struct_offset = 0;

        *ARR_HEAD(stu) = *out; // save canon value
        SER(stu)->link.schema = top;
        MANAGE_ARRAY(stu);

        assert(STU_DATA_HEAD(stu) == BIN_HEAD(data));
        return;
    }

    assert(IS_WORD(schema));

    switch (VAL_WORD_SYM(schema)) {
    case SYM_UINT8:
        Init_Integer(out, *cast(u8*, ffi_rvalue));
        break;

    case SYM_INT8:
        Init_Integer(out, *cast(i8*, ffi_rvalue));
        break;

    case SYM_UINT16:
        Init_Integer(out, *cast(u16*, ffi_rvalue));
        break;

    case SYM_INT16:
        Init_Integer(out, *cast(i16*, ffi_rvalue));
        break;

    case SYM_UINT32:
        Init_Integer(out, *cast(u32*, ffi_rvalue));
        break;

    case SYM_INT32:
        Init_Integer(out, *cast(i32*, ffi_rvalue));
        break;

    case SYM_UINT64:
        Init_Integer(out, *cast(u64*, ffi_rvalue));
        break;

    case SYM_INT64:
        Init_Integer(out, *cast(i64*, ffi_rvalue));
        break;

    case SYM_POINTER:
        Init_Integer(out, cast(REBUPT, *cast(void**, ffi_rvalue)));
        break;

    case SYM_FLOAT:
        Init_Decimal(out, *cast(float*, ffi_rvalue));
        break;

    case SYM_DOUBLE:
        Init_Decimal(out, *cast(double*, ffi_rvalue));
        break;

    case SYM_REBVAL:
        Move_Value(out, *cast(const REBVAL**, ffi_rvalue));
        break;

    case SYM_VOID:
        assert(FALSE); // not covered by generic routine.
    default:
        assert(FALSE);
        //
        // !!! Was reporting Error_Invalid_Arg on uninitialized `out`
        //
        fail ("Unknown FFI type indicator");
    }
}


//
//  Routine_Dispatcher: C
//
REB_R Routine_Dispatcher(REBFRM *f)
{
    REBRIN *rin = FUNC_ROUTINE(f->phase);

    if (RIN_LIB(rin) == NULL) {
        //
        // lib is NULL when routine is constructed from address directly,
        // so there's nothing to track whether that gets loaded or unloaded
    }
    else {
        if (IS_LIB_CLOSED(RIN_LIB(rin)))
            fail (Error_Bad_Library_Raw());
    }

    REBCNT num_fixed = RIN_NUM_FIXED_ARGS(rin);

    REBCNT num_variable;
    REBDSP dsp_orig = DSP; // variadic args pushed to stack, so save base ptr

    if (NOT(RIN_IS_VARIADIC(rin)))
        num_variable = 0;
    else {
        // The function specification should have one extra parameter for
        // the variadic source ("...")
        //
        assert(FUNC_NUM_PARAMS(f->phase) == num_fixed + 1);

        REBVAL *vararg = FRM_ARG(f, num_fixed + 1); // 1-based
        assert(IS_VARARGS(vararg) && f->binding == NULL);

        // Evaluate the VARARGS! feed of values to the data stack.  This way
        // they will be available to be counted, to know how big to make the
        // FFI argument series.
        //
        do {
            REB_R r = Do_Vararg_Op_May_Throw(f->out, vararg, VARARG_OP_TAKE);

            if (r == R_OUT_IS_THROWN)
                return R_OUT_IS_THROWN;
            if (r == R_VOID)
                break;
            assert(r == R_OUT);

            DS_PUSH(f->out);
            SET_END(f->out); // expected by Do_Vararg_Op
        } while (TRUE);

        // !!! The Atronix va_list interface required a type to be specified
        // for each argument--achieving what you would get if you used a
        // C cast on each variadic argument.  Such as:
        //
        //     printf reduce ["%d, %f" 10 + 20 [int32] 12.34 [float]]
        //
        // While this provides generality, it may be useful to use defaulting
        // like C's where integer types default to `int` and floating point
        // types default to `double`.  In the VARARGS!-based syntax it could
        // offer several possibilities:
        //
        //     (printf "%d, %f" (10 + 20) 12.34)
        //     (printf "%d, %f" [int32 10 + 20] 12.34)
        //     (printf "%d, %f" [int32] 10 + 20 [float] 12.34)
        //
        // For the moment, this is following the idea that there must be
        // pairings of values and then blocks (though the values are evaluated
        // expressions).
        //
        if ((DSP - dsp_orig) % 2 != 0)
            fail ("Variadic FFI functions must alternate blocks and values");

        num_variable = (DSP - dsp_orig) / 2;
    }

    REBCNT num_args = num_fixed + num_variable;

    // The FFI arguments are passed by void*.  Those void pointers point to
    // transformations of the Rebol arguments into ranges of memory of
    // various sizes.  This is the backing store for those arguments, which
    // is appended to for each one.  The memory is freed after the call.
    //
    // The offsets array has one element for each argument.  These point at
    // indexes of where each FFI variable resides.  Offsets are used instead
    // of pointers in case the store has to be resized, which may move the
    // base of the series.  Hence the offsets must be mutated into pointers
    // at the last minute before the FFI call.
    //
    REBSER *store = Make_Series(1, sizeof(REBYTE));

    void *ret_offset;
    if (!IS_BLANK(RIN_RET_SCHEMA(rin))) {
        ret_offset = cast(void*, arg_to_ffi(
            store, // ffi-converted arg appended here
            NULL, // dest pointer must be NULL if store is non-NULL
            NULL, // arg: none (we're only making space--leave uninitialized)
            RIN_RET_SCHEMA(rin),
            NULL // param: none (it's a return value/output)
        ));
    }
    else {
        // Shouldn't be used (assigned to NULL later) but avoid maybe
        // uninitialized warning.
        //
        ret_offset = cast(void*, cast(REBUPT, 0xDECAFBAD));
    }

    REBSER *arg_offsets;
    if (num_args == 0)
        arg_offsets = NULL; // don't waste time with the alloc + free
    else
        arg_offsets = Make_Series(num_args, sizeof(void*));

    // First gather the fixed parameters from the frame.  They are known to
    // be of correct general types (they were checked by Do_Core for the call)
    // but a STRUCT! might not be compatible with the type of STRUCT! in
    // the parameter specification.  They might also be out of range, e.g.
    // a too-large or negative INTEGER! passed to a uint8.  Could fail() here.
    //
    {
        REBCNT i = 0;
        for (; i < num_fixed; ++i) {
            REBUPT offset = arg_to_ffi(
                store, // ffi-converted arg appended here
                NULL, // dest pointer must be NULL if store is non-NULL
                FRM_ARG(f, i + 1), // 1-based
                RIN_ARG_SCHEMA(rin, i), // 0-based
                FUNC_PARAM(f->phase, i + 1) // 1-based
            );

            // We will convert the offset to a pointer later
            //
            *SER_AT(void*, arg_offsets, i) = cast(void*, offset);
        }
    }

    // If an FFI routine takes a fixed number of arguments, then its Call
    // InterFace (CIF) can be created just once.  This will be in the RIN_CIF.
    // However a variadic routine requires a CIF that matches the number
    // and types of arguments for that specific call.
    //
    // Note that because these pointers need to be freed by HANDLE! cleanup,
    // they need to remember the size.  OS_ALLOC() is used, at least until
    // HANDLE! is changed to support sizes.
    //
    ffi_cif *cif; // pre-made if not variadic, built for this call otherwise
    ffi_type **args_fftypes = NULL; // ffi_type*[] if num_variable > 0

    if (NOT(RIN_IS_VARIADIC(rin))) {
        cif = RIN_CIF(rin);
    }
    else {
        assert(IS_BLANK(RIN_AT(rin, IDX_ROUTINE_CIF)));

        // CIF creation requires a C array of argument descriptions that is
        // contiguous across both the fixed and variadic parts.  Start by
        // filling in the ffi_type*s for all the fixed args.
        //
        args_fftypes = OS_ALLOC_N(ffi_type*, num_fixed + num_variable);

        REBCNT i;
        for (i = 0; i < num_fixed; ++i)
            args_fftypes[i] = SCHEMA_FFTYPE(RIN_ARG_SCHEMA(rin, i));

        DECLARE_LOCAL (schema);
        DECLARE_LOCAL (param);

        REBDSP dsp;
        for (dsp = dsp_orig + 1; i < num_args; dsp += 2, ++i) {
            //
            // This param is used with the variadic type spec, and is
            // initialized as it would be for an ordinary FFI argument.  This
            // means its allowed type flags are set, which is not really
            // necessary.  Whatever symbol name is used here will be seen
            // in error reports.
            //
            Schema_From_Block_May_Fail(
                schema,
                param, // sets type bits in param
                DS_AT(dsp + 1) // will error if this is not a block
            );

            args_fftypes[i] = SCHEMA_FFTYPE(schema);

            INIT_TYPESET_NAME(param, Canon(SYM_ELLIPSIS));

            *SER_AT(void*, arg_offsets, i) = cast(void*, arg_to_ffi(
                store, // data appended to store
                NULL, // dest pointer must be NULL if store is non-NULL
                DS_AT(dsp), // arg
                schema,
                param // used for typecheck, VAL_TYPESET_SYM for error msgs
            ));
        }

        DS_DROP_TO(dsp_orig); // done w/args (converted to bytes in `store`)

        cif = OS_ALLOC(ffi_cif);

        ffi_status status = ffi_prep_cif_var( // "_var"-iadic prep_cif version
            cif,
            RIN_ABI(rin),
            num_fixed, // just fixed
            num_args, // fixed plus variable
            IS_BLANK(RIN_RET_SCHEMA(rin))
                ? &ffi_type_void
                : SCHEMA_FFTYPE(RIN_RET_SCHEMA(rin)), // return FFI type
            args_fftypes // arguments FFI types
        );

        if (status != FFI_OK) {
            OS_FREE(cif);
            OS_FREE(args_fftypes);
            fail ("FFI: Couldn't prep CIF_VAR");
        }
    }

    // Now that all the additions to store have been made, we want to change
    // the offsets of each FFI argument into actual pointers (since the
    // data won't be relocated)
    {
        if (IS_BLANK(RIN_RET_SCHEMA(rin)))
            ret_offset = NULL;
        else
            ret_offset = SER_DATA_RAW(store) + cast(REBUPT, ret_offset);

        REBCNT i;
        for (i = 0; i < num_args; ++i) {
            REBUPT off = cast(REBUPT, *SER_AT(void*, arg_offsets, i));
            assert(off == 0 || off < SER_LEN(store));
            *SER_AT(void*, arg_offsets, i) = SER_DATA_RAW(store) + off;
        }
    }

    // ** THE ACTUAL FFI CALL **
    //
    // Note that the "offsets" are now actually pointers.
    {
        Init_Unreadable_Blank(&Callback_Error); // !!! is it already?

        ffi_call(
            cif,
            RIN_CFUNC(rin),
            ret_offset, // actually a real pointer now (no longer an offset)
            (num_args == 0)
                ? NULL
                : SER_HEAD(void*, arg_offsets) // also real pointers now
        );

        if (!IS_BLANK_RAW(&Callback_Error))
            fail (VAL_CONTEXT(&Callback_Error)); // asserts if not ERROR!
    }

    if (IS_BLANK(RIN_RET_SCHEMA(rin)))
        Init_Void(f->out);
    else
        ffi_to_rebol(f->out, RIN_RET_SCHEMA(rin), ret_offset);

    if (num_args != 0)
        Free_Series(arg_offsets);

    Free_Series(store);

    if (num_variable != 0) {
        OS_FREE(cif);
        OS_FREE(args_fftypes);
    }

    // Note: cannot "throw" a Rebol value across an FFI boundary.

    assert(!THROWN(f->out));
    return R_OUT;
}


// The GC-able HANDLE! used by callbacks contains a ffi_closure pointer that
// needs to be freed when the handle references go away (really only one
// reference is likely--in the FUNC_BODY of the callback, but still this is
// how the GC gets hooked in Ren-C)
//
static void cleanup_ffi_closure(const REBVAL *v) {
    ffi_closure_free(VAL_HANDLE_POINTER(ffi_closure, v));
}

static void cleanup_cif(const REBVAL *v) {
    FREE(ffi_cif, VAL_HANDLE_POINTER(ffi_cif, v));
}

static void cleanup_args_fftypes(const REBVAL *v) {
    FREE_N(ffi_type*, VAL_HANDLE_LEN(v), VAL_HANDLE_POINTER(ffi_type*, v));
}


//
// Callbacks allow C code to call Rebol functions.  It does so by creating a
// stub function pointer that can be passed in slots where C code expected
// a C function pointer.  When such stubs are triggered, the FFI will call
// this dispatcher--which was registered using ffi_prep_closure_loc().
//
// An example usage of this feature is in %qsort.r, where the C library
// function qsort() is made to use a custom comparison function that is
// actually written in Rebol.
//
static void callback_dispatcher(
    ffi_cif *cif,
    void *ret,
    void **args,
    void *user_data
){
    if (!IS_BLANK_RAW(&Callback_Error)) // !!!is this possible?
        return;

    REBRIN *rin = cast(REBRIN*, user_data);
    assert(!RIN_IS_VARIADIC(rin));
    assert(cif->nargs == RIN_NUM_FIXED_ARGS(rin));

    // We do not want to longjmp() out of the callback if there is an error.
    // It needs to allow the FFI processing to unwind the stack normally so
    // that it's in a good state.  Therefore this must trap any fail()s.
    //
    struct Reb_State state;
    REBCTX *error;

    PUSH_TRAP(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

    if (error) {
        Init_Error(&Callback_Error, error);
        return;
    }

    // Build an array of code to run which represents the call.  The first
    // item in that array will be the callback function value, and then
    // the arguments will be the remaining values.
    //
    REBARR *code = Make_Array(1 + cif->nargs);
    RELVAL *elem = ARR_HEAD(code);
    Move_Value(elem, FUNC_VALUE(RIN_CALLBACK_FUNC(rin)));
    ++elem;

    REBCNT i;
    for (i = 0; i < cif->nargs; ++i, ++elem)
        ffi_to_rebol(SINK(elem), RIN_ARG_SCHEMA(rin, i), args[i]);

    TERM_ARRAY_LEN(code, 1 + cif->nargs);
    MANAGE_ARRAY(code); // DO requires managed arrays (guarded while running)

    DECLARE_LOCAL (result);
    if (Do_At_Throws(result, code, 0, SPECIFIED))
        fail (Error_No_Catch_For_Throw(result)); // !!! Tunnel throws out?

    if (cif->rtype->type == FFI_TYPE_VOID)
        assert(IS_BLANK(RIN_RET_SCHEMA(rin)));
    else {
        DECLARE_LOCAL (param);
        Init_Typeset(param, 0, Canon(SYM_RETURN));
        arg_to_ffi(
            NULL, // store must be NULL if dest is non-NULL,
            ret, // destination pointer
            result,
            RIN_RET_SCHEMA(rin),
            param // parameter used for symbol in error only
        );
    }

    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);
}


//
//  Alloc_Ffi_Function_For_Spec: C
//
// This allocates a REBFUN designed for using with the FFI--though it does
// not fill in the actual code to call.  That is done by the caller, which
// needs to be done differently if it runs a C function (routine) or if it
// makes Rebol code callable as if it were a C function (callback).
//
// It has a HANDLE! holding a Routine INfo structure (RIN) which describes
// the FFI argument types.  For callbacks, this cannot be automatically
// deduced from the parameters of the Rebol function it wraps--because there
// are multiple possible mappings (e.g. differently sized C types all of
// which are passed in from Rebol's INTEGER!)
//
// The spec format is a block which is similar to the spec for functions:
//
// [
//     "document"
//     arg1 [type1 type2] "note"
//     arg2 [type3] "note"
//     ...
//     argn [typen] "note"
//     return: [type] "note"
// ]
//
static REBFUN *Alloc_Ffi_Function_For_Spec(REBVAL *ffi_spec, ffi_abi abi) {
    assert(IS_BLOCK(ffi_spec));

    REBRIN *r = Make_Array(IDX_ROUTINE_MAX);

    Init_Integer(RIN_AT(r, IDX_ROUTINE_ABI), abi);

    // Caller will update these in the returned function.
    //
    Init_Unreadable_Blank(RIN_AT(r, IDX_ROUTINE_CFUNC));
    Init_Unreadable_Blank(RIN_AT(r, IDX_ROUTINE_CLOSURE));
    Init_Unreadable_Blank(RIN_AT(r, IDX_ROUTINE_ORIGIN)); // LIBRARY!/FUNCTION!

    Init_Blank(RIN_AT(r, IDX_ROUTINE_RET_SCHEMA)); // returns void as default

    const REBCNT capacity_guess = 8; // !!! Magic number...why 8? (can grow)

    REBARR *paramlist = Make_Array(capacity_guess);

    // first slot is reserved for the "canon value", see `struct Reb_Function`
    //
    REBVAL *rootparam = Alloc_Tail_Array(paramlist);

    // arguments can be complex, defined as structures.  A "schema" is a
    // REBVAL that holds either an INTEGER! for simple types, or a HANDLE!
    // for compound ones.
    //
    // Note that in order to avoid deep walking the schemas after construction
    // to convert them from unmanaged to managed, they are managed at the
    // time of creation.  This means that the array of them has to be
    // guarded across any evaluations, since the routine being built is not
    // ready for GC visibility.
    //
    // !!! Should the spec analysis be allowed to do evaluation? (it does)
    //
    REBARR *args_schemas = Make_Array(capacity_guess);
    MANAGE_ARRAY(args_schemas);
    PUSH_GUARD_ARRAY(args_schemas);

    REBCNT num_fixed = 0; // number of fixed (non-variadic) arguments
    REBOOL is_variadic = FALSE; // default to not being variadic

    RELVAL *item = VAL_ARRAY_AT(ffi_spec);
    for (; NOT_END(item); ++item) {
        if (IS_STRING(item))
            continue; // !!! TBD: extract FUNC_META information from spec notes

        switch (VAL_TYPE(item)) {
        case REB_WORD:{
            REBSTR *name = VAL_WORD_SPELLING(item);

            if (SAME_STR(name, Canon(SYM_ELLIPSIS))) { // variadic
                if (is_variadic)
                    fail ("FFI: Duplicate ... indicating variadic");

                is_variadic = TRUE;

                REBVAL *param = Alloc_Tail_Array(paramlist);

                // Currently the rule is that if VARARGS! is itself a valid
                // parameter type, then the varargs will not chain.  We want
                // chaining as opposed to passing the parameter pack to the
                // C code to process (it wouldn't know what to do with it)
                //
                Init_Typeset(
                    param,
                    ALL_64 & ~FLAGIT_KIND(REB_VARARGS),
                    Canon(SYM_VARARGS)
                );
                SET_VAL_FLAG(param, TYPESET_FLAG_VARIADIC);
                INIT_VAL_PARAM_CLASS(param, PARAM_CLASS_NORMAL);
            }
            else { // ordinary argument
                if (is_variadic)
                    fail ("FFI: Variadic must be final parameter");

                REBVAL *param = Alloc_Tail_Array(paramlist);

                ++item;

                DECLARE_LOCAL (block);
                Derelativize(block, item, VAL_SPECIFIER(ffi_spec));

                Schema_From_Block_May_Fail(
                    Alloc_Tail_Array(args_schemas), // schema (out)
                    param, // param (out)
                    block // block (in)
                );

                INIT_TYPESET_NAME(param, name);
                INIT_VAL_PARAM_CLASS(param, PARAM_CLASS_NORMAL);
                ++num_fixed;
            }
            break;}

        case REB_SET_WORD:
            switch (VAL_WORD_SYM(item)) {
            case SYM_RETURN:{
                if (!IS_BLANK(RIN_AT(r, IDX_ROUTINE_RET_SCHEMA)))
                    fail ("FFI: Return already specified");

                ++item;

                DECLARE_LOCAL (block);
                Derelativize(block, item, VAL_SPECIFIER(ffi_spec));

                DECLARE_LOCAL (param);
                Schema_From_Block_May_Fail(
                    RIN_AT(r, IDX_ROUTINE_RET_SCHEMA),
                    param, // dummy (a return/output has no arg to typecheck)
                    block
                );
                break;}

            default:
                fail (KNOWN(item));
            }
            break;

        default:
            fail (KNOWN(item));
        }
    }

    Init_Logic(RIN_AT(r, IDX_ROUTINE_IS_VARIADIC), is_variadic);

    TERM_ARRAY_LEN(r, IDX_ROUTINE_MAX);
    ASSERT_ARRAY(args_schemas);
    Init_Block(RIN_AT(r, IDX_ROUTINE_ARG_SCHEMAS), args_schemas);

    if (RIN_IS_VARIADIC(r)) {
        //
        // Each individual call needs to use `ffi_prep_cif_var` to make the
        // proper variadic CIF for that call.
        //
        Init_Blank(RIN_AT(r, IDX_ROUTINE_CIF));
        Init_Blank(RIN_AT(r, IDX_ROUTINE_ARG_FFTYPES));
    }
    else {
        // The same CIF can be used for every call of the routine if it is
        // not variadic.  The CIF must stay alive for the entire the lifetime
        // of the args_fftypes, apparently.
        //
        ffi_cif *cif = ALLOC(ffi_cif);

        ffi_type **args_fftypes;
        if (num_fixed == 0)
            args_fftypes = NULL;
        else
            args_fftypes = ALLOC_N(ffi_type*, num_fixed);

        REBCNT i;
        for (i = 0; i < num_fixed; ++i)
            args_fftypes[i] = SCHEMA_FFTYPE(RIN_ARG_SCHEMA(r, i));

        if (
            FFI_OK != ffi_prep_cif(
                cif,
                abi,
                num_fixed,
                IS_BLANK(RIN_RET_SCHEMA(r))
                    ? &ffi_type_void
                    : SCHEMA_FFTYPE(RIN_RET_SCHEMA(r)),
                args_fftypes // NULL if 0 fixed args
            )
        ){
            fail ("FFI: Couldn't prep CIF");
        }

        Init_Handle_Managed(
            RIN_AT(r, IDX_ROUTINE_CIF),
            cif,
            0,
            &cleanup_cif
        );

        if (args_fftypes == NULL)
            Init_Blank(RIN_AT(r, IDX_ROUTINE_ARG_FFTYPES));
        else
            Init_Handle_Managed(
                RIN_AT(r, IDX_ROUTINE_ARG_FFTYPES),
                args_fftypes,
                num_fixed,
                &cleanup_args_fftypes
            ); // lifetime must match cif lifetime
    }

    DROP_GUARD_ARRAY(args_schemas);

    // Now fill in the canon value of the paramlist so it is an actual "REBFUN"
    //
    VAL_RESET_HEADER(rootparam, REB_FUNCTION);
    rootparam->payload.function.paramlist = paramlist;
    rootparam->extra.binding = NULL;

    SET_SER_FLAG(paramlist, ARRAY_FLAG_PARAMLIST);
    MANAGE_ARRAY(paramlist);
    SER(paramlist)->link.meta = NULL;

    REBFUN *fun = Make_Function(
        paramlist,
        &Routine_Dispatcher,
        NULL, // no facade (use paramlist)
        NULL // no specialization exemplar (or inherited exemplar)
    );

    // The "body" value of a routine is the routine info array.
    //
    Init_Block(FUNC_BODY(fun), r);

    return fun; // still needs to have function or callback info added!
}


//
//  make-routine: native [
//
//  {Create a bridge for interfacing with arbitrary C code in a DLL}
//
//      return: [function!]
//      lib [library!]
//          {Library DLL that function lives in (get with MAKE LIBRARY!)}
//      name [string!]
//          {Linker name of the function in the DLL}
//      ffi-spec [block!]
//          {Description of what C argument types the function takes}
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
    INCLUDE_PARAMS_OF_MAKE_ROUTINE;

    ffi_abi abi;
    if (REF(abi))
        abi = Abi_From_Word(ARG(abi_type));
    else
        abi = FFI_DEFAULT_ABI;

    // Make sure library wasn't closed with CLOSE
    //
    REBLIB *lib = VAL_LIBRARY(ARG(lib));
    if (lib == NULL)
        fail (ARG(lib));

    // Try to find the C function pointer in the DLL, if it's there.
    // OS_FIND_FUNCTION takes a char* on both Windows and Posix.  The
    // string that gets here could be REBUNI wide or BYTE_SIZE(), so
    // make sure it's turned into a char* before passing.
    //
    // !!! Should it error if any bytes need to be UTF8 encoded?
    //
    REBVAL *name = ARG(name);
    REBCNT b_index = VAL_INDEX(name);
    REBCNT b_len = VAL_LEN_AT(name);
    REBSER *byte_sized = Temp_Bin_Str_Managed(name, &b_index, &b_len);

    CFUNC *cfunc = OS_FIND_FUNCTION(
        LIB_FD(lib),
        SER_AT(char, byte_sized, b_index) // name may not be at head index
    );
    if (cfunc == NULL)
        fail ("FFI: Couldn't find function in library");

    // Process the parameter types into a function, then fill it in

    REBFUN *fun = Alloc_Ffi_Function_For_Spec(ARG(ffi_spec), abi);
    REBRIN *r = FUNC_ROUTINE(fun);

    Init_Handle_Cfunc(RIN_AT(r, IDX_ROUTINE_CFUNC), cfunc, 0);
    Move_Value(RIN_AT(r, IDX_ROUTINE_ORIGIN), ARG(lib));

    Move_Value(D_OUT, FUNC_VALUE(fun));
    return R_OUT;
}


//
//  make-routine-raw: native [
//
//  {Create a bridge for interfacing with a C function, by pointer}
//
//      return: [function!]
//      pointer [integer!]
//          {Raw address of function in memory}
//      ffi-spec [block!]
//          {Description of what C argument types the function takes}
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
    INCLUDE_PARAMS_OF_MAKE_ROUTINE_RAW;

    ffi_abi abi;
    if (REF(abi))
        abi = Abi_From_Word(ARG(abi_type));
    else
        abi = FFI_DEFAULT_ABI;

    // Cannot cast directly to a function pointer from a 64-bit value
    // on 32-bit systems; first cast to (U)nsigned int that holds (P)oin(T)er
    //
    CFUNC *cfunc = cast(CFUNC*, cast(REBUPT, VAL_INT64(ARG(pointer))));
    if (cfunc == NULL)
        fail ("FFI: NULL pointer not allowed for raw MAKE-ROUTINE");

    REBFUN *fun = Alloc_Ffi_Function_For_Spec(ARG(ffi_spec), abi);
    REBRIN *r = FUNC_ROUTINE(fun);

    Init_Handle_Cfunc(RIN_AT(r, IDX_ROUTINE_CFUNC), cfunc, 0);
    Init_Blank(RIN_AT(r, IDX_ROUTINE_ORIGIN)); // no LIBRARY! in this case.

    Move_Value(D_OUT, FUNC_VALUE(fun));
    return R_OUT;
}


//
//  make-callback: native [
//
//  {Wrap function so it can be called in raw C code with a function pointer.}
//
//      return: [function!]
//      action [function!]
//          {The existing Rebol function whose functionality is being wrapped}
//      ffi-spec [block!]
//          {Description of what C types each Rebol argument should map to}
//      /abi
//          {Specify the Application Binary Interface (vs. using default)}
//      abi-type [word!]
//          {'CDECL, 'FASTCALL, 'STDCALL, etc.}
//  ]
//
REBNATIVE(make_callback)
{
    INCLUDE_PARAMS_OF_MAKE_CALLBACK;

    ffi_abi abi;
    if (REF(abi))
        abi = Abi_From_Word(ARG(abi_type));
    else
        abi = FFI_DEFAULT_ABI;

    REBFUN *fun = Alloc_Ffi_Function_For_Spec(ARG(ffi_spec), abi);
    REBRIN *r = FUNC_ROUTINE(fun);

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

    size_t sizeof_CFUNC_ptr = sizeof(CFUNC*);
    if (sizeof_CFUNC_ptr != sizeof(void*))
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

    Move_Value(D_OUT, FUNC_VALUE(fun));
    return R_OUT;
}
