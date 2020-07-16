//
//  File: %n-io.c
//  Summary: "native functions for input and output"
//  Section: natives
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

#include "sys-core.h"


/** Helper Functions **************************************************/


//
//  form: native [
//
//  "Converts a value to a human-readable string."
//
//      value [<opt> any-value!]
//          "The value to form"
//  ]
//
REBNATIVE(form)
{
    INCLUDE_PARAMS_OF_FORM;

    return Init_Text(D_OUT, Copy_Form_Value(ARG(value), 0));
}


//
//  mold: native [
//
//  "Converts a value to a REBOL-readable string."
//
//      value [any-value!]
//          "The value to mold"
//      /only
//          {For a block value, mold only its contents, no outer []}
//      /all
//          "Use construction syntax"
//      /flat
//          "No indentation"
//      /limit
//          "Limit to a certain length"
//      amount [integer!]
//  ]
//
REBNATIVE(mold)
{
    INCLUDE_PARAMS_OF_MOLD;

    DECLARE_MOLD (mo);
    if (REF(all))
        SET_MOLD_FLAG(mo, MOLD_FLAG_ALL);
    if (REF(flat))
        SET_MOLD_FLAG(mo, MOLD_FLAG_INDENT);
    if (REF(limit)) {
        SET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT);
        mo->limit = Int32(ARG(amount));
    }

    Push_Mold(mo);

    if (REF(only) and IS_BLOCK(ARG(value)))
        SET_MOLD_FLAG(mo, MOLD_FLAG_ONLY);

    Mold_Value(mo, ARG(value));

    return Init_Text(D_OUT, Pop_Molded_String(mo));
}


//
//  write-stdout: native [
//
//  "Write text to standard output, or raw BINARY! (for control codes / CGI)"
//
//      return: [<opt> void!]
//      value [<blank> text! char! binary!]
//          "Text to write, if a STRING! or CHAR! is converted to OS format"
//  ]
//
REBNATIVE(write_stdout)
{
    INCLUDE_PARAMS_OF_WRITE_STDOUT;

    REBVAL *v = ARG(value);

    if (IS_BINARY(v)) {
        //
        // It is sometimes desirable to write raw binary data to stdout.  e.g.
        // e.g. CGI scripts may be hooked up to stream data for a download,
        // and not want the bytes interpreted in any way.  (e.g. not changed
        // from UTF-8 to wide characters, or not having CR turned into CR LF
        // sequences).
        //
        Prin_OS_String(VAL_BIN_HEAD(v), VAL_LEN_AT(v), OPT_ENC_RAW);
    }
    else if (IS_CHAR(v)) {
        //
        // Useful for `write-stdout newline`, etc.
        //
        // !!! Temporarily just support ASCII codepoints, since making a
        // codepoint out of a string pre-UTF8-everywhere makes a REBUNI string.
        //
        if (VAL_CHAR(v) > 0x7f)
            fail ("non-ASCII CHAR! output temporarily disabled.");
        Prin_OS_String(cast(REBYTE*, &VAL_CHAR(v)), 1, OPT_ENC_0);
    }
    else {
        // !!! Temporary until UTF-8 Everywhere: translate string into UTF-8.
        // We don't put CR LF in, as this is a proxy for the string that won't
        // have CR in it.  (And even if it did, that's only really needed on
        // Windows, which will need to do a UTF-16 transformation anyway...so
        // might as well put the CR codepoints in then.)
        //
        // !!! Don't use mold buffer, because we're passing a raw pointer, and
        // it may be that the print layer runs arbitrary Rebol code that
        // might move that buffer.
        //
        assert(IS_TEXT(v));

        REBSIZ offset;
        REBSIZ size;
        REBSER *temp = Temp_UTF8_At_Managed(&offset, &size, v, VAL_LEN_AT(v));
        PUSH_GC_GUARD(temp);

        Prin_OS_String(BIN_AT(temp, offset), size, OPT_ENC_0);

        DROP_GC_GUARD(temp);
    }

    return Init_Void(D_OUT);
}


