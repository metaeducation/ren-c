//
//  File: %t-struct.c
//  Summary: "C struct object datatype"
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


// The managed HANDLE! for a ffi_type will have a reference in structs that
// use it.  Basic non-struct FFI_TYPE_XXX use the stock ffi_type_xxx pointers
// that do not have to be freed, so they use simple HANDLE! which do not
// register this cleanup hook.
//
static void cleanup_ffi_type(const REBVAL *v) {
    ffi_type *fftype = VAL_HANDLE_POINTER(ffi_type, v);
    if (fftype->type == FFI_TYPE_STRUCT)
        free(fftype->elements);
    free(fftype);
}


static void fail_if_non_accessible(REBSTU *stu)
{
    if (STU_INACCESSIBLE(stu)) {
        DECLARE_LOCAL (i);
        Init_Integer(i, cast(intptr_t, STU_DATA_HEAD(stu)));
        fail (Error_Bad_Memory_Raw(i, nullptr));  // !!! Can't pass stu here?
    }
}

static void get_scalar(
    RELVAL *out,
    REBSTU *stu,
    REBFLD *field,
    REBLEN n  // element index, starting from 0
){
    assert(n == 0 or FLD_IS_ARRAY(field));

    REBLEN offset =
        STU_OFFSET(stu) + FLD_OFFSET(field) + (n * FLD_WIDE(field));

    if (FLD_IS_STRUCT(field)) {
        //
        // In order for the schema to participate in GC it must be a series.
        // Currently this series is created with a single value of the root
        // schema in the case of a struct expansion.  This wouldn't be
        // necessary if each field that was a structure offered a REBSER
        // already... !!! ?? !!! ... it will be necessary if the schemas
        // are to uniquely carry an ffi_type freed when they are GC'd
        //
        REBSTU *sub_stu = Alloc_Singular(
            NODE_FLAG_MANAGED | SERIES_FLAG_LINK_NODE_NEEDS_MARK
        );
        LINK(sub_stu).custom.node = NOD(field);

        // The parent data may be a singular array for a HANDLE! or a BINARY!
        // series, depending on whether the data is owned by Rebol or not.
        // That series pointer is being referenced again here.
        //
        Move_Value(ARR_SINGLE(sub_stu), STU_DATA(stu));
        STU_OFFSET(sub_stu) = offset;
        assert(STU_SIZE(sub_stu) == FLD_WIDE(field));
        Init_Struct(out, sub_stu);
        return;
    }

    if (STU_INACCESSIBLE(stu)) {
        //
        // !!! Not giving an error seems like a bad idea, if the data is
        // truly inaccessible.
        //
        Init_Nulled(out);
        return;
    }

    REBYTE *p = offset + STU_DATA_HEAD(stu);

    switch (FLD_TYPE_SYM(field)) {
      case SYM_UINT8:
        Init_Integer(out, *cast(uint8_t*, p));
        break;

      case SYM_INT8:
        Init_Integer(out, *cast(int8_t*, p));
        break;

      case SYM_UINT16:
        Init_Integer(out, *cast(uint16_t*, p));
        break;

      case SYM_INT16:
        Init_Integer(out, *cast(int8_t*, p));
        break;

      case SYM_UINT32:
        Init_Integer(out, *cast(uint32_t*, p));
        break;

      case SYM_INT32:
        Init_Integer(out, *cast(int32_t*, p));
        break;

      case SYM_UINT64:
        Init_Integer(out, *cast(uint64_t*, p));
        break;

      case SYM_INT64:
        Init_Integer(out, *cast(int64_t*, p));
        break;

      case SYM_FLOAT:
        Init_Decimal(out, *cast(float*, p));
        break;

      case SYM_DOUBLE:
        Init_Decimal(out, *cast(double*, p));
        break;

      case SYM_POINTER:  // !!! Should 0 come back as a NULL to Rebol?
        Init_Integer(out, cast(intptr_t, *cast(void**, p)));
        break;

      case SYM_REBVAL:
        Move_Value(out, cast(const REBVAL*, p));
        break;

      default:
        assert(false);
        fail ("Unknown FFI type indicator");
    }
}


//
//  Get_Struct_Var: C
//
static bool Get_Struct_Var(REBVAL *out, REBSTU *stu, const RELVAL *word)
{
    REBARR *fieldlist = STU_FIELDLIST(stu);

    RELVAL *item = ARR_HEAD(fieldlist);
    for (; NOT_END(item); ++item) {
        REBFLD *field = VAL_ARRAY_KNOWN_MUTABLE(item);
        if (STR_CANON(FLD_NAME(field)) != VAL_WORD_CANON(word))
            continue;

        if (FLD_IS_ARRAY(field)) {
            //
            // Structs contain packed data for the field type in an array.
            // This data cannot expand or contract, and is not in a
            // Rebol-compatible format.  A Rebol Array is made by
            // extracting the information.
            //
            // !!! Perhaps a fixed-size VECTOR! could have its data
            // pointer into these arrays?
            //
            REBLEN dimension = FLD_DIMENSION(field);
            REBARR *arr = Make_Array(dimension);
            REBLEN n;
            for (n = 0; n < dimension; ++n)
                get_scalar(ARR_AT(arr, n), stu, field, n);
            TERM_ARRAY_LEN(arr, dimension);
            Init_Block(out, arr);
        }
        else
            get_scalar(out, stu, field, 0);

        return true;
    }

    return false; // word not found in struct's field symbols
}


