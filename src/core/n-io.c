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

    REB_MOLD mo;
    CLEARS(&mo);
    if (REF(all)) SET_FLAG(mo.opts, MOPT_MOLD_ALL);
    if (REF(flat)) SET_FLAG(mo.opts, MOPT_INDENT);

    Push_Mold(&mo);

    if (REF(only) && IS_BLOCK(ARG(value))) SET_FLAG(mo.opts, MOPT_ONLY);

    Mold_Value(&mo, ARG(value), TRUE);

    Init_String(D_OUT, Pop_Molded_String(&mo));

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
        Prin_OS_String(VAL_BIN_AT(v), VAL_LEN_AT(v), OPT_ENC_RAW);
    }
    else if (IS_CHAR(v)) { // useful for `write-stdout newline`, etc.
        Prin_OS_String(&VAL_CHAR(v), 1, OPT_ENC_UNISRC | OPT_ENC_CRLF_MAYBE);
    }
    else { // string output translated to OS format
        assert(IS_STRING(v));
        if (VAL_BYTE_SIZE(v))
            Prin_OS_String(VAL_BIN_AT(v), VAL_LEN_AT(v), OPT_ENC_CRLF_MAYBE);
        else
            Prin_OS_String(
                VAL_UNI_AT(v),
                VAL_LEN_AT(v),
                OPT_ENC_UNISRC | OPT_ENC_CRLF_MAYBE
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
//      mark
//          "Set TRUE for newline"
//      /all
//          "Set/clear marker to end of series"
//      /skip
//          {Set/clear marker periodically to the end of the series}
//      size [integer!]
//  ]
//
REBNATIVE(new_line)
{
    INCLUDE_PARAMS_OF_NEW_LINE;

    RELVAL *value = VAL_ARRAY_AT(ARG(position));
    REBOOL mark = IS_CONDITIONAL_TRUE(ARG(mark));
    REBINT skip = 0;
    REBCNT n;

    if (REF(all)) skip = 1;

    if (REF(skip)) {
        skip = Int32s(ARG(size), 1);
        if (skip < 1) skip = 1;
    }

    for (n = 0; NOT_END(value); n++, value++) {
        if ((skip != 0) && (n % skip != 0)) continue;

        if (mark)
            SET_VAL_FLAG(value, VALUE_FLAG_LINE);
        else
            CLEAR_VAL_FLAG(value, VALUE_FLAG_LINE);

        if (skip == 0) break;
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
//          "Universal time (no zone)"
//  ]
//
REBNATIVE(now)
{
    INCLUDE_PARAMS_OF_NOW;

    REBVAL *ret = D_OUT;
    OS_GET_TIME(D_OUT);

    if (NOT(REF(precise))) {
        //
        // The "time" field is measured in nanoseconds, and the historical
        // meaning of not using precise measurement was to use only the
        // seconds portion (with the nanoseconds set to 0).  This achieves
        // that by extracting the seconds and then multiplying by nanoseconds.
        //
        VAL_TIME(ret) = TIME_SEC(VAL_SECS(ret));
    }

    if (REF(utc)) {
        VAL_ZONE(ret) = 0;
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
        VAL_TIME(ret) = NO_TIME;
        VAL_ZONE(ret) = 0;
    }
    else if (REF(time)) {
        VAL_RESET_HEADER(ret, REB_TIME);
    }
    else if (REF(zone)) {
        VAL_RESET_HEADER(ret, REB_TIME);
        VAL_TIME(ret) = VAL_ZONE(ret) * ZONE_MINS * MIN_SEC;
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
        SET_INTEGER(ret, n);

    return R_OUT;
}



static REBCNT Milliseconds_From_Value(const RELVAL *v) {
    REBINT msec;

    switch (VAL_TYPE(v)) {
    case REB_INTEGER:
        msec = 1000 * Int32(v);
        break;

    case REB_DECIMAL:
        msec = cast(REBINT, 1000 * VAL_DECIMAL(v));
        break;

    case REB_TIME:
        msec = cast(REBINT, VAL_TIME(v) / (SEC_SEC / 1000));
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

    SET_BLANK(D_OUT);

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
            // SET_BLANK(val); // no timeout -- BUG: unterminated block in GC
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
            SET_BLANK(D_OUT);
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
        Do_Port_Action(frame_, port, SYM_UPDATE);
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
//  to-rebol-file: native [
//
//  {Converts a local system file path to a REBOL file path.}
//
//      path [file! string!]
//  ]
//
REBNATIVE(to_rebol_file)
{
    INCLUDE_PARAMS_OF_TO_REBOL_FILE;

    REBVAL *arg = ARG(path);

    REBSER *ser = Value_To_REBOL_Path(arg, FALSE);
    if (ser == NULL)
        fail (arg);

    Init_File(D_OUT, ser);
    return R_OUT;
}


//
//  to-local-file: native [
//
//  {Converts a REBOL file path to the local system file path.}
//
//      path [file! string!]
//      /full
//          {Prepends current dir for full path (for relative paths only)}
//  ]
//
REBNATIVE(to_local_file)
{
    INCLUDE_PARAMS_OF_TO_LOCAL_FILE;

    REBVAL *arg = ARG(path);

    REBSER *ser = Value_To_Local_Path(arg, REF(full));
    if (ser == NULL)
        fail (arg);

    Init_String(D_OUT, ser);
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
    REBSER *ser;
    REBCHR *lpath;
    REBINT len;

    REBVAL *current_path = Get_System(SYS_OPTIONS, OPTIONS_CURRENT_PATH);

    // !!! Because of the need to track a notion of "current path" which
    // could be a URL! as well as a FILE!, the state is stored in the system
    // options.  For now--however--it is "duplicate" in the case of a FILE!,
    // because the OS has its own tracked state.  We let the OS state win
    // for files if they have diverged somehow--because the code was already
    // here and it would be more compatible.  But reconsider the duplication.

    if (IS_URL(current_path)) {
        Move_Value(D_OUT, current_path);
    }
    else if (IS_FILE(current_path) || IS_BLANK(current_path)) {
        len = OS_GET_CURRENT_DIR(&lpath);

        // allocates extra for end `/`
        ser = To_REBOL_Path(
            lpath, len, PATH_OPT_SRC_IS_DIR | (OS_WIDE ? PATH_OPT_UNI_SRC : 0)
        );

        OS_FREE(lpath);

        Init_File(D_OUT, ser);
        Move_Value(current_path, D_OUT); // !!! refresh if they diverged
    }
    else {
        // Lousy error, but ATM the user can directly edit system/options.
        // They shouldn't be able to (or if they can, it should be validated)
        //
        fail (current_path);
    }

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

        REBSER *ser = Value_To_OS_Path(arg, TRUE);
        if (ser == NULL)
            fail (arg);

        DECLARE_LOCAL (val);
        Init_String(val, ser); // may be unicode or utf-8
        Check_Security(Canon(SYM_FILE), POL_EXEC, val);

        if (!OS_SET_CURRENT_DIR(SER_HEAD(REBCHR, ser)))
            fail (arg);
    }

    Move_Value(current_path, arg);

    Move_Value(D_OUT, ARG(path));
    return R_OUT;
}


//
//  browse: native [
//
//  "Open web browser to a URL or local file."
//
//      return: [<opt>]
//      location [url! file! blank!]
//  ]
//
REBNATIVE(browse)
{
    INCLUDE_PARAMS_OF_BROWSE;

    REBVAL *location = ARG(location);

    Check_Security(Canon(SYM_BROWSE), POL_EXEC, location);

    if (IS_BLANK(location))
        return R_VOID;

    // !!! By passing NULL we don't get backing series to protect!
    //
    REBCHR *url = Val_Str_To_OS_Managed(NULL, location);

    REBINT r = OS_BROWSE(url, 0);

    if (r != 0) {
        Make_OS_Error(D_OUT, r);
        fail (Error_Call_Fail_Raw(D_OUT));
    }

    return R_VOID;

}


//
//  call: native [
//
//  "Run another program; return immediately (unless /WAIT)."
//
//      command [string! block! file!]
//          {An OS-local command line (quoted as necessary), a block with
//          arguments, or an executable file}
//      /wait
//          "Wait for command to terminate before returning"
//      /console
//          "Runs command with I/O redirected to console"
//      /shell
//          "Forces command to be run from shell"
//      /info
//          "Returns process information object"
//      /input
//          "Redirects stdin to in"
//      in [string! binary! file! blank!]
//      /output
//          "Redirects stdout to out"
//      out [string! binary! file! blank!]
//      /error
//          "Redirects stderr to err"
//      err [string! binary! file! blank!]
//  ]
//
REBNATIVE(call)
//
// !!! Parameter usage may require WAIT mode even if not explicitly requested.
// /WAIT should be default, with /ASYNC (or otherwise) as exception!
{
    INCLUDE_PARAMS_OF_CALL;

#define INHERIT_TYPE 0
#define NONE_TYPE 1
#define STRING_TYPE 2
#define FILE_TYPE 3
#define BINARY_TYPE 4

#define FLAG_WAIT 1
#define FLAG_CONSOLE 2
#define FLAG_SHELL 4
#define FLAG_INFO 8

    REBINT r;
    REBVAL *arg = ARG(command);
    REBU64 pid = MAX_U64; // Was REBI64 of -1, but OS_CREATE_PROCESS wants u64
    u32 flags = 0;

    // We synthesize the argc and argv from our REBVAL arg, and in the
    // process we may need to do dynamic allocations of argc strings.  In
    // Rebol this is always done by making a series, and if those series
    // are managed then we need to keep them SAVEd from the GC for the
    // duration they will be used.  Due to an artifact of the current
    // implementation, FILE! and STRING! turned into OS-compatible character
    // representations must be managed...so we need to save them over
    // the duration of the call.  We hold the pointers to remember to unsave.
    //
    int argc;
    const REBCHR **argv;
    REBCHR *cmd;
    REBSER *argv_ser = NULL;
    REBSER *argv_saved_sers = NULL;
    REBSER *cmd_ser = NULL;

    REBVAL *input = NULL;
    REBVAL *output = NULL;
    REBVAL *err = NULL;

    // Sometimes OS_CREATE_PROCESS passes back a input/output/err pointers,
    // and sometimes it expects one as input.  If it expects one as input
    // then we may have to transform the REBVAL into pointer data the OS
    // expects.  If we do so then we have to clean up after that transform.
    // (That cleanup could be just a Free_Series(), but an artifact of
    // implementation forces us to use managed series hence SAVE/UNSAVE)
    //
    REBSER *input_ser = NULL;
    REBSER *output_ser = NULL;
    REBSER *err_ser = NULL;

    // Pointers to the string data buffers corresponding to input/output/err,
    // which may be the data of the expanded path series, the data inside
    // of a STRING!, or NULL if NONE! or default of INHERIT_TYPE
    //
    char *os_input = NULL;
    char *os_output = NULL;
    char *os_err = NULL;

    int input_type = INHERIT_TYPE;
    int output_type = INHERIT_TYPE;
    int err_type = INHERIT_TYPE;

    REBCNT input_len = 0;
    REBCNT output_len = 0;
    REBCNT err_len = 0;

    int exit_code = 0;

    Check_Security(Canon(SYM_CALL), POL_EXEC, arg);

    // If input_ser is set, it will be both managed and saved
    //
    if (REF(input)) {
        REBVAL *param = ARG(in);
        input = param;
        if (IS_STRING(param)) {
            input_type = STRING_TYPE;
            os_input = cast(char*, Val_Str_To_OS_Managed(&input_ser, param));
            PUSH_GUARD_SERIES(input_ser);
            input_len = VAL_LEN_AT(param);
        }
        else if (IS_BINARY(param)) {
            input_type = BINARY_TYPE;
            os_input = s_cast(VAL_BIN_AT(param));
            input_len = VAL_LEN_AT(param);
        }
        else if (IS_FILE(param)) {
            input_type = FILE_TYPE;
            input_ser = Value_To_OS_Path(param, FALSE);
            MANAGE_SERIES(input_ser);
            PUSH_GUARD_SERIES(input_ser);
            os_input = SER_HEAD(char, input_ser);
            input_len = SER_LEN(input_ser);
        }
        else if (IS_BLANK(param)) {
            input_type = NONE_TYPE;
        }
        else
            fail (param);
    }

    // Note that os_output is actually treated as an *input* parameter in the
    // case of a FILE! by OS_CREATE_PROCESS.  (In the other cases it is a
    // pointer of the returned data, which will need to be freed with
    // OS_FREE().)  Hence the case for FILE! is handled specially, where the
    // output_ser must be unsaved instead of OS_FREE()d.
    //
    if (REF(output)) {
        REBVAL *param = ARG(out);
        output = param;
        if (IS_STRING(param)) {
            output_type = STRING_TYPE;
        }
        else if (IS_BINARY(param)) {
            output_type = BINARY_TYPE;
        }
        else if (IS_FILE(param)) {
            output_type = FILE_TYPE;
            output_ser = Value_To_OS_Path(param, FALSE);
            MANAGE_SERIES(output_ser);
            PUSH_GUARD_SERIES(output_ser);
            os_output = SER_HEAD(char, output_ser);
            output_len = SER_LEN(output_ser);
        }
        else if (IS_BLANK(param)) {
            output_type = NONE_TYPE;
        }
        else
            fail (param);
    }

    (void)input; // suppress unused warning but keep variable

    // Error case...same note about FILE! case as with Output case above
    //
    if (REF(error)) {
        REBVAL *param = ARG(err);
        err = param;
        if (IS_STRING(param)) {
            err_type = STRING_TYPE;
        }
        else if (IS_BINARY(param)) {
            err_type = BINARY_TYPE;
        }
        else if (IS_FILE(param)) {
            err_type = FILE_TYPE;
            err_ser = Value_To_OS_Path(param, FALSE);
            MANAGE_SERIES(err_ser);
            PUSH_GUARD_SERIES(err_ser);
            os_err = SER_HEAD(char, err_ser);
            err_len = SER_LEN(err_ser);
        }
        else if (IS_BLANK(param)) {
            err_type = NONE_TYPE;
        }
        else
            fail (param);
    }

    if (
        REF(wait) ||
        (
            input_type == STRING_TYPE
            || input_type == BINARY_TYPE
            || output_type == STRING_TYPE
            || output_type == BINARY_TYPE
            || err_type == STRING_TYPE
            || err_type == BINARY_TYPE
        ) // I/O redirection implies /WAIT

    ){
        flags |= FLAG_WAIT;
    }

    if (REF(console))
        flags |= FLAG_CONSOLE;

    if (REF(shell))
        flags |= FLAG_SHELL;

    if (REF(info))
        flags |= FLAG_INFO;

    // Translate the first parameter into an `argc` and a pointer array for
    // `argv[]`.  The pointer array is backed by `argv_series` which must
    // be freed after we are done using it.
    //
    if (IS_STRING(arg)) {
        // `call {foo bar}` => execute %"foo bar"

        // !!! Interpreting string case as an invocation of %foo with argument
        // "bar" has been requested and seems more suitable.  Question is
        // whether it should go through the shell parsing to do so.

        cmd = Val_Str_To_OS_Managed(&cmd_ser, arg);
        PUSH_GUARD_SERIES(cmd_ser);

        argc = 1;
        argv_ser = Make_Series(argc + 1, sizeof(REBCHR*));
        argv = SER_HEAD(const REBCHR*, argv_ser);

        argv[0] = cmd;
        // Already implicitly SAVEd by cmd_ser, no need for argv_saved_sers

        argv[argc] = NULL;
    }
    else if (IS_BLOCK(arg)) {
        // `call ["foo" "bar"]` => execute %foo with arg "bar"

        int i;

        cmd = NULL;
        argc = VAL_LEN_AT(arg);

        if (argc <= 0) fail (Error_Too_Short_Raw());

        argv_ser = Make_Series(argc + 1, sizeof(REBCHR*));
        argv_saved_sers = Make_Series(argc, sizeof(REBSER*));
        argv = SER_HEAD(const REBCHR*, argv_ser);
        for (i = 0; i < argc; i ++) {
            RELVAL *param = VAL_ARRAY_AT_HEAD(arg, i);
            if (IS_STRING(param)) {
                REBSER *ser;
                argv[i] = Val_Str_To_OS_Managed(&ser, KNOWN(param));
                PUSH_GUARD_SERIES(ser);
                SER_HEAD(REBSER*, argv_saved_sers)[i] = ser;
            }
            else if (IS_FILE(param)) {
                REBSER *path = Value_To_OS_Path(KNOWN(param), FALSE);
                argv[i] = SER_HEAD(REBCHR, path);

                MANAGE_SERIES(path);
                PUSH_GUARD_SERIES(path);
                SER_HEAD(REBSER*, argv_saved_sers)[i] = path;
            }
            else
                fail (Error_Invalid_Arg_Core(param, VAL_SPECIFIER(arg)));
        }
        argv[argc] = NULL;
    }
    else if (IS_FILE(arg)) {
        // `call %"foo bar"` => execute %"foo bar"

        REBSER *path = Value_To_OS_Path(arg, FALSE);

        cmd = NULL;
        argc = 1;
        argv_ser = Make_Series(argc + 1, sizeof(REBCHR*));
        argv_saved_sers = Make_Series(argc, sizeof(REBSER*));

        argv = SER_HEAD(const REBCHR*, argv_ser);

        argv[0] = SER_HEAD(REBCHR, path);
        MANAGE_SERIES(path);
        PUSH_GUARD_SERIES(path);
        SER_HEAD(REBSER*, argv_saved_sers)[0] = path;

        argv[argc] = NULL;
    }
    else
        fail (arg);

    r = OS_CREATE_PROCESS(
        cmd, argc, argv,
        flags, &pid, &exit_code,
        input_type, os_input, input_len,
        output_type, &os_output, &output_len,
        err_type, &os_err, &err_len
    );

    // Call may not succeed if r != 0, but we still have to run cleanup
    // before reporting any error...
    //
    if (argv_saved_sers) {
        int i = argc;
        assert(argc > 0);
        do {
            // Count down: must unsave the most recently saved series first!
            DROP_GUARD_SERIES(*SER_AT(REBSER*, argv_saved_sers, i - 1));
            --i;
        } while (i != 0);
        Free_Series(argv_saved_sers);
    }
    if (cmd_ser) DROP_GUARD_SERIES(cmd_ser);
    Free_Series(argv_ser); // Unmanaged, so we can free it

    if (output_type == STRING_TYPE) {
        if (output != NULL
            && output_len > 0) {
            // !!! Somewhat inefficient: should there be Append_OS_Str?
            REBSER *ser = Copy_OS_Str(os_output, output_len);
            Append_String(VAL_SERIES(output), ser, 0, SER_LEN(ser));
            OS_FREE(os_output);
            Free_Series(ser);
        }
    } else if (output_type == BINARY_TYPE) {
        if (output != NULL
            && output_len > 0) {
            Append_Unencoded_Len(VAL_SERIES(output), os_output, output_len);
            OS_FREE(os_output);
        }
    }

    if (err_type == STRING_TYPE) {
        if (err != NULL
            && err_len > 0) {
            // !!! Somewhat inefficient: should there be Append_OS_Str?
            REBSER *ser = Copy_OS_Str(os_err, err_len);
            Append_String(VAL_SERIES(err), ser, 0, SER_LEN(ser));
            OS_FREE(os_err);
            Free_Series(ser);
        }
    } else if (err_type == BINARY_TYPE) {
        if (err != NULL
            && err_len > 0) {
            Append_Unencoded_Len(VAL_SERIES(err), os_err, err_len);
            OS_FREE(os_err);
        }
    }

    // If we used (and possibly created) a series for input/output/err, then
    // that series was managed and saved from GC.  Unsave them now.  Note
    // backwardsness: must unsave the most recently saved series first!!
    //
    if (err_ser) DROP_GUARD_SERIES(err_ser);
    if (output_ser) DROP_GUARD_SERIES(output_ser);
    if (input_ser) DROP_GUARD_SERIES(input_ser);

    if (REF(info)) {
        REBCTX *info = Alloc_Context(REB_OBJECT, 2);

        SET_INTEGER(Append_Context(info, NULL, Canon(SYM_ID)), pid);
        if (REF(wait))
            SET_INTEGER(
                Append_Context(info, NULL, Canon(SYM_EXIT_CODE)),
                exit_code
            );

        Init_Object(D_OUT, info);
        return R_OUT;
    }

    if (r != 0) {
        Make_OS_Error(D_OUT, r);
        fail (Error_Call_Fail_Raw(D_OUT));
    }

    // We may have waited even if they didn't ask us to explicitly, but
    // we only return a process ID if /WAIT was not explicitly used
    //
    if (REF(wait))
        SET_INTEGER(D_OUT, exit_code);
    else
        SET_INTEGER(D_OUT, pid);

    return R_OUT;
}


//
//  String_List_To_Array: C
//
// Convert a series of null terminated strings to an array of strings
// separated with '='.
//
static REBARR *String_List_To_Array(REBCHR *str)
{
    REBCNT n;
    REBCNT len = 0;
    REBCHR *start = str;
    REBCHR *eq;
    REBARR *array;

    while ((n = OS_STRLEN(str))) {
        len++;
        str += n + 1; // next
    }

    array = Make_Array(len * 2);

    str = start;
    while ((eq = OS_STRCHR(str+1, '=')) && (n = OS_STRLEN(str))) {
        Init_String(Alloc_Tail_Array(array), Copy_OS_Str(str, eq - str));
        Init_String(
            Alloc_Tail_Array(array), Copy_OS_Str(eq + 1, n - (eq - str) - 1)
        );
        str += n + 1; // next
    }

    return array;
}


//
//  Block_To_String_List: C
//
// Convert block of values to a string that holds
// a series of null terminated strings, followed
// by a final terminating string.
//
REBSER *Block_To_String_List(REBVAL *blk)
{
    RELVAL *value;

    REB_MOLD mo;
    CLEARS(&mo);

    Push_Mold(&mo);

    for (value = VAL_ARRAY_AT(blk); NOT_END(value); value++) {
        Mold_Value(&mo, value, FALSE);
        Append_Codepoint_Raw(mo.series, '\0');
    }
    Append_Codepoint_Raw(mo.series, '\0');

    return Pop_Molded_String(&mo);
}


//
//  File_List_To_Array: C
//
// Convert file directory and file name list to block.
//
static REBARR *File_List_To_Array(const REBCHR *str)
{
    REBCNT n;
    REBCNT len = 0;
    const REBCHR *start = str;
    REBARR *blk;
    REBSER *dir;

    while ((n = OS_STRLEN(str))) {
        len++;
        str += n + 1; // next
    }

    blk = Make_Array(len);

    // First is a dir path or full file path:
    str = start;
    n = OS_STRLEN(str);

    if (len == 1) {  // First is full file path
        dir = To_REBOL_Path(str, n, (OS_WIDE ? PATH_OPT_UNI_SRC : 0));
        Init_File(Alloc_Tail_Array(blk), dir);
    }
    else {  // First is dir path for the rest of the files
#ifdef TO_WINDOWS /* directory followed by files */
        assert(sizeof(wchar_t) == sizeof(REBCHR));
        dir = To_REBOL_Path(
            str,
            n,
            PATH_OPT_UNI_SRC | PATH_OPT_FORCE_UNI_DEST | PATH_OPT_SRC_IS_DIR
        );
        str += n + 1; // next
        len = SER_LEN(dir);
        while ((n = OS_STRLEN(str))) {
            SET_SERIES_LEN(dir, len);
            Append_Uni_Uni(dir, cast(const REBUNI*, str), n);
            Init_File(Alloc_Tail_Array(blk), Copy_String_Slimming(dir, 0, -1));
            str += n + 1; // next
        }
#else /* absolute pathes already */
        str += n + 1;
        while ((n = OS_STRLEN(str))) {
            dir = To_REBOL_Path(str, n, (OS_WIDE ? PATH_OPT_UNI_SRC : 0));
            Init_File(Alloc_Tail_Array(blk), Copy_String_Slimming(dir, 0, -1));
            str += n + 1; // next
        }
#endif
    }

    return blk;
}


//
//  request-file: native [
//
//  {Asks user to select a file and returns full file path (or block of paths).}
//
//      /save
//          "File save mode"
//      /multi
//          {Allows multiple file selection, returned as a block}
//      /file
//      name [file!]
//          "Default file name or directory"
//      /title
//      text [string!]
//          "Window title"
//      /filter
//      list [block!]
//          "Block of filters (filter-name filter)"
//  ]
//
REBNATIVE(request_file)
{
    INCLUDE_PARAMS_OF_REQUEST_FILE;

    // !!! This routine used to have an ENABLE_GC and DISABLE_GC
    // reference.  It is not clear what that was protecting, but
    // this code should be reviewed with GC "torture mode", and
    // if any values are being created which cannot be GC'd then
    // they should be created without handing them over to GC with
    // MANAGE_SERIES() instead.

    REBRFR fr;
    CLEARS(&fr);
    fr.files = OS_ALLOC_N(REBCHR, MAX_FILE_REQ_BUF);
    fr.len = MAX_FILE_REQ_BUF/sizeof(REBCHR) - 2;
    fr.files[0] = OS_MAKE_CH('\0');

    if (REF(save))
        SET_FLAG(fr.flags, FRF_SAVE);
    if (REF(multi))
        SET_FLAG(fr.flags, FRF_MULTI);

    if (REF(file)) {
        REBSER *ser = Value_To_OS_Path(ARG(name), TRUE);
        REBINT n = SER_LEN(ser);

        fr.dir = SER_HEAD(REBCHR, ser);

        if (OS_CH_VALUE(fr.dir[n - 1]) != OS_DIR_SEP) {
            if (n + 2 > fr.len)
                n = fr.len - 2;
            OS_STRNCPY(
                cast(REBCHR*, fr.files),
                SER_HEAD(REBCHR, ser),
                n
            );
            fr.files[n] = OS_MAKE_CH('\0');
        }
    }

    if (REF(filter)) {
        REBSER *ser = Block_To_String_List(ARG(list));
        fr.filter = SER_HEAD(REBCHR, ser);
    }

    if (REF(title)) {
        // !!! By passing NULL we don't get backing series to protect!
        fr.title = Val_Str_To_OS_Managed(NULL, ARG(text));
    }

    if (OS_REQUEST_FILE(&fr)) {
        if (GET_FLAG(fr.flags, FRF_MULTI)) {
            REBARR *array = File_List_To_Array(fr.files);
            Init_Block(D_OUT, array);
        }
        else {
            REBSER *ser = To_REBOL_Path(
                fr.files, OS_STRLEN(fr.files), (OS_WIDE ? PATH_OPT_UNI_SRC : 0)
            );
            Init_File(D_OUT, ser);
        }
    } else
        SET_BLANK(D_OUT);

    OS_FREE(fr.files);

    return R_OUT;
}


//
//  get-env: native [
//
//  {Returns the value of an OS environment variable (for current process).}
//
//      var [any-string! any-word!]
//  ]
//
REBNATIVE(get_env)
{
    INCLUDE_PARAMS_OF_GET_ENV;

    REBVAL *var = ARG(var);

    Check_Security(Canon(SYM_ENVR), POL_READ, var);

    if (ANY_WORD(var)) {
        REBSER *copy = Copy_Form_Value(var, 0);
        Init_String(var, copy);
    }

    // !!! By passing NULL we don't get backing series to protect!
    REBCHR *os_var = Val_Str_To_OS_Managed(NULL, var);

    REBINT lenplus = OS_GET_ENV(os_var, NULL, 0);
    if (lenplus == 0) return R_BLANK;
    if (lenplus < 0) return R_VOID;

    // Two copies...is there a better way?
    REBCHR *buf = ALLOC_N(REBCHR, lenplus);
    OS_GET_ENV(os_var, buf, lenplus);
    Init_String(D_OUT, Copy_OS_Str(buf, lenplus - 1));
    FREE_N(REBCHR, lenplus, buf);

    return R_OUT;
}


//
//  set-env: native [
//
//  {Sets value of operating system environment variable for current process.}
//
//      var [any-string! any-word!]
//          "Variable to set"
//      value [string! blank!]
//          "Value to set, or a BLANK! to unset it"
//  ]
//
REBNATIVE(set_env)
{
    INCLUDE_PARAMS_OF_SET_ENV;

    REBVAL *var = ARG(var);
    REBVAL *value = ARG(value);

    Check_Security(Canon(SYM_ENVR), POL_WRITE, var);

    if (ANY_WORD(var)) {
        REBSER *copy = Copy_Form_Value(var, 0);
        Init_String(var, copy);
    }

    // !!! By passing NULL we don't get backing series to protect!
    REBCHR *os_var = Val_Str_To_OS_Managed(NULL, var);

    if (ANY_STRING(value)) {
        // !!! By passing NULL we don't get backing series to protect!
        REBCHR *os_value = Val_Str_To_OS_Managed(NULL, value);
        if (OS_SET_ENV(os_var, os_value)) {
            // What function could reuse arg2 as-is?
            Init_String(D_OUT, Copy_OS_Str(os_value, OS_STRLEN(os_value)));
            return R_OUT;
        }
        return R_VOID;
    }

    assert(IS_BLANK(value));

    if (OS_SET_ENV(os_var, NULL))
        return R_BLANK;
    return R_VOID;
}


//
//  list-env: native [
//
//  {Returns a map of OS environment variables (for current process).}
//
//      ; No arguments
//  ]
//
REBNATIVE(list_env)
{
    REBARR *array = String_List_To_Array(OS_LIST_ENV());
    REBMAP *map = Mutate_Array_Into_Map(array);
    Init_Map(D_OUT, map);

    return R_OUT;
}


//
//  access-os: native [
//
//  {Access various OS functions (getuid, setuid, getpid, kill, etc.)}
//
//      field [word!]
//          "uid, euid, gid, egid, pid"
//      /set
//          "To set or kill pid (sig 15)"
//      value [integer! block!]
//          {Argument, such as uid, gid, or pid (in which case, it could be
//          a block with the signal number)}
//  ]
//
REBNATIVE(access_os)
{
    INCLUDE_PARAMS_OF_ACCESS_OS;

#define OS_ENA   -1
#define OS_EINVAL -2
#define OS_EPERM -3
#define OS_ESRCH -4

    REBVAL *field = ARG(field);
    REBOOL set = REF(set);
    REBVAL *val = ARG(value);

    switch (VAL_WORD_SYM(field)) {
        case SYM_UID:
            if (set) {
                if (IS_INTEGER(val)) {
                    REBINT ret = OS_SET_UID(VAL_INT32(val));
                    if (ret < 0) {
                        switch (ret) {
                            case OS_ENA:
                                return R_BLANK;

                            case OS_EPERM:
                                fail (Error_Permission_Denied_Raw());

                            case OS_EINVAL:
                                fail (val);

                            default:
                                fail (val);
                        }
                    } else {
                        SET_INTEGER(D_OUT, ret);
                        return R_OUT;
                    }
                }
                else
                    fail (val);
            }
            else {
                REBINT ret = OS_GET_UID();
                if (ret < 0) {
                    return R_BLANK;
                } else {
                    SET_INTEGER(D_OUT, ret);
                    return R_OUT;
                }
            }
            break;
        case SYM_GID:
            if (set) {
                if (IS_INTEGER(val)) {
                    REBINT ret = OS_SET_GID(VAL_INT32(val));
                    if (ret < 0) {
                        switch (ret) {
                            case OS_ENA:
                                return R_BLANK;

                            case OS_EPERM:
                                fail (Error_Permission_Denied_Raw());

                            case OS_EINVAL:
                                fail (val);

                            default:
                                fail (val);
                        }
                    } else {
                        SET_INTEGER(D_OUT, ret);
                        return R_OUT;
                    }
                }
                else
                    fail (val);
            }
            else {
                REBINT ret = OS_GET_GID();
                if (ret < 0) {
                    return R_BLANK;
                } else {
                    SET_INTEGER(D_OUT, ret);
                    return R_OUT;
                }
            }
            break;
        case SYM_EUID:
            if (set) {
                if (IS_INTEGER(val)) {
                    REBINT ret = OS_SET_EUID(VAL_INT32(val));
                    if (ret < 0) {
                        switch (ret) {
                            case OS_ENA:
                                return R_BLANK;

                            case OS_EPERM:
                                fail (Error_Permission_Denied_Raw());

                            case OS_EINVAL:
                                fail (val);

                            default:
                                fail (val);
                        }
                    } else {
                        SET_INTEGER(D_OUT, ret);
                        return R_OUT;
                    }
                }
                else
                    fail (val);
            }
            else {
                REBINT ret = OS_GET_EUID();
                if (ret < 0) {
                    return R_BLANK;
                } else {
                    SET_INTEGER(D_OUT, ret);
                    return R_OUT;
                }
            }
            break;
        case SYM_EGID:
            if (set) {
                if (IS_INTEGER(val)) {
                    REBINT ret = OS_SET_EGID(VAL_INT32(val));
                    if (ret < 0) {
                        switch (ret) {
                            case OS_ENA:
                                return R_BLANK;

                            case OS_EPERM:
                                fail (Error_Permission_Denied_Raw());

                            case OS_EINVAL:
                                fail (val);

                            default:
                                fail (val);
                        }
                    } else {
                        SET_INTEGER(D_OUT, ret);
                        return R_OUT;
                    }
                }
                else
                    fail (val);
            }
            else {
                REBINT ret = OS_GET_EGID();
                if (ret < 0) {
                    return R_BLANK;
                } else {
                    SET_INTEGER(D_OUT, ret);
                    return R_OUT;
                }
            }
            break;
        case SYM_PID:
            if (set) {
                REBINT ret = 0;
                RELVAL *pid = val;
                RELVAL *arg = val;
                if (IS_INTEGER(val)) {
                    ret = OS_KILL(VAL_INT32(pid));
                }
                else if (IS_BLOCK(val)) {
                    RELVAL *sig = NULL;

                    if (VAL_LEN_AT(val) != 2)
                        fail (val);

                    pid = VAL_ARRAY_AT_HEAD(val, 0);
                    if (!IS_INTEGER(pid))
                        fail (Error_Invalid_Arg_Core(pid, VAL_SPECIFIER(val)));

                    sig = VAL_ARRAY_AT_HEAD(val, 1);
                    if (!IS_INTEGER(sig))
                        fail (Error_Invalid_Arg_Core(sig, VAL_SPECIFIER(val)));

                    ret = OS_SEND_SIGNAL(VAL_INT32(pid), VAL_INT32(sig));
                    arg = sig;
                }
                else
                    fail (val);

                if (ret < 0) {
                    switch (ret) {
                        case OS_ENA:
                            return R_BLANK;

                        case OS_EPERM:
                            fail (Error_Permission_Denied_Raw());

                        case OS_EINVAL:
                            fail (KNOWN(arg));

                        case OS_ESRCH:
                            fail (Error_Process_Not_Found_Raw(pid));

                        default:
                            fail (val);
                    }
                } else {
                    SET_INTEGER(D_OUT, ret);
                    return R_OUT;
                }
            } else {
                REBINT ret = OS_GET_PID();
                if (ret < 0) {
                    return R_BLANK;
                } else {
                    SET_INTEGER(D_OUT, ret);
                    return R_OUT;
                }
            }
            break;

        default:
            fail (field);
    }
}


#ifdef TO_WINDOWS
    #include <windows.h> // Temporary: this is not desirable in core!
#else
    #include <unistd.h>
#endif

//
//  sleep: native [
//
//  "Use system sleep to wait a certain amount of time (doesn't use PORT!s)."
//
//      return: [<opt>]
//      duration [integer! decimal! time!]
//          {Length to sleep (integer and decimal are measuring seconds)}
//
//  ]
//
REBNATIVE(sleep)
//
// !!! This is a temporary workaround for the fact that it is not currently
// possible to do a WAIT on a time from within an AWAKE handler.  A proper
// solution would presumably solve that problem, so two different functions
// would not be needed.
{
    INCLUDE_PARAMS_OF_SLEEP;

    REBCNT msec = Milliseconds_From_Value(ARG(duration));

#ifdef TO_WINDOWS
    Sleep(msec);
#else
    usleep(msec * 1000);
#endif

    return R_VOID;
}
