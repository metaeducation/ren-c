//
//  File: %host-readline.c
//  Summary: "Simple readline() line input handler"
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

#include "reb-host.h"

// Configuration:
#define TERM_BUF_LEN 4096   // chars allowed per line
#define READ_BUF_LEN 64     // chars per read()
#define MAX_HISTORY  300    // number of lines stored


#define CHAR_LEN(c) (1 + trailingBytesForUTF8[c])

#define WRITE_CHAR(s) \
    do { \
        if (write(1, s, 1) == -1) { \
            /* Error here, or better to "just try to keep going"? */ \
        } \
    } while (0)

#define WRITE_CHARS(s,n) \
    do { \
        if (write(1, s, n) == -1) { \
            /* Error here, or better to "just try to keep going"? */ \
        } \
    } while (0)

#define WRITE_STR(s) \
    do { \
        if (write(1, s, strlen(s)) == -1) { \
            /* Error here, or better to "just try to keep going"? */ \
        } \
    } while (0)

//
// Stepping backwards in UTF8 just means to keep going back so long
// as you are looking at a byte with bit 7 set and bit 6 clear:
//
// https://stackoverflow.com/a/22257843/211160
//
#define STEP_BACKWARD(term) \
    do { \
        --term->pos; \
    } while ((term->buffer[term->pos] & 0xC0) == 0x80); 

typedef struct term_data {
    REBYTE *buffer;
    REBYTE *residue;
    REBYTE *out;
    int pos;
    int end;
    int hist;
} STD_TERM;

// Globals:
static REBOOL Term_Initialized = FALSE;     // Terminal init was successful
static REBYTE **Line_History;               // Prior input lines
static int Line_Count;                      // Number of prior lines

#ifndef NO_TTY_ATTRIBUTES
static struct termios Term_Attrs;   // Initial settings, restored on exit
#endif


extern STD_TERM *Init_Terminal(void);

//
//  Init_Terminal: C
//
// Change the terminal modes to those required for proper
// REBOL console handling. Return TRUE on success.
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

    // Local modes:
    attrs.c_lflag &= ~(ECHO | ICANON); // raw input

    // Input modes:
    attrs.c_iflag &= ~(ICRNL | INLCR); // leave CR an LF as is

    // Output modes:
    attrs.c_oflag |= ONLCR; // On output, emit CRLF

    // Special modes:
    attrs.c_cc[VMIN] = 1;   // min num of bytes for READ to return
    attrs.c_cc[VTIME] = 0;  // how long to wait for input

    tcsetattr(0, TCSADRAIN, &attrs);
#endif

    // Setup variables:
    Line_History = OS_ALLOC_N(REBYTE*, MAX_HISTORY + 2);

    // Make first line as an empty string
    Line_History[0] = OS_ALLOC_N(REBYTE, 1);
    Line_History[0][0] = '\0';
    Line_Count = 1;

    STD_TERM *term = OS_ALLOC_ZEROFILL(STD_TERM);
    term->buffer = OS_ALLOC_N(REBYTE, TERM_BUF_LEN);
    term->buffer[0] = 0;
    term->residue = OS_ALLOC_N(REBYTE, TERM_BUF_LEN);
    term->residue[0] = 0;

    Term_Initialized = TRUE;

    return term;
}


extern void Quit_Terminal(STD_TERM *term);

//
//  Quit_Terminal: C
//
// Restore the terminal modes original entry settings,
// in preparation for exit from program.
//
void Quit_Terminal(STD_TERM *term)
{
    int n;

    if (Term_Initialized) {
#ifndef NO_TTY_ATTRIBUTES
        tcsetattr(0, TCSADRAIN, &Term_Attrs);
#endif
        OS_FREE(term->residue);
        OS_FREE(term->buffer);
        OS_FREE(term);
        for (n = 0; n < Line_Count; n++) OS_FREE(Line_History[n]);
        OS_FREE(Line_History);
    }

    Term_Initialized = FALSE;
}