//
//  Struct_To_Array: C
//
// Used by MOLD to create a block.
//
// Cannot fail(), because fail() could call MOLD on a struct!, which will end
// up infinitive recursive calls.
//
REBARR *Struct_To_Array(REBSTU *stu)
{
    REBARR *fieldlist = STU_FIELDLIST(stu);
    RELVAL *item = ARR_HEAD(fieldlist);

    REBDSP dsp_orig = DSP;

    // fail_if_non_accessible(STU_TO_VAL(stu));

    for(; NOT_END(item); ++item) {
        REBFLD *field = VAL_ARRAY_KNOWN_MUTABLE(item);

        Init_Set_Word(DS_PUSH(), FLD_NAME(field)); // required name

        REBARR *typespec = Make_Array(2); // required type

        if (FLD_IS_STRUCT(field)) {
            Init_Word(Alloc_Tail_Array(typespec), Canon(SYM_STRUCT_X));

            DECLARE_LOCAL (nested);
            get_scalar(nested, stu, field, 0);

            PUSH_GC_GUARD(nested); // is this guard still necessary?
            Init_Block(
                Alloc_Tail_Array(typespec),
                Struct_To_Array(VAL_STRUCT(nested))
            );
            DROP_GC_GUARD(nested);
        }
        else {
            // Elemental type (from a fixed list of known C types)
            //
            Init_Word(Alloc_Tail_Array(typespec), Canon(FLD_TYPE_SYM(field)));
        }

        // "optional dimension and initialization."
        //
        // !!! Comment said the initialization was optional, but it seems
        // that the initialization always happens (?)
        //
        if (FLD_IS_ARRAY(field)) {
            //
            // Dimension becomes INTEGER! in a BLOCK! (to look like a C array)
            //
            REBLEN dimension = FLD_DIMENSION(field);
            REBARR *one_int = Alloc_Singular(NODE_FLAG_MANAGED);
            Init_Integer(ARR_SINGLE(one_int), dimension);
            Init_Block(Alloc_Tail_Array(typespec), one_int);

            // Initialization seems to be just another block after that (?)
            //
            REBARR *init = Make_Array(dimension);
            REBLEN n;
            for (n = 0; n < dimension; n ++)
                get_scalar(ARR_AT(init, n), stu, field, n);
            TERM_ARRAY_LEN(init, dimension);
            Init_Block(Alloc_Tail_Array(typespec), init);
        }
        else
            get_scalar(Alloc_Tail_Array(typespec), stu, field, 0);

        Init_Block(DS_PUSH(), typespec); // required type
    }

    return Pop_Stack_Values(dsp_orig);
}


void MF_Struct(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    UNUSED(form);

    Pre_Mold(mo, v);

    REBARR *array = Struct_To_Array(VAL_STRUCT(v));
    Mold_Array_At(mo, array, 0, "[]");
    Free_Unmanaged_Array(array);

    End_Mold(mo);
}


static bool same_fields(REBARR *tgt_fieldlist, REBARR *src_fieldlist)
{
    if (ARR_LEN(tgt_fieldlist) != ARR_LEN(src_fieldlist))
        return false;

    RELVAL *tgt_item = ARR_HEAD(tgt_fieldlist);
    RELVAL *src_item = ARR_HEAD(src_fieldlist);

    for (; NOT_END(src_item); ++src_item, ++tgt_item) {
        REBFLD *src_field = VAL_ARRAY_KNOWN_MUTABLE(src_item);
        REBFLD *tgt_field = VAL_ARRAY_KNOWN_MUTABLE(tgt_item);

        if (FLD_IS_STRUCT(tgt_field))
            if (not same_fields(
                FLD_FIELDLIST(tgt_field),
                FLD_FIELDLIST(src_field)
            )){
                return false;
            }

        if (not SAME_SYM_NONZERO(
            FLD_TYPE_SYM(tgt_field),
            FLD_TYPE_SYM(src_field)
        )){
            return false;
        }

        if (FLD_IS_ARRAY(tgt_field)) {
            if (not FLD_IS_ARRAY(src_field))
                return false;

            if (FLD_DIMENSION(tgt_field) != FLD_DIMENSION(src_field))
                return false;
        }

        if (FLD_OFFSET(tgt_field) != FLD_OFFSET(src_field))
            return false;

        assert(FLD_WIDE(tgt_field) == FLD_WIDE(src_field));
    }

    assert(IS_END(tgt_item));

    return true;
}


static bool assign_scalar_core(
    REBYTE *data_head,
    REBLEN offset,
    REBFLD *field,
    REBLEN n,
    const REBVAL *val
){
    assert(n == 0 or FLD_IS_ARRAY(field));

    void *data = data_head +
        offset + FLD_OFFSET(field) + (n * FLD_WIDE(field));

    if (FLD_IS_STRUCT(field)) {
        if (not IS_STRUCT(val))
            fail (Error_Invalid_Type(VAL_TYPE(val)));

        if (FLD_WIDE(field) != VAL_STRUCT_SIZE(val))
            fail (val);

        if (not same_fields(FLD_FIELDLIST(field), VAL_STRUCT_FIELDLIST(val)))
            fail (val);

        memcpy(data, VAL_STRUCT_DATA_AT(val), FLD_WIDE(field));

        return true;
    }

    // All other types take numbers

    int64_t i;
    double d;

    switch (VAL_TYPE(val)) {
      case REB_DECIMAL:
        d = VAL_DECIMAL(val);
        i = cast(int64_t, d);
        break;

      case REB_INTEGER:
        i = VAL_INT64(val);
        d = cast(double, i);
        break;

      default:
        // !!! REBVAL in a STRUCT! is likely not a good feature (see the
        // ALLOC-VALUE-POINTER routine for a better solution).  However, the
        // same code is used to process FFI function arguments and struct
        // definitions, and the feature may be useful for function args.

        if (FLD_TYPE_SYM(field) != SYM_REBVAL)
            fail (Error_Invalid_Type(VAL_TYPE(val)));

        // Avoid uninitialized variable warnings (should not be used)
        //
        i = 1020;
        d = 304;
    }

    switch (FLD_TYPE_SYM(field)) {
      case SYM_INT8:
        if (i > 0x7f or i < -128)
            fail (Error_Overflow_Raw());
        *cast(int8_t*, data) = cast(int8_t, i);
        break;

      case SYM_UINT8:
        if (i > 0xff or i < 0)
            fail (Error_Overflow_Raw());
        *cast(uint8_t*, data) = cast(uint8_t, i);
        break;

      case SYM_INT16:
        if (i > 0x7fff or i < -0x8000)
            fail (Error_Overflow_Raw());
        *cast(int16_t*, data) = cast(int16_t, i);
        break;

      case SYM_UINT16:
        if (i > 0xffff or i < 0)
            fail (Error_Overflow_Raw());
        *cast(uint16_t*, data) = cast(uint16_t, i);
        break;

      case SYM_INT32:
        if (i > INT32_MAX or i < INT32_MIN)
            fail (Error_Overflow_Raw());
        *cast(int32_t*, data) = cast(int32_t, i);
        break;

      case SYM_UINT32:
        if (i > UINT32_MAX or i < 0)
            fail (Error_Overflow_Raw());
        *cast(uint32_t*, data) = cast(uint32_t, i);
        break;

      case SYM_INT64:
        *cast(int64_t*, data) = i;
        break;

      case SYM_UINT64:
        if (i < 0)
            fail (Error_Overflow_Raw());
        *cast(uint64_t*, data) = cast(uint64_t, i);
        break;

      case SYM_FLOAT:
        *cast(float*, data) = cast(float, d);
        break;

      case SYM_DOUBLE:
        *cast(double*, data) = d;
        break;

      case SYM_POINTER: {
        size_t sizeof_void_ptr = sizeof(void*); // avoid constant conditional
        if (sizeof_void_ptr == 4 and i > UINT32_MAX)
            fail (Error_Overflow_Raw());
        *cast(void**, data) = cast(void*, cast(intptr_t, i));
        break; }

      case SYM_REBVAL:
        //
        // !!! This is a dangerous thing to be doing in generic structs, but
        // for the main purpose of REBVAL (tunneling) it should be okay so
        // long as the REBVAL* that is passed in is actually a pointer into
        // a frame's args.
        //
        *cast(const REBVAL**, data) = val;
        break;

      default:
        assert(not "unknown FLD_TYPE_SYM()");
        return false;
    }

    return true;
}


