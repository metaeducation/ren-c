//
//  File: %sys-hooks.h
//  Summary: "Function Pointer Definitions, defined before %tmp-internals.h"
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
    const Cell* a,
    const Cell* b,
    bool strict
);


//
// EXTENSION COLLATOR FUNCTION DEFINITION
//
// Rebol Extensions generate DLLs (or embed into the EXE) with a function
// that does initialization.  But that init function does not actually
// decompress any of the script or spec code, make any natives, or run
// any startup.  It just returns an aggregate of all the information that
// would be needed to make the extension module.  So it is called a
// "collator", and it calls the API `rebExtensionCollate_internal()`
//
// !!! The result may become an ACTION! as opposed to a BLOCK! of handle
// values, but this is a work in progress.
//
#if defined(_WIN32)
    typedef Value* (__cdecl ExtensionCollator)(RebolApiTable*);
#else
    typedef Value* (ExtensionCollator)(RebolApiTable*);
#endif
#define IDX_COLLATOR_SCRIPT 0
#define IDX_COLLATOR_SCRIPT_NUM_CODEPOINTS 1
#define IDX_COLLATOR_CFUNCS 2
#define IDX_COLLATOR_MAX 3


// PER-TYPE MAKE HOOKS: for `make datatype def`
//
// These functions must return a Value* to the type they are making
// (either in the output cell given or an API cell)...or they can return
// BOUNCE_THROWN if they throw.  (e.g. `make object! [return ...]` can throw)
//
typedef Bounce (MAKE_HOOK)(
    Level* level_,
    Kind kind,
    Option(const Value*) opt_parent,
    const Value* def
);


// PER-TYPE TO HOOKS: for `to datatype value`
//
// These functions must return a Value* to the type they are making
// (either in the output cell or an API cell).  They are NOT allowed to
// throw, and are not supposed to make use of any binding information in
// blocks they are passed...so no evaluations should be performed.
//
// !!! Note: It is believed in the future that MAKE would be constructor
// like and decided by the destination type, while TO would be "cast"-like
// and decided by the source type.  For now, the destination decides both,
// which means TO-ness and MAKE-ness are a bit too similar.
//
typedef Bounce (TO_HOOK)(Level* level_, Kind, const Value*);


// PER-TYPE MOLD HOOKS: for `mold value` and `form value`
//
// Note: ERROR! may be a context, but it has its own special FORM-ing
// beyond the class (falls through to ANY-CONTEXT? for mold), and BINARY!
// has a different handler than strings.  So not all molds are driven by
// their class entirely.
//
typedef void (MOLD_HOOK)(Molder* mo, const Cell* v, bool form);


// Just requests what symbol a custom datatype wants to use for its type
//
typedef const Symbol* (SYMBOL_HOOK)(void);


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
typedef Bounce (GENERIC_HOOK)(Level* level_, const Symbol* verb);
#define REBTYPE(n) \
    Bounce T_##n(Level* level_, const Symbol* verb)


// Port hook: for implementing generic ACTION!s on a PORT! class
//
typedef Bounce (PORT_HOOK)(Level* level_, Value* port, const Symbol* verb);


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
    const Key* key,
    const Param* param,
    Flags flags,
    void *opaque
);
