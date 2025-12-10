//
//  file: %a-constants.c
//  summary: "Special global constants, scanned to make %tmp-constants.h"
//  section: environment
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Ren-C Open Source Contributors
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
// Most text strings in Rebol should appear in the bootstrap files as Rebol
// code.  This allows for "internationalization" without needing to update
// the C code.  Other advantages are that the strings are compressed,
// "reduces tampering", etc.
//
// So to keep track of any stray English strings in the executable which make
// it into the user's view, they should be located here.
//
// NOTE: It's acceptable for hardcoded English strings to appear in the debug
// build or in other debug settings, as anyone working with the C code itself
// is basically expected to be able to read English (given the variable names
// and comments in the C are English).
//
// NOTE: For a constant to be picked up from this file, the parse rule is
// that it !!HAS TO START WITH `const`!!.  It makes the extern definition
// based on what it captures up to the `=` sign.
//

#include "reb-config.h"

#include <stdlib.h> // size_t and other types used in rebol.h

#include "assert-fix.h"
#include "needful/needful.h"
#include "c-extras.h"  // for EXTERN_C, nullptr, etc.


#include "tmp-constants.h" // need the extern definitions

// A crash() indicates a serious malfunction, and should not make use of
// Rebol-structured error message delivery in the release build.

const char g_crash_title[] = "CRASH! (Internal Error)";

const char g_crash_directions[] = {
    "If you need to file a bug in the issue tracker, please give thorough\n"
    "details on how to reproduce the problem:\n"
    "\n"
    "    https://github.com/metaeducation/ren-c/issues\n"
    "\n"
    "Include the following information in the report:\n\n"
};

const char* g_hex_digits = "0123456789ABCDEF";

// Zen Point on naming cues: was "Month_Lengths", but said 29 for Feb! --@HF
const unsigned char g_month_max_days[12] = {
    31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};


const char * const g_month_names[12] = {
    "January", "February", "March", "April", "May", "June", "July", "August",
    "September", "October", "November", "December"
};


// Used by scanner. Keep in sync with TokenEnum in %scan.h file!
//
const char * const g_token_names[] = {
    "!token-0!",
    "caret",
    "at",
    "dollar",
    "newline",
    "underscore",
    "comma",
    "word",
    "logic",
    "integer",
    "decimal",
    "percent",
    "group-end",
    "group-begin",
    "block-end",
    "block-begin",
    "fence-end",
    "fence-begin",
    "time",
    "date",
    "char",
    "apostrophe",
    "tilde",
    "string",
    "binary",
    "pair",
    "tuple",
    "chain",
    "file",
    "email",
    "url",
    "issue",
    "tag",
    "path",
    "construct",
    "end-of-script",
    nullptr
};
