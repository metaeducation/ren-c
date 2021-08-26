//
//  File: %stdio-posix.c
//  Summary: "Device: Standard I/O for Posix"
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

#include "sys-core.h"

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include "readline.h"


//
//  Startup_Stdio: C
//
void Startup_Stdio(void)
{
  #if defined(REBOL_SMART_CONSOLE)
    if (isatty(STDIN_FILENO))  // is termios-capable (not redirected to a file)
        Term_IO = Init_Terminal();
  #endif
}


//
//  Read_Stdin_Byte_Interrupted: C
//
bool Read_Stdin_Byte_Interrupted(bool *eof, REBYTE *out) {
    int byte_or_eof = fgetc(stdin);
    if (byte_or_eof != -1) {
        assert(byte_or_eof >= 0 and byte_or_eof < 256);
        *out = byte_or_eof;
        *eof = false;  // was not end of file
        return false;  // was not interrupted
    }

    if (errno == EINTR) {
        //
        // When there's an interrupt, we need to clear the error or every read
        // after this will think there's an interrupt.  Trust the caller will
        // propagate the flag.
        //
        clearerr(stdin);

      #if !defined(NDEBUG)
        *out = 0xFF;  // bad UTF8 byte
        *eof = false;  // bad eof status
      #endif
        return true;  // was interrupted
    }

    // -1 can happen on Ctrl-C or on end of file.
    //
    if (feof(stdin)) {
      #if !defined(NDEBUG)
        *out = 0xFF;  // bad UTF8 byte
      #endif
        *eof = true;  // was end of file
        return false;  // was not interrupted
    }

    return true;  // was interrupted
}


//
//  Write_IO: C
//
// This write routine takes a REBVAL* that is either a BINARY! or a TEXT!.
// Length is in conceptual units (codepoints for TEXT!, bytes for BINARY!)
//
void Write_IO(const REBVAL *data, REBLEN len)
{
    assert(IS_TEXT(data) or IS_BINARY(data));

    if (STDOUT_FILENO < 0)
        return;  // !!! This used to do nothing (?)

  #if defined(REBOL_SMART_CONSOLE)
    if (Term_IO) {
        if (IS_CHAR(data)) {
            assert(len == 1);
            Term_Insert(Term_IO, data);
        }
        else if (IS_TEXT(data)) {
            if (cast(REBLEN, rebUnbox("length of", data)) == len)
                Term_Insert(Term_IO, data);
            else {
                REBVAL *part = rebValue("copy/part", data, rebI(len));
                Term_Insert(Term_IO, part);
                rebRelease(part);
            }
        }
        else {
            // Write out one byte at a time, by translating it into two hex
            // digits and sending them to WriteConsole().
            //
            // !!! It would be nice if this were in a novel style...
            //
            bool ok = true;

            const REBYTE *tail = BIN_TAIL(VAL_BINARY(data));
            const REBYTE *bp = VAL_BINARY_AT(data);
            for (; bp != tail; ++bp) {
                char digits[2];
                digits[0] = Hex_Digits[*bp / 16];
                digits[1] = Hex_Digits[*bp % 16];
                long total = write(STDOUT_FILENO, digits, 2);
                if (total < 0) {
                    ok = false;
                    break;  // need to restore text attributes before fail()
                }
                assert(total == 2);
                UNUSED(total);
            }

            if (not ok)
                rebFail_OS(errno);
        }
    }
    else
  #endif
    {
        const REBYTE *bp;
        REBSIZ size;
        if (IS_BINARY(data)) {
            bp = VAL_DATA_AT(data);
            size = len;
        }
        else {
            bp = VAL_UTF8_SIZE_AT(&size, data);
        }

        long total = write(STDOUT_FILENO, bp, size);

        if (total < 0)
            rebFail_OS(errno);

        assert(cast(size_t, total) == size);
    }
}


//
//  Read_IO: C
//
// !!! While transitioning away from the R3-Alpha "abstract OS" model,
// this hook now receives a BINARY! which it is expected to fill with UTF-8
// data, with a certain number of bytes.
//
// The request buffer must be long enough to hold result.
//
size_t Read_IO(REBYTE *buffer, size_t size)
{
  #if defined(REBOL_SMART_CONSOLE)
    assert(not Term_IO);  // should have handled in %p-stdio.h
  #endif

    // read restarts on signal
    //
    long total = read(STDIN_FILENO, buffer, size);
    if (total < 0)
        rebFail_OS (errno);

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
