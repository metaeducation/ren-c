//
//  File: %host-readline.c
//  Summary: "Simple readline() line input handler"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2020 Rebol Open Source Contributors
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
// Processes special keys for input line editing and recall.
//
// Avoids use of complex OS libraries and GNU readline() but hardcodes some
// parts only for the common standard.
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h> //for read and write
#include <errno.h>

#ifndef NO_TTY_ATTRIBUTES
    #include <termios.h>
#endif


//=//// REBOL INCLUDES + HELPERS //////////////////////////////////////////=//

#include <assert.h>
#include <stdint.h>
#include "reb-c.h"

#include "readline.h"

#define xrebWord(cstr) \
    rebValue("'" cstr, rebEND)  // C string literal should merge apostrophe


//=//// CONFIGURATION /////////////////////////////////////////////////////=//

enum {
    BEL = 7,
    BS = 8,
    LF = 10,
    CR = 13,
    ESC = 27,
    DEL = 127
};


#define WRITE_CHAR(s) \
    do { \
        if (write(1, s, 1) == -1) { \
            /* Error here, or better to "just try to keep going"? */ \
        } \
    } while (0)

#define WRITE_UTF8(s,n) \
    do { \
        if (write(1, s, n) == -1) { \
            /* Error here, or better to "just try to keep going"? */ \
        } \
    } while (0)

inline static void WRITE_STR(const char *s) {
    do {
        if (write(1, s, strlen(s)) == -1) {
            /* Error here, or better to "just try to keep going"? */
        }
    } while (0);
}


struct Reb_Terminal_Struct {
    REBVAL *buffer;  // a TEXT! used as a buffer
    unsigned int pos;  // cursor position within the line

    unsigned char buf[READ_BUF_LEN];  // '\0' terminated, needs -1 on read()
    const unsigned char *cp;
};


//=//// GLOBALS ///////////////////////////////////////////////////////////=//

static bool Term_Initialized = false;  // Terminal init was successful

#ifndef NO_TTY_ATTRIBUTES
    static struct termios Term_Attrs;  // Initial settings, restored on exit
#endif


inline static unsigned int Term_End(STD_TERM *t)
  { return rebUnboxInteger("length of", t->buffer, rebEND); }

inline static unsigned int Term_Remain(STD_TERM *t)
  { return Term_End(t) - t->pos; }


//
//  Init_Terminal: C
//
// If possible, change the terminal to "raw" mode (where characters are
// received one at a time, as opposed to "cooked" mode where a whole line is
// read at once.)
//
STD_TERM *Init_Terminal(void)
{
  #ifndef NO_TTY_ATTRIBUTES
    //
    // Good reference on termios:
    //
    // https://blog.nelhage.com/2009/12/a-brief-introduction-to-termios/
    // https://blog.nelhage.com/2009/12/a-brief-introduction-to-termios-termios3-and-stty/
    // https://blog.nelhage.com/2010/01/a-brief-introduction-to-termios-signaling-and-job-control/
    //
    struct termios attrs;

    if (Term_Initialized || tcgetattr(0, &Term_Attrs)) return NULL;

    attrs = Term_Attrs;

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

    // Special modes.
    //
    attrs.c_cc[VMIN] = 1;   // min num of bytes for READ to return
    attrs.c_cc[VTIME] = 0;  // how long to wait for input

    tcsetattr(0, TCSADRAIN, &attrs);
  #endif

    // !!! Ultimately, we want to be able to recover line history from a
    // file across sessions.  It makes more sense for the logic doing that
    // to be doing it in Rebol.  For starters, we just make it fresh.
    //
    Line_History = rebValue("[{}]", rebEND);  // current line is empty string
    rebUnmanage(Line_History);  // allow Line_History to live indefinitely

    STD_TERM *t = cast(STD_TERM*, malloc(sizeof(STD_TERM)));
    t->buffer = rebValue("{}", rebEND);
    rebUnmanage(t->buffer);

    t->buf[0] = '\0';  // start read() byte buffer out at empty
    t->cp = t->buf;
    t->pos = 0;

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
    return rebValue("const", t->buffer, rebEND);
}


