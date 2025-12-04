//
//  file: %sys-bounce.h
//  summary: "Special States Used for Trampoline/Dispatcher Signaling"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2022-2024 Ren-C Open Source Contributors
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// An arbitrary cell pointer may be returned from a native--in which case it
// will be checked to see if it is thrown and processed if it is, or checked
// to see if it's an unmanaged API handle and released if it is...ultimately
// putting the cell into L->out.
//
// Other special instructions need to be encoded somehow:
//
// * We don't want to use UTF-8 signals like `return "C"` for BOUNCE_CONTINUE.
//   That would miss out on the opportunity to make these equivalent:
//
//       return "panic -[Error]-"
//       return rebDelegate("panic -[Error]-")
//
// * Using a Cell would put us in contention with discerning between legitmate
//   Cells and these signals.
//
// The cleanest choice was deemed to be using the BASE_BYTE_WILD byte, which
// doesn't carry BASE_FLAG_CELL.  We can make simple two byte global pointers
// for the instances, that can be tested via switch() on their bytes or by
// comparison to direct values.
//
// (Performance testing should be done to figure out what techniques are
// faster.  No real A/B testing has been done as of yet.)
//

INLINE void Init_Bounce_Wild(WildTwo out, char ch) {
    assert(out[0] == 0);  // is there any good reason Erase_Bounce_Wild()
    assert(out[1] == 0);
    out[0] = BASE_BYTE_WILD;
    out[1] = ch;
}

INLINE void Erase_Bounce_Wild(WildTwo out) {
    out[0] = 0;
    out[1] = 0;
}

INLINE bool Is_Bounce_A_Cell(Bounce b) {
    const void* p = cast(const void*, b);
    return (
        FIRST_BYTE(p) & (BASE_BYTEMASK_0x80_NODE | BASE_BYTEMASK_0x08_CELL)
    ) == (BASE_BYTEMASK_0x80_NODE | BASE_BYTEMASK_0x08_CELL);
}

INLINE bool Is_Bounce_Wild(Bounce b) {
    const void* p = cast(const void*, b);
    return FIRST_BYTE(p) == BASE_BYTE_WILD;
}

INLINE char Bounce_Type(Bounce b) {
    assert(Is_Bounce_Wild(b));

    const void* p = cast(const void*, b);
    return SECOND_BYTE(p);
}

INLINE Value* Value_From_Bounce(Bounce b) {
    assert(Is_Bounce_A_Cell(b));
    return cast(Value*, m_cast(void*, b));
}


// If Eval_Core gets back an REB_R_REDO from a dispatcher, it will re-execute
// the L->phase in the frame.  This function may be changed by the dispatcher
// from what was originally called.
//
// Note it is not safe to let arbitrary user code change values in a
// frame from expected types, and then let those reach an underlying native
// who thought the types had been checked.
//
#define C_REDO_UNCHECKED  'r'
#define BOUNCE_REDO_UNCHECKED   x_cast(Bounce, &g_bounce_redo_unchecked)

#define C_REDO_CHECKED  'R'
#define BOUNCE_REDO_CHECKED  x_cast(Bounce, &g_bounce_redo_checked)

#define C_DOWNSHIFTED  'd'
#define BOUNCE_DOWNSHIFTED  x_cast(Bounce, &g_bounce_downshifted)


// Continuations are used to mitigate the problems that occur when the C stack
// contains a mirror of frames corresponding to the frames for each stack
// level.  Avoiding this means that routines that would be conceived as doing
// a recursion instead return to the evaluator with a new request.  This helps
// avoid crashes from C stack overflows and has many other advantages.  For a
// similar approach and explanation, see:
//
// https://en.wikipedia.org/wiki/Stackless_Python
//
// What happens is that when a BOUNCE_CONTINUE comes back via the C `return`
// for a native, that native's C stack variables are all gone.  But the heap
// allocated Level stays intact and in the Rebol stack trace.  The native's C
// function will be called back again when the continuation finishes.
//
#define C_CONTINUATION  'C'
#define BOUNCE_CONTINUE  x_cast(Bounce, &g_bounce_continuation)


// A dispatcher may want to run a "continuation" but not be called back.
// This is referred to as delegation.
//
#define C_DELEGATION  'D'
#define BOUNCE_DELEGATE  x_cast(Bounce, &g_bounce_delegation)


// For starters, a simple signal for suspending stacks in order to be able to
// try not using Asyncify (or at least not relying on it so heavily)
//
#define C_SUSPEND  'S'
#define BOUNCE_SUSPEND  x_cast(Bounce, &g_bounce_suspend)


// Intrinsic typecheckers want to be able to run in the same Level as an
// action, but not overwrite the ->out cell of the level.  They motivate
// a special state for OKAY so that the L->out can be left as-is.
//
// We don't make this a Wild so it's out of band from "irreducible" bounces,
// e.g. those that can't just resolve to a state in the output cell.
//
// (A direct address is used, so it can signal "Okay" even if the code that
// assigns `okay: ~okay~` hasn't even run yet)
//
#define BOUNCE_OKAY  Stub_Cell(&g_lib_patches[SYM_OKAY])


// This signals that the evaluator is in a "thrown state".
//
#define C_THROWN  'T'
#define BOUNCE_THROWN  x_cast(Bounce, &g_bounce_thrown)
