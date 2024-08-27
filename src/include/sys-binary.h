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
    (Flex_Wide(s) == 1)


//
// BIN_XXX: Blob or byte-size string seres macros
//

INLINE Blob* Cell_Blob(const Cell* cell) {
    assert(Is_Binary(cell));
    Flex* s = Cell_Flex(cell);
    assert(Flex_Wide(s) == 1);
    return cast(Blob*, s);
}

INLINE Byte* Blob_At(Blob* bin, REBLEN n)
  { return Flex_At(Byte, bin, n); }

INLINE Byte* Blob_Head(Blob* bin)
  { return Flex_Head(Byte, bin); }

INLINE Byte* Blob_Tail(Blob* bin)
  { return Flex_Tail(Byte, bin); }

INLINE Byte* Blob_Last(Blob* bin)
  { return Series_Last(Byte, bin); }

INLINE REBLEN Blob_Len(Blob* bin) {
    assert(BYTE_SIZE(bin));
    return Flex_Len(bin);
}

INLINE void Term_Blob(Blob* bin) {
    Blob_Head(bin)[Flex_Len(bin)] = 0;
}

INLINE void Term_Blob_Len(Blob* bin, REBLEN len) {
    Set_Flex_Len(bin, len);
    Blob_Head(bin)[len] = 0;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  BINARY! (uses `struct Reb_Any_Series`)
//
//=////////////////////////////////////////////////////////////////////////=//

#define Cell_Binary_Head(v) \
    Blob_Head(Cell_Blob(v))

INLINE Byte *Cell_Binary_At(const Cell* v) {
    return Blob_At(Cell_Blob(v), VAL_INDEX(v));
}

INLINE Byte *Cell_Binary_Tail(const Cell* v) {
    return Flex_Tail(Byte, Cell_Blob(v));
}

// !!! RE: Cell_Binary_At_Head() see remarks on Cell_List_At_Head()
//
#define Cell_Binary_At_Head(v,n) \
    Blob_At(Cell_Blob(v), (n))

#define VAL_BYTE_SIZE(v) \
    BYTE_SIZE(Cell_Flex(v))

// defined as an inline to avoid side effects in:

#define Init_Binary(out, blob) \
    Init_Any_Series((out), REB_BINARY, (blob))