//
//  Quit_Terminal: C
//
// Restore the terminal modes original entry settings,
// in preparation for exit from program.
//
void Quit_Terminal(STD_TERM *t)
{
    if (Term_Initialized) {
      #ifndef NO_TTY_ATTRIBUTES
        tcsetattr(0, TCSADRAIN, &Term_Attrs);
      #endif

        rebRelease(t->buffer);
        free(t);

        rebRelease(Line_History);
        Line_History = nullptr;
    }

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

    int len = read(0, t->buf, READ_BUF_LEN - 1);  // save space for '\0'
    if (len < 0) {
        if (errno == EINTR)
            return true;  // Ctrl-C or similar, see sigaction()/SIGINT

        WRITE_STR("\nI/O terminated\n");
        Quit_Terminal(t);  // something went wrong
        exit(100);
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
    unsigned char buf[4];

    buf[0] = c;
    for (; n > 0; n--)
        WRITE_CHAR(buf);
}


//
//  Clear_Line_To_End: C
//
// Clear all the chars from the current position to the end.
// Reset cursor to current position.
//
void Clear_Line_To_End(STD_TERM *t)
{
    int num_codepoints_to_end = Term_Remain(t);
    rebElide(
        "clear skip", t->buffer, rebI(t->pos),
    rebEND);

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
            "skip", t->buffer, rebI(t->pos),
        rebEND);

        WRITE_UTF8(bytes, num_bytes);
        rebFree(bytes);
    }
    else {
        size_t num_bytes;
        unsigned char *bytes = rebBytes(&num_bytes,
            t->buffer,
        rebEND);

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
        rebElide(
            "remove skip", t->buffer, rebI(t->pos),
        rebEND);

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
                "to binary! pick", t->buffer, rebI(t->pos + 1),
            rebEND);
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
    t->buf[0] = '\0';
    t->cp = t->buf;

    return rebText("[KEY?]");
}


