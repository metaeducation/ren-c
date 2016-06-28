//
//  File: %t-strut.c
//  Summary: "C struct object datatype"
//  Section: datatypes
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

#include "sys-core.h"

#define STATIC_assert(e) do {(void)sizeof(char[1 - 2*!(e)]);} while(0)


//
//  Get_FFType_Enum_Info_Core: C
//
// Returns whether the FFI type is integer-like (REB_INTEGER) or decimal-like
// (REB_DECIMAL).  If it is neither, it gives back REB_0.  Returns a symbol
// if one is applicable.
//
// !!! Previously this was a table, which was based on a duplicate of the
// FFI_TYPE constants as an enum (STRUCT_TYPE_XXX).  Getting rid of the
// STRUCT_TYPE_XXX helps reduce confusion and redundancy, but there is no
// FFI_TYPE_MAX or fixed ordering guaranteed necessarily of the constants.
// Having a `switch` is worth not creating a mirror enum, however.
//
void *Get_FFType_Enum_Info_Core(
    REBSTR **name_out,
    enum Reb_Kind *kind_out,
    unsigned short type
) {
    switch (type) {
    case FFI_TYPE_UINT8:
        *name_out = Canon(SYM_UINT8);
        *kind_out = REB_INTEGER;
        return &ffi_type_uint8;

    case FFI_TYPE_SINT8:
        *name_out = Canon(SYM_INT8);
        *kind_out = REB_INTEGER;
        return &ffi_type_sint8;

    case FFI_TYPE_UINT16:
        *name_out = Canon(SYM_UINT16);
        *kind_out = REB_INTEGER;
        return &ffi_type_uint16;

    case FFI_TYPE_SINT16:
        *name_out = Canon(SYM_INT16);
        *kind_out = REB_INTEGER;
        return &ffi_type_sint16;

    case FFI_TYPE_UINT32:
        *name_out = Canon(SYM_UINT32);
        *kind_out = REB_INTEGER;
        return &ffi_type_uint32;

    case FFI_TYPE_SINT32:
        *name_out = Canon(SYM_INT32);
        *kind_out = REB_INTEGER;
        return &ffi_type_sint32;

    case FFI_TYPE_UINT64:
        *name_out = Canon(SYM_INT64);
        *kind_out = REB_INTEGER;
        return &ffi_type_uint64;

    case FFI_TYPE_SINT64:
        *name_out = Canon(SYM_INT64);
        *kind_out = REB_INTEGER;
        return &ffi_type_sint64;

    case FFI_TYPE_FLOAT:
        *name_out = Canon(SYM_FLOAT);
        *kind_out = REB_DECIMAL;
        return &ffi_type_float;

    case FFI_TYPE_DOUBLE:
        *name_out = Canon(SYM_DOUBLE);
        *kind_out = REB_DECIMAL;
        return &ffi_type_double;

    case FFI_TYPE_POINTER:
        *name_out = Canon(SYM_POINTER);
        *kind_out = REB_0;
        return &ffi_type_pointer;

    // !!! SYM_INTEGER, SYM_DECIMAL, SYM_STRUCT was "-1" in original table

    default:
        assert(FALSE);
        *name_out = NULL;
        *kind_out = REB_0;
        return NULL;
    }
}


static void fail_if_non_accessible(const REBVAL *val)
{
    if (VAL_STRUCT_INACCESSIBLE(val)) {
        REBSER *data = VAL_STRUCT_DATA_BIN(val);
        REBVAL i;
        SET_INTEGER(&i, cast(REBUPT, SER_DATA_RAW(data)));
        fail (Error(RE_BAD_MEMORY, &i, val));
    }
}

static void get_scalar(
    REBSTU *stu,
    const struct Struct_Field *field,
    REBCNT n, // element index, starting from 0
    REBVAL *val
){
    if (STU_INACCESSIBLE(stu)) {
        //
        // !!! This just sets the field to void with no error...that seems
        // like a bad idea.
        //
        if (field->type != FFI_TYPE_STRUCT) {
            SET_VOID(val);
            return;
        }
    }

    assert(n == 0 || field->is_array); // only index in field[N], not field

    REBYTE *data = SER_AT(
        REBYTE,
        STU_DATA_BIN(stu),
        STU_OFFSET(stu) + field->offset + n * field->size
    );

    switch (field->type) {
    case FFI_TYPE_UINT8:
        SET_INTEGER(val, *cast(u8*, data));
        break;

    case FFI_TYPE_SINT8:
        SET_INTEGER(val, *cast(i8*, data));
        break;

    case FFI_TYPE_UINT16:
        SET_INTEGER(val, *cast(u16*, data));
        break;

    case FFI_TYPE_SINT16:
        SET_INTEGER(val, *cast(i8*, data));
        break;

    case FFI_TYPE_UINT32:
        SET_INTEGER(val, *cast(u32*, data));
        break;

    case FFI_TYPE_SINT32:
        SET_INTEGER(val, *cast(i32*, data));
        break;

    case FFI_TYPE_UINT64:
        SET_INTEGER(val, *cast(u64*, data));
        break;

    case FFI_TYPE_SINT64:
        SET_INTEGER(val, *cast(i64*, data));
        break;

    case FFI_TYPE_FLOAT:
        SET_DECIMAL(val, *cast(float*, data));
        break;

    case FFI_TYPE_DOUBLE:
        SET_DECIMAL(val, *cast(double*, data));
        break;

    case FFI_TYPE_POINTER:
        if (field->is_rebval) {
            assert(field->size == sizeof(REBVAL));
            assert(field->dimension == 4);
            memcpy(val, data, sizeof(REBVAL));
        }
        else
            SET_INTEGER(val, cast(REBUPT, *cast(void**, data)));
        break;

    case FFI_TYPE_STRUCT:
        {
        // In order for the schema to participate in GC it must be a series.
        // Currently this series is created with a single value of the root
        // schema in the case of a struct expansion.  This wouldn't be
        // necessary if each field that was a structure offered a REBSER
        // already... !!! ?? !!! ... it will be necessary if the schemas
        // are to uniquely carry an ffi_type freed when they are GC'd
        //
        REBSER *field_1 = Make_Series(
            sizeof(struct Struct_Field), 1, MKS_NONE
        );
        *SER_HEAD(struct Struct_Field, field_1) = *field;
        SET_SERIES_LEN(field_1, 1);
        MANAGE_SERIES(field_1);

        REBSTU *sub_stu = Make_Singular_Array(VOID_CELL);
        ARR_SERIES(sub_stu)->link.schema = field_1;

        // In this case the structure lives at an offset inside another.
        //
        VAL_RESET_HEADER(val, REB_STRUCT);
        val->payload.structure.stu = sub_stu;
        val->payload.structure.data = STU_DATA_BIN(stu); // inside parent data
        val->extra.struct_offset = data - BIN_HEAD(VAL_STRUCT_DATA_BIN(val));
        assert(VAL_STRUCT_SIZE(val) == field->size); // implicit from schema

        // With all fields initialized, assign canon value as singular value
        //
        *ARR_HEAD(stu) = *val;
        assert(ARR_LEN(sub_stu) == 1);
        MANAGE_ARRAY(sub_stu);
        }
        break;

    default:
        assert(FALSE);
        fail (Error(RE_MISC));
    }
}