//
//  Read_Bytes_Interrupted: C
//
// Read the next "chunk" of data into the terminal buffer.  If it gets
// interrupted then return TRUE, else FALSE.
//
// Note that The read of bytes might end up getting only part of an encoded
// UTF-8 character.  But it's known how many bytes are expected from the
// leading byte.  Input_Char() can handle it by requesting the missing ones.
//
// Escape sequences could also *theoretically* be split, and they have no
// standard for telling how long the sequence could be.  (ESC '\0') could be a
// plain escape key--or it could be an unfinished read of a longer sequence.
// We assume this won't happen, because the escape sequences being entered
// usually happen one at a time (cursor up, cursor down).  Unlike text, these
// are not *likely* to be pasted in a batch that could overflow READ_BUF_LEN
// and be split up.
//
static REBOOL Read_Bytes_Interrupted(STD_TERM *term, REBYTE *buf, int len)
{
    // If we have leftovers:
    //
    if (term->residue[0]) {
        int end = LEN_BYTES(term->residue);
        if (end < len)
            len = end;
        strncpy(s_cast(buf), s_cast(term->residue), len); // terminated below
        memmove(term->residue, term->residue + len, end - len); // remove
        term->residue[end - len] = '\0';
    }
    else {
        if ((len = read(0, buf, len)) < 0) {
            if (errno == EINTR)
                return TRUE; // Ctrl-C or similar, see sigaction()/SIGINT

            WRITE_STR("\r\nI/O terminated\r\n");
            Quit_Terminal(term); // something went wrong
            exit(100);
        }
    }

    buf[len] = '\0';

    return FALSE; // not interrupted, note we could return `len` if needed
}


//
//  Write_Char: C
//
// Write out repeated number of chars.
// Unicode: not used
//
static void Write_Char(REBYTE c, int n)
{
    REBYTE buf[4];

    buf[0] = c;
    for (; n > 0; n--)
        WRITE_CHAR(buf);
}


//
//  Store_Line: C
//
// Makes a copy of the current buffer and store it in the
// history list. Returns the copied string.
//
static void Store_Line(STD_TERM *term)
{
    term->buffer[term->end] = 0;
    term->out = OS_ALLOC_N(REBYTE, term->end + 1);
    strcpy(s_cast(term->out), s_cast(term->buffer));

    // If max history, drop older lines (but not [0] empty line):
    if (Line_Count >= MAX_HISTORY) {
        OS_FREE(Line_History[1]);
        memmove(
            Line_History + 1,
            Line_History + 2,
            (MAX_HISTORY - 2) * sizeof(REBYTE*)
        );
        Line_Count = MAX_HISTORY - 1;
    }

    Line_History[Line_Count] = term->out;
    ++Line_Count;
}


//
//  Recall_Line: C
//
// Set the current buffer to the contents of the history
// list at its current position. Clip at the ends.
// Return the history line index number.
// Unicode: ok
//
static void Recall_Line(STD_TERM *term)
{
    if (term->hist < 0) term->hist = 0;

    if (term->hist == 0)
        Write_Char(BEL, 1); // bell

    if (term->hist >= Line_Count) {
        // Special case: no "next" line:
        term->hist = Line_Count;
        term->buffer[0] = '\0';
        term->pos = term->end = 0;
    }
    else {
        // Fetch prior line:
        strcpy(s_cast(term->buffer), s_cast(Line_History[term->hist]));
        term->pos = term->end = LEN_BYTES(term->buffer);
    }
}


//
//  Clear_Line: C
//
// Clear all the chars from the current position to the end.
// Reset cursor to current position.
// Unicode: not used
//
static void Clear_Line(STD_TERM *term)
{
    Write_Char(' ', term->end - term->pos); // wipe prior line
    Write_Char(BS, term->end - term->pos); // return to position
}


//
//  Home_Line: C
//
// Reset cursor to home position.
// Unicode: ok
//
static void Home_Line(STD_TERM *term)
{
    while (term->pos > 0) {
        STEP_BACKWARD(term);
        Write_Char(BS, 1);
    }
}


//
//  End_Line: C
//
// Move cursor to end position.
// Unicode: not used
//
static void End_Line(STD_TERM *term)
{
    int len = term->end - term->pos;

    if (len > 0) {
        WRITE_CHARS(term->buffer+term->pos, len);
        term->pos = term->end;
    }
}

//
//  Strlen_UTF8: C
//
//  Count the character length (not byte length) of a UTF-8 string.
//  !!! Used to calculate the correct number of BS to us in Show_Line().
//      Would stepping through the UTF-8 string be better?
//
static int Strlen_UTF8(REBYTE *buffer, int byte_count) 
{
    int char_count = 0;
    int i = 0;
        for(i = 0 ; i < byte_count ; i++) 
            if ((buffer[i] & 0xC0) != 0x80) 
                char_count++;

        return char_count;
}

