//
//  File: %struct-action.h
//  Summary: "Action structure definitions preceding %tmp-internals.h"
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

#if CPLUSPLUS_11
    struct Details : public Array {};
    struct Action : public Flex {};
    struct Phase : public Action {};
#else
    typedef Array Details;
    typedef Flex Action;
    typedef Action Phase;
#endif


#define MISC_DetailsAdjunct_TYPE      Context*
#define HAS_MISC_DetailsAdjunct       FLAVOR_DETAILS

// Note: LINK on details is the DISPATCHER, on varlists it's KEYSOURCE


//=//// ARRAY_FLAG_IS_KEYLIST /////////////////////////////////////////////=//
//
// Context keylist arrays and Action paramlist arrays are converging, and
// this flag is used to mark them.  It's the same bit as used to mark a
// string as being a symbol, which is a forward-thinking move to aim for a
// time when single-length keylists can be represented by just a pointer to
// a symbol.
//
#define ARRAY_FLAG_IS_KEYLIST FLEX_FLAG_IS_KEYLIKE


//=//// DETAILS_FLAG_POSTPONES_ENTIRELY ///////////////////////////////////=//
//
// A postponing operator causes everything on its left to run before it will.
// Like a deferring operator, it is only allowed to appear after the last
// parameter of an expression except it closes out *all* the parameters on
// the stack vs. just one.
//
#define DETAILS_FLAG_POSTPONES_ENTIRELY \
    FLEX_FLAG_24


//=//// DETAILS_FLAG_25 ///////////////////////////////////////////////////=//
//
#define DETAILS_FLAG_25 \
    FLEX_FLAG_25


//=//// DETAILS_FLAG_DEFERS_LOOKBACK //////////////////////////////////////=//
//
// Special action property set with TWEAK.  Used by THEN, ELSE, and ALSO.
//
// Tells you whether a function defers its first real argument when used as a
// lookback.  Because lookback dispatches cannot use refinements, the answer
// is always the same for invocation via a plain word.
//
#define DETAILS_FLAG_DEFERS_LOOKBACK \
    FLEX_FLAG_26


//=//// DETAILS_FLAG_27 ///////////////////////////////////////////////////=//
//
#define DETAILS_FLAG_27 \
    FLEX_FLAG_27


//=//// DETAILS_FLAG_28 ///////////////////////////////////////////////////=//
//
#define DETAILS_FLAG_28 \
    FLEX_FLAG_28


//=//// DETAILS_FLAG_IS_NATIVE ////////////////////////////////////////////=//
//
// Native functions are flagged that their dispatcher represents a native in
// order to say that their Phase_Details() follow the protocol that the [0]
// slot is "equivalent source" (may be a TEXT!, as in user natives, or a
// BLOCK!).  The [1] slot is a module or other context into which APIs like
// rebValue() etc. should consider for binding, in addition to lib.  A BLANK!
// in the 1 slot means no additional consideration...bind to lib only.
//
// Note: This was tactially set to be the same as FLEX_INFO_HOLD to make it
// possible to branchlessly mask in the bit to stop frames from being mutable
// by user code once native code starts running.  Shuffling made this no
// longer possible, so that was dropped...but it could be brought back.
//
#define DETAILS_FLAG_IS_NATIVE \
    FLEX_FLAG_29

typedef enum {
    NATIVE_NORMAL,
    NATIVE_COMBINATOR,
    NATIVE_INTRINSIC
} NativeType;


//=//// DETAILS_FLAG_30 ///////////////////////////////////////////////////=//
//
#define DETAILS_FLAG_30 \
    FLEX_FLAG_30


//=//// DETAILS_FLAG_31 ///////////////////////////////////////////////////=//
//
#define DETAILS_FLAG_31 \
    FLEX_FLAG_31


// These flags should be copied when specializing or adapting.  They may not
// be derivable from the paramlist (e.g. a native with no RETURN does not
// track if it requotes beyond the paramlist).
//
#define DETAILS_MASK_INHERIT \
    (DETAILS_FLAG_DEFERS_LOOKBACK | DETAILS_FLAG_POSTPONES_ENTIRELY)


#define Set_Action_Flag(act,name) \
    Set_Subclass_Flag(DETAILS, ACT_IDENTITY(act), name)

#define Get_Action_Flag(act,name) \
    Get_Subclass_Flag(DETAILS, ACT_IDENTITY(act), name)

#define Clear_Action_Flag(act,name) \
    Clear_Subclass_Flag(DETAILS, ACT_IDENTITY(act), name)