inline static bool assign_scalar(
    REBSTU *stu,
    REBFLD *field,
    REBLEN n,
    const REBVAL *val
){
    return assign_scalar_core(
        STU_DATA_HEAD(stu), STU_OFFSET(stu), field, n, val
    );
}


//
//  Set_Struct_Var: C
//
static bool Set_Struct_Var(
    REBSTU *stu,
    const RELVAL *word,
    const REBVAL *elem,
    const REBVAL *val
){
    REBARR *fieldlist = STU_FIELDLIST(stu);
    RELVAL *item = ARR_HEAD(fieldlist);

    for (; NOT_END(item); ++item) {
        REBFLD *field = VAL_ARRAY_KNOWN_MUTABLE(item);

        if (VAL_WORD_CANON(word) != STR_CANON(FLD_NAME(field)))
            continue;

        if (FLD_IS_ARRAY(field)) {
            if (elem == nullptr) { // set the whole array
                if (not IS_BLOCK(val))
                    return false;

                REBLEN dimension = FLD_DIMENSION(field);
                if (dimension != VAL_LEN_AT(val))
                    return false;

                REBLEN n = 0;
                for(n = 0; n < dimension; ++n) {
                    if (not assign_scalar(
                        stu, field, n, SPECIFIC(VAL_ARRAY_AT_HEAD(val, n))
                    )) {
                        return false;
                    }
                }
            }
            else { // set only one element
                if (not IS_INTEGER(elem) or VAL_INT32(elem) != 1)
                    return false;

                return assign_scalar(stu, field, 0, val);
            }
            return true;
        }

        return assign_scalar(stu, field, 0, val);
    }

    return false;
}


/* parse struct attribute */
static void parse_attr(
    const RELVAL *blk,
    REBINT *raw_size,
    uintptr_t *raw_addr
){
    const REBVAL *attr = SPECIFIC(VAL_ARRAY_AT(blk));

    *raw_size = -1;
    *raw_addr = 0;

    while (NOT_END(attr)) {
        if (not IS_SET_WORD(attr))
            fail (attr);

        switch (VAL_WORD_SYM(attr)) {
          case SYM_RAW_SIZE:
            ++ attr;
            if (IS_END(attr) or not IS_INTEGER(attr))
                fail (attr);
            if (*raw_size > 0)
                fail ("FFI: duplicate raw size");
            *raw_size = VAL_INT64(attr);
            if (*raw_size <= 0)
                fail ("FFI: raw size cannot be zero");
            break;

          case SYM_RAW_MEMORY:
            ++ attr;
            if (IS_END(attr) or not IS_INTEGER(attr))
                fail (attr);
            if (*raw_addr != 0)
                fail ("FFI: duplicate raw memory");
            *raw_addr = cast(REBU64, VAL_INT64(attr));
            if (*raw_addr == 0)
                fail ("FFI: void pointer illegal for raw memory");
            break;

          case SYM_EXTERN: {
            ++ attr;

            if (*raw_addr != 0)
                fail ("FFI: raw memory is exclusive with extern");

            if (IS_END(attr) or not IS_BLOCK(attr) or VAL_LEN_AT(attr) != 2)
                fail (attr);

            const RELVAL *lib = VAL_ARRAY_AT_HEAD(attr, 0);
            if (not IS_LIBRARY(lib))
                fail (attr);
            if (IS_LIB_CLOSED(VAL_LIBRARY(lib)))
                fail (Error_Bad_Library_Raw());

            const RELVAL *sym = VAL_ARRAY_AT_HEAD(attr, 1);
            if (not ANY_STRING(sym))
                fail (rebUnrelativize(sym));

            CFUNC *addr = Find_Function(
                VAL_LIBRARY_FD(lib),
                cs_cast(VAL_UTF8_AT(nullptr, sym))
            );
            if (addr == nullptr)
                fail (Error_Symbol_Not_Found_Raw(rebUnrelativize(sym)));

            *raw_addr = cast(uintptr_t, addr);
            break; }

        // !!! This alignment code was commented out for some reason.
        /*
        case SYM_ALIGNMENT:
            ++ attr;
            if (not IS_INTEGER(attr))
                fail (attr);

            alignment = VAL_INT64(attr);
            break;
        */

        default:
            fail (attr);
        }

        ++ attr;
    }
}


// The managed handle logic always assumes a cleanup function, so it doesn't
// have to test for nullptr.
//
static void cleanup_noop(const REBVAL *v) {
    assert(IS_HANDLE(v));
    UNUSED(v);
}


