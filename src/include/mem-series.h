//
//  File: %mem-series.h
//  Summary: {Low level memory-oriented access routines for series}
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
// These are implementation details of series that most code should not need
// to use.
//

// Non-series-internal code needs to read SER_WIDE but should not be
// needing to set it directly.
//
// !!! Can't `assert((w) < MAX_SERIES_WIDE)` without triggering "range of
// type makes this always false" warning; C++ build could sense if it's a
// REBYTE and dodge the comparison if so.
//

#define MAX_SERIES_WIDE 0x100

inline static void SER_SET_WIDE(REBSER *s, REBYTE w) {
    CLEAR_8_RIGHT_BITS(s->info.bits);
    s->info.bits |= FLAGBYTE_RIGHT(w);
}

//
// Bias is empty space in front of head:
//

inline static REBCNT SER_BIAS(REBSER *s) {
    assert(Get_Ser_Info(s, SERIES_INFO_HAS_DYNAMIC));
    return cast(REBCNT, ((s)->content.dynamic.bias >> 16) & 0xffff);
}

#define MAX_SERIES_BIAS 0x1000

inline static void SER_SET_BIAS(REBSER *s, REBCNT bias) {
    assert(Get_Ser_Info(s, SERIES_INFO_HAS_DYNAMIC));
    s->content.dynamic.bias =
        (s->content.dynamic.bias & 0xffff) | (bias << 16);
}

#define SER_ADD_BIAS(s,b) \
    ((s)->content.dynamic.bias += (b << 16))

#define SER_SUB_BIAS(s,b) \
    ((s)->content.dynamic.bias -= (b << 16))

inline static size_t SER_TOTAL(REBSER *s) {
    return (SER_REST(s) + SER_BIAS(s)) * SER_WIDE(s);
}

inline static size_t SER_TOTAL_IF_DYNAMIC(REBSER *s) {
    if (Not_Ser_Info(s, SERIES_INFO_HAS_DYNAMIC))
        return 0;
    return SER_TOTAL(s);
}