//
//  File: %sys-hooks.h
//  Summary: {Function Pointer Definitions, defined before %tmp-internals.h}
//  Project: "Ren-C Interpreter and Run-time"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
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
// These are function pointers that need to be defined early, before the
// aggregated forward declarations for the core.
//


// The REB_R type is a REBVAL* but with the idea that it is legal to hold
// types like REB_R_THROWN, etc.  This helps document interface contract.
//
typedef const REBVAL *REB_R;


// PER-TYPE COMPARE HOOKS, to support GREATER?, EQUAL?, LESSER?...
//
// Every datatype should have a comparison function, because otherwise a
// block containing an instance of that type cannot SORT.  Like the
// generic dispatchers, compare hooks are done on a per-class basis, with
// no overrides for individual types (only if they are the only type in
// their class).
//
typedef REBINT (COMPARE_HOOK)(
    noquote(const Cell*) a,
    noquote(const Cell*) b,
    bool strict
);


// PER-TYPE MAKE HOOKS: for `make datatype def`
//
// These functions must return a REBVAL* to the type they are making
// (either in the output cell given or an API cell)...or they can return
// R_THROWN if they throw.  (e.g. `make object! [return]` can throw)
//
typedef REB_R (MAKE_HOOK)(
    REBVAL *out,
    enum Reb_Kind kind,
    option(const REBVAL*) opt_parent,
    const REBVAL *def
);


// PER-TYPE TO HOOKS: for `to datatype value`
//
// These functions must return a REBVAL* to the type they are making
// (either in the output cell or an API cell).  They are NOT allowed to
// throw, and are not supposed to make use of any binding information in
// blocks they are passed...so no evaluations should be performed.
//
// !!! Note: It is believed in the future that MAKE would be constructor
// like and decided by the destination type, while TO would be "cast"-like
// and decided by the source type.  For now, the destination decides both,
// which means TO-ness and MAKE-ness are a bit too similar.
//
typedef REB_R (TO_HOOK)(REBVAL*, enum Reb_Kind, const REBVAL*);


// PER-TYPE MOLD HOOKS: for `mold value` and `form value`
//
// Note: ERROR! may be a context, but it has its own special FORM-ing
// beyond the class (falls through to ANY-CONTEXT! for mold), and BINARY!
// has a different handler than strings.  So not all molds are driven by
// their class entirely.
//
typedef void (MOLD_HOOK)(REB_MOLD *mo, noquote(const Cell*) v, bool form);


// These definitions are needed in %sys-rebval.h, and can't be put in
// %sys-rebact.h because that depends on Reb_Array, which depends on
// Reb_Series, which depends on values... :-/

// C function implementing a native ACTION!
//
typedef REB_R (*REBNAT)(REBFRM *frame_);
#define REBNATIVE(n) \
    REB_R N_##n(REBFRM *frame_)

//
// PER-TYPE GENERIC HOOKS: e.g. for `append value x` or `select value y`
//
// This is using the term in the sense of "generic functions":
// https://en.wikipedia.org/wiki/Generic_function
//
// The current assumption (rightly or wrongly) is that the handler for
// a generic action (e.g. APPEND) doesn't need a special hook for a
// specific datatype, but that the class has a common function.  But note
// any behavior for a specific type can still be accomplished by testing
// the type passed into that common hook!
//
typedef REB_R (GENERIC_HOOK)(REBFRM *frame_, const REBSYM *verb);
#define REBTYPE(n) \
    REB_R T_##n(REBFRM *frame_, const REBSYM *verb)


// PER-TYPE PATH HOOKS: for `a/b`, `:a/b`, `a/b:`, `pick a b`, `poke a b`
//
typedef REB_R (PATH_HOOK)(REBPVS *pvs, const RELVAL *picker);


// Port hook: for implementing generic ACTION!s on a PORT! class
//
typedef REB_R (PORT_HOOK)(REBFRM *frame_, REBVAL *port, const REBSYM *verb);


//=//// PARAMETER ENUMERATION /////////////////////////////////////////////=//
//
// Parameter lists of composed/derived functions still must have compatible
// frames with their underlying C code.  This makes parameter enumeration of
// a derived function a 2-pass process that is a bit tricky.
//
// !!! Due to a current limitation of the prototype scanner, a function type
// can't be used directly in a function definition and have it be picked up
// for %tmp-internals.h, it has to be a typedef.
//
typedef enum {
    PHF_UNREFINED = 1 << 0  // a /refinement that takes an arg, made "normal"
} Reb_Param_Hook_Flags;
#define PHF_MASK_NONE 0
typedef bool (PARAM_HOOK)(
    const REBKEY *key,
    const REBPAR *param,
    REBFLGS flags,
    void *opaque
);
