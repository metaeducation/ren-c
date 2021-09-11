//
//  File: %p-console.c
//  Summary: "console port interface"
//  Section: ports
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
// !!! R3's CONSOLE "actor" came with only a READ method and no WRITE.
// Writing was done through Prin_OS_String() to the Dev_StdIO device without
// going through a port.  SYSTEM/PORTS/INPUT was thus created from it.
//

#include "sys-core.h"

#include "readline.h"


// This used to be a function you had to build a "device request" to interact
// with.  But so long as our file I/O is synchronous, there's no reason for
// that layer.  And if we were going to do asynchronous file I/O it should
// be done with a solidified layer like libuv, vs. what was in R3-Alpha.
//
extern size_t Read_IO(REBYTE *buf, size_t size);


#if defined(REBOL_SMART_CONSOLE)
    STD_TERM *Term_IO = nullptr;
#endif

// The history mechanism is deliberately separated out from the line-editing
// mechanics.  The I/O layer is only supposed to emit keystrokes and let the
// higher level code (ultimately usermode Rebol) make decisions on what to
// do with that.  No key is supposed to have an intrinsic "behavior".
//
#define MAX_HISTORY  300   // number of lines stored
REBVAL *Line_History;  // Prior input lines (BLOCK!)
int Line_History_Index;  // Current position in the line history
#define Line_Count \
    rebUnboxInteger("length of", Line_History)

#if defined(REBOL_SMART_CONSOLE)


