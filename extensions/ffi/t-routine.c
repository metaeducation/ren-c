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
// Copyright 2014-2017 Ren-C Open Source Contributors
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

#include "sys-core.h"

#include "reb-struct.h"

// Currently there is a static linkage dependency on the implementation
// details of VECTOR!.  Trust the build system got the include directory in.
//
#include "sys-vector.h"


static struct {
    REBSYM sym;
    uintptr_t bits;
} syms_to_typesets[] = {
    {SYM_VOID, FLAGIT_KIND(REB_VOID)},
    {SYM_UINT8, FLAGIT_KIND(REB_INTEGER)},
    {SYM_INT8, FLAGIT_KIND(REB_INTEGER)},
    {SYM_UINT16, FLAGIT_KIND(REB_INTEGER)},
    {SYM_INT16, FLAGIT_KIND(REB_INTEGER)},
    {SYM_UINT32, FLAGIT_KIND(REB_INTEGER)},
    {SYM_INT32, FLAGIT_KIND(REB_INTEGER)},
    {SYM_UINT64, FLAGIT_KIND(REB_INTEGER)},
    {SYM_INT64, FLAGIT_KIND(REB_INTEGER)},
    {SYM_FLOAT, FLAGIT_KIND(REB_DECIMAL)},
    {SYM_DOUBLE, FLAGIT_KIND(REB_DECIMAL)},
    {
        SYM_POINTER,
        FLAGIT_KIND(REB_INTEGER)
            | FLAGIT_KIND(REB_NULLED)   // Rebol's null as 0 seems sensible
            | FLAGIT_KIND(REB_TEXT)
            | FLAGIT_KIND(REB_BINARY)
            | FLAGIT_KIND(REB_CUSTOM)  // !!! Was REB_VECTOR, must narrow (!)
            | FLAGIT_KIND(REB_ACTION)  // legal if routine or callback
    },
    {SYM_REBVAL, TS_VALUE},
    {SYM_0, 0}
};


//
// Writes into `schema_out` a Rebol value which describes either a basic FFI
// type or the layout of a STRUCT! (not including data).
//
static void Schema_From_Block_May_Fail(
    RELVAL *schema_out,  // => INTEGER! or HANDLE! for struct
    RELVAL *opt_param_out,  // => parameter for use in ACTION!s
    const REBVAL *blk,
    const REBSTR *opt_spelling
){
    TRASH_CELL_IF_DEBUG(schema_out);
    if (not opt_spelling)
        assert(not opt_param_out);
    else {
        assert(opt_param_out);
        TRASH_CELL_IF_DEBUG(opt_param_out);
    }

    assert(IS_BLOCK(blk));
    if (VAL_LEN_AT(blk) == 0)
        fail (blk);

    const RELVAL *item = VAL_ARRAY_AT(blk);

    DECLARE_LOCAL (def);
    DECLARE_LOCAL (temp);

    if (IS_WORD(item) and VAL_WORD_SYM(item) == SYM_STRUCT_X) {
        //
        // [struct! [...struct definition...]]

        ++item;
        if (IS_END(item) or not IS_BLOCK(item))
            fail (blk);

        // Use the block spec to build a temporary structure through the same
        // machinery that implements `make struct! [...]`

        Derelativize(def, item, VAL_SPECIFIER(blk));

        MAKE_Struct(temp, REB_CUSTOM, nullptr, def);  // may fail()
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
        if (opt_spelling)
            Init_Param(
                opt_param_out,
                REB_P_NORMAL,
                opt_spelling,
                FLAGIT_KIND(REB_CUSTOM)  // !!! Was REB_STRUCT, must narrow!
            );
        return;
    }

    if (IS_STRUCT(item)) {
        Init_Block(schema_out, VAL_STRUCT_SCHEMA(item));
        if (opt_spelling)
            Init_Param(
                opt_param_out,
                REB_P_NORMAL,
                opt_spelling,
                FLAGIT_KIND(REB_CUSTOM)  // !!! Was REB_STRUCT, must narrow!
            );
        return;
    }

    if (VAL_LEN_AT(blk) != 1)
        fail (blk);

    // !!! It was presumed the only parameter convention that made sense was
    // a normal args, but quoted ones could work too.  In particular, anything
    // passed to the C as a REBVAL*.  Not a huge priority.
    //
    if (not IS_WORD(item))
        fail (blk);

    Init_Word(schema_out, VAL_WORD_SPELLING(item));

    REBSYM sym = VAL_WORD_SYM(item);
    if (sym == SYM_VOID) {
        assert(
            not opt_param_out
            or VAL_PARAM_SYM(opt_param_out) == SYM_RETURN
        );  // can only do void for return types
        Init_Blank(schema_out);
    }

    if (opt_spelling) {
        int index = 0;
        for (; ; ++index) {
            if (syms_to_typesets[index].sym == REB_0)
                fail ("Invalid FFI type indicator");

            if (SAME_SYM_NONZERO(syms_to_typesets[index].sym, sym)) {
                Init_Param(
                    opt_param_out,
                    REB_P_NORMAL,
                    opt_spelling,
                    syms_to_typesets[index].bits
                );
                break;
            }
        }
    }
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
    uintptr_t *offset_out,
    REBLEN align,
    REBBIN *store,
    REBLEN size
){
    REBLEN padding = BIN_LEN(store) % align;
    if (padding != 0)
        padding = align - padding;

    *offset_out = BIN_LEN(store) + padding;
    EXPAND_SERIES_TAIL(store, padding + size);
    return SER_DATA(store) + *offset_out;
}

