/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
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
**  Module:  t-typeset.c
**  Summary: typeset datatype
**  Section: datatypes
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


//
// symbol-to-typeset-bits mapping table
//
// NOTE: Order of symbols is important, because this is used to build a
// list of typeset word symbols ordered relative to their symbol #,
// which lays out the legal unbound WORD! values you can use during
// a MAKE TYPESET! (bound words will be looked up as variables to see
// if they contain a DATATYPE! or a typeset, but general reduction is
// not performed on the block passed in.)
//
// !!! Is it necessary for MAKE TYPESET! to allow unbound words at all,
// or should the typesets be required to be in bound variables?  Should
// clients be asked to pass in only datatypes and typesets, hence doing
// their own reduce before trying to make a typeset out of a block?
//
const struct {
    REBSYM sym;
    REBU64 bits;
} Typesets[] = {
    {SYM_ANY_NOTHING_X, TS_NOTHING},
    {SYM_ANY_SOMETHING_X, TS_SOMETHING},
    {SYM_ANY_VALUE_X, TS_VALUE},
    {SYM_OPT_ANY_VALUE_X, TS_VALUE | FLAGIT_KIND(REB_UNSET)},
    {SYM_ANY_WORD_X, TS_WORD},
    {SYM_ANY_PATH_X, TS_PATH},
    {SYM_ANY_FUNCTION_X, TS_FUNCTION},
    {SYM_ANY_NUMBER_X, TS_NUMBER},
    {SYM_ANY_SCALAR_X, TS_SCALAR},
    {SYM_ANY_SERIES_X, TS_SERIES},
    {SYM_ANY_STRING_X, TS_STRING},
    {SYM_ANY_CONTEXT_X, TS_CONTEXT},
    {SYM_ANY_ARRAY_X, TS_ARRAY},

    {SYM_0, 0}
};


//
//  CT_Typeset: C
//
REBINT CT_Typeset(const REBVAL *a, const REBVAL *b, REBINT mode)
{
    if (mode < 0) return -1;
    return EQUAL_TYPESET(a, b);
}


//
//  Init_Typesets: C
// 
// Create typeset variables that are defined above.
// For example: NUMBER is both integer and decimal.
// Add the new variables to the system context.
//
void Init_Typesets(void)
{
    REBVAL *value;
    REBINT n;

    Set_Root_Series(ROOT_TYPESETS, ARR_SERIES(Make_Array(40)));

    for (n = 0; Typesets[n].sym != SYM_0; n++) {
        value = Alloc_Tail_Array(VAL_ARRAY(ROOT_TYPESETS));

        // Note: the symbol in the typeset is not the symbol of a word holding
        // the typesets, rather an extra data field used when the typeset is
        // in a context key slot to identify that field's name
        //
        Val_Init_Typeset(value, Typesets[n].bits, SYM_0);

        *Append_Context(Lib_Context, NULL, Typesets[n].sym) = *value;
    }
}


//
//  Val_Init_Typeset: C
// 
// Note: sym is optional, and can be SYM_0
//
void Val_Init_Typeset(REBVAL *value, REBU64 bits, REBSYM sym)
{
    VAL_RESET_HEADER(value, REB_TYPESET);
    VAL_TYPESET_SYM(value) = sym;
    VAL_TYPESET_BITS(value) = bits;
}


//
//  VAL_TYPESET_SYM_Ptr_Debug: C
// 
// !!! Needed temporarily due to reorganization (though it should
// be checked via C++ build's static typing eventually...)
//
REBCNT *VAL_TYPESET_SYM_Ptr_Debug(const REBVAL *typeset)
{
    assert(IS_TYPESET(typeset));
    // loses constness, but that's not the particular concern needed
    // to be caught in the wake of the UNWORD => TYPESET change...
    return (REBCNT*)(&typeset->payload.typeset.sym);
}


//
//  Make_Typeset: C
// 
// block - block of datatypes (datatype words ok too)
// value - value to hold result (can be word-spec type too)
//
REBOOL Make_Typeset(REBVAL *block, REBVAL *value, REBOOL load)
{
    const REBVAL *val;
    REBARR *types = VAL_ARRAY(ROOT_TYPESETS);

    VAL_TYPESET_BITS(value) = 0;

    for (; NOT_END(block); block++) {
        val = NULL;
        if (IS_WORD(block) && !(val = TRY_GET_OPT_VAR(block))) {
            //Print("word: %s", Get_Word_Name(block));
            REBSYM sym = VAL_WORD_SYM(block);

            if (IS_KIND_SYM(sym)) { // Accept datatype word
                TYPE_SET(value, KIND_FROM_SYM(sym));
                continue;
            } // Special typeset symbols:
            else if (sym >= SYM_ANY_NOTHING_X && sym < SYM_DATATYPES)
                val = ARR_AT(types, sym - SYM_ANY_NOTHING_X);
        }
        if (!val) val = block;
        if (IS_DATATYPE(val)) {
            TYPE_SET(value, VAL_TYPE_KIND(val));
        } else if (IS_TYPESET(val)) {
            VAL_TYPESET_BITS(value) |= VAL_TYPESET_BITS(val);
        } else {
            if (load) return FALSE;
            fail (Error_Invalid_Arg(block));
        }
    }

    return TRUE;
}


