//
//  File: %mod-stdio.c
//  Summary: "Standard Input And Output Ports"
//  Section: ports
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
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


#include "sys-core.h"

#include "tmp-mod-stdio.h"

EXTERN_C REBDEV Dev_StdIO;


extern REB_R Console_Actor(REBFRM *frame_, REBVAL *port, const REBVAL *verb);

//
//  get-console-actor-handle: native [
//
//  {Retrieve handle to the native actor for console}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_console_actor_handle)
{
    Make_Port_Actor_Handle(D_OUT, &Console_Actor);
    return D_OUT;
}


//
//  export register-stdio-device: native [
//
//  ]
//
REBNATIVE(register_stdio_device)
{
    OS_Register_Device(&Dev_StdIO);

    REBREQ *req = OS_Make_Devreq(&Dev_StdIO);

    // !!! "The device is already open, so this call will just setup the
    // request fields properly." (?)

    OS_DO_DEVICE_SYNC(req, RDC_OPEN);

    Free_Req(req);

    return Init_Void(D_OUT);
}


// Encoding options (reduced down to just being used by WRITE-STDOUT)
//
enum encoding_opts {
    OPT_ENC_0 = 0,
    OPT_ENC_RAW = 1 << 0
};


//
//  Prin_OS_String: C
//
// Print a string (with no line terminator).
//
// The encoding options are OPT_ENC_XXX flags OR'd together.
//
static void Prin_OS_String(const REBYTE *utf8, REBSIZ size, REBFLGS opts)
{
    REBREQ *rebreq = OS_Make_Devreq(&Dev_StdIO);
    struct rebol_devreq *req = Req(rebreq);

    req->flags |= RRF_FLUSH;
    if (opts & OPT_ENC_RAW)
        req->modes &= ~RFM_TEXT;
    else
        req->modes |= RFM_TEXT;

    req->actual = 0;

    DECLARE_LOCAL (temp);
    SET_END(temp);

    // !!! The historical division of labor between the "core" and the "host"
    // is that the host doesn't know how to poll for cancellation.  So data
    // gets broken up into small batches and it's this loop that has access
    // to the core "Do_Signals_Throws" query.  Hence one can send a giant
    // string to the OS_DO_DEVICE with RDC_WRITE and be able to interrupt it,
    // even though that device request could block forever in theory.
    //
    // There may well be a better way to go about this.
    //
    req->common.data = m_cast(REBYTE*, utf8); // !!! promises to not write
    while (size > 0) {
        if (Do_Signals_Throws(temp))
            fail (Error_No_Catch_For_Throw(temp));

        assert(IS_END(temp));

        // !!! Req_SIO->length is actually the "size", e.g. number of bytes.
        //
        if (size <= 1024)
            req->length = size;
        else if (not (opts & OPT_ENC_RAW))
            req->length = 1024;
        else {
            // Correct for UTF-8 batching so we don't span an encoded
            // character, back off until we hit a valid leading character.
            // Start by scanning 4 bytes back since that's the longest valid
            // UTF-8 encoded character.
            //
            req->length = 1020;
            while (Is_Continuation_Byte_If_Utf8(req->common.data[req->length]))
                ++req->length;
            assert(req->length <= 1024);
        }

        OS_DO_DEVICE_SYNC(rebreq, RDC_WRITE);

        req->common.data += req->length;
        size -= req->length;
    }

    Free_Req(rebreq);
}


//
//  Print_OS_Line: C
//
// Print a new line.
//
void Print_OS_Line(void)
{
    // !!! Don't put const literal directly into mutable Req_SIO->data

    static REBYTE newline[] = "\n";

    REBREQ *req = OS_Make_Devreq(&Dev_StdIO);

    Req(req)->common.data = newline;
    Req(req)->length = 1;
    Req(req)->actual = 0;

    REBVAL *result = OS_DO_DEVICE(req, RDC_WRITE);
    assert(result != NULL);
    assert(not IS_ERROR(result));
    rebRelease(result);

    Free_Req(req);
}


//
//  export write-stdout: native [
//
//  "Write text to standard output, or raw BINARY! (for control codes / CGI)"
//
//      return: [<opt> void!]
//      value [<blank> text! char! binary!]
//          "Text to write, if a STRING! or CHAR! is converted to OS format"
//  ]
//
REBNATIVE(write_stdout)
{
    INCLUDE_PARAMS_OF_WRITE_STDOUT;

    REBVAL *v = ARG(value);

    if (IS_BINARY(v)) {
        //
        // It is sometimes desirable to write raw binary data to stdout.  e.g.
        // e.g. CGI scripts may be hooked up to stream data for a download,
        // and not want the bytes interpreted in any way.  (e.g. not changed
        // from UTF-8 to wide characters, or not having CR turned into CR LF
        // sequences).
        //
        Prin_OS_String(VAL_BIN_AT(v), VAL_LEN_AT(v), OPT_ENC_RAW);
    }
    else {
        assert(IS_TEXT(v) or IS_ISSUE(v));

        // !!! Should be passing the STRING!, so the printing port gets the
        // number of codepoints as well as the UTF-8 size.
        //
        REBSIZ utf8_size;
        REBCHR(const*) utf8 = VAL_UTF8_SIZE_AT(&utf8_size, v);

        Prin_OS_String(utf8, utf8_size, OPT_ENC_0);
    }

    return Init_Void(D_OUT);
}
