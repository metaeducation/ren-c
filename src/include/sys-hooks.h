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


// PER-TYPE COMPARE HOOKS, to support GREATER?, EQUAL?, LESSER?...
//
// Every datatype should have a comparison function, because otherwise a
// block containing an instance of that type cannot SORT.  Like the
// generic dispatchers, compare hooks are done on a per-class basis, with
// no overrides for individual types (only if they are the only type in
// their class).
//
typedef REBINT (COMPARE_HOOK)(
    noquote(Cell(const*)) a,
    noquote(Cell(const*)) b,
    bool strict
);

// Helper for declaring a native dispatcher function
//
#define DECLARE_NATIVE(n) \
    Bounce N_##n(Frame(*) frame_)


// PER-TYPE MAKE HOOKS: for `make datatype def`
//
// These functions must return a REBVAL* to the type they are making
// (either in the output cell given or an API cell)...or they can return
// BOUNCE_THROWN if they throw.  (e.g. `make object! [return]` can throw)
//
typedef Bounce (MAKE_HOOK)(
    Frame(*) frame_,
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
typedef Bounce (TO_HOOK)(Frame(*) frame_, enum Reb_Kind, const REBVAL*);


// PER-TYPE MOLD HOOKS: for `mold value` and `form value`
//
// Note: ERROR! may be a context, but it has its own special FORM-ing
// beyond the class (falls through to ANY-CONTEXT! for mold), and BINARY!
// has a different handler than strings.  So not all molds are driven by
// their class entirely.
//
typedef void (MOLD_HOOK)(REB_MOLD *mo, noquote(Cell(const*)) v, bool form);


// Just requests what symbol a custom datatype wants to use for its type
//
typedef Symbol(const*) (SYMBOL_HOOK)(void);


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
typedef Bounce (GENERIC_HOOK)(Frame(*) frame_, Symbol(const*) verb);
#define REBTYPE(n) \
    Bounce T_##n(Frame(*) frame_, Symbol(const*) verb)


// Port hook: for implementing generic ACTION!s on a PORT! class
//
typedef Bounce (PORT_HOOK)(Frame(*) frame_, REBVAL *port, Symbol(const*) verb);


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
    Flags flags,
    void *opaque
);
