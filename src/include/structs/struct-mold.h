//
//  file: %struct-mold.h
//  summary: "Structure for Molding preceding %tmp-internals.h"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2025 Ren-C Open Source Contributors
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

typedef struct {
    Strand* strand;  // destination String (utf8, as all String are)
    struct {
        Index index;  // codepoint index where mold starts within String
        Size size;  // byte offset where mold starts within String
    } base;
    Flags opts;       // special option flags
    REBLEN limit;       // how many characters before cutting off
    REBLEN reserve;     // how much capacity to reserve at the outset
    REBINT indent;      // indentation amount
    Byte period;      // for decimal point
    Byte dash;        // for date fields
    Byte digits;      // decimal digits
} Molder;

// Modes allowed by FORM
enum {
    FORM_FLAG_ONLY = 0,
    FORM_FLAG_REDUCE = 1 << 0,
    FORM_FLAG_NEWLINE_SEQUENTIAL_STRINGS = 1 << 1,
    FORM_FLAG_NEWLINE_UNLESS_EMPTY = 1 << 2,
    FORM_FLAG_MOLD = 1 << 3
};

// Mold and form options:
enum {
    MOLD_FLAG_0 = 0,
    MOLD_FLAG_1 = 1 << 0,
    MOLD_FLAG_COMMA_PT = 1 << 1, // Decimal point is a comma.
    MOLD_FLAG_SLASH_DATE = 1 << 2, // Date as 1/1/2000
    MOLD_FLAG_INDENT = 1 << 3, // Indentation
    MOLD_FLAG_TIGHT = 1 << 4, // No space between block values
    MOLD_FLAG_SPREAD = 1 << 5, // Mold Spread - no outer block []
    MOLD_FLAG_LINES  = 1 << 6, // add a linefeed between each value
    MOLD_FLAG_LIMIT = 1 << 7, // Limit length to mold->limit, then "..."
    MOLD_FLAG_RESERVE = 1 << 8,  // At outset, reserve capacity for buffer
    MOLD_FLAG_WAS_TRUNCATED = 1 << 9  // Set true upon truncation
};

#define MOLD_MASK_NONE 0
