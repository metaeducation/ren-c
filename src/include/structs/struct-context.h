//
//  file: %struct-context.h
//  summary: "Context structure definitions preceding %tmp-internals.h"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// Conceptually a Context maps from Key (Symbols) to values.  But what data
// structure is used depends on the instance.  `Context` is the superclass of
// all the variants that convey Key->Value relationships.
//
// Contexts are able to link in an inherited fashion, so lookups are done
// according to chains of Context that are built up as the code runs.  So
// you can wind up with a Context* for a Let variable, that points to
// a Context* for a FRAME! VarList, that points to a Context! for a MODULE!
// that was captured by the body block when a function was generated.
//


// (It may be that someday Pairings or Cell* are considered for binding, but
// right now all instances are derived from Stub.)
//
typedef Stub Context;

#define LINK_CONTEXT_INHERIT_BIND(c)    STUB_LINK(c)
// MISC, INFO, BONUS are alls used differently for different CONTEXT subtypes


//=//// LET: STUB STORAGE FOR JUST ONE VARIABLE ///////////////////////////=//
//
// The LET structure is optimized to store a variable cell and key in a Stub,
// which is the size of two cells.  Because it is a Stub, words can bind
// directly to it.
//

typedef Context Let;

#define STUB_MASK_LET ( \
    FLAG_FLAVOR(LET) \
        | BASE_FLAG_MANAGED \
        | STUB_FLAG_LINK_NEEDS_MARK  /* Inherit_Bind() */ \
        | STUB_FLAG_INFO_NEEDS_MARK  /* Let symbol */ \
    )

#define INFO_LET_SYMBOL(let)  STUB_INFO(let)


//=//// USE: CONTAINER FOR PUTTING CONTEXTS IN BINDING CHAINS /////////////=//
//
// VarLists, SeaOfVars, and Lets all have pointers by which they can be linked
// into a binding chain.  But that only allows them to be linked into one
// chain--so a Use is a small container that can hold a reference to a
// context as well as a link to the next thing in the binding chain.
//
// 1. MISC was once "Variant": a circularly linked list of variations of this
//    USE with different Link_Inherit_Bind() data.  The idea was to assist in
//    avoiding creating unnecessary duplicate chains.  Diminish_Stub() would
//    remove patches from the list during GC.  This idea may have some
//    form that has merit, but that one didn't help anything.
//

typedef Context Use;

#define STUB_MASK_USE ( \
    FLAG_FLAVOR(USE) \
        | BASE_FLAG_MANAGED \
        | STUB_FLAG_LINK_NEEDS_MARK  /* Inherit_Bind() */ \
        | not STUB_FLAG_INFO_NEEDS_MARK  /* not yet used */ \
        | not STUB_FLAG_MISC_NEEDS_MARK  /* unused, was "Variant" [1] */ \
    )


//=//// USE_FLAG_SET_WORDS_ONLY ///////////////////////////////////////////=//
//
#define USE_FLAG_SET_WORDS_ONLY \
    STUB_SUBCLASS_FLAG_24
