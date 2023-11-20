//
//  File: %mod-stdio.c
//  Summary: "Standard Input And Output Ports"
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


#include "sys-core.h"

#include "tmp-mod-stdio.h"

#include "readline.h"


// See %stdio-posix.c and %stdio-windows.c for the differing implementations of
// what has to be done on startup and shutdown of stdin, stdout, or smart
// terminal services.
//
extern void Startup_Stdio(void);
extern void Shutdown_Stdio(void);

// This used to be a function you had to build a "device request" to interact
// with.  But so long as our file I/O is synchronous, there's no reason for
// that layer.  And if we were going to do asynchronous file I/O it should
// be done with a solidified layer like libuv, vs. what was in R3-Alpha.
//
extern void Write_IO(const REBVAL *data, REBLEN len);

extern bool Read_Stdin_Byte_Interrupted(bool *eof, Byte* out);


extern Bounce Console_Actor(Level* level_, REBVAL *port, const Symbol* verb);


//
//  get-console-actor-handle: native [
//
//  {Retrieve handle to the native actor for console}
//
//      return: [handle!]
//  ]
//
DECLARE_NATIVE(get_console_actor_handle)
{
    Make_Port_Actor_Handle(OUT, &Console_Actor);
    return OUT;
}


//
//  startup*: native [  ; Note: DO NOT EXPORT!
//
//      return: <none>
//  ]
//
DECLARE_NATIVE(startup_p)
{
    STDIO_INCLUDE_PARAMS_OF_STARTUP_P;

    // This does the platform-specific initialization for stdio.  Included in
    // that is doing things like figuring out if the input or output have
    // been redirected to a file--in which case, it has to know not to try
    // and treat it as a "smart console" with cursoring around ability.
    //
    Startup_Stdio();

    return rebNone();
}


//
//  export write-stdout: native [
//
//  "Write text to standard output, or raw BINARY! (for control codes / CGI)"
//
//      return: <none>
//      value [<maybe> text! char! binary!]
//          "Text to write, if a STRING! or CHAR! is converted to OS format"
//  ]
//
DECLARE_NATIVE(write_stdout)
//
// Note: It is sometimes desirable to write raw binary data to stdout.  e.g.
// CGI scripts may be hooked up to stream data for a download, and not want the
// bytes interpreted in any way.  (e.g. not changed from UTF-8 to wide
// characters, or not having CR turned into CR LF sequences).
{
    INCLUDE_PARAMS_OF_WRITE_STDOUT;

    REBVAL *v = ARG(value);

    // !!! We want to make the chunking easier, by having a position in the
    // cell...but ISSUE! has no position.  Alias it as a read-only TEXT!.
    //
    if (IS_ISSUE(v)) {
        bool threw = rebRunThrows(
            cast(REBVAL*, SPARE),  // <-- output cell
            Canon(AS), Canon(TEXT_X), v
        );
        assert(not threw);
        UNUSED(threw);
        Move_Cell(v, SPARE);
    }

    // !!! The Write_IO() function does not test for halts.  So data is broken
    // up into small batches and it's this loop that calls rebWasHalting().
    // So one can send a giant string to the code that does write() and be
    // able to interrupt it...
    //
    // ...at least, theoretically.  The device request could block forever.
    // There might be some smarter way to plug into the unix signal system
    // to guarantee breaking out of writes.
    //
    // There may well be a better way to go about this, but at least a very
    // long write can be canceled with this.
    //
    REBLEN remaining;
    while ((remaining = VAL_LEN_AT(v)) > 0) {
        //
        // Yield to signals processing for cancellation requests.
        //
        if (rebWasHalting()) {  // the test clears halt request
            rebHalt();  // put in a new halt request and stop
            return NONE;  // ...or, could also RAISE() or FAIL() a halt
        }

        REBLEN part;
        if (remaining <= 1024)
            part = remaining;
        else
            part = 1024;

        Write_IO(v, part);

        VAL_INDEX_RAW(v) += part;
    }

    return NONE;
}


