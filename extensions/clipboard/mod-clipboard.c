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
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));  // implied by `port`

        Option(SymId) property = Cell_Word_Id(ARG(property));
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
            return FAIL(Error_Bad_Refines_Raw());

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

        if (not OpenClipboard(nullptr))
            return rebDelegate("fail -{OpenClipboard() fail while reading}-");

        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h == nullptr) {
            CloseClipboard();
            return rebDelegate(
              "fail",
                "-{IsClipboardFormatAvailable()/GetClipboardData() mismatch}-"
            );
        }

        WCHAR *wide = cast(WCHAR*, GlobalLock(h));
        if (wide == nullptr) {
            CloseClipboard();
            return rebDelegate(
                "fail -{Couldn't GlobalLock() UCS2 clipboard data}-"
            );
        }

        Value* str = rebTextWide(wide);

        GlobalUnlock(h);
        CloseClipboard();

        return rebValue("as blob!", rebR(str)); }  // READ -> UTF-8

      case SYM_WRITE: {
        INCLUDE_PARAMS_OF_WRITE;
        UNUSED(ARG(destination));  // implied by `port`

        if (REF(append) or REF(lines))
            return FAIL(Error_Bad_Refines_Raw());

        Value* data = ARG(data);

        // !!! Traditionally the currency of READ and WRITE is binary data.
        // R3-Alpha had a behavior of ostensibly taking string or binary, but
        // the length only made sense if it was a string.  Review.
        //
        if (rebNot("text?", data))
            return FAIL(Error_Invalid_Port_Arg_Raw(data));

        // Handle :PART refinement:
        //
        REBINT len = Cell_Series_Len_At(data);
        if (REF(part) and VAL_INT32(ARG(part)) < len)
            len = VAL_INT32(ARG(part));

        if (not OpenClipboard(nullptr))
            return rebDelegate(
                "fail -{OpenClipboard() fail on clipboard write}-"
            );

        if (not EmptyClipboard()) // !!! is this superfluous?
            return rebDelegate(
                "fail -{EmptyClipboard() fail on clipboard write}-"
            );

        // Clipboard wants a Windows memory handle with UCS2 data.  Allocate a
        // sufficienctly sized handle, decode Rebol STRING! into it, transfer
        // ownership of that handle to the clipboard.

        unsigned int num_wchars = rebSpellIntoWide(nullptr, 0, data);

        HANDLE h = GlobalAlloc(GHND, sizeof(WCHAR) * (num_wchars + 1));
        if (h == nullptr)  // per documentation, not INVALID_HANDLE_VALUE
            return rebDelegate(
                "fail -{GlobalAlloc() fail on clipboard write}-"
            );

        WCHAR *wide = cast(WCHAR*, GlobalLock(h));
        if (wide == nullptr)
            return rebDelegate(
                "fail -{GlobalLock() fail on clipboard write}-"
            );

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
            return rebDelegate("fail -{SetClipboardData() failed}-");

        assert(h_check == h);

        return COPY(port); }

      case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;
        UNUSED(PARAM(spec));

        if (REF(new) or REF(read) or REF(write))
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
DECLARE_NATIVE(get_clipboard_actor_handle)
{
    Make_Port_Actor_Handle(OUT, &Clipboard_Actor);
    return OUT;
}
