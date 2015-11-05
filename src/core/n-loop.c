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
**  Module:  n-loop.c
**  Summary: native functions for loops
**  Section: natives
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"
#include "sys-int-funcs.h" //REB_I64_ADD_OF

typedef enum {
	LOOP_FOR_EACH,
	LOOP_REMOVE_EACH,
	LOOP_MAP_EACH,
	LOOP_EVERY
} LOOP_MODE;


/***********************************************************************
**
*/	REBFLG Catching_Break_Or_Continue(REBVAL *val, REBFLG *stop)
/*
**	Determines if a thrown value is either a break or continue.  If so,
**	modifies `val` to be the throw's argument, sets `stop` flag if it
**	was a BREAK or BREAK/WITH, and returns TRUE.
**
**	If FALSE is returned then the throw name `val` was not a break
**	or continue, and needs to be bubbled up or handled another way.
**
***********************************************************************/
{
	assert(THROWN(val));

	// Throw /NAME-s used by CONTINUE and BREAK are the actual native
	// function values of the routines themselves.
	if (!IS_NATIVE(val))
		return FALSE;

	if (VAL_FUNC_CODE(val) == VAL_FUNC_CODE(ROOT_BREAK_NATIVE)) {
		*stop = TRUE; // was BREAK or BREAK/WITH
		CATCH_THROWN(val, val); // will be unset if no /WITH was used
		return TRUE;
	}

	if (VAL_FUNC_CODE(val) == VAL_FUNC_CODE(ROOT_CONTINUE_NATIVE)) {
		*stop = FALSE; // was CONTINUE or CONTINUE/WITH
		CATCH_THROWN(val, val); // will be unset if no /WITH was used
		return TRUE;
	}

	// Else: Let all other thrown values bubble up.
	return FALSE;
}


/***********************************************************************
**
*/	static REBSER *Init_Loop(const REBVAL *spec, REBVAL *body_blk, REBSER **fram)
/*
**		Initialize standard for loops (copy block, make frame, bind).
**		Spec: WORD or [WORD ...]
**
**		Note that because we are copying the block in order to rebind it, the
**		ensuing loop code will `Do_At_Throws(out, body, 0);`.  Starting at
**		zero is correct because the duplicate body has already had the
**		items before its VAL_INDEX() omitted.
**
***********************************************************************/
{
	REBSER *frame;
	REBINT len;
	REBVAL *word;
	REBVAL *vals;
	REBSER *body;

	// For :WORD format, get the var's value:
	if (IS_GET_WORD(spec)) spec = GET_VAR(spec);

	// Hand-make a FRAME (done for for speed):
	len = IS_BLOCK(spec) ? VAL_LEN(spec) : 1;
	if (len == 0) raise Error_Invalid_Arg(spec);
	frame = Make_Frame(len, FALSE);
	SERIES_TAIL(frame) = len+1;
	SERIES_TAIL(FRM_KEYLIST(frame)) = len + 1;

	// Setup for loop:
	word = FRM_KEY(frame, 1); // skip SELF
	vals = BLK_SKIP(frame, 1);
	if (IS_BLOCK(spec)) spec = VAL_BLK_DATA(spec);

	// Optimally create the FOREACH frame:
	while (len-- > 0) {
		if (!IS_WORD(spec) && !IS_SET_WORD(spec)) {
			// Prevent inconsistent GC state:
			Free_Series(FRM_KEYLIST(frame));
			Free_Series(frame);
			raise Error_Invalid_Arg(spec);
		}
		Val_Init_Typeset(word, ALL_64, VAL_WORD_SYM(spec));
		word++;
		SET_NONE(vals);
		vals++;
		spec++;
	}
	SET_END(word);
	SET_END(vals);

	body = Copy_Array_At_Deep_Managed(
		VAL_SERIES(body_blk), VAL_INDEX(body_blk)
	);
	Bind_Values_Deep(BLK_HEAD(body), frame);

	*fram = frame;

	return body;
}


