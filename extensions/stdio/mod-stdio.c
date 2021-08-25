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
// Copyright 2012-2021 Ren-C Open Source Contributors
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

#include "readline.h"


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
//  startup*: native [  ; Note: DO NOT EXPORT!
//
//      return: <none>
//  ]
//
REBNATIVE(startup_p)
{
    STDIO_INCLUDE_PARAMS_OF_STARTUP_P;

    // This does the platform-specific initialization for stdio.  Included in
    // that is doing things like figuring out if the input or output have
    // been redirected to a file--in which case, it has to know not to try
    // and treat it as a "smart console" with cursoring around ability.
    //
    Startup_Stdio();

    return rebNone();
}


//
//  export write-stdout: native [
//
//  "Write text to standard output, or raw BINARY! (for control codes / CGI)"
//
//      return: [<opt> bad-word!]
//      value [<blank> text! char! binary!]
//          "Text to write, if a STRING! or CHAR! is converted to OS format"
//  ]
//
REBNATIVE(write_stdout)
//
// Note: It is sometimes desirable to write raw binary data to stdout.  e.g.
// CGI scripts may be hooked up to stream data for a download, and not want the
// bytes interpreted in any way.  (e.g. not changed from UTF-8 to wide
// characters, or not having CR turned into CR LF sequences).
{
    INCLUDE_PARAMS_OF_WRITE_STDOUT;

    REBVAL *v = ARG(value);

    // !!! We want to make the chunking easier, by having a position in the
    // cell...but ISSUE! has no position.  Alias it as a read-only TEXT!.
    //
    if (IS_ISSUE(v)) {
        REBVAL *temp = rebValue(Lib(AS), Lib(TEXT_X), v);
        Move_Cell(v, temp);
        rebRelease(temp);
    }

    // !!! The historical division of labor between the "core" and the "host"
    // is that the host doesn't know how to poll for cancellation.  So data
    // gets broken up into small batches and it's this loop that has access
    // to the core "Do_Signals_Throws" query.  Hence one can send a giant
    // string to the code that does write() and be able to interrupt it,
    // even though that device request could block forever in theory.
    //
    // There may well be a better way to go about this.
    //
    REBLEN remaining;
    while ((remaining = VAL_LEN_AT(v)) > 0) {
        //
        // Yield to signals processing for cancellation requests.
        //
        if (Do_Signals_Throws(SET_END(D_SPARE)))
            fail (Error_No_Catch_For_Throw(D_SPARE));
        assert(IS_END(D_SPARE));

        REBLEN part;
        if (remaining <= 1024)
            part = remaining;
        else
            part = 1024;

        Write_IO(v, remaining);

        VAL_INDEX_RAW(v) += part;
    }

    return Init_None(D_OUT);
}


//
//  shutdown*: native [  ; Note: DO NOT EXPORT!
//
//  {Shut down the stdio and terminal devices, called on extension unload}
//
//      return: <none>
//  ]
//
REBNATIVE(shutdown_p)
{
    STDIO_INCLUDE_PARAMS_OF_SHUTDOWN_P;

    // This shutdown does platform-specific teardown, freeing buffers that
    // may only be have been created for Windows, etc.
    //
    Shutdown_Stdio();

    return rebNone();
}
