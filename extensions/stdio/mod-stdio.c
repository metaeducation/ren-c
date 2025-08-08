//
//  file: %mod-stdio.c
//  summary: "Standard Input And Output Ports"
//  section: ports
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
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

// Synchronous I/O (libuv supports asynchronous, but the stdio extension is
// designed to be independent of libuv)
//
extern void Write_IO(const Value* data, REBLEN len);

extern bool Read_Stdin_Byte_Interrupted(bool *eof, Byte* out);


//
//  startup*: native [  ; Note: DO NOT EXPORT!
//
//      return: []
//  ]
//
DECLARE_NATIVE(STARTUP_P)
//
// 1. Besides making buffers or other initialization, the platform startup does
//    things like figure out if the input or output have been redirected to a
//    file--in which case, it has to know not to try and treat it as a "smart
//    console" with cursoring around ability.
{
    INCLUDE_PARAMS_OF_STARTUP_P;

    Startup_Stdio();  // platform-specific init, redirect detection [1]

    return "~";
}


//
//  export write-stdout: native [
//
//  "Write text or raw BLOB! to stdout (for control codes / CGI)"  ; [1]
//
//      return: []
//      value [<opt-out> text! char? blob!]
//          "Text to write, if a STRING! or CHAR! is converted to OS format"
//  ]
//
DECLARE_NATIVE(WRITE_STDOUT)
//
// 1. It is sometimes desirable to write raw binary data to stdout.  e.g. CGI
//    scripts may be hooked up to stream data for a download, and not want the
//    bytes interpreted in any way.  (e.g. not changed from UTF-8 to wide
//    characters, or not having LF turned into CR LF sequences).
//
// 2. The Write_IO() function does not currently test for halts.  So data is
//    broken up into small batches, and rebWasHaltRequested() gets called by
//    this loop.  There may well be a better way to go about this, but at least
//    a very long write can be canceled with this.
//
// 3. We want to make the chunking in [2] easier by having a position in the
//    cell, but RUNE! has no position.  Alias it as a read-only TEXT!
{
    INCLUDE_PARAMS_OF_WRITE_STDOUT;

    Value* v = ARG(VALUE);

    if (Is_Rune(v)) {  // [3]
        Value *alias = rebValue("as text!", v);
        Copy_Cell(v, alias);
        rebRelease(alias);
    }

    REBLEN remaining;
    while ((remaining = Series_Len_At(v)) > 0) {  // chunk for halts [2]
        //
        // Yield to signals processing for cancellation requests.
        //
        if (rebWasHaltRequested())  // the test clears halt request
            return "halt";

        REBLEN part;
        if (remaining <= 1024)
            part = remaining;
        else
            part = 1024;

        Write_IO(v, part);

        SERIES_INDEX_UNBOUNDED(v) += part;
    }

    return "~";
}


static Value* Make_Escape_Error(const char* name) {
    return rebValue("make warning! [",
        "id: 'escape",
        "message: spaced [", rebT(name), "-[cancelled by user (e.g. ESCAPE)]-]"
    "]");
}


static Value* Make_Non_Halt_Error(const char* name) {
    return rebValue("make warning! [",
        "id: 'escape",
        "message: spaced [", rebT(name), "-[interrupted by non-HALT signal]-]"
    "]");
}


