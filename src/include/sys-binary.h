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


// Is it a byte-sized series?
//
#define BYTE_SIZE(s) \
    (Series_Wide(s) == 1)


//
// BIN_XXX: Binary or byte-size string seres macros
//

#define Binary_At(bin,n) \
    Series_At(Byte, (bin), (n))

#define Binary_Head(bin) \
    Series_Head(Byte, (bin))

#define Binary_Tail(bin) \
    Series_Tail(Byte, (bin))

#define Binary_Last(bin) \
    Series_Last(Byte, (bin))

INLINE REBLEN Binary_Len(Binary* bin) {
    assert(BYTE_SIZE(bin));
    return Series_Len(bin);
}

INLINE void TERM_BIN(Binary* bin) {
    Binary_Head(bin)[Series_Len(bin)] = 0;
}

INLINE void TERM_BIN_LEN(Binary* bin, REBLEN len) {
    Set_Series_Len(bin, len);
    Binary_Head(bin)[len] = 0;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  BINARY! (uses `struct Reb_Any_Series`)
//
//=////////////////////////////////////////////////////////////////////////=//

#define VAL_BIN_HEAD(v) \
    Binary_Head(VAL_SERIES(v))

INLINE Byte *Cell_Binary_At(const Cell* v) {
    return Binary_At(VAL_SERIES(v), VAL_INDEX(v));
}

INLINE Byte *Cell_Binary_Tail(const Cell* v) {
    return Series_Tail(Byte, VAL_SERIES(v));
}

// !!! RE: VAL_BIN_AT_HEAD() see remarks on VAL_ARRAY_AT_HEAD()
//
#define VAL_BIN_AT_HEAD(v,n) \
    Binary_At(VAL_SERIES(v), (n))

#define VAL_BYTE_SIZE(v) \
    BYTE_SIZE(VAL_SERIES(v))

// defined as an inline to avoid side effects in:

#define Init_Binary(out, bin) \
    Init_Any_Series((out), REB_BINARY, (bin))