/***********************************************************************
**
*/	static REBFLG Loop_Series_Throws(REBVAL *out, REBVAL *var, REBSER* body, REBVAL *start, REBINT ei, REBINT ii)
/*
***********************************************************************/
{
	REBINT si = VAL_INDEX(start);
	REBCNT type = VAL_TYPE(start);

	*var = *start;

	if (ei >= cast(REBINT, VAL_TAIL(start)))
		ei = cast(REBINT, VAL_TAIL(start));

	if (ei < 0) ei = 0;

	SET_UNSET_UNLESS_LEGACY_NONE(out); // Default if the loop does not run

	for (; (ii > 0) ? si <= ei : si >= ei; si += ii) {
		VAL_INDEX(var) = si;

		if (Do_At_Throws(out, body, 0)) {
			REBFLG stop;
			if (Catching_Break_Or_Continue(out, &stop)) {
				if (stop) break;
				goto next_iteration;
			}
			return TRUE;
		}

	next_iteration:
		if (VAL_TYPE(var) != type) raise Error_1(RE_INVALID_TYPE, var);
		si = VAL_INDEX(var);
	}

	return FALSE;
}


/***********************************************************************
**
*/	static REBFLG Loop_Integer_Throws(REBVAL *out, REBVAL *var, REBSER* body, REBI64 start, REBI64 end, REBI64 incr)
/*
***********************************************************************/
{
	VAL_SET(var, REB_INTEGER);

	SET_UNSET_UNLESS_LEGACY_NONE(out); // Default if the loop does not run

	while ((incr > 0) ? start <= end : start >= end) {
		VAL_INT64(var) = start;

		if (Do_At_Throws(out, body, 0)) {
			REBFLG stop;
			if (Catching_Break_Or_Continue(out, &stop)) {
				if (stop) break;
				goto next_iteration;
			}
			return TRUE;
		}

	next_iteration:
		if (!IS_INTEGER(var)) raise Error_Has_Bad_Type(var);
		start = VAL_INT64(var);

		if (REB_I64_ADD_OF(start, incr, &start))
			raise Error_0(RE_OVERFLOW);
	}

	return FALSE;
}


/***********************************************************************
**
*/	static REBFLG Loop_Number_Throws(REBVAL *out, REBVAL *var, REBSER* body, REBVAL *start, REBVAL *end, REBVAL *incr)
/*
***********************************************************************/
{
	REBDEC s;
	REBDEC e;
	REBDEC i;

	if (IS_INTEGER(start))
		s = cast(REBDEC, VAL_INT64(start));
	else if (IS_DECIMAL(start) || IS_PERCENT(start))
		s = VAL_DECIMAL(start);
	else
		raise Error_Invalid_Arg(start);

	if (IS_INTEGER(end))
		e = cast(REBDEC, VAL_INT64(end));
	else if (IS_DECIMAL(end) || IS_PERCENT(end))
		e = VAL_DECIMAL(end);
	else
		raise Error_Invalid_Arg(end);

	if (IS_INTEGER(incr))
		i = cast(REBDEC, VAL_INT64(incr));
	else if (IS_DECIMAL(incr) || IS_PERCENT(incr))
		i = VAL_DECIMAL(incr);
	else
		raise Error_Invalid_Arg(incr);

	VAL_SET(var, REB_DECIMAL);

	SET_UNSET_UNLESS_LEGACY_NONE(out); // Default if the loop does not run

	for (; (i > 0.0) ? s <= e : s >= e; s += i) {
		VAL_DECIMAL(var) = s;

		if (Do_At_Throws(out, body, 0)) {
			REBFLG stop;
			if (Catching_Break_Or_Continue(out, &stop)) {
				if (stop) break;
				goto next_iteration;
			}
			return TRUE;
		}

	next_iteration:
		if (!IS_DECIMAL(var)) raise Error_Has_Bad_Type(var);
		s = VAL_DECIMAL(var);
	}

	return FALSE;
}


