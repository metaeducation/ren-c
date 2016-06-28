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
// Copyright 2012-2016 Rebol Open Source Contributors
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
//  echo: native [
//  
//  "Copies console output to a file."
//  
//      target [file! blank! logic!]
//  ]
//
REBNATIVE(echo)
{
    REBVAL *val = D_ARG(1);
    REBSER *ser = 0;

    Echo_File(0);

    if (IS_FILE(val))
        ser = Value_To_OS_Path(val, TRUE);
    else if (IS_LOGIC(val) && VAL_LOGIC(val))
        ser = To_Local_Path("output.txt", 10, FALSE, TRUE);

    if (ser) {
        if (!Echo_File(SER_HEAD(REBCHR, ser)))
            fail (Error(RE_CANNOT_OPEN, val));
    }

    return R_OUT;
}


//
//  form: native [
//  
//  "Converts a value to a human-readable string."
//  
//      value [<opt> any-value!] "The value to form"
//      /delimit
//          "Use a delimiter between expressions that added to the output"
//      delimiter [blank! any-scalar! any-string! block!]
//          "Delimiting value (or block of delimiters for each depth)"
//      /quote
//          "Do not reduce values in blocks"
//      /new
//          "TEMPORARY: Test new formatting logic for Ren-C (reduces)"
//  ]
//
REBNATIVE(form)
{
    PARAM(1, value);
    REFINE(2, delimit);
    PARAM(3, delimiter);
    REFINE(4, quote);
    REFINE(5, new);

    REBVAL *value = ARG(value);

    if (REF(new)) {
        REBVAL pending_delimiter;
        SET_END(&pending_delimiter);

        REB_MOLD mo;
        CLEARS(&mo);

        // !!! Temporary experiment to make the strings done by the new
        // print logic available programmatically.  Possible outcome is that
        // this would become the new FORM.

        REBVAL *delimiter;
        if (REF(delimit))
            delimiter = ARG(delimiter);
        else
            delimiter = SPACE_VALUE;

        Push_Mold(&mo);

        if (Form_Value_Throws(
            D_OUT,
            &mo,
            &pending_delimiter,
            value,
            (REF(quote) || !IS_BLOCK(value) ? 0 : FORM_FLAG_REDUCE),
            delimiter,
            0 // depth
        )) {
            return R_OUT_IS_THROWN;
        }

        Val_Init_String(D_OUT, Pop_Molded_String(&mo));
    }
    else {
        Val_Init_String(D_OUT, Copy_Form_Value(value, 0));
    }
    return R_OUT;
}


//
//  mold: native [
//  
//  "Converts a value to a REBOL-readable string."
//  
//      value [any-value!] "The value to mold"
//      /only {For a block value, mold only its contents, no outer []}
//      /all "Use construction syntax"
//      /flat "No indentation"
//  ]
//
REBNATIVE(mold)
{
    PARAM(1, value);
    REFINE(2, only);
    REFINE(3, all);
    REFINE(4, flat);

    REB_MOLD mo;
    CLEARS(&mo);
    if (REF(all)) SET_FLAG(mo.opts, MOPT_MOLD_ALL);
    if (REF(flat)) SET_FLAG(mo.opts, MOPT_INDENT);

    Push_Mold(&mo);

    if (REF(only) && IS_BLOCK(ARG(value))) SET_FLAG(mo.opts, MOPT_ONLY);

    Mold_Value(&mo, ARG(value), TRUE);

    Val_Init_String(D_OUT, Pop_Molded_String(&mo));

    return R_OUT;
}


