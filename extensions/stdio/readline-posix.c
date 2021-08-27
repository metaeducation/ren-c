//
//  File: %readline-posix.c
//  Summary: "Simple readline() line input handler"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2020 Ren-C Open Source Contributors
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
// Processes special keys for input line editing and recall.
//
// Avoids use of complex OS libraries and GNU readline() but hardcodes some
// parts only for the common standard.
//

#include <assert.h>
#include <stdint.h>
#include "reb-c.h"

#include "readline.h"  // might define REBOL_SMART_CONSOLE

#if defined(REBOL_SMART_CONSOLE)

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>  // has POSIX read() and write()
#include <errno.h>

#include <termios.h>

#define UNI_ENCODED_MAX 4


//=//// REBOL INCLUDES + HELPERS //////////////////////////////////////////=//


#define xrebWord(cstr) \
    rebValue("the", cstr)


//=//// CONFIGURATION /////////////////////////////////////////////////////=//

enum {
    BEL = 7,
    BS = 8,
    LF = 10,
    CR = 13,
    ESC = 27,
    DEL = 127
};


inline static void WRITE_UTF8(const unsigned char *utf8, size_t size) {
    if (write(STDOUT_FILENO, utf8, size) == -1) {
        /* Error here, or better to "just try to keep going"? */
    }
}

#define READ_BUF_LEN 64

struct Reb_Terminal_Struct {
    REBVAL *buffer;  // a TEXT! used as a buffer
    unsigned int pos;  // cursor position within the line

    unsigned char buf[READ_BUF_LEN];  // '\0' terminated, needs -1 on read()
    const unsigned char *cp;

    // In buffered mode, printable characters accumulate in a TEXT!, and are
    // not sent until the input buffer triggers a non-bufferable event.  Then
    // the buffer is sent, with the non-bufferable event held for next call.
    //
    REBVAL *e_pending;

    struct termios original_attrs;
};


//=//// GLOBALS ///////////////////////////////////////////////////////////=//

static bool Term_Initialized = false;  // Terminal init was successful


inline static unsigned int Term_End(STD_TERM *t)
  { return rebUnboxInteger("length of", t->buffer); }

inline static unsigned int Term_Remain(STD_TERM *t)
  { return Term_End(t) - t->pos; }


static bool Read_Bytes_Interrupted(STD_TERM *t);

//
//  Init_Terminal: C
//
// If possible, change the terminal to "raw" mode (where characters are
// received one at a time, as opposed to "cooked" mode where a whole line is
// read at once.)
//
STD_TERM *Init_Terminal(void)
{
    assert(not Term_Initialized);

    // Good reference on termios:
    //
    // https://blog.nelhage.com/2009/12/a-brief-introduction-to-termios/
    // https://blog.nelhage.com/2009/12/a-brief-introduction-to-termios-termios3-and-stty/
    // https://blog.nelhage.com/2010/01/a-brief-introduction-to-termios-signaling-and-job-control/
    //
    struct termios attrs;
    if (tcgetattr(STDIN_FILENO, &attrs) == -1)
        return nullptr;  // Note: errno is set if tcgetattr() fails

    STD_TERM *t = cast(STD_TERM*, malloc(sizeof(STD_TERM)));

    // !!! Ultimately, we want to be able to recover line history from a
    // file across sessions.  It makes more sense for the logic doing that
    // to be doing it in Rebol.  For starters, we just make it fresh.
    //
    Line_History = rebValue("[{}]");  // current line is empty string
    rebUnmanage(Line_History);  // allow Line_History to live indefinitely

    t->buffer = rebValue("{}");
    rebUnmanage(t->buffer);

    t->buf[0] = '\0';  // start read() byte buffer out at empty
    t->cp = t->buf;
    t->pos = 0;

    t->e_pending = nullptr;

    t->original_attrs = attrs;  // cache, to restore upon shutdown

    // Local modes.
    //
    attrs.c_lflag &= ~(ECHO | ICANON);  // raw input

    // Input modes.  Note later Linuxes have a IUTF8 flag that POSIX doesn't,
    // but it seems to only affect the "cooked" mode (as opposed to "raw").
    //
    attrs.c_iflag &= ~(ICRNL | INLCR);  // leave CR and LF as-is

    // Output modes.  If you don't add ONLCR then a single `\n` will just go
    // to the next line and not put the cursor at the start of that line.
    // So ONLCR is needed for the typical unix expectation `\n` does both.
    //
    attrs.c_oflag |= ONLCR;  // On (O)utput, map (N)ew(L)ine to (CR) LF

    // Note: The first time we request input we do so accepting a minimum of
    // 0 characters to read.  This is so we can tell if there has been any
    // buffering/typing by the user prior to switching over into "raw mode".
    //
    attrs.c_cc[VMIN] = 0;   // min num of bytes for READ to return
    attrs.c_cc[VTIME] = 0;  // how long to wait for input

    tcsetattr(STDIN_FILENO, TCSADRAIN, &attrs);

    // If we peek the buffer at the moment of the switch, the user must have
    // typed them before the switch and the characters have already been echoed.
    //
    if (not Read_Bytes_Interrupted(t) and *t->cp != '\0') {
        //
        // There's no perfect "fix" to dealing with pending characters that
        // were typed during startup.  Consider if the console wishes to remap
        // pressing "a" to show "b".  Trying to guess what on the screen you
        // should erase is non-viable, so the best thing to do is just sync up
        // to a new line and let the user know what's happening.
        //
        // !!! Would be nice if this were printed in some kind of color or
        // style to show it wasn't normal output.
        //
        const char *msg = "[rebuffering]\n";
        WRITE_UTF8(cast(const unsigned char *, msg), strsize(msg));
    }

    // Now flip back into the mode where reading requires at least 1 character.
    //
    attrs.c_cc[VMIN] = 1;   // min num of bytes for READ to return
    tcsetattr(STDIN_FILENO, TCSADRAIN, &attrs);

    Term_Initialized = true;
    return t;
}


