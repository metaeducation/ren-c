//
//  File: %cell-map.h
//  Summary: "Definitions for MAP! Cells"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
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
// * VAL_MAP() not replaced because this is likely to become Cell_Dictionary()
//   (but the discussion has not yet finaliezd).
//

INLINE const Map* VAL_MAP(const Cell* v) {
    assert(Cell_Heart(v) == REB_MAP);
    if (Not_Node_Readable(Cell_Node1(v)))
        fail (Error_Series_Data_Freed_Raw());

    return cast(Map*, Cell_Node1(v));  // identity is the PairList
}

#define VAL_MAP_Ensure_Mutable(v) \
    m_cast(Map*, VAL_MAP(Ensure_Mutable(v)))

#define VAL_MAP_Known_Mutable(v) \
    m_cast(Map*, VAL_MAP(Known_Mutable(v)))