//
//  export read-stdin: native [
//
//  {Read binary data from standard input}
//
//      return: "Null if no more input is available, ~escape~ if aborted"
//          [<opt> binary! quasi-word!]
//      @eof "Set to true if end of file reached"
//          [logic!]
//      size "Maximum size of input to read"
//          [integer!]
//  ]
//
DECLARE_NATIVE(read_stdin)
//
// READ-LINE caters to the needs of the console and always returns TEXT!.  So
// it will error if input is redirected from a file that is not UTF-8.  But
// but READ-STDIN is for piping arbitrary data.
//
// There's a lot of parameterization someone might want here, involving
// timeouts and such.  Those designs should probably be looking to libuv or
// Boost.ASIO for design inspiration.
{
    STDIO_INCLUDE_PARAMS_OF_READ_STDIN;

  #ifdef REBOL_SMART_CONSOLE
    if (Term_IO) {
        if (rebRunThrows(
            cast(REBVAL*, OUT),  // <-- output cell
            "as binary! maybe read-line"
        )){
            return THROWN;
        }
        if (Is_Nulled(OUT))
            return nullptr;  // don't proxy multi-returns

        Init_Logic(ARG(eof), false);  // never terminates?
        return Proxy_Multi_Returns(level_);
    }
    else  // we have a smart console but aren't using it (redirected to file?)
  #endif
    {
        // For the moment, just do a terribly inefficient implementation that
        // just APPENDs to a BINARY!.
        //
        bool eof = false;

        Size max = VAL_UINT32(ARG(size));
        Binary* bin = Make_Binary(max);
        REBLEN i = 0;
        while (Binary_Len(bin) < max) {
            if (Read_Stdin_Byte_Interrupted(&eof, Binary_At(bin, i))) {  // Ctrl-C
                if (rebWasHalting())
                    rebJumps(Canon(HALT));
                fail ("Interruption of READ-STDIN for reason other than HALT?");
            }
            if (eof)
                break;
            ++i;
        }
        Term_Binary_Len(bin, i);

        Init_Logic(ARG(eof), eof);
        Init_Binary(OUT, bin);
        return Proxy_Multi_Returns(level_);
    }
}