//
// set storage memory to external addr: raw_addr
//
// "External Storage" is the idea that a STRUCT! which is modeling a C
// struct doesn't use a BINARY! series as the backing store, rather a pointer
// that is external to the system.  When Atronix added the FFI initially,
// this was done by creating a separate type of REBSER that could use an
// external pointer.  This uses a managed HANDLE! for the same purpose, as
// a less invasive way of doing the same thing.
//
static void make_ext_storage(
    REBSTU *stu,
    REBLEN len,
    REBINT raw_size,
    uintptr_t raw_addr
) {
    if (raw_size >= 0 and raw_size != cast(REBINT, len)) {
        DECLARE_LOCAL (i);
        Init_Integer(i, raw_size);
        fail (Error_Invalid_Data_Raw(i));
    }

    Init_Handle_Cdata_Managed(
        ARR_SINGLE(stu),
        cast(REBYTE*, raw_addr),
        len,
        &cleanup_noop
    );
}


//
//  Total_Struct_Dimensionality: C
//
// This recursively counts the total number of data elements inside of a
// struct.  This includes for instance every array element inside a
// nested struct's field, along with its fields.
//
// !!! Is this really how char[1000] would be handled in the FFI?  By
// creating 1000 ffi_types?  :-/
//
static REBLEN Total_Struct_Dimensionality(REBARR *fields)
{
    REBLEN n_fields = 0;

    RELVAL *item = ARR_HEAD(fields);
    for (; NOT_END(item); ++item) {
        REBFLD *field = VAL_ARRAY_KNOWN_MUTABLE(item);

        if (FLD_IS_STRUCT(field))
            n_fields += Total_Struct_Dimensionality(FLD_FIELDLIST(field));
        else
            n_fields += FLD_IS_ARRAY(field) ? FLD_DIMENSION(field) : 1;
    }
    return n_fields;
}


//
//  Prepare_Field_For_FFI: C
//
// The main reason structs exist is so that they can be used with the FFI,
// and the FFI requires you to set up a "ffi_type" C struct describing
// each datatype. This is a helper function that sets up proper ffi_type.
// There are stock types for the primitives, but each structure needs its
// own.
//
static void Prepare_Field_For_FFI(REBFLD *schema)
{
    ASSERT_UNREADABLE_IF_DEBUG(FLD_AT(schema, IDX_FIELD_FFTYPE));

    ffi_type *fftype;

    if (not FLD_IS_STRUCT(schema)) {
        fftype = Get_FFType_For_Sym(FLD_TYPE_SYM(schema));
        assert(fftype != nullptr);

        // The FFType pointers returned by Get_FFType_For_Sym should not be
        // freed, so a "simple" handle is used that just holds the pointer.
        //
        Init_Handle_Cdata(
            FLD_AT(schema, IDX_FIELD_FFTYPE),
            fftype,
            sizeof(&fftype)
        );
        return;
    }

    // For struct fields--on the other hand--it's necessary to do a custom
    // allocation for a new type registered with the FFI.
    //
    fftype = cast(ffi_type*, malloc(sizeof(ffi_type)));
    fftype->type = FFI_TYPE_STRUCT;

    // "This is set by libffi; you should initialize it to zero."
    // http://www.atmark-techno.com/~yashi/libffi.html#Structures
    //
    fftype->size = 0;
    fftype->alignment = 0;

    REBARR *fieldlist = FLD_FIELDLIST(schema);

    REBLEN dimensionality = Total_Struct_Dimensionality(fieldlist);
    fftype->elements = cast(ffi_type**,
        malloc(sizeof(ffi_type*) * (dimensionality + 1)) // nullptr term
    );

    RELVAL *item = ARR_HEAD(fieldlist);

    REBLEN j = 0;
    for (; NOT_END(item); ++item) {
        REBFLD *field = VAL_ARRAY_KNOWN_MUTABLE(item);
        REBLEN dimension = FLD_IS_ARRAY(field) ? FLD_DIMENSION(field) : 1;

        REBLEN n = 0;
        for (n = 0; n < dimension; ++n)
            fftype->elements[j++] = FLD_FFTYPE(field);
    }

    fftype->elements[j] = nullptr;

    Init_Handle_Cdata_Managed(
        FLD_AT(schema, IDX_FIELD_FFTYPE),
        fftype,
        dimensionality + 1,
        &cleanup_ffi_type
    );
}


