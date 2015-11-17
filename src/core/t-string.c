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
**  Module:  t-string.c
**  Summary: string related datatypes
**  Section: datatypes
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"
#include "sys-deci-funcs.h"
#include "sys-int-funcs.h"


//
//  CT_String: C
//
REBINT CT_String(REBVAL *a, REBVAL *b, REBINT mode)
{
	REBINT num;

	if (mode == 3)
		return VAL_SERIES(a) == VAL_SERIES(b) && VAL_INDEX(a) == VAL_INDEX(b);

	num = Compare_String_Vals(a, b, (REBOOL) !(mode > 1));
	if (mode >= 0) return (num == 0);
	if (mode == -1) return (num >= 0);
	return (num > 0);
}


/***********************************************************************
**
**	Local Utility Functions
**
***********************************************************************/

// !!! "STRING value to CHAR value (save some code space)" <-- what?
static void str_to_char(REBVAL *out, REBVAL *val, REBCNT idx)
{
	// Note: out may equal val, do assignment in two steps
	REBUNI codepoint = GET_ANY_CHAR(VAL_SERIES(val), idx);
	SET_CHAR(out, codepoint);
}

static void swap_chars(REBVAL *val1, REBVAL *val2)
{
	REBUNI c1;
	REBUNI c2;
	REBSER *s1 = VAL_SERIES(val1);
	REBSER *s2 = VAL_SERIES(val2);

	c1 = GET_ANY_CHAR(s1, VAL_INDEX(val1));
	c2 = GET_ANY_CHAR(s2, VAL_INDEX(val2));

	if (BYTE_SIZE(s1) && c2 > 0xff) Widen_String(s1, TRUE);
	SET_ANY_CHAR(s1, VAL_INDEX(val1), c2);

	if (BYTE_SIZE(s2) && c1 > 0xff) Widen_String(s2, TRUE);
	SET_ANY_CHAR(s2, VAL_INDEX(val2), c1);
}

static void reverse_string(REBVAL *value, REBCNT len)
{
	REBCNT n;
	REBCNT m;
	REBUNI c;

	if (VAL_BYTE_SIZE(value)) {
		REBYTE *bp = VAL_BIN_DATA(value);

		for (n = 0, m = len-1; n < len / 2; n++, m--) {
			c = bp[n];
			bp[n] = bp[m];
			bp[m] = (REBYTE)c;
		}
	}
	else {
		REBUNI *up = VAL_UNI_DATA(value);

		for (n = 0, m = len-1; n < len / 2; n++, m--) {
			c = up[n];
			up[n] = up[m];
			up[m] = c;
		}
	}
}

static REBCNT find_string(REBSER *series, REBCNT index, REBCNT end, REBVAL *target, REBCNT len, REBCNT flags, REBINT skip)
{
	REBCNT start = index;

	if (flags & (AM_FIND_REVERSE | AM_FIND_LAST)) {
		skip = -1;
		start = 0;
		if (flags & AM_FIND_LAST) index = end - len;
		else index--;
	}

	if (ANY_BINSTR(target)) {
		// Do the optimal search or the general search?
		if (BYTE_SIZE(series) && VAL_BYTE_SIZE(target) && !(flags & ~(AM_FIND_CASE|AM_FIND_MATCH)))
			return Find_Byte_Str(series, start, VAL_BIN_DATA(target), len, !GET_FLAG(flags, ARG_FIND_CASE-1), GET_FLAG(flags, ARG_FIND_MATCH-1));
		else
			return Find_Str_Str(series, start, index, end, skip, VAL_SERIES(target), VAL_INDEX(target), len, flags & (AM_FIND_MATCH|AM_FIND_CASE));
	}
	else if (IS_BINARY(target)) {
		return Find_Byte_Str(series, start, VAL_BIN_DATA(target), len, 0, GET_FLAG(flags, ARG_FIND_MATCH-1));
	}
	else if (IS_CHAR(target)) {
		return Find_Str_Char(series, start, index, end, skip, VAL_CHAR(target), flags);
	}
	else if (IS_INTEGER(target)) {
		return Find_Str_Char(series, start, index, end, skip, (REBUNI)VAL_INT32(target), flags);
	}
	else if (IS_BITSET(target)) {
		return Find_Str_Bitset(series, start, index, end, skip, VAL_SERIES(target), flags);
	}

	return NOT_FOUND;
}