//
//  export read-line: native [
//
//  {Read a line from standard input, with smart line editing if available}
//
//      return: "Null if no more input is available, ~escape~ if aborted"
//          [<opt> text! quasi-word!]
//      @eof "Set to true if end of file reached"
//          [logic!]
//      /raw "Include the newline, and allow reaching end of file with no line"
//      /hide "Mask input with a * character (not implemented)"
//  ]
//
DECLARE_NATIVE(read_line)
{
    STDIO_INCLUDE_PARAMS_OF_READ_LINE;

    if (REF(hide))
        fail (
            "READ-LINE/HIDE not yet implemented:"
            " https://github.com/rebol/rebol-issues/issues/476"
        );

    // !!! When this primitive was based on system.ports.input, you could get
    // ~halt~ returned from a READ operation when there had been a Ctrl-C.
    // The reasoning was that when the lower-level read() call sensed it was
    // interrupted it was not a safe time to throw across API processing.  This
    // meant READ-LINE would raise the actual halt signal.  That idea should
    // be reviewed in light of this new entry point.

    REBVAL *line;
    bool eof;  // can tell whether all code paths assign or not, vs ARG(eof)

  #ifdef REBOL_SMART_CONSOLE
    if (Term_IO) {
        line = Read_Line(Term_IO);
        if (rebUnboxLogic(rebQ(line), "= '~halt~"))
            rebJumps(Canon(HALT));

        // ESCAPE is a special condition distinct from end of file.  It is a
        // request to nullify the current input--which may apply to several
        // lines of input, e.g. in the REPL.
        //
        if (rebUnboxLogic(rebQ(line), "= '~escape~")) {
            Init_False(ARG(eof));
            return line;
        }

        // !!! A concept for the smart terminal is that if you were running an
        // interactive console, then you could indicate an end of file for the
        // currently running command...but that would only be an end of file
        // until it ended.  Then the input would appear to come back.
        //
        eof = false;
    }
    else  // we have a smart console but aren't using it (redirected to file?)
  #endif
    {
        // FWIW: There is no standard getline() function in C.  But we'd want
        // to use our own memory management since we're making a TEXT! anyway.
        //
        // !!! This uses the internal API to have access to the mold buffer.
        // Attempts were made to keep most of the stdio extension using the
        // "friendly" libRebol API, but this seems like a case where using
        // the core has an advantage.
        //
        // !!! Windows redirected files give bytes as-is, unlike the console
        // which gives UTF16.  READ-LINE expects UTF-8, while READ-STDIN
        // would presumably be able to process any bytes.
        //
        DECLARE_MOLD (mo);
        Push_Mold(mo);

        Byte encoded[UNI_ENCODED_MAX];

        while (true) {
            if (Read_Stdin_Byte_Interrupted(&eof, &encoded[0])) {  // Ctrl-C
                if (rebWasHalting())
                    rebJumps(Canon(HALT));

                fail ("Interruption of READ-LINE for reason other than HALT?");
            }
            if (eof) {
                if (mo->base.size == String_Size(mo->series)) {
                    //
                    // If we hit the end of file before accumulating any data,
                    // then just return nullptr as an end of file signal.
                    //
                    Drop_Mold(mo);
                    Init_True(ARG(eof));
                    return nullptr;
                }

                if (REF(raw))
                    break;
                fail ("READ-LINE without /RAW hit end of file with no newline");
            }

            Codepoint c;

            uint_fast8_t trail = g_trailing_bytes_for_utf8[encoded[0]];
            if (trail == 0)
                c = encoded[0];
            else {
                Size size = 1;  // we add to size as we count trailing bytes
                while (trail != 0) {
                    if (Read_Stdin_Byte_Interrupted(&eof, &encoded[size])) {
                        if (rebWasHalting())
                            rebJumps(Canon(HALT));

                        fail ("Interruption of READ-LINE"
                              " for reason other than HALT?");
                    }
                    if (eof)
                        fail ("Incomplete UTF-8 sequence from stdin at EOF");
                    ++size;
                    --trail;
                }

                if (nullptr == Back_Scan_UTF8_Char(&c, encoded, &size))
                    fail ("Invalid UTF-8 Sequence found in READ-LINE");
            }

            if (c == '\n') {  // found a newline
                if (REF(raw))
                    Append_Codepoint(mo->series, c);
                break;
            }

            Append_Codepoint(mo->series, c);
        }

        line = Init_Text(Alloc_Value(), Pop_Molded_String(mo));
    }

  #if !defined(NDEBUG)
    if (line) {
        assert(IS_TEXT(line));

        // READ-LINE is textual, and enforces the rules of Ren-C TEXT!.  So
        // there should be no CR.  It may be that the /RAW mode permits reading
        // CR, but it also may be that READ-STDIN should be used for BINARY!
        // instead.  Ren-C wants to stamp CR out of all the files it can.
        //
        rebElide("assert [not find", line, "CR]");

        if (not REF(raw))
            rebElide("assert [not find", line, "LF]");
    }
  #endif

    Copy_Cell(OUT, line);
    rebRelease(line);

    if (Is_Nulled(OUT))
        return nullptr;

    Init_Logic(ARG(eof), eof);
    return Proxy_Multi_Returns(level_);
}