//
//  new-line: native [
//
//  {Sets or clears the new-line marker within a block or group.}
//
//      position [block! group!]
//          "Position to change marker (modified)"
//      mark [logic!]
//          "Set TRUE for newline"
//      /all
//          "Set/clear marker to end of series"
//      /skip
//          {Set/clear marker periodically to the end of the series}
//      count [integer!]
//  ]
//
REBNATIVE(new_line)
{
    INCLUDE_PARAMS_OF_NEW_LINE;

    bool mark = VAL_LOGIC(ARG(mark));
    REBVAL *pos = ARG(position);
    REBARR *a = VAL_ARRAY(pos);

    FAIL_IF_READ_ONLY_ARRAY(a);

    Move_Value(D_OUT, pos); // always returns the input position

    RELVAL *item = VAL_ARRAY_AT(pos);

    if (IS_END(item)) { // no value at tail to mark; use bit in array
        if (mark)
            SET_SER_FLAG(a, ARRAY_FLAG_TAIL_NEWLINE);
        else
            CLEAR_SER_FLAG(a, ARRAY_FLAG_TAIL_NEWLINE);
        return D_OUT;
    }

    REBINT skip;
    if (REF(all))
        skip = 1;
    else if (REF(skip)) {
        skip = Int32s(ARG(count), 1);
        if (skip < 1)
            skip = 1;
    }
    else
        skip = 0;

    REBCNT n;
    for (n = 0; NOT_END(item); ++n, ++item) {
        if (skip != 0 and (n % skip != 0))
            continue;

        if (mark)
            SET_VAL_FLAG(item, VALUE_FLAG_NEWLINE_BEFORE);
        else
            CLEAR_VAL_FLAG(item, VALUE_FLAG_NEWLINE_BEFORE);

        if (skip == 0)
            break;
    }

    return D_OUT;
}


//
//  new-line?: native [
//
//  {Returns the state of the new-line marker within a block or group.}
//
//      position [block! group! varargs!] "Position to check marker"
//  ]
//
REBNATIVE(new_line_q)
{
    INCLUDE_PARAMS_OF_NEW_LINE_Q;

    REBVAL *pos = ARG(position);

    REBARR *arr;
    const RELVAL *item;

    if (IS_VARARGS(pos)) {
        REBFRM *f;
        REBVAL *shared;
        if (Is_Frame_Style_Varargs_May_Fail(&f, pos)) {
            if (not f->source->array) {
                //
                // C va_args input to frame, as from the API, but not in the
                // process of using string components which *might* have
                // newlines.  Review edge cases, like:
                //
                //    REBVAL *new_line_q = rebValue(":new-line?");
                //    bool case_one = rebDid("new-line?", "[\n]");
                //    bool case_two = rebDid(new_line_q, "[\n]");
                //
                assert(f->source->index == TRASHED_INDEX);
                return Init_Logic(D_OUT, false);
            }

            arr = f->source->array;
            item = f->value;
        }
        else if (Is_Block_Style_Varargs(&shared, pos)) {
            arr = VAL_ARRAY(shared);
            item = VAL_ARRAY_AT(shared);
        }
        else
            panic ("Bad VARARGS!");
    }
    else {
        assert(IS_GROUP(pos) or IS_BLOCK(pos));
        arr = VAL_ARRAY(pos);
        item = VAL_ARRAY_AT(pos);
    }

    if (NOT_END(item))
        return Init_Logic(
            D_OUT,
            GET_VAL_FLAG(item, VALUE_FLAG_NEWLINE_BEFORE)
        );

    return Init_Logic(
        D_OUT,
        GET_SER_FLAG(arr, ARRAY_FLAG_TAIL_NEWLINE)
    );
}