//
// This takes a spec like `[int32 [2]]` and sets the output field's properties
// by recognizing a finite set of FFI type keywords defined in %words.r.
//
// This also allows for embedded structure types.  If the type is not being
// included by reference, but rather with a sub-definition inline, then it
// will actually be creating a new `inner` STRUCT! value.  Since this value
// is managed and not referred to elsewhere, there can't be evaluations.
//
static void Parse_Field_Type_May_Fail(
    REBFLD *field,
    REBVAL *spec,
    REBVAL *inner // will be set only if STRUCT!
){
    TRASH_CELL_IF_DEBUG(inner);

    const RELVAL *val = VAL_ARRAY_AT(spec);

    if (IS_END(val))
        fail ("Empty field type in FFI");

    if (IS_WORD(val)) {
        REBSYM sym = VAL_WORD_SYM(val);

        // Initialize the type symbol with the unbound word by default (will
        // be overwritten in the struct cases).
        //
        Init_Word(FLD_AT(field, IDX_FIELD_TYPE), Canon(sym));

        switch (sym) {
          case SYM_UINT8:
            Init_Integer(FLD_AT(field, IDX_FIELD_WIDE), 1);
            Prepare_Field_For_FFI(field);
            break;

          case SYM_INT8:
            Init_Integer(FLD_AT(field, IDX_FIELD_WIDE), 1);
            Prepare_Field_For_FFI(field);
            break;

          case SYM_UINT16:
            Init_Integer(FLD_AT(field, IDX_FIELD_WIDE), 2);
            Prepare_Field_For_FFI(field);
            break;

          case SYM_INT16:
            Init_Integer(FLD_AT(field, IDX_FIELD_WIDE), 2);
            Prepare_Field_For_FFI(field);
            break;

          case SYM_UINT32:
            Init_Integer(FLD_AT(field, IDX_FIELD_WIDE), 4);
            Prepare_Field_For_FFI(field);
            break;

          case SYM_INT32:
            Init_Integer(FLD_AT(field, IDX_FIELD_WIDE), 4);
            Prepare_Field_For_FFI(field);
            break;

          case SYM_UINT64:
            Init_Integer(FLD_AT(field, IDX_FIELD_WIDE), 8);
            Prepare_Field_For_FFI(field);
            break;

          case SYM_INT64:
            Init_Integer(FLD_AT(field, IDX_FIELD_WIDE), 8);
            Prepare_Field_For_FFI(field);
            break;

          case SYM_FLOAT:
            Init_Integer(FLD_AT(field, IDX_FIELD_WIDE), 4);
            Prepare_Field_For_FFI(field);
            break;

          case SYM_DOUBLE:
            Init_Integer(FLD_AT(field, IDX_FIELD_WIDE), 8);
            Prepare_Field_For_FFI(field);
            break;

          case SYM_POINTER:
            Init_Integer(FLD_AT(field, IDX_FIELD_WIDE), sizeof(void*));
            Prepare_Field_For_FFI(field);
            break;

          case SYM_STRUCT_X: {
            ++ val;
            if (not IS_BLOCK(val))
                fail (Error_Unexpected_Type(REB_BLOCK, VAL_TYPE(val)));

            DECLARE_LOCAL (specific);
            Derelativize(specific, val, VAL_SPECIFIER(spec));
            MAKE_Struct(inner, REB_CUSTOM, nullptr, specific);  // may fail()

            Init_Integer(
                FLD_AT(field, IDX_FIELD_WIDE),
                VAL_STRUCT_DATA_LEN(inner)
            );
            Init_Block(
                FLD_AT(field, IDX_FIELD_TYPE),
                VAL_STRUCT_FIELDLIST(inner)
            );

            // Borrow the same ffi_type* that was built for the inner struct
            // (What about just storing the STRUCT! value itself in the type
            // field, instead of the array of fields?)
            //
            Move_Value(
                FLD_AT(field, IDX_FIELD_FFTYPE),
                FLD_AT(VAL_STRUCT_SCHEMA(inner), IDX_FIELD_FFTYPE)
            );
            break; }

          case SYM_REBVAL: {
            //
            // While most data types have some kind of proxying of when you
            // pass a Rebol value in (such as turning an INTEGER! into bits
            // for a C `int`) if the argument is marked as being a REBVAL
            // then the VAL_TYPE is ignored, and it acts like a pointer to
            // the actual argument in the frame...whatever that may be.
            //
            // !!! The initial FFI implementation from Atronix would actually
            // store sizeof(REBVAL) in the struct, not sizeof(REBVAL*).  The
            // struct's binary data was then hooked into the garbage collector
            // to make sure that cell was marked.  Because the intended use
            // of the feature is "tunneling" a value from a routine's frame
            // to a callback's frame, the lifetime of the REBVAL* should last
            // for the entirety of the routine it was passed to.
            //
            Init_Integer(FLD_AT(field, IDX_FIELD_WIDE), sizeof(REBVAL*));
            Prepare_Field_For_FFI(field);
            break; }

          default:
            fail (Error_Invalid_Type(VAL_TYPE(val)));
        }
    }
    else if (IS_STRUCT(val)) {
        //
        // [b: [struct-a] val-a]
        //
        Init_Integer(
            FLD_AT(field, IDX_FIELD_WIDE),
            VAL_STRUCT_DATA_LEN(val)
        );
        Init_Block(
            FLD_AT(field, IDX_FIELD_TYPE),
            VAL_STRUCT_FIELDLIST(val)
        );

        // Borrow the same ffi_type* that the struct uses, see above note
        // regarding alternative ideas.
        //
        Move_Value(
            FLD_AT(field, IDX_FIELD_FFTYPE),
            FLD_AT(VAL_STRUCT_SCHEMA(val), IDX_FIELD_FFTYPE)
        );
        Derelativize(inner, val, VAL_SPECIFIER(spec));
    }
    else
        fail (Error_Invalid_Type(VAL_TYPE(val)));

    ++ val;

    // Find out the array dimension (if there is one)
    //
    if (IS_END(val)) {
        Init_Blank(FLD_AT(field, IDX_FIELD_DIMENSION)); // scalar
    }
    else if (IS_BLOCK(val)) {
        //
        // make struct! [a: [int32 [2]] [0 0]]
        //
        DECLARE_LOCAL (ret);
        REBSPC *derived = Derive_Specifier(VAL_SPECIFIER(spec), val);
        if (Do_Any_Array_At_Throws(ret, val, derived))
            fail (Error_No_Catch_For_Throw(ret));

        if (not IS_INTEGER(ret))
            fail (Error_Unexpected_Type(REB_INTEGER, VAL_TYPE(val)));

        Init_Integer(FLD_AT(field, IDX_FIELD_DIMENSION), VAL_INT64(ret));
        ++ val;
    }
    else
        fail (Error_Invalid_Type(VAL_TYPE(val)));
}


//
//  Init_Struct_Fields: C
//
// a: make struct! [uint 8 i: 1]
// b: make a [i: 10]
//
void Init_Struct_Fields(REBVAL *ret, REBVAL *spec)
{
    const RELVAL *spec_item = VAL_ARRAY_AT(spec);

    while (NOT_END(spec_item)) {
        const RELVAL *word;
        if (IS_BLOCK(spec_item)) { // options: raw-memory, etc
            REBINT raw_size = -1;
            uintptr_t raw_addr = 0;

            // make sure no other field initialization
            if (VAL_LEN_HEAD(spec) != 1)
                fail (spec);

            parse_attr(spec_item, &raw_size, &raw_addr);
            make_ext_storage(
                VAL_STRUCT(ret),
                VAL_STRUCT_SIZE(ret),
                raw_size,
                raw_addr
            );
            break;
        }
        else {
            word = spec_item;
            if (not IS_SET_WORD(word))
                fail (rebUnrelativize(word));
        }

        const RELVAL *fld_val = spec_item + 1;
        if (IS_END(fld_val))
            fail (Error_Need_Non_End_Raw(rebUnrelativize(fld_val)));

        REBARR *fieldlist = VAL_STRUCT_FIELDLIST(ret);
        RELVAL *item = ARR_HEAD(fieldlist);

        for (; NOT_END(item); ++item) {
            REBFLD *field = VAL_ARRAY_KNOWN_MUTABLE(item);

            if (STR_CANON(FLD_NAME(field)) != VAL_WORD_CANON(word))
                continue;

            if (FLD_IS_ARRAY(field)) {
                if (IS_BLOCK(fld_val)) {
                    REBLEN dimension = FLD_DIMENSION(field);

                    if (VAL_LEN_AT(fld_val) != dimension)
                        fail (rebUnrelativize(fld_val));

                    REBLEN n = 0;
                    for (n = 0; n < dimension; ++n) {
                        if (not assign_scalar(
                            VAL_STRUCT(ret),
                            field,
                            n,
                            SPECIFIC(VAL_ARRAY_AT_HEAD(fld_val, n))
                        )){
                            fail (rebUnrelativize(fld_val));
                        }
                    }
                }
                else if (IS_INTEGER(fld_val)) { // interpret as a data pointer
                    void *ptr = cast(void *,
                        cast(intptr_t, VAL_INT64(fld_val))
                    );

                    // assuming valid pointer to enough space
                    memcpy(
                        VAL_STRUCT_DATA_HEAD(ret) + FLD_OFFSET(field),
                        ptr,
                        FLD_LEN_BYTES_TOTAL(field)
                    );
                }
                else
                    fail (rebUnrelativize(fld_val));
            }
            else {
                if (not assign_scalar(
                    VAL_STRUCT(ret),
                    field,
                    0,
                    SPECIFIC(fld_val)
                )){
                    fail (rebUnrelativize(fld_val));
                }
            }
            goto next_spec_pair;
        }

        fail ("FFI: field not in the parent struct");

      next_spec_pair:

        spec_item += 2;
    }
}