//
//  Show_Line: C
//
// Refresh a line from the current position to the end.
// Extra blanks can be specified to erase chars off end.
// If blanks is negative, stay at end of line.
// Reset the cursor back to current position.
// Unicode: ok
//
static void Show_Line(STD_TERM *term, int blanks)
{
    int len;

    // Clip bounds:
    if (term->pos < 0) term->pos = 0;
    else if (term->pos > term->end) term->pos = term->end;

    if (blanks >= 0) {
        len = term->end - term->pos;
        WRITE_CHARS(term->buffer+term->pos, len);
    }
    else {
        WRITE_CHARS(term->buffer, term->end);
        blanks = -blanks;
        len = 0;
    }

    Write_Char(' ', blanks);

    // return to original position or end
    Write_Char(BS,  blanks);
    Write_Char(BS,  Strlen_UTF8(term->buffer+term->pos, len));
}


// Just by looking at the first byte of a UTF-8 character sequence, you can
// tell how many additional bytes it will require.
//
// !!! This table is already in Rebol Core.  Really whatever logic gets used
// should be shareable here, but it's just getting pasted in here as a
// "temporary" measure.
//
static const char trailingBytesForUTF8[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5
};


//
//  Insert_Char_Null_If_Interrupted: C
//
// * Insert a char at the current position.
// * Adjust end position.
// * Redisplay the line.
//
static const REBYTE *Insert_Char_Null_If_Interrupted(
    STD_TERM *term,
    REBYTE *buf,
    int limit,
    const REBYTE *cp
){
    int encoded_len = 1 + trailingBytesForUTF8[*cp];

    if (term->end < TERM_BUF_LEN - encoded_len) { // avoid buffer overrun

        if (term->pos < term->end) { // open space for it:
            memmove(
                term->buffer + term->pos + encoded_len, // dest pointer
                term->buffer + term->pos, // source pointer
                encoded_len + term->end - term->pos // length
            );
        }

        int i;
        for (i = 0; i < encoded_len; ++i) {
            if (*cp == '\0') {
                //
                // Premature end, the UTF-8 data must have gotten split on
                // a buffer boundary.  Refill the buffer with another read,
                // where the remaining UTF-8 characters *should* be found.
                //
                if (Read_Bytes_Interrupted(term, buf, limit - 1))
                    return NULL; // signal interruption

                cp = buf;
            }

            WRITE_CHAR(cp);
            term->buffer[term->pos] = *cp;
            term->end++;
            term->pos++;
            ++cp;
        }

        Show_Line(term, 0);
    }

    return cp;
}


//
//  Delete_Char: C
//
// Delete a char at the current position. Adjust end position.
// Redisplay the line. Blank out extra char at end.
// Unicode: ok
//
static void Delete_Char(STD_TERM *term, REBOOL back)
{
    if (term->pos == term->end && NOT(back))
        return; //Ctrl-D at EOL

    if (back) 
        STEP_BACKWARD(term);

    int encoded_len = 1 + trailingBytesForUTF8[term->buffer[term->pos]];
    int len = encoded_len + term->end - term->pos;

    if (term->pos >= 0 && len > 0) {
        memmove(
            term->buffer + term->pos,
            term->buffer + term->pos + encoded_len,
            len
        );
        if (back)
            Write_Char(BS, 1);

        term->end -= encoded_len;
        Show_Line(term, 1);
    }
    else
        term->pos = 0;
}