//
//  Term_Pos: C
//
// The STD_TERM is opaque, but it holds onto a buffer.
//
int Term_Pos(STD_TERM *t)
{
    return t->pos;
}


//
//  Term_Buffer: C
//
// This gives you a read-only perspective on the buffer.  You should not
// change it directly because doing so would not be in sync with the cursor
// position or what is visible on the display.  All changes need to go through
// the terminal itself.
//
REBVAL *Term_Buffer(STD_TERM *t)
{
    return rebValue("const", t->buffer);
}


//
//  Quit_Terminal: C
//
// Restore the terminal modes original entry settings,
// in preparation for exit from program.
//
void Quit_Terminal(STD_TERM *t)
{
    assert(Term_Initialized);

    tcsetattr(0, TCSADRAIN, &t->original_attrs);

    rebRelease(t->buffer);
    free(t);

    rebRelease(Line_History);
    Line_History = nullptr;

    Term_Initialized = false;
}


//
//  Read_Bytes_Interrupted: C
//
// Read the next "chunk" of data into the terminal buffer.  If it gets
// interrupted then return true, else false.
//
// Note that The read of bytes might end up getting only part of an encoded
// UTF-8 character.  But it's known how many bytes are expected from the
// leading byte.
//
// Escape sequences could also *theoretically* be split, and they have no
// standard for telling how long the sequence could be.  (ESC '\0') could be a
// plain escape key--or it could be an unfinished read of a longer sequence.
// We assume this won't happen, because the escape sequences being entered
// usually happen one at a time (cursor up, cursor down).  Unlike text, these
// are not *likely* to be pasted in a batch that could overflow READ_BUF_LEN
// and be split up.
//
static bool Read_Bytes_Interrupted(STD_TERM *t)
{
    assert(*t->cp == '\0');  // Don't read more bytes if buffer not exhausted

    int len = read(STDIN_FILENO, t->buf, READ_BUF_LEN - 1);  // -1, room for \0
    if (len < 0) {
        if (errno == EINTR) {
            //
            // If we don't clear the error, then every successive read() will
            // get EINTR.  Hope caller does the handling of true return result.
            //
            clearerr(stdin);

            t->cp = t->buf;
            t->buf[0] = '\0';

            return true;  // Ctrl-C or similar, see sigaction()/SIGINT
        }

        rebFail_OS (errno);
    }

    t->buf[len] = '\0';
    t->cp = t->buf;

    return false;  // not interrupted (note we could return `len` if needed)
}


//
//  Write_Char: C
//
// Write out repeated number of chars.
//
void Write_Char(unsigned char c, int n)
{
    for (; n > 0; n--)
        WRITE_UTF8(&c, 1);
}


