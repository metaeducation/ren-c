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
// Copyright 2012-2019 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See notes in %extensions/gob/README.md
//

#include "sys-core.h"

#include "tmp-mod-gob.h"

#include "reb-gob.h"

REBTYP *EG_Gob_Type = nullptr;  // (E)xtension (G)lobal

//
//  register-gob-hooks: native [
//
//  {Make the GOB! datatype work with GENERIC actions, comparison ops, etc}
//
//      return: [void!]
//      generics "List for HELP of which generics are supported (unused)"
//          [block!]
//  ]
//
REBNATIVE(register_gob_hooks)
{
    GOB_INCLUDE_PARAMS_OF_REGISTER_GOB_HOOKS;

    Extend_Generics_Someday(ARG(generics));  // !!! vaporware, see comments

    // !!! See notes on Hook_Datatype for this poor-man's substitute for a
    // coherent design of an extensible object system (as per Lisp's CLOS)
    //
    EG_Gob_Type = Hook_Datatype(
        "http://datatypes.rebol.info/gob",
        "graphical object",
        &T_Gob,
        &PD_Gob,
        &CT_Gob,
        &MAKE_Gob,
        &TO_Gob,
        &MF_Gob
    );

    return Init_Void(D_OUT);
}


//
//  unregister-gob-hooks: native [
//
//  {Remove behaviors for GOB! added by REGISTER-GOB-HOOKS}
//
//      return: [void!]
//  ]
//
REBNATIVE(unregister_gob_hooks)
{
    GOB_INCLUDE_PARAMS_OF_UNREGISTER_GOB_HOOKS;

    Unhook_Datatype(EG_Gob_Type);

    return Init_Void(D_OUT);
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
REBNATIVE(map_gob_offset)
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

    REBARR *arr = Make_Array(2);
    Init_Gob(Alloc_Tail_Array(arr), gob);
    Init_Pair_Dec(Alloc_Tail_Array(arr), xo, yo);

    return Init_Block(D_OUT, arr);
}
