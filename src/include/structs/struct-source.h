//
//  file: %struct-source.h
//  summary: "Definitions for the Source Array subclass"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
    typedef Stub Source;
#endif


//=//// SOURCE ARRAY SLOT USAGE ///////////////////////////////////////////=//
//
// There are many flags available for source arrays, so they could indicate
// storing different kinds of information.  For now, the file and line is
// the only option.
//
// Rather than let nullptr in the link slot indicate there is no filename,
// the routines go on the basis of STUB_FLAG_LINK_NEEDS_MARK.  This lets
// marking source arrays without a filename be a bit faster, since it doesn't
// have to test for null.
//

#define LINK_SOURCE_FILENAME_NODE       STUB_LINK
#define MISC_SOURCE_LINE(source)        (source)->misc.line
// source arrays use their INFO for FLEX_INFO_XXX bits
// source arrays BONUS_FLEX_BIAS()