//
//  export read-char: native [
//
//  {Inputs a single character from the input}
//
//      return: "Null if end of file or input was aborted (e.g. via ESCAPE)"
//          [<opt> char! word! quasi-word!]
//
//      /virtual "Return keys like Up, Ctrl-A, or ESCAPE vs. ignoring them"
//      /timeout "Seconds to wait before returning ~timeout~ if no input"
//          [integer! decimal!]
//  ]
//
DECLARE_NATIVE(read_char)
//
// Note: There is no EOF signal here as in READ-LINE.  Because READ-LINE in
// raw mode needed to distinguishing between termination due to newline and
// termination due to end of file.  Here, it's only a single character.  Hence
// NULL is sufficient to signal the caller is to treat it as no more input
// available... that's EOF.
{
    STDIO_INCLUDE_PARAMS_OF_READ_CHAR;

    int timeout_msec;
    if (not REF(timeout))
        timeout_msec = 0;
    else {
        // !!! Because 0 sounds like "timeout in 0 msec" it could mean return
        // instantly if no character is available.  It's used to mean "no
        // timeout" in the quick and dirty implementation added for POSIX, but
        // this may change.
        //
        if (IS_DECIMAL(ARG(timeout)))
            timeout_msec = VAL_DECIMAL(ARG(timeout)) * 1000;
        else
            timeout_msec = VAL_INT32(ARG(timeout)) * 1000;

        if (timeout_msec == 0)
            fail ("Use NULL instead of 0 for no /TIMEOUT in READ-CHAR");
    }

  #ifdef REBOL_SMART_CONSOLE
    if (Term_IO) {
        //
        // We don't want to use buffering, because that tries to batch up
        // several keystrokes into a TEXT! if it can.  We want the first char
        // typed we can get.
        //
      retry: ;
        const bool buffered = false;
        REBVAL *e = Try_Get_One_Console_Event(Term_IO, buffered, timeout_msec);
        // (^-- it's an ANY-VALUE!, not a R3-Alpha-style EVENT!)

        if (e == nullptr) {
            rebJumps(
                "fail {nullptr interruption of terminal not done yet}"
            );
        }

        if (rebUnboxLogic("quasi?", rebQ(e))) {
            if (rebUnboxLogic(rebQ(e), "= '~halt~"))  // Ctrl-C instead of key
                rebJumps(Canon(HALT));

            if (rebUnboxLogic(rebQ(e), "= '~timeout~"))
                return e;  // just return the timeout answer

            // For the moment there aren't any other signals; if there were,
            // they may be interesting to the caller.
            //
            assert(!"Unknown QUASI! signal in Try_Get_One_Console_Event()");
            return e;
        }

        if (rebUnboxLogic("char? @", e))
            return e;  // we got the character, note it hasn't been echoed

        if (rebUnboxLogic("word? @", e)) {  // recognized "virtual key"
            if (REF(virtual))
                return e;  // user wanted to know the virtual key

            if (rebUnboxLogic("'escape = @", e)) {
                //
                // In the non-virtual mode, allow escape to return null.
                //
                Term_Abandon_Pending_Events(Term_IO);
                rebRelease(e);

                return nullptr;
            }

            rebRelease(e);  // ignore all other non-printable keys
        }
        else if (rebUnboxLogic("issue? @", e)) {  // unrecognized key
            //
            // Assume they wanted to know what it was if virtual.
            //
            if (REF(virtual))
                return e;

            rebRelease(e);
        }

        goto retry;
    }
    else  // we have a smart console but aren't using it (redirected to file?)
  #endif
    {
        bool eof;

        Byte encoded[UNI_ENCODED_MAX];

        if (Read_Stdin_Byte_Interrupted(&eof, &encoded[0])) {  // Ctrl-C
            if (rebWasHalting())
                rebJumps(Canon(HALT));

            fail ("Interruption of READ-CHAR for reason other than HALT?");
        }
        if (eof) {
            //
            // If we hit the end of file before accumulating any data,
            // then just return nullptr as an end of file signal.
            //
            return nullptr;
        }

        Codepoint c;

        uint_fast8_t trail = g_trailing_bytes_for_utf8[encoded[0]];
        if (trail == 0)
            c = encoded[0];
        else {
            Size size = 1;  // we add to size as we count trailing bytes
            while (trail != 0) {
                if (Read_Stdin_Byte_Interrupted(&eof, &encoded[size])) {
                    if (rebWasHalting())
                        rebJumps(Canon(HALT));

                    fail ("Interruption of READ-CHAR"
                            " for reason other than HALT?");
                }
                if (eof)
                    fail ("Incomplete UTF-8 sequence from stdin at EOF");
                ++size;
                --trail;
            }

            if (nullptr == Back_Scan_UTF8_Char(&c, encoded, &size))
                fail ("Invalid UTF-8 Sequence found in READ-CHAR");
        }

        return rebChar(c);
    }
}


//
//  shutdown*: native [  ; Note: DO NOT EXPORT!
//
//  {Shut down the stdio and terminal devices, called on extension unload}
//
//      return: <none>
//  ]
//
DECLARE_NATIVE(shutdown_p)
{
    STDIO_INCLUDE_PARAMS_OF_SHUTDOWN_P;

    // This shutdown does platform-specific teardown, freeing buffers that
    // may only be have been created for Windows, etc.
    //
    Shutdown_Stdio();

    return rebNone();
}
