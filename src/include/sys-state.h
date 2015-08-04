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
**  Summary: CPU State
**  Module:  sys-state.h
**  Author:  Carl Sassenrath, @HostileFork
**  Notes:
**		Rebol is settled upon a stable and pervasive implementation
**		baseline of ANSI-C (C89).  That commitment provides certain
**		advantages.  One of the disadvantages is that there is no safe
**		way to do non-local jumps with stack unwinding (as in C++).  If
**		you've written some code that performs a raw malloc and then
**		wants to "throw" with a longjmp, that will leak the malloc.
**
**		Basically all code must be aware of "throws", and if one can
**		happen then there must be explicit handling of the cleanup.
**		This must be either at the point of the longjmp, or the moment
**		when the setjmp runs its "true" branch after a non-local jump:
**
**			http://stackoverflow.com/questions/1376085/
**
**		(Note:  If you are integrating with C++ and a longjmp crosses
**		a constructed object, abandon all hope...UNLESS you use Ren/C++.
**		It is careful to avoid this trap, and you don't want to redo
**		that work.)
**
**		!!! v-- TRIAGE NOT YET INTEGRATED IN REN/C
**
**		In order to mitigate the inherent failure of trying to emulate
**		stack unwinding via longjmp, Rebol wraps the abstraction a bit.
**		If you had allocated any series and they were in "triage" at
**		the time the "throw" happened, then those will be automatically
**		freed (the garbage collector didn't know about them yet).
**
***********************************************************************/


// "Under FreeBSD 5.2.1 and Mac OS X 10.3, setjmp and longjmp save and restore
// the signal mask. Linux 2.4.22 and Solaris 9, however, do not do this.
// FreeBSD and Mac OS X provide the functions _setjmp and _longjmp, which do
// not save and restore the signal mask."
//
// "To allow either form of behavior, POSIX.1 does not specify the effect of
// setjmp and longjmp on signal masks. Instead, two new functions, sigsetjmp
// and siglongjmp, are defined by POSIX.1. These two functions should always
// be used when branching from a signal handler."

#ifdef HAS_POSIX_SIGNAL
	#define SET_JUMP(s) sigsetjmp((s), 1)
	#define LONG_JUMP(s, v) siglongjmp((s), (v))
#else
	#define SET_JUMP(s) setjmp(s)
	#define LONG_JUMP(s, v) longjmp((s), (v))
#endif


// Structure holding the information about the last point in the stack that
// wanted to set up an opportunity to "catch" a DO ERROR! or a THROW.

typedef struct Rebol_State {
	struct Rebol_State *last_state;

	REBINT dsp;
	struct Reb_Call *dsf;
	REBCNT hold_tail;	// Tail for GC_Protect
	REBVAL error;
	REBINT gc_disable;      // Count of GC_Disables at time of Push

#ifdef HAS_POSIX_SIGNAL
	sigjmp_buf cpu_state;
#else
	jmp_buf cpu_state;
#endif
} REBOL_STATE;


// You really can't put "setjmp" in arbitrary conditional contexts, such
// as `setjmp(...) ? x : y`.  That's against the rules.  Although the
// preprocessor abuse below is a bit ugly, it helps establish that anyone
// modifying this code later not be able to avoid the truth of the limitation
// (by removing the return value of setjmp from the caller's view).
//
//		http://stackoverflow.com/questions/30416403/
//
// IMPORTANT: You must DROP_CATCH from the same scope you PUSH_CATCH from.
// Do not call PUSH_CATCH in a function, then return from that function and
// DROP_CATCH at another stack level:
//
//		"If the function that called setjmp has exited (whether by return
//		or by a different longjmp higher up the stack), the behavior is
//		undefined. In other words, only long jumps up the call stack
//		are allowed."
//
//		http://en.cppreference.com/w/c/program/longjmp

#define PUSH_CATCH(e,s) \
	do { \
		Push_Catch_Helper(s); \
		if (SET_JUMP((s)->cpu_state)) { \
			if (Catch_Error_Helper(s)) \
				*(e) = cast(const REBVAL*, &(s)->error); \
			else \
				*(e) = NULL; \
		} else \
			*(e) = NULL; \
	} while (0)

#define PUSH_CATCH_ANY(e,s) \
	do { \
		Push_Catch_Helper(s); \
		if (SET_JUMP((s)->cpu_state)) { \
			if (Catch_Any_Error_Helper(s)) \
				*(e) = cast(const REBVAL*, &(s)->error); \
			else \
				*(e) = NULL; \
		} else \
			*(e) = NULL; \
	} while (0)

#define DROP_CATCH_SAME_STACKLEVEL_AS_PUSH(s) \
	Drop_Catch_Helper((s), FALSE);