//
//  MAKE_Struct: C
//
// Format:
//     make struct! [
//         field1 [type1]
//         field2: [type2] field2-init-value
//         field3: [struct [field1 [type1]]]
//         field4: [type1[3]]
//         ...
//     ]
//
REB_R MAKE_Struct(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    assert(kind == REB_CUSTOM);
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    if (not IS_BLOCK(arg))
        fail (arg);

    DECLARE_FRAME_AT (f, arg, EVAL_MASK_DEFAULT);
    SHORTHAND (v, f->feed->value, const RELVAL*);
    SHORTHAND (specifier, f->feed->specifier, REBSPC*);

    Push_Frame(nullptr, f);

    REBINT max_fields = 16;

//
// SET UP SCHEMA
//
    // Every struct has a "schema"--this is a description (potentially
    // hierarchical) of its fields, including any nested structs.  The
    // schema should be shared between common instances of the same struct.
    //
    // Though the schema is not managed until the end of this creation, the
    // MAKE process runs evaluations, so the fields must be GC valid.
    //
    REBFLD *schema = Make_Array(IDX_FIELD_MAX);
    Init_Unreadable_Void(FLD_AT(schema, IDX_FIELD_TYPE));  // will fill in
    Init_Blank(FLD_AT(schema, IDX_FIELD_DIMENSION));  // not making an array
    Init_Unreadable_Void(FLD_AT(schema, IDX_FIELD_FFTYPE));  // will fill in
    Init_Blank(FLD_AT(schema, IDX_FIELD_NAME));  // no symbol for structs
    Init_Blank(FLD_AT(schema, IDX_FIELD_OFFSET));  // the offset is not used
    Init_Unreadable_Void(FLD_AT(schema, IDX_FIELD_WIDE));  // will fill in
    TERM_ARRAY_LEN(schema, IDX_FIELD_MAX);

//
// PROCESS FIELDS
//

    uint64_t offset = 0; // offset in data

    REBINT raw_size = -1;
    uintptr_t raw_addr = 0;

    if (NOT_END(*v) and IS_BLOCK(*v)) {
        //
        // !!! This would suggest raw-size, raw-addr, or extern can be leading
        // in the struct definition, perhaps as:
        //
        //     make struct! [[raw-size] ...]
        //
        DECLARE_LOCAL (specific);
        Derelativize(specific, *v, VAL_SPECIFIER(arg));
        parse_attr(specific, &raw_size, &raw_addr);
        Fetch_Next_Forget_Lookback(f);
    }

    // !!! This makes binary data for each struct level? ???
    //
    REBSER *data_bin;
    if (raw_addr == 0)
        data_bin = Make_Binary(max_fields << 2);
    else
        data_bin = nullptr; // not used, but avoid maybe uninitialized warning

    REBINT field_idx = 0; // for field index

    REBDSP dsp_orig = DSP; // use data stack to accumulate fields (BLOCK!s)

    DECLARE_LOCAL (spec);
    DECLARE_LOCAL (init); // for result to save in data

    while (NOT_END(*v)) {

        // Add another field...although we don't manage the array (so it won't
        // get GC'd) we do run evaluations, so it must be GC-valid.

        REBFLD *field = Make_Array(IDX_FIELD_MAX);
        Init_Unreadable_Void(FLD_AT(field, IDX_FIELD_TYPE));
        Init_Unreadable_Void(FLD_AT(field, IDX_FIELD_DIMENSION));
        Init_Unreadable_Void(FLD_AT(field, IDX_FIELD_FFTYPE));
        Init_Unreadable_Void(FLD_AT(field, IDX_FIELD_NAME));
        Init_Integer(FLD_AT(field, IDX_FIELD_OFFSET), offset);
        Init_Unreadable_Void(FLD_AT(field, IDX_FIELD_WIDE));
        TERM_ARRAY_LEN(field, IDX_FIELD_MAX);

        // Must be a word or a set-word, with set-words initializing

        bool expect_init;
        if (IS_SET_WORD(*v)) {
            expect_init = true;
            if (raw_addr) {
                // initialization is not allowed for raw memory struct
                fail (Error_Bad_Value_Core(*v, *specifier));
            }
        }
        else if (IS_WORD(*v))
            expect_init = false;
        else
            fail (Error_Invalid_Type(VAL_TYPE(*v)));

        Init_Word(FLD_AT(field, IDX_FIELD_NAME), VAL_WORD_SPELLING(*v));

        Fetch_Next_Forget_Lookback(f);
        if (IS_END(*v) or not IS_BLOCK(*v))
            fail (Error_Bad_Value_Core(*v, VAL_SPECIFIER(arg)));

        Derelativize(spec, *v, VAL_SPECIFIER(arg));

        // Fills in the width, dimension, type, and ffi_type (if needed)
        //
        Parse_Field_Type_May_Fail(field, spec, init);

        REBLEN dimension = FLD_IS_ARRAY(field) ? FLD_DIMENSION(field) : 1;
        Fetch_Next_Forget_Lookback(f);

        // !!! Why does the fail take out as an argument?  (Copied from below)

        if (FLD_WIDE(field) > UINT32_MAX)
            fail (Error_Size_Limit_Raw(out));
        if (dimension > UINT32_MAX)
            fail (Error_Size_Limit_Raw(out));

        uint64_t step =
            cast(uint64_t, FLD_WIDE(field)) * cast(uint64_t, dimension);

        if (step > VAL_STRUCT_LIMIT)
            fail (Error_Size_Limit_Raw(out));

        if (raw_addr == 0)
            EXPAND_SERIES_TAIL(data_bin, step);

        if (expect_init) {
            if (IS_END(*v))
               fail (arg);

            if (IS_BLOCK(*v)) {
                REBDSP dsp_reduce = DSP;
                if (Reduce_To_Stack_Throws(out, *v, VAL_SPECIFIER(arg)))
                    fail (Error_No_Catch_For_Throw(init));

                Init_Block(init, Pop_Stack_Values(dsp_reduce));

                Fetch_Next_Forget_Lookback(f);
            }
            else {
                if (Eval_Step_Throws(init, f))
                    fail (Error_No_Catch_For_Throw(init));
            }

            if (FLD_IS_ARRAY(field)) {
                if (IS_INTEGER(init)) {  // interpreted as a C pointer
                    void *ptr = cast(void*, cast(intptr_t, VAL_INT64(init)));

                    // assume valid pointer to enough space
                    memcpy(
                        SER_AT(REBYTE, data_bin, cast(REBLEN, offset)),
                        ptr,
                        FLD_LEN_BYTES_TOTAL(field)
                    );
                }
                else if (IS_BLOCK(init)) {
                    REBLEN n = 0;

                    if (VAL_LEN_AT(init) != FLD_DIMENSION(field))
                        fail (init);

                    // assign
                    for (n = 0; n < FLD_DIMENSION(field); n ++) {
                        if (not assign_scalar_core(
                            BIN_HEAD(data_bin),
                            offset,
                            field,
                            n,
                            SPECIFIC(VAL_ARRAY_AT_HEAD(init, n))
                        )){
                            fail ("FFI: Failed to assign element value");
                        }
                    }
                }
                else
                    fail (Error_Unexpected_Type(REB_BLOCK, VAL_TYPE(*v)));
            }
            else {
                // scalar
                if (not assign_scalar_core(
                    BIN_HEAD(data_bin), offset, field, 0, init
                )) {
                    fail ("FFI: Failed to assign scalar value");
                }
            }
        }
        else if (raw_addr == 0) {
            if (FLD_IS_STRUCT(field)) {
                REBLEN n = 0;
                for (
                    n = 0;
                    n < (FLD_IS_ARRAY(field) ? FLD_DIMENSION(field) : 1);
                    ++n
                ){
                    memcpy(
                        SER_AT(
                            REBYTE,
                            data_bin,
                            cast(REBLEN, offset) + (n * FLD_WIDE(field))
                        ),
                        VAL_STRUCT_DATA_HEAD(init),
                        FLD_WIDE(field)
                    );
                }
            }
            else {
                memset(
                    SER_AT(REBYTE, data_bin, cast(REBLEN, offset)),
                    0,
                    FLD_LEN_BYTES_TOTAL(field)
                );
            }
        }

        offset += step;

        //if (alignment != 0) {
        //  offset = ((offset + alignment - 1) / alignment) * alignment;

        if (offset > VAL_STRUCT_LIMIT)
            fail (Error_Size_Limit_Raw(out));

        ++field_idx;

        Init_Block(DS_PUSH(), field);  // really should be an OBJECT!
    }

    REBARR *fieldlist = Pop_Stack_Values_Core(dsp_orig, NODE_FLAG_MANAGED);

    Init_Block(FLD_AT(schema, IDX_FIELD_TYPE), fieldlist);
    Prepare_Field_For_FFI(schema);

    Init_Integer(FLD_AT(schema, IDX_FIELD_WIDE), offset); // total size known

//
// FINALIZE VALUE
//

    REBSTU *stu = Alloc_Singular(
        NODE_FLAG_MANAGED | SERIES_FLAG_LINK_NODE_NEEDS_MARK
    );
    Manage_Array(schema);
    LINK_SCHEMA_NODE(stu) = NOD(schema);

    if (raw_addr) {
        make_ext_storage(
            stu,
            FLD_LEN_BYTES_TOTAL(schema),
            raw_size,
            raw_addr
        );
    }
    else {
        TERM_BIN(data_bin);
        Init_Binary(ARR_SINGLE(stu), data_bin);
    }

    Init_Struct(out, stu);
    Drop_Frame(f);  // has to be after the pop and all nodes managed

    return out;
}


