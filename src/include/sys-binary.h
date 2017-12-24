//
//  File: %sys-binary.h
//  Summary: {Definitions for binary series}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
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
// Byte-sized series are also used by the STRING! datatype.  There is no
// technical difference between such series used as strings or used as binary,
// the difference comes from being marked REB_BINARY or REB_STRING in the
// header of the value carrying the series.
//
// For easier type-correctness, the series macros are given with names BIN_XXX
// and UNI_XXX.  There aren't distinct data types for the series themselves,
// just REBSER* is used.  Hence BIN_LEN() and UNI_LEN() aren't needed as you
// could just use SER_LEN(), but it helps a bit for readability...and an
// assert is included to ensure the size matches up.
//


// Is it a byte-sized series?
//
#define BYTE_SIZE(s) \
    LOGICAL(SER_WIDE(s) == 1)


//
// BIN_XXX: Binary or byte-size string seres macros
//

#define BIN_AT(s,n) \
    SER_AT(REBYTE, (s), (n))

#define BIN_HEAD(s) \
    SER_HEAD(REBYTE, (s))

#define BIN_TAIL(s) \
    SER_TAIL(REBYTE, (s))

#define BIN_LAST(s) \
    SER_LAST(REBYTE, (s))

inline static REBCNT BIN_LEN(REBSER *s) {
    assert(BYTE_SIZE(s));
    return SER_LEN(s);
}

inline static void TERM_BIN(REBSER *s) {
    BIN_HEAD(s)[SER_LEN(s)] = 0;
}

inline static void TERM_BIN_LEN(REBSER *s, REBCNT len) {
    SET_SERIES_LEN(s, len);
    BIN_HEAD(s)[len] = 0;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  BINARY! (uses `struct Reb_Any_Series`)
//
//=////////////////////////////////////////////////////////////////////////=//

#define VAL_BIN_HEAD(v) \
    BIN_HEAD(VAL_SERIES(v))

inline static REBYTE *VAL_BIN_AT(const RELVAL *v) {
    return BIN_AT(VAL_SERIES(v), VAL_INDEX(v));
}

inline static REBYTE *VAL_BIN_TAIL(const RELVAL *v) {
    return SER_TAIL(REBYTE, VAL_SERIES(v));
}

// !!! RE: VAL_BIN_AT_HEAD() see remarks on VAL_ARRAY_AT_HEAD()
//
#define VAL_BIN_AT_HEAD(v,n) \
    BIN_AT(VAL_SERIES(v), (n))

#define VAL_BYTE_SIZE(v) \
    BYTE_SIZE(VAL_SERIES(v))

// defined as an inline to avoid side effects in:

#define Init_Binary(out, bin) \
    Init_Any_Series((out), REB_BINARY, (bin))
