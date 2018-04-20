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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The clipboard is currently implemented for Windows only, see #2029
//
// 

#ifdef TO_WINDOWS
    #include <windows.h>
    #undef IS_ERROR
#endif

#include "sys-core.h"
#include "sys-ext.h"


#include "tmp-mod-clipboard-first.h"


//
//  Clipboard_Actor: C
//
static REB_R Clipboard_Actor(REBFRM *frame_, REBCTX *port, REBSYM action)
{
    // !!! All state is in windows, nothing in the port at the moment.  It
    // could track whether it's open or not, but the details of what is
    // needed depends on the development of a coherent port model.
    //
    UNUSED(port);

    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    switch (action) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // implied by `port`
        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != 0);

        switch (property) {
        case SYM_OPEN_Q:
            return R_FROM_BOOL(TRUE); // !!! does it need "port state"?  :-/

        default:
            break;
        }

        break; }

    case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PAR(source)); // already accounted for
        if (REF(part)) {
            UNUSED(ARG(limit));
            fail (Error_Bad_Refines_Raw());
        }
        if (REF(seek)) {
            UNUSED(ARG(index));
            fail (Error_Bad_Refines_Raw());
        }
        UNUSED(PAR(string)); // handled in dispatcher
        UNUSED(PAR(lines)); // handled in dispatcher

        SetLastError(NO_ERROR);
        if (NOT(IsClipboardFormatAvailable(CF_UNICODETEXT))) {
            //
            // This is not necessarily an "error", just may be the clipboard
            // doesn't have text on it (an image, or maybe nothing at all);
            //
            DWORD last_error = GetLastError();
            if (last_error != NO_ERROR)
                rebFail_OS (last_error);

            return R_BLANK;
        }

        if (NOT(OpenClipboard(NULL)))
            rebFail ("{OpenClipboard() fail while reading}", rebEnd());

        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h == NULL) {
            CloseClipboard();
            rebFail (
                "{IsClipboardFormatAvailable()/GetClipboardData() mismatch}",
                rebEnd()
            );
        }

        WCHAR *wide = cast(WCHAR*, GlobalLock(h));
        if (wide == NULL) {
            CloseClipboard();
            rebFail ("{Couldn't GlobalLock() UCS2 clipboard data}", rebEnd());
        }

        REBVAL *str = rebStringW(wide);

        GlobalUnlock(h);
        CloseClipboard();

        // !!! We got wide character data back, which had to be made into a
        // string.  But READ wants BINARY! data.  With UTF-8 Everywhere, the
        // byte representation of the string could be locked + aliased
        // as a UTF-8 binary series.  Conversion is needed for the moment.

        size_t size;
        char *utf8 = rebSpellingOfAlloc(&size, str);
        rebRelease(str);

        // See notes on how rebRepossess reclaims the memory of a rebMalloc()
        // (which is used by rebSpellingOfAlloc()) as a BINARY!.
        //
        REBVAL *binary = rebRepossess(utf8, size);
        Move_Value(D_OUT, binary);
        rebRelease(binary); // output has reference on the series, drop ours
        return R_OUT; }

    case SYM_WRITE: {
        INCLUDE_PARAMS_OF_WRITE;

        UNUSED(PAR(destination));
        UNUSED(PAR(data)); // used as arg

        if (REF(seek)) {
            UNUSED(ARG(index));
            fail (Error_Bad_Refines_Raw());
        }
        if (REF(append))
            fail (Error_Bad_Refines_Raw());
        if (REF(allow)) {
            UNUSED(ARG(access));
            fail (Error_Bad_Refines_Raw());
        }
        if (REF(lines))
            fail (Error_Bad_Refines_Raw());

        // !!! Traditionally the currency of READ and WRITE is binary data.
        // R3-Alpha had a behavior of ostensibly taking string or binary, but
        // the length only made sense if it was a string.  Review.
        //
        if (NOT(IS_STRING(arg)))
            fail (Error_Invalid_Port_Arg_Raw(arg));

        // Handle /part refinement:
        //
        REBINT len = VAL_LEN_AT(arg);
        if (REF(part) && VAL_INT32(ARG(limit)) < len)
            len = VAL_INT32(ARG(limit));

        if (NOT(OpenClipboard(NULL)))
            rebFail ("{OpenClipboard() fail on clipboard write}", rebEnd());

        if (NOT(EmptyClipboard())) // !!! is this superfluous?
            rebFail ("{EmptyClipboard() fail on clipboard write}", rebEnd());

        // Clipboard wants a Windows memory handle with UCS2 data.  Allocate a
        // sufficienctly sized handle, decode Rebol STRING! into it, transfer
        // ownership of that handle to the clipboard.

        HANDLE h = GlobalAlloc(GHND, sizeof(WCHAR) * (len + 1));
        if (h == NULL) // per documentation, not INVALID_HANDLE_VALUE
            rebFail ("{GlobalAlloc() fail on clipboard write}", rebEnd());

        WCHAR *wide = cast(WCHAR*, GlobalLock(h));
        if (wide == NULL)
            rebFail ("{GlobalLock() fail on clipboard write}", rebEnd());

        REBINT len_check = rebSpellingOfW(wide, len, arg); // UTF-16 extract
        assert(len <= len_check); // may only be writing /PART of the string
        UNUSED(len_check);

        GlobalUnlock(h);

        HANDLE h_check = SetClipboardData(CF_UNICODETEXT, h);
        CloseClipboard();

        if (h_check == NULL)
            rebFail ("{SetClipboardData() failed.}", rebEnd());

        assert(h_check == h);

        goto return_port; }

    case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;

        UNUSED(PAR(spec));
        if (REF(new))
            fail (Error_Bad_Refines_Raw());
        if (REF(read))
            fail (Error_Bad_Refines_Raw());
        if (REF(write))
            fail (Error_Bad_Refines_Raw());
        if (REF(seek))
            fail (Error_Bad_Refines_Raw());
        if (REF(allow)) {
            UNUSED(ARG(access));
            fail (Error_Bad_Refines_Raw());
        }

        // !!! Currently just ignore (it didn't do anything)

        goto return_port; }

    case SYM_CLOSE: {

        // !!! Currently just ignore (it didn't do anything)

        goto return_port; }

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_PORT, action));

return_port:
    Move_Value(D_OUT, D_ARG(1));
    return R_OUT;
}


//
//  get-clipboard-actor-handle: native/export [
//
//  {Retrieve handle to the native actor for clipboard}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_clipboard_actor_handle)
{
    Make_Port_Actor_Handle(D_OUT, &Clipboard_Actor);
    return R_OUT;
}

#include "tmp-mod-clipboard-last.h"
