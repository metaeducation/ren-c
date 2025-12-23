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
// BLOCK! antiforms are known as PACK!, and used as a mechanism for bundling
// values so they can be passed as a single value.  They are leveraged in
// particular for multi-return, because a SET-WORD! will unpack only the
// first item, while a SET-BLOCK! will unpack others.
//
//      >> pack [<a> <b>]
//      == ~['<a> '<b>]~  ; anti
//
//      >> x: pack [<a> <b>]
//      == <a>
//
//      >> [x y]: pack [<a> <b>]
//      == <a>
//
//      >> x
//      == <a>
//
//      >> y
//      == <b>
//

INLINE Value* Init_Pack_Untracked(Init(Value) out, const Source* a) {
    Init_Any_List_At_Core_Untracked(out, TYPE_BLOCK, a, 0, SPECIFIED);
    Unstably_Antiformize_Unbound_Fundamental(out);
    assert(Is_Pack(out));
    return out;
}

#define Init_Pack(out,a) \
    TRACK(Init_Pack_Untracked((out), (a)))

#define Init_Lifted_Pack(out,a) \
    TRACK(Quasify_Isotopic_Fundamental(Init_Any_List_At_Core_Untracked( \
        (out), TYPE_BLOCK, (a), 0, SPECIFIED)))


//=//// "HEAVY VOID" (EMPTY PACK! ANTIFORM) ///////////////////////////////=//

#define Init_Heavy_Void_Untracked(out) \
    Init_Pack_Untracked((out), g_empty_array)

#define Init_Heavy_Void(out) \
    TRACK(Init_Heavy_Void_Untracked(out))

INLINE bool Is_Heavy_Void(Exact(const Value*) v) {
    if (not Is_Pack(v))
        return false;
    const Element* tail;
    const Element* at = List_At(&tail, v);
    return tail == at;
}

INLINE Element* Init_Lifted_Heavy_Void_Untracked(Sink(Element) out) {
    Init_Any_List_At_Core_Untracked(
        out, TYPE_BLOCK, g_empty_array, 0, SPECIFIED
    );
    Quasify_Isotopic_Fundamental(out);
    return out;
}

#define Init_Lifted_Heavy_Void(out) \
    TRACK(Init_Lifted_Heavy_Void_Untracked((out)))

INLINE bool Is_Lifted_Heavy_Void(const Stable* v) {
    if (not Is_Lifted_Pack(v))
        return false;
    const Element* tail;
    const Element* at = List_At(&tail, v);
    return tail == at;
}

INLINE bool Any_Void(Exact(const Value*) v) {
    return Is_Ghost(v) or Is_Heavy_Void(v);
}

INLINE bool Is_Any_Lifted_Void(const Stable* v) {
    return Is_Lifted_Ghost(v) or Is_Lifted_Heavy_Void(v);
}
