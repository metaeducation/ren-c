//
//  file: %struct-details.h
//  summary: "Dispatcher Details definitions preceding %tmp-internals.h"
//  section: core
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// As in historical Rebol, Ren-C has several different kinds of functions...
// each of which have a different implementation path in the system.
//
// Each action has an associated C function that runs when it is invoked, and
// this is called the "dispatcher".  A dispatcher may be general and reused
// by many different actions.  For example: the same dispatcher code is used
// for most `FUNC [...] [...]` instances--but each one has a different body
// array and spec, so the behavior is different.  Other times a dispatcher can
// be for a single function, such as with natives like IF that have C code
// which is solely used to implement IF.
//
// The information that lets function instances with the same Dispatcher
// run differently is an array called its dispatch "Details".  So while
// every FUNC in the system uses the same Dispatcher* C function, each one
// has a different Details array that contains the unique body block and
// points to a different ParamList* of parameter definitions to use.
//
// (See the comments in the %src/core/functionals/ directory for each function
// variation for descriptions of how they use their details arrays.)
//
//

#if CPLUSPLUS_11
    struct Details : public Context {};
#else
    typedef Stub Details;
#endif


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


//=//// DETAILS_FLAG_OWNS_PARAMLIST ////////////////////////////////////////=//
//
// When the Frame_Lens() of a FRAME! is a Details*, then this flag drives
// whether or not all the variables of the associated ParamList* are visible
// or just the inputs.  It's important because while ADAPT shares the same
// ParamList* as the function it's adapting, you shouldn't be able to get
// at the locals of that adaptee...so it shouldn't use this flag.  But things
// like FUNCTION need it, otherwise locals and RETURN wouldn't be visible.
//
#define DETAILS_FLAG_OWNS_PARAMLIST \
    STUB_SUBCLASS_FLAG_27


//=//// DETAILS_FLAG_API_CONTINUATIONS_OK /////////////////////////////////=//
//
// Originally the rebContinue() and rebDelegate() functions would look to see
// if TOP_LEVEL was explicitly the Api_Function_Dispatcher(), and only let
// you do a continuation if it was.  But there's no real reason why the
// JavaScript code can't do reb.Continue() and reb.Delegate(), so instead
// it checks for this flag on TOP_LEVEL.
//
#define DETAILS_FLAG_API_CONTINUATIONS_OK \
    STUB_SUBCLASS_FLAG_28


//=//// DETAILS_FLAG_RAW_NATIVE ///////////////////////////////////////////=//
//
// Once the Action_Executor() has fulfilled a function's frame, it will
// sub-dispatch it to the Dispatcher* function in the Details.  There are
// different dispatchers for things like FUNC or CASCADE or ADAPT or ENCLOSE,
// which know how to interpret the Details array into the right kind of
// behavior to execute.
//
// Functions that have their implementations as C code, but that intend to
// use the API, have a dispatcher as well: the Api_Function_Dispatcher().  It
// doesn't do much...but it extracts the varlist from the Level and gets it
// managed and inheritance linked to be used with the API.  It also does
// checking to make sure the return result coming back from that C function
// implementation is the right type.
//
// But then there are "Raw" natives, whose Dispatcher* actually -is- the full
// implementation of the function itself.  This is for fundamental functions
// like IF or ANY or the FUNC native itself.  To get the most efficiency,
// these take Level* instead of Context*...and there is no type checking in
// the release build of their results.  There's no automatic management or
// inheritance of the varlist to use it for API calls (in fact, there may
// be no varlist at all...see DETAILS_FLAG_CAN_DISPATCH_AS_INTRINSIC).
//
// Because each of these functions is a fully unique Dispatcher, there is no
// Details_Querier() that covers them.
//
#define DETAILS_FLAG_RAW_NATIVE \
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


#define DETAILS_MASK_NONE  0


//=//// DETAILS STUB SLOT USAGE ///////////////////////////////////////////=//

#define STUB_MASK_DETAILS \
    (BASE_FLAG_BASE \
        | FLAG_FLAVOR(FLAVOR_DETAILS) \
        | STUB_FLAG_DYNAMIC \
        | not STUB_FLAG_LINK_NEEDS_MARK  /* don't mark dispatcher */ \
        | 0 /* STUB_FLAG_MISC_NEEDS_MARK */  /* Adjunct, maybe null */ \
        | not STUB_FLAG_INFO_NEEDS_MARK  /* info not currently used */ \
    )