static REBSER *make_string(REBVAL *arg, REBOOL make)
{
	REBSER *ser = 0;

	// MAKE <type> 123
	if (make && (IS_INTEGER(arg) || IS_DECIMAL(arg))) {
		ser = Make_Binary(Int32s(arg, 0));
	}
	// MAKE/TO <type> <binary!>
	else if (IS_BINARY(arg)) {
		REBYTE *bp = VAL_BIN_DATA(arg);
		REBCNT len = VAL_LEN(arg);
		switch (What_UTF(bp, len)) {
		case 0:
			break;
		case 8: // UTF-8 encoded
			bp  += 3;
			len -= 3;
			break;
		default:
			fail (Error(RE_BAD_DECODE));
		}
		ser = Decode_UTF_String(bp, len, 8); // UTF-8
	}
	// MAKE/TO <type> <any-string>
	else if (ANY_BINSTR(arg)) {
		ser = Copy_String(VAL_SERIES(arg), VAL_INDEX(arg), VAL_LEN(arg));
	}
	// MAKE/TO <type> <any-word>
	else if (ANY_WORD(arg)) {
		ser = Copy_Mold_Value(arg, TRUE);
		//ser = Append_UTF8(0, Get_Word_Name(arg), -1);
	}
	// MAKE/TO <type> #"A"
	else if (IS_CHAR(arg)) {
		ser = (VAL_CHAR(arg) > 0xff) ? Make_Unicode(2) : Make_Binary(2);
		Append_Codepoint_Raw(ser, VAL_CHAR(arg));
	}
	// MAKE/TO <type> <any-value>
//	else if (IS_NONE(arg)) {
//		ser = Make_Binary(0);
//	}
	else
		ser = Copy_Form_Value(arg, 1<<MOPT_TIGHT);

	return ser;
}

static REBSER *Make_Binary_BE64(REBVAL *arg)
{
	REBSER *ser = Make_Binary(9);
	REBI64 n = VAL_INT64(arg);
	REBINT count;
	REBYTE *bp = BIN_HEAD(ser);

	for (count = 7; count >= 0; count--) {
		bp[count] = (REBYTE)(n & 0xff);
		n >>= 8;
	}
	bp[8] = 0;
	ser->tail = 8;

	return ser;
}

static REBSER *make_binary(REBVAL *arg, REBOOL make)
{
	REBSER *ser;

	// MAKE BINARY! 123
	switch (VAL_TYPE(arg)) {
	case REB_INTEGER:
	case REB_DECIMAL:
		if (make) ser = Make_Binary(Int32s(arg, 0));
		else ser = Make_Binary_BE64(arg);
		break;

	// MAKE/TO BINARY! BINARY!
	case REB_BINARY:
		ser = Copy_Bytes(VAL_BIN_DATA(arg), VAL_LEN(arg));
		break;

	// MAKE/TO BINARY! <any-string>
	case REB_STRING:
	case REB_FILE:
	case REB_EMAIL:
	case REB_URL:
	case REB_TAG:
//	case REB_ISSUE:
		ser = Make_UTF8_From_Any_String(arg, VAL_LEN(arg), 0);
		break;

	case REB_BLOCK:
		// Join_Binary returns a shared buffer, so produce a copy:
		ser = Copy_Sequence(Join_Binary(arg, -1));
		break;

	// MAKE/TO BINARY! <tuple!>
	case REB_TUPLE:
		ser = Copy_Bytes(VAL_TUPLE(arg), VAL_TUPLE_LEN(arg));
		break;

	// MAKE/TO BINARY! <char!>
	case REB_CHAR:
		ser = Make_Binary(6);
		ser->tail = Encode_UTF8_Char(BIN_HEAD(ser), VAL_CHAR(arg));
		TERM_SEQUENCE(ser);
		break;

	// MAKE/TO BINARY! <bitset!>
	case REB_BITSET:
		ser = Copy_Bytes(VAL_BIN(arg), VAL_TAIL(arg));
		break;

	// MAKE/TO BINARY! <image!>
	case REB_IMAGE:
	  	ser = Make_Image_Binary(arg);
		break;

	case REB_MONEY:
		ser = Make_Binary(12);
		ser->tail = 12;
		deci_to_binary(ser->data, VAL_MONEY_AMOUNT(arg));
		ser->data[12] = 0;
		break;

	default:
		ser = 0;
	}

	return ser;
}

