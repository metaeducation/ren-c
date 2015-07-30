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
**  Summary: REBOL Stack Definitions
**  Module:  sys-stack.h
**  Author:  Carl Sassenrath
**  Notes:
**
**	DSP: index to the top of stack (active value)
**	DSF: index to the base of stack frame (return value)
**
**	Stack frame format:
**
**		   +---------------+
**	DSF->0:| Return Value  | normally becomes TOS after func return
**		   +---------------+
**		 1:|  Prior Frame  | old DSF, block, and block index
**		   +---------------+
**		 2:|   Func Word   | for backtrace info
**		   +---------------+
**		 3:|   Func Value  | in case value is moved or modified
**		   +---------------+
**		 4:|     Arg 1     | args begin here
**		   +---------------+
**		   |     Arg 2     |
**		   +---------------+
**
***********************************************************************/

// Data Stack Pointer isn't a "C pointer", but indexes into Rebol's data stack

#define DSP DS_Index


// Prior to StableStack, a pointer to a REBVAL that lives in the data stack
// is very volatile.  Any pushes can expand the stack, which means any
// pointers may go invalid.  As an interim step, it can be helpful to be
// assured that a pointer is not into the data stack.  For assertions only.

#if !defined(NDEBUG)
	#define IN_DATA_STACK(p) \
		(((p) >= &DS_Base[0]) && ((p <= &DS_Base[DS_Index])))
#endif


// "Data Stack Frame" indexes into Rebol's data stack at the location where
// the block of information about a function call begins.  It starts with the
// location where the return value is written, and has other properties (like
// the REBVAL of the function being called itself) up to the values that are
// computed arguments to the function.

// !!! Note that terminology-wise, the slot in the frame that used to be
// called DSF_RETURN is now called DSF_OUT.  It is the first element in
// the frame in the data-stack implementation, because when the stack is
// "dropped" back to the point where the call was made, it is what is on
// the top of the stack.  But in StableStack, this can be a pointer to any
// address, as function calls can be told to write their output anywhere.
// (and the REBVAL* parameter to the replacement for Do_Next is called "out"
// so it makes sense in that way, too.)
//
// !!! Vis a vis, concordantly...DSF_RETURN is reserved for the definitionally
// scoped return function built for the specific call the frame represents.

#define DSF_SIZE		4					// from DSF to ARGS-1
#define DSF_OUT(d)		(&DS_Base[d])		// where to write return value
#define PRIOR_DSF(d) \
	VAL_INT32(&DS_Base[(d)+1])
#define DSF_POSITION(d) (&DS_Base[(d)+2])	// block and index of execution
#define DSF_LABEL(d)	(&DS_Base[(d)+3])	// func word backtrace
#define DSF_FUNC(d)		(&DS_Base[(d)+4])	// function value saved
#define DSF_RETURN(d)	coming@soon			// return func linked to this call
#define DSF_ARG(d,n)	(&DS_Base[(d)+DSF_SIZE+(n)])


#ifdef STRESS
	// In a "stress checked" debug mode, every time the DSF is accessed we
	// can verify that it is well-formed.
	#define DSF (*DSF_Stress())
	#define SET_DSF(ds) \
		(DS_Frame_Index = (ds), cast(void, DSF_Stress()))
#else
	// Normal builds just use DS_Frame_Index directly
	#define DSF DS_Frame_Index
	#define SET_DSF(ds) \
		(DS_Frame_Index = (ds))
#endif


// Special stack controls (used by init and GC):
#define DS_TERMINATE	(SERIES_TAIL(DS_Series) = DSP+1);

// Access value at given stack location:
#define DS_VALUE(d)		(&DS_Base[d])

// Stack pointer based actions:
#define DS_TOP			(&DS_Base[DSP])

#define DS_POP_INTO(v) \
	do { \
		*(v) = *DS_TOP; \
		DS_DROP; \
	} while (0)

#define DS_DROP			(DSP--)
#define DS_GET(d)		(&DS_Base[d])
#define DS_PUSH(v)		(DS_Base[++DSP]=*(v))		// atomic

#define DS_PUSH_TRASH_SAFE \
	(++DSP, SET_TRASH_SAFE(DS_TOP))

#define DS_PUSH_TRASH \
	(++DSP, SET_TRASH(DS_TOP))

#define DS_PUSH_UNSET	SET_UNSET(&DS_Base[++DSP])	// atomic
#define DS_PUSH_NONE	SET_NONE(&DS_Base[++DSP])	// atomic
#define DS_PUSH_TRUE	VAL_SET(&DS_Base[++DSP], REB_LOGIC), \
						VAL_LOGIC(&DS_Base[DSP]) = TRUE // not atomic
#define DS_PUSH_INTEGER(n)	VAL_SET(&DS_Base[++DSP], REB_INTEGER), \
						VAL_INT64(&DS_Base[DSP]) = n // not atomic
#define DS_PUSH_DECIMAL(n)	VAL_SET(&DS_Base[++DSP], REB_DECIMAL), \
						VAL_DECIMAL(&DS_Base[DSP]) = n // not atomic

// Reference from ds that points to current return value:
#define D_OUT			DSF_OUT(call_->dsf)
#define D_ARG(n)		DSF_ARG(call_->dsf, (n))
#define D_REF(n)		(!IS_NONE(D_ARG(n)))

// Reference from current DSF index:
#define DS_ARG_BASE		(DSF+DSF_SIZE)
#define DS_ARG(n)		DSF_ARG(DSF, n)
#define DS_REF(n)		(!IS_NONE(DS_ARG(n)))
#define DS_ARGC			(DSP-DS_ARG_BASE)

#define DS_OUT		(&DS_Base[DSF])
