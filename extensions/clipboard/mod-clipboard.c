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

#ifdef TO_WINDOWS
    #include <windows.h>
    #undef IS_ERROR
    #undef OUT  // %minwindef.h defines this, we have a better use for it
    #undef VOID  // %winnt.h defines this, we have a better use for it
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
    Value* port,
    Value* verb
){
    Value* arg = D_ARGC > 1 ? D_ARG(2) : nullptr;

    switch (Cell_Word_Id(verb)) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // implied by `port`
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
            rebJumps("FAIL {OpenClipboard() fail while reading}");

        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h == nullptr) {
            CloseClipboard();
            rebJumps (
                "FAIL",
                "{IsClipboardFormatAvailable()/GetClipboardData() mismatch}"
            );
        }

        WCHAR *wide = cast(WCHAR*, GlobalLock(h));
        if (wide == nullptr) {
            CloseClipboard();
            rebJumps(
                "FAIL {Couldn't GlobalLock() UCS2 clipboard data}"
            );
        }

        Value* str = rebTextWide(wide);

        GlobalUnlock(h);
        CloseClipboard();

        // !!! We got wide character data back, which had to be made into a
        // string.  But READ wants BINARY! data.  With UTF-8 Everywhere, the
        // byte representation of the string could be locked + aliased
        // as a UTF-8 binary series.  Conversion is needed for the moment.

        size_t size = rebSpellInto(nullptr, 0, str);  // size query
        char *utf8 = rebAllocN(char, size + 1);
        size_t check_size = rebSpellInto(utf8, size, str); // now fetch
        assert(check_size == size);
        UNUSED(check_size);

        rebRelease(str);

        // See notes on how rebRepossess reclaims the memory of a rebMalloc()
        // (which is used by rebSpell()) as a BINARY!.
        //
        return rebRepossess(utf8, size); }

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
        if (rebNot("text?", arg))
            fail (Error_Invalid_Port_Arg_Raw(arg));

        // Handle /part refinement:
        //
        REBINT len = VAL_LEN_AT(arg);
        if (REF(part) and VAL_INT32(ARG(limit)) < len)
            len = VAL_INT32(ARG(limit));

        if (not OpenClipboard(nullptr))
            rebJumps(
                "FAIL {OpenClipboard() fail on clipboard write}"
            );

        if (not EmptyClipboard()) // !!! is this superfluous?
            rebJumps(
                "FAIL {EmptyClipboard() fail on clipboard write}"
            );

        // Clipboard wants a Windows memory handle with UCS2 data.  Allocate a
        // sufficienctly sized handle, decode Rebol STRING! into it, transfer
        // ownership of that handle to the clipboard.

        HANDLE h = GlobalAlloc(GHND, sizeof(WCHAR) * (len + 1));
        if (h == nullptr)  // per documentation, not INVALID_HANDLE_VALUE
            rebJumps("FAIL {GlobalAlloc() fail on clipboard write}");

        WCHAR *wide = cast(WCHAR*, GlobalLock(h));
        if (wide == nullptr)
            rebJumps("FAIL {GlobalLock() fail on clipboard write}");

        REBINT len_check = rebSpellIntoW(wide, len, arg); // UTF-16 extract
        assert(len <= len_check); // may only be writing /PART of the string
        UNUSED(len_check);

        GlobalUnlock(h);

        HANDLE h_check = SetClipboardData(CF_UNICODETEXT, h);
        CloseClipboard();

        if (h_check == nullptr)
            rebJumps("FAIL {SetClipboardData() failed.}");

        assert(h_check == h);

        RETURN (port); }

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

        RETURN (port); }

    case SYM_CLOSE: {

        // !!! Currently just ignore (it didn't do anything)

        RETURN (port); }

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_PORT, verb));
}


//
//  export get-clipboard-actor-handle: native [
//
//  {Retrieve handle to the native actor for clipboard}
//
//      return: [handle!]
//  ]
//
DECLARE_NATIVE(get_clipboard_actor_handle)
{
    Make_Port_Actor_Handle(OUT, &Clipboard_Actor);
    return OUT;
}