//
//  MT_String: C
//
REBFLG MT_String(REBVAL *out, REBVAL *data, enum Reb_Kind type)
{
	REBCNT i;

	if (!ANY_BINSTR(data)) return FALSE;
	*out = *data++;
	VAL_SET(out, type);
	i = IS_INTEGER(data) ? Int32(data) - 1 : 0;
	if (i > VAL_TAIL(out)) i = VAL_TAIL(out); // clip it
	VAL_INDEX(out) = i;
	return TRUE;
}


enum COMPARE_CHR_FLAGS {
	CC_FLAG_WIDE = 1 << 0, // String is REBUNI[] and not REBYTE[]
	CC_FLAG_CASE = 1 << 1, // Case sensitive sort
	CC_FLAG_REVERSE = 1 << 2 // Reverse sort order
};


//
//  Compare_Chr: C
// 
// This function is called by qsort_r, on behalf of the string sort
// function.  The `thunk` is an argument passed through from the caller
// and given to us by the sort routine, which tells us about the string
// and the kind of sort that was requested.
//
static int Compare_Chr(void *thunk, const void *v1, const void *v2)
{
	REBCNT * const flags = cast(REBCNT*, thunk);

	REBUNI c1;
	REBUNI c2;
	if (*flags & CC_FLAG_WIDE) {
		c1 = *cast(const REBUNI*, v1);
		c2 = *cast(const REBUNI*, v2);
	}
	else {
		c1 = cast(REBUNI, *cast(const REBYTE*, v1));
		c2 = cast(REBUNI, *cast(const REBYTE*, v2));
	}

	if (*flags & CC_FLAG_CASE) {
		if (*flags & CC_FLAG_REVERSE)
			return *cast(const REBYTE*, v2) - *cast(const REBYTE*, v1);
		else
			return *cast(const REBYTE*, v1) - *cast(const REBYTE*, v2);
	}
	else {
		if (*flags & CC_FLAG_REVERSE) {
			if (c1 < UNICODE_CASES)
				c1 = UP_CASE(c1);
			if (c2 < UNICODE_CASES)
				c2 = UP_CASE(c2);
			return c2 - c1;
		}
		else {
			if (c1 < UNICODE_CASES)
				c1 = UP_CASE(c1);
			if (c2 < UNICODE_CASES)
				c2 = UP_CASE(c2);
			return c1 - c2;
		}
	}
}


//
//  Sort_String: C
//
static void Sort_String(REBVAL *string, REBFLG ccase, REBVAL *skipv, REBVAL *compv, REBVAL *part, REBFLG all, REBFLG rev)
{
	REBCNT len;
	REBCNT skip = 1;
	REBCNT size = 1;
	REBCNT thunk = 0;
	int (*sfunc)(const void *v1, const void *v2);

	// Determine length of sort:
	len = Partial(string, 0, part, 0);
	if (len <= 1) return;

	// Skip factor:
	if (!IS_UNSET(skipv)) {
		skip = Get_Num_Arg(skipv);
		if (skip <= 0 || len % skip != 0 || skip > len)
			fail (Error_Invalid_Arg(skipv));
	}

	// Use fast quicksort library function:
	if (skip > 1) len /= skip, size *= skip;

	if (!VAL_BYTE_SIZE(string)) thunk |= CC_FLAG_WIDE;
	if (ccase) thunk |= CC_FLAG_CASE;
	if (rev) thunk |= CC_FLAG_REVERSE;

	reb_qsort_r(
		VAL_DATA(string),
		len,
		size * SERIES_WIDE(VAL_SERIES(string)),
		&thunk,
		Compare_Chr
	);
}