//
//  now: native [
//
//  "Returns current date and time with timezone adjustment."
//
//      /year
//          "Returns year only"
//      /month
//          "Returns month only"
//      /day
//          "Returns day of the month only"
//      /time
//          "Returns time only"
//      /zone
//          "Returns time zone offset from UCT (GMT) only"
//      /date
//          "Returns date only"
//      /weekday
//          {Returns day of the week as integer (Monday is day 1)}
//      /yearday
//          "Returns day of the year (Julian)"
//      /precise
//          "High precision time"
//      /utc
//          "Universal time (zone +0:00)"
//      /local
//          "Give time in current zone without including the time zone"
//  ]
//
REBNATIVE(now)
{
    INCLUDE_PARAMS_OF_NOW;

    REBVAL *timestamp = OS_GET_TIME();

    // However OS-level date and time is plugged into the system, it needs to
    // have enough granularity to give back date, time, and time zone.
    //
    assert(IS_DATE(timestamp));
    assert(GET_VAL_FLAG(timestamp, DATE_FLAG_HAS_TIME));
    assert(GET_VAL_FLAG(timestamp, DATE_FLAG_HAS_ZONE));

    Move_Value(D_OUT, timestamp);
    rebRelease(timestamp);

    if (not REF(precise)) {
        //
        // The "time" field is measured in nanoseconds, and the historical
        // meaning of not using precise measurement was to use only the
        // seconds portion (with the nanoseconds set to 0).  This achieves
        // that by extracting the seconds and then multiplying by nanoseconds.
        //
        VAL_NANO(D_OUT) = SECS_TO_NANO(VAL_SECS(D_OUT));
    }

    if (REF(utc)) {
        //
        // Say it has a time zone component, but it's 0:00 (as opposed
        // to saying it has no time zone component at all?)
        //
        INIT_VAL_ZONE(D_OUT, 0);
    }
    else if (REF(local)) {
        //
        // Clear out the time zone flag
        //
        CLEAR_VAL_FLAG(D_OUT, DATE_FLAG_HAS_ZONE);
    }
    else {
        if (
            REF(year)
            || REF(month)
            || REF(day)
            || REF(time)
            || REF(date)
            || REF(weekday)
            || REF(yearday)
        ){
            const bool to_utc = false;
            Adjust_Date_Zone(D_OUT, to_utc); // Add timezone, adjust date/time
        }
    }

    REBINT n = -1;

    if (REF(date)) {
        CLEAR_VAL_FLAGS(D_OUT, DATE_FLAG_HAS_TIME | DATE_FLAG_HAS_ZONE);
    }
    else if (REF(time)) {
        RESET_VAL_HEADER(D_OUT, REB_TIME); // reset clears date flags
    }
    else if (REF(zone)) {
        VAL_NANO(D_OUT) = VAL_ZONE(D_OUT) * ZONE_MINS * MIN_SEC;
        RESET_VAL_HEADER(D_OUT, REB_TIME); // reset clears date flags
    }
    else if (REF(weekday))
        n = Week_Day(VAL_DATE(D_OUT));
    else if (REF(yearday))
        n = Julian_Date(VAL_DATE(D_OUT));
    else if (REF(year))
        n = VAL_YEAR(D_OUT);
    else if (REF(month))
        n = VAL_MONTH(D_OUT);
    else if (REF(day))
        n = VAL_DAY(D_OUT);

    if (n > 0)
        Init_Integer(D_OUT, n);

    return D_OUT;
}


//
//  Milliseconds_From_Value: C
//
// Note that this routine is used by the SLEEP extension, as well as by WAIT.
//
REBCNT Milliseconds_From_Value(const RELVAL *v) {
    REBINT msec;

    switch (VAL_TYPE(v)) {
    case REB_INTEGER:
        msec = 1000 * Int32(v);
        break;

    case REB_DECIMAL:
        msec = cast(REBINT, 1000 * VAL_DECIMAL(v));
        break;

    case REB_TIME:
        msec = cast(REBINT, VAL_NANO(v) / (SEC_SEC / 1000));
        break;

    default:
        panic (NULL); // avoid uninitialized msec warning
    }

    if (msec < 0)
        fail (Error_Out_Of_Range(KNOWN(v)));

    return cast(REBCNT, msec);
}