//
//  Read_Line: C
//
// Read a line (as a sequence of bytes) from the terminal.  Handles line
// editing and line history recall.
//
// If HALT is encountered (e.g. a Ctrl-C), this routine will return BAD-WORD!
// If ESC is pressed, this will return a BLANK!.
// Otherwise it will return a TEXT! of the read-in string.
//
// !!! Read_Line is a transitional step as a C version of what should move to
// be usermode Rebol, making decisions about communication with the terminal
// on a keystroke-by-keystroke basis.
//
REBVAL *Read_Line(STD_TERM *t)
{
    Line_History_Index = Line_Count;

    // When we ask to read input, we may not be at the start of a line (e.g.
    // there could be a prompt to the left).  We want a keystroke like Ctrl-A
    // for "go to start of line" to seek the place we start at, not the end.
    //
    int original_column = Term_Pos(t);

    REBVAL *line = nullptr;
    while (line == nullptr) {
        const bool buffered = true;
        REBVAL *e = Try_Get_One_Console_Event(t, buffered);
        // (^-- it's an ANY-VALUE!, not a R3-Alpha-style EVENT!)

        if (e == nullptr) {
            rebJumps(
                "fail {nullptr interruption of terminal not done yet}"
            );
        }

        if (rebDid("bad-word?", rebQ(e)))  // e.g. ~halt~
            return e;

        if (rebDid("@", e, "= newline")) {
            //
            // !!! This saves a line in the "history", but it's not clear
            // exactly long term what level this history should cut into
            // the system.
            //
            // If max history, drop oldest line (but not first empty line)
            //
            if (Line_Count >= MAX_HISTORY)
                rebElide("remove next", Line_History);

            // We don't want the terminal's whole line buffer--just the part
            // after any prompt that was already on the line.
            //
            line = rebValue(
                "copy skip", rebR(Term_Buffer(t)), rebI(original_column)
            );

            rebElide("append", Line_History, "copy", line);
        }
        else if (rebDid("match [text! char!] @", e)) {  // printable
            //
            // Because we are using the "buffered" mode, the terminal will
            // accrue TEXT! in a batch until an "unbufferable" key event
            // is gathered (which includes newlines).  Doing otherwise would
            // lead to an even higher latency on pastes.
            //
            Term_Insert(t, e);
        }
        else if (rebDid("word? @", e)) {  // recognized "virtual key"
            uint32_t c = rebUnboxChar(
                "switch @", e, "[",
                    "'escape [#E]",

                    "'up [#U]",
                    "'down [#D]",
                    "'ctrl-b",  // Backward One Character (bash)
                        "'left [#L]",
                    "'ctrl-f",  // Forward One Character (bash)
                        "'right [#R]",

                    "'backspace [#b]",
                    "'ctrl-d",  // Delete Character Under Cursor (bash)
                        "'delete [#d]",

                    "'tab [#t]",  // completion logic (bash)

                    "'ctrl-a",  // Beginning of Line (bash)
                        "'home [#h]",
                    "'ctrl-e",  // CTRL-E, end of Line (bash)
                        "'end [#e]",

                    "'clear [#c]",

                "] else [#]"  // unboxes as '\0'
            );

            switch (c) {
              case 0:  // Ignored (e.g. unknown Ctrl-XXX)
                break;

              case 'E':  // ESCAPE
                Term_Abandon_Pending_Events(t);
                line = rebValue("'~escape~");
                break;

              case 'U':  // UP
                Line_History_Index -= 2;  // actually -1 (down_arrow adds 1)
                goto down_arrow;  // ...has otherwise same updating code...

              down_arrow:;
              case 'D': {  // DOWN
                ++Line_History_Index;

                if (Line_History_Index < 0)
                    Line_History_Index = 0;

                if (Line_History_Index == 0)
                    Term_Beep(t);  // !!! is an audible alert good?

                Term_Seek(t, original_column);
                Term_Clear_To_End(t);
                assert(Term_Pos(t) == original_column);

                if (Line_History_Index >= Line_Count) {  // no "next"
                    Line_History_Index = Line_Count;  // we already cleared
                }
                else {
                    REBVAL *recall = rebValue(
                        "pick", Line_History, rebI(Line_History_Index + 1)
                    );

                    Term_Insert(t, recall);

                  #if !defined(NDEBUG)
                    int len = rebUnboxInteger("length of", recall);
                    assert(Term_Pos(t) == len + original_column);
                  #endif

                    rebRelease(recall);
                }
                break; }

              case 'L':  // LEFT
                if (Term_Pos(t) > original_column)
                    Move_Cursor(t, -1);
                break;

              case 'R': {  // RIGHT
                int len = rebUnboxInteger("length of", rebR(Term_Buffer(t)));
                if (Term_Pos(t) < len)
                    Move_Cursor(t, 1);
                break; }

              case 'b':  // backspace
                if (Term_Pos(t) > original_column)
                    Delete_Char(t, true);
                break;

              case 'd': {  // delete
                int len = rebUnboxInteger("length of", rebR(Term_Buffer(t)));
                if (Term_Pos(t) < len)
                    Delete_Char(t, false);
                break; }

              case 'h':  // home
                Term_Seek(t, original_column);
                break;

              case 'e': {  // end
                int len = rebUnboxInteger("length of", rebR(Term_Buffer(t)));
                Term_Seek(t, len);
                break; }

              case 'c':  // clear (to end of line)
                Term_Clear_To_End(t);
                break;

              case 't': {  // complete current selection
                //
                // Protocol for TAB-COMPLETE is currently to edit the string
                // you give it directly, and return the new position.
                //
                REBVAL *buffer_copy = rebValue("copy", rebR(Term_Buffer(t)));
                int new_pos = rebUnboxInteger(
                    "tab-complete", buffer_copy, rebI(Term_Pos(t))
                );
                Term_Seek(t, original_column);
                Term_Clear_To_End(t);
                Term_Insert(t, buffer_copy);  // cursor at end of insertion
                Term_Seek(t, original_column + new_pos);  // seek returned pos
                rebRelease(buffer_copy);
                break; }

              default:
                rebJumps(
                    "fail {Invalid key press returned from console}"
                );
            }
        }
        else if (rebDid("issue? @", e)) {  // unrecognized key
            //
            // When an unrecognized key is hit, people may want to know that
            // at least the keypress was received.  Or not.  For now, output
            // a key message to say "we don't know what you hit".
            //
            // !!! In the future, this might do something more interesting to
            // get the BINARY! information for the key sequence back up out of
            // the terminal, so that people could see what the key registered
            // as on their machine and configure the console to respond to it.
            //
            REBVAL *text = rebValue("as text!", e);
            Term_Insert(t, text);
            rebRelease(text);
        }

        rebRelease(e);
    }

    // ASK has a display invariant that a newline is visually expected as part
    // of what the user contributed.  The HALT returns before this point, and
    // the console extension throws in the newline in that case.
    //
    REBVAL *newline = rebChar('\n');
    Term_Insert(t, newline);
    rebRelease(newline);

    return line;
}

#endif  // if defined(REBOL_SMART_CONSOLE)