//
//  PD_String: C
//
REBINT PD_String(REBPVS *pvs)
{
	REBVAL *data = pvs->value;
	REBVAL *val = pvs->setval;
	REBINT n = 0;
	REBCNT i;
	REBINT c;
	REBSER *ser = VAL_SERIES(data);

	if (IS_INTEGER(pvs->select)) {
		n = Int32(pvs->select) + VAL_INDEX(data) - 1;
	}
	else return PE_BAD_SELECT;

	if (val == 0) {
		if (n < 0 || (REBCNT)n >= SERIES_TAIL(ser)) return PE_NONE;
		if (IS_BINARY(data)) {
			SET_INTEGER(pvs->store, *BIN_SKIP(ser, n));
		} else {
			SET_CHAR(pvs->store, GET_ANY_CHAR(ser, n));
		}
		return PE_USE;
	}

	if (n < 0 || (REBCNT)n >= SERIES_TAIL(ser)) return PE_BAD_RANGE;

	if (IS_CHAR(val)) {
		c = VAL_CHAR(val);
		if (c > MAX_CHAR) return PE_BAD_SET;
	}
	else if (IS_INTEGER(val)) {
		c = Int32(val);
		if (c > MAX_CHAR || c < 0) return PE_BAD_SET;
		if (IS_BINARY(data)) { // special case for binary
			if (c > 0xff) fail (Error_Out_Of_Range(val));
			BIN_HEAD(ser)[n] = (REBYTE)c;
			return PE_OK;
		}
	}
	else if (ANY_BINSTR(val)) {
		i = VAL_INDEX(val);
		if (i >= VAL_TAIL(val)) return PE_BAD_SET;
		c = GET_ANY_CHAR(VAL_SERIES(val), i);
	}
	else
		return PE_BAD_SELECT;

	TRAP_PROTECT(ser);

	if (BYTE_SIZE(ser) && c > 0xff) Widen_String(ser, TRUE);
	SET_ANY_CHAR(ser, n, c);

	return PE_OK;
}


//
//  PD_File: C
//
REBINT PD_File(REBPVS *pvs)
{
	REBSER *ser;
	REBCNT n;
	REBUNI c;
	REBSER *arg;

	if (pvs->setval) return PE_BAD_SET;

	ser = Copy_Sequence_At_Position(pvs->value);

	n = SERIES_TAIL(ser);
	if (n > 0) c = GET_ANY_CHAR(ser, n-1);
	if (n == 0 || c != '/') Append_Codepoint_Raw(ser, '/');

	if (ANY_STR(pvs->select))
		arg = VAL_SERIES(pvs->select);
	else {
		REB_MOLD mo;
		CLEARS(&mo);
		Reset_Mold(&mo);
		Mold_Value(&mo, pvs->select, 0);
		arg = mo.series;
	}

	c = GET_ANY_CHAR(arg, 0);
	n = (c == '/' || c == '\\') ? 1 : 0;
	Append_String(ser, arg, n, arg->tail-n);

	Val_Init_Series(pvs->store, VAL_TYPE(pvs->value), ser);

	return PE_USE;
}