inline static void *Expand_And_Align(
    uintptr_t *offset_out,
    REBBIN *store,
    REBLEN size // assumes align == size
){
    return Expand_And_Align_Core(offset_out, size, store, size);
}


//
// Convert a Rebol value into a bit pattern suitable for the expectations of
// the FFI for how a C argument would be represented.  (e.g. turn an
// INTEGER! into the appropriate representation of an `int` in memory.)
//
static uintptr_t arg_to_ffi(
    REBBIN *store,
    void *dest,
    const REBVAL *arg,
    const REBVAL *schema,
    const REBVAL *param
){
    // Only one of dest or store should be non-nullptr.  This allows to write
    // either to a known pointer of sufficient size (dest) or to a series
    // that will expand enough to accommodate the data (store).
    //
    assert(store == nullptr ? dest != nullptr : dest == nullptr);

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
        assert(arg != nullptr and IS_PARAM(param));
    else
        assert(arg == nullptr);  // return value, just make space (no arg)
  #endif

    REBFRM *frame_ = FS_TOP;  // So you can use the D_xxx macros

    uintptr_t offset;
    if (dest == nullptr)
        offset = 0;
    else
        offset = 10200304;  // shouldn't be used, but avoid warning

    if (IS_BLOCK(schema)) {
        REBFLD *top = VAL_ARRAY_KNOWN_MUTABLE(schema);

        assert(FLD_IS_STRUCT(top));
        assert(not FLD_IS_ARRAY(top));  // !!! wasn't supported--should be?

        // !!! In theory a struct has to be aligned to its maximal alignment
        // needed by a fundamental member.  We'll assume that the largest
        // is sizeof(void*) here...this may waste some space in the padding
        // between arguments, but that shouldn't have any semantic effect.
        //
        if (dest == nullptr)
            dest = Expand_And_Align_Core(
                &offset,
                sizeof(void*),
                store,
                FLD_WIDE(top)  // !!! What about FLD_LEN_BYTES_TOTAL ?
            );

        if (arg == nullptr) {
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

        if (not IS_STRUCT(arg))
            fail (Error_Arg_Type(D_FRAME, param, VAL_TYPE(arg)));

        if (STU_SIZE(VAL_STRUCT(arg)) != FLD_WIDE(top))
            fail (Error_Arg_Type(D_FRAME, param, VAL_TYPE(arg)));

        memcpy(dest, VAL_STRUCT_DATA_AT(arg), STU_SIZE(VAL_STRUCT(arg)));

        TERM_BIN_LEN(store, offset + STU_SIZE(VAL_STRUCT(arg)));
        return offset;
    }

    assert(IS_WORD(schema));

    union {
        uint8_t u8;
        int8_t i8;
        uint16_t u16;
        int16_t i16;
        uint32_t u32;
        int32_t i32;
        int64_t i64;

        float f;
        double d;

        intptr_t ipt;
    } buffer;

    char *data;
    REBSIZ size;

    switch (VAL_WORD_SYM(schema)) {
      case SYM_UINT8: {
        if (not arg)
            buffer.u8 = 0;  // return value, make space (but initialize)
        else if (IS_INTEGER(arg))
            buffer.u8 = cast(uint8_t, VAL_INT64(arg));
        else
            fail (Error_Arg_Type(D_FRAME, param, VAL_TYPE(arg)));

        data = cast(char*, &buffer.u8);
        size = sizeof(buffer.u8);
        break; }

      case SYM_INT8: {
        if (not arg)
            buffer.i8 = 0;  // return value, make space (but initialize)
        else if (IS_INTEGER(arg))
            buffer.i8 = cast(int8_t, VAL_INT64(arg));
        else
            fail (Error_Arg_Type(D_FRAME, param, VAL_TYPE(arg)));

        data = cast(char*, &buffer.i8);
        size = sizeof(buffer.i8);
        break; }

      case SYM_UINT16: {
        if (not arg)
            buffer.u16 = 0;  // return value, make space (but initialize)
        else if (IS_INTEGER(arg))
            buffer.u16 = cast(uint16_t, VAL_INT64(arg));
        else
            fail (Error_Arg_Type(D_FRAME, param, VAL_TYPE(arg)));

        data = cast(char*, &buffer.u16);
        size = sizeof(buffer.u16);
        break; }

      case SYM_INT16: {
        if (not arg)
            buffer.i16 = 0;  // return value, make space (but initialize)
        else if (IS_INTEGER(arg))
            buffer.i16 = cast(int16_t, VAL_INT64(arg));
        else
            fail (Error_Arg_Type(D_FRAME, param, VAL_TYPE(arg)));

        data = cast(char*, &buffer.i16);
        size = sizeof(buffer.i16);
        break; }

      case SYM_UINT32: {
        if (not arg)
            buffer.u32 = 0;  // return value, make space (but initialize)
        else if (IS_INTEGER(arg))
            buffer.u32 = cast(int32_t, VAL_INT64(arg));
        else
            fail (Error_Arg_Type(D_FRAME, param, VAL_TYPE(arg)));

        data = cast(char*, &buffer.u32);
        size = sizeof(buffer.u32);
        break; }

      case SYM_INT32: {
        if (not arg)
            buffer.i32 = 0;  // return value, make space (but initialize)
        else if (IS_INTEGER(arg))
            buffer.i32 = cast(int32_t, VAL_INT64(arg));
        else
            fail (Error_Arg_Type(D_FRAME, param, VAL_TYPE(arg)));

        data = cast(char*, &buffer.i32);
        size = sizeof(buffer.i32);
        break; }

      case SYM_UINT64:
      case SYM_INT64: {
        if (not arg)
            buffer.i64 = 0;  // return value, make space (but initialize)
        else if (IS_INTEGER(arg))
            buffer.i64 = VAL_INT64(arg);
        else
            fail (Error_Arg_Type(D_FRAME, param, VAL_TYPE(arg)));

        data = cast(char*, &buffer.i64);
        size = sizeof(buffer.i64);
        break; }

      case SYM_POINTER: {
        //
        // Note: Function pointers and data pointers may not be same size.
        //
        if (not arg) {
            buffer.ipt = 0xDECAFBAD;  // return value, make space (but init)
        }
        else switch (VAL_TYPE(arg)) {
          case REB_NULLED:
            buffer.ipt = 0;
            break;

          case REB_INTEGER:
            buffer.ipt = VAL_INT64(arg);
            break;

        // !!! This is a questionable idea, giving out pointers directly into
        // Rebol series data.  The data may be relocated in memory if any
        // modifications happen during a callback (or in the future, just for
        // GC compaction even if not changed)...so the memory is not "stable".
        //
          case REB_TEXT:  // !!! copies a *pointer*!
            buffer.ipt = cast(intptr_t, VAL_UTF8_AT(nullptr, arg));
            break;

          case REB_BINARY:  // !!! copies a *pointer*!
            buffer.ipt = cast(intptr_t, VAL_BYTES_AT(nullptr, arg));
            break;

          case REB_CUSTOM:  // !!! copies a *pointer* (and assumes vector!)
            buffer.ipt = cast(intptr_t, VAL_VECTOR_HEAD(arg));
            break;

          case REB_ACTION: {
            if (not IS_ACTION_RIN(arg))
                fail (Error_Only_Callback_Ptr_Raw());  // but routines, too

            REBRIN *rin = ACT_DETAILS(VAL_ACTION(arg));
            CFUNC* cfunc = RIN_CFUNC(rin);
            size_t sizeof_cfunc = sizeof(cfunc);  // avoid conditional const
            if (sizeof_cfunc != sizeof(intptr_t))  // not necessarily true
                fail ("intptr_t size not equal to function pointer size");
            memcpy(&buffer.ipt, &cfunc, sizeof(intptr_t));
            break; }

          default:
            fail (Error_Arg_Type(D_FRAME, param, VAL_TYPE(arg)));
        }

        data = cast(char*, &buffer.ipt);
        size = sizeof(buffer.ipt);
        break; }  // end case FFI_TYPE_POINTER

      case SYM_REBVAL: {
        if (not arg)
            buffer.ipt = 0xDECAFBAD;  // return value, make space (but init)
        else
            buffer.ipt = cast(intptr_t, arg);

        data = cast(char*, &buffer.ipt);
        size = sizeof(buffer.ipt);
        break; }

      case SYM_FLOAT: {
        if (not arg)
            buffer.f = 0;  // return value, make space (but initialize)
        else if (IS_DECIMAL(arg))
            buffer.f = cast(float, VAL_DECIMAL(arg));
        else
            fail (Error_Arg_Type(D_FRAME, param, VAL_TYPE(arg)));

        data = cast(char*, &buffer.f);
        size = sizeof(buffer.f);
        break; }

      case SYM_DOUBLE: {
        if (not arg)
            buffer.d = 0;
        else if (IS_DECIMAL(arg))
            buffer.d = VAL_DECIMAL(arg);
        else
            fail (Error_Arg_Type(D_FRAME, param, VAL_TYPE(arg)));

        data = cast(char*, &buffer.d);
        size = sizeof(buffer.d);
        break;}

      case SYM_STRUCT_X:
        //
        // structs should be processed above by the HANDLE! case, not WORD!
        //
        assert(false);
      case SYM_VOID:
        //
        // can't return a meaningful offset for "void"--it's only valid for
        // return types, so caller should check and not try to pass it in.
        //
        assert(false);
      default:
        fail (arg);
    }

    if (store) {
        assert(dest == nullptr);
        dest = Expand_And_Align(&offset, store, size);
    }

    memcpy(dest, data, size);

    if (store)
        TERM_BIN_LEN(store, offset + size);

    return offset;
}


/* convert the return value to rebol
 */
static void ffi_to_rebol(
    RELVAL *out,
    const REBVAL *schema,
    void *ffi_rvalue
){
    if (IS_BLOCK(schema)) {
        REBFLD *top = VAL_ARRAY_KNOWN_MUTABLE(schema);

        assert(FLD_IS_STRUCT(top));
        assert(not FLD_IS_ARRAY(top));  // !!! wasn't supported, should be?

        REBSTU *stu = Alloc_Singular(
            NODE_FLAG_MANAGED | SERIES_FLAG_LINK_NODE_NEEDS_MARK
        );
        LINK_SCHEMA_NODE(stu) = NOD(top);

        REBSER *data = Make_Series_Core(
            FLD_WIDE(top),  // !!! what about FLD_LEN_BYTES_TOTAL ?
            sizeof(REBYTE),
            NODE_FLAG_MANAGED
        );
        memcpy(SER_HEAD(REBYTE, data), ffi_rvalue, FLD_WIDE(top));

        RESET_CUSTOM_CELL(out, EG_Struct_Type, CELL_FLAG_FIRST_IS_NODE);
        INIT_VAL_NODE(out, stu);
        VAL_STRUCT_OFFSET(out) = 0;

        Init_Binary(ARR_SINGLE(stu), data);

        assert(STU_DATA_HEAD(stu) == BIN_HEAD(data));
        return;
    }

    assert(IS_WORD(schema));

    switch (VAL_WORD_SYM(schema)) {
      case SYM_UINT8:
        Init_Integer(out, *cast(uint8_t*, ffi_rvalue));
        break;

      case SYM_INT8:
        Init_Integer(out, *cast(int8_t*, ffi_rvalue));
        break;

      case SYM_UINT16:
        Init_Integer(out, *cast(uint16_t*, ffi_rvalue));
        break;

      case SYM_INT16:
        Init_Integer(out, *cast(int16_t*, ffi_rvalue));
        break;

      case SYM_UINT32:
        Init_Integer(out, *cast(uint32_t*, ffi_rvalue));
        break;

      case SYM_INT32:
        Init_Integer(out, *cast(int32_t*, ffi_rvalue));
        break;

      case SYM_UINT64:
        Init_Integer(out, *cast(uint64_t*, ffi_rvalue));
        break;

      case SYM_INT64:
        Init_Integer(out, *cast(int64_t*, ffi_rvalue));
        break;

      case SYM_POINTER:  // !!! Should 0 come back as a NULL to Rebol?
        Init_Integer(out, cast(uintptr_t, *cast(void**, ffi_rvalue)));
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
        assert(false); // not covered by generic routine.
      default:
        assert(false);
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
    REBRIN *rin = ACT_DETAILS(FRM_PHASE(f));

    if (RIN_IS_CALLBACK(rin) or RIN_LIB(rin) == nullptr) {
        //
        // lib is nullptr when routine is constructed from address directly,
        // so there's nothing to track whether that gets loaded or unloaded
    }
    else {
        if (IS_LIB_CLOSED(RIN_LIB(rin)))
            fail (Error_Bad_Library_Raw());
    }

    REBLEN num_fixed = RIN_NUM_FIXED_ARGS(rin);

    REBLEN num_variable;
    REBDSP dsp_orig = DSP; // variadic args pushed to stack, so save base ptr

    if (not RIN_IS_VARIADIC(rin))
        num_variable = 0;
    else {
        // The function specification should have one extra parameter for
        // the variadic source ("...")
        //
        assert(ACT_NUM_PARAMS(FRM_PHASE(f)) == num_fixed + 1);

        REBVAL *vararg = FRM_ARG(f, num_fixed + 1); // 1-based
        assert(IS_VARARGS(vararg) and FRM_BINDING(f) == UNBOUND);

        // Evaluate the VARARGS! feed of values to the data stack.  This way
        // they will be available to be counted, to know how big to make the
        // FFI argument series.
        //
        do {
            if (Do_Vararg_Op_Maybe_End_Throws(
                f->out,
                VARARG_OP_TAKE,
                vararg
            )){
                return R_THROWN;
            }

            if (IS_END(f->out))
                break;

            Move_Value(DS_PUSH(), f->out);
            SET_END(f->out); // expected by Do_Vararg_Op
        } while (true);

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

    REBLEN num_args = num_fixed + num_variable;

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
    REBBIN *store = Make_Binary(1);

    void *ret_offset;
    if (not IS_BLANK(RIN_RET_SCHEMA(rin))) {
        ret_offset = cast(void*, arg_to_ffi(
            store, // ffi-converted arg appended here
            nullptr, // dest pointer must be nullptr if store is non-nullptr
            nullptr, // arg: none (we're only making space--leave uninitialized)
            RIN_RET_SCHEMA(rin),
            nullptr // param: none (it's a return value/output)
        ));
    }
    else {
        // Shouldn't be used (assigned to nullptr later) but avoid maybe
        // uninitialized warning.
        //
        ret_offset = cast(void*, cast(uintptr_t, 0xDECAFBAD));
    }

    REBSER *arg_offsets;
    if (num_args == 0)
        arg_offsets = nullptr;  // don't waste time with the alloc + free
    else
        arg_offsets = Make_Series(num_args, sizeof(void*));

    // First gather the fixed parameters from the frame.  They are known to
    // be of correct general types (they were checked by Eval_Core for the call)
    // but a STRUCT! might not be compatible with the type of STRUCT! in
    // the parameter specification.  They might also be out of range, e.g.
    // a too-large or negative INTEGER! passed to a uint8.  Could fail() here.
    //
  blockscope {
    REBLEN i = 0;
    for (; i < num_fixed; ++i) {
        uintptr_t offset = arg_to_ffi(
            store,  // ffi-converted arg appended here
            nullptr,  // dest pointer must be nullptr if store is non-null
            FRM_ARG(f, i + 1),  // 1-based
            RIN_ARG_SCHEMA(rin, i),  // 0-based
            ACT_PARAM(FRM_PHASE(f), i + 1)  // 1-based
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
    // These pointers need to be freed by HANDLE! cleanup.
    //
    ffi_cif *cif;  // pre-made if not variadic, built for this call otherwise
    ffi_type **args_fftypes = nullptr;  // ffi_type*[] if num_variable > 0

    if (not RIN_IS_VARIADIC(rin)) {
        cif = RIN_CIF(rin);
    }
    else {
        assert(IS_BLANK(RIN_AT(rin, IDX_ROUTINE_CIF)));

        // CIF creation requires a C array of argument descriptions that is
        // contiguous across both the fixed and variadic parts.  Start by
        // filling in the ffi_type*s for all the fixed args.
        //
        args_fftypes = rebAllocN(ffi_type*, num_fixed + num_variable);

        REBLEN i;
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
                DS_AT(dsp + 1), // will error if this is not a block
                Canon(SYM_ELLIPSIS)
            );

            args_fftypes[i] = SCHEMA_FFTYPE(schema);

            *SER_AT(void*, arg_offsets, i) = cast(void*, arg_to_ffi(
                store,  // data appended to store
                nullptr,  // dest pointer must be null if store is non-null
                DS_AT(dsp),  // arg
                schema,
                param  // used for typecheck, VAL_TYPESET_SYM for error msgs
            ));
        }

        DS_DROP_TO(dsp_orig);  // done w/args (converted to bytes in `store`)

        cif = rebAlloc(ffi_cif);

        ffi_status status = ffi_prep_cif_var(  // _var-iadic prep_cif version
            cif,
            RIN_ABI(rin),
            num_fixed,  // just fixed
            num_args,  // fixed plus variable
            IS_BLANK(RIN_RET_SCHEMA(rin))
                ? &ffi_type_void
                : SCHEMA_FFTYPE(RIN_RET_SCHEMA(rin)),  // return FFI type
            args_fftypes  // arguments FFI types
        );

        if (status != FFI_OK) {
            rebFree(cif);  // would free automatically on fail
            rebFree(args_fftypes);  // would free automatically on fail
            fail ("FFI: Couldn't prep CIF_VAR");
        }
    }

    // Now that all the additions to store have been made, we want to change
    // the offsets of each FFI argument into actual pointers (since the
    // data won't be relocated)
    //
  blockscope {
    if (IS_BLANK(RIN_RET_SCHEMA(rin)))
        ret_offset = nullptr;
    else
        ret_offset = SER_DATA(store) + cast(uintptr_t, ret_offset);

    REBLEN i;
    for (i = 0; i < num_args; ++i) {
        uintptr_t off = cast(uintptr_t, *SER_AT(void*, arg_offsets, i));
        assert(off == 0 or off < BIN_LEN(store));
        *SER_AT(void*, arg_offsets, i) = BIN_AT(store, off);
    }
  }

    // THE ACTUAL FFI CALL
    //
    // Note that the "offsets" are now direct pointers.  Also note that
    // any callbacks which run Rebol code during the course of calling this
    // arbitrary C code are not allowed to propagate failures out of the
    // callback--they'll panic and crash the interpreter, since they don't
    // know what to do otherwise.  See MAKE-CALLBACK/FALLBACK for some
    // mitigation of this problem.
    //
    ffi_call(
        cif,
        RIN_CFUNC(rin),
        ret_offset,  // actually a real pointer now (no longer an offset)
        (num_args == 0)
            ? nullptr
            : SER_HEAD(void*, arg_offsets)  // also real pointers now
    );

    if (IS_BLANK(RIN_RET_SCHEMA(rin)))
        Init_Nulled(f->out);
    else
        ffi_to_rebol(f->out, RIN_RET_SCHEMA(rin), ret_offset);

    if (num_args != 0)
        Free_Unmanaged_Series(arg_offsets);

    Free_Unmanaged_Series(store);

    if (RIN_IS_VARIADIC(rin)) {
        rebFree(cif);
        rebFree(args_fftypes);
    }

    // Note: cannot "throw" a Rebol value across an FFI boundary.

    return f->out;
}


//
// cleanup_ffi_closure: C
//
// The GC-able HANDLE! used by callbacks contains a ffi_closure pointer that
// needs to be freed when the handle references go away (really only one
// reference is likely--in the ACT_BODY of the callback, but still this is
// how the GC gets hooked in Ren-C)
//
void cleanup_ffi_closure(const REBVAL *v) {
    ffi_closure_free(VAL_HANDLE_POINTER(ffi_closure, v));
}

static void cleanup_cif(const REBVAL *v) {
    FREE(ffi_cif, VAL_HANDLE_POINTER(ffi_cif, v));
}

static void cleanup_args_fftypes(const REBVAL *v) {
    FREE_N(ffi_type*, VAL_HANDLE_LEN(v), VAL_HANDLE_POINTER(ffi_type*, v));
}


struct Reb_Callback_Invocation {
    ffi_cif *cif;
    void *ret;
    void **args;
    REBRIN *rin;
};

static REBVAL *callback_dispatcher_core(struct Reb_Callback_Invocation *inv)
{
    // Build an array of code to run which represents the call.  The first
    // item in that array will be the callback function value, and then
    // the arguments will be the remaining values.
    //
    REBARR *code = Make_Array(1 + inv->cif->nargs);
    RELVAL *elem = ARR_HEAD(code);
    Init_Action_Unbound(elem, RIN_CALLBACK_ACTION(inv->rin));
    ++elem;

    REBLEN i;
    for (i = 0; i != inv->cif->nargs; ++i, ++elem)
        ffi_to_rebol(elem, RIN_ARG_SCHEMA(inv->rin, i), inv->args[i]);

    TERM_ARRAY_LEN(code, 1 + inv->cif->nargs);
    Manage_Array(code);  // DO requires managed arrays (guarded while running)

    DECLARE_LOCAL (result);
    if (Do_At_Mutable_Throws(result, code, 0, SPECIFIED))
        fail (Error_No_Catch_For_Throw(result));  // caller will panic()

    if (inv->cif->rtype->type == FFI_TYPE_VOID)
        assert(IS_BLANK(RIN_RET_SCHEMA(inv->rin)));
    else {
        DECLARE_LOCAL (param);
        Init_Param(param, REB_P_NORMAL, Canon(SYM_RETURN), 0);
        arg_to_ffi(
            nullptr,  // store must be null if dest is non-null,
            inv->ret,  // destination pointer
            result,
            RIN_RET_SCHEMA(inv->rin),
            param  // parameter used for symbol in error only
        );
    }

    return nullptr;  // return result not used
}


//
// callback_dispatcher: C
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
void callback_dispatcher(
    ffi_cif *cif,
    void *ret,
    void **args,
    void *user_data
){
    struct Reb_Callback_Invocation inv;
    inv.cif = cif;
    inv.ret = ret;
    inv.args = args;
    inv.rin = cast(REBRIN*, user_data);

    assert(not RIN_IS_VARIADIC(inv.rin));
    assert(cif->nargs == RIN_NUM_FIXED_ARGS(inv.rin));

    REBVAL *error = rebRescue(cast(REBDNG*, callback_dispatcher_core), &inv);
    if (error != nullptr) {
        //
        // If a callback encounters an un-trapped error in mid-run, there's
        // nothing we can do here to "guess" what its C contract return
        // value should be.  And we can't just jump up to the next trap point,
        // because that would cross unknown FFI code (if it were C++, the
        // destructors might not run, etc.)
        //
        // See MAKE-CALLBACK/FALLBACK for the usermode workaround.
        //
        panic (error);
    }
}


//
//  Alloc_Ffi_Action_For_Spec: C
//
// This allocates a REBACT designed for using with the FFI--though it does
// not fill in the actual code to run.  That is done by the caller, which
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
REBACT *Alloc_Ffi_Action_For_Spec(REBVAL *ffi_spec, ffi_abi abi) {
    assert(IS_BLOCK(ffi_spec));

    // Build the paramlist on the data stack.  First slot is reserved for the
    // ACT_ARCHETYPE (see comments on `struct Reb_Action`)
    //
    REBDSP dsp_orig = DSP;
    Init_Unreadable_Void(DS_PUSH());  // GC-safe form of "trash"

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
    const REBLEN capacity_guess = 8;  // !!! Magic number...why 8? (can grow)
    REBARR *args_schemas = Make_Array(capacity_guess);
    Manage_Array(args_schemas);
    PUSH_GC_GUARD(args_schemas);

    DECLARE_LOCAL (ret_schema);
    Init_Blank(ret_schema);  // ret_schema defaults blank (e.g. void C func)
    PUSH_GC_GUARD(ret_schema);

    REBLEN num_fixed = 0;  // number of fixed (non-variadic) arguments
    bool is_variadic = false;  // default to not being variadic

    const RELVAL *item = VAL_ARRAY_AT(ffi_spec);
    for (; NOT_END(item); ++item) {
        if (IS_TEXT(item))
            continue;  // !!! TBD: extract ACT_META info from spec notes

        switch (VAL_TYPE(item)) {
          case REB_WORD: {
            const REBSTR *name = VAL_WORD_SPELLING(item);

            if (SAME_STR(name, Canon(SYM_ELLIPSIS))) {  // variadic
                if (is_variadic)
                    fail ("FFI: Duplicate ... indicating variadic");

                is_variadic = true;

                // !!! Originally, a feature in VARARGS! was that they would
                // "chain" by default, if VARARGS! was not explicitly added.
                // This feature was removed, but may be re-added:
                //
                // https://github.com/metaeducation/ren-c/issues/801
                //
                // For that reason, varargs was not in the list by default.
                //
                Init_Param(
                    DS_PUSH(),
                    REB_P_NORMAL,
                    Canon(SYM_VARARGS),
                    TS_VALUE & ~FLAGIT_KIND(REB_VARARGS)
                );
                TYPE_SET(DS_TOP, REB_TS_VARIADIC);
            }
            else {  // ordinary argument
                if (is_variadic)
                    fail ("FFI: Variadic must be final parameter");

                ++item;

                DECLARE_LOCAL (block);
                Derelativize(block, item, VAL_SPECIFIER(ffi_spec));

                Schema_From_Block_May_Fail(
                    Alloc_Tail_Array(args_schemas),  // schema (out)
                    DS_PUSH(),  // param (out)
                    block,  // block (in)
                    name
                );

                ++num_fixed;
            }
            break; }

          case REB_SET_WORD:
            switch (VAL_WORD_SYM(item)) {
              case SYM_RETURN:{
                if (not IS_BLANK(ret_schema))
                    fail ("FFI: Return already specified");

                ++item;

                DECLARE_LOCAL (block);
                Derelativize(block, item, VAL_SPECIFIER(ffi_spec));

                Schema_From_Block_May_Fail(
                    ret_schema,
                    nullptr,  // dummy (return/output has no arg to typecheck)
                    block,
                    nullptr  // no symbol name
                );
                break; }

              default:
                fail (SPECIFIC(item));
            }
            break;

          default:
            fail (SPECIFIC(item));
        }
    }

    REBARR *paramlist = Pop_Stack_Values_Core(
        dsp_orig,
        SERIES_MASK_PARAMLIST | NODE_FLAG_MANAGED
    );

    // Now fill in the canon value of the paramlist so it is an actual REBACT
    //
    REBVAL *rootparam = RESET_CELL(
        ARR_HEAD(paramlist),
        REB_ACTION,
        CELL_MASK_ACTION
    );
    Sync_Paramlist_Archetype(paramlist);
    INIT_BINDING(rootparam, UNBOUND);

    MISC_META_NODE(paramlist) = nullptr;

    REBACT *action = Make_Action(
        paramlist,
        &Routine_Dispatcher,
        nullptr,  // no underlying action (use paramlist)
        nullptr,  // no specialization exemplar (or inherited exemplar)
        IDX_ROUTINE_MAX  // details array len
    );

    REBRIN *r = ACT_DETAILS(action);

    Init_Integer(RIN_AT(r, IDX_ROUTINE_ABI), abi);

    // Caller must update these in the returned function.
    //
    TRASH_CELL_IF_DEBUG(RIN_AT(r, IDX_ROUTINE_CFUNC));
    TRASH_CELL_IF_DEBUG(RIN_AT(r, IDX_ROUTINE_CLOSURE));
    TRASH_CELL_IF_DEBUG(RIN_AT(r, IDX_ROUTINE_ORIGIN));  // LIBRARY!/ACTION!

    Move_Value(RIN_AT(r, IDX_ROUTINE_RET_SCHEMA), ret_schema);
    DROP_GC_GUARD(ret_schema);

    Init_Logic(RIN_AT(r, IDX_ROUTINE_IS_VARIADIC), is_variadic);

    ASSERT_ARRAY(args_schemas);
    Init_Block(RIN_AT(r, IDX_ROUTINE_ARG_SCHEMAS), args_schemas);
    DROP_GC_GUARD(args_schemas);

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
            args_fftypes = nullptr;
        else
            args_fftypes = ALLOC_N(ffi_type*, num_fixed);

        REBLEN i;
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
                args_fftypes  // nullptr if 0 fixed args
            )
        ){
            fail ("FFI: Couldn't prep CIF");
        }

        Init_Handle_Cdata_Managed(
            RIN_AT(r, IDX_ROUTINE_CIF),
            cif,
            sizeof(&cif),
            &cleanup_cif
        );

        if (args_fftypes == nullptr)
            Init_Blank(RIN_AT(r, IDX_ROUTINE_ARG_FFTYPES));
        else
            Init_Handle_Cdata_Managed(
                RIN_AT(r, IDX_ROUTINE_ARG_FFTYPES),
                args_fftypes,
                num_fixed,
                &cleanup_args_fftypes
            );  // lifetime must match cif lifetime
    }

    TERM_ARRAY_LEN(r, IDX_ROUTINE_MAX);

    return action;
}
