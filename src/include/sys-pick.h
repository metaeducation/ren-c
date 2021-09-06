//
//  File: %sys-pick.h
//  Summary: "Definitions for Processing Sequence Picking/Poking"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// When a path like `a/(b + c)/d` is evaluated, it moves in steps.  The
// evaluative result of chaining the prior steps is offered as input to
// the next step.  The path evaluator `Eval_Path_Throws` delegates steps to
// type-specific "(P)ath (D)ispatchers" with names like PD_Context,
// PD_Array, etc.
//
// R3-Alpha left several open questions about the handling of paths.  One
// of the trickiest regards the mechanics of how to use a SET-PATH! to
// write data into native structures when more than one path step is
// required.  For instance:
//
//     >> gob/size
//     == 10x20
//
//     >> gob/size/x: 304
//     >> gob/size
//     == 10x304
//
// Because GOB! stores its size as packed bits that are not a full PAIR!,
// the `gob/size` path dispatch can't give back a pointer to a REBVAL* to
// which later writes will update the GOB!.  It can only give back a
// temporary value built from its internal bits.  So workarounds are needed,
// as they are for a similar situation in trying to set values inside of
// C arrays in STRUCT!.
//
// The way the workaround works involves allowing a SET-PATH! to run forward
// and write into a temporary value.  Then in these cases the temporary
// REBVAL is observed and used to write back into the native bits before the
// SET-PATH! evaluation finishes.  This means that it's not currently
// prohibited for the effect of a SET-PATH! to be writing into a temporary.
//
// Further, the `value` slot is writable...even when it is inside of the path
// that is being dispatched:
//
//     >> code: compose [(make set-path! [12-Dec-2012 day]) 1]
//     == [12-Dec-2012/day: 1]
//
//     >> do code
//
//     >> probe code
//     [1-Dec-2012/day: 1]
//
// Ren-C has largely punted on resolving these particular questions in order
// to look at "more interesting" ones.  However, names and functions have
// been updated during investigation of what was being done.
//


#define PVS_PICKER(pvs) \
    pvs->u.path.picker



// For efficiency, native PICK-POKE* implementations reuse the frame (this is
// somewhat comparable to R3-Alpha's "PVS" struct, reused for all the path
// dispatches...but with the added protections frames have with the GC).
//
// For pokes, the poke location of the value that is doing the chaining to
// another pickpoke needs to be preserved...because the bits in the container
// may need to be updated for some immediate types, as their storage is
// actually in the container.
//
inline static REB_R Run_Pickpoke_Dispatch(
    REBFRM *frame_,
    const REBSYM *verb,
    const REBVAL *new_location
){
    Copy_Cell(DS_PUSH(), D_ARG(1));
    Copy_Cell(RESET(D_ARG(1)), new_location);
    REB_R r = Run_Generic_Dispatch(D_ARG(1), frame_, verb);
    Copy_Cell(RESET(D_ARG(1)), DS_TOP);
    DS_DROP();
    return r;
}
