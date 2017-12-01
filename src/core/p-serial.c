//
//  File: %p-serial.c
//  Summary: "serial port interface"
//  Section: ports
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2013 REBOL Technologies
// Copyright 2013-2017 Rebol Open Source Contributors
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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"
#include "reb-evtypes.h"

#define MAX_SERIAL_DEV_PATH 128

//
//  Serial_Actor: C
//
static REB_R Serial_Actor(REBFRM *frame_, REBCTX *port, REBSYM action)
{
    FAIL_IF_BAD_PORT(port);

    REBVAL *spec = CTX_VAR(port, STD_PORT_SPEC);
    REBVAL *path = Obj_Value(spec, STD_PORT_SPEC_HEAD_REF);
    if (path == NULL)
        fail (Error_Invalid_Spec_Raw(spec));

    REBREQ *req = Ensure_Port_State(port, RDI_SERIAL);
    struct devreq_serial *serial = DEVREQ_SERIAL(req);

    // Actions for an unopened serial port:
    if (NOT(req->flags & RRF_OPEN)) {
        switch (action) {

        case SYM_REFLECT: {
            INCLUDE_PARAMS_OF_REFLECT;

            UNUSED(ARG(value));
            REBSYM property = VAL_WORD_SYM(ARG(property));
            assert(property != SYM_0);

            switch (property) {
            case SYM_OPEN_Q:
                return R_FALSE;

            default:
                break; }

            fail (Error_On_Port(RE_NOT_OPEN, port, -12)); }

        case SYM_OPEN: {
            REBVAL *path = Obj_Value(spec, STD_PORT_SPEC_SERIAL_PATH);
            if (NOT(IS_FILE(path) || IS_STRING(path) || IS_BINARY(path)))
                fail (Error_Invalid_Port_Arg_Raw(path));

            serial->path = path;

            REBVAL *speed = Obj_Value(spec, STD_PORT_SPEC_SERIAL_SPEED);
            if (NOT(IS_INTEGER(speed)))
                fail (Error_Invalid_Port_Arg_Raw(speed));

            serial->baud = VAL_INT32(speed);

            REBVAL *size = Obj_Value(spec, STD_PORT_SPEC_SERIAL_DATA_SIZE);
            if (!IS_INTEGER(size)
                || VAL_INT64(size) < 5
                || VAL_INT64(size) > 8
            ){
                fail (Error_Invalid_Port_Arg_Raw(size));
            }
            serial->data_bits = VAL_INT32(size);

            REBVAL *stop = Obj_Value(spec, STD_PORT_SPEC_SERIAL_STOP_BITS);
            if (!IS_INTEGER(stop)
                || VAL_INT64(stop) < 1
                || VAL_INT64(stop) > 2
            ){
                fail (Error_Invalid_Port_Arg_Raw(stop));
            }
            serial->stop_bits = VAL_INT32(stop);

            REBVAL *parity = Obj_Value(spec, STD_PORT_SPEC_SERIAL_PARITY);
            if (IS_BLANK(parity)) {
                serial->parity = SERIAL_PARITY_NONE;
            }
            else {
                if (!IS_WORD(parity))
                    fail (Error_Invalid_Port_Arg_Raw(parity));

                switch (VAL_WORD_SYM(parity)) {
                case SYM_ODD:
                    serial->parity = SERIAL_PARITY_ODD;
                    break;

                case SYM_EVEN:
                    serial->parity = SERIAL_PARITY_EVEN;
                    break;

                default:
                    fail (Error_Invalid_Port_Arg_Raw(parity));
                }
            }

            REBVAL *flow = Obj_Value(spec, STD_PORT_SPEC_SERIAL_FLOW_CONTROL);
            if (IS_BLANK(flow)) {
                serial->flow_control = SERIAL_FLOW_CONTROL_NONE;
            }
            else {
                if (!IS_WORD(flow))
                    fail (Error_Invalid_Port_Arg_Raw(flow));

                switch (VAL_WORD_SYM(flow)) {
                case SYM_HARDWARE:
                    serial->flow_control = SERIAL_FLOW_CONTROL_HARDWARE;
                    break;

                case SYM_SOFTWARE:
                    serial->flow_control = SERIAL_FLOW_CONTROL_SOFTWARE;
                    break;

                default:
                    fail (Error_Invalid_Port_Arg_Raw(flow));
                }
            }

            OS_DO_DEVICE(req, RDC_OPEN);
            req->flags |= RRF_OPEN;
            goto return_port; }

        case SYM_CLOSE:
            goto return_port;

        default:
            fail (Error_On_Port(RE_NOT_OPEN, port, -12));
        }
    }

    // Actions for an open socket:
    switch (action) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value));
        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_OPEN_Q:
            return R_TRUE;

        default:
            break;
        }

        break; }

    case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PAR(source));
        if (REF(part)) {
            UNUSED(ARG(limit));
            fail (Error_Bad_Refines_Raw());
        }
        if (REF(seek)) {
            UNUSED(ARG(index));
            fail (Error_Bad_Refines_Raw());
        }
        UNUSED(PAR(string)); // handled in dispatcher
        UNUSED(PAR(lines)); // handled in dispatcher

        // Setup the read buffer (allocate a buffer if needed):
        REBVAL *data = CTX_VAR(port, STD_PORT_DATA);
        if (!IS_BINARY(data))
            Init_Binary(data, Make_Binary(32000));

        REBSER *ser = VAL_SERIES(data);
        req->length = SER_AVAIL(ser); // space available
        if (req->length < 32000 / 2)
            Extend_Series(ser, 32000);
        req->length = SER_AVAIL(ser);

        req->common.data = BIN_TAIL(ser); // write at tail

        req->actual = 0; // Actual for THIS read, not for total.