//
//  REBTYPE: C
//
REBTYPE(String)
{
	REBVAL	*value = D_ARG(1);
	REBVAL  *arg = DS_ARGC > 1 ? D_ARG(2) : NULL;
	REBINT	index;
	REBINT	tail;
	REBINT	len;
	REBSER  *ser;
	enum Reb_Kind type;
	REBCNT	args;
	REBCNT	ret;

	if ((IS_FILE(value) || IS_URL(value)) && action >= PORT_ACTIONS) {
		return T_Port(call_, action);
	}

	len = Do_Series_Action(call_, action, value, arg);
	if (len >= 0) return len;

	// Common setup code for all actions:
	if (action != A_MAKE && action != A_TO) {
		index = (REBINT)VAL_INDEX(value);
		tail  = (REBINT)VAL_TAIL(value);
	}

	// Check must be in this order (to avoid checking a non-series value);
	if (action >= A_TAKE && action <= A_SORT && IS_PROTECT_SERIES(VAL_SERIES(value)))
		fail (Error(RE_PROTECTED));

	switch (action) {

	//-- Modification:
	case A_APPEND:
	case A_INSERT:
	case A_CHANGE:
		//Modify_String(action, value, arg);
		// Length of target (may modify index): (arg can be anything)
		len = Partial1((action == A_CHANGE) ? value : arg, D_ARG(AN_LIMIT));
		index = VAL_INDEX(value);
		args = 0;
		if (IS_BINARY(value)) SET_FLAG(args, AN_SERIES); // special purpose
		if (D_REF(AN_PART)) SET_FLAG(args, AN_PART);
		index = Modify_String(action, VAL_SERIES(value), index, arg, args, len, D_REF(AN_DUP) ? Int32(D_ARG(AN_COUNT)) : 1);
		ENSURE_SERIES_MANAGED(VAL_SERIES(value));
		VAL_INDEX(value) = index;
		break;

	//-- Search:
	case A_SELECT:
		ret = ALL_SELECT_REFS;
		goto find;
	case A_FIND:
		ret = ALL_FIND_REFS;
find:
		args = Find_Refines(call_, ret);

		if (IS_BINARY(value)) {
			args |= AM_FIND_CASE;

			if (!IS_BINARY(arg) && !IS_INTEGER(arg) && !IS_BITSET(arg))
				fail (Error(RE_NOT_SAME_TYPE));

			if (IS_INTEGER(arg)) {
				if (VAL_INT64(arg) < 0 || VAL_INT64(arg) > 255)
					fail (Error_Out_Of_Range(arg));
				len = 1;
			}
		}
		else {
			if (IS_CHAR(arg) || IS_BITSET(arg)) len = 1;
			else if (!ANY_STR(arg)) {
				Val_Init_String(arg, Copy_Form_Value(arg, 0));
			}
		}

		if (ANY_BINSTR(arg)) len = VAL_LEN(arg);

		if (args & AM_FIND_PART) tail = Partial(value, 0, D_ARG(ARG_FIND_LIMIT), 0);
		ret = 1; // skip size
		if (args & AM_FIND_SKIP) ret = Partial(value, 0, D_ARG(ARG_FIND_SIZE), 0);

		ret = find_string(VAL_SERIES(value), index, tail, arg, len, args, ret);

		if (ret >= (REBCNT)tail) goto is_none;
		if (args & AM_FIND_ONLY) len = 1;

		if (action == A_FIND) {
			if (args & (AM_FIND_TAIL | AM_FIND_MATCH)) ret += len;
			VAL_INDEX(value) = ret;
		}
		else {
			ret++;
			if (ret >= (REBCNT)tail) goto is_none;
			if (IS_BINARY(value)) {
				SET_INTEGER(value, *BIN_SKIP(VAL_SERIES(value), ret));
			}
			else
				str_to_char(value, value, ret);
		}
		break;

	//-- Picking:
	case A_PICK:
	case A_POKE:
		len = Get_Num_Arg(arg); // Position
		//if (len > 0) index--;
		if (REB_I32_SUB_OF(len, 1, &len)
			|| REB_I32_ADD_OF(index, len, &index)
			|| index < 0 || index >= tail) {
			if (action == A_PICK) goto is_none;
			fail (Error_Out_Of_Range(arg));
		}
		if (action == A_PICK) {
pick_it:
			if (IS_BINARY(value)) {
				SET_INTEGER(D_OUT, *VAL_BIN_SKIP(value, index));
			}
			else
				str_to_char(D_OUT, value, index);
			return R_OUT;
		}
		else {
			REBUNI c;
			arg = D_ARG(3);
			if (IS_CHAR(arg))
				c = VAL_CHAR(arg);
			else if (IS_INTEGER(arg) && VAL_UNT64(arg) <= MAX_CHAR)
				c = VAL_INT32(arg);
			else
				fail (Error_Invalid_Arg(arg));

			ser = VAL_SERIES(value);
			if (IS_BINARY(value)) {
				if (c > 0xff) fail (Error_Out_Of_Range(arg));
				BIN_HEAD(ser)[index] = (REBYTE)c;
			}
			else {
				if (BYTE_SIZE(ser) && c > 0xff) Widen_String(ser, TRUE);
				SET_ANY_CHAR(ser, index, c);
			}
			value = arg;
		}
		break;

	case A_TAKE:
		if (D_REF(2)) {
			len = Partial(value, 0, D_ARG(3), 0);
			if (len == 0) {
zero_str:
				Val_Init_Series(D_OUT, VAL_TYPE(value), Make_Binary(0));
				return R_OUT;
			}
		} else
			len = 1;

		index = VAL_INDEX(value); // /part can change index

		// take/last:
		if (D_REF(5)) index = tail - len;
		if (index < 0 || index >= tail) {
			if (!D_REF(2)) goto is_none;
			goto zero_str;
		}

		ser = VAL_SERIES(value);
		// if no /part, just return value, else return string:
		if (!D_REF(2)) {
			if (IS_BINARY(value)) {
				SET_INTEGER(value, *VAL_BIN_SKIP(value, index));
			} else
				str_to_char(value, value, index);
		}
		else Val_Init_Series(value, VAL_TYPE(value), Copy_String(ser, index, len));
		Remove_Series(ser, index, len);
		break;

	case A_CLEAR:
		if (index < tail) {
			if (index == 0) Reset_Series(VAL_SERIES(value));
			else {
				VAL_TAIL(value) = (REBCNT)index;
				TERM_SEQUENCE(VAL_SERIES(value));
			}
		}
		break;

	//-- Creation:

	case A_COPY:
		len = Partial(value, 0, D_ARG(3), 0); // Can modify value index.
		ser = Copy_String(VAL_SERIES(value), VAL_INDEX(value), len);
		goto ser_exit;

	case A_MAKE:
	case A_TO:
		// Determine the datatype to create:
		type = VAL_TYPE(value);
		if (type == REB_DATATYPE) type = VAL_TYPE_KIND(value);

		if (IS_NONE(arg)) fail (Error_Bad_Make(type, arg));

		ser = (type != REB_BINARY)
			? make_string(arg, (REBOOL)(action == A_MAKE))
			: make_binary(arg, (REBOOL)(action == A_MAKE));

		if (ser) goto str_exit;
		fail (Error_Invalid_Arg(arg));

	//-- Bitwise:

	case A_AND:
	case A_OR:
	case A_XOR:
		if (!IS_BINARY(arg)) fail (Error_Invalid_Arg(arg));
		VAL_LIMIT_SERIES(value);
		VAL_LIMIT_SERIES(arg);
		ser = Xandor_Binary(action, value, arg);
		goto ser_exit;

	case A_COMPLEMENT:
		if (!IS_BINARY(value)) fail (Error_Invalid_Arg(value));
		ser = Complement_Binary(value);
		goto ser_exit;

	//-- Special actions:

	case A_TRIM:
		// Check for valid arg combinations:
		args = Find_Refines(call_, ALL_TRIM_REFS);
		if (
			((args & (AM_TRIM_ALL | AM_TRIM_WITH)) &&
			(args & (AM_TRIM_HEAD | AM_TRIM_TAIL | AM_TRIM_LINES | AM_TRIM_AUTO))) ||
			((args & AM_TRIM_AUTO) &&
			(args & (AM_TRIM_HEAD | AM_TRIM_TAIL | AM_TRIM_LINES | AM_TRIM_ALL | AM_TRIM_WITH)))
		) {
			fail (Error(RE_BAD_REFINES));
		}

		Trim_String(VAL_SERIES(value), VAL_INDEX(value), VAL_LEN(value), args, D_ARG(ARG_TRIM_STR));
		break;

	case A_SWAP:
		if (VAL_TYPE(value) != VAL_TYPE(arg))
			fail (Error(RE_NOT_SAME_TYPE));

		if (IS_PROTECT_SERIES(VAL_SERIES(arg)))
			fail (Error(RE_PROTECTED));

		if (index < tail && VAL_INDEX(arg) < VAL_TAIL(arg))
			swap_chars(value, arg);
		break;

	case A_REVERSE:
		len = Partial(value, 0, D_ARG(3), 0);
		if (len > 0) reverse_string(value, len);
		break;

	case A_SORT:
		Sort_String(
			value,
			D_REF(2),	// case sensitive
			D_ARG(4),	// skip size
			D_ARG(6),	// comparator
			D_ARG(8),	// part-length
			D_REF(9),	// all fields
			D_REF(10)	// reverse
		);
		break;

	case A_RANDOM:
		if (D_REF(2)) { // seed
			Set_Random(Compute_CRC(VAL_BIN_DATA(value), VAL_LEN(value)));
			return R_UNSET;
		}
		if (D_REF(4)) { // /only
			if (index >= tail) goto is_none;
			index += (REBCNT)Random_Int(D_REF(3)) % (tail - index);  // /secure
			goto pick_it;
		}
		Shuffle_String(value, D_REF(3));  // /secure
		break;

	default:
		fail (Error_Illegal_Action(VAL_TYPE(value), action));
	}

	*D_OUT = *value;
	return R_OUT;

ser_exit:
	type = VAL_TYPE(value);
str_exit:
	Val_Init_Series(D_OUT, type, ser);
	return R_OUT;

is_none:
	return R_NONE;
}
