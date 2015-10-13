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
*/	REBOOL Loop_Throw_Should_Return(REBVAL *val)
/*
**		Process values thrown during loop, and tell the loop whether
**		to take that processed value and return it up the stack.
**		If not, then the throw was a continue...and the code
**		should just keep going.
**
**		Note: This modifies the input value.  If it returns FALSE
**		then the value is guaranteed to not be THROWN(), but it
**		may-or-may-not be THROWN() if TRUE is returned.
**
***********************************************************************/
{
	assert(THROWN(val));

	// Using words for BREAK and CONTINUE to parallel old VAL_ERR_SYM()
	// code.  So if the throw wasn't a word it can't be either of those,
	// hence the loop doesn't handle it and needs to bubble up the THROWN()
	if (!IS_WORD(val))
		return TRUE;

	// If it's a CONTINUE then wipe out the THROWN() value with UNSET,
	// and tell the loop it doesn't have to return.
	if (VAL_WORD_SYM(val) == SYM_CONTINUE) {
		SET_UNSET(val);
		return FALSE;
	}

	// If it's a BREAK, get the /WITH value (UNSET! if no /WITH) and
	// say it should be returned.
	if (VAL_WORD_SYM(val) == SYM_BREAK) {
		TAKE_THROWN_ARG(val, val);
		return TRUE;
	}

	// Else: Let all other THROWN() values bubble up.
	return TRUE;
}


/***********************************************************************
**
*/	static REBSER *Init_Loop(const REBVAL *spec, REBVAL *body_blk, REBSER **fram)
/*
**		Initialize standard for loops (copy block, make frame, bind).
**		Spec: WORD or [WORD ...]
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
*/	static void Loop_Series(REBVAL *out, REBVAL *var, REBSER* body, REBVAL *start, REBINT ei, REBINT ii)
/*
***********************************************************************/
{
	REBINT si = VAL_INDEX(start);
	REBCNT type = VAL_TYPE(start);

	*var = *start;

	if (ei >= cast(REBINT, VAL_TAIL(start)))
		ei = cast(REBINT, VAL_TAIL(start));

	if (ei < 0) ei = 0;

	SET_NONE(out); // Default result to NONE if the loop does not run

	for (; (ii > 0) ? si <= ei : si >= ei; si += ii) {
		VAL_INDEX(var) = si;

		if (Do_Block_Throws(out, body, 0)) {
			if (Loop_Throw_Should_Return(out)) break;
		}

		if (VAL_TYPE(var) != type) raise Error_1(RE_INVALID_TYPE, var);
		si = VAL_INDEX(var);
	}
}


/***********************************************************************
**
*/	static void Loop_Integer(REBVAL *out, REBVAL *var, REBSER* body, REBI64 start, REBI64 end, REBI64 incr)
/*
***********************************************************************/
{
	VAL_SET(var, REB_INTEGER);

	SET_NONE(out); // Default result to NONE if the loop does not run

	while ((incr > 0) ? start <= end : start >= end) {
		VAL_INT64(var) = start;

		if (Do_Block_Throws(out, body, 0)) {
			if (Loop_Throw_Should_Return(out)) break;
		}

		if (!IS_INTEGER(var)) raise Error_Has_Bad_Type(var);
		start = VAL_INT64(var);

		if (REB_I64_ADD_OF(start, incr, &start))
			raise Error_0(RE_OVERFLOW);
	}
}


/***********************************************************************
**
*/	static void Loop_Number(REBVAL *out, REBVAL *var, REBSER* body, REBVAL *start, REBVAL *end, REBVAL *incr)
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

	SET_NONE(out); // Default result to NONE if the loop does not run

	for (; (i > 0.0) ? s <= e : s >= e; s += i) {
		VAL_DECIMAL(var) = s;

		if (Do_Block_Throws(out, body, 0)) {
			if (Loop_Throw_Should_Return(out)) break;
		}

		if (!IS_DECIMAL(var)) raise Error_Has_Bad_Type(var);
		s = VAL_DECIMAL(var);
	}
}


