/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2014 Atronix Engineering, Inc.
**
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
**  Summary: Struct to C function
**  Module:  reb-struct.h
**  Author:  Shixin Zeng
**
***********************************************************************/

enum {
    STRUCT_TYPE_UINT8 = 0,
    STRUCT_TYPE_INT8,
    STRUCT_TYPE_UINT16,
    STRUCT_TYPE_INT16,
    STRUCT_TYPE_UINT32,
    STRUCT_TYPE_INT32,
    STRUCT_TYPE_UINT64,
    STRUCT_TYPE_INT64,
    STRUCT_TYPE_INTEGER,

    STRUCT_TYPE_FLOAT,
    STRUCT_TYPE_DOUBLE,
    STRUCT_TYPE_DECIMAL,

    STRUCT_TYPE_POINTER,
    STRUCT_TYPE_STRUCT,
    STRUCT_TYPE_REBVAL,
    STRUCT_TYPE_MAX
};

struct Struct_Field {
    REBARR* spec; /* for nested struct */
    REBSER* fields; /* for nested struct */
    REBSYM sym;

    REBINT type; /* rebol type */

    /* size is limited by struct->offset, so only 16-bit */
    REBCNT offset;
    REBCNT dimension; /* for arrays */
    REBCNT size; /* size of element, in bytes */

    /* Note: C89 bitfields may be 'int', 'unsigned int', or 'signed int' */
    unsigned int array:1;
    /* field is initialized? */
    /* (used by GC to decide if the value needs to be marked) */
    unsigned int done:1;
};

/* this is hackish to work around the size limit of REBSTU
 *  VAL_STRUCT_DATA(val) is not the actual data, but a series with
 *  one Struct_Data element, and this element has various infomation
 *  about the struct data
 * */
struct Struct_Data {
    REBSER *data;
    REBCNT offset;
    REBCNT len;
    REBFLGS flags;
};

#define STRUCT_DATA_BIN(v) (SER_HEAD(struct Struct_Data, (v)->data)->data)
#define STRUCT_OFFSET(v) (SER_HEAD(struct Struct_Data, (v)->data)->offset)
#define STRUCT_LEN(v) (SER_HEAD(struct Struct_Data, (v)->data)->len)
#define STRUCT_FLAGS(v) (SER_HEAD(struct Struct_Data, (v)->data)->flags)

#define VAL_STRUCT_DATA_BIN(v) STRUCT_DATA_BIN(&VAL_STRUCT(v))
#define VAL_STRUCT_OFFSET(v) STRUCT_OFFSET(&VAL_STRUCT(v))
#define VAL_STRUCT_LEN(v) STRUCT_LEN(&VAL_STRUCT(v))
#define VAL_STRUCT_FLAGS(v) STRUCT_FLAGS(&VAL_STRUCT(v))

#define VAL_STRUCT_LIMIT MAX_U32
