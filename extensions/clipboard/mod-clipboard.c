//
//  File: %mod-clipboard.c
//  Summary: "Clipboard Interface"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Ren-C Open Source Contributors
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
// The clipboard is currently implemented for Windows only, see #2029
//

#if TO_WINDOWS
    #define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
    #include <windows.h>
    #undef IS_ERROR
    #undef OUT  // %minwindef.h defines this, we have a better use for it
#endif

#include "sys-core.h"

#include "tmp-mod-clipboard.h"


//
//  Clipboard_Actor: C
//
// !!! Note: All state is in Windows, nothing in the port at the moment.  It
// could track whether it's "open" or not, but the details of what is needed
// depends on the development of a coherent port model.
//
static REB_R Clipboard_Actor(
    REBFRM *frame_,
    REBVAL *port,
    const REBSYM *verb
){
    switch (ID_OF_SYMBOL(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));  // implied by `port`

        SYMID property = VAL_WORD_ID(ARG(property));
        assert(property != 0);

        switch (property) {
          case SYM_OPEN_Q:
            return Init_Logic(OUT, true); // !!! need "port state"?  :-/

        default:
            break;
        }

        break; }

      case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;
        UNUSED(ARG(source));  // implied by `port`

        if (REF(part) or REF(seek))
            fail (Error_Bad_Refines_Raw());

        UNUSED(REF(string));  // handled in dispatcher
        UNUSED(REF(lines));  // handled in dispatcher

        SetLastError(NO_ERROR);
        if (not IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            //
            // This is not necessarily an "error", just may be the clipboard
            // doesn't have text on it (an image, or maybe nothing at all);
            //
            DWORD last_error = GetLastError();
            if (last_error != NO_ERROR)
                rebFail_OS (last_error);

            return Init_Blank(OUT);
        }

        if (not OpenClipboard(NULL))
            rebJumps("fail {OpenClipboard() fail while reading}");

        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h == NULL) {
            CloseClipboard();
            rebJumps (
                "fail",
                "{IsClipboardFormatAvailable()/GetClipboardData() mismatch}"
            );
        }

        WCHAR *wide = cast(WCHAR*, GlobalLock(h));
        if (wide == NULL) {
            CloseClipboard();
            rebJumps("fail {Couldn't GlobalLock() UCS2 clipboard data}");
        }

        REBVAL *str = rebTextWide(wide);

        GlobalUnlock(h);
        CloseClipboard();

        return rebValue("as binary!", rebR(str)); }  // READ -> UTF-8

      case SYM_WRITE: {
        INCLUDE_PARAMS_OF_WRITE;
        UNUSED(ARG(destination));  // implied by `port`

        if (REF(append) or REF(lines))
            fail (Error_Bad_Refines_Raw());

        REBVAL *data = ARG(data);

        // !!! Traditionally the currency of READ and WRITE is binary data.
        // R3-Alpha had a behavior of ostensibly taking string or binary, but
        // the length only made sense if it was a string.  Review.
        //
        if (rebNot("text?", data))
            fail (Error_Invalid_Port_Arg_Raw(data));

        // Handle /part refinement:
        //
        REBINT len = VAL_LEN_AT(data);
        if (REF(part) and VAL_INT32(ARG(part)) < len)
            len = VAL_INT32(ARG(part));

        if (not OpenClipboard(NULL))
            rebJumps("fail {OpenClipboard() fail on clipboard write}");

        if (not EmptyClipboard()) // !!! is this superfluous?
            rebJumps("fail {EmptyClipboard() fail on clipboard write}");

        // Clipboard wants a Windows memory handle with UCS2 data.  Allocate a
        // sufficienctly sized handle, decode Rebol STRING! into it, transfer
        // ownership of that handle to the clipboard.

        unsigned int num_wchars = rebSpellIntoWide(nullptr, 0, data);

        HANDLE h = GlobalAlloc(GHND, sizeof(WCHAR) * (num_wchars + 1));
        if (h == NULL) // per documentation, not INVALID_HANDLE_VALUE
            rebJumps("fail {GlobalAlloc() fail on clipboard write}");

        WCHAR *wide = cast(WCHAR*, GlobalLock(h));
        if (wide == NULL)
            rebJumps("fail {GlobalLock() fail on clipboard write}");

        // Extract text as UTF-16
        //
        REBINT check = rebSpellIntoWide(wide, num_wchars, data);
        assert(check == cast(REBINT, num_wchars));
        assert(len <= check); // may only be writing /PART of the string
        UNUSED(check);

        GlobalUnlock(h);

        HANDLE h_check = SetClipboardData(CF_UNICODETEXT, h);
        CloseClipboard();

        if (h_check == NULL)
            rebJumps("fail {SetClipboardData() failed.}");

        assert(h_check == h);

        return port; }

      case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;
        UNUSED(PAR(spec));

        if (REF(new) or REF(read) or REF(write))
            fail (Error_Bad_Refines_Raw());

        // !!! Currently just ignore (it didn't do anything)

        return port; }

      case SYM_CLOSE: {

        // !!! Currently just ignore (it didn't do anything)

        return port; }

      default:
        break;
    }

    return R_UNHANDLED;
}


//
//  export get-clipboard-actor-handle: native [
//
//  {Retrieve handle to the native actor for clipboard}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_clipboard_actor_handle)
{
    Make_Port_Actor_Handle(OUT, &Clipboard_Actor);
    return OUT;
}
