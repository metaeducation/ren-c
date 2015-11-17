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
	REBCNT sym;
	REBU64 bits;
} Typesets[] = {
	{SYM_ANY_VALUEX, (cast(REBU64, 1) << REB_MAX) - 2}, // do not include END!
	{SYM_ANY_WORDX, TS_WORD},
	{SYM_ANY_PATHX, TS_PATH},
	{SYM_ANY_FUNCTIONX, TS_FUNCTION},
	{SYM_ANY_NUMBERX, TS_NUMBER},
	{SYM_ANY_SCALARX, TS_SCALAR},
	{SYM_ANY_SERIESX, TS_SERIES},
	{SYM_ANY_STRINGX, TS_STRING},
	{SYM_ANY_OBJECTX, TS_OBJECT},
	{SYM_ANY_ARRAYX, TS_ARRAY},

	{SYM_0, 0}
};


//
//  CT_Typeset: C
//
REBINT CT_Typeset(REBVAL *a, REBVAL *b, REBINT mode)
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

	Set_Root_Series(ROOT_TYPESETS, Make_Array(40), "typeset presets");

	for (n = 0; Typesets[n].sym != SYM_0; n++) {
		value = Alloc_Tail_Array(VAL_SERIES(ROOT_TYPESETS));
		VAL_SET(value, REB_TYPESET);
		VAL_TYPESET_BITS(value) = Typesets[n].bits;

		*Append_Frame(Lib_Context, NULL, Typesets[n].sym) = *value;
	}
}


//
//  Val_Init_Typeset: C
// 
// Note: sym is optional, and can be SYM_0
//
void Val_Init_Typeset(REBVAL *value, REBU64 bits, REBCNT sym)
{
	VAL_SET(value, REB_TYPESET);
	VAL_TYPESET_SYM(value) = sym;
	VAL_TYPESET_BITS(value) = bits;
}


//
//  Val_Typeset_Sym_Ptr_Debug: C
// 
// !!! Needed temporarily due to reorganization (though it should
// be checked via C++ build's static typing eventually...)
//
REBCNT *Val_Typeset_Sym_Ptr_Debug(const REBVAL *typeset)
{
	assert(IS_TYPESET(typeset));
	// loses constness, but that's not the particular concern needed
	// to be caught in the wake of the UNWORD => TYPESET change...
	return cast(REBCNT*, &typeset->data.typeset.sym);
}


//
//  Make_Typeset: C
// 
// block - block of datatypes (datatype words ok too)
// value - value to hold result (can be word-spec type too)
//
REBFLG Make_Typeset(REBVAL *block, REBVAL *value, REBFLG load)
{
	const REBVAL *val;
	REBCNT sym;
	REBSER *types = VAL_SERIES(ROOT_TYPESETS);

	VAL_TYPESET_BITS(value) = 0;

	for (; NOT_END(block); block++) {
		val = NULL;
		if (IS_WORD(block)) {
			//Print("word: %s", Get_Word_Name(block));
			sym = VAL_WORD_SYM(block);
			if (VAL_WORD_FRAME(block)) { // Get word value
				val = GET_VAR(block);
			} else if (IS_KIND_SYM(sym)) { // Accept datatype word
				TYPE_SET(value, KIND_FROM_SYM(sym));
				continue;
			} // Special typeset symbols:
			else if (sym >= SYM_ANY_VALUEX && sym < SYM_DATATYPES)
				val = BLK_SKIP(types, sym - SYM_ANY_VALUEX);
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
REBFLG MT_Typeset(REBVAL *out, REBVAL *data, enum Reb_Kind type)
{
	if (!IS_BLOCK(data)) return FALSE;

	if (!Make_Typeset(VAL_BLK_HEAD(data), out, TRUE)) return FALSE;
	VAL_SET(out, REB_TYPESET);

	return TRUE;
}


//
//  Find_Typeset: C
//
REBINT Find_Typeset(REBVAL *block)
{
	REBVAL value;
	REBVAL *val;
	REBINT n;

	VAL_SET(&value, REB_TYPESET);
	Make_Typeset(block, &value, 0);

	val = VAL_BLK_SKIP(ROOT_TYPESETS, 1);

	for (n = 1; NOT_END(val); val++, n++) {
		if (EQUAL_TYPESET(&value, val)){
			//Print("FTS: %d", n);
			return n;
		}
	}

//	Print("Size Typesets: %d", VAL_TAIL(ROOT_TYPESETS));
	Append_Value(VAL_SERIES(ROOT_TYPESETS), &value);
	return n;
}


//
//  Typeset_To_Block: C
// 
// Converts typeset value to a block of datatypes.
// No order is specified.
//
REBSER *Typeset_To_Block(REBVAL *tset)
{
	REBSER *block;
	REBVAL *value;
	REBINT n;
	REBINT size = 0;

	for (n = 0; n < REB_MAX; n++) {
		if (TYPE_CHECK(tset, n)) size++;
	}

	block = Make_Array(size);

	// Convert bits to types:
	for (n = 0; n < REB_MAX; n++) {
		if (TYPE_CHECK(tset, n)) {
			value = Alloc_Tail_Array(block);
			Val_Init_Datatype(value, n);
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
	REBVAL *arg = DS_ARGC > 1 ? D_ARG(2) : NULL;

	switch (action) {

	case A_FIND:
		if (IS_DATATYPE(arg)) {
			DECIDE(TYPE_CHECK(val, VAL_TYPE_KIND(arg)));
		}
		fail (Error_Invalid_Arg(arg));

	case A_MAKE:
	case A_TO:
		if (IS_BLOCK(arg)) {
			VAL_SET(D_OUT, REB_TYPESET);
			Make_Typeset(VAL_BLK_DATA(arg), D_OUT, 0);
			return R_OUT;
		}
	//	if (IS_NONE(arg)) {
	//		VAL_SET(arg, REB_TYPESET);
	//		VAL_TYPESET_BITS(arg) = 0L;
	//		return R_ARG2;
	//	}
		if (IS_TYPESET(arg)) return R_ARG2;
		fail (Error_Bad_Make(REB_TYPESET, arg));

	case A_AND:
	case A_OR:
	case A_XOR:
		if (IS_DATATYPE(arg)) VAL_TYPESET_BITS(arg) = FLAGIT_64(VAL_TYPE_KIND(arg));
		else if (!IS_TYPESET(arg)) fail (Error_Invalid_Arg(arg));

		if (action == A_OR) VAL_TYPESET_BITS(val) |= VAL_TYPESET_BITS(arg);
		else if (action == A_AND) VAL_TYPESET_BITS(val) &= VAL_TYPESET_BITS(arg);
		else VAL_TYPESET_BITS(val) ^= VAL_TYPESET_BITS(arg);
		return R_ARG1;

	case A_COMPLEMENT:
		VAL_TYPESET_BITS(val) = ~VAL_TYPESET_BITS(val);
		return R_ARG1;

	default:
		fail (Error_Illegal_Action(REB_TYPESET, action));
	}

is_true:
	return R_TRUE;

is_false:
	return R_FALSE;
}