/***********************************************************************
**
*/	static REB_R Loop_All(struct Reb_Call *call_, REBINT mode)
/*
**		0: forall
**		1: forskip
**
***********************************************************************/
{
	REBVAL *var;
	REBSER *body;
	REBCNT bodi;
	REBSER *dat;
	REBINT idx;
	REBINT inc = 1;
	REBCNT type;
	REBVAL *ds;

	var = GET_MUTABLE_VAR(D_ARG(1));

	SET_UNSET_UNLESS_LEGACY_NONE(D_OUT);

	// Useful when the caller does an evaluation like `forall (any ...) [...]`
	// and wishes the code to effectively "opt-out" of the loop on an unset
	// or a none.
	if (IS_NONE(var) || IS_UNSET(var)) return R_OUT;

	// Save the starting var value:
	*D_ARG(1) = *var;

	if (mode == 1) inc = Int32(D_ARG(2));

	type = VAL_TYPE(var);
	body = VAL_SERIES(D_ARG(mode+2));
	bodi = VAL_INDEX(D_ARG(mode+2));

	// Starting location when past end with negative skip:
	if (inc < 0 && VAL_INDEX(var) >= VAL_TAIL(var)) {
		VAL_INDEX(var) = VAL_TAIL(var) + inc;
	}

	// NOTE: This math only works for index in positive ranges!

	if (ANY_SERIES(var)) {
		while (TRUE) {
			dat = VAL_SERIES(var);
			idx = VAL_INDEX(var);
			if (idx < 0) break;
			if (idx >= cast(REBINT, SERIES_TAIL(dat))) {
				if (inc >= 0) break;
				idx = SERIES_TAIL(dat) + inc; // negative
				if (idx < 0) break;
				VAL_INDEX(var) = idx;
			}

			if (Do_At_Throws(D_OUT, body, bodi)) {
				REBFLG stop;
				if (Catching_Break_Or_Continue(D_OUT, &stop)) {
					if (stop) {
						// Return value has been set in D_OUT, but we need
						// to reset var to its initial value
						*var = *D_ARG(1);
						return R_OUT;
					}
					goto next_iteration;
				}
				return R_OUT_IS_THROWN;
			}

		next_iteration:
			if (VAL_TYPE(var) != type) raise Error_Invalid_Arg(var);
			VAL_INDEX(var) += inc;
		}
	}
	else
		raise Error_Invalid_Arg(var);

	// !!!!! ???? allowed to write VAR????
	*var = *D_ARG(1);

	return R_OUT;
}