//
//  wait: native [
//
//  "Waits for a duration, port, or both."
//
//      value [<opt> any-number! time! port! block!]
//      /all "Returns all in a block"
//      /only "only check for ports given in the block to this function"
//  ]
//
REBNATIVE(wait)
{
    INCLUDE_PARAMS_OF_WAIT;

    REBCNT timeout = 0; // in milliseconds
    REBARR *ports = NULL;
    REBINT n = 0;


    RELVAL *val;
    if (not IS_BLOCK(ARG(value)))
        val = ARG(value);
    else {
        REBDSP dsp_orig = DSP;
        if (Reduce_To_Stack_Throws(
            D_OUT,
            ARG(value),
            REDUCE_MASK_NONE
        )){
            return R_THROWN;
        }

        // !!! This takes the stack array and creates an unmanaged array from
        // it, which ends up being put into a value and becomes managed.  So
        // it has to be protected.
        //
        ports = Pop_Stack_Values(dsp_orig);

        for (val = ARR_HEAD(ports); NOT_END(val); val++) { // find timeout
            if (Pending_Port(KNOWN(val)))
                ++n;

            if (IS_INTEGER(val) or IS_DECIMAL(val) or IS_TIME(val))
                break;
        }
        if (IS_END(val)) {
            if (n == 0) {
                Free_Unmanaged_Array(ports);
                return nullptr; // has no pending ports!
            }
            timeout = ALL_BITS; // no timeout provided
        }
    }

    if (NOT_END(val)) {
        switch (VAL_TYPE(val)) {
        case REB_INTEGER:
        case REB_DECIMAL:
        case REB_TIME:
            timeout = Milliseconds_From_Value(val);
            break;

        case REB_PORT:
            if (not Pending_Port(KNOWN(val)))
                return nullptr;
            ports = Make_Arr(1);
            Append_Value(ports, KNOWN(val));
            timeout = ALL_BITS;
            break;

        case REB_BLANK:
            timeout = ALL_BITS; // wait for all windows
            break;

        default:
            fail (Error_Invalid_Core(val, SPECIFIED));
        }
    }

    // Prevent GC on temp port block:
    // Note: Port block is always a copy of the block.
    //
    if (ports)
        Init_Block(D_OUT, ports);

    // Process port events [stack-move]:
    if (Wait_Ports_Throws(D_OUT, ports, timeout, REF(only)))
        return R_THROWN;

    assert(IS_LOGIC(D_OUT));

    if (IS_FALSEY(D_OUT)) { // timeout
        Sieve_Ports(NULL); // just reset the waked list
        return nullptr;
    }

    if (not ports)
        return nullptr;

    // Determine what port(s) waked us:
    Sieve_Ports(ports);

    if (not REF(all)) {
        val = ARR_HEAD(ports);
        if (not IS_PORT(val))
            return nullptr;

        Move_Value(D_OUT, KNOWN(val));
    }

    return D_OUT;
}


//
//  wake-up: native [
//
//  "Awake and update a port with event."
//
//      return: [logic!]
//      port [port!]
//      event [event!]
//  ]
//
REBNATIVE(wake_up)
//
// Calls port update for native actors.
// Calls port awake function.
{
    INCLUDE_PARAMS_OF_WAKE_UP;

    FAIL_IF_BAD_PORT(ARG(port));

    REBCTX *ctx = VAL_CONTEXT(ARG(port));

    REBVAL *actor = CTX_VAR(ctx, STD_PORT_ACTOR);
    if (Is_Native_Port_Actor(actor)) {
        //
        // We don't pass `actor` or `event` in, because we just pass the
        // current call info.  The port action can re-read the arguments.
        //
        // !!! Most of the R3-Alpha event model is around just as "life
        // support".  Added assertion and convention here that this call
        // doesn't throw or return meaningful data... (?)
        //
        DECLARE_LOCAL (verb);
        Init_Word(verb, Canon(SYM_ON_WAKE_UP));
        const REBVAL *r = Do_Port_Action(frame_, ARG(port), verb);
        assert(IS_BAR(r));
        UNUSED(r);
    }

    bool woke_up = true; // start by assuming success

    REBVAL *awake = CTX_VAR(ctx, STD_PORT_AWAKE);
    if (IS_ACTION(awake)) {
        const bool fully = true; // error if not all arguments consumed

        if (Apply_Only_Throws(D_OUT, fully, awake, ARG(event), rebEND))
            fail (Error_No_Catch_For_Throw(D_OUT));

        if (not (IS_LOGIC(D_OUT) and VAL_LOGIC(D_OUT)))
            woke_up = false;
    }

    return Init_Logic(D_OUT, woke_up);
}


//
//  local-to-file: native [
//
//  {Converts a local system file path TEXT! to a Rebol FILE! path.}
//
//      return: [<opt> file!]
//          {The returned value should be a valid natural FILE! literal}
//      path [<blank> text! file!]
//          {Path to convert (by default, only TEXT! for type safety)}
//      /pass
//          {Convert TEXT!, but pass thru FILE!, assuming it's canonized}
//      /dir
//          {Ensure input path is treated as a directory}
//  ]
//
REBNATIVE(local_to_file)
{
    INCLUDE_PARAMS_OF_LOCAL_TO_FILE;

    REBVAL *path = ARG(path);
    if (IS_FILE(path)) {
        if (not REF(pass))
            fail ("LOCAL-TO-FILE only passes through FILE! if /PASS used");

        return Init_File(
            D_OUT,
            Copy_Sequence_At_Len( // Copy (callers frequently modify result)
                VAL_SERIES(path),
                VAL_INDEX(path),
                VAL_LEN_AT(path)
            )
        );
    }

    return Init_File(
        D_OUT,
        To_REBOL_Path(path, REF(dir) ? PATH_OPT_SRC_IS_DIR : 0)
    );
}