//
//  Get_Struct_Var: C
//
static REBOOL Get_Struct_Var(REBSTU *stu, const REBVAL *word, REBVAL *val)
{
    REBSER *fieldlist = STU_FIELDLIST(stu);

    struct Struct_Field *field = SER_HEAD(struct Struct_Field, fieldlist);

    REBCNT i;
    for (i = 0; i < SER_LEN(fieldlist); ++i, ++field) {
        if (STR_CANON(field->name) == VAL_WORD_CANON(word)) {
            if (field->is_array) {
                //
                // Structs contain packed data for the field type in an array.
                // This data cannot expand or contract, and is not in a
                // Rebol-compatible format.  A Rebol Array is made by
                // extracting the information.
                //
                // !!! Perhaps a fixed-size VECTOR! could have its data
                // pointer into these arrays?
                //
                REBARR *array = Make_Array(field->dimension);
                REBCNT n;
                for (n = 0; n < field->dimension; n ++) {
                    REBVAL elem;
                    get_scalar(stu, field, n, &elem);
                    Append_Value(array, &elem);
                }
                Val_Init_Block(val, array);
            }
            else
                get_scalar(stu, field, 0, val);

            return TRUE;
        }
    }

    return FALSE; // word not found in struct's field symbols
}


#ifdef NEED_SET_STRUCT_VARS
//
//  Set_Struct_Vars: C
//
static void Set_Struct_Vars(REBSTU *strut, REBVAL *blk)
{
}
#endif


//
//  Struct_To_Array: C
// 
// Used by MOLD to create a block.
//
// Cannot fail(), because fail() could call MOLD on a struct!, which will end
// up infinitive recursive calls
//
REBARR *Struct_To_Array(REBSTU *stu)
{
    REBARR *array = Make_Array(10);
    REBSER *fieldlist = STU_FIELDLIST(stu);
    struct Struct_Field *field = SER_HEAD(struct Struct_Field, fieldlist);

    // We are building a recursive structure.  So if we did not hand each
    // sub-series over to the GC then a single Free_Series() would not know
    // how to free them all.  There would have to be a specialized walk to
    // free the resulting structure.  Hence, don't invoke the GC until the
    // root series being returned is done being used or is safe from GC!
    //
    MANAGE_ARRAY(array);

    // fail_if_non_accessible(STU_TO_VAL(stu));

    REBCNT i;
    for(i = 0; i < SER_LEN(fieldlist); i++, field ++) {
        REBVAL *val = NULL;
        REBVAL *type_blk = NULL;

        /* required field name */
        val = Alloc_Tail_Array(array);
        Val_Init_Word(val, REB_SET_WORD, field->name);

        /* required type */
        type_blk = Alloc_Tail_Array(array);
        Val_Init_Block(type_blk, Make_Array(1));

        val = Alloc_Tail_Array(VAL_ARRAY(type_blk));
        if (field->type == FFI_TYPE_STRUCT) {
            REBVAL *nested;
            DS_PUSH_TRASH_SAFE;
            nested = DS_TOP;

            Val_Init_Word(val, REB_WORD, Canon(SYM_STRUCT_X));
            get_scalar(stu, field, 0, nested);
            val = Alloc_Tail_Array(VAL_ARRAY(type_blk));
            Val_Init_Block(val, Struct_To_Array(VAL_STRUCT(nested)));

            DS_DROP;
        }
        else {
            REBSTR *name;
            enum Reb_Kind kind; // dummy
            Get_FFType_Enum_Info(&name, &kind, field->type);
            assert(name != NULL); // !!! was not previously asserted (?)
            Val_Init_Word(val, REB_WORD, name);
        }

        /* optional dimension */
        if (field->dimension > 1) {
            REBARR *dim = Make_Array(1);
            REBVAL *dv = NULL;
            val = Alloc_Tail_Array(VAL_ARRAY(type_blk));
            Val_Init_Block(val, dim);

            dv = Alloc_Tail_Array(dim);
            SET_INTEGER(dv, field->dimension);
        }

        /* optional initialization */
        if (field->dimension > 1) {
            REBARR *dim = Make_Array(1);
            REBCNT n = 0;
            val = Alloc_Tail_Array(array);
            Val_Init_Block(val, dim);
            for (n = 0; n < field->dimension; n ++) {
                REBVAL *dv = Alloc_Tail_Array(dim);
                get_scalar(stu, field, n, dv);
            }
        } else {
            val = Alloc_Tail_Array(array);
            get_scalar(stu, field, 0, val);
        }
    }
    return array;
}


static REBOOL same_fields(REBSER *tgt, REBSER *src)
{
    struct Struct_Field *tgt_fields = SER_HEAD(struct Struct_Field, tgt);
    struct Struct_Field *src_fields = SER_HEAD(struct Struct_Field, src);

    if (SER_LEN(tgt) != SER_LEN(src))
        return FALSE;

    REBCNT n;
    for(n = 0; n < SER_LEN(src); n ++) {
        if (tgt_fields[n].type != src_fields[n].type) {
            return FALSE;
        }
        if (!SAME_STR(tgt_fields[n].name, src_fields[n].name)
            || tgt_fields[n].offset != src_fields[n].offset
            || tgt_fields[n].dimension != src_fields[n].dimension
            || tgt_fields[n].size != src_fields[n].size) {
            return FALSE;
        }
        if (tgt_fields[n].type == FFI_TYPE_STRUCT
            && ! same_fields(tgt_fields[n].fields, src_fields[n].fields)) {
            return FALSE;
        }
    }

    return TRUE;
}


