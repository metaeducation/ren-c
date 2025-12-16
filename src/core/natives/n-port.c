//
//  file: %n-port.c
//  summary: "Native functions for PORT!s"
//  section: natives
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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


//
//  create: native:generic [
//
//  "Send port a create request"
//
//      return: [port!]
//      port [port! file! url! block!]
//  ]
//
DECLARE_NATIVE(CREATE)
{
    INCLUDE_PARAMS_OF_CREATE;

    Element* port = Element_ARG(PORT);
    return Run_Generic_Dispatch(port, LEVEL, CANON(CREATE));
}


//
//  delete: native:generic [
//
//  "Send port a delete request"
//
//      return: [port!]
//      port [port! file! url! block!]
//  ]
//
DECLARE_NATIVE(DELETE)
{
    INCLUDE_PARAMS_OF_DELETE;

    Element* port = Element_ARG(PORT);
    return Run_Generic_Dispatch(port, LEVEL, CANON(DELETE));
}


//
//  open: native:generic [
//
//  "Opens a port; makes a new port from a specification if necessary"
//
//      return: [port!]
//      spec [port! file! url! block!]
//      :new "Create new file - if it exists, reset it (truncate)"
//      :read "Open for read access"
//      :write "Open for write access"
//  ]
//
DECLARE_NATIVE(OPEN)
{
    INCLUDE_PARAMS_OF_OPEN;

    Element* spec = Element_ARG(SPEC);
    return Run_Generic_Dispatch(spec, LEVEL, CANON(OPEN));
}


//
//  close: native:generic [
//
//  "Closes a port, library, etc."
//
//      return: [fundamental?]  ; !!! Is returning the port useful?
//      port [fundamental?]  ; "target", "value", "element" instead of port?
//  ]
//
DECLARE_NATIVE(CLOSE)
{
    INCLUDE_PARAMS_OF_CLOSE;

    Element* port = Element_ARG(PORT);
    return Dispatch_Generic(CLOSE, port, LEVEL);
}


//
//  read: native:generic [
//
//  "Read from a file, URL, or other port"
//
//      ; mostly crazy invariants from R3-Alpha's READ
//      return: [
//          <null> "null returned on (some) failures"
//          blob! "!!! should all READ return a BLOB!"
//          text! "READ:STRING returned TEXT!"
//          block! "READ:LINES returned BLOCK!"
//          port! "asynchronous READ on PORT!s returned the PORT!"
//          tuple! "READ:DNS returned tuple!"
//          ~(~halt~)~ "!!! If READ is Ctrl-C'd in nonhaltable API calls"
//      ]
//      source [port! file! url! block!]
//      :part "Partial read a given number of units (source relative)"
//          [any-number?]
//      :seek "Read from a specific position (source relative)"
//          [any-number?]
//      :string "Convert UTF and line terminators to standard text string"
//      :lines "Convert to block of strings (implies :string)"
//  ]
//
DECLARE_NATIVE(READ)
{
    INCLUDE_PARAMS_OF_READ;

    Element* port = Element_ARG(SOURCE);
    return Run_Generic_Dispatch(port, LEVEL, CANON(READ));
}


//
//  write: native:generic [
//
//  "Writes to a file, URL, or port - auto-converts text strings"
//
//      return: [port! block! ~(#stdout)~]  ; !!! http write returns BLOCK (?)
//      destination [port! file! url! block! ~(#stdout)~]
//      data "Data to write (non-binary converts to UTF-8)"
//          [blob! text! block! object! rune!]
//      :part "Partial write a given number of units"
//          [any-number?]
//      :seek "Write at a specific position"
//          [any-number?]
//      :append "Write data at end of file"
//      :lines "Write each value in a block as a separate line"
//  ]
//
DECLARE_NATIVE(WRITE)
{
    INCLUDE_PARAMS_OF_WRITE;

    Element* port = Element_ARG(DESTINATION);
    Element* data = Element_ARG(DATA);

    if (Is_Rune(port)) {
        if (rebNot("#stdout =", port))
            panic ("only #stdout support on WRITE for RUNE! right now");

        if (ARG(PART) or ARG(SEEK) or ARG(APPEND))
            panic (Error_Bad_Refines_Raw());

        if (ARG(LINES)) {
            if (Is_Block(data))
                Pinify_Cell(data);  // don't reduce
            Api(Stable*) delimited = rebStable("delimit:tail newline @", data);
            if (not delimited)  // e.g. [] input
                return COPY(port);
            Copy_Cell(data, Known_Element(delimited));
            rebRelease(delimited);
        }

        return rebDelegate(
            CANON(WRITE_STDOUT), rebQ(data),
            port
        );
    }

    return Run_Generic_Dispatch(port, LEVEL, CANON(WRITE));
}


//
//  query: native:generic [
//
//  "Returns information about a port, file, or URL"
//
//      return: [<null> object!]
//      target [port! file! url! block!]
//  ]
//
DECLARE_NATIVE(QUERY)
{
    INCLUDE_PARAMS_OF_QUERY;

    Element* target = Element_ARG(TARGET);
    return Run_Generic_Dispatch(target, LEVEL, CANON(QUERY));
}


//
//  modify: native:generic [
//
//  "Change mode or control for port or file, and return success status"  ; [1]
//
//      return: [logic?]
//      target [port! file!]
//      field [<opt-out> word!]
//      value
//  ]
//
DECLARE_NATIVE(MODIFY)
//
// 1. !!! To the extent this is going to influence anything which would be
//    kept, failure should be returning an ERROR! to say what happened, and
//    let people TRY that or display it, not returning a logic.
{
    INCLUDE_PARAMS_OF_MODIFY;

    Element* target = Element_ARG(TARGET);
    return Run_Generic_Dispatch(target, LEVEL, CANON(MODIFY));
}


//
//  rename: native:generic [
//
//  "Rename a file"
//
//      return: [port! file! url!]
//      from [port! file! url! block!]
//      to [port! file! url! block!]
//  ]
//
DECLARE_NATIVE(RENAME)
{
    INCLUDE_PARAMS_OF_RENAME;

    Element* from = Element_ARG(FROM);
    return Run_Generic_Dispatch(from, LEVEL, CANON(RENAME));
}