#ifdef DEBUG_SERIAL
        printf("(max read length %d)", req->length);
#endif

        OS_DO_DEVICE(req, RDC_READ); // recv can happen immediately

#ifdef DEBUG_SERIAL
        for (len = 0; len < req->actual; len++) {
            if (len % 16 == 0) printf("\n");
            printf("%02x ", req->common.data[len]);
        }
        printf("\n");
#endif
        goto return_port; }

    case SYM_WRITE: {
        INCLUDE_PARAMS_OF_WRITE;

        UNUSED(PAR(destination));

        if (REF(seek)) {
            UNUSED(ARG(index));
            fail (Error_Bad_Refines_Raw());
        }
        if (REF(append))
            fail (Error_Bad_Refines_Raw());
        if (REF(allow)) {
            UNUSED(ARG(access));
            fail (Error_Bad_Refines_Raw());
        }
        if (REF(lines))
            fail (Error_Bad_Refines_Raw());

        // Determine length. Clip /PART to size of binary if needed.

        REBVAL *data = ARG(data);
        REBCNT len = VAL_LEN_AT(data);
        if (REF(part)) {
            REBCNT n = Int32s(ARG(limit), 0);
            if (n <= len)
                len = n;
        }

        Move_Value(CTX_VAR(port, STD_PORT_DATA), data); // keep it GC safe
        req->length = len;
        req->common.data = VAL_BIN_AT(data);
        req->actual = 0;

        //Print("(write length %d)", len);
        OS_DO_DEVICE(req, RDC_WRITE); // send can happen immediately
        goto return_port; }

    case SYM_ON_WAKE_UP: {
        // Update the port object after a READ or WRITE operation.
        // This is normally called by the WAKE-UP function.

        REBVAL *data = CTX_VAR(port, STD_PORT_DATA);
        if (req->command == RDC_READ) {
            if (IS_BINARY(data)) {
                SET_SERIES_LEN(
                    VAL_SERIES(data),
                    VAL_LEN_HEAD(data) + req->actual
                );
            }
        }
        else if (req->command == RDC_WRITE) {
            Init_Blank(data);  // Write is done.
        }
        return R_BLANK; }

    case SYM_CLOSE:
        if (req->flags & RRF_OPEN) {
            OS_DO_DEVICE(req, RDC_CLOSE);
            req->flags &= ~RRF_OPEN;
        }
        goto return_port;

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_PORT, action));

return_port:
    Move_Value(D_OUT, D_ARG(1));
    return R_OUT;
}


//
//  get-serial-actor-handle: native [
//
//  {Retrieve handle to the native actor for the serial port}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_serial_actor_handle)
{
    Make_Port_Actor_Handle(D_OUT, &Serial_Actor);
    return R_OUT;
}
