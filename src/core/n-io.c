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

    REBVAL *value = ARG(value);

    Init_String(D_OUT, Copy_Form_Value(value, 0));

    return R_OUT;
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

    Push_Mold(mo);

    if (REF(only) && IS_BLOCK(ARG(value)))
        SET_MOLD_FLAG(mo, MOLD_FLAG_ONLY);

    Mold_Value(mo, ARG(value));

    Init_String(D_OUT, Pop_Molded_String(mo));

    return R_OUT;
}


//
//  write-stdout: native [
//
//  "Write text to standard output, or raw BINARY! (for control codes / CGI)"
//
//      return: [<opt>]
//      value [string! char! binary!]
//          "Text to write, if a STRING! or CHAR! is converted to OS format"
//  ]
//
REBNATIVE(write_stdout)
{
    INCLUDE_PARAMS_OF_WRITE_STDOUT;

    REBVAL *v = ARG(value);

    if (IS_BINARY(v)) { // raw output
        assert(FALSE); // temporarily disabled
        Prin_OS_String(NULL, 0, OPT_ENC_RAW);
    }
    else if (IS_CHAR(v)) { // useful for `write-stdout newline`, etc.
        Prin_OS_String(&VAL_CHAR(v), 1, OPT_ENC_CRLF_MAYBE);
    }
    else { // string output translated to OS format
        assert(IS_STRING(v));
        Prin_OS_String(
            VAL_UNI_AT(v),
            VAL_LEN_AT(v),
            OPT_ENC_CRLF_MAYBE
        );
    }

    return R_VOID;
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

    RELVAL *v = VAL_ARRAY_AT(ARG(position));
    REBOOL mark = VAL_LOGIC(ARG(mark));

    // Given that VALUE_FLAG_LINE means "put a newline *before* this value is
    // output", there's no value cell on which to put an end-of-line marker
    // at the tail of an array.  Red and R3-Alpha ignore this.  It would be
    // mechanically possible to add an ARRAY_FLAG_XXX for this case, but
    // currently it is thrown out--tell user this will have no effect.
    //
    if (IS_END(v))
        fail ("Attempt to set end-of-line marker at tail of array.");

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
    for (n = 0; NOT_END(v); ++n, ++v) {
        if ((skip != 0) && (n % skip != 0))
            continue;

        if (mark)
            SET_VAL_FLAG(v, VALUE_FLAG_LINE);
        else
            CLEAR_VAL_FLAG(v, VALUE_FLAG_LINE);

        if (skip == 0)
            break;
    }

    Move_Value(D_OUT, ARG(position));
    return R_OUT;
}


//
//  new-line?: native [
//
//  {Returns the state of the new-line marker within a block or group.}
//
//      position [block! group!] "Position to check marker"
//  ]
//
REBNATIVE(new_line_q)
{
    INCLUDE_PARAMS_OF_NEW_LINE_Q;

    if (GET_VAL_FLAG(VAL_ARRAY_AT(ARG(position)), VALUE_FLAG_LINE))
        return R_TRUE;

    return R_FALSE;
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

    REBVAL *ret = D_OUT;
    OS_GET_TIME(D_OUT);

    // However OS-level date and time is plugged into the system, it needs to
    // have enough granularity to give back date, time, and time zone.
    //
    assert(IS_DATE(D_OUT));
    assert(GET_VAL_FLAG(D_OUT, DATE_FLAG_HAS_TIME));
    assert(GET_VAL_FLAG(D_OUT, DATE_FLAG_HAS_ZONE));

    if (NOT(REF(precise))) {
        //
        // The "time" field is measured in nanoseconds, and the historical
        // meaning of not using precise measurement was to use only the
        // seconds portion (with the nanoseconds set to 0).  This achieves
        // that by extracting the seconds and then multiplying by nanoseconds.
        //
        VAL_NANO(ret) = SECS_TO_NANO(VAL_SECS(ret));
    }

    if (REF(utc)) {
        //
        // Say it has a time zone component, but it's 0:00 (as opposed
        // to saying it has no time zone component at all?)
        //
        INIT_VAL_ZONE(ret, 0);
    }
    else if (REF(local)) {
        // Clear out the time zone flag
        //
        CLEAR_VAL_FLAG(ret, DATE_FLAG_HAS_ZONE);
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
            Adjust_Date_Zone(ret, FALSE); // Add time zone, adjust date/time
        }
    }

    REBINT n = -1;

    if (REF(date)) {
        CLEAR_VAL_FLAGS(ret, DATE_FLAG_HAS_TIME | DATE_FLAG_HAS_ZONE);
    }
    else if (REF(time)) {
        VAL_RESET_HEADER(ret, REB_TIME); // reset clears date flags
    }
    else if (REF(zone)) {
        VAL_NANO(ret) = VAL_ZONE(ret) * ZONE_MINS * MIN_SEC;
        VAL_RESET_HEADER(ret, REB_TIME); // reset clears date flags
    }
    else if (REF(weekday))
        n = Week_Day(VAL_DATE(ret));
    else if (REF(yearday))
        n = Julian_Date(VAL_DATE(ret));
    else if (REF(year))
        n = VAL_YEAR(ret);
    else if (REF(month))
        n = VAL_MONTH(ret);
    else if (REF(day))
        n = VAL_DAY(ret);

    if (n > 0)
        Init_Integer(ret, n);

    return R_OUT;
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
        fail (Error_Out_Of_Range(const_KNOWN(v)));

    return cast(REBCNT, msec);
}