//
//  Move_Cursor: C
//
// Move cursor right or left by one char.
// Unicode: not yet supported!
//
static void Move_Cursor(STD_TERM *term, int count)
{
    if (count < 0) {
        if (term->pos > 0) {
            STEP_BACKWARD(term);
            Write_Char(BS, 1);
        }
    }
    else {
        if (term->pos < term->end) {
            int encoded_len = CHAR_LEN(term->buffer[term->pos]);
            WRITE_CHARS(term->buffer + term->pos, encoded_len);
            term->pos += encoded_len;
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
inline static void Unrecognized_Key_Sequence(const REBYTE* cp)
{
    UNUSED(cp);

#if !defined(NDEBUG)
    WRITE_STR("[KEY?]");
#endif
}

extern int Read_Line(STD_TERM *term, REBYTE *result, int limit);

//
//  Read_Line: C
//
// Read a line (as a sequence of bytes) from the terminal.  Handles line
// editing and line history recall.
//
// !!! The line history is slated to be moved into userspace HOST-CONSOLE
// code, storing STRING!s in Rebol BLOCK!s.  Do not invest heavily in
// editing of that part of this code.
//
// Returns number of bytes in line.  If a plain ESC is pressed, then the
// result will be 0.  All successful results have at least a line feed, which
// will be returned as part of the data.
//
int Read_Line(STD_TERM *term, REBYTE *result, int limit)
{
    term->pos = 0;
    term->end = 0;
    term->hist = Line_Count;
    term->out = 0;
    term->buffer[0] = 0;

restart:;
    //
    // See notes on why Read_Bytes_Interrupted() can wind up splitting UTF-8
    // encodings (which can happen with pastes of text), and this is handled
    // by Insert_Char_Null_If_Interrupted().
    //
    // Also see notes there on why escape sequences are anticipated to come
    // in one at a time.  Hence unrecognized escape sequences jump up here to
    // `restart`.  Thus, hitting an unknown escape sequence and a character
    // very fast after it may discard that character.
    //
    REBYTE buf[READ_BUF_LEN]; // will always be '\0' terminated, hence `- 1`
    const REBYTE *cp = buf;

    if (Read_Bytes_Interrupted(term, buf, READ_BUF_LEN - 1))
        goto return_halt;

    while (*cp != '\0') {
        if (
            (*cp >= 32 && *cp < 127) // 32 is space, 127 is DEL(ete)
            || *cp > 127 // high-bit set UTF-8 start byte
        ){
            //
            // ASCII printable character or UTF-8
            //
            // https://en.wikipedia.org/wiki/ASCII
            // https://en.wikipedia.org/wiki/UTF-8
            //
            // Inserting a character may consume multiple bytes...and if the
            // buffer end was reached on a partial input of a UTF-8 character,
            // it may need to do its own read in order to get the missing
            // bytes and reset the buffer pointer.  So it can adjust cp even
            // backwards, if such a read is done.
            //
            cp = Insert_Char_Null_If_Interrupted(term, buf, READ_BUF_LEN, cp);
            if (cp == NULL)
                goto return_halt;

            continue;
        }

        if (*cp == ESC && cp[1] == '\0') {
            //
            // Plain Escape - Cancel Current Input (...not Halt Script)
            //
            // There are two distinct ways we want INPUT to be canceled.  One
            // is in a way that a script can detect, and continue running:
            //
            //    print "Enter filename (ESC to return to main menu):"
            //    if blank? filename: input [
            //       return 'go-to-main-menu
            //    ]
            //
            // The other kind of aborting would stop the script from running
            // further entirely...and either return to the REPL or exit the
            // Rebol process.  By near-universal convention in programming,
            // this is Ctrl-C (SIGINT - Signal Interrupt).  ESC seems like
            // a reasonable choice for the other.
            //
            // The way the notice of abort is wound up to the INPUT command
            // through the (deprecated) R3-Alpha "OS Host" is by a convention
            // that aborted lines will be an escape char and a terminator.
            //
            // The convention here seems to be that there's a terminator after
            // the length returned.
            //
            goto return_blank;

            // !!! See notes in the Windows Terminal usage of ReadConsole()
            // about how ESC cannot be overridden when using ENABLE_LINE_INPUT
            // to do anything other than clear the line.  Ctrl-D is used
            // there instead.
        }

        if (*cp == ESC && cp[1] == '[') {
            //
            // CSI Escape Sequences, VT100/VT220 Escape Sequences, etc:
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

            cp += 2; // skip ESC and '['

            switch (*cp) {

            case 'A': // up arrow (VT100)
                term->hist -= 2;
                /* fallthrough */
            case 'B': { // down arrow (VT100)
                int len = term->end;

                ++term->hist;

                Home_Line(term);
                Recall_Line(term);

                if (len <= term->end)
                    len = 0;
                else
                    len = term->end - len;

                Show_Line(term, len - 1); // len < 0 (stay at end)
                break; }

            case 'D': // left arrow (VT100)
                Move_Cursor(term, -1);
                break;

            case 'C': // right arrow (VT100)
                Move_Cursor(term, 1);
                break;

            case '1': // home (CSI) or higher function keys (VT220)
                if (cp[1] != '~') {
                    Unrecognized_Key_Sequence(cp - 2);
                    goto restart;
                }
                Home_Line(term);
                ++cp; // remove 1, the ~ is consumed after the switch
                break;

            case '4': // end (CSI)
                if (cp[1] != '~') {
                    Unrecognized_Key_Sequence(cp - 2);
                    goto restart;
                }
                End_Line(term);
                ++cp; // remove 4, the ~ is consumed after the switch
                break;

            case '3': // delete (CSI)
                if (cp[1] != '~') {
                    Unrecognized_Key_Sequence(cp - 2);
                    goto restart;
                }
                Delete_Char(term, FALSE);
                ++cp; // remove 3, the ~ is consumed after the switch
                break;

            case 'H': // home (VT100)
                Home_Line(term);
                break;

            case 'F': // end !!! (in what standard?)
                End_Line(term);
                break;

            case 'J': // erase to end of screen (VT100)
                Clear_Line(term);
                break;

            default:
                Unrecognized_Key_Sequence(cp - 2);
                goto restart;
            }

            ++cp;
            continue;
        }

        if (*cp == ESC) {
            //
            // non-CSI Escape Sequences
            //
            // http://ascii-table.com/ansi-escape-sequences-vt-100.php

            ++cp;

            switch (*cp) {
            case 'H':   // !!! "home" (in what standard??)
            #if !defined(NDEBUG)
                rebFail ("ESC 'H' - please report your system info");
            #endif
                Home_Line(term);
                break;

            case 'F':   // !!! "end" (in what standard??)
            #if !defined(NDEBUG)
                rebFail ("ESC 'F' - please report your system info");
            #endif
                End_Line(term);
                break;

            case '\0':
                assert(FALSE); // plain escape handled earlier for clarity
                /* fallthrough */
            default:
                Unrecognized_Key_Sequence(cp - 1);
                goto restart;
            }

            ++cp;
            continue;
        }

        // C0 control codes and Bash-inspired Shortcuts
        //
        // https://en.wikipedia.org/wiki/C0_and_C1_control_codes
        // https://ss64.com/bash/syntax-keyboard.html
        //
        switch (*cp) {
        case BS: // backspace (C0)
        case DEL: // delete (C0)
            Delete_Char(term, TRUE);
            break;

        case CR: // carriage return (C0)
            if (cp[1] == LF)
                ++cp; // disregard the CR character, else treat as LF
            /* falls through */
        case LF: // line feed (C0)
            WRITE_STR("\r\n");
            Store_Line(term);
            ++cp;
            goto line_end_reached;

        case 1: // CTRL-A, Beginning of Line (bash)
            Home_Line(term);
            break;

        case 2: // CTRL-B, Backward One Character (bash)
            Move_Cursor(term, -1);
            break;

        case 3: // CTRL-C, Interrupt (ANSI, <signal.h> is standard C)
            //
            // It's theoretically possible to clear the termios `c_lflag` ISIG
            // in order to receive literal Ctrl-C, but we dont' want to get
            // involved at that level.  Using sigaction() on SIGINT and
            // causing EINTR is how we would like to be triggering HALT.
            //
            rebFail ("console got literal Ctrl-C, but didn't request it");

        case 4: // CTRL-D, Synonym for Cancel Input (Windows Terminal Garbage)
            //
            // !!! In bash this is "Delete Character Under the Cursor".  But
            // the Windows Terminal forces our hands to not use Escape for
            // canceling input.  See notes regarding ReadConsole() along with
            // ENABLE_LINE_INPUT.
            //
            // If one is forced to choose only one thing, it makes more sense
            // to make this compatible with the Windows console so there's
            // one shortcut you can learn that works on both platforms.
            // Though ideally it would be configurable--and it could be, if
            // the Windows version had to manage the edit buffer with as
            // much manual code as this POSIX version does.
            //
        #if 0
            Delete_Char(term, FALSE);
            break
        #else
            goto return_blank;
        #endif

        case 5: // CTRL-E, End of Line (bash)
            End_Line(term);
            break;

        case 6: // CTRL-F, Forward One Character (bash)
            Move_Cursor(term, 1);
            break;

        default:
            Unrecognized_Key_Sequence(cp);
            goto restart;
        }

        ++cp;
    }

    if (term->out == 0)
        goto restart;

return_blank:
    //
    // INPUT has a display invariant that the author of the code expected
    // a newline to be part of what the user contributed.  To keep the visual
    // flow in the case of a cancellation key that didn't have a newline, we
    // have to throw one in.
    //
    WRITE_STR("\r\n");
    result[0] = ESC;
    result[1] = '\0';
    return 1;

return_halt:
    WRITE_STR("\r\n"); // see note above on INPUT's display invariant
    result[0] = '\0';
    return 0;

line_end_reached:
    // Not at end of input? Save any unprocessed chars:
    if (*cp != '\0') {
        if (LEN_BYTES(term->residue) + LEN_BYTES(cp) >= TERM_BUF_LEN - 1) {
            //
            // avoid overrun
        }
        else
            strcat(s_cast(term->residue), cs_cast(cp));
    }

    // Fill the output buffer:
    int len = LEN_BYTES(term->out); // length of IO read
    if (len >= limit - 1)
        len = limit - 2;
    strncpy(s_cast(result), s_cast(term->out), limit);
    result[len++] = LF;
    result[len] = '\0';
    return len;
}
