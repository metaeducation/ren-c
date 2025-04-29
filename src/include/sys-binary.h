//
//  file: %sys-binary.h
//  summary:{Definitions for binary series}
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// Binary_Xxx: Binary or byte-size string seres macros
//

INLINE Binary* Cell_Binary(const Cell* cell) {
    assert(Is_Binary(cell));
    Flex* s = Cell_Flex(cell);
    assert(Flex_Wide(s) == 1);
    return cast(Binary*, s);
}

INLINE Byte* Binary_At(Binary* bin, REBLEN n)
  { return Flex_At(Byte, bin, n); }

INLINE Byte* Binary_Head(Binary* bin)
  { return Flex_Head(Byte, bin); }

INLINE Byte* Binary_Tail(Binary* bin)
  { return Flex_Tail(Byte, bin); }

INLINE Byte* Binary_Last(Binary* bin)
  { return Series_Last(Byte, bin); }

INLINE REBLEN Binary_Len(Binary* bin) {
    assert(BYTE_SIZE(bin));
    return Flex_Len(bin);
}

INLINE void Term_Binary(Binary* bin) {
    Binary_Head(bin)[Flex_Len(bin)] = 0;
}

INLINE void Term_Binary_Len(Binary* bin, REBLEN len) {
    Set_Flex_Len(bin, len);
    Binary_Head(bin)[len] = 0;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  BINARY! (uses `struct Reb_Any_Series`)
//
//=////////////////////////////////////////////////////////////////////////=//

#define Cell_Blob_Head(v) \
    Binary_Head(Cell_Binary(v))

INLINE Byte *Cell_Blob_At(const Cell* v) {
    return Binary_At(Cell_Binary(v), VAL_INDEX(v));
}

INLINE Byte *Cell_Blob_Tail(const Cell* v) {
    return Flex_Tail(Byte, Cell_Binary(v));
}

// !!! RE: Cell_Blob_At_Head() see remarks on Cell_List_At_Head()
//
#define Cell_Blob_At_Head(v,n) \
    Binary_At(Cell_Binary(v), (n))

#define VAL_BYTE_SIZE(v) \
    BYTE_SIZE(Cell_Flex(v))

// defined as an inline to avoid side effects in:

#define Init_Blob(out,binary) \
    Init_Any_Series((out), TYPE_BINARY, (binary))