//
//  wait: native [
//
//  "Waits for a duration, port, or both."
//
//      value [any-number! time! port! block! blank!]
//      /all
//          "Returns all in a block"
//      /only
//          {only check for ports given in the block to this function}
//  ]
//
REBNATIVE(wait)
{
    INCLUDE_PARAMS_OF_WAIT;

    REBCNT timeout = 0; // in milliseconds
    REBARR *ports = NULL;
    REBINT n = 0;

    Init_Blank(D_OUT);

    RELVAL *val;
    if (IS_BLOCK(ARG(value))) {
        DECLARE_LOCAL (unsafe); // temporary not safe from GC

        if (Reduce_Any_Array_Throws(
            unsafe, ARG(value), REDUCE_FLAG_DROP_BARS
        )){
            Move_Value(D_OUT, unsafe);
            return R_OUT_IS_THROWN;
        }

        ports = VAL_ARRAY(unsafe);
        for (val = ARR_HEAD(ports); NOT_END(val); val++) { // find timeout
            if (Pending_Port(KNOWN(val))) n++;
            if (IS_INTEGER(val)
                || IS_DECIMAL(val)
                || IS_TIME(val)
                )
                break;
        }
        if (IS_END(val)) {
            if (n == 0) return R_BLANK; // has no pending ports!
            else timeout = ALL_BITS; // no timeout provided
            // Init_Blank(val); // no timeout -- BUG: unterminated block in GC
        }
    }
    else
        val = ARG(value);

    if (NOT_END(val)) {
        switch (VAL_TYPE(val)) {
        case REB_INTEGER:
        case REB_DECIMAL:
        case REB_TIME:
            timeout = Milliseconds_From_Value(val);
            break;

        case REB_PORT:
            if (!Pending_Port(KNOWN(val))) return R_BLANK;
            ports = Make_Array(1);
            Append_Value(ports, KNOWN(val));
            // fall thru...
        case REB_BLANK:
            timeout = ALL_BITS; // wait for all windows
            break;

        default:
            fail (Error_Invalid_Arg_Core(val, SPECIFIED));
        }
    }

    // Prevent GC on temp port block:
    // Note: Port block is always a copy of the block.
    if (ports)
        Init_Block(D_OUT, ports);

    // Process port events [stack-move]:
    if (!Wait_Ports(ports, timeout, REF(only))) {
        Sieve_Ports(NULL); // just reset the waked list
        return R_BLANK;
    }
    if (!ports) return R_BLANK;

    // Determine what port(s) waked us:
    Sieve_Ports(ports);

    if (NOT(REF(all))) {
        val = ARR_HEAD(ports);
        if (IS_PORT(val))
            Move_Value(D_OUT, KNOWN(val));
        else
            Init_Blank(D_OUT);
    }

    return R_OUT;
}