//
//  file-to-local: native [
//
//  {Converts a Rebol FILE! path to TEXT! of the local system file path}
//
//      return: [<opt> text!]
//          {A TEXT! like "\foo\bar" is not a "natural" FILE! %\foo\bar}
//      path [<blank> file! text!]
//          {Path to convert (by default, only FILE! for type safety)}
//      /pass
//          {Convert FILE!s, but pass thru TEXT!, assuming it's local}
//      /full
//          {For relative paths, prepends current dir for full path}
//      /no-tail-slash
//          {For directories, do not add a slash or backslash to the tail}
//      /wild
//          {For directories, add a * to the end}
//  ]
//
REBNATIVE(file_to_local)
{
    INCLUDE_PARAMS_OF_FILE_TO_LOCAL;

    REBVAL *path = ARG(path);
    if (IS_TEXT(path)) {
        if (not REF(pass))
            fail ("FILE-TO-LOCAL only passes through STRING! if /PASS used");

        return Init_Text(
            D_OUT,
            Copy_Sequence_At_Len( // Copy (callers frequently modify result)
                VAL_SERIES(path),
                VAL_INDEX(path),
                VAL_LEN_AT(path)
            )
        );
    }

    return Init_Text(
        D_OUT,
        To_Local_Path(
            path,
            REB_FILETOLOCAL_0
                | (REF(full) ? REB_FILETOLOCAL_FULL : 0)
                | (REF(no_tail_slash) ? REB_FILETOLOCAL_NO_TAIL_SLASH : 0)
                | (REF(wild) ? REB_FILETOLOCAL_WILD : 0)
        )
    );
}


//
//  what-dir: native [
//  "Returns the current directory path."
//      ; No arguments
//  ]
//
REBNATIVE(what_dir)
{
    REBVAL *current_path = Get_System(SYS_OPTIONS, OPTIONS_CURRENT_PATH);

    if (IS_FILE(current_path) || IS_BLANK(current_path)) {
        //
        // !!! Because of the need to track a notion of "current path" which
        // could be a URL! as well as a FILE!, the state is stored in the
        // system options.  For now--however--it is "duplicate" in the case
        // of a FILE!, because the OS has its own tracked state.  We let the
        // OS state win for files if they have diverged somehow--because the
        // code was already here and it would be more compatible.  But
        // reconsider the duplication.

        REBVAL *refresh = OS_GET_CURRENT_DIR();
        Move_Value(current_path, refresh);
        rebRelease(refresh);
    }
    else if (not IS_URL(current_path)) {
        //
        // Lousy error, but ATM the user can directly edit system/options.
        // They shouldn't be able to (or if they can, it should be validated)
        //
        fail (Error_Invalid(current_path));
    }

    // Note the expectation is that WHAT-DIR will return a value that can be
    // mutated by the caller without affecting future calls to WHAT-DIR, so
    // the variable holding the current path must be copied.
    //
    return Init_Any_Series_At(
        D_OUT,
        VAL_TYPE(current_path),
        Copy_Sequence_Core(VAL_SERIES(current_path), NODE_FLAG_MANAGED),
        VAL_INDEX(current_path)
    );
}


//
//  change-dir: native [
//
//  {Changes the current path (where scripts with relative paths will be run).}
//
//      path [file! url!]
//  ]
//
REBNATIVE(change_dir)
{
    INCLUDE_PARAMS_OF_CHANGE_DIR;

    REBVAL *arg = ARG(path);
    REBVAL *current_path = Get_System(SYS_OPTIONS, OPTIONS_CURRENT_PATH);

    if (IS_URL(arg)) {
        // There is no directory listing protocol for HTTP (although this
        // needs to be methodized to work for SFTP etc.)  So this takes
        // your word for it for the moment that it's a valid "directory".
        //
        // !!! Should it at least check for a trailing `/`?
    }
    else {
        assert(IS_FILE(arg));

        Check_Security(Canon(SYM_FILE), POL_EXEC, arg);

        bool success = OS_SET_CURRENT_DIR(arg);

        if (not success)
            fail (Error_Invalid(arg));
    }

    Move_Value(current_path, arg);

    RETURN (ARG(path));
}
