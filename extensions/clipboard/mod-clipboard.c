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
// Copyright 2012-2018 Rebol Open Source Contributors
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
//=////////////////////////////////////////////////////////////////////////=//
//
// The clipboard is currently implemented for Windows only, see #2029
//

#ifdef TO_WINDOWS
    #include <windows.h>
    #undef IS_ERROR
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
    const REBVAL *verb
){
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    switch (VAL_WORD_SYM(verb)) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));  // implied by `port`

        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != 0);

        switch (property) {
        case SYM_OPEN_Q:
            return Init_Logic(D_OUT, true); // !!! need "port state"?  :-/

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

            return Init_Blank(D_OUT);
        }

        if (not OpenClipboard(NULL))
            rebJumps("FAIL {OpenClipboard() fail while reading}", rebEND);

        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h == NULL) {
            CloseClipboard();
            rebJumps (
                "FAIL",
                "{IsClipboardFormatAvailable()/GetClipboardData() mismatch}",
                rebEND
            );
        }

        WCHAR *wide = cast(WCHAR*, GlobalLock(h));
        if (wide == NULL) {
            CloseClipboard();
            rebJumps(
                "FAIL {Couldn't GlobalLock() UCS2 clipboard data}", rebEND
            );
        }

        REBVAL *str = rebTextWide(wide);

        GlobalUnlock(h);
        CloseClipboard();

        REBVAL *binary = rebValueQ("as binary!", str, rebEND);  // READ -> UTF-8
        rebRelease(str);

        return binary; }

    case SYM_WRITE: {
        INCLUDE_PARAMS_OF_WRITE;
        UNUSED(ARG(destination));  // implied by `port`
        UNUSED(ARG(data)); // implied by `arg`

        if (REF(seek) or REF(append) or REF(allow) or REF(lines))
            fail (Error_Bad_Refines_Raw());

        // !!! Traditionally the currency of READ and WRITE is binary data.
        // R3-Alpha had a behavior of ostensibly taking string or binary, but
        // the length only made sense if it was a string.  Review.
        //
        if (rebNot("text?", arg, rebEND))
            fail (Error_Invalid_Port_Arg_Raw(arg));

        // Handle /part refinement:
        //
        REBINT len = VAL_LEN_AT(arg);
        if (REF(part) and VAL_INT32(ARG(part)) < len)
            len = VAL_INT32(ARG(part));

        if (not OpenClipboard(NULL))
            rebJumps(
                "FAIL {OpenClipboard() fail on clipboard write}", rebEND
            );

        if (not EmptyClipboard()) // !!! is this superfluous?
            rebJumps(
                "FAIL {EmptyClipboard() fail on clipboard write}", rebEND
            );

        // Clipboard wants a Windows memory handle with UCS2 data.  Allocate a
        // sufficienctly sized handle, decode Rebol STRING! into it, transfer
        // ownership of that handle to the clipboard.

        HANDLE h = GlobalAlloc(GHND, sizeof(WCHAR) * (len + 1));
        if (h == NULL) // per documentation, not INVALID_HANDLE_VALUE
            rebJumps(
                "FAIL {GlobalAlloc() fail on clipboard write}", rebEND
            );

        WCHAR *wide = cast(WCHAR*, GlobalLock(h));
        if (wide == NULL)
            rebJumps(
                "FAIL {GlobalLock() fail on clipboard write}", rebEND
            );

        // Extract the UTF-16
        //
        REBINT len_check = rebSpellIntoWideQ(wide, len, arg, rebEND);
        assert(len <= len_check); // may only be writing /PART of the string
        UNUSED(len_check);

        GlobalUnlock(h);

        HANDLE h_check = SetClipboardData(CF_UNICODETEXT, h);
        CloseClipboard();

        if (h_check == NULL)
            rebJumps("FAIL {SetClipboardData() failed.}", rebEND);

        assert(h_check == h);

        RETURN (port); }

    case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;
        UNUSED(PAR(spec));

        if (REF(new) or REF(read) or REF(write) or REF(seek) or REF(allow))
            fail (Error_Bad_Refines_Raw());

        // !!! Currently just ignore (it didn't do anything)

        RETURN (port); }

    case SYM_CLOSE: {

        // !!! Currently just ignore (it didn't do anything)

        RETURN (port); }

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
    Make_Port_Actor_Handle(D_OUT, &Clipboard_Actor);
    return D_OUT;
}
