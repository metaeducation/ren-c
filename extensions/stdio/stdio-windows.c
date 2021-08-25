//
//  File: %dev-stdio.c
//  Summary: "Device: Standard I/O for Win32"
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
// Provides basic I/O streams support for redirection and
// opening a console window if necessary.
//

#define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
#include <windows.h>
#undef IS_ERROR

// !!! Read_IO writes directly into a BINARY!, whose size it needs to keep up
// to date (in order to have it properly terminated and please the GC).  At
// the moment it does this with the internal API, though libRebol should
// hopefully suffice in the future.  This is part of an ongoing effort to
// make the device layer work more in the vocabulary of Rebol types.
//
#include "sys-core.h"

#include "readline.h"

#if defined(REBOL_SMART_CONSOLE)
    extern STD_TERM *Term_IO;
#endif

EXTERN_C REBDEV Dev_StdIO;

static HANDLE Stdout_Handle = nullptr;
static HANDLE Stdin_Handle = nullptr;

// While pipes and redirected files in Windows do raw bytes, the console
// uses UTF-16.  The calling layer expects UTF-8 back, so the Windows API
// for conversion is used.  The UTF-16 data must be held in a buffer.
//
#define WCHAR_BUF_CAPACITY (16 * 1024)
static WCHAR *Wchar_Buf = nullptr;

static bool Redir_Out = false;
static bool Redir_Inp = false;

//**********************************************************************


extern void Shutdown_Stdio(void);
void Shutdown_Stdio(void)
{
    if (Wchar_Buf) {
        free(Wchar_Buf);
        Wchar_Buf = nullptr;
    }

  #if defined(REBOL_SMART_CONSOLE)
    if (Term_IO) {
        Quit_Terminal(Term_IO);
        Term_IO = nullptr;
    }
  #endif
}


