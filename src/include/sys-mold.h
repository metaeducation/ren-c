//
//  File: %sys-mold.h
//  Summary: "Rebol Value to Text Conversions ('MOLD'ing and 'FORM'ing)"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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

struct rebol_mold {
    Blob* series;     // destination series (uni)
    REBLEN start;       // index where this mold starts within series
    REBFLGS opts;        // special option flags
    REBLEN limit;       // how many characters before cutting off with "..."
    REBLEN reserve;     // how much capacity to reserve at the outset
    REBINT indent;      // indentation amount
    Byte period;      // for decimal point
    Byte dash;        // for date fields
    Byte digits;      // decimal digits
};

#define Drop_Mold_If_Pushed(mo) \
    Drop_Mold_Core((mo), true)

#define Drop_Mold(mo) \
    Drop_Mold_Core((mo), false)

#define Pop_Molded_String(mo) \
    Pop_Molded_String_Core((mo), UNKNOWN)

#define Pop_Molded_String_Len(mo,len) \
    Pop_Molded_String_Core((mo), (len))

#define Mold_Value(mo,v) \
    Mold_Or_Form_Value((mo), (v), false)

#define Form_Value(mo,v) \
    Mold_Or_Form_Value((mo), (v), true)

#define Copy_Mold_Value(v,opts) \
    Copy_Mold_Or_Form_Value((v), (opts), false)

#define Copy_Form_Value(v,opts) \
    Copy_Mold_Or_Form_Value((v), (opts), true)

// Modes allowed by FORM
enum {
    FORM_FLAG_ONLY = 0,
    FORM_FLAG_REDUCE = 1 << 0,
    FORM_FLAG_NEWLINE_SEQUENTIAL_STRINGS = 1 << 1,
    FORM_FLAG_NEWLINE_UNLESS_EMPTY = 1 << 2,
    FORM_FLAG_MOLD = 1 << 3
};

// Mold and form options:
enum REB_Mold_Opts {
    MOLD_FLAG_0 = 0,
    MOLD_FLAG_ALL = 1 << 0, // Output lexical types in #[type...] format
    MOLD_FLAG_COMMA_PT = 1 << 1, // Decimal point is a comma.
    MOLD_FLAG_SLASH_DATE = 1 << 2, // Date as 1/1/2000
    MOLD_FLAG_INDENT = 1 << 3, // Indentation
    MOLD_FLAG_TIGHT = 1 << 4, // No space between block values
    MOLD_FLAG_ONLY = 1 << 5, // Mold/only - no outer block []
    MOLD_FLAG_LINES  = 1 << 6, // add a linefeed between each value
    MOLD_FLAG_LIMIT = 1 << 7, // Limit length to mold->limit, then "..."
    MOLD_FLAG_RESERVE = 1 << 8  // At outset, reserve capacity for buffer
};

// Temporary:
#define MOLD_FLAG_NON_ANSI_PARENED \
    MOLD_FLAG_ALL // Non ANSI chars are ^() escaped

#define DECLARE_MOLD(name) \
    REB_MOLD mold_struct; \
    mold_struct.series = nullptr; /* used to tell if pushed or not */ \
    mold_struct.opts = 0; \
    mold_struct.indent = 0; \
    REB_MOLD *name = &mold_struct; \

#define SET_MOLD_FLAG(mo,f) \
    ((mo)->opts |= (f))

#define GET_MOLD_FLAG(mo,f) \
    (did ((mo)->opts & (f)))

#define NOT_MOLD_FLAG(mo,f) \
    (not ((mo)->opts & (f)))

#define CLEAR_MOLD_FLAG(mo,f) \
    ((mo)->opts &= ~(f))


// Special flags for decimal formatting:
enum {
    DEC_MOLD_PERCENT = 1 << 0,      // follow num with %
    DEC_MOLD_MINIMAL = 1 << 1       // allow decimal to be integer
};

#define MAX_DIGITS 17   // number of digits
#define MAX_NUMCHR 32   // space for digits and -.e+000%

#define MAX_INT_LEN     21
#define MAX_HEX_LEN     16
