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
	if (len == 0) Trap_Arg_DEAD_END(spec);
	frame = Make_Frame(len, FALSE);
	SERIES_TAIL(frame) = len+1;
	SERIES_TAIL(FRM_WORD_SERIES(frame)) = len+1;

	// Setup for loop:
	word = FRM_WORD(frame, 1); // skip SELF
	vals = BLK_SKIP(frame, 1);
	if (IS_BLOCK(spec)) spec = VAL_BLK_DATA(spec);

	// Optimally create the FOREACH frame:
	while (len-- > 0) {
		if (!IS_WORD(spec) && !IS_SET_WORD(spec)) {
			// Prevent inconsistent GC state:
			Free_Series(FRM_WORD_SERIES(frame));
			Free_Series(frame);
			Trap_Arg_DEAD_END(spec);
		}
		Val_Init_Word_Typed(word, VAL_TYPE(spec), VAL_WORD_SYM(spec), ALL_64);
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
			if (Process_Loop_Throw(out) >= 0) break;
		}

		if (VAL_TYPE(var) != type) Trap1(RE_INVALID_TYPE, var);
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
			if (Process_Loop_Throw(out) >= 0) break;
		}

		if (!IS_INTEGER(var)) Trap_Type(var);
		start = VAL_INT64(var);

		if (REB_I64_ADD_OF(start, incr, &start)) {
			Trap(RE_OVERFLOW);
		}
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

	if (IS_INTEGER(start)) s = cast(REBDEC, VAL_INT64(start));
	else if (IS_DECIMAL(start) || IS_PERCENT(start)) s = VAL_DECIMAL(start);
	else { Trap_Arg(start); DEAD_END_VOID; }

	if (IS_INTEGER(end)) e = cast(REBDEC, VAL_INT64(end));
	else if (IS_DECIMAL(end) || IS_PERCENT(end)) e = VAL_DECIMAL(end);
	else { Trap_Arg(end); DEAD_END_VOID; }

	if (IS_INTEGER(incr)) i = cast(REBDEC, VAL_INT64(incr));
	else if (IS_DECIMAL(incr) || IS_PERCENT(incr)) i = VAL_DECIMAL(incr);
	else { Trap_Arg(incr); DEAD_END_VOID; }

	VAL_SET(var, REB_DECIMAL);

	SET_NONE(out); // Default result to NONE if the loop does not run

	for (; (i > 0.0) ? s <= e : s >= e; s += i) {
		VAL_DECIMAL(var) = s;

		if (Do_Block_Throws(out, body, 0)) {
			if (Process_Loop_Throw(out) >= 0) break;
		}

		if (!IS_DECIMAL(var)) Trap_Type(var);
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
				if (Process_Loop_Throw(D_OUT) >= 0) {
					break;
				}
			}

			if (VAL_TYPE(var) != type) Trap_Arg_DEAD_END(var);

			VAL_INDEX(var) += inc;
		}
	}
	else Trap_Arg_DEAD_END(var);

	// !!!!! ???? allowed to write VAR????
	*var = *D_ARG(1);

	return R_OUT;
}


