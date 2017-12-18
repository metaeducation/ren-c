//
//  File: %dev-stdio.c
//  Summary: "Device: Standard I/O for Win32"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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
// Provides basic I/O streams support for redirection and
// opening a console window if necessary.
//

#include <stdio.h>
#include <windows.h>
#include <process.h>
#include <assert.h>

#include <fcntl.h>
#include <io.h>

#include "reb-host.h"

#define BUF_SIZE (16 * 1024)    // MS restrictions apply

static HANDLE Std_Out = NULL;
static HANDLE Std_Inp = NULL;
static WCHAR *Std_Buf = NULL; // Used for UTF-8 conversion of stdin/stdout.

static BOOL Redir_Out = 0;
static BOOL Redir_Inp = 0;

//**********************************************************************


static void Close_Stdio(void)
{
    if (Std_Buf) {
        OS_FREE(Std_Buf);
        Std_Buf = 0;
        //FreeConsole();  // problem: causes a delay
    }
}


//
//  Quit_IO: C
//
DEVICE_CMD Quit_IO(REBREQ *dr)
{
    REBDEV *dev = (REBDEV*)dr; // just to keep compiler happy above

    Close_Stdio();
    //if (dev->flags & RDF_OPEN)) FreeConsole();
    dev->flags &= ~RDF_OPEN;
    return DR_DONE;
}


//
//  Open_IO: C
//
DEVICE_CMD Open_IO(REBREQ *req)
{
    REBDEV *dev;

    dev = Devices[req->device];

    // Avoid opening the console twice (compare dev and req flags):
    if (dev->flags & RDF_OPEN) {
        // Device was opened earlier as null, so req must have that flag:
        if (dev->flags & SF_DEV_NULL)
            req->modes |= RDM_NULL;
        req->flags |= RRF_OPEN;
        return DR_DONE; // Do not do it again
    }

    if (NOT(req->modes & RDM_NULL)) {
        // Get the raw stdio handles:
        Std_Out = GetStdHandle(STD_OUTPUT_HANDLE);
        Std_Inp = GetStdHandle(STD_INPUT_HANDLE);
        //Std_Err = GetStdHandle(STD_ERROR_HANDLE);

        Redir_Out = (GetFileType(Std_Out) != FILE_TYPE_CHAR);
        Redir_Inp = (GetFileType(Std_Inp) != FILE_TYPE_CHAR);

        if (!Redir_Inp || !Redir_Out) {
            // If either input or output is not redirected, preallocate
            // a buffer for conversion from/to UTF-8.
            Std_Buf = OS_ALLOC_N(WCHAR, BUF_SIZE);
        }

        if (!Redir_Inp) {
            //
            // Windows offers its own "smart" line editor (with history
            // management, etc.) in the form of the Windows Terminal.  These
            // modes only apply if a the input is coming from the terminal,
            // not if Rebol has a file redirection connected to the input.
            //
            // While the line editor is running with ENABLE_LINE_INPUT, there
            // are very few hooks offered.  (See remarks on ReadConsole() call
            // about how even being able to terminate the input with escape
            // is not possible--much less reading function keys, etc.)  For
            // the moment, delegating the editing process to proven code
            // built into the OS is considered worth it for the limitations in
            // the console client--given development priorities.
            //
            SetConsoleMode(
                Std_Inp,
                ENABLE_LINE_INPUT
                | ENABLE_PROCESSED_INPUT
                | ENABLE_ECHO_INPUT
                | 0x0080 // ENABLE_EXTENDED_FLAGS (need for quick edit/insert)
                | 0x0040 // quick edit (not defined in VC6)
                | 0x0020 // quick insert (not defined in VC6)
            );
        }
    }
    else
        dev->flags |= SF_DEV_NULL;

    req->flags |= RRF_OPEN;
    dev->flags |= RDF_OPEN;

    return DR_DONE;
}


//
//  Close_IO: C
//
DEVICE_CMD Close_IO(REBREQ *req)
{
    REBDEV *dev = Devices[req->device];

    Close_Stdio();

    dev->flags &= ~RRF_OPEN;

    return DR_DONE;
}


