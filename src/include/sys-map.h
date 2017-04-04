//
//  File: %sys-map.h
//  Summary: {Definitions for REBMAP}
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
// Maps are implemented as a light hashing layer on top of an array.  The
// hash indices are stored in the series node's "misc", while the values are
// retained in pairs as `[key val key val key val ...]`.
//
// When there are too few values to warrant hashing, no hash indices are
// made and the array is searched linearly.  This is indicated by the hashlist
// being NULL.
//
// Though maps are not considered a series in the "ANY-SERIES!" value sense,
// they are implemented using series--and hence are in %sys-series.h, at least
// until a better location for the definition is found.
//
// !!! Should there be a MAP_LEN()?  Current implementation has NONE in
// slots that are unused, so can give a deceptive number.  But so can
// objects with hidden fields, locals in paramlists, etc.
//

struct Reb_Map {
    struct Reb_Array pairlist; // hashlist is held in ->link.hashlist
};

inline static REBARR *MAP_PAIRLIST(REBMAP *m) {
    assert(Get_Ser_Flag(&(m)->pairlist, ARRAY_FLAG_PAIRLIST));
    return (&(m)->pairlist);
}

#define MAP_HASHLIST(m) \
    (AS_SERIES(MAP_PAIRLIST(m))->link.hashlist)

#define MAP_HASHES(m) \
    SER_HEAD(MAP_HASHLIST(m))

#define AS_MAP(s) \
    cast(REBMAP*, (s))




inline static REBMAP *VAL_MAP(const RELVAL *v) {
    assert(IS_MAP(v));

    // Ren-C introduced const REBVAL* usage, but propagating const vs non
    // const REBSER pointers didn't show enough benefit to be worth the
    // work in supporting them (at this time).  Mutability cast needed.
    //
    return AS_MAP(m_cast(RELVAL*, v)->payload.any_series.series);
}

inline static REBCNT Length_Map(REBMAP *map)
{
    REBVAL *v = KNOWN(ARR_HEAD(MAP_PAIRLIST(map)));

    REBCNT count = 0;
    for (; NOT_END(v); v += 2) {
        if (!IS_VOID(v + 1))
            ++count;
    }

    return count;
}