static REBOOL assign_scalar_core(
    REBSER *data_bin,
    REBCNT offset,
    struct Struct_Field *field,
    REBCNT n,
    const REBVAL *val
){
    assert(n == 0 || field->is_array);

    void *data = SER_AT(
        REBYTE,
        data_bin,
        offset + field->offset + n * field->size
    );

    u64 i = 0;
    double d = 0;

    if (field->is_rebval) {
        assert(FALSE); // need to actually adjust for correct n
        assert(field->type == FFI_TYPE_POINTER);
        assert(field->dimension % 4 == 0);
        assert(field->size == sizeof(REBVAL));
        memcpy(data, val, sizeof(REBVAL));
        return TRUE;
    }

    REBSTR *sym; // dummy
    enum Reb_Kind kind;
    Get_FFType_Enum_Info(&sym, &kind, field->type);

    switch (VAL_TYPE(val)) {
        case REB_DECIMAL:
            if (kind != REB_INTEGER && kind != REB_DECIMAL)
                fail (Error_Invalid_Type(VAL_TYPE(val)));

            d = VAL_DECIMAL(val);
            i = (u64) d;
            break;
        case REB_INTEGER:
            if (kind != REB_INTEGER && kind != REB_DECIMAL)
                if (field->type != FFI_TYPE_POINTER)
                    fail (Error_Invalid_Type(VAL_TYPE(val)));

            i = (u64) VAL_INT64(val);
            d = (double)i;
            break;
        case REB_STRUCT:
            if (FFI_TYPE_STRUCT != field->type)
                fail (Error_Invalid_Type(VAL_TYPE(val)));
            break;
        default:
            fail (Error_Invalid_Type(VAL_TYPE(val)));
    }

    switch (field->type) {
        case FFI_TYPE_SINT8:
            *(i8*)data = (i8)i;
            break;
        case FFI_TYPE_UINT8:
            *(u8*)data = (u8)i;
            break;
        case FFI_TYPE_SINT16:
            *(i16*)data = (i16)i;
            break;
        case FFI_TYPE_UINT16:
            *(u16*)data = (u16)i;
            break;
        case FFI_TYPE_SINT32:
            *(i32*)data = (i32)i;
            break;
        case FFI_TYPE_UINT32:
            *(u32*)data = (u32)i;
            break;
        case FFI_TYPE_SINT64:
            *(i64*)data = (i64)i;
            break;
        case FFI_TYPE_UINT64:
            *(u64*)data = (u64)i;
            break;
        case FFI_TYPE_POINTER:
            *cast(void**, data) = cast(void*, cast(REBUPT, i));
            break;
        case FFI_TYPE_FLOAT:
            *(float*)data = (float)d;
            break;
        case FFI_TYPE_DOUBLE:
            *(double*)data = (double)d;
            break;
        case FFI_TYPE_STRUCT:
            if (field->size != VAL_STRUCT_SIZE(val))
                fail (Error_Invalid_Arg(val));

            if (same_fields(field->fields, VAL_STRUCT_FIELDLIST(val))) {
                memcpy(
                    data,
                    SER_AT(
                        REBYTE,
                        VAL_STRUCT_DATA_BIN(val),
                        VAL_STRUCT_OFFSET(val)
                    ),
                    field->size
                );
            } else
                fail (Error_Invalid_Arg(val));
            break;
        default:
            /* should never be here */
            return FALSE;
    }
    return TRUE;
}


inline static REBOOL assign_scalar(
    REBSTU *stu,
    struct Struct_Field *field,
    REBCNT n,
    const REBVAL *val
) {
    return assign_scalar_core(
        STU_DATA_BIN(stu), STU_OFFSET(stu), field, n, val
    );
}


//
//  Set_Struct_Var: C
//
static REBOOL Set_Struct_Var(
    REBSTU *stu,
    const REBVAL *word,
    const REBVAL *elem,
    const REBVAL *val
) {
    REBSER *fieldlist = STU_FIELDLIST(stu);
    struct Struct_Field *field = SER_HEAD(struct Struct_Field, fieldlist);

    REBCNT i;

    for (i = 0; i < SER_LEN(fieldlist); i ++, field ++) {
        if (VAL_WORD_CANON(word) == STR_CANON(field->name)) {
            if (field->is_array) {
                if (elem == NULL) { //set the whole array
                    REBCNT n = 0;

                    if (!IS_BLOCK(val))
                        return FALSE;

                    if (field->dimension != VAL_LEN_AT(val))
                        return FALSE;

                    for(n = 0; n < field->dimension; n ++) {
                        if (!assign_scalar(
                            stu, field, n, KNOWN(VAL_ARRAY_AT_HEAD(val, n))
                        )) {
                            return FALSE;
                        }
                    }
                }
                else { // set only one element
                    if (!IS_INTEGER(elem)
                        || VAL_INT32(elem) <= 0
                        || VAL_INT32(elem) > cast(REBINT, field->dimension)
                    ) {
                        return FALSE;
                    }
                    return assign_scalar(stu, field, VAL_INT32(elem) - 1, val);
                }
                return TRUE;
            } else {
                return assign_scalar(stu, field, 0, val);
            }
            return TRUE;
        }
    }
    return FALSE;
}


