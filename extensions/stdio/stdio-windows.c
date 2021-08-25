//
//  File: %stdio-windows.c
//  Summary: "Device: Standard I/O for Win32"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2021 Ren-C Open Source Contributors
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

static HANDLE Stdout_Handle = nullptr;
static HANDLE Stdin_Handle = nullptr;

// While pipes and redirected files in Windows do raw bytes, the console
// uses UTF-16.  The calling layer expects UTF-8 back, so the Windows API
// for conversion is used.  The UTF-16 data must be held in a buffer.
//
// We only allocate this buffer if we're not redirecting and it is necessary.
//
#define WCHAR_BUF_CAPACITY (16 * 1024)
static WCHAR *Wchar_Buf = nullptr;


static bool Redir_Out = false;
static bool Redir_Inp = false;


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
}


//
//  Write_IO: C
//
// This write routine takes a REBVAL* that is either a BINARY! or a TEXT!.
// Length is in conceptual units (codepoints for TEXT!, bytes for BINARY!)
//
void Write_IO(const REBVAL *data, REBLEN len)
{
    assert(IS_BINARY(data) or IS_TEXT(data));

    if (Stdout_Handle == nullptr)
        return;

  #if defined(REBOL_SMART_CONSOLE)
    if (Term_IO) {
        if (IS_TEXT(data)) {
            //
            // !!! Having to subset the string is wasteful, so Term_Insert()
            // should take a length -or- series slicing needs to be solved.
            //
            if (cast(REBLEN, rebUnbox("length of", data)) == len)
                Term_Insert(Term_IO, data);
            else {
                REBVAL *part = rebValue("copy/part", data, rebI(len));
                Term_Insert(Term_IO, part);
                rebRelease(part);
            }
        }
        else {
            // Writing a BINARY! to a redirected standard out, e.g. a CGI
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
            // So we change the color and write the data in hexadecimal!

            CONSOLE_SCREEN_BUFFER_INFO csbi;
            GetConsoleScreenBufferInfo(Stdout_Handle, &csbi);  // save color

            SetConsoleTextAttribute(
                Stdout_Handle,
                BACKGROUND_GREEN | FOREGROUND_BLUE
            );

            BOOL ok = true;

            // Write out one byte at a time, by translating it into two hex
            // digits and sending them to WriteConsole().
            //
            const REBYTE *tail = BIN_TAIL(VAL_BINARY(data));
            const REBYTE *bp = VAL_BINARY_AT(data);
            for (; bp != tail; ++bp) {
                WCHAR digits[2];
                digits[0] = Hex_Digits[*bp / 16];
                digits[1] = Hex_Digits[*bp % 16];
                DWORD total_wide_chars;
                ok = WriteConsoleW(
                    Stdout_Handle,
                    digits,
                    2,  // wants wide character count
                    &total_wide_chars,
                    0
                );
                if (not ok)
                    break;  // need to restore text attributes before fail()
                assert(total_wide_chars == 2);
                UNUSED(total_wide_chars);
            }

            SetConsoleTextAttribute(
                Stdout_Handle,
                csbi.wAttributes  // restore these attributes
            );

            if (not ok)
                rebFail_OS (GetLastError());
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

        // !!! Historically, Rebol on Windows automatically "enlined" strings
        // on write to turn LF to CR LF.  This was done in Prin_OS_String().
        // However, the current idea is to be more prescriptive and not
        // support this without a special codec.  In lieu of a more efficient
        // codec method, those wishing to get CR LF will need to manually
        // enline, or ADAPT their WRITE to do this automatically.
        //
        // Note that redirection on Windows does not use UTF-16 typically.
        // Even CMD.EXE requires a /U switch to do so.

        const REBYTE *bp;
        REBSIZ size;
        if (IS_BINARY(data)) {
            bp = VAL_DATA_AT(data);
            size = len;
        }
        else {
            bp = VAL_UTF8_SIZE_AT(&size, data);
        }

        DWORD total_bytes;
        BOOL ok = WriteFile(
            Stdout_Handle,
            bp,
            size,
            &total_bytes,
            0
        );
        if (not ok)
            rebFail_OS (GetLastError());

        assert(total_bytes == size);
        UNUSED(total_bytes);
    }
}


//
//  Read_IO: C
//
// The request buffer must be long enough to hold result.
// Result is NOT terminated.
//
size_t Read_IO(REBYTE *buffer, size_t capacity)
{
    assert(capacity >= 2);  // abort is signaled with (ESC '\0')

    if (Stdin_Handle == nullptr)
        return 0;  // can't read from a null handle

    // !!! While Windows historically uses UCS-2/UTF-16 in its console I/O,
    // the plain ReadFile() style calls are byte-oriented, so you get whatever
    // code page is in use.  This is good for UTF-8 files, but would need
    // some kind of conversion to get better than ASCII on systems without
    // the REBOL_SMART_CONSOLE setting.

    DWORD bytes_to_read = capacity;

  try_smaller_read: ;  // semicolon since next line is declaration
    DWORD total;
    BOOL ok = ReadFile(
        Stdin_Handle,
        buffer,
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

    return total;
}


//
//  Shutdown_Stdio: C
//
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