//
//  export read-stdin: native [
//
//  "Read binary data from standard input"
//
//      return: "Null if no more input is available, returns error on escape"
//          [null? blob! error!]
//      size "Maximum size of input to read"
//          [integer!]
//  ]
//
DECLARE_NATIVE(READ_STDIN)
//
// READ-LINE caters to the needs of the console and always returns TEXT!.  So
// it will error if input is redirected from a file that is not UTF-8.  But
// but READ-STDIN is for piping arbitrary data.
//
// There's a lot of parameterization someone might want here, involving
// timeouts and such.  Those designs should probably be looking to libuv or
// Boost.ASIO for design inspiration.
//
// NOTE: This should be dispatched to by `read stdin`, but the mechanics to
// do that do not exist yet.
{
    INCLUDE_PARAMS_OF_READ_STDIN;

  #ifdef REBOL_SMART_CONSOLE
    if (Term_IO) {
        return rebDelegate("catch [",
            "throw as blob! opt (",
                "read-line stdin except e -> [throw fail e]",
            ")",
        "]");
    }
    else  // we have a smart console but aren't using it (redirected to file?)
        goto read_from_stdin;
  #endif

  read_from_stdin: { //////////////////////////////////////////////////////=//

    bool eof = false;

    Size max = VAL_UINT32(ARG(SIZE));
    Binary* b = Make_Binary(max);
    REBLEN i = 0;
    while (Binary_Len(b) < max) {  // inefficient, read one byte at a time
        if (Read_Stdin_Byte_Interrupted(&eof, Binary_At(b, i))) {  // Ctrl-C
            if (rebWasHaltRequested())
                return "halt";
            return rebDelegate("panic", Make_Non_Halt_Error("READ-STDIN"));
        }
        if (eof)
            break;
        ++i;
    }
    Term_Binary_Len(b, i);

    return Init_Blob(OUT, b);;
}}


//
//  export read-line: native [
//
//  "Read a line from standard input, with smart line editing if available"
//
//      return: "Null if no more input is available, error antiform on escape"
//          [null? text! error!]
//      source "Where to read from (stdin currently only place supported)"
//          [~(@stdin)~]
//      :raw "Include the newline, and allow reaching end of file with no line"
//      :hide "Mask input with a * character (not implemented)"
//  ]
//
DECLARE_NATIVE(READ_LINE)
//
// 1. !!! When this primitive was based on READ of SYSTEM.PORTS.INPUT, that
//    READ would give back ~halt~ on a Ctrl-C (vs. having the READ execute
//    the halt).  The reasoning was that when the lower-level read() call
//    sensed it was interrupted it was not a safe time to throw across API
//    processing.  This is why READ-LINE is raising the actual HALT signal
//    (as a rebDelegate(), so it's not using setjmp/longjmp or exceptions).
//    READ-LINE now uses a lower-level API, so this raises the question of
//    what READ should be doing now in terms of HALTs.  Review.
//
// 2. ESCAPE is a special condition distinct from end of file.  It can
//    happen in the console, though it's not clear if piped input from a
//    file would ever "cancel".  This returns an error antiform.
//
// 3. !!! This uses the core API to have access to the mold buffer.  Attempts
//    were made to keep most of the stdio extension using the "friendly"
//    libRebol API, but this seems like a case where using the core has an
//    actual advantage.  Review.
//
// 4. There is no standard getline() function in C.  But we'd want to use our
//    own memory management since we're constructing a TEXT! anyway.
//
// 5. READ-LINE is textual, and enforces the rules of Ren-C TEXT!.  So there
//    should be no CR.  It may be that the :RAW mode permits reading CR, but
//    it also may be that READ-STDIN should be used for BLOB! instead.  Ren-C
//    wants to stamp CR out of all the files it can.
{
    INCLUDE_PARAMS_OF_READ_LINE;

  #if RUNTIME_CHECKS
    rebElide("assert [@stdin =", ARG(SOURCE), "]");
  #else
    UNUSED(ARG(SOURCE));
  #endif

    bool raw = Bool_ARG(RAW);
    bool hide = Bool_ARG(HIDE);

    if (hide)  // https://github.com/rebol/rebol-issues/issues/476
        return "panic -[READ-LINE:HIDE not yet implemented:]-";

    Value* line;

  #ifdef REBOL_SMART_CONSOLE
    if (Term_IO) {
        line = Read_Line(Term_IO);
        if (rebUnboxLogic(rebQ(line), "= '~halt~")) {
            rebRelease(line);
            return "halt";  // Execute throwing HALT [1]
        }
        if (rebUnboxLogic(rebQ(line), "= '~escape~")) { // distinct from eof [2]
            rebRelease(line);
            return rebDelegate(  // return definitional error
                "fail", rebR(Make_Escape_Error("READ-LINE"))
            );
        }
        goto got_line;
    }
    else  // we have a smart console but aren't using it (redirected to file?)
        goto read_from_stdin;
  #else
    UNUSED(Make_Escape_Error);
  #endif

  read_from_stdin: { //////////////////////////////////////////////////////=//

    DECLARE_MOLDER (mo);  // use of the internal API for efficiency [3]
    Push_Mold(mo);

    Byte encoded[UNI_ENCODED_MAX];

    while (true) {  // No getline() in C standard, implement ourselves [4]
        bool eof;
        if (Read_Stdin_Byte_Interrupted(&eof, &encoded[0])) {  // Ctrl-C
            if (rebWasHaltRequested())
                return "halt";  // [1]

            return rebDelegate("panic", Make_Non_Halt_Error("READ-LINE"));
        }
        if (eof) {
            if (mo->base.size == Strand_Size(mo->strand)) {
                Drop_Mold(mo);
                return nullptr;  // eof before any data, ok to say we're done
            }
            if (raw)
                break;  // caller should tell by no newline
            return "panic -[READ-LINE without :RAW hit EOF with no newline]-";
        }

        Codepoint c;

        uint_fast8_t trail = g_trailing_bytes_for_utf8[encoded[0]];
        if (trail == 0)
            c = encoded[0];
        else {
            Size size = 1;  // we add to size as we count trailing bytes
            while (trail != 0) {
                if (Read_Stdin_Byte_Interrupted(&eof, &encoded[size])) {
                    if (rebWasHaltRequested())
                        return "halt";  // [1]

                    return rebDelegate(
                        "panic", Make_Non_Halt_Error("READ-LINE")
                    );
                }
                if (eof)
                    return "panic -[Incomplete stdin UTF-8 sequence at EOF]-";
                ++size;
                --trail;
            }

            const Byte* bp = encoded;
            require (
              c = Back_Scan_Utf8_Char(&bp, &size)
            );
        }

        if (c == '\n') {  // found a newline
            if (raw)
                Append_Codepoint(mo->strand, c);
            break;
        }

        Append_Codepoint(mo->strand, c);
    }

    line = Init_Text(Alloc_Value(), Pop_Molded_Strand(mo));

} got_line: { /////////////////////////////////////////////////////////////=//

  #if RUNTIME_CHECKS
    rebElide(
        "ensure text!", line,
        "assert [not find", line, "CR]"  // Ren-C text rule [5]
    );
    if (not raw)
        rebElide("assert [not find", line, "LF]");
  #endif

    return line;  // implicit rebRelease()

}}


