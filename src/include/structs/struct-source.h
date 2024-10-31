//
//  File: %struct-source.h
//  Summary: "Definitions for the Source Array subclass"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// Source is an array subclass that is suitable for backing a BLOCK!, GROUP!,
// FENCE!, etc.  It enforces that it doesn't hold antiforms, and it also
// has special interpretation of the LINK and MISC nodes to hold file and
// line information.
//

#if CPLUSPLUS_11
    struct Source : public Array {};
#else
    typedef Flex Source;
#endif


//=//// SOURCE_FLAG_HAS_FILE_LINE /////////////////////////////////////////=//
//
// The Flex Stub has two pointers in it, ->link and ->misc, which are
// used for a variety of purposes (pointing to the KeyList for an object,
// the C code that runs as the dispatcher for an Action, etc.)  But for
// regular source Arrays, they can be used to store the filename and line
// number, if applicable.
//
// Only Array preserves file and line info, as UTF-8 Strings need to use the
// ->misc and ->link fields for caching purposes in String.
//
// We don't need a separate array flag for this.  We just assume that if a
// link node is set to need marking, then the array has file and line info.

#define SOURCE_FLAG_HAS_FILE_LINE  STUB_FLAG_LINK_NODE_NEEDS_MARK

#define LINK_Filename_TYPE          const String*
#define HAS_LINK_Filename           FLAVOR_SOURCE


// These flags are only for checking "plain" array flags...so not varlists
// or paramlists or anything that isn't just an ordinary source-level array
// (like you'd find in a BLOCK!)
//
// 1. See mutability notes on Set_Flex_Flag() / Clear_Flex_Flag()

#define Get_Source_Flag(a,flag) \
    Get_Flavor_Flag(SOURCE, ensure(const Source*, (a)), flag)

#define Not_Source_Flag(a,flag) \
    Not_Flavor_Flag(SOURCE, ensure(const Source*, (a)), flag)

#define Set_Source_Flag(a,flag) \
    Set_Flavor_Flag(SOURCE, ensure(const Source*, (a)), flag)

#define Clear_Source_Flag(a,flag) \
    Clear_Flavor_Flag(SOURCE, ensure(const Source*, (a)), flag)


#define FLEX_MASK_UNMANAGED_SOURCE \
    FLAG_FLAVOR(SOURCE)

#define FLEX_MASK_MANAGED_SOURCE \
    (FLAG_FLAVOR(SOURCE) | NODE_FLAG_MANAGED)
