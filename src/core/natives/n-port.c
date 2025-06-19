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
    Element* port = cast(Element*, ARG_N(1));
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
    Element* port = cast(Element*, ARG_N(1));
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
    Element* spec = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(spec, LEVEL, CANON(OPEN));
}


//
//  connect: native:generic [
//
//  "Connects a port (used to be 'second open step')"
//
//      return: [port!]
//      spec [port!]
//  ]
//
DECLARE_NATIVE(CONNECT)
{
    Element* port = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(port, LEVEL, CANON(CONNECT));
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
    Element* port = cast(Element*, ARG_N(1));
    return Dispatch_Generic(CLOSE, port, LEVEL);
}


//
//  read: native:generic [
//
//  "Read from a file, URL, or other port"
//
//      return: "null on (some) failures (REVIEW port model!)" [
//          null? blob!  ; should all READ return a BLOB!?
//          text!  ; READ:STRING returned TEXT!
//          block!  ; READ:LINES returned BLOCK!
//          port!  ; asynchronous READ on PORT!s returned the PORT!
//          tuple!  ; READ:DNS returned tuple!
//          quasi?  ; !!! If READ is Ctrl-C'd in nonhaltable API calls, ATM
//      ]
//      source [port! file! url! block!]
//      :part "Partial read a given number of units (source relative)"
//          [any-number?]
//      :seek "Read from a specific position (source relative)"
//          [any-number?]
//      :string "Convert UTF and line terminators to standard text string"
//      :lines "Convert to block of strings (implies /string)"
//  ]
//
DECLARE_NATIVE(READ)
{
    Element* port = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(port, LEVEL, CANON(READ));
}


//
//  write: native:generic [
//
//  "Writes to a file, URL, or port - auto-converts text strings"
//
//      return: [port! block! @word!]  ; !!! http write returns BLOCK!, why?
//      destination [port! file! url! block! @word!]
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

    if (Is_Pinned_Form_Of(WORD, port)) {
        if (Cell_Word_Id(port) != SYM_STDOUT)
            return PANIC("only @stdout support on WRITE for @ right now");

        if (Bool_ARG(PART) or Bool_ARG(SEEK) or Bool_ARG(APPEND))
            return PANIC(Error_Bad_Refines_Raw());

        if (Bool_ARG(LINES)) {
            if (Is_Block(data))
                Pinify(data);  // don't reduce
            Value* delimited = rebValue("delimit:tail newline", rebQ(data));
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
//      return: [null? object!]
//      target [port! file! url! block!]
//  ]
//
DECLARE_NATIVE(QUERY)
{
    Element* port = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(port, LEVEL, CANON(QUERY));
}


//
//  modify: native:generic [
//
//  "Change mode or control for port or file"
//
//      return: "TRUE if successful, FALSE if unsuccessful (!!! REVIEW)"
//          [logic?]
//      target [port! file!]
//      field [<opt-out> word!]
//      value
//  ]
//
DECLARE_NATIVE(MODIFY)
{
    Element* target = cast(Element*, ARG_N(1));
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
    Element* from = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(from, LEVEL, CANON(RENAME));
}