//
//  Write_IO: C
//
// Low level "raw" standard output function.
//
// Allowed to restrict the write to a max OS buffer size.
//
// Returns the number of chars written.
//
DEVICE_CMD Write_IO(REBREQ *req)
{
    if (req->modes & RDM_NULL) {
        req->actual = req->length;
        return DR_DONE;
    }

    BOOL ok = FALSE; // Note: Windows BOOL, not REBOOL

    if (Std_Out) {

        if (Redir_Out) { // Always UTF-8
            DWORD total_bytes;
            ok = WriteFile(
                Std_Out,
                req->common.data,
                req->length,
                &total_bytes,
                0
            );
            UNUSED(total_bytes);
        }
        else {
            // Convert UTF-8 buffer to Win32 wide-char format for console.
            // Thankfully, MS provides something other than mbstowcs();
            // however, if our buffer overflows, it's an error. There's no
            // efficient way at this level to split-up the input data,
            // because its UTF-8 with variable char sizes.
            //
            DWORD len = MultiByteToWideChar(
                CP_UTF8,
                0,
                s_cast(req->common.data),
                req->length,
                Std_Buf,
                BUF_SIZE
            );
            if (len > 0) { // no error
                DWORD total_wide_chars;
                ok = WriteConsoleW(
                    Std_Out,
                    Std_Buf,
                    len,
                    &total_wide_chars,
                    0
                );
                UNUSED(total_wide_chars);
            }
        }

        if (NOT(ok))
            rebFail_OS (GetLastError());

        req->actual = req->length; // want byte count written, assume success

        //if (req->flags & RRF_FLUSH) {
        //  FLUSH();
        //}
    }

    return DR_DONE;
}


