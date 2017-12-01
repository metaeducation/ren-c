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
// A "paired" series hands out its handle as the REBVAL that does *not* have
// REBSER header bits scanned on it.  This value is always mutable.  The
// key, on the other hand, will only allow modifications if it is unmanaged
// (this stops inadvertent writes for other purposes from clearing the managed
// bit).
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

inline static REBVAL *PAIRING_KEY(REBVAL *pairing) {
    return pairing - 1;
}



#define VAL_PAIR(v) \
    ((v)->payload.pair)

#define VAL_PAIR_X(v) \
    VAL_DECIMAL(PAIRING_KEY((v)->payload.pair))

#define VAL_PAIR_Y(v) \
    VAL_DECIMAL((v)->payload.pair)

#define VAL_PAIR_X_INT(v) \
    ROUND_TO_INT(VAL_PAIR_X(v))

#define VAL_PAIR_Y_INT(v) \
    ROUND_TO_INT(VAL_PAIR_Y(v))

inline static void SET_PAIR(RELVAL *v, float x, float y) {
    VAL_RESET_HEADER(v, REB_PAIR);
    v->payload.pair = Alloc_Pairing();
    Init_Decimal(PAIRING_KEY((v)->payload.pair), x);
    Init_Decimal((v)->payload.pair, y);
    Manage_Pairing((v)->payload.pair);
}

inline static void SET_ZEROED(RELVAL *v, enum Reb_Kind kind) {
    //
    // !!! SET_ZEROED is a capturing of a dodgy behavior of R3-Alpha,
    // which was to assume that clearing the payload of a value and then
    // setting the header made it the `zero?` of that type.  Review uses.
    //
    if (kind == REB_PAIR) {
        SET_PAIR(v, 0, 0); // !!! inefficient, performs allocation, review
    }
    else {
        VAL_RESET_HEADER(v, kind);
        CLEAR(&v->extra, sizeof(union Reb_Value_Extra));
        CLEAR(&v->payload, sizeof(union Reb_Value_Payload));
    }
}