//
//  Term_Clear_To_End: C
//
// Clear all the chars from the current position to the end.
// Reset cursor to current position.
//
void Term_Clear_To_End(STD_TERM *t)
{
    int num_codepoints_to_end = Term_Remain(t);
    rebElide("clear skip", t->buffer, rebI(t->pos));

    Write_Char(' ', num_codepoints_to_end);  // wipe to end of line...
    Write_Char(BS, num_codepoints_to_end);  // ...then return to position
}


//
//  Term_Seek: C
//
// Reset cursor to home position.
//
void Term_Seek(STD_TERM *t, unsigned int pos)
{
    int delta = (pos < t->pos) ? -1 : 1;
    while (pos != t->pos)
        Move_Cursor(t, delta);
}


//
//  Show_Line: C
//
// Refresh a line from the current position to the end.
// Extra blanks can be specified to erase chars off end.
// If blanks is negative, stay at end of line.
// Reset the cursor back to current position.
//
static void Show_Line(STD_TERM *t, int blanks)
{
    // Clip bounds
    //
    unsigned int end = Term_End(t);
    if (t->pos > end)
        t->pos = end;

    if (blanks >= 0) {
        size_t num_bytes;
        unsigned char *bytes = rebBytes(&num_bytes,
            "skip", t->buffer, rebI(t->pos)
        );

        WRITE_UTF8(bytes, num_bytes);
        rebFree(bytes);
    }
    else {
        size_t num_bytes;
        unsigned char *bytes = rebBytes(&num_bytes,
            t->buffer
        );

        WRITE_UTF8(bytes, num_bytes);
        rebFree(bytes);

        blanks = -blanks;
    }

    Write_Char(' ', blanks);
    Write_Char(BS,  blanks);  // return to original position or end

    // We want to write as many backspace characters as there are *codepoints*
    // in the buffer to end of line.
    //
    Write_Char(BS, Term_Remain(t));
}


//
//  Delete_Char: C
//
// Delete a char at the current position. Adjust end position.
// Redisplay the line. Blank out extra char at end.
//
void Delete_Char(STD_TERM *t, bool back)
{
    unsigned int end = Term_End(t);

    if (t->pos == end and not back)
        return;  // Ctrl-D (forward-delete) at end of line

    if (t->pos == 0 and back)
        return;  // backspace at beginning of line

    if (back)
        --t->pos;

    if (end > 0) {
        rebElide("remove skip", t->buffer, rebI(t->pos));

        if (back)
            Write_Char(BS, 1);

        Show_Line(t, 1);
    }
    else
        t->pos = 0;
}


//
//  Move_Cursor: C
//
// Move cursor right or left by one char.
//
void Move_Cursor(STD_TERM *t, int count)
{
    if (count < 0) {
        //
        // "backspace" in TERMIOS lets you move the cursor left without
        //  knowing what character is there and without overwriting it.
        //
        if (t->pos > 0) {
            --t->pos;
            Write_Char(BS, 1);
        }
    }
    else {
        // Moving right without affecting a character requires writing the
        // character you know to be already there (via the buffer).
        //
        unsigned int end = Term_End(t);
        if (t->pos < end) {
            size_t encoded_size;
            unsigned char *encoded_char = rebBytes(&encoded_size,
                "to binary! pick", t->buffer, rebI(t->pos + 1)
            );
            WRITE_UTF8(encoded_char, encoded_size);
            rebFree(encoded_char);

            t->pos += 1;
        }
    }
}


// When an unrecognized key is hit, people may want to know that at least the
// keypress was received.  Or not.  For now just give a message in the debug
// build.
//
// !!! In the future, this might do something more interesting to get the
// BINARY! information for the key sequence back up out of the terminal, so
// that people could see what the key registered as on their machine and
// configure their console to respond to it.
//
// !!! Given the way the code works, escape sequences should be able to span
// buffer reads, and the current method of passing in subtracted codepoint
// addresses wouldn't work since `cp` can change on spanned reads.  This
// should probably be addressed rigorously if one wanted to actually do
// something with `delta`, but code is preserved as it was for annotation.
//
REBVAL *Unrecognized_Key_Sequence(STD_TERM *t, int delta)
{
    assert(delta <= 0);
    UNUSED(delta);

    // We don't really know how long an incomprehensible escape sequence is.
    // For now, just drop all the data, pending better heuristics or ideas.
    //
    Term_Abandon_Pending_Events(t);

    return rebValue("as issue! {[KEY?]}");
}


