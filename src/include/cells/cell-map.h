//
//  file: %cell-map.h
//  summary: "Definitions for MAP! Cells"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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

#define CELL_MAP_PAIRLIST(c)  CELL_NODE1(c)

INLINE const Map* VAL_MAP(const Cell* c) {
    assert(Heart_Of(c) == TYPE_MAP);

    Node* node = CELL_MAP_PAIRLIST(c);
    if (Not_Node_Readable(node))
        fail (Error_Series_Data_Freed_Raw());

    return cast(Map*, node);  // identity is the PairList
}

#define VAL_MAP_Ensure_Mutable(v) \
    m_cast(Map*, VAL_MAP(Ensure_Mutable(v)))

#define VAL_MAP_Known_Mutable(v) \
    m_cast(Map*, VAL_MAP(Known_Mutable(v)))


// A map has an additional hash element hidden in the ->extra field of the
// Stub which needs to be given to memory management as well.
//
INLINE Element* Init_Map(Init(Element) out, Map* map)
{
    if (MAP_HASHLIST(map))
        Force_Flex_Managed(MAP_HASHLIST(map));

    Force_Flex_Managed(MAP_PAIRLIST(map));

    Reset_Cell_Header_Noquote(TRACK(out), CELL_MASK_MAP);
    Corrupt_Unused_Field(out->extra.corrupt);
    CELL_MAP_PAIRLIST(out) = MAP_PAIRLIST(map);
    Corrupt_Unused_Field(out->payload.split.two.corrupt);

    return out;
}
