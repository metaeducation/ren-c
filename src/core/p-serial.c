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
// Copyright 2013-2016 Rebol Open Source Contributors
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
#include "reb-net.h"
#include "reb-evtypes.h"

#define MAX_SERIAL_DEV_PATH 128

//
//  Serial_Actor: C
//
static REB_R Serial_Actor(struct Reb_Frame *frame_, REBCTX *port, REBSYM action)
{
    REBREQ *req;    // IO request
    REBVAL *spec;   // port spec
    REBVAL *arg;    // action argument value
    REBVAL *val;    // e.g. port number value
    REBINT result;  // IO result
    REBCNT refs;    // refinement argument flags
    REBCNT len;     // generic length
    REBSER *ser;    // simplifier
    REBVAL *path;

    Validate_Port(port, action);

    *D_OUT = *D_ARG(1);

    // Validate PORT fields:
    spec = CTX_VAR(port, STD_PORT_SPEC);
    if (!IS_OBJECT(spec)) fail (Error(RE_INVALID_PORT));
    path = Obj_Value(spec, STD_PORT_SPEC_HEAD_REF);
    if (!path) fail (Error(RE_INVALID_SPEC, spec));

    //if (!IS_FILE(path)) fail (Error(RE_INVALID_SPEC, path));

    req = cast(REBREQ*, Use_Port_State(port, RDI_SERIAL, sizeof(*req)));

    // Actions for an unopened serial port:
    if (!IS_OPEN(req)) {

        switch (action) {

        case SYM_OPEN:
            arg = Obj_Value(spec, STD_PORT_SPEC_SERIAL_PATH);
            if (! (IS_FILE(arg) || IS_STRING(arg) || IS_BINARY(arg)))
                fail (Error(RE_INVALID_PORT_ARG, arg));

            req->special.serial.path = ALLOC_N(REBCHR, MAX_SERIAL_DEV_PATH);
            OS_STRNCPY(
                req->special.serial.path,
                //
                // !!! This is assuming VAL_DATA contains native chars.
                // Should it? (2 bytes on windows, 1 byte on linux/mac)
                //
                SER_AT(REBCHR, VAL_SERIES(arg), VAL_INDEX(arg)),
                MAX_SERIAL_DEV_PATH
            );
            arg = Obj_Value(spec, STD_PORT_SPEC_SERIAL_SPEED);
            if (! IS_INTEGER(arg))
                fail (Error(RE_INVALID_PORT_ARG, arg));

            req->special.serial.baud = VAL_INT32(arg);
            //Secure_Port(SYM_SERIAL, ???, path, ser);
            arg = Obj_Value(spec, STD_PORT_SPEC_SERIAL_DATA_SIZE);
            if (!IS_INTEGER(arg)
                || VAL_INT64(arg) < 5
                || VAL_INT64(arg) > 8
            ) {
                fail (Error(RE_INVALID_PORT_ARG, arg));
            }
            req->special.serial.data_bits = VAL_INT32(arg);

            arg = Obj_Value(spec, STD_PORT_SPEC_SERIAL_STOP_BITS);
            if (!IS_INTEGER(arg)
                || VAL_INT64(arg) < 1
                || VAL_INT64(arg) > 2
            ) {
                fail (Error(RE_INVALID_PORT_ARG, arg));
            }
            req->special.serial.stop_bits = VAL_INT32(arg);

            arg = Obj_Value(spec, STD_PORT_SPEC_SERIAL_PARITY);
            if (IS_BLANK(arg)) {
                req->special.serial.parity = SERIAL_PARITY_NONE;
            } else {
                if (!IS_WORD(arg))
                    fail (Error(RE_INVALID_PORT_ARG, arg));

                switch (VAL_WORD_SYM(arg)) {
                    case SYM_ODD:
                        req->special.serial.parity = SERIAL_PARITY_ODD;
                        break;
                    case SYM_EVEN:
                        req->special.serial.parity = SERIAL_PARITY_EVEN;
                        break;
                    default:
                        fail (Error(RE_INVALID_PORT_ARG, arg));
                }
            }

            arg = Obj_Value(spec, STD_PORT_SPEC_SERIAL_FLOW_CONTROL);
            if (IS_BLANK(arg)) {
                req->special.serial.flow_control = SERIAL_FLOW_CONTROL_NONE;
            } else {
                if (!IS_WORD(arg))
                    fail (Error(RE_INVALID_PORT_ARG, arg));

                switch (VAL_WORD_SYM(arg)) {
                    case SYM_HARDWARE:
                        req->special.serial.flow_control = SERIAL_FLOW_CONTROL_HARDWARE;
                        break;
                    case SYM_SOFTWARE:
                        req->special.serial.flow_control = SERIAL_FLOW_CONTROL_SOFTWARE;
                        break;
                    default:
                        fail (Error(RE_INVALID_PORT_ARG, arg));
                }
            }

            if (OS_DO_DEVICE(req, RDC_OPEN))
                fail (Error_On_Port(RE_CANNOT_OPEN, port, -12));
            SET_OPEN(req);
            return R_OUT;

        case SYM_CLOSE:
            return R_OUT;

        case SYM_OPEN_Q:
            return R_FALSE;

        default:
            fail (Error_On_Port(RE_NOT_OPEN, port, -12));
        }
    }

    // Actions for an open socket:
    switch (action) {

    case SYM_READ:
        refs = Find_Refines(frame_, ALL_READ_REFS);

        // Setup the read buffer (allocate a buffer if needed):
        arg = CTX_VAR(port, STD_PORT_DATA);
        if (!IS_STRING(arg) && !IS_BINARY(arg)) {
            Val_Init_Binary(arg, Make_Binary(32000));
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
        *D_OUT = *arg;
        return R_OUT;

    case SYM_WRITE:
        refs = Find_Refines(frame_, ALL_WRITE_REFS);

        // Determine length. Clip /PART to size of string if needed.
        spec = D_ARG(2);
        len = VAL_LEN_AT(spec);
        if (refs & AM_WRITE_PART) {
            REBCNT n = Int32s(D_ARG(ARG_WRITE_LIMIT), 0);
            if (n <= len) len = n;
        }

        // Setup the write:
        *CTX_VAR(port, STD_PORT_DATA) = *spec;  // keep it GC safe
        req->length = len;
        req->common.data = VAL_BIN_AT(spec);
        req->actual = 0;

        //Print("(write length %d)", len);
        result = OS_DO_DEVICE(req, RDC_WRITE); // send can happen immediately
        if (result < 0) fail (Error_On_Port(RE_WRITE_ERROR, port, req->error));
        break;

    case SYM_UPDATE:
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
            SET_BLANK(arg);  // Write is done.
        }
        return R_BLANK;

    case SYM_OPEN_Q:
        return R_TRUE;

    case SYM_CLOSE:
        if (IS_OPEN(req)) {
            OS_DO_DEVICE(req, RDC_CLOSE);
            SET_CLOSED(req);
        }
        break;

    default:
        fail (Error_Illegal_Action(REB_PORT, action));
    }

    return R_OUT;
}


//
//  Init_Serial_Scheme: C
//
void Init_Serial_Scheme(void)
{
    Register_Scheme(Canon(SYM_SERIAL), 0, Serial_Actor);
}