//
//  Try_Get_One_Console_Event: C
//
REBVAL *Try_Get_One_Console_Event(STD_TERM *t, bool buffered)
{
    REBVAL *e = nullptr;  // *unbuffered* event to return
    REBVAL *e_buffered = nullptr;  // buffered event

    if (t->e_pending) {
        e = t->e_pending;
        t->e_pending = nullptr;
        return e;
    }

  start_over:
    assert(not e and not t->e_pending);
    assert(
        not e_buffered
        or (buffered and rebDid("text?", e_buffered))
    );

    // See notes on why Read_Bytes_Interrupted() can wind up splitting UTF-8
    // encodings (which can happen with pastes of text).
    //
    // Also see notes there on why escape sequences are anticipated to come
    // in one at a time, and there's no good way of handling unrecognized
    // sequences.
    //
    if (*t->cp == '\0') {  // no residual bytes from a previous read pending
        if (e_buffered)
            return e_buffered;  // pass anything we gathered so far first

        if (Read_Bytes_Interrupted(t))
            return rebValue("'~halt~");

        assert(*t->cp != '\0');
    }

    if (
        (*t->cp >= 32 and *t->cp < 127)  // 32 is space, 127 is DEL(ete)
        or *t->cp > 127  // high-bit set UTF-8 start byte
    ){
    //=//// ASCII printable character or UTF-8 ////////////////////////////=//
        //
        // https://en.wikipedia.org/wiki/ASCII
        // https://en.wikipedia.org/wiki/UTF-8
        //
        // A UTF-8 character may span multiple bytes...and if the buffer end
        // was reached on a partial read() of a UTF-8 character, we may need
        // to do more reading to get the missing bytes here.

        char encoded[UNI_ENCODED_MAX];
        int encoded_size = 1 + rebUnboxInteger(
            "trailing-bytes-for-utf8",
                rebR(rebInteger(cast(unsigned char, *t->cp)))
        );
        assert(encoded_size <= UNI_ENCODED_MAX);

        // `cp` can jump back to the beginning of the buffer on each read.
        // So build up an encoded UTF-8 character as continuous bytes so
        // it can be inserted into a Rebol string atomically.
        //
        int i;
        for (i = 0; i < encoded_size; ++i) {
            if (*t->cp == '\0') {
                //
                // Premature end, the UTF-8 data must have gotten split on
                // a buffer boundary.  Refill the buffer with another read,
                // where the remaining UTF-8 characters *should* be found.
                // (This should not block.)
                //
                if (Read_Bytes_Interrupted(t))
                    return rebValue("'~halt~");
            }
            assert(*t->cp != '\0');
            encoded[i] = *t->cp;
            ++t->cp;
        }

        REBVAL *char_bin = rebSizedBinary(encoded, encoded_size);
        if (not buffered) {
            e = rebValue("make issue!", char_bin);
        }
        else {
            if (e_buffered)
                rebElide("append", e_buffered, char_bin);
            else
                e_buffered = rebValue("as text!", char_bin);
        }
        rebRelease(char_bin);
    }
    else if (*t->cp == ESC and t->cp[1] == '\0') {

    //=//// Plain Escape //////////////////////////////////////////////////=//

        ++t->cp;  // consume from buffer
        e = xrebWord("escape");
    }
    else if (*t->cp == ESC and t->cp[1] == '[') {

    //=//// CSI Escape Sequences, VT100/VT220 Escape Sequences, etc. //////=//
        //
        // https://en.wikipedia.org/wiki/ANSI_escape_code#CSI_sequences
        // http://ascii-table.com/ansi-escape-sequences-vt-100.php
        // http://aperiodic.net/phil/archives/Geekery/term-function-keys.html
        //
        // While these are similar in beginning with ESC and '[', the
        // actual codes vary.  HOME in CSI would be (ESC '[' '1' '~').
        // But to HOME in VT100, it can be as simple as (ESC '[' 'H'),
        // although there can be numbers between the '[' and 'H'.
        //
        // There's not much in the way of "rules" governing the format of
        // sequences, though official CSI codes always fit this pattern
        // with the following sequence:
        //
        //    the ESC then the '[' ("the CSI")
        //    one of `0-9:;<=>?` ("parameter byte")
        //    any number of `!"# $%&'()*+,-./` ("intermediate bytes")
        //    one of `@A-Z[\]^_`a-z{|}~` ("final byte")
        //
        // But some codes might look like CSI codes while not actually
        // fitting that rule.  e.g. the F8 function key on my machine
        // generates (ESC '[' '1' '9' '~'), which is a VT220 code
        // conflicting with the CSI interpretation of HOME above.
        //
        // Note: This kind of conflict confuses "linenoise", leading F8 to
        // jump to the beginning of line and display a tilde:
        //
        // https://github.com/antirez/linenoise

        t->cp += 2;  // skip ESC and '['

        char first = *t->cp;
        ++t->cp;
        switch (first) {
          case 'A':  // up arrow (VT100)
            e = xrebWord("up");
            break;

          case 'B':  // down arrow (VT100)
            e = xrebWord("down");
            break;

          case 'D':  // left arrow (VT100)
            e = xrebWord("left");
            break;

          case 'C':  // right arrow (VT100)
            e = xrebWord("right");
            break;

          case '1':  // home (CSI) or higher function keys (VT220)
            if (t->cp[1] == '~') {
                ++t->cp;
                e = xrebWord("home");
            }
            else
                e = Unrecognized_Key_Sequence(t, -3);
            break;

          case '4': // end (CSI)
            if (t->cp[1] == '~') {
                ++t->cp;
                e = xrebWord("end");
            }
            else
                e = Unrecognized_Key_Sequence(t, -3);
            break;

          case '3':  // delete (CSI)
            if (t->cp[1] == '~') {
                ++t->cp;
                e = xrebWord("delete");
            }
            else
                e = Unrecognized_Key_Sequence(t, -2);
            break;

          case 'H':  // home (VT100)
            e = xrebWord("home");
            break;

          case 'F':  // end !!! (in what standard?)
            e = xrebWord("end");
            break;

          case 'J':  // erase to end of screen (VT100)
            e = xrebWord("clear");
            break;

          default:
            e = Unrecognized_Key_Sequence(t, -2);
            break;
        }
    }
    else if (*t->cp == ESC) {

    //=//// non-CSI Escape Sequences //////////////////////////////////////=//
        //
        // http://ascii-table.com/ansi-escape-sequences-vt-100.php

        ++t->cp;  // skip ESC

        char first = *t->cp;
        ++t->cp;
        switch (first) {
          case 'H':   // !!! "home" (in what standard??)
          #if !defined(NDEBUG)
            rebJumps("fail {ESC H: please report your system info}");
          #else
            e = xrebWord("home");
          #endif
            break;

          case 'F':  // !!! "end" (in what standard??)
          #if !defined(NDEBUG)
            rebJumps("fail {ESC F: please report your system info}");
          #else
            e = xrebWord("end");
          #endif
            break;

          case '\0':
            assert(false);  // plain escape handled earlier for clarity
            e = xrebWord("escape");
            break;

          default:
            e = Unrecognized_Key_Sequence(t, -3);
            break;
        }
    }
    else {

    //=//// C0 Control Codes and Bash-inspired Shortcuts //////////////////=//
        //
        // https://en.wikipedia.org/wiki/C0_and_C1_control_codes
        // https://ss64.com/bash/syntax-keyboard.html

        char first = *t->cp;
        ++t->cp;
        if (first == 3)  {  // CTRL-C, Interrupt (ANSI, <signal.h> is C89)
            //
            // It's theoretically possible to clear the termios `c_lflag` ISIG
            // in order to receive literal Ctrl-C, but we don't want to get
            // involved at that level.  Using sigaction() on SIGINT and
            // causing EINTR is how we would like to be triggering HALT.
            //
            rebJumps("fail {Unexpected literal Ctrl-C in console}");
        }
        else switch (first) {
          case DEL:  // delete (C0)
            //
            // From Wikipedia:
            // "On modern systems, terminal emulators typically turn keys
            // marked "Delete" or "Del" into an escape sequence such as
            // ^[[3~. Terminal emulators may produce DEL when backspace
            // is pressed."
            //
            // We assume "modern" interpretation of DEL as backspace synonym.
            //
            goto backspace;

          case '\t':  // tab (C0)
            e = xrebWord("tab");
            break;

          case BS:  // backspace (C0)
          backspace:
            e = xrebWord("backspace");
            break;

          case CR:  // carriage return (C0)
            if (*t->cp == '\n')
                ++t->cp;  // disregard the CR character, else treat as LF
            goto line_feed;

          line_feed:;
          case LF:  // line feed (C0)
            e = rebChar('\n');  // default case would do it, but be clear
            break;

          default:
            if (first >= 1 and first <= 26) {  // Ctrl-A, Ctrl-B, etc.
                e = rebValue(
                    "as word! unspaced [",
                        "{ctrl-}", rebR(rebChar(first - 1 + 'a')),
                    "]"
                );
            }
            else
                e = Unrecognized_Key_Sequence(t, -1);
        }
    }

    if (e != nullptr) {  // a non-buffered event was produced
        if (e_buffered) {  // but we have pending buffered text...
            t->e_pending = e;  // ...so make the non-buffered event pending
            return e_buffered;  // and return the buffer first
        }

        return e;  // if no buffer in waiting, return non-buffered event
    }

    goto start_over;
}


