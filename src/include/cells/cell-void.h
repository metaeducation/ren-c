//
//  file: %cell-void.h
//  summary: "Unstable Antiform State of ~[]~ Used For Opting Out"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2025 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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
// This unstable antiform can't be used in conventional assignments.  The
// assignments that do allow it will actually remove keys from a mapping
// entirely, because it has no variable representation.
//
// It is sensibly represented as a PACK! of length 0.
//

#define Init_Void_Untracked(out) \
    Init_Pack_Untracked((out), g_empty_array)

#define Init_Void(out) \
    TRACK(Init_Void_Untracked(out))

INLINE bool Is_Void(Need(const Atom*) v) {
    if (not Is_Pack(v))
        return false;
    const Element* tail;
    const Element* at = Cell_List_At(&tail, v);
    return tail == at;
}

INLINE Element* Init_Meta_Of_Void_Untracked(Sink(Element) out) {
    Init_Any_List_At_Core_Untracked(out, TYPE_BLOCK, g_empty_array, 0, SPECIFIED);
    QUOTE_BYTE(out) = QUASIFORM_2_COERCE_ONLY;
    return out;
}

#define Init_Meta_Of_Void(out) \
    TRACK(Init_Meta_Of_Void_Untracked((out)))

INLINE bool Is_Meta_Of_Void(const Cell* v) {
    if (not Is_Meta_Of_Pack(v))
        return false;
    const Element* tail;
    const Element* at = Cell_List_At(&tail, v);
    return tail == at;
}

INLINE bool Is_Ghost_Or_Void(Need(Atom*) v) {
    return Is_Ghost(v) or Is_Void(v);
}

INLINE bool Is_Meta_Of_Ghost_Or_Void(const Cell* v) {
    return Is_Meta_Of_Ghost(v) or Is_Meta_Of_Void(v);
}
