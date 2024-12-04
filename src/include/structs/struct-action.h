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
    struct Details : public Phase {};
    struct ParamList : public VarList {};  // see VarList (inherits from Phase)
#else
    typedef Flex Details;
    typedef Flex ParamList;
#endif


#define MISC_DetailsAdjunct_TYPE      VarList*
#define HAS_MISC_DetailsAdjunct       FLAVOR_DETAILS

// Note: LINK on details is the DISPATCHER, on varlists it's KEYSOURCE


//=//// DETAILS_FLAG_24 ///////////////////////////////////////////////////=//
//
#define DETAILS_FLAG_24 \
    STUB_SUBCLASS_FLAG_24


//=//// DETAILS_FLAG_25 ///////////////////////////////////////////////////=//
//
#define DETAILS_FLAG_25 \
    STUB_SUBCLASS_FLAG_25


//=//// DETAILS_FLAG_26 ///////////////////////////////////////////////////=//
//
//
#define DETAILS_FLAG_26 \
    STUB_SUBCLASS_FLAG_26


//=//// DETAILS_FLAG_27 ///////////////////////////////////////////////////=//
//
#define DETAILS_FLAG_27 \
    STUB_SUBCLASS_FLAG_27


//=//// DETAILS_FLAG_28 ///////////////////////////////////////////////////=//
//
#define DETAILS_FLAG_28 \
    STUB_SUBCLASS_FLAG_28


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
    STUB_SUBCLASS_FLAG_29

typedef enum {
    NATIVE_NORMAL,
    NATIVE_COMBINATOR,
    NATIVE_INTRINSIC
} NativeType;


//=//// DETAILS_FLAG_CAN_DISPATCH_AS_INTRINSIC ////////////////////////////=//
//
// See %sys-intrinsic.h for a description of intrinsics.
//
#define DETAILS_FLAG_CAN_DISPATCH_AS_INTRINSIC \
    STUB_SUBCLASS_FLAG_30


//=//// DETAILS_FLAG_31 ///////////////////////////////////////////////////=//
//
#define DETAILS_FLAG_31 \
    STUB_SUBCLASS_FLAG_31


#define Set_Details_Flag(p,name) \
    Set_Flavor_Flag(DETAILS, ensure(Details*, (p)), name)

#define Get_Details_Flag(p,name) \
    Get_Flavor_Flag(DETAILS, ensure(Details*, (p)), name)

#define Clear_Details_Flag(p,name) \
    Clear_Flavor_Flag(DETAILS, ensure(Details*, (p)), name)

#define Not_Details_Flag(p,name) \
    Not_Flavor_Flag(DETAILS, ensure(Details*, (p)), name)


// Includes STUB_FLAG_DYNAMIC because an action's paramlist is always
// allocated dynamically, in order to make access to the archetype and the
// parameters faster than Array_At().  See code for Phase_Key(), etc.
//
// !!! This used to include FLEX_FLAG_FIXED_SIZE for both.  However, that
// meant the mask was different for paramlists and context keylists (which
// are nearing full convergence).  And on the details array, it got in the
// way of HIJACK, which may perform expansion.  So that was removed.
//
#define FLEX_MASK_PARAMLIST FLEX_MASK_VARLIST

#define FLEX_MASK_DETAILS \
    (NODE_FLAG_NODE \
        | FLAG_FLAVOR(DETAILS) \
        | STUB_FLAG_MISC_NODE_NEEDS_MARK  /* meta */ \
        /* LINK is dispatcher, a c function pointer, should not mark */ \
        | STUB_FLAG_INFO_NODE_NEEDS_MARK  /* exemplar */ )


//=//// PARAMETER CLASSES ////////////////////////////////////////////////=//

typedef enum {
    PARAMCLASS_0,  // temporary state for Option(ParamClass)

    // `PARAMCLASS_NORMAL` is cued by an ordinary WORD! in the function spec
    // to indicate that you would like that argument to be evaluated normally.
    //
    //     >> /foo: function [a] [print ["a is" a]]
    //
    //     >> foo 1 + 2
    //     a is 3
    //
    PARAMCLASS_NORMAL,

    // `PARAMCLASS_JUST` is cued by a quoted WORD! in the function spec
    // dialect.  It indicates that a single value of content at the callsite
    // should be passed through *literally*, with no evaluation or binding:
    //
    //     >> /foo: lambda ['a] [a]
    //
    //     >> foo (1 + 2)
    //     == (1 + 2)
    //
    //     >> x: 10, foo x
    //     == x
    //
    //     >> x: 10, get foo x
    //     ** Error: not bound
    //
    PARAMCLASS_JUST,

    // `PARAMCLASS_THE` is cued by a THE-WORD! in the function spec
    // dialect.  It indicates that a single value of content at the callsite
    // should be passed through literally, BUT it will pick up binding:
    //
    //     >> /foo: lambda [@a] [a]
    //
    //     >> foo (1 + 2)
    //     == (1 + 2)
    //
    //     >> x: 10, foo x
    //     == x
    //
    //     >> x: 10, get foo x
    //     == 10  ; different from (lambda ['a] [a]) result
    //
    PARAMCLASS_THE,

    // `PARAMCLASS_SOFT` is cued by a THE-GROUP! in the function spec
    // dialect.  It quotes with the exception of GROUP!, which is evaluated:
    //
    //     >> /foo: function [@(a)] [print [{a is} a]
    //
    //     >> foo x
    //     a is x
    //
    //     >> foo (1 + 2)
    //     a is 3
    //
    // It is possible to *mostly* implement soft quoting with hard quoting,
    // though it is a convenient way to allow callers to "escape" a quoted
    // context when they need to, and have type checking still applied.
    //
    // However there is a nuance which makes soft quoting fundamentally
    // different from hard quoting, regarding how it resolves contention
    // with other hard quotes.  If you have a situation like:
    //
    //     /right-soft: func [@(arg)] [...]
    //     /left-literal: infix func [@left right] [...]
    //
    // Soft quoting will "tie break" by assuming the soft literal operation
    // is willing to let the hard literal operation run:
    //
    //     right-escapable X left-literal Y
    //     =>
    //     right-escapable (X left-literal Y)
    //
    PARAMCLASS_SOFT,

    // `PARAMCLASS_META` is the only parameter type that can accept unstable
    // isotopes.  Antiforms become quasiforms when they are an argument, and
    // all other types receive one added quote level.
    //
    //     >> /foo: function [^a] [print [{a is} a]
    //
    //     >> foo 1 + 2
    //     a is '3
    //
    //     >> foo get:any $asdfasfasdf
    //     a is ~
    //
    PARAMCLASS_META
} ParamClass;
