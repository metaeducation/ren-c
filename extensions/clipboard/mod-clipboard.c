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
    #undef OUT  // %minwindef.h defines this, we have a better use for it
    #undef VOID  // %winnt.h defines this, we have a better use for it
#endif

#include "tmp-mod-clipboard.h"

#include "tmp-paramlists.h"  // !!! for INCLUDE_PARAMS_OF_OPEN, etc.

//
//  Clipboard_Actor: C
//
// !!! Note: All state is in Windows, nothing in the port at the moment.  It
// could track whether it's "open" or not, but the details of what is needed
// depends on the development of a coherent port model.
//
static Bounce Clipboard_Actor(
    Level* level_,
    Value* port,
    const Symbol* verb
){
    switch (Symbol_Id(verb)) {
      case SYM_OPEN_Q:
        return Init_Logic(OUT, true); // !!! need "port state"?  :-/

      case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;
        UNUSED(ARG(SOURCE));  // implied by `port`

        if (REF(PART) or REF(SEEK))
            return FAIL(Error_Bad_Refines_Raw());

        UNUSED(REF(STRING));  // handled in dispatcher
        UNUSED(REF(LINES));  // handled in dispatcher

        SetLastError(NO_ERROR);
        if (not IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            //
            // This is not necessarily an "error", just may be the clipboard
            // doesn't have text on it (an image, or maybe nothing at all);
            //
            DWORD last_error = GetLastError();
            if (last_error != NO_ERROR)
                rebFail_OS (last_error);

            return "~";
        }

        if (not OpenClipboard(nullptr))
            return "fail -{OpenClipboard() fail while reading}-";

        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h == nullptr) {
            CloseClipboard();
            return "fail -{GetClipboardData() format mismatch}-";
        }

        WCHAR *wide = cast(WCHAR*, GlobalLock(h));
        if (wide == nullptr) {
            CloseClipboard();
            return "fail -{Couldn't GlobalLock() UCS2 clipboard data}-";
        }

        Value* str = rebTextWide(wide);

        GlobalUnlock(h);
        CloseClipboard();

        return rebValue("as blob!", rebR(str)); }  // READ -> UTF-8

      case SYM_WRITE: {
        INCLUDE_PARAMS_OF_WRITE;
        UNUSED(ARG(DESTINATION));  // implied by `port`

        if (REF(APPEND) or REF(LINES))
            return FAIL(Error_Bad_Refines_Raw());

        Value* data = ARG(DATA);

        // !!! Traditionally the currency of READ and WRITE is binary data.
        // R3-Alpha had a behavior of ostensibly taking string or binary, but
        // the length only made sense if it was a string.  Review.
        //
        if (rebNot("text?", data))
            return FAIL(Error_Invalid_Port_Arg_Raw(data));

        // Handle :PART refinement:
        //
        REBINT len = Cell_Series_Len_At(data);
        if (REF(PART) and VAL_INT32(ARG(PART)) < len)
            len = VAL_INT32(ARG(PART));

        if (not OpenClipboard(nullptr))
            return "fail -{OpenClipboard() fail on clipboard write}-";

        if (not EmptyClipboard()) // !!! is this superfluous?
            return "fail -{EmptyClipboard() fail on clipboard write}-";

        // Clipboard wants a Windows memory handle with UCS2 data.  Allocate a
        // sufficienctly sized handle, decode Rebol STRING! into it, transfer
        // ownership of that handle to the clipboard.

        unsigned int num_wchars = rebSpellIntoWide(nullptr, 0, data);

        HANDLE h = GlobalAlloc(GHND, sizeof(WCHAR) * (num_wchars + 1));
        if (h == nullptr)  // per documentation, not INVALID_HANDLE_VALUE
            return "fail -{GlobalAlloc() fail on clipboard write}-";

        WCHAR *wide = cast(WCHAR*, GlobalLock(h));
        if (wide == nullptr)
            return "fail -{GlobalLock() fail on clipboard write}-";

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
            return "fail -{SetClipboardData() failed}-";

        assert(h_check == h);

        return COPY(port); }

      case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;
        UNUSED(PARAM(SPEC));

        if (REF(NEW) or REF(READ) or REF(WRITE))
            return FAIL(Error_Bad_Refines_Raw());

        // !!! Currently just ignore (it didn't do anything)

        return COPY(port); }

      case SYM_CLOSE: {

        // !!! Currently just ignore (it didn't do anything)

        return COPY(port); }

      default:
        break;
    }

    return UNHANDLED;
}


//
//  export /get-clipboard-actor-handle: native [
//
//  "Retrieve handle to the native actor for clipboard"
//
//      return: [handle!]
//  ]
//
DECLARE_NATIVE(GET_CLIPBOARD_ACTOR_HANDLE)
{
    Make_Port_Actor_Handle(OUT, &Clipboard_Actor);
    return OUT;
}