/***********************************************************************
**
*/	static int Loop_All(struct Reb_Call *call_, REBINT mode)
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
	if (IS_NONE(var)) return R_NONE;

	// Save the starting var value:
	*D_ARG(1) = *var;

	SET_NONE(D_OUT);

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

			if (Do_Block_Throws(D_OUT, body, bodi)) {
				if (Loop_Throw_Should_Return(D_OUT)) {
					// return value is set, but we still need to assign var
					break;
				}
			}

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
	REBOOL break_with = FALSE;
	REBOOL every_true = TRUE;
	REBCNT i;
	REBCNT j;
	REBVAL *ds;

	if (IS_NONE(data)) return R_NONE;

	body = Init_Loop(D_ARG(1), D_ARG(3), &frame); // vars, body
	Val_Init_Object(D_ARG(1), frame); // keep GC safe
	Val_Init_Block(D_ARG(3), body); // keep GC safe

	SET_NONE(D_OUT); // Default result to NONE if the loop does not run

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

					if (ANY_BLOCK(data)) {
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

		if (Do_Block_Throws(D_OUT, body, 0)) {
			if (IS_WORD(D_OUT) && VAL_WORD_SYM(D_OUT) == SYM_CONTINUE) {
				if (mode == LOOP_REMOVE_EACH) {
					// signal the post-body-execution processing that we
					// *do not* want to remove the element on a CONTINUE
					SET_FALSE(D_OUT);
				}
				else {
					// CONTINUE otherwise acts "as if" the loop body execution
					// returned an UNSET!
					SET_UNSET(D_OUT);
				}
			}
			else if (IS_WORD(D_OUT) && VAL_WORD_SYM(D_OUT) == SYM_BREAK) {
				// If it's a BREAK, get the /WITH value (UNSET! if no /WITH)
				// Though technically this doesn't really tell us if a
				// BREAK/WITH happened, as you can BREAK/WITH an UNSET!
				TAKE_THROWN_ARG(D_OUT, D_OUT);
				if (!IS_UNSET(D_OUT))
					break_with = TRUE;
				index = rindex;
				break;
			}
			else {
				// Any other kind of throw, with a WORD! name or otherwise...
				index = rindex;
				break;
			}
		}

		switch (mode) {
		case LOOP_FOR_EACH:
			// no action needed after body is run
			break;
		case LOOP_REMOVE_EACH:
			// If FALSE return, copy values to the write location
			// !!! Should UNSET! also act as conditional false here?  Error?
			if (IS_CONDITIONAL_FALSE(D_OUT)) {
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
			if (every_true) {
				// !!! This currently treats UNSET! as true, which ALL
				// effectively does right now.  That's likely a bad idea.
				// When ALL changes, so should this.
				//
				every_true = IS_CONDITIONAL_TRUE(D_OUT);
			}
			break;
		default:
			assert(FALSE);
		}
skip_hidden: ;
	}

	switch (mode) {
	case LOOP_FOR_EACH:
		// Nothing to do but return last result (will be UNSET! if an
		// ordinary BREAK was used, the /WITH if a BREAK/WITH was used,
		// and an UNSET! if the last loop iteration did a CONTINUE.)
		return R_OUT;

	case LOOP_REMOVE_EACH:
		// Remove hole (updates tail):
		if (windex < index) Remove_Series(series, windex, index - windex);
		SET_INTEGER(D_OUT, index - windex);

		return R_OUT;

	case LOOP_MAP_EACH:
		DROP_GUARD_SERIES(out);
		if (break_with) {
			// If BREAK is given a /WITH parameter that is not an UNSET!, it
			// is assumed that you want to override the accumulated mapped
			// data so far and return the /WITH value. (which will be in
			// D_OUT when the loop above is `break`-ed)

			// !!! Would be nice if we could Free_Series(out), but it is owned
			// by GC (we had to make it that way to use SAVE_SERIES on it)
			return R_OUT;
		}

		// If you BREAK/WITH an UNSET! (or just use a BREAK that has no
		// /WITH, which is indistinguishable in the thrown value) then it
		// returns the accumulated results so far up to the break.

		Val_Init_Block(D_OUT, out);
		return R_OUT;

	case LOOP_EVERY:
		// Result is the cumulative TRUE? state of all the input (with any
		// unsets taken out of the consideration).  The last TRUE? input
		// if all valid and NONE! otherwise.  (Like ALL.)  If the loop
		// never runs, `every_true` will be TRUE *but* D_OUT will be NONE!
		if (!every_true)
			SET_NONE(D_OUT);
		return R_OUT;
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
		Loop_Integer(D_OUT, var, body, VAL_INT64(start),
			IS_DECIMAL(end) ? (REBI64)VAL_DECIMAL(end) : VAL_INT64(end), VAL_INT64(incr));
	}
	else if (ANY_SERIES(start)) {
		if (ANY_SERIES(end))
			Loop_Series(D_OUT, var, body, start, VAL_INDEX(end), Int32(incr));
		else
			Loop_Series(D_OUT, var, body, start, Int32s(end, 1) - 1, Int32(incr));
	}
	else
		Loop_Number(D_OUT, var, body, start, end, incr);

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
	do {
		if (Do_Block_Throws(D_OUT, VAL_SERIES(D_ARG(1)), 0)) {
			if (Loop_Throw_Should_Return(D_OUT)) return R_OUT;
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
	REBSER *block = VAL_SERIES(D_ARG(2));
	REBCNT index  = VAL_INDEX(D_ARG(2));
	REBVAL *ds;

	SET_NONE(D_OUT); // Default result to NONE if the loop does not run

	for (; count > 0; count--) {
		if (Do_Block_Throws(D_OUT, block, index)) {
			if (Loop_Throw_Should_Return(D_OUT)) return R_OUT;
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

	if (IS_NONE(count)) return R_NONE;

	if (IS_DECIMAL(count) || IS_PERCENT(count)) {
		VAL_INT64(count) = Int64(count);
		VAL_SET(count, REB_INTEGER);
	}

	body = Init_Loop(D_ARG(1), D_ARG(3), &frame);
	var = FRM_VALUE(frame, 1); // safe: not on stack
	Val_Init_Object(D_ARG(1), frame); // keep GC safe
	Val_Init_Block(D_ARG(3), body); // keep GC safe

	if (ANY_SERIES(count)) {
		Loop_Series(D_OUT, var, body, count, VAL_TAIL(count) - 1, 1);
		return R_OUT;
	}
	else if (IS_INTEGER(count)) {
		Loop_Integer(D_OUT, var, body, 1, VAL_INT64(count), 1);
		return R_OUT;
	}

	return R_NONE;
}


/***********************************************************************
**
*/	REBNATIVE(until)
/*
***********************************************************************/
{
	REBSER *b1 = VAL_SERIES(D_ARG(1));
	REBCNT i1  = VAL_INDEX(D_ARG(1));

	do {
utop:
		if (Do_Block_Throws(D_OUT, b1, i1)) {
			if (Loop_Throw_Should_Return(D_OUT)) return R_OUT;
			goto utop;
		}

		if (IS_UNSET(D_OUT)) raise Error_0(RE_NO_RETURN);

	} while (IS_CONDITIONAL_FALSE(D_OUT)); // Break, return errors fall out.
	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(while)
/*
***********************************************************************/
{
	REBSER *cond_series = VAL_SERIES(D_ARG(1));
	REBCNT cond_index = VAL_INDEX(D_ARG(1));
	REBSER *body_series = VAL_SERIES(D_ARG(2));
	REBCNT body_index = VAL_INDEX(D_ARG(2));

	// We need to keep the condition and body safe from GC, so we can't
	// use a D_ARG slot for evaluating the condition (can't overwrite
	// D_OUT because that's the last loop's value we might return)
	REBVAL cond_out;

	// If the loop body never runs (and condition doesn't error or throw),
	// we want to return a NONE!
	SET_NONE(D_OUT);

	do {
		if (Do_Block_Throws(&cond_out, cond_series, cond_index)) {
			// A while loop should only look for breaks and continues in its
			// body, not in its condition.  So `while [break] []` is a
			// request to break the enclosing loop (or error if there is
			// nothing to catch that break).  Hence we bubble up the throw.
			*D_OUT = cond_out;
			return R_OUT;
		}

		if (IS_CONDITIONAL_FALSE(&cond_out)) {
			// When the condition evaluates to a LOGIC! false or a NONE!,
			// WHILE returns whatever the last value was that the body
			// evaluated to (or none if no body evaluations yet).
			return R_OUT;
		}

		if (IS_UNSET(&cond_out))
			raise Error_0(RE_NO_RETURN);

		if (Do_Block_Throws(D_OUT, body_series, body_index)) {
			// !!! Process_Loop_Throw may modify its argument
			if (Loop_Throw_Should_Return(D_OUT)) return R_OUT;
		}
	} while (TRUE);
}