//
//  Startup_Stdio: C
//
void Startup_Stdio(void)
{
    // Get the raw stdio handles:
    Stdout_Handle = GetStdHandle(STD_OUTPUT_HANDLE);
    Stdin_Handle = GetStdHandle(STD_INPUT_HANDLE);
    //StdErr_Handle = GetStdHandle(STD_ERROR_HANDLE);

    Redir_Out = (GetFileType(Stdout_Handle) != FILE_TYPE_CHAR);
    Redir_Inp = (GetFileType(Stdin_Handle) != FILE_TYPE_CHAR);

    if (not Redir_Inp or not Redir_Out) {
        //
        // If either input or output is not redirected, preallocate
        // a buffer for conversion from/to UTF-8.
        //
        Wchar_Buf = cast(WCHAR*,
            malloc(sizeof(WCHAR) * WCHAR_BUF_CAPACITY)
        );
    }

  #if defined(REBOL_SMART_CONSOLE)
    //
    // We can't sensibly manage the character position for an editing
    // buffer if either the input or output are redirected.  This means
    // no smart terminal functions (including history) are available.
    //
    if (not Redir_Inp and not Redir_Out)
        Term_IO = Init_Terminal();
  #endif

    Dev_StdIO.flags |= RDF_OPEN;
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
DEVICE_CMD Write_IO(REBREQ *io)
{
    struct rebol_devreq *req = Req(io);

    if (Stdout_Handle == nullptr)
        return DR_DONE;

  #if defined(REBOL_SMART_CONSOLE)
    if (Term_IO) {
        if (req->modes & RFM_TEXT) {
            //
            // !!! This is a wasteful step as the text initially came from
            // a Rebol TEXT! :-/  But moving this one step at a time, to
            // where the device layer speaks in terms of Rebol datatypes.
            //
            REBVAL *text = rebSizedText(s_cast(req->common.data), req->length);
            Term_Insert(Term_IO, text);
            rebRelease(text);
        }
        else {
            // !!! Writing a BINARY! to a redirected console, e.g. a CGI
            // script, makes sense--e.g. it might be some full bandwidth data
            // being downloaded that's neither UTF-8 nor UTF-16.  And it makes
            // some sense on UNIX, as the terminal will just have to figure
            // out what to do with those bytes.  But on Windows, there's a
            // problem...since the console API takes wide characters.
            //
            // We *could* assume the user meant to write UTF-16 data, and only
            // fail if it's an odd number of bytes.  But that means that the
            // write of the BINARY! would have different meanings if directed
            // at a file as opposed to not redirected.  If there was a true
            // need to write UTF-16 data directly to the console, that should
            // be a distinct console-oriented function.
            //
            // Instead, we can do something like change the color and write
            // out some information.  Ideally this would be something like
            // the data in hexadecimal, but since this is a niche leave it
            // as a placeholder.
            //
            // !!! The caller currently breaks up binary data into chunks to
            // pass in order to handle cancellation, so that should also be
            // taken into account.

            CONSOLE_SCREEN_BUFFER_INFO csbi;
            GetConsoleScreenBufferInfo(Stdout_Handle, &csbi);  // save color

            SetConsoleTextAttribute(
                Stdout_Handle,
                BACKGROUND_GREEN | FOREGROUND_BLUE
            );

            WCHAR message[] = L"Binary Data Sent to Non-Redirected Console";

            DWORD total_wide_chars;
            BOOL ok = WriteConsoleW(
                Stdout_Handle,
                message,
                wcslen(message),  // wants wide character count
                &total_wide_chars,
                0
            );
            SetConsoleTextAttribute(
                Stdout_Handle,
                csbi.wAttributes  // restore these attributes
            );

            if (not ok)
                rebFail_OS (GetLastError());
            UNUSED(total_wide_chars);
        }
    }
    else
  #endif
    {
        // !!! The concept of building C89 on Windows would require us to
        // still go through a UTF-16 conversion process to write to the
        // console if we were to write to the terminal...even though we would
        // not have the rich line editing.  Rather than fixing this, it
        // would be better to just go through printf()...thus having a generic
        // answer for C89 builds on arbitrarily limited platforms, vs.
        // catering to it here.
        //
      #if defined(REBOL_SMART_CONSOLE)
        assert(Redir_Inp or Redir_Out);  // should have used smarts otherwise
      #endif

        if (req->modes & RFM_TEXT) {
            //
            // Writing UTF-8 text.  Currently no actual check is done to make
            // sure that it's valid UTF-8, even invalid bytes would be written
            // but this could be changed.
        }

        // !!! Historically, Rebol on Windows automatically "enlined" strings
        // on write to turn LF to CR LF.  This was done in Prin_OS_String().
        // However, the current idea is to be more prescriptive and not
        // support this without a special codec.  In lieu of a more efficient
        // codec method, those wishing to get CR LF will need to manually
        // enline, or ADAPT their WRITE to do this automatically.
        //
        // Note that redirection on Windows does not use UTF-16 typically.
        // Even CMD.EXE requires a /U switch to do so.

        DWORD total_bytes;
        BOOL ok = WriteFile(
            Stdout_Handle,
            req->common.data,
            req->length,
            &total_bytes,
            0
        );
        if (not ok)
            rebFail_OS (GetLastError());
        UNUSED(total_bytes);
    }

    req->actual = req->length;  // want byte count written, assume success

    // !!! There was some code in R3-Alpha here which checked req->flags for
    // "RRF_FLUSH" and would flush, but it was commented out (?)

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
DEVICE_CMD Read_IO(REBREQ *io)
{
    struct rebol_devreq *req = Req(io);
    assert(req->length >= 2);  // abort is signaled with (ESC '\0')

    // !!! While transitioning away from the R3-Alpha "abstract OS" model,
    // this hook now receives a BINARY! in req->text which it is expected to
    // fill with UTF-8 data, with req->length bytes.
    //
    REBBIN *bin = VAL_BINARY_KNOWN_MUTABLE(req->common.binary);
    assert(SER_AVAIL(bin) >= req->length);

    REBLEN orig_len = BIN_LEN(bin);

    if (Stdin_Handle == nullptr) {
        TERM_BIN_LEN(bin, 0);
        return DR_DONE;
    }

    // !!! While Windows historically uses UCS-2/UTF-16 in its console I/O,
    // the plain ReadFile() style calls are byte-oriented, so you get whatever
    // code page is in use.  This is good for UTF-8 files, but would need
    // some kind of conversion to get better than ASCII on systems without
    // the REBOL_SMART_CONSOLE setting.

    DWORD bytes_to_read = req->length;

  try_smaller_read: ;  // semicolon since next line is declaration
    DWORD total;
    BOOL ok = ReadFile(
        Stdin_Handle,
        BIN_AT(bin, orig_len),
        bytes_to_read,
        &total,
        0
    );
    if (not ok) {
        DWORD error_code = GetLastError();
        if (error_code == ERROR_NOT_ENOUGH_MEMORY) {
            //
            // When you call ReadFile() instead of ReadConsole() on a standard
            // input handle that's attached to a console, some versions of
            // Windows (notably Windows 7) can return this error when the
            // length of the read request is too large.  How large is unknown.
            //
            // https://github.com/golang/go/issues/13697
            //
            // To address this, we back the size off and try again a few
            // times before actually raising an error.
            //
            if (bytes_to_read > 10 * 1024) {
                bytes_to_read -= 1024;
                goto try_smaller_read;
            }
        }
        rebFail_OS (GetLastError());
    }

    TERM_BIN_LEN(bin, total + orig_len);
    return DR_DONE;
}


/***********************************************************************
**
**  Command Dispatch Table (RDC_ enum order)
**
***********************************************************************/

static DEVICE_CMD_CFUNC Dev_Cmds[RDC_MAX] =
{
    0,
    0,
    Read_IO,
    Write_IO,
    0,  // connect
    0,  // query
    0,  // CREATE was once used for opening echo file
};

DEFINE_DEV(
    Dev_StdIO,
    "Standard IO", 1, Dev_Cmds, RDC_MAX, sizeof(struct rebol_devreq)
);