/***********************************************************************
**
*/	static REB_R Loop_Each(struct Reb_Call *call_, LOOP_MODE mode)
/*
**		Common implementation code of FOR-EACH, REMOVE-EACH, MAP-EACH,
**		and EVERY.
**
***********************************************************************/
{
	REBSER *body;
	REBVAL *vars;
	REBVAL *keys;
	REBSER *frame;

	// `data` is the series/object/map/etc. being iterated over
	// Note: `data_is_object` flag is optimized out, but hints static analyzer
	REBVAL *data = D_ARG(2);
	REBSER *series;
	const REBOOL data_is_object = ANY_OBJECT(data);

	REBSER *out;	// output block (needed for MAP-EACH)

	REBINT index;	// !!!! should these be REBCNT?
	REBINT tail;
	REBINT windex;	// write
	REBINT rindex;	// read
	REBCNT i;
	REBCNT j;
	REBVAL *ds;

	REBFLG stop = FALSE;
	REBFLG every_true = TRUE; // need due to OPTIONS_NONE_INSTEAD_OF_UNSETS
	REBOOL threw = FALSE; // did a non-BREAK or non-CONTINUE throw occur

	if (mode == LOOP_EVERY)
		SET_TRUE(D_OUT); // Default output is TRUE, to match ALL MAP-EACH
	else
		SET_UNSET_UNLESS_LEGACY_NONE(D_OUT); // Default if loop does not run

	if (IS_NONE(data) || IS_UNSET(data)) return R_OUT;

	body = Init_Loop(D_ARG(1), D_ARG(3), &frame); // vars, body
	Val_Init_Object(D_ARG(1), frame); // keep GC safe
	Val_Init_Block(D_ARG(3), body); // keep GC safe

	if (mode == LOOP_MAP_EACH) {
		// Must be managed *and* saved...because we are accumulating results
		// into it, and those results must be protected from GC

		// !!! This means we cannot Free_Series in case of a BREAK, we
		// have to leave it to the GC.  Is there a safe and efficient way
		// to allow inserting the managed values into a single-deep
		// unmanaged series if we *promise* not to go deeper?

		out = Make_Array(VAL_LEN(data));
		MANAGE_SERIES(out);
		PUSH_GUARD_SERIES(out);
	}

	// Get series info:
	if (data_is_object) {
		series = VAL_OBJ_FRAME(data);
		out = FRM_KEYLIST(series); // words (the out local reused)
		index = 1;
		//if (frame->tail > 3) raise Error_Invalid_Arg(FRM_KEY(frame, 3));
	}
	else if (IS_MAP(data)) {
		series = VAL_SERIES(data);
		index = 0;
		//if (frame->tail > 3) raise Error_Invalid_Arg(FRM_KEY(frame, 3));
	}
	else {
		series = VAL_SERIES(data);
		index  = VAL_INDEX(data);
		if (index >= cast(REBINT, SERIES_TAIL(series))) {
			if (mode == LOOP_REMOVE_EACH) {
				SET_INTEGER(D_OUT, 0);
			}
			else if (mode == LOOP_MAP_EACH) {
				DROP_GUARD_SERIES(out);
				Val_Init_Block(D_OUT, out);
			}
			return R_OUT;
		}
	}

	windex = index;

	// Iterate over each value in the data series block:
	while (index < (tail = SERIES_TAIL(series))) {

		rindex = index;  // remember starting spot
		j = 0;

		// Set the FOREACH loop variables from the series:
		for (i = 1; i < frame->tail; i++) {

			vars = FRM_VALUE(frame, i);
			keys = FRM_KEY(frame, i);

			if (TRUE) { // was IS_WORD but no longer applicable...

				if (index < tail) {

					if (ANY_ARRAY(data)) {
						*vars = *BLK_SKIP(series, index);
					}
					else if (data_is_object) {
						if (!VAL_GET_EXT(BLK_SKIP(out, index), EXT_WORD_HIDE)) {
							// Alternate between word and value parts of object:
							if (j == 0) {
								Val_Init_Word(vars, REB_WORD, VAL_TYPESET_SYM(BLK_SKIP(out, index)), series, index);
								if (NOT_END(vars+1)) index--; // reset index for the value part
							}
							else if (j == 1)
								*vars = *BLK_SKIP(series, index);
							else {
								// !!! Review this error (and this routine...)
								REBVAL key_name;
								Val_Init_Word_Unbound(
									&key_name, REB_WORD, VAL_TYPESET_SYM(keys)
								);
								raise Error_Invalid_Arg(&key_name);
							}
							j++;
						}
						else {
							// Do not evaluate this iteration
							index++;
							goto skip_hidden;
						}
					}
					else if (IS_VECTOR(data)) {
						Set_Vector_Value(vars, series, index);
					}
					else if (IS_MAP(data)) {
						REBVAL *val = BLK_SKIP(series, index | 1);
						if (!IS_NONE(val)) {
							if (j == 0) {
								*vars = *BLK_SKIP(series, index & ~1);
								if (IS_END(vars+1)) index++; // only words
							}
							else if (j == 1)
								*vars = *BLK_SKIP(series, index);
							else {
								// !!! Review this error (and this routine...)
								REBVAL key_name;
								Val_Init_Word_Unbound(
									&key_name, REB_WORD, VAL_TYPESET_SYM(keys)
								);
								raise Error_Invalid_Arg(&key_name);
							}
							j++;
						}
						else {
							index += 2;
							goto skip_hidden;
						}
					}
					else { // A string or binary
						if (IS_BINARY(data)) {
							SET_INTEGER(vars, (REBI64)(BIN_HEAD(series)[index]));
						}
						else if (IS_IMAGE(data)) {
							Set_Tuple_Pixel(BIN_SKIP(series, index), vars);
						}
						else {
							VAL_SET(vars, REB_CHAR);
							VAL_CHAR(vars) = GET_ANY_CHAR(series, index);
						}
					}
					index++;
				}
				else SET_NONE(vars);
			}
			else if (FALSE) { // !!! was IS_SET_WORD(keys), what was that for?
				if (ANY_OBJECT(data) || IS_MAP(data))
					*vars = *data;
				else
					Val_Init_Block_Index(vars, series, index);

				//if (index < tail) index++; // do not increment block.
			}
		}

		if (index == rindex) {
			// the word block has only set-words: for-each [a:] [1 2 3][]
			index++;
		}

		if (Do_At_Throws(D_OUT, body, 0)) {
			if (!Catching_Break_Or_Continue(D_OUT, &stop)) {
				// A non-loop throw, we should be bubbling up
				threw = TRUE;
				break;
			}

			// Fall through and process the D_OUT (unset if no /WITH) for
			// this iteration.  `stop` flag will be checked ater that.
		}

		switch (mode) {
		case LOOP_FOR_EACH:
			// no action needed after body is run
			break;
		case LOOP_REMOVE_EACH:
			// If FALSE return (or unset), copy values to the write location
			if (IS_CONDITIONAL_FALSE(D_OUT) || IS_UNSET(D_OUT)) {
				REBYTE wide = SERIES_WIDE(series);
				// memory areas may overlap, so use memmove and not memcpy!

				// !!! This seems a slow way to do it, but there's probably
				// not a lot that can be done as the series is expected to
				// be in a good state for the next iteration of the body. :-/
				memmove(
					series->data + (windex * wide),
					series->data + (rindex * wide),
					(index - rindex) * wide
				);
				windex += index - rindex;
			}
			break;
		case LOOP_MAP_EACH:
			// anything that's not an UNSET! will be added to the result
			if (!IS_UNSET(D_OUT)) Append_Value(out, D_OUT);
			break;
		case LOOP_EVERY:
			every_true = every_true && IS_CONDITIONAL_TRUE(D_OUT);
			break;
		default:
			assert(FALSE);
		}

		if (stop) break;

skip_hidden: ;
	}

	if (mode == LOOP_MAP_EACH) DROP_GUARD_SERIES(out);

	if (threw) {
		// a non-BREAK and non-CONTINUE throw overrides any other return
		// result we might give (generic THROW, RETURN, QUIT, etc.)

		return R_OUT_IS_THROWN;
	}

	// Note: This finalization will be run by finished loops as well as
	// interrupted ones.  So:
	//
	//    map-each x [1 2 3 4] [if x = 3 [break]] => [1 2]
	//
	//    map-each x [1 2 3 4] [if x = 3 [break/with "A"]] => [1 2 "A"]
	//
	//    every x [1 3 6 12] [if x = 6 [break/with 7] even? x] => 7
	//
	// This provides the most flexibility in the loop's processing, because
	// "override" logic already exists in the form of CATCH & THROW.

#if !defined(NDEBUG)
	if (LEGACY(OPTIONS_BREAK_WITH_OVERRIDES)) {
		// In legacy R3-ALPHA, BREAK without a provided value did *not*
		// override the result.  It returned the partial results.
		if (stop && !IS_UNSET(D_OUT))
			return R_OUT;
	}
#endif

	switch (mode) {
	case LOOP_FOR_EACH:
		// Returns last body result or /WITH of BREAK (or the /WITH of a
		// CONTINUE if it turned out to be the last iteration)
		return R_OUT;

	case LOOP_REMOVE_EACH:
		// Remove hole (updates tail):
		if (windex < index) Remove_Series(series, windex, index - windex);
		SET_INTEGER(D_OUT, index - windex);
		return R_OUT;

	case LOOP_MAP_EACH:
		Val_Init_Block(D_OUT, out);
		return R_OUT;

	case LOOP_EVERY:
		if (threw) return R_OUT_IS_THROWN;

		// Result is the cumulative TRUE? state of all the input (with any
		// unsets taken out of the consideration).  The last TRUE? input
		// if all valid and NONE! otherwise.  (Like ALL.)
		if (!every_true) return R_NONE;

		// We want to act like `ALL MAP-EACH ...`, hence we effectively ignore
		// unsets and return TRUE if the last evaluation leaves an unset.
		if (IS_UNSET(D_OUT)) return R_TRUE;

		return R_OUT;

	default:
		assert(FALSE);
	}

	DEAD_END;
}