//
//  print: native [
//      <punctuates>
//  
//  "Outputs value to standard output using the PRINT dialect."
//  
//      value [any-value!]
//          "Value or BLOCK! literal in PRINT dialect, newline if any output"
//      /only
//          "Do not include automatic spacing or newlines"
//      /delimit
//          "Use a delimiter between expressions that added to the output"
//      delimiter [blank! any-scalar! any-string! block!]
//          "Delimiting value (or block of delimiters for each depth)"
//      /eval
//          "Print block using the evaluating dialect (even if not literal)"
//      /quote
//          "Do not reduce values in blocks"
//  ]
//
REBNATIVE(print)
{
    PARAM(1, value);
    REFINE(2, only);
    REFINE(3, delimit);
    PARAM(4, delimiter);
    REFINE(5, eval);
    REFINE(6, quote);

    REBVAL *value = ARG(value);

    if (IS_VOID(value)) // opt out of printing
        return R_VOID;

    // Literal blocks passed to PRINT are assumed to be all right to evaluate.
    // But for safety, `print x` will not run code if x is a block, unless the
    // /EVAL switch is used.  This helps prevent accidents, confusions, or
    // security problems if a block gets into a slot that wasn't expecting it.
    //
    if (
        IS_BLOCK(value)
        && GET_VAL_FLAG(value, VALUE_FLAG_EVALUATED)
        && NOT(REF(eval))
    ){
        fail (Error(RE_PRINT_NEEDS_EVAL));
    }

    const REBVAL *delimiter;
    if (REF(delimit))
        delimiter = ARG(delimiter);
    else if (REF(only))
        delimiter = BLANK_VALUE;
    else {
        // By default, we assume the delimiter is supposed to be a space at the
        // outermost level and nothing at every level beyond that.
        //
        delimiter = SPACE_VALUE;
    }

    if (Print_Value_Throws(
        D_OUT,
        value,
        delimiter,
        0, // `limit`: 0 means do not limit output length
        (REF(only) ? FORM_FLAG_ONLY : FORM_FLAG_NEWLINE_SEQUENTIAL_STRINGS)
            | (REF(quote) ? 0 : FORM_FLAG_REDUCE)
            | (REF(only) ? 0 : FORM_FLAG_NEWLINE_UNLESS_EMPTY)
    )) {
        return R_OUT_IS_THROWN;
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
    PARAM(1, position);
    PARAM(2, mark);
    REFINE(3, all);
    REFINE(4, skip);
    PARAM(5, size);

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

    *D_OUT = *ARG(position);
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
    PARAM(1, position);

    if (GET_VAL_FLAG(VAL_ARRAY_AT(ARG(position)), VALUE_FLAG_LINE))
        return R_TRUE;

    return R_FALSE;
}


//
//  now: native [
//  
//  "Returns current date and time with timezone adjustment."
//  
//      /year "Returns year only"
//      /month "Returns month only"
//      /day "Returns day of the month only"
//      /time "Returns time only"
//      /zone "Returns time zone offset from UCT (GMT) only"
//      /date "Returns date only"
//      /weekday {Returns day of the week as integer (Monday is day 1)}
//      /yearday "Returns day of the year (Julian)"
//      /precise "High precision time"
//      /utc "Universal time (no zone)"
//  ]
//
REBNATIVE(now)
{
    REBOL_DAT dat;
    REBINT n = -1;
    REBVAL *ret = D_OUT;

    OS_GET_TIME(&dat);
    if (!D_REF(9)) dat.nano = 0; // Not /precise
    Set_Date(ret, &dat);
    Current_Year = dat.year;

    if (D_REF(10)) { // UTC
        VAL_ZONE(ret) = 0;
    }
    else {
        if (D_REF(1) || D_REF(2) || D_REF(3) || D_REF(4)
            || D_REF(6) || D_REF(7) || D_REF(8))
            Adjust_Date_Zone(ret, FALSE); // Add time zone, adjust date and time
    }

    // Check for /date, /time, /zone
    if (D_REF(6)) {         // date
        VAL_TIME(ret) = NO_TIME;
        VAL_ZONE(ret) = 0;
    }
    else if (D_REF(4)) {    // time
        //if (dat.time == ???) SET_BLANK(ret);
        VAL_RESET_HEADER(ret, REB_TIME);
    }
    else if (D_REF(5)) {    // zone
        VAL_RESET_HEADER(ret, REB_TIME);
        VAL_TIME(ret) = VAL_ZONE(ret) * ZONE_MINS * MIN_SEC;
    }
    else if (D_REF(7)) n = Week_Day(VAL_DATE(ret));
    else if (D_REF(8)) n = Julian_Date(VAL_DATE(ret));
    else if (D_REF(1)) n = VAL_YEAR(ret);
    else if (D_REF(2)) n = VAL_MONTH(ret);
    else if (D_REF(3)) n = VAL_DAY(ret);

    if (n > 0) SET_INTEGER(ret, n);

    return R_OUT;
}


//
//  wait: native [
//  
//  "Waits for a duration, port, or both."
//  
//      value [any-number! time! port! block! blank!]
//      /all "Returns all in a block"
//      /only {only check for ports given in the block to this function}
//  ]
//
REBNATIVE(wait)
{
    PARAM(1, value);
    REFINE(2, all);
    REFINE(3, only);

    RELVAL *val = ARG(value);
    REBINT timeout = 0; // in milliseconds
    REBARR *ports = NULL;
    REBINT n = 0;

    SET_BLANK(D_OUT);

    if (IS_BLOCK(val)) {
        REBVAL unsafe; // temporary not safe from GC

        if (Reduce_Array_Throws(
            &unsafe,
            VAL_ARRAY(ARG(value)),
            VAL_INDEX(val),
            VAL_SPECIFIER(ARG(value)),
            FALSE
        )) {
            *D_OUT = unsafe;
            return R_OUT_IS_THROWN;
        }

        ports = VAL_ARRAY(&unsafe);
        for (val = ARR_HEAD(ports); NOT_END(val); val++) { // find timeout
            if (Pending_Port(KNOWN(val))) n++;
            if (IS_INTEGER(val) || IS_DECIMAL(val)) break;
        }
        if (IS_END(val)) {
            if (n == 0) return R_BLANK; // has no pending ports!
            else timeout = ALL_BITS; // no timeout provided
            // SET_BLANK(val); // no timeout -- BUG: unterminated block in GC
        }
    }

    if (NOT_END(val)) {
        switch (VAL_TYPE(val)) {
        case REB_INTEGER:
            timeout = 1000 * Int32(KNOWN(val));
            goto chk_neg;

        case REB_DECIMAL:
            timeout = (REBINT)(1000 * VAL_DECIMAL(val));
            goto chk_neg;

        case REB_TIME:
            timeout = (REBINT) (VAL_TIME(val) / (SEC_SEC / 1000));
        chk_neg:
            if (timeout < 0) fail (Error_Out_Of_Range(KNOWN(val)));
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
    if (ports) Val_Init_Block(D_OUT, ports);

    // Process port events [stack-move]:
    if (!Wait_Ports(ports, timeout, D_REF(3))) {
        Sieve_Ports(NULL); /* just reset the waked list */
        return R_BLANK;
    }
    if (!ports) return R_BLANK;

    // Determine what port(s) waked us:
    Sieve_Ports(ports);

    if (!REF(all)) {
        val = ARR_HEAD(ports);
        if (IS_PORT(val)) *D_OUT = *KNOWN(val);
        else SET_BLANK(D_OUT);
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
    PARAM(1, port);
    PARAM(2, event);

    REBCTX *port = VAL_CONTEXT(ARG(port));
    REBOOL awakened = TRUE; // start by assuming success
    REBVAL *value;

    if (CTX_LEN(port) < STD_PORT_MAX - 1) panic (Error(RE_MISC));

    value = CTX_VAR(port, STD_PORT_ACTOR);
    if (IS_FUNCTION(value)) {
        //
        // We don't pass `value` or `event` in, because we just pass the
        // current call info.  The port action can re-read the arguments.
        //
        Do_Port_Action(frame_, port, SYM_UPDATE);
    }

    value = CTX_VAR(port, STD_PORT_AWAKE);
    if (IS_FUNCTION(value)) {
        if (Apply_Only_Throws(D_OUT, value, ARG(event), END_CELL))
            fail (Error_No_Catch_For_Throw(D_OUT));

        if (!(IS_LOGIC(D_OUT) && VAL_LOGIC(D_OUT))) awakened = FALSE;
        SET_TRASH_SAFE(D_OUT);
    }

    return awakened ? R_TRUE : R_FALSE;
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
    REBVAL *arg = D_ARG(1);
    REBSER *ser;

    ser = Value_To_REBOL_Path(arg, FALSE);
    if (!ser) fail (Error_Invalid_Arg(arg));
    Val_Init_File(D_OUT, ser);

    return R_OUT;
}


//
//  to-local-file: native [
//  
//  {Converts a REBOL file path to the local system file path.}
//  
//      path [file! string!]
//      /full {Prepends current dir for full path (for relative paths only)}
//  ]
//
REBNATIVE(to_local_file)
{
    REBVAL *arg = D_ARG(1);
    REBSER *ser;

    ser = Value_To_Local_Path(arg, D_REF(2));
    if (!ser) fail (Error_Invalid_Arg(arg));
    Val_Init_String(D_OUT, ser);

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
        *D_OUT = *current_path;
    }
    else if (IS_FILE(current_path) || IS_BLANK(current_path)) {
        len = OS_GET_CURRENT_DIR(&lpath);

        // allocates extra for end `/`
        ser = To_REBOL_Path(
            lpath, len, PATH_OPT_SRC_IS_DIR | (OS_WIDE ? PATH_OPT_UNI_SRC : 0)
        );

        OS_FREE(lpath);

        Val_Init_File(D_OUT, ser);
        *current_path = *D_OUT; // !!! refresh system option if they diverged
    }
    else {
        // Lousy error, but ATM the user can directly edit system/options.
        // They shouldn't be able to (or if they can, it should be validated)
        fail (Error_Invalid_Arg(current_path));
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
    PARAM(1, path);

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
        if (!ser)
            fail (Error_Invalid_Arg(arg)); // !!! ERROR MSG

        REBVAL val;
        Val_Init_String(&val, ser); // may be unicode or utf-8
        Check_Security(Canon(SYM_FILE), POL_EXEC, &val);

        if (!OS_SET_CURRENT_DIR(SER_HEAD(REBCHR, ser)))
            fail (Error_Invalid_Arg(arg)); // !!! ERROR MSG
    }

    *current_path = *arg;

    *D_OUT = *ARG(path);
    return R_OUT;
}


//
//  browse: native [
//  
//  "Open web browser to a URL or local file."
//  
//      url [url! file! blank!]
//  ]
//
REBNATIVE(browse)
{
    REBINT r;
    REBCHR *url = 0;
    REBVAL *arg = D_ARG(1);

    Check_Security(Canon(SYM_BROWSE), POL_EXEC, arg);

    if (IS_BLANK(arg))
        return R_VOID;

    // !!! By passing NULL we don't get backing series to protect!
    url = Val_Str_To_OS_Managed(NULL, arg);

    r = OS_BROWSE(url, 0);

    if (r == 0) {
        return R_VOID;
    } else {
        Make_OS_Error(D_OUT, r);
        fail (Error(RE_CALL_FAIL, D_OUT));
    }

    return R_VOID;
}


//
//  call: native [
//  
//  "Run another program; return immediately."
//  
//      command [string! block! file!] 
//      {An OS-local command line (quoted as necessary), a block with arguments, or an executable file}
//      
//      /wait "Wait for command to terminate before returning"
//      /console "Runs command with I/O redirected to console"
//      /shell "Forces command to be run from shell"
//      /info "Returns process information object"
//      /input in [string! binary! file! blank!]
//          "Redirects stdin to in"
//      /output out [string! binary! file! blank!]
//          "Redirects stdout to out"
//      /error err [string! binary! file! blank!]
//          "Redirects stderr to err"
//  ]
//
REBNATIVE(call)
{
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
    REBVAL *arg = D_ARG(1);
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

    // Parameter usage may require WAIT mode even if not explicitly requested
    // !!! /WAIT should be default, with /ASYNC (or otherwise) as exception!
    //
    REBOOL flag_wait = FALSE;
    REBOOL flag_console = FALSE;
    REBOOL flag_shell = FALSE;
    REBOOL flag_info = FALSE;

    int exit_code = 0;

    Check_Security(Canon(SYM_CALL), POL_EXEC, arg);

    if (D_REF(2)) flag_wait = TRUE;
    if (D_REF(3)) flag_console = TRUE;
    if (D_REF(4)) flag_shell = TRUE;
    if (D_REF(5)) flag_info = TRUE;

    // If input_ser is set, it will be both managed and saved
    if (D_REF(6)) {
        REBVAL *param = D_ARG(7);
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
            fail (Error_Invalid_Arg(param));
    }

    // Note that os_output is actually treated as an *input* parameter in the
    // case of a FILE! by OS_CREATE_PROCESS.  (In the other cases it is a
    // pointer of the returned data, which will need to be freed with
    // OS_FREE().)  Hence the case for FILE! is handled specially, where the
    // output_ser must be unsaved instead of OS_FREE()d.
    //
    if (D_REF(8)) {
        REBVAL *param = D_ARG(9);
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
            fail (Error_Invalid_Arg(param));
    }

    (void)input; // suppress unused warning but keep variable

    // Error case...same note about FILE! case as with Output case above
    if (D_REF(10)) {
        REBVAL *param = D_ARG(11);
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
            fail (Error_Invalid_Arg(param));
    }

    /* I/O redirection implies /wait */
    if (input_type == STRING_TYPE
        || input_type == BINARY_TYPE
        || output_type == STRING_TYPE
        || output_type == BINARY_TYPE
        || err_type == STRING_TYPE
        || err_type == BINARY_TYPE) {
        flag_wait = TRUE;
    }

    if (flag_wait) flags |= FLAG_WAIT;
    if (flag_console) flags |= FLAG_CONSOLE;
    if (flag_shell) flags |= FLAG_SHELL;
    if (flag_info) flags |= FLAG_INFO;

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
        argv_ser = Make_Series(argc + 1, sizeof(REBCHR*), MKS_NONE);
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

        if (argc <= 0) fail (Error(RE_TOO_SHORT));

        argv_ser = Make_Series(argc + 1, sizeof(REBCHR*), MKS_NONE);
        argv_saved_sers = Make_Series(argc, sizeof(REBSER*), MKS_NONE);
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
        argv_ser = Make_Series(argc + 1, sizeof(REBCHR*), MKS_NONE);
        argv_saved_sers = Make_Series(argc, sizeof(REBSER*), MKS_NONE);

        argv = SER_HEAD(const REBCHR*, argv_ser);

        argv[0] = SER_HEAD(REBCHR, path);
        PUSH_GUARD_SERIES(path);
        SER_HEAD(REBSER*, argv_saved_sers)[0] = path;

        argv[argc] = NULL;
    }
    else
        fail (Error_Invalid_Arg(arg));

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

    if (flag_info) {
        REBCTX *info = Alloc_Context(2);

        SET_INTEGER(Append_Context(info, NULL, Canon(SYM_ID)), pid);
        if (flag_wait)
            SET_INTEGER(
                Append_Context(info, NULL, Canon(SYM_EXIT_CODE)),
                exit_code
            );

        Val_Init_Object(D_OUT, info);
        return R_OUT;
    }

    if (r != 0) {
        Make_OS_Error(D_OUT, r);
        fail (Error(RE_CALL_FAIL, D_OUT));
    }

    // We may have waited even if they didn't ask us to explicitly, but
    // we only return a process ID if /WAIT was not explicitly used
    if (flag_wait)
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
        Val_Init_String(Alloc_Tail_Array(array), Copy_OS_Str(str, eq - str));
        Val_Init_String(
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
        Val_Init_File(Alloc_Tail_Array(blk), dir);
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
            Val_Init_File(Alloc_Tail_Array(blk), Copy_String_Slimming(dir, 0, -1));
            str += n + 1; // next
        }
#else /* absolute pathes already */
        str += n + 1;
        while ((n = OS_STRLEN(str))) {
            dir = To_REBOL_Path(str, n, (OS_WIDE ? PATH_OPT_UNI_SRC : 0));
            Val_Init_File(Alloc_Tail_Array(blk), Copy_String_Slimming(dir, 0, -1));
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
//      /save "File save mode"
//      /multi {Allows multiple file selection, returned as a block}
//      /file name [file!] "Default file name or directory"
//      /title text [string!] "Window title"
//      /filter list [block!] "Block of filters (filter-name filter)"
//  ]
//
REBNATIVE(request_file)
{
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

    if (D_REF(ARG_REQUEST_FILE_SAVE)) SET_FLAG(fr.flags, FRF_SAVE);
    if (D_REF(ARG_REQUEST_FILE_MULTI)) SET_FLAG(fr.flags, FRF_MULTI);

    if (D_REF(ARG_REQUEST_FILE_FILE)) {
        REBSER *ser = Value_To_OS_Path(D_ARG(ARG_REQUEST_FILE_NAME), TRUE);
        REBINT n = SER_LEN(ser);

        fr.dir = SER_HEAD(REBCHR, ser);

        if (OS_CH_VALUE(fr.dir[n-1]) != OS_DIR_SEP) {
            if (n+2 > fr.len) n = fr.len - 2;
            OS_STRNCPY(
                cast(REBCHR*, fr.files),
                SER_HEAD(REBCHR, ser),
                n
            );
            fr.files[n] = OS_MAKE_CH('\0');
        }
    }

    if (D_REF(ARG_REQUEST_FILE_FILTER)) {
        REBSER *ser = Block_To_String_List(D_ARG(ARG_REQUEST_FILE_LIST));
        fr.filter = SER_HEAD(REBCHR, ser);
    }

    if (D_REF(ARG_REQUEST_FILE_TITLE)) {
        // !!! By passing NULL we don't get backing series to protect!
        fr.title = Val_Str_To_OS_Managed(NULL, D_ARG(ARG_REQUEST_FILE_TEXT));
    }

    if (OS_REQUEST_FILE(&fr)) {
        if (GET_FLAG(fr.flags, FRF_MULTI)) {
            REBARR *array = File_List_To_Array(fr.files);
            Val_Init_Block(D_OUT, array);
        }
        else {
            REBSER *ser = To_REBOL_Path(
                fr.files, OS_STRLEN(fr.files), (OS_WIDE ? PATH_OPT_UNI_SRC : 0)
            );
            Val_Init_File(D_OUT, ser);
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
    REBCHR *cmd;
    REBINT lenplus;
    REBCHR *buf;
    REBVAL *arg = D_ARG(1);

    Check_Security(Canon(SYM_ENVR), POL_READ, arg);

    if (ANY_WORD(arg)) Val_Init_String(arg, Copy_Form_Value(arg, 0));

    // !!! By passing NULL we don't get backing series to protect!
    cmd = Val_Str_To_OS_Managed(NULL, arg);

    lenplus = OS_GET_ENV(cmd, NULL, 0);
    if (lenplus == 0) return R_BLANK;
    if (lenplus < 0) return R_VOID;

    // Two copies...is there a better way?
    buf = ALLOC_N(REBCHR, lenplus);
    OS_GET_ENV(cmd, buf, lenplus);
    Val_Init_String(D_OUT, Copy_OS_Str(buf, lenplus - 1));
    FREE_N(REBCHR, lenplus, buf);

    return R_OUT;
}


//
//  set-env: native [
//  
//  {Sets the value of an operating system environment variable (for current process).}
//  
//      var [any-string! any-word!] "Variable to set"
//      value [string! blank!] "Value to set, or NONE to unset it"
//  ]
//
REBNATIVE(set_env)
{
    REBCHR *cmd;
    REBVAL *arg1 = D_ARG(1);
    REBVAL *arg2 = D_ARG(2);
    REBOOL success;

    Check_Security(Canon(SYM_ENVR), POL_WRITE, arg1);

    if (ANY_WORD(arg1)) Val_Init_String(arg1, Copy_Form_Value(arg1, 0));

    // !!! By passing NULL we don't get backing series to protect!
    cmd = Val_Str_To_OS_Managed(NULL, arg1);

    if (ANY_STRING(arg2)) {
        // !!! By passing NULL we don't get backing series to protect!
        REBCHR *value = Val_Str_To_OS_Managed(NULL, arg2);
        success = OS_SET_ENV(cmd, value);
        if (success) {
            // What function could reuse arg2 as-is?
            Val_Init_String(D_OUT, Copy_OS_Str(value, OS_STRLEN(value)));
            return R_OUT;
        }
        return R_VOID;
    }

    if (IS_BLANK(arg2)) {
        success = OS_SET_ENV(cmd, 0);
        if (success)
            return R_BLANK;
        return R_VOID;
    }

    // is there any checking that native interface has not changed
    // out from under the expectations of the code?

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
    Val_Init_Map(D_OUT, map);

    return R_OUT;
}

//
//  access-os: native [
//  
//  {Access to various operating system functions (getuid, setuid, getpid, kill, etc.)}
//  
//      field [word!] "uid, euid, gid, egid, pid"
//      /set "To set or kill pid (sig 15)"
//      value [integer! block!] 
//      {Argument, such as uid, gid, or pid (in which case, it could be a block with the signal no)}
//  ]
//
REBNATIVE(access_os)
{
#define OS_ENA   -1
#define OS_EINVAL -2
#define OS_EPERM -3
#define OS_ESRCH -4

    REBVAL *field = D_ARG(1);
    REBOOL set = D_REF(2);
    REBVAL *val = D_ARG(3);

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
                                fail (Error(RE_PERMISSION_DENIED));

                            case OS_EINVAL:
                                fail (Error_Invalid_Arg(val));

                            default:
                                fail (Error_Invalid_Arg(val));
                        }
                    } else {
                        SET_INTEGER(D_OUT, ret);
                        return R_OUT;
                    }
                }
                else
                    fail (Error_Invalid_Arg(val));
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
                                fail (Error(RE_PERMISSION_DENIED));

                            case OS_EINVAL:
                                fail (Error_Invalid_Arg(val));

                            default:
                                fail (Error_Invalid_Arg(val));
                        }
                    } else {
                        SET_INTEGER(D_OUT, ret);
                        return R_OUT;
                    }
                }
                else
                    fail (Error_Invalid_Arg(val));
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
                                fail (Error(RE_PERMISSION_DENIED));

                            case OS_EINVAL:
                                fail (Error_Invalid_Arg(val));

                            default:
                                fail (Error_Invalid_Arg(val));
                        }
                    } else {
                        SET_INTEGER(D_OUT, ret);
                        return R_OUT;
                    }
                }
                else
                    fail (Error_Invalid_Arg(val));
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
                                fail (Error(RE_PERMISSION_DENIED));

                            case OS_EINVAL:
                                fail (Error_Invalid_Arg(val));

                            default:
                                fail (Error_Invalid_Arg(val));
                        }
                    } else {
                        SET_INTEGER(D_OUT, ret);
                        return R_OUT;
                    }
                }
                else
                    fail (Error_Invalid_Arg(val));
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

                    if (VAL_LEN_AT(val) != 2) fail (Error_Invalid_Arg(val));

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
                    fail (Error_Invalid_Arg(val));

                if (ret < 0) {
                    switch (ret) {
                        case OS_ENA:
                            return R_BLANK;

                        case OS_EPERM:
                            fail (Error(RE_PERMISSION_DENIED));

                        case OS_EINVAL:
                            fail (Error_Invalid_Arg(KNOWN(arg)));

                        case OS_ESRCH:
                            fail (Error(RE_PROCESS_NOT_FOUND, pid));

                        default:
                            fail (Error_Invalid_Arg(val));
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
            fail (Error_Invalid_Arg(field));
    }
}
