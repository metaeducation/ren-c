//
//  file: %enum-flavor.h
//  summary: "Stub Subclass Type Enumeration"
//  project: "Ren-C Interpreter and Run-time"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2023 Ren-C Open Source Contributors
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
// A byte in the Stub header is used to store an enumeration value of the kind
// of Stub that it is.  This takes the place of storing a special element
// "width" in the Flex (which R3-Alpha did).  Instead, the element width is
// determined by the "Flavor".
//
// In order to maximize the usefulness of this byte, the enumeration is
// organized in a way where the ordering conveys information.  So all the
// arrays are grouped together so a single test can tell if a subclass is
// an array type. This saves on needing to have separate flags like
// FLEX_FLAG_IS_ARRAY.
//
//=//// NOTES ////////////////////////////////////////////////////////////=//
//
// * It would be nice if this file could be managed by a %flavors.r file that
//   would be something like the %types.r for value types...where the process
//   of auto-generation generated testing macros automatically.
//


typedef enum {
    //
    // FLAVOR_0 is reserved as an illegal flavor byte, which can be used to
    // make an Optional(Flavor).
    //
    FLAVOR_0,

    // Arrays that can be used with BLOCK! or other such types.  This
    // is what you get when you use plain Make_Source().
    //
    // NOTE: This flavor implicitly implies that file and line numbering should
    // be captured by Make_Flex()
    //
    FLAVOR_SOURCE,

    // A "use" is a request in a virtual binding chain to make an object's
    // fields visible virtually in the code.  LETs can also be in the chain,
    // and a frame varlist is also allowed to temrinate it.
    //
    FLAVOR_USE,

    // A FLAVOR_STUMP is an ephemeral element which is chained into the
    // "hitch" list on a symbol, when that symbol is being bound.  Currently
    // it holds an integer for a binding position, but allowing it to hold
    // arbitrary things for a mapping is being considered.
    //
    FLAVOR_STUMP,

    FLAVOR_LIBRARY,
    FLAVOR_HANDLE,

    FLAVOR_FEED,
    FLAVOR_API,

    // This is used by rebINLINE() to place an array of content as raw
    // material to execute.  (It leverages similar code as MACRO.)
    //
    FLAVOR_INSTRUCTION_SPLICE,

    // A "Sea" of Vars is what's used to hold a sparse mapping of Symbol to
    // Variable, such as with MODULE!
    //
    FLAVOR_SEA,

    MIN_FLAVOR_ANTIFORMS_OK,  //=//// BELOW HERE, ARRAYS CAN HOLD ANTIFORMS

    // The data stack is implemented as an array but has its own special
    // marking routine.  However, antiforms are legal in the data stack... but
    // when popping the stack it is checked that the array being popped *into*
    // allows antiforms.
    //
    // (This is also used by "PLUG" cells which preserve the datastack, along
    // with some additional values.)
    //
    FLAVOR_DATASTACK = MIN_FLAVOR_ANTIFORMS_OK,

    // Pairlists are used by map! (note that Unreadable() is used for zombie
    // keys.)  It was relaxed to be allowed to store antiforms, just not
    // nulled or trash keys.
    //
    FLAVOR_PAIRLIST,

    // This indicates this Flex represents the "varlist" of a context (which
    // is interchangeable with the identity of the varlist itself).  See
    // notes on the definition of VarList.
    //
    FLAVOR_VARLIST,

    FLAVOR_PARAMLIST = FLAVOR_VARLIST,  // review

    // "Details" are the per-ACTION! instance information (e.g. this would be
    // the body array for a usermode function, or the datatype that a type
    // checker dispatcher would want to check against.)  The first element of
    // the array is an archetypal value for the action (no binding/phase).
    //
    FLAVOR_DETAILS,

    // The concept of "Virtual Binding" is that instances of ANY-LIST? values
    // can carry along a collection of contexts that override the bindings of
    // words that are encountered.  This collection is done by means of
    // "lets" that make a linked list of overrides.
    //
    FLAVOR_LET,

    // A "patch" is a container for a single variable for a context.  Rather
    // than live in the context directly, it stands on its own.  Modules are
    // made up of patches vs. using the packed array VARLIST of frames and
    // contexts.
    //
    FLAVOR_PATCH,

    // Extensions use FLAVOR_CELLS to indicate that they are making something
    // with cells that need to be marked, but are using the MISC, LINK, INFO,
    // and BONUS slots in a way that doesn't have anything to do with how
    // FLAVOR_SOURCE would use them.
    //
    FLAVOR_CELLS,

    MAX_FLAVOR_HOLDS_CELLS = FLAVOR_CELLS,  //=//// ^-- WIDTH IS sizeof(Cell)

    // For the moment all Flexes that don't store Cells or or byte data of
    // WIDTH=1 store items of size pointer.
    //
    FLAVOR_KEYLIST,  // width = sizeof(Symbol*)
    FLAVOR_POINTERS,  // generic
    FLAVOR_CANONTABLE,  // for canons table
    FLAVOR_NODELIST,  // e.g. GC protect list
    FLAVOR_FLEXLIST,  // e.g. the list of manually allocated Flexes
    FLAVOR_MOLDSTACK,

    FLAVOR_HASHLIST,  // outlier, sizeof(REBLEN)...
    FLAVOR_BOOKMARKLIST,  // also outlier, sizeof(Bookmark)
    FLAVOR_DISPATCHERTABLE,  // also outlier, sizeof(DispatcherAndQuerier)

    MIN_FLAVOR_BYTESIZE,  //=////////////////// BELOW THIS LINE HAS WIDTH = 1

    FLAVOR_BINARY = MIN_FLAVOR_BYTESIZE,

    // FLAVOR_BINARY has to keep the MISC and LINK slots available, because a
    // BLOB! can be generically aliased as a TEXT! or WORD!, which would mean
    // that the stub suddenly starts using those fields.  Stubs which want to
    // use the Stub.misc and Stub.link fields should use FLAVOR_BYTES.
    //
    FLAVOR_BYTES,

    MIN_FLAVOR_STRING,  //=////////////// BELOW THIS LINE IS UTF-8 (OR CORRUPT)

    FLAVOR_NONSYMBOL = MIN_FLAVOR_STRING,

    // While the content format is UTF-8 for both ANY-STRING? and ANY-WORD?,
    // String.misc and String.link are used differently.  Non-symbols cache
    // the length in codepoints so that isn't recalculated, and it also has
    // caches of "bookmarks" mapping codepoint indexes to byte offsets.  Words
    // store a pointer that is used in a circularly linked list to find their
    // canon spelling form...as well as point to module variable instances.
    //
    FLAVOR_SYMBOL,

    // Right now there is only one instance of FLAVOR_THE_GLOBAL_INACCESSIBLE
    // Flex.  All Stubs that have BASE_FLAG_UNREADABLE will be canonized
    // to this Base.
    //
    FLAVOR_THE_GLOBAL_INACCESSIBLE,

    MAX_FLAVOR = FLAVOR_THE_GLOBAL_INACCESSIBLE
} FlavorEnum;

typedef FlavorEnum Flavor;  // may become more complex wrapper in the future