//
//  TO_Struct: C
//
REB_R TO_Struct(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    return MAKE_Struct(out, kind, nullptr, arg);
}


//
//  PD_Struct: C
//
REB_R PD_Struct(
    REBPVS *pvs,
    const RELVAL *picker,
    const REBVAL *opt_setval
){
    REBSTU *stu = VAL_STRUCT(pvs->out);
    fail_if_non_accessible(stu);

    if (not IS_WORD(picker))
        return R_UNHANDLED;

    if (opt_setval == nullptr) {
        if (not Get_Struct_Var(pvs->out, stu, picker))
            return R_UNHANDLED;

        // !!! Comment here said "Setting element to an array in the struct"
        // and gave the example `struct/field/1: 0`.  What is thus happening
        // here is that the ordinary SET-PATH! dispatch which goes one step
        // at a time can't work to update something whose storage is not
        // a REBVAL*.  So (struct/field) produces a temporary BLOCK! out of
        // the C array data, and if the set just sets an element in that
        // block then it will be forgotten and have no effect.
        //
        // So the workaround is to bypass ordinary dispatch and call it to
        // look ahead manually by one step.  Whatever change is made to
        // the block is then turned around and re-set in the underlying
        // memory that produced it.
        //
        // A better general mechanism for this kind of problem is needed,
        // although it only affects "extension types" which use natively
        // packed structures to store their state instead of REBVAL.  (See
        // a similar technique used by PD_Gob)
        //
        if (
            PVS_IS_SET_PATH(pvs)
            and IS_BLOCK(pvs->out)
            and IS_END(pvs->feed->value + 1)
        ) {
            // !!! This is dodgy; it has to copy (as picker is a pointer to
            // a memory cell it may not own), has to guard (as the next path
            // evaluation may not protect the result...)
            //
            DECLARE_LOCAL (sel_orig);
            Blit_Cell(cast(RELVAL*, sel_orig), picker);
            PUSH_GC_GUARD(sel_orig);

            if (Next_Path_Throws(pvs)) { // updates pvs->out, PVS_PICKER()
                DROP_GC_GUARD(sel_orig);
                fail (Error_No_Catch_For_Throw(pvs->out)); // !!! Review
            }

            DECLARE_LOCAL (specific);
            if (VAL_TYPE(pvs->out) == REB_R_REFERENCE)
                Derelativize(
                    specific,
                    pvs->u.ref.cell,
                    pvs->u.ref.specifier
                );
            else
                Move_Value(specific, pvs->out);

            if (not Set_Struct_Var(stu, sel_orig, nullptr, specific))
                return R_UNHANDLED;

            DROP_GC_GUARD(sel_orig);

            return R_INVISIBLE;
        }

        return pvs->out;
    }
    else {
        if (not Set_Struct_Var(stu, picker, nullptr, opt_setval))
            return R_UNHANDLED;

        return R_INVISIBLE;
    }
}