#define Not_Action_Flag(act,name) \
    Not_Subclass_Flag(DETAILS, ACT_IDENTITY(act), name)


// Includes FLEX_FLAG_DYNAMIC because an action's paramlist is always
// allocated dynamically, in order to make access to the archetype and the
// parameters faster than Array_At().  See code for ACT_KEY(), etc.
//
// !!! This used to include FLEX_FLAG_FIXED_SIZE for both.  However, that
// meant the mask was different for paramlists and context keylists (which
// are nearing full convergence).  And on the details array, it got in the
// way of HIJACK, which may perform expansion.  So that was removed.
//
#define FLEX_MASK_PARAMLIST FLEX_MASK_VARLIST

#define FLEX_MASK_DETAILS \
    (NODE_FLAG_NODE \
        | FLEX_FLAG_MISC_NODE_NEEDS_MARK  /* meta */ \
        | FLAG_FLAVOR(DETAILS) \
        /* LINK is dispatcher, a c function pointer, should not mark */ \
        | FLEX_FLAG_INFO_NODE_NEEDS_MARK  /* exemplar */ )

#define FLEX_MASK_PARTIALS \
    (NODE_FLAG_NODE \
        | FLAG_FLAVOR(PARTIALS) \
        /* LINK is unused at this time */ \
        /* MISC is unused at this time (could be paramlist cache?) */)


//=//// PARAMETER CLASSES ////////////////////////////////////////////////=//

typedef enum {
    PARAMCLASS_0,  // temporary state for Option(ParamClass)

    // `PARAMCLASS_NORMAL` is cued by an ordinary WORD! in the function spec
    // to indicate that you would like that argument to be evaluated normally.
    //
    //     >> foo: function [a] [print [{a is} a]]
    //
    //     >> foo 1 + 2
    //     a is 3
    //
    PARAMCLASS_NORMAL,

    PARAMCLASS_RETURN,

    PARAMCLASS_OUTPUT,

    // `PARAMCLASS_HARD` is cued by a quoted WORD! in the function spec
    // dialect.  It indicates that a single value of content at the callsite
    // should be passed through *literally*, without any evaluation:
    //
    //     >> foo: function ['a] [print [{a is} a]]
    //
    //     >> foo (1 + 2)
    //     a is (1 + 2)
    //
    //     >> foo :(1 + 2)
    //     a is :(1 + 2)
    //
    //
    PARAMCLASS_HARD,

    // `PARAMCLASS_MEDIUM` is cued by a QUOTED GET-WORD! in the function spec
    // dialect.  It quotes with the exception of GET-GROUP!, GET-WORD!, and
    // GET-TUPLE!...which will be evaluated:
    //
    //     >> foo: function [':a] [print [{a is} a]
    //
    //     >> foo (1 + 2)
    //     a is (1 + 2)
    //
    //     >> foo :(1 + 2)
    //     a is 3
    //
    // Although possible to implement medium quoting with hard quoting, it is
    // a convenient way to allow callers to "escape" a quoted context when
    // they need to.
    //
    PARAMCLASS_MEDIUM,

    // `PARAMCLASS_SOFT` is cued by a PLAIN GET-WORD!.  It's a more nuanced
    // version of PARAMCLASS_MEDIUM which is escapable but will defer to enfix.
    // This covers cases like:
    //
    //     if ok [...] then :(func [...] [...])  ; want escapability
    //     if ok [...] then x -> [...]  ; but want enfix -> lookback to win
    //
    // Hence it is the main mode of quoting for branches.  It would be
    // unsuitable for cases like OF, however, due to this problem:
    //
    //     integer! = kind of 1  ; want left quoting semantics on `kind` WORD!
    //     integer! = :(first [kind length]) of 1  ; want escapability
    //
    // OF wants its left hand side to be escapable, however it wants the
    // quoting behavior to out-prioritize the completion of enfix on the
    // left.  Contrast this with how THEN wants the enfix on the right to
    // win out ahead of its quoting.
    //
    // This is a subtlety that most functions don't have to worry about, so
    // using soft quoting is favored to medium quoting for being one less
    // character to type.
    //
    PARAMCLASS_SOFT,

    // `PARAMCLASS_META` is the only parameter type that can accept unstable
    // isotopes.  Antiforms become quasiforms when they are an argument, and
    // all other types receive one added quote level.
    //
    //     >> foo: function [^a] [print [{a is} a]
    //
    //     >> foo 1 + 2
    //     a is '3
    //
    //     >> foo get/any $asdfasfasdf
    //     a is ~
    //
    PARAMCLASS_META
} ParamClass;