//
//  Term_Abandon_Pending_Events: C
//
void Term_Abandon_Pending_Events(STD_TERM *t)
{
    t->buf[0] = '\0';
    t->cp = t->buf;
}


//
//  Term_Insert_Char: C
//
static void Term_Insert_Char(STD_TERM *t, uint32_t c)
{
    if (c == BS) {
        if (t->pos > 0) {
            rebElide("remove skip", t->buffer, rebI(t->pos));
            --t->pos;
            Write_Char(BS, 1);
        }
    }
    else if (c == LF) {
        //
        // !!! Currently, if a newline actually makes it into the terminal
        // by asking to put it there, you see a newline visually, but the
        // buffer content is lost.  You can't then backspace over it.  So
        // perhaps obviously, the terminal handling code when it gets a
        // LF *key* as input needs to copy the buffer content out before it
        // decides to ask for the LF to be output visually.
        //
        rebElide("clear", t->buffer);
        t->pos = 0;
        Write_Char(LF, 1);
    }
    else {
        REBVAL *codepoint = rebChar(c);

        size_t encoded_size;
        unsigned char *encoded = rebBytes(&encoded_size,
            "insert skip", t->buffer, rebI(t->pos), codepoint,
            codepoint  // fold returning of codepoint in with insertion
        );
        WRITE_UTF8(encoded, encoded_size);
        rebFree(encoded);

        rebRelease(codepoint);

        ++t->pos;
    }
}