//
//  Try_Get_One_Console_Event: C
//
// This attempts to get one unit of "event" from the console.  It does not
// use the Rebol EVENT! datatype at this time.  Instead it returns:
//
//    CHAR! => a printable character
//    WORD! => keystroke or control code
//    TEXT! => printable characters to insert
//    VOID! => interrupted by HALT or Ctrl-C
//
// It does not do any printing or handling while fetching the event.
//
// The reason it returns accrued TEXT! in runs (vs. always returning each
// character individually) is because of pasting.  Taking the read() buffer
// in per-line chunks is much faster than trying to process each character
// insertion with its own code (it's noticeably slow).  But at typing speed
// it's fine.
//
// Note Ctrl-C comes from the SIGINT signal and not from the physical detection
// of the key combination "Ctrl + C", which this routine should not receive
// due to deferring to the default UNIX behavior for that (otherwise, scripts
// could not be cancelled unless they were waiting at an input prompt).
//
// !!! The idea is that if there is no event available, this routine will
// return a nullptr.  That would allow some way of exiting the read() to
// do another operation (process network requests for a real-time chat, etc.)
// This is at the concept stage at the moment.
//
REBVAL *Try_Get_One_Console_Event(STD_TERM *t, bool buffered)
{
    REBVAL *e = nullptr;  // the event to return

    // See notes on why Read_Bytes_Interrupted() can wind up splitting UTF-8
    // encodings (which can happen with pastes of text).
    //
    // Also see notes there on why escape sequences are anticipated to come
    // in one at a time, and there's no good way of handling unrecognized
    // sequences.
    //
    if (*t->cp == '\0') {  // no residual bytes from a previous read pending
        if (Read_Bytes_Interrupted(t))
            return rebVoid();  // signal a HALT

        assert(*t->cp != '\0');
    }

    char encoded[READ_BUF_LEN];
    size_t encoded_size = 0;

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

      printable_char:;

        int size = 1 + rebUnboxInteger(
            "trailing-bytes-for-utf8",
                rebR(rebInteger(cast(unsigned char, *t->cp))),
        rebEND);
        assert(size <= 4);

        if (encoded_size + size > READ_BUF_LEN) {
            assert(not buffered);  // how did the buffer get so big?
            e = rebSizedText(encoded, encoded_size);
        }
        else {
            // `cp` can jump back to the beginning of the buffer on each read.
            // So build up an encoded UTF-8 character as continuous bytes so
            // it can be inserted into a Rebol string atomically.
            //
            int i;
            for (i = 0; i < size; ++i) {
                if (*t->cp == '\0') {
                    //
                    // Premature end, the UTF-8 data must have gotten split on
                    // a buffer boundary.  Refill the buffer with another read,
                    // where the remaining UTF-8 characters *should* be found.
                    // (This should not block.)
                    //
                    if (Read_Bytes_Interrupted(t))
                        return nullptr;  // signal a HALT
                }
                assert(*t->cp != '\0');
                encoded[encoded_size] = *t->cp;
                ++encoded_size;
                ++t->cp;
            }

            if (not buffered) {
                assert(encoded_size == 0);
                e = rebValue(
                    "to char!", rebR(rebSizedBinary(encoded, encoded_size)),
                rebEND);
            }
            else {
                if (
                    (*t->cp >= 32 and *t->cp < 127)  // 32 is space, 127 DEL
                    or *t->cp > 127  // high-bit set UTF-8 start byte
                ){
                    goto printable_char;
                }
                e = rebSizedText(encoded, encoded_size);
            }
        }
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

        switch (*t->cp) {
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
            if (t->cp[1] != '~')
                return Unrecognized_Key_Sequence(t, -2);

            e = xrebWord("home");
            ++t->cp;  // remove 1, the ~ is consumed after the switch
            break;

          case '4': // end (CSI)
            if (t->cp[1] != '~')
                return Unrecognized_Key_Sequence(t, -2);

            e = xrebWord("end");
            ++t->cp;  // remove 4, the ~ is consumed after the switch
            break;

          case '3':  // delete (CSI)
            if (t->cp[1] != '~')
                return Unrecognized_Key_Sequence(t, -2);

            e = xrebWord("delete");
            ++t->cp;  // remove 3, the ~ is consumed after the switch
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
            return Unrecognized_Key_Sequence(t, -2);
        }

        ++t->cp;
    }
    else if (*t->cp == ESC) {

    //=//// non-CSI Escape Sequences //////////////////////////////////////=//
        //
        // http://ascii-table.com/ansi-escape-sequences-vt-100.php

        ++t->cp;

        switch (*t->cp) {
          case 'H':   // !!! "home" (in what standard??)
          #if !defined(NDEBUG)
            rebJumps(
                "FAIL {ESC H: please report your system info}",
            rebEND);
          #endif
            e = xrebWord("home");
            break;

          case 'F':  // !!! "end" (in what standard??)
          #if !defined(NDEBUG)
            rebJumps(
                "FAIL {ESC F: please report your system info}",
            rebEND);
          #endif
            e = xrebWord("end");
            break;

          case '\0':
            assert(false);  // plain escape handled earlier for clarity
            e = xrebWord("escape");
            break;

          default:
            return Unrecognized_Key_Sequence(t, -2);
        }

        ++t->cp;
    }
    else {

    //=//// C0 Control Codes and Bash-inspired Shortcuts //////////////////=//
        //
        // https://en.wikipedia.org/wiki/C0_and_C1_control_codes
        // https://ss64.com/bash/syntax-keyboard.html
    
        if (*t->cp == 3)  {  // CTRL-C, Interrupt (ANSI, <signal.h> is C89)
            //
            // It's theoretically possible to clear the termios `c_lflag` ISIG
            // in order to receive literal Ctrl-C, but we don't want to get
            // involved at that level.  Using sigaction() on SIGINT and
            // causing EINTR is how we would like to be triggering HALT.
            //
            rebJumps(
                "FAIL {Unexpected literal Ctrl-C in console}",
            rebEND);
        }
        else switch (*t->cp) {
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

          case BS:  // backspace (C0)
          backspace:
            e = xrebWord("backspace");
            break;

          case CR:  // carriage return (C0)
            if (t->cp[1] == '\n')
                ++t->cp;  // disregard the CR character, else treat as LF
            goto line_feed;

          line_feed:;
          case LF:  // line feed (C0)
            e = rebChar('\n');  // default case would do it, but be clear
            break;

          default:
            if (*t->cp >= 1 and *t->cp <= 26) {  // Ctrl-A, Ctrl-B, etc.
                e = rebValue(
                    "as word! unspaced [",
                        "{ctrl-}", rebR(rebChar(*t->cp - 1 + 'a')),
                    "]",
                rebEND);
            }
            else
                return Unrecognized_Key_Sequence(t, 0);
        }
        ++t->cp;
    }

    assert(e != nullptr);
    return e;
}


