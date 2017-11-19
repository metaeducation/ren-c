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

    REBVAL *arg;    // action argument value
    REBINT result;  // IO result
    REBCNT len;     // generic length
    REBSER *ser;    // simplifier

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

        case SYM_OPEN:
            arg = Obj_Value(spec, STD_PORT_SPEC_SERIAL_PATH);
            if (NOT(IS_FILE(arg) || IS_STRING(arg) || IS_BINARY(arg)))
                fail (Error_Invalid_Port_Arg_Raw(arg));

            serial->path = arg;

            arg = Obj_Value(spec, STD_PORT_SPEC_SERIAL_SPEED);
            if (NOT(IS_INTEGER(arg)))
                fail (Error_Invalid_Port_Arg_Raw(arg));

            serial->baud = VAL_INT32(arg);
            //Secure_Port(SYM_SERIAL, ???, path, ser);
            arg = Obj_Value(spec, STD_PORT_SPEC_SERIAL_DATA_SIZE);
            if (!IS_INTEGER(arg)
                || VAL_INT64(arg) < 5
                || VAL_INT64(arg) > 8
            ) {
                fail (Error_Invalid_Port_Arg_Raw(arg));
            }
            serial->data_bits = VAL_INT32(arg);

            arg = Obj_Value(spec, STD_PORT_SPEC_SERIAL_STOP_BITS);
            if (!IS_INTEGER(arg)
                || VAL_INT64(arg) < 1
                || VAL_INT64(arg) > 2
            ) {
                fail (Error_Invalid_Port_Arg_Raw(arg));
            }
            serial->stop_bits = VAL_INT32(arg);

            arg = Obj_Value(spec, STD_PORT_SPEC_SERIAL_PARITY);
            if (IS_BLANK(arg)) {
                serial->parity = SERIAL_PARITY_NONE;
            } else {
                if (!IS_WORD(arg))
                    fail (Error_Invalid_Port_Arg_Raw(arg));

                switch (VAL_WORD_SYM(arg)) {
                    case SYM_ODD:
                        serial->parity = SERIAL_PARITY_ODD;
                        break;
                    case SYM_EVEN:
                        serial->parity = SERIAL_PARITY_EVEN;
                        break;
                    default:
                        fail (Error_Invalid_Port_Arg_Raw(arg));
                }
            }

            arg = Obj_Value(spec, STD_PORT_SPEC_SERIAL_FLOW_CONTROL);
            if (IS_BLANK(arg)) {
                serial->flow_control = SERIAL_FLOW_CONTROL_NONE;
            } else {
                if (!IS_WORD(arg))
                    fail (Error_Invalid_Port_Arg_Raw(arg));

                switch (VAL_WORD_SYM(arg)) {
                    case SYM_HARDWARE:
                        serial->flow_control = SERIAL_FLOW_CONTROL_HARDWARE;
                        break;
                    case SYM_SOFTWARE:
                        serial->flow_control = SERIAL_FLOW_CONTROL_SOFTWARE;
                        break;
                    default:
                        fail (Error_Invalid_Port_Arg_Raw(arg));
                }
            }

            if (OS_DO_DEVICE(req, RDC_OPEN))
                fail (Error_On_Port(RE_CANNOT_OPEN, port, -12));
            req->flags |= RRF_OPEN;
            goto return_port;

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
        arg = CTX_VAR(port, STD_PORT_DATA);
        if (!IS_STRING(arg) && !IS_BINARY(arg)) {
            Init_Binary(arg, Make_Binary(32000));
        }
        ser = VAL_SERIES(arg);
        req->length = SER_AVAIL(ser); // space available
        if (req->length < 32000/2) Extend_Series(ser, 32000);
        req->length = SER_AVAIL(ser);

        // This used STR_TAIL (obsolete, equivalent to BIN_TAIL) but was it
        // sure the series was byte sized?  Added in a check.
        assert(BYTE_SIZE(ser));
        req->common.data = BIN_TAIL(ser); // write at tail

        //if (SER_LEN(ser) == 0)
        req->actual = 0;  // Actual for THIS read, not for total.
#ifdef DEBUG_SERIAL
        printf("(max read length %d)", req->length);
#endif
        result = OS_DO_DEVICE(req, RDC_READ); // recv can happen immediately
        if (result < 0) fail (Error_On_Port(RE_READ_ERROR, port, req->error));
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

        // Determine length. Clip /PART to size of string if needed.
        REBVAL *data = ARG(data);
        len = VAL_LEN_AT(data);
        if (REF(part)) {
            REBCNT n = Int32s(ARG(limit), 0);
            if (n <= len) len = n;
        }

        // Setup the write:
        Move_Value(CTX_VAR(port, STD_PORT_DATA), data); // keep it GC safe
        req->length = len;
        req->common.data = VAL_BIN_AT(data);
        req->actual = 0;

        //Print("(write length %d)", len);
        result = OS_DO_DEVICE(req, RDC_WRITE); // send can happen immediately
        if (result < 0)
            fail (Error_On_Port(RE_WRITE_ERROR, port, req->error));
        goto return_port; }

    case SYM_ON_WAKE_UP:
        // Update the port object after a READ or WRITE operation.
        // This is normally called by the WAKE-UP function.
        arg = CTX_VAR(port, STD_PORT_DATA);
        if (req->command == RDC_READ) {
            if (ANY_BINSTR(arg)) {
                SET_SERIES_LEN(
                    VAL_SERIES(arg),
                    VAL_LEN_HEAD(arg) + req->actual
                );
            }
        }
        else if (req->command == RDC_WRITE) {
            Init_Blank(arg);  // Write is done.
        }
        return R_BLANK;

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
