//
//  File: %p-dns.c
//  Summary: "DNS port interface"
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

#include "sys-core.h"
#include "reb-net.h"


//
//  DNS_Actor: C
//
static REB_R DNS_Actor(REBFRM *frame_, REBVAL *port, const REBVAL *verb)
{
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    REBREQ *req = Ensure_Port_State(port, RDI_DNS);
    struct rebol_devreq *sock = Req(req);

    sock->timeout = 4000; // where does this go? !!!

    REBCTX *ctx = VAL_CONTEXT(port);
    REBVAL *spec = CTX_VAR(ctx, STD_PORT_SPEC);

    REBCNT len;

    switch (VAL_WORD_SYM(verb)) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value));
        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_OPEN_Q:
            return Init_Logic(D_OUT, did (sock->flags & RRF_OPEN));

        default:
            break;
        }

        break; }

    case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PAR(source));

        if (REF(part) or REF(seek))
            fail (Error_Bad_Refines_Raw());

        UNUSED(PAR(string)); // handled in dispatcher
        UNUSED(PAR(lines)); // handled in dispatcher

        if (not (sock->flags & RRF_OPEN))
            OS_DO_DEVICE_SYNC(req, RDC_OPEN);

        arg = Obj_Value(spec, STD_PORT_SPEC_NET_HOST);

        // A DNS read e.g. of `read dns://66.249.66.140` should do a reverse
        // lookup.  The scheme handler may pass in either a TUPLE! or a string
        // that scans to a tuple, at this time (currently uses a string)
        //
        if (IS_TUPLE(arg)) {
            sock->modes |= RST_REVERSE;
            memcpy(&(ReqNet(req)->remote_ip), VAL_TUPLE(arg), 4);
        }
        else if (IS_TEXT(arg)) {
            REBSIZ utf8_size;
            const REBYTE *utf8 = VAL_UTF8_AT(&utf8_size, arg);

            DECLARE_LOCAL (tuple);
            if (Scan_Tuple(tuple, utf8, utf8_size) != NULL) {
                sock->modes |= RST_REVERSE;
                memcpy(&(ReqNet(req)->remote_ip), VAL_TUPLE(tuple), 4);
            }
            else // lookup string's IP address
                sock->common.data = m_cast(REBYTE*, utf8);
        }
        else
            fail (Error_On_Port(SYM_INVALID_SPEC, port, -10));

        OS_DO_DEVICE_SYNC(req, RDC_READ);

        len = 1;
        goto pick_with_position_in_len; }

    case SYM_PICK: { // FIRST - return result
        if (not (sock->flags & RRF_OPEN))
            fail (Error_On_Port(SYM_NOT_OPEN, port, -12));

        len = Get_Num_From_Arg(arg); // Position

       pick_with_position_in_len:

        if (len != 1)
            fail (Error_Out_Of_Range(arg));

        assert(sock->flags & RRF_DONE); // R3-Alpha async DNS removed

        if (ReqNet(req)->host_info == NULL) // HOST_NOT_FOUND, NO_ADDRESS
            return nullptr;

        if (sock->modes & RST_REVERSE)
            Init_Text(D_OUT, Make_String_UTF8(cs_cast(sock->common.data)));
        else
            Init_Tuple(D_OUT, cast(REBYTE*, &ReqNet(req)->remote_ip), 4);

        OS_DO_DEVICE_SYNC(req, RDC_CLOSE);
        return D_OUT; }

    case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;

        UNUSED(PAR(spec));

        if (REF(new) or REF(read) or REF(write) or REF(seek) or REF(allow))
            fail (Error_Bad_Refines_Raw());

        OS_DO_DEVICE_SYNC(req, RDC_OPEN);
        RETURN (port); }

    case SYM_CLOSE: {
        OS_DO_DEVICE_SYNC(req, RDC_CLOSE);
        RETURN (port); }

    case SYM_ON_WAKE_UP:
        return Init_Void(D_OUT);

    default:
        break;
    }

    return R_UNHANDLED;
}


//
//  get-dns-actor-handle: native [
//
//  {Retrieve handle to the native actor for DNS}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_dns_actor_handle)
{
    Make_Port_Actor_Handle(D_OUT, &DNS_Actor);
    return D_OUT;
}
