//
//  File: %sys-pair.h
//  Summary: {Definitions for Pairing Series and the Pair Datatype}
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
// A "pairing" fits in a REBSER node, but actually holds two distinct REBVALs.
//
// !!! There is consideration of whether series payloads of length 2 might
// be directly allocated as paireds.  This would require positioning such
// series in the pool so that they abutted against END markers.  It would be
// premature optimization to do it right now, but the design leaves it open.
//
// PAIR! values are implemented using the pairing in Ren-C, which is to say
// that they are garbage collected and can hold any two values--not just
// two numbers.
//

inline static REBVAL *PAIRING_KEY(REBVAL *paired) {
    return paired + 1;
}



#define VAL_PAIR(v) \
    ((v)->payload.pair)

#define VAL_PAIR_FIRST(v) \
    PAIRING_KEY((v)->payload.pair)

#define VAL_PAIR_SECOND(v) \
    ((v)->payload.pair)

inline static REBDEC VAL_PAIR_X_DEC(const RELVAL *v) {
    if (IS_INTEGER(VAL_PAIR_FIRST(v)))
        return VAL_INT64(VAL_PAIR_FIRST(v));
    return VAL_DECIMAL(VAL_PAIR_FIRST(v));
}

inline static REBDEC VAL_PAIR_Y_DEC(const RELVAL *v) {
    if (IS_INTEGER(VAL_PAIR_SECOND(v)))
        return VAL_INT64(VAL_PAIR_SECOND(v));
    return VAL_DECIMAL(VAL_PAIR_SECOND(v));
}

inline static REBI64 VAL_PAIR_X_INT(const RELVAL *v) {
    if (IS_INTEGER(VAL_PAIR_FIRST(v)))
        return VAL_INT64(VAL_PAIR_FIRST(v));
    return ROUND_TO_INT(VAL_DECIMAL(VAL_PAIR_FIRST(v)));
}

inline static REBI64 VAL_PAIR_Y_INT(const RELVAL *v) {
    if (IS_INTEGER(VAL_PAIR_SECOND(v)))
        return VAL_INT64(VAL_PAIR_SECOND(v));
    return ROUND_TO_INT(VAL_DECIMAL(VAL_PAIR_SECOND(v)));
}

inline static REBVAL *Init_Pair_Dec(RELVAL *out, float x, float y) {
    RESET_CELL(out, REB_PAIR);
    out->payload.pair = Alloc_Pairing();
    Init_Decimal(PAIRING_KEY(out->payload.pair), x);
    Init_Decimal(out->payload.pair, y);
    Manage_Pairing(out->payload.pair);
    return KNOWN(out);
}

inline static REBVAL *Init_Pair_Int(RELVAL *out, REBI64 x, REBI64 y) {
    RESET_CELL(out, REB_PAIR);
    out->payload.pair = Alloc_Pairing();
    Init_Integer(PAIRING_KEY(out->payload.pair), x);
    Init_Integer(out->payload.pair, y);
    Manage_Pairing(out->payload.pair);
    return KNOWN(out);
}

inline static REBVAL *Init_Pair(
    RELVAL *out,
    const REBVAL *first,
    const REBVAL *second
){
    RESET_CELL(out, REB_PAIR);
    assert(IS_INTEGER(first) or IS_DECIMAL(first));
    assert(IS_INTEGER(second) or IS_DECIMAL(second));
    out->payload.pair = Alloc_Pairing();
    Move_Value(PAIRING_KEY(out->payload.pair), first);
    Move_Value(out->payload.pair, second);
    Manage_Pairing(out->payload.pair);
    return KNOWN(out);
}


inline static REBVAL *Init_Zeroed_Hack(RELVAL *out, enum Reb_Kind kind) {
    //
    // !!! This captures of a dodgy behavior of R3-Alpha, which was to assume
    // that clearing the payload of a value and then setting the header made
    // it the `zero?` of that type.  Review uses.
    //
    if (kind == REB_PAIR) {
        Init_Pair_Int(out, 0, 0);
    }
    else {
        RESET_CELL(out, kind);
        CLEAR(&out->extra, sizeof(union Reb_Value_Extra));
        CLEAR(&out->payload, sizeof(union Reb_Value_Payload));
    }
    return KNOWN(out);
}
