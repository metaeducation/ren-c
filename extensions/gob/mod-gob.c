//
//  File: %mod-gob.c
//  Summary: "GOB! extension main C file"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 Atronix Engineering
// Copyright 2012-2019 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See notes in %extensions/gob/README.md
//

#include "sys-core.h"

#include "tmp-mod-gob.h"

#include "reb-gob.h"

REBTYP *EG_Gob_Type = nullptr;  // (E)xtension (G)lobal

Symbol(const*) S_Gob(void) {
    return Canon(GOB_X);
}


//
//  startup*: native [
//
//  {Make the GOB! datatype work with GENERIC actions, comparison ops, etc}
//
//      return: <none>
//  ]
//
DECLARE_NATIVE(startup_p)
{
    GOB_INCLUDE_PARAMS_OF_STARTUP_P;

    Extend_Generics_Someday(nullptr);  // !!! vaporware, see comments

    // !!! See notes on Hook_Datatype for this poor-man's substitute for a
    // coherent design of an extensible object system (as per Lisp's CLOS)
    //
    EG_Gob_Type = Hook_Datatype(
        "http://datatypes.rebol.info/gob",
        "graphical object",
        &S_Gob,
        &T_Gob,
        &CT_Gob,
        &MAKE_Gob,
        &TO_Gob,
        &MF_Gob
    );

    return NONE;
}


//
//  shutdown*: native [
//
//  {Remove behaviors for GOB! added by STARTUP*}
//
//      return: <none>
//  ]
//
DECLARE_NATIVE(shutdown_p)
{
    GOB_INCLUDE_PARAMS_OF_SHUTDOWN_P;

    Unhook_Datatype(EG_Gob_Type);

    return NONE;
}


//
//  Map_Gob_Inner: C
//
// Map a higher level gob coordinate to a lower level.
// Returns GOB and sets new offset pair.
//
static REBGOB *Map_Gob_Inner(REBGOB *gob, REBD32 *xo, REBD32 *yo)
{
    REBINT max_depth = 1000; // avoid infinite loops

    REBD32 x = 0;
    REBD32 y = 0;

    while (GOB_PANE(gob) && (max_depth-- > 0)) {
        REBINT len = GOB_LEN(gob);

        REBVAL *item = GOB_HEAD(gob) + len - 1;

        REBINT n;
        for (n = 0; n < len; ++n, --item) {
            REBGOB *child = VAL_GOB(item);
            if (
                (*xo >= x + GOB_X(child)) &&
                (*xo <  x + GOB_X(child) + GOB_W(child)) &&
                (*yo >= y + GOB_Y(child)) &&
                (*yo <  y + GOB_Y(child) + GOB_H(child))
            ){
                x += GOB_X(child);
                y += GOB_Y(child);
                gob = child;
                break;
            }
        }
        if (n >= len)
            break; // not found
    }

    *xo -= x;
    *yo -= y;

    return gob;
}


//
//  map-gob-offset: native [
//
//  {Translate gob and offset to deepest gob and offset in it}
//
//      return: [block!]
//          "[GOB! PAIR!] 2-element block"
//      gob [gob!]
//          "Starting object"
//      xy [pair!]
//          "Staring offset"
//      /reverse
//          "Translate from deeper gob to top gob."
//  ]
//
DECLARE_NATIVE(map_gob_offset)
{
    GOB_INCLUDE_PARAMS_OF_MAP_GOB_OFFSET;
    UNUSED(ARG(gob));
    UNUSED(ARG(xy));
    UNUSED(REF(reverse));

    REBGOB *gob = VAL_GOB(ARG(gob));
    REBD32 xo = VAL_PAIR_X_DEC(ARG(xy));
    REBD32 yo = VAL_PAIR_Y_DEC(ARG(xy));

    if (REF(reverse)) {
        REBINT max_depth = 1000; // avoid infinite loops
        while (
            GOB_PARENT(gob)
            && (max_depth-- > 0)
            && !GET_GOB_FLAG(gob, GOBF_WINDOW)
        ){
            xo += GOB_X(gob);
            yo += GOB_Y(gob);
            gob = GOB_PARENT(gob);
        }
    }
    else {
        xo = VAL_PAIR_X_DEC(ARG(xy));
        yo = VAL_PAIR_Y_DEC(ARG(xy));
        gob = Map_Gob_Inner(gob, &xo, &yo);
    }

    Array(*) arr = Make_Array(2);
    Init_Gob(Alloc_Tail_Array(arr), gob);
    Init_Pair_Dec(Alloc_Tail_Array(arr), xo, yo);

    return Init_Block(OUT, arr);
}
