//
//  file: %mod-clipboard.c
//  summary: "Clipboard Interface"
//  section: extension
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
    #undef OUT  // %minwindef.h defines this, we have a better use for it
    #undef VOID  // %winnt.h defines this, we have a better use for it
#endif

#include "sys-core.h"
#include "tmp-mod-clipboard.h"

#include "tmp-paramlists.h"  // !!! for INCLUDE_PARAMS_OF_OPEN, etc.


//
//  export clipboard-actor: native [
//
//  "Handler for OLDGENERIC dispatch on Clipboard PORT!s"
//
//      return: [any-stable?]
//  ]
//
DECLARE_NATIVE(CLIPBOARD_ACTOR)
//
// !!! Note: All state is in Windows, nothing in the port at the moment.  It
// could track whether it's "open" or not, but the details of what is needed
// depends on the development of a coherent port model.
{
    Stable* port = ARG_N(1);
    const Symbol* verb = Level_Verb(LEVEL);

    switch (opt Symbol_Id(verb)) {
      case SYM_OPEN_Q:
        return LOGIC(true); // !!! need "port state"?  :-/

      case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;
        UNUSED(ARG(SOURCE));  // implied by `port`

        if (Bool_ARG(PART) or Bool_ARG(SEEK))
            panic (Error_Bad_Refines_Raw());

        UNUSED(Bool_ARG(STRING));  // handled in dispatcher
        UNUSED(Bool_ARG(LINES));  // handled in dispatcher

        SetLastError(NO_ERROR);
        if (not IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            //
            // This is not necessarily an "error", just may be the clipboard
            // doesn't have text on it (an image, or maybe nothing at all);
            //
            DWORD last_error = GetLastError();
            if (last_error != NO_ERROR)
                rebPanic_OS (last_error);

            return "~";
        }

        if (not OpenClipboard(nullptr))
            return "panic -[OpenClipboard() fail while reading]-";

        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h == nullptr) {
            CloseClipboard();
            return "panic -[GetClipboardData() format mismatch]-";
        }

        WCHAR *wide = cast(WCHAR*, GlobalLock(h));
        if (wide == nullptr) {
            CloseClipboard();
            return "panic -[Couldn't GlobalLock() UCS2 clipboard data]-";
        }

        Api(Stable*) str = Known_Stable_Api(rebTextWide(wide));

        GlobalUnlock(h);
        CloseClipboard();

        return rebValue("as blob!", rebR(str)); }  // READ -> UTF-8

      case SYM_WRITE: {
        INCLUDE_PARAMS_OF_WRITE;
        UNUSED(ARG(DESTINATION));  // implied by `port`

        if (Bool_ARG(APPEND) or Bool_ARG(LINES))
            panic (Error_Bad_Refines_Raw());

        Stable* data = ARG(DATA);

        // !!! Traditionally the currency of READ and WRITE is binary data.
        // R3-Alpha had a behavior of ostensibly taking string or binary, but
        // the length only made sense if it was a string.  Review.
        //
        if (rebNot("text?", data))
            panic (Error_Invalid_Port_Arg_Raw(data));

        // Handle :PART refinement:
        //
        REBINT len = Series_Len_At(data);
        if (Bool_ARG(PART) and VAL_INT32(ARG(PART)) < len)
            len = VAL_INT32(ARG(PART));

        if (not OpenClipboard(nullptr))
            return "panic -[OpenClipboard() fail on clipboard write]-";

        if (not EmptyClipboard()) // !!! is this superfluous?
            return "panic -[EmptyClipboard() fail on clipboard write]-";

        // Clipboard wants a Windows memory handle with UCS2 data.  Allocate a
        // sufficienctly sized handle, decode Rebol STRING! into it, transfer
        // ownership of that handle to the clipboard.

        unsigned int num_wchars = rebSpellIntoWide(nullptr, 0, data);

        HANDLE h = GlobalAlloc(GHND, sizeof(WCHAR) * (num_wchars + 1));
        if (h == nullptr)  // per documentation, not INVALID_HANDLE_VALUE
            return "panic -[GlobalAlloc() fail on clipboard write]-";

        WCHAR *wide = cast(WCHAR*, GlobalLock(h));
        if (wide == nullptr)
            return "panic -[GlobalLock() fail on clipboard write]-";

        // Extract text as UTF-16
        //
        Length check = rebSpellIntoWide(wide, num_wchars, data);
        assert(check == num_wchars);
        assert(len <= check);  // may only be writing :PART of the string
        UNUSED(check);

        GlobalUnlock(h);

        HANDLE h_check = SetClipboardData(CF_UNICODETEXT, h);
        CloseClipboard();

        if (h_check == nullptr)
            return "panic -[SetClipboardData() failed]-";

        assert(h_check == h);

        return COPY(port); }

      case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;
        UNUSED(PARAM(SPEC));

        if (Bool_ARG(NEW) or Bool_ARG(READ) or Bool_ARG(WRITE))
            panic (Error_Bad_Refines_Raw());

        // !!! Currently just ignore (it didn't do anything)

        return COPY(port); }

      case SYM_CLOSE: {

        // !!! Currently just ignore (it didn't do anything)

        return COPY(port); }

      default:
        break;
    }

    panic (UNHANDLED);
}