//
//  wake-up: native [
//
//  "Awake and update a port with event."
//
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

    REBCTX *port = VAL_CONTEXT(ARG(port));
    FAIL_IF_BAD_PORT(port);

    REBVAL *actor = CTX_VAR(port, STD_PORT_ACTOR);
    if (Is_Native_Port_Actor(actor)) {
        //
        // We don't pass `actor` or `event` in, because we just pass the
        // current call info.  The port action can re-read the arguments.
        //
        Do_Port_Action(frame_, port, SYM_ON_WAKE_UP);
    }

    REBOOL woke_up = TRUE; // start by assuming success

    REBVAL *awake = CTX_VAR(port, STD_PORT_AWAKE);
    if (IS_FUNCTION(awake)) {
        const REBOOL fully = TRUE; // error if not all arguments consumed

        if (Apply_Only_Throws(D_OUT, fully, awake, ARG(event), END))
            fail (Error_No_Catch_For_Throw(D_OUT));

        if (NOT(IS_LOGIC(D_OUT) && VAL_LOGIC(D_OUT)))
            woke_up = FALSE;
    }

    return R_FROM_BOOL(woke_up);
}


//
//  local-to-file: native [
//
//  {Converts a local system file path STRING! to a Rebol FILE! path.}
//
//      return: [file!]
//          {The returned value should be a valid natural FILE! literal}
//      path [string! file!]
//          {Path to convert (by default, only STRING! for type safety)}
//      /only
//          {Convert STRING!s, but copy FILE!s to pass through unmodified}
//  ]
//
REBNATIVE(local_to_file)
{
    INCLUDE_PARAMS_OF_LOCAL_TO_FILE;

    REBVAL *path = ARG(path);
    if (IS_FILE(path)) {
        if (NOT(REF(only)))
            fail ("LOCAL-TO-FILE only passes through FILE! if /ONLY used");

        Init_File(
            D_OUT,
            Copy_Sequence_At_Len( // Copy (callers frequently modify result)
                VAL_SERIES(path),
                VAL_INDEX(path),
                VAL_LEN_AT(path)
            )
        );
        return R_OUT;
    }

    const REBOOL is_dir = FALSE;
    Init_File(
        D_OUT,
        To_REBOL_Path(
            VAL_UNI_AT(path),
            VAL_LEN_AT(path),
            is_dir ? PATH_OPT_SRC_IS_DIR : 0
        )
    );
    return R_OUT;
}


//
//  file-to-local: native [
//
//  {Converts a Rebol FILE! path to a STRING! of the local system file path.}
//
//      return: [string!]
//          {A STRING! like "\foo\bar" is not a "natural" FILE! %\foo\bar}
//      path [file! string!]
//          {Path to convert (by default, only FILE! for type safety)}
//      /only
//          {Convert FILE!s, but copy STRING!s to pass through unmodified}
//      /full
//          {For relative paths, prepends current dir for full path}
//  ]
//
REBNATIVE(file_to_local)
{
    INCLUDE_PARAMS_OF_FILE_TO_LOCAL;

    REBVAL *path = ARG(path);
    if (IS_STRING(path)) {
        if (NOT(REF(only)))
            fail ("FILE-TO-LOCAL only passes through STRING! if /ONLY used");

        Init_String(
            D_OUT,
            Copy_Sequence_At_Len( // Copy (callers frequently modify result)
                VAL_SERIES(path),
                VAL_INDEX(path),
                VAL_LEN_AT(path)
            )
        );
        return R_OUT;
    }

    Init_String(
        D_OUT,
        To_Local_Path(VAL_UNI_AT(path), VAL_LEN_AT(path), REF(full))
    );
    return R_OUT;
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
    else if (NOT(IS_URL(current_path))) {
        //
        // Lousy error, but ATM the user can directly edit system/options.
        // They shouldn't be able to (or if they can, it should be validated)
        //
        fail (current_path);
    }

    // Note the expectation is that WHAT-DIR will return a value that can be
    // mutated by the caller without affecting future calls to WHAT-DIR, so
    // the variable holding the current path must be copied.
    //
    Init_Any_Series_At(
        D_OUT,
        VAL_TYPE(current_path),
        Copy_Sequence(VAL_SERIES(current_path)),
        VAL_INDEX(current_path)
    );

    return R_OUT;
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

        REBOOL success = OS_SET_CURRENT_DIR(arg);

        if (NOT(success))
            fail (arg);
    }

    Move_Value(current_path, arg);

    Move_Value(D_OUT, ARG(path));
    return R_OUT;
}
