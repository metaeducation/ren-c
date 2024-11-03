//
//  File: %n-port.c
//  Summary: "Native functions for PORT!s"
//  Section: natives
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


//
//  /create: native:generic [
//
//  "Send port a create request"
//
//      return: [port!]
//      port [port! file! url! block!]
//  ]
//
DECLARE_NATIVE(create)
{
    Element* port = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(port, LEVEL, Canon(CREATE));
}


//
//  /delete: native:generic [
//
//  "Send port a delete request"
//
//      return: [port!]
//      port [port! file! url! block!]
//  ]
//
DECLARE_NATIVE(delete)
{
    Element* port = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(port, LEVEL, Canon(DELETE));
}


//
//  /open: native:generic [
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
DECLARE_NATIVE(open)
{
    Element* spec = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(spec, LEVEL, Canon(OPEN));
}


//
//  /connect: native:generic [
//
//  "Connects a port (used to be 'second open step')"
//
//      return: [port!]
//      spec [port!]
//  ]
//
DECLARE_NATIVE(connect)
{
    Element* port = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(port, LEVEL, Canon(CONNECT));
}


//
//  /close: native:generic [
//
//  "Closes a port"  ; !!! Used to also close LIBRARY!
//
//      return: [port!]  ; !!! Is returning the port useful?
//      port [port!]
//  ]
//
DECLARE_NATIVE(close)
{
    Element* port = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(port, LEVEL, Canon(CLOSE));
}


//
//  /read: native:generic [
//
//  "Read from a file, URL, or other port"
//
//      return: "null on (some) failures (REVIEW port model!)" [
//          ~null~ blob!  ; should all READ return a BLOB!?
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
DECLARE_NATIVE(read)
{
    Element* port = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(port, LEVEL, Canon(READ));
}


//
//  /write: native:generic [
//
//  "Writes to a file, URL, or port - auto-converts text strings"
//
//      return: [port! block!]  ; !!! http write returns BLOCK!, why?
//      destination [port! file! url! block!]
//      data "Data to write (non-binary converts to UTF-8)"
//          [blob! text! block! object! issue!]
//      :part "Partial write a given number of units"
//          [any-number?]
//      :seek "Write at a specific position"
//          [any-number?]
//      :append "Write data at end of file"
//      :lines "Write each value in a block as a separate line"
//  ]
//
DECLARE_NATIVE(write)
{
    Element* port = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(port, LEVEL, Canon(WRITE));
}


//
//  /query: native:generic [
//
//  "Returns information about a port, file, or URL"
//
//      return: [~null~ object!]
//      target [port! file! url! block!]
//  ]
//
DECLARE_NATIVE(query)
{
    Element* port = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(port, LEVEL, Canon(QUERY));
}


//
//  /modify: native:generic [
//
//  "Change mode or control for port or file"
//
//      return: "TRUE if successful, FALSE if unsuccessful (!!! REVIEW)"
//          [logic?]
//      target [port! file!]
//      field [word! blank!]
//      value
//  ]
//
DECLARE_NATIVE(modify)
{
    Element* target = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(target, LEVEL, Canon(MODIFY));
}


//
//  /rename: native:generic [
//
//  "Rename a file"
//
//      return: [port! file! url!]
//      from [port! file! url! block!]
//      to [port! file! url! block!]
//  ]
//
DECLARE_NATIVE(rename)
{
    Element* from = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(from, LEVEL, Canon(RENAME));
}