//
//  Cmp_Struct: C
//
REBINT Cmp_Struct(REBCEL(const*) s, REBCEL(const*) t)
{
    REBINT n = VAL_STRUCT_FIELDLIST(s) - VAL_STRUCT_FIELDLIST(t);
    fail_if_non_accessible(VAL_STRUCT(s));
    fail_if_non_accessible(VAL_STRUCT(t));
    if (n != 0) {
        return n;
    }
    n = VAL_STRUCT(s) - VAL_STRUCT(t);
    return n;
}


//
//  CT_Struct: C
//
REBINT CT_Struct(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    if (strict)
        return Cmp_Struct(a, b);

    if (Cmp_Struct(a, b) == 0)
        return 0;

    return (
        CELL_KIND(a) == REB_CUSTOM
        and CELL_KIND(b) == REB_CUSTOM
        and CELL_CUSTOM_TYPE(a) == EG_Struct_Type
        and CELL_CUSTOM_TYPE(b) == EG_Struct_Type
        and same_fields(VAL_STRUCT_FIELDLIST(a), VAL_STRUCT_FIELDLIST(b))
        and VAL_STRUCT_SIZE(a) == VAL_STRUCT_SIZE(b)
        and not memcmp(
            VAL_STRUCT_DATA_HEAD(a),
            VAL_STRUCT_DATA_HEAD(b),
            VAL_STRUCT_SIZE(a)
        )
    ) ? 0 : 1;  // !!! > or < result needed, but comparison is under review
}


//
//  Copy_Struct_Managed: C
//
REBSTU *Copy_Struct_Managed(REBSTU *src)
{
    fail_if_non_accessible(src);
    assert(ARR_LEN(src) == 1);

    // This doesn't copy the data out of the array, or the schema...just the
    // value.  In fact, the schema is in the misc field and has to just be
    // linked manually.
    //
    REBSTU *copy = Copy_Array_Shallow(src, SPECIFIED);
    LINK_SCHEMA_NODE(copy) = LINK_SCHEMA_NODE(src);  // share the same schema
    MISC_STU_OFFSET(copy) = MISC_STU_OFFSET(src);  // copies offset

    // Update the binary data with a copy of its sequence.
    //
    // !!! Note that the offset is left intact, and as written will make a
    // copy as big as struct the instance is embedded into if nonzero offset.
    //
    REBSER *bin_copy = Make_Binary(STU_DATA_LEN(src));
    memcpy(BIN_HEAD(bin_copy), STU_DATA_HEAD(src), STU_DATA_LEN(src));
    TERM_BIN_LEN(bin_copy, STU_DATA_LEN(src));
    Init_Binary(ARR_SINGLE(copy), bin_copy);

    Manage_Array(copy);
    return copy;
}


//
//  REBTYPE: C
//
REBTYPE(Struct)
{
    REBVAL *val = D_ARG(1);
    REBVAL *arg;

    // unary actions
    switch (VAL_WORD_SYM(verb)) {
      case SYM_CHANGE: {
        arg = D_ARG(2);
        if (not IS_BINARY(arg))
            fail (Error_Unexpected_Type(REB_BINARY, VAL_TYPE(arg)));

        if (VAL_LEN_AT(arg) != VAL_STRUCT_DATA_LEN(val))
            fail (arg); // !!! better to fail on PAR(value)?

        memcpy(
            VAL_STRUCT_DATA_HEAD(val),
            BIN_HEAD(VAL_SERIES(arg)),
            VAL_STRUCT_DATA_LEN(val)
        );
        Move_Value(D_OUT, val);
        return D_OUT; }

      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value));
        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_LENGTH:
            return Init_Integer(D_OUT, VAL_STRUCT_DATA_LEN(val));

        case SYM_VALUES: {
            fail_if_non_accessible(VAL_STRUCT(val));
            REBSER *bin = Make_Binary(VAL_STRUCT_SIZE(val));
            memcpy(
                BIN_HEAD(bin),
                VAL_STRUCT_DATA_AT(val),
                VAL_STRUCT_SIZE(val)
            );
            TERM_BIN_LEN(bin, VAL_STRUCT_SIZE(val));
            return Init_Binary(D_OUT, bin); }

        case SYM_SPEC:
            return Init_Block(D_OUT, Struct_To_Array(VAL_STRUCT(val)));

        default:
            break;
        }
        // !!! Used to say REB_STRUCT, but it's not a builtin type--errors
        // need thinking for custom types.
        //
        fail (Error_Cannot_Reflect(REB_CUSTOM, ARG(property))); }

      default:
        break;
    }

    return R_UNHANDLED;
}