//
//  MT_Typeset: C
//
REBOOL MT_Typeset(REBVAL *out, REBVAL *data, enum Reb_Kind type)
{
    if (!IS_BLOCK(data)) return FALSE;

    if (!Make_Typeset(VAL_ARRAY_HEAD(data), out, TRUE)) return FALSE;
    VAL_RESET_HEADER(out, REB_TYPESET);

    return TRUE;
}


//
//  Find_Typeset: C
//
REBINT Find_Typeset(REBVAL *block)
{
    REBVAL *val;
    REBINT n;

    REBVAL value;
    VAL_INIT_WRITABLE_DEBUG(&value);
    VAL_RESET_HEADER(&value, REB_TYPESET);
    Make_Typeset(block, &value, FALSE);

    val = VAL_ARRAY_AT_HEAD(ROOT_TYPESETS, 1);

    for (n = 1; NOT_END(val); val++, n++) {
        if (EQUAL_TYPESET(&value, val)){
            //Print("FTS: %d", n);
            return n;
        }
    }

//  Print("Size Typesets: %d", VAL_LEN_HEAD(ROOT_TYPESETS));
    Append_Value(VAL_ARRAY(ROOT_TYPESETS), &value);
    return n;
}


//
//  Typeset_To_Array: C
// 
// Converts typeset value to a block of datatypes.
// No order is specified.
//
REBARR *Typeset_To_Array(REBVAL *tset)
{
    REBARR *block;
    REBVAL *value;
    REBINT n;
    REBINT size = 0;

    for (n = 0; n < REB_MAX_0; n++) {
        if (TYPE_CHECK(tset, KIND_FROM_0(n))) size++;
    }

    block = Make_Array(size);

    // Convert bits to types:
    for (n = 0; n < REB_MAX_0; n++) {
        if (TYPE_CHECK(tset, KIND_FROM_0(n))) {
            value = Alloc_Tail_Array(block);
            Val_Init_Datatype(value, KIND_FROM_0(n));
        }
    }
    return block;
}


//
//  REBTYPE: C
//
REBTYPE(Typeset)
{
    REBVAL *val = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    switch (action) {

    case A_FIND:
        if (IS_DATATYPE(arg)) {
            DECIDE(TYPE_CHECK(val, VAL_TYPE_KIND(arg)));
        }
        fail (Error_Invalid_Arg(arg));

    case A_MAKE:
    case A_TO:
        if (IS_BLOCK(arg)) {
            VAL_RESET_HEADER(D_OUT, REB_TYPESET);
            Make_Typeset(VAL_ARRAY_AT(arg), D_OUT, FALSE);
            return R_OUT;
        }
    //  if (IS_NONE(arg)) {
    //      VAL_RESET_HEADER(arg, REB_TYPESET);
    //      VAL_TYPESET_BITS(arg) = 0L;
    //      *D_OUT = *D_ARG(2);
    //      return R_OUT;
    //  }
        if (IS_TYPESET(arg)) {
            *D_OUT = *D_ARG(2);
            return R_OUT;
        }
        fail (Error_Bad_Make(REB_TYPESET, arg));

    case A_AND_T:
    case A_OR_T:
    case A_XOR_T:
        if (IS_DATATYPE(arg)) {
            VAL_TYPESET_BITS(arg) = FLAGIT_KIND(VAL_TYPE(arg));
        }
        else if (!IS_TYPESET(arg))
            fail (Error_Invalid_Arg(arg));

        if (action == A_OR_T)
            VAL_TYPESET_BITS(val) |= VAL_TYPESET_BITS(arg);
        else if (action == A_AND_T)
            VAL_TYPESET_BITS(val) &= VAL_TYPESET_BITS(arg);
        else
            VAL_TYPESET_BITS(val) ^= VAL_TYPESET_BITS(arg);
        *D_OUT = *D_ARG(1);
        return R_OUT;

    case A_COMPLEMENT:
        VAL_TYPESET_BITS(val) = ~VAL_TYPESET_BITS(val);
        *D_OUT = *D_ARG(1);
        return R_OUT;

    default:
        fail (Error_Illegal_Action(REB_TYPESET, action));
    }

is_true:
    return R_TRUE;

is_false:
    return R_FALSE;
}