//
//  Console_Actor: C
//
REB_R Console_Actor(REBFRM *frame_, REBVAL *port, const REBSYM *verb)
{
    REBCTX *ctx = VAL_CONTEXT(port);

    switch (ID_OF_SYMBOL(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value)); // implied by `port`

        SYMID property = VAL_WORD_ID(ARG(property));
        switch (property) {
          case SYM_OPEN_Q:
            return Init_True(D_OUT);  // stdio port always open

          default:
            break;
        }

        break; }

      case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PAR(source));

        if (REF(part))
            fail (Error_Bad_Refines_Raw());

        if (REF(seek))
            fail (Error_Bad_Refines_Raw());

        UNUSED(PAR(string)); // handled in dispatcher
        UNUSED(PAR(lines)); // handled in dispatcher

      #if defined(REBOL_SMART_CONSOLE)
        if (Term_IO) {  // e.g. no redirection (Term_IO is null if so)
            REBVAL *result = Read_Line(Term_IO);
            if (rebDid("'~halt~ =", rebQ(result))) {  // HALT received
                rebRelease(result);
                return rebNone();
            }
            if (rebDid("blank?", result)) {  // ESCAPE received
                rebRelease(result);
                return rebValue(
                    "const to binary!", rebR(rebChar(ESC))
                );
            }
            assert(rebDid("text?", result));
            return rebValue("as binary!", rebR(result));
        }
      #endif

        // This build either doesn't have smart console features, or it does
        // and the input or output have been redirected to a file.
        //
        // !!! A fixed size buffer is used to gather standard input.  This is
        // re-used between READ requests.  A better strategy should be used:
        //
        // https://github.com/rebol/rebol-issues/issues/2364
        //
        // !!! It appears using ReadFile() on a stdin handle which is attached
        // to a console can give ERROR_NOT_ENOUGH_MEMORY on some versions of
        // windows when too large a request is made (e.g. Windows 7).  The
        // issue arose in the Go language as well:
        //
        // https://github.com/golang/go/issues/13697

        const REBLEN readbuf_size = 30 * 1024;  // may back off to smaller size

        REBVAL *data = CTX_VAR(ctx, STD_PORT_DATA);
        if (not IS_BINARY(data)) {
            Init_Binary(RESET(data), Make_Binary(readbuf_size));
        }
        else if (SER_REST(VAL_BINARY(data)) < readbuf_size) {
            REBBIN *bin = VAL_BINARY_ENSURE_MUTABLE(data);
            EXPAND_SERIES_TAIL(bin, readbuf_size - SER_REST(bin));
        }

        // !!! An egregious hack in READ-LINE to try and coax the system to
        // work with piped input actually puts data back into the buffer.
        // So it may have all the input that was left and the pipe could be
        // closed.  We do not want to get ERROR_BROKEN_PIPE by asking for a
        // read on a closed handle, so if we have enough data in the buffer
        // that a line could be read, pass it back.
        //
        // All of this code is bad and needs to be thrown out; redirection
        // of stdio is tricky and R3-Alpha was not designed for it.  These
        // hacks are just to try and facilitate the automated testing of more
        // critical design features.
        //
        if (rebNot("find", data, "lf")) {
            //
            // Since we're not using the terminal code, we don't have per-char
            // control to eliminate the CR characters.  Raw READ from stdio must
            // be able to go byte level, however.  Those wishing to interpret
            // Windows data as text with lines will thus have to deline it (!)
            //
            size_t size = readbuf_size - VAL_LEN_AT(data);
            REBBIN *bin = VAL_BINARY_ENSURE_MUTABLE(data);
            REBLEN orig_len = VAL_LEN_AT(data);

            assert(SER_AVAIL(bin) >= size);

            REBYTE *buf = BIN_AT(bin, orig_len);

            size_t actual = Read_IO(buf, size);  // appends to tail

            TERM_BIN_LEN(bin, orig_len + actual);
        }

        // Give back a BINARY! which is as large as the portion of the buffer
        // actually used, and clear the buffer for reuse.
        //
        return rebValue("copy", data, "elide clear", data); }

      case SYM_OPEN:
        RETURN (port);

      case SYM_CLOSE:
        RETURN (port);

      default:
        break;
    }

    return R_UNHANDLED;
}