//
//  Term_Insert_Char: C
//
static void Term_Insert_Char(STD_TERM *t, uint32_t c)
{
    if (c == BS) {
        if (t->pos > 0) {
            rebElide("remove skip", t->buffer, rebI(t->pos), rebEND);
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
        rebElide("clear", t->buffer, rebEND);
        t->pos = 0;
        Write_Char(LF, 1);
    }
    else {
        REBVAL *codepoint = rebChar(c);

        size_t encoded_size;
        unsigned char *encoded = rebBytes(&encoded_size,
            "insert skip", t->buffer, rebI(t->pos), codepoint,
            codepoint,  // fold returning of codepoint in with insertion
        rebEND);
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
    if (rebDid("char?", v, rebEND)) {
        Term_Insert_Char(t, rebUnboxChar(v, rebEND));
        return;
    }

    int len = rebUnboxInteger("length of", v, rebEND);

    if (rebDid("find", v, "backspace", rebEND)) {
        //
        // !!! The logic for backspace and how it interacts is nit-picky,
        // and "reaches out" to possibly edit the existing buffer.  There's
        // no particularly easy way to handle this, so for now just go
        // through a slow character-by-character paste.  Assume this is rare.
        //
        int i;
        for (i = 1; i <= len; ++i)
            Term_Insert_Char(t, rebUnboxChar("pick", v, rebI(i), rebEND));
    }
    else {  // Finesse by doing one big write
        //
        // Systems may handle tabs differently, but we want our buffer to
        // have the right number of spaces accounted for.  Just transform.
        //
        REBVAL *v_no_tab = rebValue(
            "if find", v, "tab [",
                "replace/all copy", v, "tab", "{    }"
            "]",
        rebEND);

        size_t encoded_size;
        unsigned char *encoded = rebBytes(&encoded_size,
            v_no_tab ? v_no_tab : v,
        rebEND);

        rebRelease(v_no_tab);  // null-tolerant

        // Go ahead with the OS-level write, in case it can do some processing
        // of that asynchronously in parallel with the following Rebol code.
        //
        WRITE_UTF8(encoded, encoded_size);
        rebFree(encoded);

        REBVAL *v_last_line = rebValue(
            "next try find-last", v, "newline",
        rebEND);

        // If there were any newlines, then whatever is in the current line
        // buffer will no longer be there.
        //
        if (v_last_line) {
            rebElide("clear", t->buffer, rebEND);
            t->pos = 0;
        }

        const REBVAL *insertion = v_last_line ? v_last_line : v;

        t->pos += rebUnboxInteger(
            "insert skip", t->buffer, rebI(t->pos), insertion,
            "length of", insertion,
        rebEND);

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