/* parse struct attribute */
static void parse_attr (REBVAL *blk, REBINT *raw_size, REBUPT *raw_addr)
{
    REBVAL *attr = KNOWN(VAL_ARRAY_AT(blk));

    *raw_size = -1;
    *raw_addr = 0;

    while (NOT_END(attr)) {
        if (IS_SET_WORD(attr)) {
            switch (VAL_WORD_SYM(attr)) {
                case SYM_RAW_SIZE:
                    ++ attr;
                    if (NOT_END(attr) && IS_INTEGER(attr)) {
                        if (*raw_size > 0) /* duplicate raw-size */
                            fail (Error_Invalid_Arg(attr));

                        *raw_size = VAL_INT64(attr);
                        if (*raw_size <= 0)
                            fail (Error_Invalid_Arg(attr));
                    }
                    else
                        fail (Error_Invalid_Arg(attr));
                    break;

                case SYM_RAW_MEMORY:
                    ++ attr;
                    if (NOT_END(attr) && IS_INTEGER(attr)) {
                        if (*raw_addr != 0) /* duplicate raw-memory */
                            fail (Error_Invalid_Arg(attr));

                        *raw_addr = cast(REBU64, VAL_INT64(attr));
                        if (*raw_addr == 0)
                            fail (Error_Invalid_Arg(attr));
                    }
                    else
                        fail (Error_Invalid_Arg(attr));
                    break;

                case SYM_EXTERN:
                    ++ attr;

                    if (*raw_addr != 0) // raw-memory is exclusive with extern
                        fail (Error_Invalid_Arg(attr));

                    if (IS_END(attr) || !IS_BLOCK(attr)
                        || VAL_LEN_AT(attr) != 2) {
                        fail (Error_Invalid_Arg(attr));
                    }
                    else {
                        REBVAL *lib = KNOWN(VAL_ARRAY_AT_HEAD(attr, 0));
                        if (!IS_LIBRARY(lib))
                            fail (Error_Invalid_Arg(attr));
                        if (IS_LIB_CLOSED(VAL_LIBRARY(lib)))
                            fail (Error(RE_BAD_LIBRARY));

                        REBVAL *sym = KNOWN(VAL_ARRAY_AT_HEAD(attr, 1));
                        if (!ANY_BINSTR(sym))
                            fail (Error_Invalid_Arg(sym));

                        CFUNC *addr = OS_FIND_FUNCTION(
                            VAL_LIBRARY_FD(lib),
                            s_cast(VAL_RAW_DATA_AT(sym))
                        );
                        if (!addr)
                            fail (Error(RE_SYMBOL_NOT_FOUND, sym));

                        *raw_addr = cast(REBUPT, addr);
                    }
                    break;

                    /*
                       case SYM_ALIGNMENT:
                       ++ attr;
                       if (IS_INTEGER(attr)) {
                       alignment = VAL_INT64(attr);
                       } else {
                       fail (Error_Invalid_Arg(attr));
                       }
                       break;
                       */
                default:
                    fail (Error_Invalid_Arg(attr));
            }
        }
        else
            fail (Error_Invalid_Arg(attr));

        ++ attr;
    }
}

//
// set storage memory to external addr: raw_addr
//
// !!! The STRUCT! type is being converted to be "more like a user defined
// type", in that it will be driven less by specialized C structures and
// done more in usermode mechanics.  One glitch in that is this routine
// which apparently was used specifically on structs to allow their data
// pointer to come from an external memory address.  With STRUCT! relying
// on BINARY! for its storage, this introduces the concept of an
// "external binary".  While a generalized external binary might be
// an interesting feature, it would come at the cost that every series
// access on BINARY! for the data would have to check if it was loaded
// or not.
//
// This suggests perhaps that BINARY! can't be used and some kind of handle
// would instead.
//
// This is something that should be discussed with Atronix to figure out
// exactly why this was added
//
static REBSER *make_ext_storage(
    REBCNT len,
    REBINT raw_size,
    REBUPT raw_addr
) {
    if (raw_size >= 0 && raw_size != cast(REBINT, len))
        fail (Error(RE_INVALID_DATA));

    REBSER *ser = Make_Series(
        len + 1, // include term. !!! not included otherwise (?)
        1, // width
        MKS_EXTERNAL
    );

    SER_SET_EXTERNAL_DATA(ser, cast(REBYTE*, raw_addr));
    SET_SER_FLAG(ser, SERIES_FLAG_ACCESSIBLE); // accessible by default
    SET_SERIES_LEN(ser, len);

    MANAGE_SERIES(ser);
    return ser;
}