#define LINK_DETAILS_DISPATCHER(details)  (details)->link.cfunc
#define MISC_DETAILS_ADJUNCT(details)     STUB_MISC(details)
// INFO in details currently unused, just the info flags
// BONUS in details currently unused


//=//// DETAILS "QUERIERS" ////////////////////////////////////////////////=//
//
// DetailsQueriers are used for getting things like the RETURN or BODY of
// a function.  They are specific to each dispatcher (with a common querier
// used by all DETAILS_FLAG_RAW_NATIVE functions).
//

typedef bool (DetailsQuerier)(
    Sink(Value) out,
    Details* details,
    SymId property
);

typedef struct {
    Dispatcher* dispatcher;
    DetailsQuerier* querier;
} DispatcherAndQuerier;


#if CPLUSPLUS_11
    struct ParamList : public VarList {};  // see VarList (inherits from Phase)
#else
    typedef Stub ParamList;
#endif


//=//// PHASE IS A WEIRD "TYPESET" OF DETAILS* AND PARAMLIST* ////////////=//
//
// We would like to say:
//
//     struct Details : public Phase {}
//     struct ParamList : public Phase {}
//
// This way you could pass a Details* or a ParamList* anywhere a Phase* would
// be accepted.  Except this would lose important properties--like that
// a ParamList* is actually a Context*.  It makes more sense for ParamList
// to inherit from VarList.
//
// The Needful library gives us a tool for building compile-time typecheckers
// that check for a match against a list of arbitrary types:
//
//     ensure_any((Phase*, Details*, ParamList*), expression)
//
// Beyond that, it actually lets you specialize arity-1 ensure(), so that
// you can make the check against that list the semantics of:
//
//     ensure(Phase*, expression)
//
// So we rely on this--although it forces us to write macros to interface
// with functions that take Phase* in order to get them to take Details* or
// ParamList*.  But it seems to work.
//
#if CPLUSPLUS_11
    struct Phase : public Stub {};
#else
    typedef Stub Phase;
#endif


// Includes STUB_FLAG_DYNAMIC because an action's paramlist is always
// allocated dynamically, in order to make access to the archetype and the
// parameters faster than Array_At().  See code for Phase_Key(), etc.
//
// !!! This used to include FLEX_FLAG_FIXED_SIZE for both.  However, that
// meant the mask was different for paramlists and context keylists (which
// are nearing full convergence).  And on the details array, it got in the
// way of HIJACK, which may perform expansion.  So that was removed.
//
#define STUB_MASK_PARAMLIST STUB_MASK_VARLIST

//=//// PARAMETER CLASSES ////////////////////////////////////////////////=//
//
// This has to be defined in a file included before %tmp-internals.h, since
// ParamClass is used in function interfaces.  Cant be in %cell-parameter.h
//

typedef enum {
    PARAMCLASS_0,  // temporary state for Option(ParamClass)

    // `PARAMCLASS_NORMAL` is cued by an ordinary WORD! in the function spec
    // to indicate that you would like that argument to be evaluated normally.
    //
    //     >> foo: function [a] [print ["a is" a]]
    //
    //     >> foo 1 + 2
    //     a is 3
    //
    PARAMCLASS_NORMAL,

    // `PARAMCLASS_JUST` is cued by a quoted WORD! in the function spec
    // dialect.  It indicates that a single value of content at the callsite
    // should be passed through *literally*, with no evaluation or binding:
    //
    //     >> foo: lambda ['a] [a]
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

    // `PARAMCLASS_THE` is cued by an @WORD! in the function spec
    // dialect.  It indicates that a single value of content at the callsite
    // should be passed through literally, BUT it will pick up binding:
    //
    //     >> foo: lambda [@a] [a]
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

    // `PARAMCLASS_SOFT` is cued by an @GROUP! in the function spec
    // dialect.  It quotes with the exception of GROUP!, which is evaluated:
    //
    //     >> foo: function [@(a)] [print [{a is} a]
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
    //     right-soft: func [@(arg)] [...]
    //     left-literal: infix func [@left right] [...]
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
    //     >> foo: function [^a] [print [{a is} a]
    //
    //     >> foo 1 + 2
    //     a is '3
    //
    //     >> foo get:any $asdfasfasdf
    //     a is ~
    //
    PARAMCLASS_META
} ParamClass;
