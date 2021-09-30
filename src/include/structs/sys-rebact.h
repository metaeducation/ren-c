//
//  File: %sys-rebact.h
//  Summary: "action! defs BEFORE %tmp-internals.h"
//  Section: core
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2020 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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
// See %sys-action.h for information about the workings of REBACT and ACTION!.
// This file just defines basic structures and flags.
//



#define MISC_DetailsMeta_TYPE      REBCTX*
#define MISC_DetailsMeta_CAST      CTX
#define HAS_MISC_DetailsMeta       FLAVOR_DETAILS

// Note: LINK on details is the DISPATCHER, on varlists it's KEYSOURCE


//=//// ARRAY_FLAG_IS_KEYLIST /////////////////////////////////////////////=//
//
// Context keylist arrays and Action paramlist arrays are converging, and
// this flag is used to mark them.  It's the same bit as used to mark a
// string as being a symbol, which is a forward-thinking move to aim for a
// time when single-length keylists can be represented by just a pointer to
// a symbol.
//
#define ARRAY_FLAG_IS_KEYLIST SERIES_FLAG_IS_KEYLIKE


//=//// DETAILS_FLAG_POSTPONES_ENTIRELY ///////////////////////////////////=//
//
// A postponing operator causes everything on its left to run before it will.
// Like a deferring operator, it is only allowed to appear after the last
// parameter of an expression except it closes out *all* the parameters on
// the stack vs. just one.
//
#define DETAILS_FLAG_POSTPONES_ENTIRELY \
    SERIES_FLAG_24


//=//// DETAILS_FLAG_IS_BARRIER ///////////////////////////////////////////=//
//
// Special action property set with TWEAK.  Used by |
//
// The "expression barrier" was once a built-in type (BAR!) in order to get
// a property not possible to achieve with functions...that it would error
// if it was used during FULFILL_ARG and would be transparent in evaluation.
//
// Transparency was eventually generalized as "invisibility".  But attempts
// to intuit the barrier-ness from another property (e.g. "enfix but no args")
// were confusing.  It seems an orthogonal feature in its own right, so it
// was added to the TWEAK list pending a notation in function specs.
//
#define DETAILS_FLAG_IS_BARRIER \
    SERIES_FLAG_25


//=//// DETAILS_FLAG_DEFERS_LOOKBACK //////////////////////////////////////=//
//
// Special action property set with TWEAK.  Used by THEN, ELSE, and ALSO.
//
// Tells you whether a function defers its first real argument when used as a
// lookback.  Because lookback dispatches cannot use refinements, the answer
// is always the same for invocation via a plain word.
//
#define DETAILS_FLAG_DEFERS_LOOKBACK \
    SERIES_FLAG_26


//=//// DETAILS_FLAG_QUOTES_FIRST /////////////////////////////////////////=//
//
// This is a calculated property, which is cached by Make_Action().
//
// This is another cached property, needed because lookahead/lookback is done
// so frequently, and it's quicker to check a bit on the function than to
// walk the parameter list every time that function is called.
//
#define DETAILS_FLAG_QUOTES_FIRST \
    SERIES_FLAG_27


//=//// DETAILS_FLAG_SKIPPABLE_FIRST //////////////////////////////////////=//
//
// This is a calculated property, which is cached by Make_Action().
//
// It is good for the evaluator to have a fast test for knowing if the first
// argument to a function is willing to be skipped, as this comes into play
// in quote resolution.  (It's why `x: default [10]` can have default looking
// for SET-WORD! and SET-PATH! to its left, but `case [... default [x]]` can
// work too when it doesn't see a SET-WORD! or SET-PATH! to the left.)
//
#define DETAILS_FLAG_SKIPPABLE_FIRST \
    SERIES_FLAG_28


//=//// DETAILS_FLAG_IS_NATIVE ////////////////////////////////////////////=//
//
// Native functions are flagged that their dispatcher represents a native in
// order to say that their ACT_DETAILS() follow the protocol that the [0]
// slot is "equivalent source" (may be a TEXT!, as in user natives, or a
// BLOCK!).  The [1] slot is a module or other context into which APIs like
// rebValue() etc. should consider for binding, in addition to lib.  A BLANK!
// in the 1 slot means no additional consideration...bind to lib only.
//
// Note: This was tactially set to be the same as SERIES_INFO_HOLD to make it
// possible to branchlessly mask in the bit to stop frames from being mutable
// by user code once native code starts running.  Shuffling made this no
// longer possible, so that was dropped...but it could be brought back.
//
#define DETAILS_FLAG_IS_NATIVE \
    SERIES_FLAG_29


//=//// DETAILS_FLAG_ENFIXED //////////////////////////////////////////////=//
//
// An enfix function gets its first argument from its left.  For a time, this
// was the property of a binding and not an ACTION! itself.  This was an
// attempt at simplification which caused more problems than it solved.
//
#define DETAILS_FLAG_ENFIXED \
    SERIES_FLAG_30


//=//// DETAILS_FLAG_31 ///////////////////////////////////////////////////=//
//
#define DETAILS_FLAG_31 \
    SERIES_FLAG_31


// These are the flags which are scanned for and set during Make_Action
//
#define DETAILS_MASK_CACHED \
    (DETAILS_FLAG_QUOTES_FIRST | DETAILS_FLAG_SKIPPABLE_FIRST)

// These flags should be copied when specializing or adapting.  They may not
// be derivable from the paramlist (e.g. a native with no RETURN does not
// track if it requotes beyond the paramlist).
//
#define DETAILS_MASK_INHERIT \
    (DETAILS_FLAG_DEFERS_LOOKBACK | DETAILS_FLAG_POSTPONES_ENTIRELY)


#define SET_ACTION_FLAG(act,name) \
    SET_SUBCLASS_FLAG(DETAILS, ACT_IDENTITY(act), name)

#define GET_ACTION_FLAG(act,name) \
    GET_SUBCLASS_FLAG(DETAILS, ACT_IDENTITY(act), name)

#define CLEAR_ACTION_FLAG(act,name) \
    CLEAR_SUBCLASS_FLAG(DETAILS, ACT_IDENTITY(act), name)

#define NOT_ACTION_FLAG(act,name) \
    NOT_SUBCLASS_FLAG(DETAILS, ACT_IDENTITY(act), name)


// Includes SERIES_FLAG_DYNAMIC because an action's paramlist is always
// allocated dynamically, in order to make access to the archetype and the
// parameters faster than ARR_AT().  See code for ACT_KEY(), etc.
//
// !!! This used to include SERIES_FLAG_FIXED_SIZE for both.  However, that
// meant the mask was different for paramlists and context keylists (which
// are nearing full convergence).  And on the details array, it got in the
// way of HIJACK, which may perform expansion.  So that was removed.
//
#define SERIES_MASK_PARAMLIST SERIES_MASK_VARLIST

#define SERIES_MASK_DETAILS \
    (NODE_FLAG_NODE \
        | SERIES_FLAG_MISC_NODE_NEEDS_MARK  /* meta */ \
        | FLAG_FLAVOR(DETAILS) \
        /* LINK is dispatcher, a c function pointer, should not mark */ \
        | SERIES_FLAG_INFO_NODE_NEEDS_MARK  /* exemplar */ )

#define SERIES_MASK_PARTIALS \
    (NODE_FLAG_NODE \
        | FLAG_FLAVOR(PARTIALS) \
        /* LINK is unused at this time */ \
        /* MISC is unused at this time (could be paramlist cache?) */)
