//
//  file: %stdio-posix.c
//  summary: "Device: Standard I/O for Posix"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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

// We are only using the limited facilities of stdin and fgetc().
// Generally speaking, we avoid usage of other stdio functions in the system.
// (Outside of printf() in RUNTIME_CHECKS builds).
//
#include <stdio.h>

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
bool Read_Stdin_Byte_Interrupted(bool *eof, Byte* out) {
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

      #if RUNTIME_CHECKS
        *out = 0xFF;  // bad UTF8 byte
        *eof = false;  // bad eof status
      #endif
        return true;  // was interrupted
    }

    // -1 can happen on Ctrl-C or on end of file.
    //
    if (feof(stdin)) {
      #if RUNTIME_CHECKS
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
// This write routine takes a Value* that is either a BLOB! or a TEXT!.
// Length is in conceptual units (codepoints for TEXT!, bytes for BLOB!)
//
void Write_IO(const Value* data, REBLEN len)
{
    assert(Is_Text(data) or Is_Blob(data));

    if (STDOUT_FILENO < 0)
        return;  // !!! This used to do nothing (?)

  #if defined(REBOL_SMART_CONSOLE)
    if (Term_IO) {
        if (Is_Rune_And_Is_Char(data)) {
            assert(len == 1);
            Term_Insert(Term_IO, data);
        }
        else if (Is_Text(data)) {
            if (rebUnbox("length of", data) == len)
                Term_Insert(Term_IO, data);
            else {
                Value* part = rebValue("copy:part", data, rebI(len));
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

            const Byte* tail = Binary_Tail(Cell_Binary(data));
            const Byte* bp = Blob_At(data);
            for (; bp != tail; ++bp) {
                char digits[2];
                digits[0] = g_hex_digits[*bp / 16];
                digits[1] = g_hex_digits[*bp % 16];
                long total = write(STDOUT_FILENO, digits, 2);
                if (total < 0) {
                    ok = false;
                    break;  // need to restore text attributes before panic()
                }
                assert(total == 2);
                UNUSED(total);
            }

            if (not ok)
                rebPanic_OS(errno);
        }
    }
    else
  #endif
    {
        const Byte* bp;
        Size size;
        if (Is_Blob(data)) {
            bp = Blob_At(data);
            size = len;
        }
        else {
            REBLEN len_check;
            bp = Cell_Utf8_Len_Size_At_Limit(&len_check, &size, data, &len);
            assert(len_check == len);
            UNUSED(len_check);
        }

        long total = write(STDOUT_FILENO, bp, size);

        if (total < 0)
            rebPanic_OS(errno);

        assert(total == size);
    }
}


//
//  Read_IO: C
//
// !!! While transitioning away from the R3-Alpha "abstract OS" model,
// this hook now receives a BLOB! which it is expected to fill with UTF-8
// data, with a certain number of bytes.
//
// The request buffer must be long enough to hold result.
//
size_t Read_IO(Byte* buffer, size_t size)
{
  #if defined(REBOL_SMART_CONSOLE)
    assert(not Term_IO);  // should have handled in %p-stdio.h
  #endif

    // read restarts on signal
    //
    long total = read(STDIN_FILENO, buffer, size);
    if (total < 0)
        rebPanic_OS (errno);

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