//
//  Read_IO: C
//
// Low level "raw" standard input function.
//
// The request buffer must be long enough to hold result.
//
// Result is NOT terminated (the actual field has length.)
//
DEVICE_CMD Read_IO(REBREQ *req)
{
    assert(req->length >= 2); // abort is signaled with (ESC '\0')

    if (req->modes & RDM_NULL) {
        req->common.data[0] = 0;
        return DR_DONE;
    }

    if (Std_Inp == NULL) {
        req->actual = 0;
        return DR_DONE;
    }

    if (Redir_Inp) { // always UTF-8
        DWORD len = MIN(req->length, BUF_SIZE);

        DWORD total;
        REBOOL ok = ReadFile(Std_Inp, req->common.data, len, &total, 0);
        if (NOT(ok))
            rebFail_OS (GetLastError());

        req->actual = total;
        return DR_DONE;
    }

    // !!! ReadConsole() in the ENABLE_LINE_INPUT mode is a terribly limited
    // API, and if you don't use that mode you are basically completely on
    // your own for line editing (backspace, cursoring, etc.)  It's all or
    // nothing--there's no way to hook it--and you can't even tell if an
    // escape is pressed...it always clears to the beginning of line.
    //
    // There might seem to be some hope in the CONSOLE_READCONSOLE_CONTROL
    // parameter.  The structure is horribly documented on MSDN, but it is
    // supposed to offer a way to register some control keys to break out of
    // the input besides a completing newline.  It turns out dwCtrlWakeupMask
    // is (supposedly) a bit mask of 0-31 ASCII points for control characters:
    //
    // https://stackoverflow.com/a/43836992/211160
    //
    // Theory is that with ENABLE_LINE_INPUT, a successfully completed line
    // will always end in CR LF for a `total` of at least 2.  Then if
    // `dwCtrlWakeupMask` is registered for a key, and `nInitialChars` is
    // set to 0 (preserve nothing), the fact that the user terminated with the
    // control key *should* be detectable by `total == 0`.
    //
    // But as mentioned, masking escape in as (1 << 27) has no effect.  And
    // when using ENABLE_PROCESSED_INPUT (which you must in order to get the
    // backspace/etc. behavior in the line editor) then Ctrl-C will exit
    // ReadConsole() call and return a total of 0...regardless of whether you
    // mask (1 << 3) or not.  It also exits before the SetConsoleCtrlHandler()
    // does for handling CTRL_C_EVENT.  :-/
    //
    // Then Ctrl-D can be in the mask.  It does indeed exit the read when it
    // is hit, but ignores `nInitialChars` and just sticks a codepoint of 4
    // (^D) wherever the cursor is!!!
    //
    // As awful as this all sounds, it actually can be manipulated to give
    // three different outcomes.  It's just rather rickety-seeming, but the
    // odds are this all comes from bend-over-backward legacy support of
    // things that couldn't be changed to be better...so it will probably
    // be working this way for however long Win32 stays relevant.
    //
    // For the moment, having Ctrl-D instead of escape for abort input (vs.
    // abort script) is accepted as the price paid, to delegate the Unicode
    // aware cursoring/backspacing/line-editing to the OS.  Which also means
    // a smaller executable than trying to rewrite it oneself.
    //
    CONSOLE_READCONSOLE_CONTROL ctl;
    ctl.nLength = sizeof(CONSOLE_READCONSOLE_CONTROL);
    ctl.nInitialChars = 0; // when hit, empty buffer...no CR LF
    ctl.dwCtrlWakeupMask = (1 << 4); // ^D (^C is implicit)
    ctl.dwControlKeyState = 0; // no alt+shift modifiers (beyond ctrl)

    DWORD total;
    REBOOL ok = ReadConsoleW(Std_Inp, Std_Buf, BUF_SIZE - 1, &total, &ctl);
    if (NOT(ok))
        rebFail_OS (GetLastError());

    // Ctrl-C and Ctrl-D will terminate input without the newline that is
    // expected by code calling INPUT.  If these forms of cancellation are
    // encountered, we write a line to maintain the visual invariant.
    //
    WCHAR cr_lf_term[3];
    cr_lf_term[0] = CR;
    cr_lf_term[1] = LF;
    cr_lf_term[2] = '\0';

    if (total == 0) {
        //
        // Has to be a Ctrl-C, because it returns 0 total.  There is no
        // apparent way to avoid this behavior a priori, nor to resume the
        // console operation as if nothing had happened.
        //
        // Given that, write compensating line.  !!! Check error?
        //
        WriteConsoleW(Std_Out, cr_lf_term, 2, NULL, 0);

        // The Ctrl-C will be passed on to the SetConsoleCtrlHandler().
        // Regardless of what the Ctrl-C event does (it runs on its own thread
        // in a console app) we'll get here, and have to return *something*
        // to INPUT or whoever called.
        //
        // Give a zero length output.  If halting was enabled, further Rebol
        // code of INPUT should not run.  In the case that INPUT sees this
        // signal and a halt does not happen, it will FAIL.  Only special
        // clients which can run with no cancellability (HOST-CONSOLE)
        // should trap it and figure out what to do with the non-ideal state.
        //
        strcpy(s_cast(req->common.data), "");
        req->actual = 0;

        return DR_DONE;
    }

    DWORD i;
    for (i = 0; i < total; ++i) {
        if (Std_Buf[i] == 4) {
            //
            // A Ctrl-D poked in at any position means escape.  Return it
            // as a single-character null terminated string of escape.
            //
            strcpy(s_cast(req->common.data), "\x1B"); // 0x1B = 27 (escape)
            req->actual = 1;

            // Write compensating line.  !!! Check error?
            //
            WriteConsoleW(Std_Out, cr_lf_term, 2, NULL, 0);
            return DR_DONE;
        }
    }

    DWORD encoded_len = WideCharToMultiByte(
        CP_UTF8,
        0,
        Std_Buf,
        total,
        s_cast(req->common.data),
        req->length,
        0,
        0
    );

    // Note: WideCharToMultibyte would fail if cchWideChar was 0.  (we
    // know total is *not* 0 as it was handled above.)  In any case, a 0
    // result for the encoded length is how errors are signaled, as it could
    // not happen any other way.
    //
    if (encoded_len == 0)
        rebFail_OS (GetLastError());

    req->actual = encoded_len;
    return DR_DONE;
}


/***********************************************************************
**
**  Command Dispatch Table (RDC_ enum order)
**
***********************************************************************/

static DEVICE_CMD_FUNC Dev_Cmds[RDC_MAX] =
{
    0,  // init
    Quit_IO,
    Open_IO,
    Close_IO,
    Read_IO,
    Write_IO,
    0,  // poll
    0,  // connect
    0,  // query
    0,  // modify
    0,  // CREATE was once used for opening echo file
};

DEFINE_DEV(
    Dev_StdIO,
    "Standard IO", 1, Dev_Cmds, RDC_MAX, sizeof(struct devreq_file)
);