/***********************************************************************
**
*/	static REB_R Loop_Each(struct Reb_Call *call_, REBINT mode)
/*
**		Supports these natives (modes):
**			0: foreach
**			1: remove-each
**			2: map
**
***********************************************************************/
{
	REBSER *body;
	REBVAL *vars;
	REBVAL *words;
	REBSER *frame;
	REBVAL *value;
	REBSER *series;
	REBSER *out;	// output block (for MAP, mode = 2)

	REBINT index;	// !!!! should these be REBCNT?
	REBINT tail;
	REBINT windex;	// write
	REBINT rindex;	// read
	REBINT err;
	REBCNT i;
	REBCNT j;
	REBVAL *ds;

	assert(mode >= 0 && mode < 3);

	value = D_ARG(2); // series
	if (IS_NONE(value)) return R_NONE;

	body = Init_Loop(D_ARG(1), D_ARG(3), &frame); // vars, body
	Val_Init_Object(D_ARG(1), frame); // keep GC safe
	Val_Init_Block(D_ARG(3), body); // keep GC safe

	SET_NONE(D_OUT); // Default result to NONE if the loop does not run

	// If it's MAP, create result block:
	if (mode == 2) {
		// Must be managed *and* saved...because we are accumulating results
		// into it, and those results must be protected from GC

		// !!! This means we cannot Free_Series in case of a BREAK, we
		// have to leave it to the GC.  Should there be a variant which
		// lets a series be a GC root for a temporary time even if it is
		// not SER_KEEP?

		out = Make_Array(VAL_LEN(value));
		MANAGE_SERIES(out);
		SAVE_SERIES(out);
	}

	// Get series info:
	if (ANY_OBJECT(value)) {
		series = VAL_OBJ_FRAME(value);
		out = FRM_WORD_SERIES(series); // words (the out local reused)
		index = 1;
		//if (frame->tail > 3) Trap_Arg_DEAD_END(FRM_WORD(frame, 3));
	}
	else if (IS_MAP(value)) {
		series = VAL_SERIES(value);
		index = 0;
		//if (frame->tail > 3) Trap_Arg_DEAD_END(FRM_WORD(frame, 3));
	}
	else {
		series = VAL_SERIES(value);
		index  = VAL_INDEX(value);
		if (index >= cast(REBINT, SERIES_TAIL(series))) {
			if (mode == 1) {
				SET_INTEGER(D_OUT, 0);
			} else if (mode == 2) {
				UNSAVE_SERIES(out);
				Val_Init_Block(D_OUT, out);
			}
			return R_OUT;
		}
	}

	windex = index;

	// Iterate over each value in the series block:
	while (index < (tail = SERIES_TAIL(series))) {

		rindex = index;  // remember starting spot
		j = 0;

		// Set the FOREACH loop variables from the series:
		for (i = 1; i < frame->tail; i++) {

			vars = FRM_VALUE(frame, i);
			words = FRM_WORD(frame, i);

			// var spec is WORD
			if (IS_WORD(words)) {

				if (index < tail) {

					if (ANY_BLOCK(value)) {
						*vars = *BLK_SKIP(series, index);
					}

					else if (ANY_OBJECT(value)) {
						if (!VAL_GET_EXT(BLK_SKIP(out, index), EXT_WORD_HIDE)) {
							// Alternate between word and value parts of object:
							if (j == 0) {
								Val_Init_Word(vars, REB_WORD, VAL_WORD_SYM(BLK_SKIP(out, index)), series, index);
								if (NOT_END(vars+1)) index--; // reset index for the value part
							}
							else if (j == 1)
								*vars = *BLK_SKIP(series, index);
							else
								Trap_Arg_DEAD_END(words);
							j++;
						}
						else {
							// Do not evaluate this iteration
							index++;
							goto skip_hidden;
						}
					}

					else if (IS_VECTOR(value)) {
						Set_Vector_Value(vars, series, index);
					}

					else if (IS_MAP(value)) {
						REBVAL *val = BLK_SKIP(series, index | 1);
						if (!IS_NONE(val)) {
							if (j == 0) {
								*vars = *BLK_SKIP(series, index & ~1);
								if (IS_END(vars+1)) index++; // only words
							}
							else if (j == 1)
								*vars = *BLK_SKIP(series, index);
							else
								Trap_Arg_DEAD_END(words);
							j++;
						}
						else {
							index += 2;
							goto skip_hidden;
						}
					}

					else { // A string or binary
						if (IS_BINARY(value)) {
							SET_INTEGER(vars, (REBI64)(BIN_HEAD(series)[index]));
						}
						else if (IS_IMAGE(value)) {
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

			// var spec is SET_WORD:
			else if (IS_SET_WORD(words)) {
				if (ANY_OBJECT(value) || IS_MAP(value))
					*vars = *value;
				else
					Val_Init_Block_Index(vars, series, index);

				//if (index < tail) index++; // do not increment block.
			}
			else Trap_Arg_DEAD_END(words);
		}
		if (index == rindex) index++; //the word block has only set-words: foreach [a:] [1 2 3][]

		if (Do_Block_Throws(D_OUT, body, 0)) {
			if ((err = Process_Loop_Throw(D_OUT)) >= 0) {
				index = rindex;
				break;
			}
			// else CONTINUE:
			if (mode == 1) SET_FALSE(D_OUT); // keep the value (for mode == 1)
		} else {
			err = 0; // prevent later test against uninitialized value
		}

		if (mode > 0) {
			// If FALSE return, copy values to the write location:
			if (mode == 1) {  // remove-each
				if (IS_CONDITIONAL_FALSE(D_OUT)) {
					REBYTE wide = SERIES_WIDE(series);
					// memory areas may overlap, so use memmove and not memcpy!
					memmove(series->data + (windex * wide), series->data + (rindex * wide), (index - rindex) * wide);
					windex += index - rindex;
					// old: while (rindex < index) *BLK_SKIP(series, windex++) = *BLK_SKIP(series, rindex++);
				}
			}
			else
				if (!IS_UNSET(D_OUT)) Append_Value(out, D_OUT); // (mode == 2)
		}
skip_hidden: ;
	}

	// Finish up:
	if (mode == 1) {
		// Remove hole (updates tail):
		if (windex < index) Remove_Series(series, windex, index - windex);
		SET_INTEGER(D_OUT, index - windex);

		return R_OUT;
	}

	// If MAP...
	if (mode == 2) {
		UNSAVE_SERIES(out);
		if (err != 2) {
			// ...and not BREAK/RETURN:
			Val_Init_Block(D_OUT, out);
			return R_OUT;
		}
		// Would be nice if we could Free_Series(out), but it is owned by GC
		// (we had to make it that way to use SAVE_SERIES on it)
	}

	return R_OUT;
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
			if (Process_Loop_Throw(D_OUT) >= 0) return R_OUT;
		}
	} while (TRUE);

	DEAD_END;
}


/***********************************************************************
**
*/	REBNATIVE(foreach)
/*
**		{Evaluates a block for each value(s) in a series.}
**		'word [get-word! word! block!] {Word or block of words}
**		data [series!] {The series to traverse}
**		body [block!] {Block to evaluate each time}
**
***********************************************************************/
{
	return Loop_Each(call_, 0);
}


/***********************************************************************
**
*/	REBNATIVE(remove_each)
/*
**		'word [get-word! word! block!] {Word or block of words}
**		data [series!] {The series to traverse}
**		body [block!] {Block to evaluate each time}
**
***********************************************************************/
{
	return Loop_Each(call_, 1);
}


/***********************************************************************
**
*/	REBNATIVE(map_each)
/*
**		'word [get-word! word! block!] {Word or block of words}
**		data [series!] {The series to traverse}
**		body [block!] {Block to evaluate each time}
**
***********************************************************************/
{
	return Loop_Each(call_, 2);
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
			if (Process_Loop_Throw(D_OUT) >= 0) break;
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
			if (Process_Loop_Throw(D_OUT) >= 0) break;
			goto utop;
		}

		if (IS_UNSET(D_OUT)) Trap_DEAD_END(RE_NO_RETURN);

	} while (IS_CONDITIONAL_FALSE(D_OUT)); // Break, return errors fall out.
	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(while)
/*
***********************************************************************/
{
	REBSER *b1 = VAL_SERIES(D_ARG(1));
	REBCNT i1  = VAL_INDEX(D_ARG(1));
	REBSER *b2 = VAL_SERIES(D_ARG(2));
	REBCNT i2  = VAL_INDEX(D_ARG(2));

	// We need to keep the condition and body safe from GC, so we can't
	// use a D_ARG slot for evaluating the condition (can't overwrite
	// D_OUT because that's the last loop's value we might return)
	REBVAL temp;

	// If the loop body never runs (and condition doesn't error or throw),
	// we want to return a NONE!
	SET_NONE(D_OUT);

	do {
		if (Do_Block_Throws(&temp, b1, i1) || IS_UNSET(&temp)) {
			if (Process_Loop_Throw(&temp) >= 0) {
				// Process_Loop_Throw modifies its argument so temp will be
				// UNSET! (or the arg to BREAK/WITH) if a BREAK happened.
				*D_OUT = temp;
				return R_OUT;
			}
			// CONTINUE will pass through here...
		}

		if (IS_CONDITIONAL_FALSE(&temp)) return R_OUT;

		// Not interested in the value of the condition loop once we've
		// decided to run the body...

		if (Do_Block_Throws(D_OUT, b2, i2)) {
			// !!! Process_Loop_Throw may modify its argument
			if (Process_Loop_Throw(D_OUT) >= 0) return R_OUT;
		}
	} while (TRUE);
}
