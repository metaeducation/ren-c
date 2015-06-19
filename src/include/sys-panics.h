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
**  Summary: REBOL Panic Values
**  Module:  sys-panics.h
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

enum reb_panics {

//  Boot Errors (very limited environment avaliable)
	RP_BOOT_DATA = 1000,	// no boot.r text found
	RP_REBVAL_ALIGNMENT,	// not aligned perfectly in memory
	RP_BAD_SIZE,			// expected size did not match
	RP_NO_BUFFER,			// buffer not yet allocated
	RP_BAD_BOOT_STRING,		// boot strings area is invalid
	RP_BAD_BOOT_TYPE_BLOCK,	// boot block is wrong size
	RP_BAD_END_TYPE_WORD,	// the end word is not correct
	RP_ACTION_OVERFLOW,		// more actions than we should have
	RE_NATIVE_BOOT,			// bad boot.r native ordering
	RP_EARLY_ERROR,			// error before error handling
	RP_BAD_END_CANON_WORD,	// END was not found
	RP_BAD_TRUE_CANON_WORD,	// TRUE was not found

//  Internal Errors (other things that could go wrong)
	RP_INTERNAL = 1100,
	RP_BAD_EVALTYPE,		// invalid datatype for evaluation
	RP_CORRUPT_MEMORY,		// Check_Memory() found a problem
	RP_HASH_OVERFLOW,		// Hash ran out of space
	RP_NO_PRINT_PTR,		// print is missing string pointer
	RP_BAD_TYPE_ACTION,		// datatype out of range in action dispatch
	RP_MAX_SCHEMES,			// Too many native schemes
	RP_BAD_PORT_ACTION,		// datatype out of range for ports
	RP_BAD_OBJ_INDEX,		// object index past tail
	RP_MISSING_END,			// block did not have an END marker
	RP_UNEXPECTED_END,		// in GC, block ended before length reached
	RP_OVER_SERIES,			// series overflow happened

//	!!! Reclaim (formerly for "ASSERT codes", which are now normal C asserts)
	RP_UNUSED_CATEGORY_1200 = 1200,

//  Datatype Errors (300 + N --indicates location)
	RP_DATATYPE = 1300,

//  Documented Errors (keep in-sync with error strings in boot.r!)
	RP_STR_BASE = 1400,
	RP_NO_MEMORY,			// not enough memory: %d bytes
	RP_BAD_WIDTH,			// invalid series width: %d %d %d
	RP_ERROR_CATCH,			// error already caught
	RP_STACK_OVERFLOW,		// data stack overflow
	RP_IO_ERROR,			// problem with IO
	RP_MAX_WORDS,			// too many words
	RP_WORD_LIST,			// word list (cache) already in use
	RP_LOCKED_SERIES,		// locked series expansion
	RP_ERROR_RECYCLED,		// the error object was gc'd!
	RP_NO_CATCH,			// top level uncaught error
	RP_NO_SAVED_STATE,		// saved state frame is missing
	RP_MAX_EVENTS,			// event queue overflow
	RP_NOT_AVAILABLE,		// not available

//	Unspecified (just count them)
	RP_MISC
};


