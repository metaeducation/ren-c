//
//  File: %sys-flavor.h
//  Summary: "Stub Subclass Type Enumeration"
//  Project: "Ren-C Interpreter and Run-time"
//  Homepage: https://github.com/metaeducation/ren-c/
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


enum StubFlavorEnum {
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

    // A FLAVOR_HITCH is an ephemeral element which is chained into the
    // "hitch" list on a symbol, when that symbol is being bound.  Currently
    // it holds an integer for a binding position, but allowing it to hold
    // arbitrary things for a mapping is being considered.
    //
    // !!! Think how this might relate to locking and inodes.  Does it?
    //
    FLAVOR_HITCH,

    // To make it possible to reuse exemplars and paramlists in action
    // variations that have different partial specializations, a splice of
    // partial refinements sit between the action cell and its "speciality".
    //
    FLAVOR_PARTIALS,

    FLAVOR_LIBRARY,
    FLAVOR_HANDLE,

    FLAVOR_FEED,
    FLAVOR_API,

    // This is used by rebINLINE() to place an array of content as raw
    // material to execute.  (It leverages similar code as MACRO.)
    //
    FLAVOR_INSTRUCTION_SPLICE,

    // Pairlists are used by map! (note that Unreadable() is used for zombie
    // keys, but it's not an antiform...)
    //
    FLAVOR_PAIRLIST,

    FLAVOR_MIN_ANTIFORMS_OK,  //=//// BELOW HERE, ARRAYS CAN HOLD ANTIFORMS

    // This indicates this Flex represents the "varlist" of a context (which
    // is interchangeable with the identity of the varlist itself).  A second
    // Flex can be reached from it via the LINK() in the Array Stub, which
    // is known as a "KeyList".
    //
    // See notes on Context for further details about what a context is.
    //
    FLAVOR_VARLIST = FLAVOR_MIN_ANTIFORMS_OK,

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

    // The data stack is implemented as an array but has its own special
    // marking routine.  However, antiforms are legal in the data stack... but
    // when popping the stack it is checked that the array being popped *into*
    // allows antiforms.
    //
    FLAVOR_DATASTACK,

    FLAVOR_PLUG,

    FLAVOR_MAX_HOLDS_CELLS = FLAVOR_PLUG,  //=//// ^-- WIDTH IS sizeof(Cell)

    // For the moment all Flexes that don't store Cells or or byte data of
    // WIDTH=1 store items of size pointer.
    //
    FLAVOR_KEYLIST,  // width = sizeof(Symbol*)
    FLAVOR_POINTER,  // generic
    FLAVOR_CANONTABLE,  // for canons table
    FLAVOR_NODELIST,  // e.g. GC protect list
    FLAVOR_FLEXLIST,  // e.g. the list of manually allocated Flexes
    FLAVOR_MOLDSTACK,

    FLAVOR_HASHLIST,  // outlier, sizeof(REBLEN)...
    FLAVOR_BOOKMARKLIST,  // also outlier, sizeof(Bookmark)

    FLAVOR_MIN_BYTESIZE,  //=/////////////////// BELOW THIS LINE HAS WIDTH = 1

    FLAVOR_BINARY = FLAVOR_MIN_BYTESIZE,

    FLAVOR_MIN_STRING,  //=////////////// BELOW THIS LINE IS UTF-8 (OR CORRUPT)

    FLAVOR_NONSYMBOL = FLAVOR_MIN_STRING,

    // While the content format is UTF-8 for both ANY-STRING? and ANY-WORD?,
    // MISC() and LINK() fields are used differently.  String caches its length
    // in codepoints so that doesn't have to be recalculated, and it also has
    // caches of "bookmarks" mapping codepoint indexes to byte offsets.  Words
    // store a pointer that is used in a circularly linked list to find their
    // canon spelling form...as well as hold binding information.
    //
    FLAVOR_SYMBOL,

    // Right now there is only one instance of FLAVOR_THE_GLOBAL_INACCESSIBLE
    // Flex.  All Stubs that have NODE_FLAG_UNREADABLE will be canonized
    // to this Node.  This allows a decayed Flex to still convey what flavor
    // it was before being decayed.  That's useful at least for debugging, but
    // maybe for other mechanisms that sometimes might want to propagate some
    // residual information from decayed Flex to the referencing sites.
    //
    // (For instance: Such a mechanism would've been necessary for propagating
    // Symbols back into words, when bound words gave up their Symbols...if the
    // Flex they were bound to went away.  Not needed now--but an example.)
    //
    FLAVOR_THE_GLOBAL_INACCESSIBLE,

    FLAVOR_MAX
};

typedef enum StubFlavorEnum Flavor;