//
//  Term_Insert: C
//
// Inserts a Rebol value (TEXT!, CHAR!) at the current cursor position.
// This is made complicated because we have to sync our internal knowledge
// with what the last line in the terminal is showing...which means mirroring
// its logic regarding cursor position, newlines, backspacing.
//
void Term_Insert(STD_TERM *t, const REBVAL *v) {
    if (rebDid("char?", v)) {
        Term_Insert_Char(t, rebUnboxChar(v));
        return;
    }

    int len = rebUnboxInteger("length of", v);

    if (rebDid("find", v, "backspace")) {
        //
        // !!! The logic for backspace and how it interacts is nit-picky,
        // and "reaches out" to possibly edit the existing buffer.  There's
        // no particularly easy way to handle this, so for now just go
        // through a slow character-by-character paste.  Assume this is rare.
        //
        int i;
        for (i = 1; i <= len; ++i)
            Term_Insert_Char(t, rebUnboxChar("pick", v, rebI(i)));
    }
    else {  // Finesse by doing one big write
        //
        // Systems may handle tabs differently, but we want our buffer to
        // have the right number of spaces accounted for.  Just transform.
        //
        REBVAL *v_no_tab = rebValue(
            "if find", v, "tab [",
                "replace/all copy", v, "tab", "{    }"
            "]"
        );

        size_t encoded_size;
        unsigned char *encoded = rebBytes(&encoded_size,
            v_no_tab ? v_no_tab : v
        );

        rebRelease(v_no_tab);  // null-tolerant

        // Go ahead with the OS-level write, in case it can do some processing
        // of that asynchronously in parallel with the following Rebol code.
        //
        WRITE_UTF8(encoded, encoded_size);
        rebFree(encoded);

        REBVAL *v_last_line = rebValue("next try find-last", v, "newline");

        // If there were any newlines, then whatever is in the current line
        // buffer will no longer be there.
        //
        if (v_last_line) {
            rebElide("clear", t->buffer);
            t->pos = 0;
        }

        const REBVAL *insertion = v_last_line ? v_last_line : v;

        t->pos += rebUnboxInteger(
            "insert skip", t->buffer, rebI(t->pos), insertion,
            "length of", insertion
        );

        rebRelease(v_last_line);  // null-tolerant
    }

    Show_Line(t, 0);
}


//
//  Term_Beep: C
//
// Trigger some beep or alert sound.
//
void Term_Beep(STD_TERM *t)
{
    UNUSED(t);
    Write_Char(BEL, 1);
}

#endif  // end smart console branch
