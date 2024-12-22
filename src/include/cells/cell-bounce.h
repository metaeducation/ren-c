//
//  File: %cell-bounce.h
//  Summary: "Special Cell States Used for Trampoline Signaling"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
//       return "fail -{Error}-"
//       return rebDelegate("fail -{Error}-")
//
// * Between "weird Cell" and "weird Stub" choices, "weird Cell" is smaller
//   (4 platform pointers instead of 8).  So we go with a cell using an
//   out-of-range HEART_BYTE.
//

INLINE Value* Init_Return_Signal_Untracked(Init(Value) out, char ch) {
    Reset_Cell_Header_Noquote(
        out,
        FLAG_HEART_BYTE(REB_T_RETURN_SIGNAL) | CELL_MASK_NO_NODES
    );
    Tweak_Cell_Binding(out, UNBOUND);
    out->payload.split.one.ch = ch;
    Corrupt_Unused_Field(out->payload.split.two.corrupt);

    return out;
}

#define Init_Return_Signal(out,ch) \
    TRACK(Init_Return_Signal_Untracked((out), (ch)))

INLINE bool Is_Bounce_An_Atom(Bounce b)
  { return HEART_BYTE(cast(Value*, b)) != REB_T_RETURN_SIGNAL; }

INLINE char Bounce_Type(Bounce b) {
    assert(not Is_Bounce_An_Atom(b));
    return cast(Value*, b)->payload.split.one.ch;
}

INLINE Atom* Atom_From_Bounce(Bounce b) {
    assert(Is_Bounce_An_Atom(b));
    return cast(Atom*, b);
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
#define BOUNCE_REDO_UNCHECKED   cast(Bounce, &PG_Bounce_Redo_Unchecked)

#define C_REDO_CHECKED  'R'
#define BOUNCE_REDO_CHECKED  cast(Bounce, &PG_Bounce_Redo_Checked)

#define C_DOWNSHIFTED  'd'
#define BOUNCE_DOWNSHIFTED  cast(Bounce, &PG_Bounce_Downshifted)


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
#define BOUNCE_CONTINUE  cast(Bounce, &PG_Bounce_Continuation)


// A dispatcher may want to run a "continuation" but not be called back.
// This is referred to as delegation.
//
#define C_DELEGATION  'D'
#define BOUNCE_DELEGATE  cast(Bounce, &PG_Bounce_Delegation)


// For starters, a simple signal for suspending stacks in order to be able to
// try not using Asyncify (or at least not relying on it so heavily)
//
#define C_SUSPEND  'S'
#define BOUNCE_SUSPEND  cast(Bounce, &PG_Bounce_Suspend)


// Intrinsic typecheckers want to be able to run in the same Level as an
// action, but not overwrite the ->out cell of the level.  They motivate
// a special state for OKAY so that the L->out can be left as-is.
//
#define C_OKAY  'O'
#define BOUNCE_OKAY  cast(Bounce, &PG_Bounce_Okay)


// This signals that the evaluator is in a "thrown state".
//
#define C_THROWN  'T'
#define BOUNCE_THROWN  cast(Bounce, &PG_Bounce_Thrown)


// This signals that the evaluator is in a "thrown state".
//
#define C_FAIL  'F'
#define BOUNCE_FAIL  cast(Bounce, &PG_Bounce_Fail)


// In order to be fast, intrinsics fold their typechecking into their native
// implementation.  If that check fails, then they want to act like they
// were never called...which may mean erroring in some places, or just being
// bypassed (e.g. if used as a typechecker).  To make sure their type check
// case is cheap, they simply return this bounce value.
//
#define C_BAD_INTRINSIC_ARG  'B'
#define BOUNCE_BAD_INTRINSIC_ARG  cast(Bounce, &PG_Bounce_Downshifted)
