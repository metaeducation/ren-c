//
//  File: %p-console.c
//  Summary: "console port interface"
//  Section: ports
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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
// !!! R3's CONSOLE "actor" came with only a READ method and no WRITE.
// Writing was done through Prin_OS_String() to the Dev_StdIO device without
// going through a port.  SYSTEM/PORTS/INPUT was thus created from it.
//

#include "sys-core.h"

//
//  Console_Actor: C
//
REB_R Console_Actor(REBFRM *frame_, REBVAL *port, const REBVAL *verb)
{
    REBCTX *ctx = VAL_CONTEXT(port);

    REBREQ *req = Ensure_Port_State(port, RDI_STDIO);

    switch (VAL_WORD_SYM(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value)); // implied by `port`

        REBSYM property = VAL_WORD_SYM(ARG(property));
        switch (property) {
          case SYM_OPEN_Q:
            return Init_Logic(D_OUT, did (Req(req)->flags & RRF_OPEN));

          default:
            break;
        }

        break; }

      case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PAR(source));

        if (REF(part))
            fail (Error_Bad_Refines_Raw());

        if (REF(seek))
            fail (Error_Bad_Refines_Raw());

        UNUSED(PAR(string)); // handled in dispatcher
        UNUSED(PAR(lines)); // handled in dispatcher

        // If not open, open it:
        if (not (Req(req)->flags & RRF_OPEN))
            OS_DO_DEVICE_SYNC(req, RDC_OPEN);

        // !!! A fixed size buffer is used to gather console input.  This is
        // re-used between READ requests.
        //
        //https://github.com/rebol/rebol-issues/issues/2364
        //
        const REBCNT readbuf_size = 32 * 1024;

        REBVAL *data = CTX_VAR(ctx, STD_PORT_DATA);
        if (not IS_BINARY(data))
            Init_Binary(data, Make_Binary(readbuf_size));
        else {
            assert(VAL_INDEX(data) == 0);
            assert(VAL_LEN_AT(data) == 0);
        }

        Req(req)->common.binary = data;  // appends to tail (but it's empty)
        Req(req)->length = readbuf_size;

        OS_DO_DEVICE_SYNC(req, RDC_READ);

        // Give back a BINARY! which is as large as the portion of the buffer
        // that was used, and clear the buffer for reuse.
        //
        return rebValueQ("copy", data, "elide clear", data, rebEND); }

      case SYM_OPEN:
        Req(req)->flags |= RRF_OPEN;
        RETURN (port);

      case SYM_CLOSE:
        Req(req)->flags &= ~RRF_OPEN;
        RETURN (port);

      default:
        break;
    }

    return R_UNHANDLED;
}