//
//  export read-char: native [
//
//  "Inputs a single character from the input"
//
//      return: "Null if end of file, error if escape or timeout"
//          [null? char? word! error!]
//      source "Where to read from (stdin currently only place supported)"
//          [~(@stdin)~]
//      :raw "Return keys like Up, Ctrl-A, or ESCAPE literally"
//      :timeout "Seconds to wait before returning ~timeout~ if no input"
//          [integer! decimal!]
//  ]
//
DECLARE_NATIVE(READ_CHAR)
//
// Note: There is no EOF signal here as in READ-LINE.  Because READ-LINE in
// /RAW mode needed to distinguish between termination due to newline and
// termination due to end of file.  Here, it's only a single character.  Hence
// NULL is sufficient to signal the caller is to treat it as no more input
// available... that's EOF.
//
// 1. Because 0 sounds like "timeout in 0 msec" it could mean return instantly
//    if no character is available.  It's used to mean "no timeout" in the
//    quick and dirty implementation added for POSIX, but this may change.
//    In any case, we don't want it to mean no timeout (that's just not
//    using the refinement), so bump to 1 for now.
{
    INCLUDE_PARAMS_OF_READ_CHAR;

  #if RUNTIME_CHECKS
    rebElide("assert [@stdin =", ARG(SOURCE), "]");
  #else
    UNUSED(ARG(SOURCE));
  #endif

    bool raw = Bool_ARG(RAW);

    int timeout_msec;
    if (not Bool_ARG(TIMEOUT))
        timeout_msec = 0;  // "no timeout" in Try_Get_One_Console_Event() [1]
    else {
        timeout_msec = rebUnboxInteger("case [",
            "decimal?", ARG(TIMEOUT), "[1000 * round:up", ARG(TIMEOUT), "]",
            "integer?", ARG(TIMEOUT), "[1000 *", ARG(TIMEOUT), "]",
            "panic ~[unreachable]~",
        "]");

        if (timeout_msec == 0)
            timeout_msec = 1;  // 0 would currently mean "no timeout" [1]
    }

  #ifdef REBOL_SMART_CONSOLE
    if (Term_IO) {
      retry: ;
        const bool buffered = false;
        Value* e = Try_Get_One_Console_Event(Term_IO, buffered, timeout_msec);

        if (e == nullptr)  // can smart terminal ever "disconnect" (?)
            return "panic -[Unexpected EOF reached with Smart Terminal API]-";

        if (rebUnboxLogic("quasi?", rebQ(e))) {
            if (rebUnboxLogic(rebQ(e), "= '~halt~"))  // Ctrl-C instead of key
                return "halt";

            if (rebUnboxLogic(rebQ(e), "= '~timeout~"))
                return "fail -[Timeout in READ-CHAR]-";

            // Note: no other signals at time of writing
            return "panic -[Unknown QUASI? from Try_Get_One_Console_Event()]-";
        }

        if (rebUnboxLogic("char? @", e))
            return e;  // we got the character, note it hasn't been echoed

        if (rebUnboxLogic("word? @", e)) {  // recognized "virtual key"
            if (raw)
                return e;  // user wanted to know the virtual key

            if (rebUnboxLogic("'escape = @", e)) {
                Term_Abandon_Pending_Events(Term_IO);
                rebRelease(e);
                return rebDelegate(
                    "fail", rebR(Make_Escape_Error("READ-CHAR"))
                );
            }

            rebRelease(e);  // ignore all other non-printable keys
            goto retry;
        }

        if (rebUnboxLogic("rune? @", e)) {  // unrecognized key
            if (raw)
                return e;

            rebRelease(e);  // ignore all other non-recognized keys
            goto retry;
        }

        return "panic -[Unexpected type from Try_Get_One_Console_Event()]-";
    }
    else  // we have a smart console but aren't using it (redirected to file?)
        goto read_from_stdin;
  #else
    UNUSED(raw);  // raw mode not supported without smart console
  #endif

  read_from_stdin: {  /////////////////////////////////////////////////////=//

    bool eof;

    Byte encoded[UNI_ENCODED_MAX];

    if (Read_Stdin_Byte_Interrupted(&eof, &encoded[0])) {  // Ctrl-C
        if (rebWasHaltRequested())
            return "halt";

        return rebDelegate("panic", Make_Non_Halt_Error("READ-CHAR"));
    }
    if (eof)  // eof before any data read, return null as end of input signal
        return nullptr;

    Codepoint c;

    uint_fast8_t trail = g_trailing_bytes_for_utf8[encoded[0]];
    if (trail == 0)
        c = encoded[0];
    else {
        Size size = 1;  // we add to size as we count trailing bytes
        while (trail != 0) {
            if (Read_Stdin_Byte_Interrupted(&eof, &encoded[size])) {
                if (rebWasHaltRequested())
                    return "halt";

                return rebDelegate("panic", Make_Non_Halt_Error("READ-CHAR"));
            }
            if (eof)
                return "panic -[Incomplete UTF-8 sequence from stdin at EOF]-";

            ++size;
            --trail;
        }

        const Byte* bp = encoded;
        require (
          c = Back_Scan_Utf8_Char(&bp, &size)
        );
    }

    return rebChar(c);
}}


//
//  shutdown*: native [  ; Note: DO NOT EXPORT!
//
//  "Shut down the stdio and terminal devices, called on extension unload"
//
//      return: []
//  ]
//
DECLARE_NATIVE(SHUTDOWN_P)
{
    INCLUDE_PARAMS_OF_SHUTDOWN_P;

    Shutdown_Stdio();  // platform-specific teardown (free buffers, etc.)

    return "~";
}
