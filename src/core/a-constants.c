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
// Copyright 2012-2018 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
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
#include "pstdint.h" // polyfill <stdint.h> for pre-C99/C++11 compilers
#include "pstdbool.h" // polyfill <stdbool.h> for pre-C99/C++11 compilers

#include "c-enhanced.h"

#include "rebol.h"
typedef RebolValue Value;

#include "tmp-constants.h" // need the extern definitions

const char Str_REBOL[] = "REBOL";

// A crash() indicates a serious malfunction, and should not make use of
// Rebol-structured error message delivery in the release build.

const char g_crash_title[] = "Rebol Internal Error";

const char g_crash_directions[] = {
    "If you need to file a bug in the issue tracker, please give thorough\n"
    "details on how to reproduce the problem:\n"
    "\n"
    "    https://github.com/metaeducation/ren-c/issues\n"
    "\n"
    "Include the following information in the report:\n\n"
};

const char * Hex_Digits = "0123456789ABCDEF";

const char * const Esc_Names[] = {
    // Must match enum REBOL_Esc_Codes!
    "line",
    "tab",
    "page",
    "escape",
    "esc",
    "back",
    "del",
    "null"
};

const unsigned char Esc_Codes[] = {
    // Must match enum REBOL_Esc_Codes!
    10,     // line
    9,      // tab
    12,     // page
    27,     // escape
    27,     // esc
    8,      // back
    127,    // del
    0       // null
};

// Zen Point on naming cues: was "Month_Lengths", but said 29 for Feb! --@HF
const unsigned char Month_Max_Days[12] = {
    31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

const char * const Month_Names[12] = {
    "January", "February", "March", "April", "May", "June", "July", "August",
    "September", "October", "November", "December"
};


// Used by scanner. Keep in sync with TokenEnum in %scan.h file!
//
const char * const Token_Names[] = {
    "end-of-script",
    "newline",
    "comma",
    "block-end",
    "group-end",
    "word",
    "apostrophe",
    "blank",
    "logic",
    "integer",
    "decimal",
    "percent",
    "money",
    "time",
    "date",
    "char",
    "block-begin",
    "group-begin",
    "string",
    "binary",
    "pair",
    "tuple",
    "chain",
    "file",
    "email",
    "url",
    "issue",
    "tripwire",
    "tag",
    "path",
    "construct",
    nullptr
};


// !!! For now, (R)ebol (M)essages use the historical Debug_Fmt() output
// method, which is basically like `printf()`.  Over the long term, they
// should use declarations like the (R)ebol (E)rrors do with RE_XXX values
// loaded during boot.
//
// The goal should be that any non-debug-build only strings mentioned from C
// that can be seen in the course of normal operation should go through this
// abstraction.  Ultimately that would permit internationalization, and the
// benefit of not needing to ship a release build binary with a string-based
// format dialect.
//
// Switching strings to use this convention should ultimately parallel the
// `Error()` generation, where the arguments are Rebol values and not C
// raw memory as parameters.  Debug_Fmt() should also just be changed to
// a normal `Print()` naming.
//
const char RM_ERROR_LABEL[] = "Error: ";
const char RM_BAD_ERROR_FORMAT[] = "(improperly formatted error)";
const char RM_ERROR_WHERE[] = "** Where: ";
const char RM_ERROR_NEAR[] = "** Near: ";
const char RM_ERROR_FILE[] = "** File: ";
const char RM_ERROR_LINE[] = "** Line: ";

const char RM_WATCH_RECYCLE[] = "RECYCLE: %d series";
