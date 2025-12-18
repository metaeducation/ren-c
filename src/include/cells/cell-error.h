//
//  file: %cell-context.h
//  summary: "Context definitions AFTER including %tmp-internals.h"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2025 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
// REBOL is a trademark of REBOL Technologies
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
// An ERROR! in Ren-C is an antiform failure state.
//
// It is specifically an "unstable antiform"...which means it not only can't
// be stored in lists like BLOCK!...it also can't be stored in variables.
// Errors will be elevated to an exceptions if you try to assign them or
// otherwise use them without going through some ^META operation to triage
// them.  While errors are in the ^META state, they it can be assigned to
// variables or put in blocks, until they are UNMETA'd back to failure again.
//
//=//// NOTES ////////////////////////////////////////////////////////////=//
//
// * Some ERROR!s with specific IDs (like VETO and DONE) are used as signals
//   to indicate special handling in evaluative slots, out-of-band from the
//   normal values that might appear there.  This signaling use starts to blur
//   the line a little bit abot what an "error" is...but the defining
//   characteristic is that they will promote to panics if not triaged.
//
//   (Compare with what would happen if you made an ERROR! with [id = 'pack]
//   and a BLOCK! in [error.arg1], and tried to use that to simulate a signal
//   for multi-return.  It could be made to work if a callsite was aware of
//   the idea you were using an error for that purpose, and reacted to it.
//   But it wouldn't gracefully decay to its first value if a receiving site
//   didn't know about your "pack error protocol".)
//


//=//// ERROR FIELD ACCESS ////////////////////////////////////////////////=//
//
// Errors are a subtype of ANY-CONTEXT? which follow a standard layout.
// That layout is in %specs/sysobj.r as standard/error.
//
// Historically errors could have a maximum of 3 arguments, with the fixed
// names of `arg1`, `arg2`, and `arg3`.  They would also have a numeric code
// which would be used to look up a a formatting block, which would contain
// a block for a message with spots showing where the args were to be inserted
// into a message.  These message templates can be found in %specs/errors.r
//
// Ren-C is exploring the customization of user errors to be able to provide
// arbitrary named arguments and message templates to use them.  It is
// a work in progress, but refer to the FAIL native, the corresponding
// `panic()` C macro in the source, and the various routines in %c-error.c

#define ERR_VARS(e) \
    cast(ERROR_VARS*, Varlist_Slots_Head(e))

#define VAL_ERR_VARS(v) \
    ERR_VARS(Cell_Varlist(v))

INLINE void Force_Location_Of_Error(Error* error, Level* L) {
    ERROR_VARS *vars = ERR_VARS(error);

    DECLARE_STABLE (where);
    require (
      Read_Slot(where, &vars->where)
    );
    if (Is_Nulled(where))
        Set_Location_Of_Error(error, L);
}


//=//// NON-ANTIFORM WARNING! STATE ///////////////////////////////////////=//
//
// It may be that ERROR! becomes simply an antiform of any generic OBJECT!
// (a bit like "armed" errors vs. "disarmed" objects in Rebol2).
//
// However, the ERROR! type has historically been a specially formatted
// subtype of OBJECT!.  Just to get things working for starters, there had to
// be a name for this type of object when not an antiform...so it just got
// called WARNING!.  It's not a terrible name, but we can see how it feels.
//

#define Init_Warning(v,c) \
    Init_Context_Cell((v), TYPE_WARNING, (c))

INLINE Value* Failify(Exact(Value*) v) {  // WARNING! => ERROR!
    assert(Heart_Of(v) == TYPE_WARNING and LIFT_BYTE(v) == NOQUOTE_2);
    Force_Location_Of_Error(Cell_Error(v), TOP_LEVEL);  // ideally a noop
    Unstably_Antiformize_Unbound_Fundamental(v);
    assert(Is_Error(v));
    return v;
}


//=//// "VETO" ERRORS (error.id = 'veto) //////////////////////////////////=//
//
// VETO error antiforms signal a desire to cancel the operation that requested
// the evaluation.  Unlike VOID which opts out of slots but keeps running,
// many operations that observe a VETO will return NULL:
//
//     >> reduce ["a" void "b"]
//     == ["a" "b"]
//
//     >> reduce ["a" veto "b"]
//     == ~null~  ; anti
//
// In PARSE, a GROUP! that evaluates to VETO doesn't cancel the whole parse,
// but rather just fails that specific GROUP!'s combinator, rolling over to
// the next alternate.
//
//     >> parse [a b] ['a (if 1 < 2 [veto]) 'b | (print "alternate!") 'a 'b]
//     alternate!
//     == 'b
//
// You can produce a VETO from a NULL using OPT:VETO, shorthanded as ?!, as
// a natural progression from the ? shorthand for plain voiding OPT:
//
//     >> reduce ["a" ? null "b"]
//     == ["a" "b"]
//
//     >> reduce ["a" ?! null "b"]
//     == ~null~  ; anti
//

INLINE bool Is_Error_Veto_Signal(Error* error) {
    ERROR_VARS *vars = ERR_VARS(error);

    DECLARE_STABLE (id);
    require (
      Read_Slot(id, &vars->id)
    );
    if (not Is_Word(id))
        return false;
    return Word_Id(id) == SYM_VETO;
}


//=//// "DONE" ERRORS (error.id = 'done) //////////////////////////////////=//
//
// DONE error antiforms report that an enumeration is exhausted and has no
// further items to give back.  They're used by YIELD or functions that want
// to act as generators for looping constructs like FOR-EACH or MAP:
//
//     count: 0
//     make-one-thru-five: func [
//         return: [error! integer!]
//     ][
//          if count = 5 [return done]
//          return count: count + 1
//     ]
//
//     >> map 'i make-one-thru-five/ [i * 10]
//     == [10 20 30 40 50]
//
// Using an unstable antiform which can't be stored in a variable means that
// the generator can return anything that can be stored as a variable in-band.
//

INLINE bool Is_Error_Done_Signal(Error* error) {
    ERROR_VARS *vars = ERR_VARS(error);

    DECLARE_STABLE (id);
    require (
      Read_Slot(id, &vars->id)
    );
    if (not Is_Word(id))
        return false;
    return Word_Id(id) == SYM_DONE;
}