//
// This takes a spec like `[int32 [2]]` and sets the output field's properties
// such as `type`, `size`, `is_array`, `dimension`, etc.  It does this by
// recognizing a finite set of FFI type keywords defined in %words.r.
//
// This also allows for embedded structure types.  If the type is not being
// included by reference, but rather with a sub-definition inline, then it
// will actually be creating a new `inner` STRUCT! value.  Since this value
// is managed and not referred to elsewhere, there can't be evaluations.
//
static void Parse_Field_Type_May_Fail(
    struct Struct_Field *field,
    REBVAL *spec,
    REBVAL *inner,
    REBVAL **init
){
    RELVAL *val = VAL_ARRAY_AT(spec);

    if (IS_END(val))
        fail (Error(RE_MISC)); // !!! better error

    field->is_rebval = FALSE; // by default, not a REBVAL

    if (IS_WORD(val)) {

        switch (VAL_WORD_SYM(val)) {
        case SYM_UINT8:
            field->type = FFI_TYPE_UINT8;
            field->size = 1;
            break;

        case SYM_INT8:
            field->type = FFI_TYPE_SINT8;
            field->size = 1;
            break;

        case SYM_UINT16:
            field->type = FFI_TYPE_UINT16;
            field->size = 2;
            break;

        case SYM_INT16:
            field->type = FFI_TYPE_SINT16;
            field->size = 2;
            break;

        case SYM_UINT32:
            field->type = FFI_TYPE_UINT32;
            field->size = 4;
            break;

        case SYM_INT32:
            field->type = FFI_TYPE_SINT32;
            field->size = 4;
            break;

        case SYM_UINT64:
            field->type = FFI_TYPE_UINT64;
            field->size = 8;
            break;

        case SYM_INT64:
            field->type = FFI_TYPE_SINT64;
            field->size = 8;
            break;

        case SYM_FLOAT:
            field->type = FFI_TYPE_FLOAT;
            field->size = 4;
            break;

        case SYM_DOUBLE:
            field->type = FFI_TYPE_DOUBLE;
            field->size = 8;
            break;

        case SYM_POINTER:
            field->type = FFI_TYPE_POINTER;
            field->size = sizeof(void*);
            break;

        case SYM_STRUCT_X:
            ++ val;
            if (IS_BLOCK(val)) {
                REBVAL specified;
                COPY_VALUE(&specified, val, VAL_SPECIFIER(spec));
                MAKE_Struct(inner, REB_STRUCT, &specified); // may fail()

                field->size = SER_LEN(VAL_STRUCT_DATA_BIN(inner));
                field->type = FFI_TYPE_STRUCT;
                field->fields = VAL_STRUCT_FIELDLIST(inner);
                field->spec = VAL_STRUCT_SPEC(inner);
                *init = inner; // a shortcut for struct intialization
            }
            else
                fail (Error_Unexpected_Type(REB_BLOCK, VAL_TYPE(val)));
            break;

        case SYM_REBVAL:
            field->is_rebval = TRUE;
            field->type = FFI_TYPE_POINTER;
            field->size = sizeof(void*); // multiplied by 4 at the end
            break;

        default:
            fail (Error_Invalid_Type(VAL_TYPE(val)));
        }
    }
    else if (IS_STRUCT(val)) {
        //
        // [b: [struct-a] val-a]
        //
        field->size = SER_LEN(VAL_STRUCT_DATA_BIN(val));
        field->type = FFI_TYPE_STRUCT;
        field->fields = VAL_STRUCT_FIELDLIST(val);
        field->spec = VAL_STRUCT_SPEC(val);
        COPY_VALUE(*init, val, VAL_SPECIFIER(spec));
    }
    else
        fail (Error_Invalid_Type(VAL_TYPE(val)));

    ++ val;

    if (IS_END(val)) {
        field->dimension = 1; // scalar
        field->is_array = 0; // FALSE, but bitfield must be integer
    }
    else if (IS_BLOCK(val)) {
        //
        // make struct! [a: [int32 [2]] [0 0]]
        //
        REBVAL ret;
        if (Do_At_Throws(
            &ret,
            VAL_ARRAY(val),
            VAL_INDEX(val),
            VAL_SPECIFIER(spec)
        )) {
            // !!! Does not check for thrown cases...what should this
            // do in case of THROW, BREAK, QUIT?
            fail (Error_No_Catch_For_Throw(&ret));
        }

        if (!IS_INTEGER(&ret))
            fail (Error_Unexpected_Type(REB_INTEGER, VAL_TYPE(val)));

        field->dimension = cast(REBCNT, VAL_INT64(&ret));
        field->is_array = 1; // TRUE, but bitfield must be integer
        ++ val;
    }
    else
        fail (Error_Invalid_Type(VAL_TYPE(val)));

    if (field->is_rebval)
        field->dimension = field->dimension * 4;
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
static REBCNT Total_Struct_Dimensionality(REBSER *fields)
{
    REBCNT n_fields = 0;

    REBCNT i;
    for (i = 0; i < SER_LEN(fields); ++i) {
        struct Struct_Field *field = SER_AT(struct Struct_Field, fields, i);

        if (field->type != FFI_TYPE_STRUCT)
            n_fields += field->dimension;
        else
            n_fields += Total_Struct_Dimensionality(field->fields);
    }
    return n_fields;
}


//
//  Init_Struct_Fields: C
//
// a: make struct! [uint 8 i: 1]
// b: make a [i: 10]
//
void Init_Struct_Fields(REBVAL *ret, REBVAL *spec)
{
    REBVAL *blk = NULL;

    for (blk = KNOWN(VAL_ARRAY_AT(spec)); NOT_END(blk); blk += 2) {
        unsigned int i = 0;
        REBVAL *word = blk;
        REBVAL *fld_val = blk + 1;

        if (IS_BLOCK(word)) { // options: raw-memory, etc
            REBINT raw_size = -1;
            REBUPT raw_addr = 0;

            // make sure no other field initialization
            if (VAL_LEN_HEAD(spec) != 1)
                fail (Error_Invalid_Arg(spec));

            parse_attr(word, &raw_size, &raw_addr);
            ret->payload.structure.data
                 = make_ext_storage(VAL_STRUCT_SIZE(ret), raw_size, raw_addr);

            break;
        }
        else if (! IS_SET_WORD(word))
            fail (Error_Invalid_Arg(word));

        if (IS_END(fld_val))
            fail (Error(RE_NEED_VALUE, fld_val));

        REBSER *fieldlist = VAL_STRUCT_FIELDLIST(ret);

        for (i = 0; i < SER_LEN(fieldlist); i ++) {
            struct Struct_Field *fld
                = SER_AT(struct Struct_Field, fieldlist, i);

            if (STR_CANON(fld->name) == VAL_WORD_CANON(word)) {
                if (fld->dimension > 1) {
                    REBCNT n = 0;
                    if (IS_BLOCK(fld_val)) {
                        if (VAL_LEN_AT(fld_val) != fld->dimension)
                            fail (Error_Invalid_Arg(fld_val));

                        for(n = 0; n < fld->dimension; n ++) {
                            if (!assign_scalar(
                                VAL_STRUCT(ret),
                                fld,
                                n,
                                KNOWN(VAL_ARRAY_AT_HEAD(fld_val, n))
                            )) {
                                fail (Error_Invalid_Arg(fld_val));
                            }
                        }
                    }
                    else if (IS_INTEGER(fld_val)) {
                        void *ptr = cast(void *,
                            cast(REBUPT, VAL_INT64(fld_val))
                        );

                        // assuming valid pointer to enough space
                        memcpy(
                            SER_AT(
                                REBYTE,
                                VAL_STRUCT_DATA_BIN(ret),
                                fld->offset
                            ),
                            ptr,
                            fld->size * fld->dimension
                        );
                    }
                    else
                        fail (Error_Invalid_Arg(fld_val));
                }
                else {
                    if (!assign_scalar(VAL_STRUCT(ret), fld, 0, fld_val))
                        fail (Error_Invalid_Arg(fld_val));
                }
                break;
            }
        }

        if (i == SER_LEN(fieldlist))
            fail (Error_Invalid_Arg(word)); // field not in the parent struct
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
void MAKE_Struct(REBVAL *out, enum Reb_Kind type, const REBVAL *arg) {
    if (!IS_BLOCK(arg))
        fail (Error_Invalid_Arg(arg));

    REBINT max_fields = 16;

//
// SET UP SCHEMA
//
    // Every struct has a "schema"--this is a description (potentially
    // hierarchical) of its fields, including any nested structs.  The
    // schema should be shared between common instances of the same struct.
    //
    REBSER *field_1 = Make_Series(1, sizeof(struct Struct_Field), MKS_NONE);
    SET_SERIES_LEN(field_1, 1);

    struct Struct_Field *schema = SER_HEAD(struct Struct_Field, field_1);

    schema->spec = Copy_Array_Shallow(VAL_ARRAY(arg), VAL_SPECIFIER(arg));
    schema->type = FFI_TYPE_STRUCT;
    schema->is_array = FALSE;
    schema->is_rebval = FALSE;
    schema->name = NULL; // no symbol for the struct itself
    schema->offset = 999999; // shouldn't be used
    schema->fields = Make_Series(
        max_fields, sizeof(struct Struct_Field), MKS_NONE
    );

    // `schema->size =` ...don't know until after the fields are made


//
// PROCESS FIELDS
//

    u64 offset = 0; // offset in data

    REBINT raw_size = -1;
    REBUPT raw_addr = 0;

    RELVAL *item = VAL_ARRAY_AT(arg);
    if (IS_BLOCK(item)) {
        //
        // !!! This would suggest raw-size, raw-addr, or extern can be leading
        // in the struct definition, perhaps as:
        //
        //     make struct! [[raw-size] ...]
        //
        REBVAL specified;
        COPY_VALUE(&specified, item, VAL_SPECIFIER(arg));
        parse_attr(&specified, &raw_size, &raw_addr);
        ++item;
    }

    // !!! This makes binary data for each struct level? ???
    //
    REBSER *data_bin;
    if (raw_addr == 0)
        data_bin = Make_Binary(max_fields << 2);

    REBINT field_idx = 0; // for field index
    REBIXO eval_idx = 0; // for spec block evaluation
    REBCNT alignment = 0;

    while (NOT_END(item)) {

        // Add another field...

        EXPAND_SERIES_TAIL(schema->fields, 1);
        struct Struct_Field *field = SER_AT(
            struct Struct_Field,
            schema->fields,
            field_idx
        );
        field->offset = (REBCNT)offset;

        // Must be a word or a set-word, with set-words initializing

        REBOOL expect_init;
        if (IS_SET_WORD(item)) {
            expect_init = TRUE;
            if (raw_addr) {
                // initialization is not allowed for raw memory struct
                fail (Error_Invalid_Arg_Core(item, VAL_SPECIFIER(arg)));
            }
        }
        else if (IS_WORD(item))
            expect_init = FALSE;
        else
            fail (Error_Invalid_Type(VAL_TYPE(item)));

        field->name = VAL_WORD_CASED(item);

        ++item;
        if (IS_END(item) || !IS_BLOCK(item))
            fail (Error_Invalid_Arg_Core(item, VAL_SPECIFIER(arg)));

        REBVAL spec;
        COPY_VALUE(&spec, item, VAL_SPECIFIER(arg));

        REBVAL inner;
        SET_BLANK(&inner);
        PUSH_GUARD_VALUE(&inner);

        REBVAL *init = NULL; // for result to save in data

        Parse_Field_Type_May_Fail(field, &spec, &inner, &init);

        u64 step = 0;

        ++item;

        STATIC_assert(sizeof(field->size) <= 4);
        STATIC_assert(sizeof(field->dimension) <= 4);

        step = (u64)field->size * (u64)field->dimension;
        if (step > VAL_STRUCT_LIMIT)
            fail (Error(RE_SIZE_LIMIT, out));

        if (raw_addr == 0)
            EXPAND_SERIES_TAIL(data_bin, step);

        if (expect_init) {
            REBVAL safe; // result of reduce or do (GC saved during eval)

            init = &safe;

            if (IS_END(item))
               fail (Error_Invalid_Arg(arg));

            if (IS_BLOCK(item)) {
                if (Reduce_Array_Throws(
                    init,
                    VAL_ARRAY(item),
                    0,
                    IS_SPECIFIC(item)
                        ? VAL_SPECIFIER(KNOWN(item))
                        : VAL_SPECIFIER(arg),
                    FALSE
                )) {
                    fail (Error_No_Catch_For_Throw(init));
                }
                ++item;
            }
            else {
                eval_idx = DO_NEXT_MAY_THROW(
                    init,
                    VAL_ARRAY(arg),
                    item - VAL_ARRAY_AT(arg),
                    VAL_SPECIFIER(arg)
                );
                if (eval_idx == THROWN_FLAG)
                    fail (Error_No_Catch_For_Throw(init));

                if (eval_idx == END_FLAG)
                    item = VAL_ARRAY_TAIL(arg);
                else
                    item = VAL_ARRAY_AT_HEAD(item, eval_idx);
            }

            if (field->is_array) {
                if (IS_INTEGER(init)) { // interpreted as a C pointer
                    void *ptr = cast(void *, cast(REBUPT, VAL_INT64(init)));

                    // assume valid pointer to enough space
                    memcpy(
                        SER_AT(REBYTE, data_bin, cast(REBCNT, offset)),
                        ptr,
                        field->size * field->dimension
                    );
                }
                else if (IS_BLOCK(init)) {
                    REBCNT n = 0;

                    if (VAL_LEN_AT(init) != field->dimension)
                        fail (Error_Invalid_Arg(init));

                    // assign
                    for (n = 0; n < field->dimension; n ++) {
                        if (!assign_scalar_core(
                            data_bin,
                            offset,
                            field,
                            n,
                            KNOWN(VAL_ARRAY_AT_HEAD(init, n))
                        )) {
                            //RL_Print("Failed to assign element value\n");
                            fail (Error(RE_MISC));
                        }
                    }
                }
                else
                    fail (Error_Unexpected_Type(REB_BLOCK, VAL_TYPE(item)));
            }
            else {
                // scalar
                if (!assign_scalar_core(data_bin, offset, field, 0, init)) {
                    //RL_Print("Failed to assign scalar value\n");
                    fail (Error(RE_MISC));
                }
            }
        }
        else if (raw_addr == 0) {
            if (field->type == FFI_TYPE_STRUCT) {
                REBCNT n = 0;
                for (n = 0; n < field->dimension; n ++) {
                    memcpy(
                        SER_AT(
                            REBYTE,
                            data_bin,
                            cast(REBCNT, offset) + n * field->size
                        ),
                        BIN_HEAD(VAL_STRUCT_DATA_BIN(init)),
                        field->size
                    );
                }
            }
            else if (field->is_rebval) {
                assert(FALSE); // needs work

                REBCNT n = 0;
                for (n = 0; n < field->dimension; n ++) {
                    if (!assign_scalar(
                        VAL_STRUCT(out), field, n, VOID_CELL
                    )) {
                        //RL_Print("Failed to assign element value\n");
                        fail (Error(RE_MISC));
                    }
                }
            }
            else {
                memset(
                    SER_AT(REBYTE, data_bin, cast(REBCNT, offset)),
                    0,
                    field->size * field->dimension
                );
            }
        }

        offset +=  step;

        //if (alignment != 0) {
        //  offset = ((offset + alignment - 1) / alignment) * alignment;

        if (offset > VAL_STRUCT_LIMIT)
            fail (Error(RE_SIZE_LIMIT, out));

        field->done = 1; // TRUE, but bitfields must be integer

        ++ field_idx;

        DROP_GUARD_VALUE(&inner);
    }

    schema->size = offset; // now we know the total size so save it


//
// SET UP FOR FFI
//

    // The reason structs exist at all is so that they can be used with the
    // FFI, and the FFI requires you to set up a "ffi_type" C struct describing
    // each datatype.  There are stock types for the primitives, but each
    // structure needs its own.  We build the ffi_type at the same time as
    // the structure.
    //
    schema->fftype = Make_Series(1, sizeof(ffi_type), MKS_NONE);
    SET_SER_FLAG(schema->fftype, SERIES_FLAG_FIXED_SIZE);
    ffi_type *fftype = SER_HEAD(ffi_type, schema->fftype);

    fftype->size = 0;
    fftype->alignment = 0;
    fftype->type = FFI_TYPE_STRUCT;

    schema->fields_fftype_ptrs = Make_Series(
        Total_Struct_Dimensionality(schema->fields) + 1, // 1 for null at end
        sizeof(ffi_type*),
        MKS_NONE
    );
    SET_SER_FLAG(schema->fields_fftype_ptrs, SERIES_FLAG_FIXED_SIZE);
    fftype->elements = SER_HEAD(ffi_type*, schema->fields_fftype_ptrs);

    REBCNT j = 0;
    REBCNT i;
    for (i = 0; i < SER_LEN(schema->fields); ++i) {
        struct Struct_Field *field
            = SER_AT(struct Struct_Field, schema->fields, i);

        if (field->is_rebval) {
            //
            // "don't see a point to pass a rebol value to external functions"
            //
            // !!! ^-- What if the value is being passed through and will
            // come back via a callback?
            //
            fail (Error(RE_MISC));
        }
        else if (field->type == FFI_TYPE_STRUCT) {
            REBCNT n = 0;
            for (n = 0; n < field->dimension; ++n) {
                fftype->elements[j++] = SER_HEAD(ffi_type, field->fftype);
            }
        }
        else {
            REBSTR *sym; // dummy
            enum Reb_Kind kind; // dummy
            ffi_type* field_fftype
                = Get_FFType_Enum_Info(&sym, &kind, field->type);

            REBCNT n;
            for (n = 0; n < field->dimension; ++n) {
                fftype->elements[j++] = field_fftype;
            }
        }
    }
    fftype->elements[j] = NULL;


//
// FINALIZE VALUE
//

    MANAGE_SERIES(schema->fields_fftype_ptrs);
    MANAGE_SERIES(schema->fftype);
    MANAGE_ARRAY(schema->spec);
    MANAGE_SERIES(schema->fields);

    REBSTU *stu = Make_Singular_Array(BLANK_VALUE);
    MANAGE_SERIES(field_1);
    ARR_SERIES(stu)->link.schema = field_1;

    VAL_RESET_HEADER(out, REB_STRUCT);
    out->payload.structure.stu = stu;
    if (raw_addr) {
        out->payload.structure.data
            = make_ext_storage(schema->size, raw_size, raw_addr);
    }
    else {
        MANAGE_SERIES(data_bin);
        out->payload.structure.data = data_bin;
    }
    out->extra.struct_offset = 0;

    *ARR_HEAD(stu) = *out;
    assert(ARR_LEN(stu) == 1);
    MANAGE_ARRAY(stu);
}


//
//  TO_Struct: C
//
void TO_Struct(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    MAKE_Struct(out, kind, arg);
}


//
//  PD_Struct: C
//
REBINT PD_Struct(REBPVS *pvs)
{
    struct Struct_Field *field = NULL;
    REBCNT i = 0;
    REBSTU *stu = VAL_STRUCT(pvs->value);
    if (!IS_WORD(pvs->selector))
        fail (Error_Bad_Path_Select(pvs));

    fail_if_non_accessible(KNOWN(pvs->value));

    if (!pvs->opt_setval || NOT_END(pvs->item + 1)) {
        if (!Get_Struct_Var(stu, pvs->selector, pvs->store))
            fail (Error_Bad_Path_Select(pvs));

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
            pvs->opt_setval
            && IS_BLOCK(pvs->store)
            && IS_END(pvs->item + 2)
        ) {
            // !!! This is dodgy; it has to copy (as selector is a pointer to
            // a memory cell it may not own), has to guard (as the next path
            // evaluation may not protect the result...)
            //
            REBVAL sel_orig = *pvs->selector;
            PUSH_GUARD_VALUE(&sel_orig);

            pvs->value = pvs->store;
            pvs->value_specifier = SPECIFIED;

            if (Next_Path_Throws(pvs)) { // updates pvs->store, pvs->selector
                DROP_GUARD_VALUE(&sel_orig);
                fail (Error_No_Catch_For_Throw(pvs->store)); // !!! Review
            }

            {
                REBVAL specific;
                COPY_VALUE(&specific, pvs->value, pvs->value_specifier);

                if (!Set_Struct_Var(stu, &sel_orig, pvs->selector, &specific))
                    fail (Error_Bad_Path_Set(pvs));
            }

            DROP_GUARD_VALUE(&sel_orig);

            return PE_OK;
        }

        return PE_USE_STORE;
    }
    else {
        // setting (because opt_setval is non-NULL, and at end of path)

        if (!Set_Struct_Var(stu, pvs->selector, NULL, pvs->opt_setval))
            fail (Error_Bad_Path_Set(pvs));

        return PE_OK;
    }

    fail (Error_Bad_Path_Select(pvs));
}


//
//  Cmp_Struct: C
//
REBINT Cmp_Struct(const RELVAL *s, const RELVAL *t)
{
    REBINT n = VAL_STRUCT_FIELDLIST(s) - VAL_STRUCT_FIELDLIST(t);
    fail_if_non_accessible(const_KNOWN(s));
    fail_if_non_accessible(const_KNOWN(t));
    if (n != 0) {
        return n;
    }
    n = VAL_STRUCT(s) - VAL_STRUCT(t);
    return n;
}


//
//  CT_Struct: C
//
REBINT CT_Struct(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    //printf("comparing struct a (%p) with b (%p), mode: %d\n", a, b, mode);
    switch (mode) {
        case 1: // strict equality
            return 0 == Cmp_Struct(a, b);

        case 0: // coerced equality
            if (Cmp_Struct(a, b) == 0)
                return 1;

            return (
                IS_STRUCT(a) && IS_STRUCT(b)
                && same_fields(VAL_STRUCT_FIELDLIST(a), VAL_STRUCT_FIELDLIST(b))
                && VAL_STRUCT_SIZE(a) == VAL_STRUCT_SIZE(b)
                && !memcmp(
                    BIN_HEAD(VAL_STRUCT_DATA_BIN(a)),
                    BIN_HEAD(VAL_STRUCT_DATA_BIN(b)),
                    VAL_STRUCT_SIZE(a)
                )
            );

        default:
            return -1;
    }
    return -1;
}


//
//  Copy_Struct_Managed: C
//
REBSTU *Copy_Struct_Managed(REBSTU *src)
{
    fail_if_non_accessible(STU_VALUE(src));

    assert(ARR_LEN(src) == 1);
    assert(IS_STRUCT(ARR_AT(src, 0)));

    // This doesn't copy the data out of the array, or the schema...just the
    // value.  In fact, the schema is in the misc field and has to just be
    // linked manually.
    //
    REBSTU *copy = Copy_Array_Shallow(src, SPECIFIED);
    ARR_SERIES(copy)->link.schema = ARR_SERIES(src)->link.schema;

    // Update the binary data with a copy of its sequence.
    //
    // !!! Note that this leaves the offset intact, and will wind up making a
    // copy as big as struct the instance is embedded into if nonzero offset.

    REBSER *bin_copy = Copy_Sequence(STU_DATA_BIN(src));
    STU_VALUE(copy)->payload.structure.data = bin_copy;
    assert(STU_DATA_BIN(copy) == bin_copy);

    MANAGE_SERIES(bin_copy);
    MANAGE_ARRAY(copy);
    return copy;
}


//
//  REBTYPE: C
//
REBTYPE(Struct)
{
    REBVAL *val;
    REBVAL *arg;
    REBVAL *ret;

    val = D_ARG(1);

    ret = D_OUT;

    SET_VOID(ret);
    // unary actions
    switch(action) {
        case SYM_CHANGE:
            {
                arg = D_ARG(2);
                if (!IS_BINARY(arg))
                    fail (Error_Unexpected_Type(REB_BINARY, VAL_TYPE(arg)));

                if (VAL_LEN_AT(arg) != SER_LEN(VAL_STRUCT_DATA_BIN(val)))
                    fail (Error_Invalid_Arg(arg));

                memcpy(
                    BIN_HEAD(VAL_STRUCT_DATA_BIN(val)),
                    BIN_HEAD(VAL_SERIES(arg)),
                    BIN_LEN(VAL_STRUCT_DATA_BIN(val))
                );
            }
            break;

        case SYM_REFLECT:
            {
                arg = D_ARG(2);
                switch (VAL_WORD_SYM(arg)) {
                    case SYM_VALUES:
                        fail_if_non_accessible(val);
                        Val_Init_Binary(ret, Copy_Sequence_At_Len(VAL_STRUCT_DATA_BIN(val), VAL_STRUCT_OFFSET(val), VAL_STRUCT_SIZE(val)));
                        break;
                    case SYM_SPEC:
                        Val_Init_Block(
                            ret,
                            Copy_Array_Deep_Managed(
                                VAL_STRUCT_SPEC(val), SPECIFIED
                            )
                        );
                        Unbind_Values_Deep(VAL_ARRAY_HEAD(val));
                        break;
                    case SYM_ADDR:
                        SET_INTEGER(
                            ret,
                            cast(REBUPT, SER_AT(
                                REBYTE,
                                VAL_STRUCT_DATA_BIN(val),
                                VAL_STRUCT_OFFSET(val)
                            ))
                        );
                        break;
                    default:
                        fail (Error_Cannot_Reflect(REB_STRUCT, arg));
                }
            }
            break;

        case SYM_LENGTH:
            SET_INTEGER(ret, SER_LEN(VAL_STRUCT_DATA_BIN(val)));
            break;
        default:
            fail (Error_Illegal_Action(REB_STRUCT, action));
    }
    return R_OUT;
}


//
//  destroy-struct-storage: native [
//
//  {Destroy the external memory associated the struct}
//      s   [struct!]
//      /free func [function!] {Specify the function to free the memory}
//  ]
//
REBNATIVE(destroy_struct_storage)
{
    PARAM(1, val);
    REFINE(2, free_q);
    PARAM(3, free_func);

    if (REF(free_q)) {
        if (IS_FUNCTION_RIN(ARG(free_func)))
            fail (Error(RE_FREE_NEEDS_ROUTINE));
    }

    return Destroy_External_Storage(D_OUT,
                                    VAL_STRUCT_DATA_BIN(ARG(val)),
                                    REF(free_q)? ARG(free_func) : NULL);
}
