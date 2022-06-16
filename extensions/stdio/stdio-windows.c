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
#undef OUT  // %minwindef.h defines this, we have a better use for it
#undef VOID  // %winnt.h defines this, we have a better use for it


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


enum Piped_Type {
    Piped_0 = 0,  // uninitialized

    Not_Piped,
    Piped_To_File,
    Piped_To_NUL,
};

// If we don't know if the input is redirected from NUL, we do not know if
// a read of 0 should act like an end of file or be ignored as if it was just
// some process that incidentally did a WriteFile() of 0 bytes.
//
// !!! Note: This tried a technique described here that did not work:
//
// https://stackoverflow.com/a/21070042
//
// There is a more promising-seeming "GetFileInformationByHandleEx()" but a
// superficial attempt at using it did not work.  So until it becomes a
// priority, we use a heuristic that if something gives an unreasonable number
// of 0 byte reads in a row it is treated as an EOF.
//
enum Piped_Type Detect_Handle_Piping(HANDLE h)
{
    if (GetFileType(h) != FILE_TYPE_CHAR)
        return Piped_To_File;

    // !!! See note, can't detect Piped_To_NUL at present.

    return Not_Piped;
}


static enum Piped_Type stdin_piping;
static enum Piped_Type stdout_piping;


//
//  Startup_Stdio: C
//
void Startup_Stdio(void)
{
    Stdout_Handle = GetStdHandle(STD_OUTPUT_HANDLE);
    Stdin_Handle = GetStdHandle(STD_INPUT_HANDLE);
    //StdErr_Handle = GetStdHandle(STD_ERROR_HANDLE);

    stdout_piping = Detect_Handle_Piping(Stdout_Handle);
    stdin_piping = Detect_Handle_Piping(Stdin_Handle);

  #if defined(REBOL_SMART_CONSOLE)
    //
    // We can't sensibly manage the character position for an editing buffer
    // if either the input or output are redirected.  At the moment, this
    // means no smart terminal functions (including history) are available.
    //
    // Note: Technically the command history could be offered as a list even
    // without a smart terminal.  You just couldn't cursor through it.  Review.
    //
    if (stdin_piping == Not_Piped and stdout_piping == Not_Piped)
        Term_IO = Init_Terminal();
  #endif
}


//
//  Read_Stdin_Byte_Interrupted: C
//
// Returns true if the operation was interrupted by a SIGINT.
//
bool Read_Stdin_Byte_Interrupted(bool *eof, REBYTE *out) {
    //
    // We don't read bytes from the smart console--it uses UTF16 and should
    // be read with the terminal layer.  This is just for redirection or use
    // of a non-smart console.
    //
  #ifdef REBOL_SMART_CONSOLE
    assert(Term_IO == nullptr);
  #endif

    // !!! See note in Detect_Handle_Piping(), that currently we don't have
    // a working mechanism to detect null.  Workaround uses num_read_attempts.
    //
    if (stdin_piping == Piped_To_NUL) {  // reads nothing forever, no eof
        *eof = true;  // but treat like it is an end of file
        *out = 0xFF;  // unused, make it trash (0xFF is bad UTF-8)
        return false;  // not interrupted
    }

    DWORD actual;
    bool ok;
    int num_zero_reads = 0;
    do {
        // The `actual` will come back as 0 if the other end of a pipe called
        // the WriteFile function with nNumberOfBytesToWrite set to zero.
        // WinAPI docs say "The behavior of a null write operation depends on
        // the underlying file system or communications technology."  Another
        // source says "A null write operation does not write any bytes but
        // does cause the time stamp to change."
        //
        // Empirically it seems a null write needs to be accepted if received
        // on a pipe...just skipped over:
        //
        // https://marc.info/?l=cygwin&m=133547528003210
        //
        //   "While a null write appears nonsensical, every single .NET program
        //   that uses the Console class to write to standard output/error will
        //   do a null write, as .NET does this to verify the stream is OK.
        //   Other software could easily decide to write zero bytes to standard
        //   output as well (e.g. if outputting an empty string)."
        //
        // We have to be careful of redirects of NUL to input, which will
        // always act like it wrote 0 bytes on the pipe.  Handled above.
        //
        ok = ReadFile(Stdin_Handle, out, 1, &actual, nullptr);

        if (++num_zero_reads == 128) {  // heuristic to detect NUL in piping
            *eof = true;  // treat it like an end of file
            *out = 0xFF;  // unused, make it trash (0xFF is bad UTF-8)
            return false;  // not interrupted
        }
    } while (ok and actual == 0);

    if (ok) {
        //
        // The general philosophy on CR LF sequences is that files containing
        // them are a foreign encoding.  We do not automatically filter any
        // files for them--and READ-LINE will choke on it.  You have to use
        // READ-BINARY if you want to handle CR.
        //
        // But if you are not redirecting I/O, Windows unfortunately does throw
        // in CR LF sequences from what you type in the console.  Filter those.
        //
        if (*out != CR or stdin_piping == Piped_To_File) {
            *eof = false;  // not end of file
            return false;  // not interrupted
        }

        assert(stdin_piping == Not_Piped);

        // Be robust if the console implementation does 0 byte WriteFile()
        do {
            ok = ReadFile(Stdin_Handle, out, 1, &actual, nullptr);
        } while (ok and actual == 0);

        if (ok and *out == LF) {
            *eof = false;  // not end of file
            return false;  // not interrupted
        }

        DWORD last_error = GetLastError();
        if (*out != LF or last_error == ERROR_HANDLE_EOF)
            fail ("CR found not followed by LF in Windows typed input");

        fail (rebError_OS(last_error));
    }

    // If you are piping with something like `echo "hello" | r3 reader.r` then
    // it is expected you will get the "error" of a broken pipe when the sender
    // is finished.  It's up to higher-level protocols to decide if the
    // connection was at a proper time.
    //
    DWORD last_error = GetLastError();
    if (last_error == ERROR_HANDLE_EOF or last_error == ERROR_BROKEN_PIPE) {
        *eof = true;  // was end of file
        return false;  // was not interrupted
    }
    fail (rebError_OS(GetLastError()));
}


//
//  Write_IO: C
//
// This write routine takes a REBVAL* that is either a BINARY! or a TEXT!.
// Length is in conceptual units (codepoints for TEXT!, bytes for BINARY!)
//
void Write_IO(const REBVAL *data, REBLEN len)
{
    assert(IS_BINARY(data) or IS_TEXT(data) or IS_ISSUE(data));

    if (Stdout_Handle == nullptr)
        return;

  #if defined(REBOL_SMART_CONSOLE)
    if (Term_IO) {
        if (IS_CHAR(data)) {
            assert(len == 1);
            Term_Insert(Term_IO, data);
        }
        else if (IS_TEXT(data)) {
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
        assert(stdin_piping != Not_Piped or stdout_piping != Not_Piped);
                // ^-- should have used smarts otherwise
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
  #if defined(REBOL_SMART_CONSOLE)
    if (Term_IO) {
        Quit_Terminal(Term_IO);
        Term_IO = nullptr;
    }
  #endif
}