/***********************************************************************
**
*/	REBNATIVE(for)
/*
**		FOR var start end bump [ body ]
**
***********************************************************************/
{
	REBSER *body;
	REBSER *frame;
	REBVAL *var;
	REBVAL *start = D_ARG(2);
	REBVAL *end   = D_ARG(3);
	REBVAL *incr  = D_ARG(4);

	// Copy body block, make a frame, bind loop var to it:
	body = Init_Loop(D_ARG(1), D_ARG(5), &frame);
	var = FRM_VALUE(frame, 1); // safe: not on stack
	Val_Init_Object(D_ARG(1), frame); // keep GC safe
	Val_Init_Block(D_ARG(5), body); // keep GC safe

	if (IS_INTEGER(start) && IS_INTEGER(end) && IS_INTEGER(incr)) {
		if (Loop_Integer_Throws(
			D_OUT,
			var,
			body,
			VAL_INT64(start),
			IS_DECIMAL(end) ? (REBI64)VAL_DECIMAL(end) : VAL_INT64(end),
			VAL_INT64(incr)
		)) {
			return R_OUT_IS_THROWN;
		}
	}
	else if (ANY_SERIES(start)) {
		if (ANY_SERIES(end)) {
			if (Loop_Series_Throws(
				D_OUT, var, body, start, VAL_INDEX(end), Int32(incr)
			)) {
				return R_OUT_IS_THROWN;
			}
		}
		else {
			if (Loop_Series_Throws(
				D_OUT, var, body, start, Int32s(end, 1) - 1, Int32(incr)
			)) {
				return R_OUT_IS_THROWN;
			}
		}
	}
	else {
		if (Loop_Number_Throws(D_OUT, var, body, start, end, incr))
			return R_OUT_IS_THROWN;
	}

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(forall)
/*
***********************************************************************/
{
	return Loop_All(call_, 0);
}


/***********************************************************************
**
*/	REBNATIVE(forskip)
/*
***********************************************************************/
{
	return Loop_All(call_, 1);
}


/***********************************************************************
**
*/	REBNATIVE(forever)
/*
***********************************************************************/
{
	REBVAL * const block = D_ARG(1);

	do {
		if (DO_ARRAY_THROWS(D_OUT, block)) {
			REBFLG stop;
			if (Catching_Break_Or_Continue(D_OUT, &stop)) {
				if (stop) return R_OUT;
				continue;
			}
			return R_OUT_IS_THROWN;
		}
	} while (TRUE);

	DEAD_END;
}


/***********************************************************************
**
*/	REBNATIVE(for_each)
/*
**		{Evaluates a block for each value(s) in a series.}
**		'word [get-word! word! block!] {Word or block of words}
**		data [any-series!] {The series to traverse}
**		body [block!] {Block to evaluate each time}
**
***********************************************************************/
{
	return Loop_Each(call_, LOOP_FOR_EACH);
}


/***********************************************************************
**
*/	REBNATIVE(remove_each)
/*
**		'word [get-word! word! block!] {Word or block of words}
**		data [any-series!] {The series to traverse}
**		body [block!] {Block to evaluate each time}
**
***********************************************************************/
{
	return Loop_Each(call_, LOOP_REMOVE_EACH);
}


/***********************************************************************
**
*/	REBNATIVE(map_each)
/*
**		'word [get-word! word! block!] {Word or block of words}
**		data [any-series!] {The series to traverse}
**		body [block!] {Block to evaluate each time}
**
***********************************************************************/
{
	return Loop_Each(call_, LOOP_MAP_EACH);
}


/***********************************************************************
**
*/	REBNATIVE(every)
/*
**		'word [get-word! word! block!] {Word or block of words}
**		data [any-series!] {The series to traverse}
**		body [block!] {Block to evaluate each time}
**
***********************************************************************/
{
	return Loop_Each(call_, LOOP_EVERY);
}


/***********************************************************************
**
*/	REBNATIVE(loop)
/*
***********************************************************************/
{
	REBI64 count = Int64(D_ARG(1));
	REBVAL * const block = D_ARG(2);
	REBVAL *ds;

	SET_UNSET_UNLESS_LEGACY_NONE(D_OUT); // Default if the loop does not run

	for (; count > 0; count--) {
		if (DO_ARRAY_THROWS(D_OUT, block)) {
			REBFLG stop;
			if (Catching_Break_Or_Continue(D_OUT, &stop)) {
				if (stop) return R_OUT;
				continue;
			}
			return R_OUT_IS_THROWN;
		}
	}

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(repeat)
/*
**		REPEAT var 123 [ body ]
**
***********************************************************************/
{
	REBSER *body;
	REBSER *frame;
	REBVAL *var;
	REBVAL *count = D_ARG(2);

	if (IS_NONE(count)) {
		SET_UNSET_UNLESS_LEGACY_NONE(D_OUT);
		return R_OUT;
	}

	if (IS_DECIMAL(count) || IS_PERCENT(count)) {
		VAL_INT64(count) = Int64(count);
		VAL_SET(count, REB_INTEGER);
	}

	body = Init_Loop(D_ARG(1), D_ARG(3), &frame);
	var = FRM_VALUE(frame, 1); // safe: not on stack
	Val_Init_Object(D_ARG(1), frame); // keep GC safe
	Val_Init_Block(D_ARG(3), body); // keep GC safe

	if (ANY_SERIES(count)) {
		if (Loop_Series_Throws(
			D_OUT, var, body, count, VAL_TAIL(count) - 1, 1
		)) {
			return R_OUT_IS_THROWN;
		}

		return R_OUT;
	}
	else if (IS_INTEGER(count)) {
		if (Loop_Integer_Throws(D_OUT, var, body, 1, VAL_INT64(count), 1))
			return R_OUT_IS_THROWN;

		return R_OUT;
	}

	SET_UNSET_UNLESS_LEGACY_NONE(D_OUT);
	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(until)
/*
***********************************************************************/
{
	REBVAL * const block = D_ARG(1);

	do {
		if (DO_ARRAY_THROWS(D_OUT, block)) {
			REBFLG stop;
			if (Catching_Break_Or_Continue(D_OUT, &stop)) {
				if (stop) return R_OUT;
				continue;
			}
			return R_OUT_IS_THROWN;
		}

		if (IS_UNSET(D_OUT)) raise Error_0(RE_NO_RETURN);

	} while (IS_CONDITIONAL_FALSE(D_OUT));

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(while)
/*
***********************************************************************/
{
	REBVAL * const condition = D_ARG(1);
	REBVAL * const body = D_ARG(2);

	// We need to keep the condition and body safe from GC, so we can't
	// use a D_ARG slot for evaluating the condition (can't overwrite
	// D_OUT because that's the last loop's value we might return)
	REBVAL temp;

	// If the loop body never runs (and condition doesn't error or throw),
	// we want to return an UNSET!
	SET_UNSET_UNLESS_LEGACY_NONE(D_OUT);

	do {
		if (DO_ARRAY_THROWS(&temp, condition)) {
			// A while loop should only look for breaks and continues in its
			// body, not in its condition.  So `while [break] []` is a
			// request to break the enclosing loop (or error if there is
			// nothing to catch that break).  Hence we bubble up the throw.
			*D_OUT = temp;
			return R_OUT_IS_THROWN;
		}

		if (IS_UNSET(&temp))
			raise Error_0(RE_NO_RETURN);

		if (IS_CONDITIONAL_FALSE(&temp)) {
			// When the condition evaluates to a LOGIC! false or a NONE!,
			// WHILE returns whatever the last value was that the body
			// evaluated to (or none if no body evaluations yet).
			return R_OUT;
		}

		if (DO_ARRAY_THROWS(D_OUT, body)) {
			REBFLG stop;
			if (Catching_Break_Or_Continue(D_OUT, &stop)) {
				if (stop) return R_OUT;
				continue;
			}
			return R_OUT_IS_THROWN;
		}
	} while (TRUE);
}
