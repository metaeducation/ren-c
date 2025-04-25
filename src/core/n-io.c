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
//      value [any-element!]
//          "The value to form"
//  ]
//
DECLARE_NATIVE(FORM)
{
    INCLUDE_PARAMS_OF_FORM;

    return Init_Text(OUT, Copy_Form_Value(ARG(VALUE), 0));
}


//
//  mold: native [
//
//  "Converts a value to a REBOL-readable string."
//
//      value "The value to mold"
//          [any-element!]
//      /only "For a block value, mold only its contents, no outer []"
//      /flat "No indentation"
//      /limit "Limit to a certain length"
//      amount [integer!]
//  ]
//
DECLARE_NATIVE(MOLD)
{
    INCLUDE_PARAMS_OF_MOLD;

    DECLARE_MOLDER (mo);
    if (Bool_ARG(FLAT))
        SET_MOLD_FLAG(mo, MOLD_FLAG_INDENT);
    if (Bool_ARG(LIMIT)) {
        SET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT);
        mo->limit = Int32(ARG(AMOUNT));
    }

    Push_Mold(mo);

    if (Bool_ARG(ONLY) and Is_Block(ARG(VALUE)))
        SET_MOLD_FLAG(mo, MOLD_FLAG_ONLY);

    Mold_Value(mo, ARG(VALUE));

    return Init_Text(OUT, Pop_Molded_String(mo));
}


//
//  write-stdout: native [
//
//  "Write text to standard output, or raw BINARY! (for control codes / CGI)"
//
//      return: [~null~ trash!]
//      value [<maybe> text! char! binary!]
//          "Text to write, if a STRING! or CHAR! is converted to OS format"
//  ]
//
DECLARE_NATIVE(WRITE_STDOUT)
{
    INCLUDE_PARAMS_OF_WRITE_STDOUT;

    Value* v = ARG(VALUE);

    if (Is_Binary(v)) {
        //
        // It is sometimes desirable to write raw binary data to stdout.  e.g.
        // e.g. CGI scripts may be hooked up to stream data for a download,
        // and not want the bytes interpreted in any way.  (e.g. not changed
        // from UTF-8 to wide characters, or not having CR turned into CR LF
        // sequences).
        //
        Prin_OS_String(Cell_Blob_Head(v), Cell_Series_Len_At(v), OPT_ENC_RAW);
    }
    else if (Is_Char(v)) {
        //
        // Useful for `write-stdout newline`, etc.
        //
        // !!! Temporarily just support ASCII codepoints, since making a
        // codepoint out of a string pre-UTF8-everywhere makes a Ucs2Unit string.
        //
        if (VAL_CHAR(v) > 0x7f)
            fail ("non-ASCII CHAR! output temporarily disabled.");
        Prin_OS_String(cast(Byte*, &VAL_CHAR(v)), 1, OPT_ENC_0);
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
        assert(Is_Text(v));

        Size offset;
        Size size;
        Binary* temp = Temp_UTF8_At_Managed(&offset, &size, v, Cell_Series_Len_At(v));
        Push_GC_Guard(temp);

        Prin_OS_String(Binary_At(temp, offset), size, OPT_ENC_0);

        Drop_GC_Guard(temp);
    }

    return Init_Trash(OUT);
}


//
//  new-line: native [
//
//  {Sets or clears the new-line marker within a block or group.}
//
//      position [block! group!]
//          "Position to change marker (modified)"
//      mark [word!]
//          "Set YES for newline, NO for no newline"
//      /all
//          "Set/clear marker to end of series"
//      /skip
//          {Set/clear marker periodically to the end of the series}
//      count [integer!]
//  ]
//
DECLARE_NATIVE(NEW_LINE)
{
    INCLUDE_PARAMS_OF_NEW_LINE;

    bool mark;
    if (Cell_Word_Id(ARG(MARK)) == SYM_YES)
        mark = true;
    else if (Cell_Word_Id(ARG(MARK)) == SYM_NO)
        mark = false;
    else
        fail (PARAM(MARK));

    Value* pos = ARG(POSITION);
    Array* a = Cell_Array(pos);

    Fail_If_Read_Only_Flex(a);

    Copy_Cell(OUT, pos); // always returns the input position

    Cell* item = Cell_List_At(pos);

    if (IS_END(item)) { // no value at tail to mark; use bit in array
        if (mark)
            Set_Array_Flag(a, NEWLINE_AT_TAIL);
        else
            Clear_Array_Flag(a, NEWLINE_AT_TAIL);
        return OUT;
    }

    REBINT skip;
    if (Bool_ARG(ALL))
        skip = 1;
    else if (Bool_ARG(SKIP)) {
        skip = Int32s(ARG(COUNT), 1);
        if (skip < 1)
            skip = 1;
    }
    else
        skip = 0;

    REBLEN n;
    for (n = 0; NOT_END(item); ++n, ++item) {
        if (skip != 0 and (n % skip != 0))
            continue;

        if (mark)
            Set_Cell_Flag(item, NEWLINE_BEFORE);
        else
            Clear_Cell_Flag(item, NEWLINE_BEFORE);

        if (skip == 0)
            break;
    }

    return OUT;
}


//
//  new-line?: native [
//
//  {Returns the state of the new-line marker within a block or group.}
//
//      position [block! group! varargs!] "Position to check marker"
//  ]
//
DECLARE_NATIVE(NEW_LINE_Q)
{
    INCLUDE_PARAMS_OF_NEW_LINE_Q;

    Value* pos = ARG(POSITION);

    Array* arr;
    const Cell* item;

    if (Is_Varargs(pos)) {
        Level* L;
        Value* shared;
        if (Is_Level_Style_Varargs_May_Fail(&L, pos)) {
            if (not L->source->array) {
                //
                // C va_args input to frame, as from the API, but not in the
                // process of using string components which *might* have
                // newlines.  Review edge cases, like:
                //
                //    Value* new_line_q = rebValue(":new-line?");
                //    bool case_one = rebDid("new-line?", "[\n]");
                //    bool case_two = rebDid(new_line_q, "[\n]");
                //
                assert(L->source->index == TRASHED_INDEX);
                return Init_Logic(OUT, false);
            }

            arr = L->source->array;
            item = L->value;
        }
        else if (Is_Block_Style_Varargs(&shared, pos)) {
            arr = Cell_Array(shared);
            item = Cell_List_At(shared);
        }
        else
            panic ("Bad VARARGS!");
    }
    else {
        assert(Is_Group(pos) or Is_Block(pos));
        arr = Cell_Array(pos);
        item = Cell_List_At(pos);
    }

    if (NOT_END(item))
        return Init_Logic(
            OUT,
            Get_Cell_Flag(item, NEWLINE_BEFORE)
        );

    return Init_Logic(
        OUT,
        Get_Array_Flag(arr, NEWLINE_AT_TAIL)
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
DECLARE_NATIVE(NOW)
{
    INCLUDE_PARAMS_OF_NOW;

    Value* timestamp = OS_GET_TIME();

    // However OS-level date and time is plugged into the system, it needs to
    // have enough granularity to give back date, time, and time zone.
    //
    assert(Is_Date(timestamp));
    assert(Get_Cell_Flag(timestamp, DATE_HAS_TIME));
    assert(Get_Cell_Flag(timestamp, DATE_HAS_ZONE));

    Copy_Cell(OUT, timestamp);
    rebRelease(timestamp);

    if (not Bool_ARG(PRECISE)) {
        //
        // The "time" field is measured in nanoseconds, and the historical
        // meaning of not using precise measurement was to use only the
        // seconds portion (with the nanoseconds set to 0).  This achieves
        // that by extracting the seconds and then multiplying by nanoseconds.
        //
        VAL_NANO(OUT) = SECS_TO_NANO(VAL_SECS(OUT));
    }

    if (Bool_ARG(UTC)) {
        //
        // Say it has a time zone component, but it's 0:00 (as opposed
        // to saying it has no time zone component at all?)
        //
        INIT_VAL_ZONE(OUT, 0);
    }
    else if (Bool_ARG(LOCAL)) {
        //
        // Clear out the time zone flag
        //
        Clear_Cell_Flag(OUT, DATE_HAS_ZONE);
    }
    else {
        if (
            Bool_ARG(YEAR)
            || Bool_ARG(MONTH)
            || Bool_ARG(DAY)
            || Bool_ARG(TIME)
            || Bool_ARG(DATE)
            || Bool_ARG(WEEKDAY)
            || Bool_ARG(YEARDAY)
        ){
            const bool to_utc = false;
            Adjust_Date_Zone(OUT, to_utc); // Add timezone, adjust date/time
        }
    }

    REBINT n = -1;

    if (Bool_ARG(DATE)) {
        Clear_Cell_Flag(OUT, DATE_HAS_TIME);
        Clear_Cell_Flag(OUT, DATE_HAS_ZONE);
    }
    else if (Bool_ARG(TIME)) {
        RESET_CELL(OUT, TYPE_TIME); // reset clears date flags
    }
    else if (Bool_ARG(ZONE)) {
        VAL_NANO(OUT) = VAL_ZONE(OUT) * ZONE_MINS * MIN_SEC;
        RESET_CELL(OUT, TYPE_TIME); // reset clears date flags
    }
    else if (Bool_ARG(WEEKDAY))
        n = Week_Day(VAL_DATE(OUT));
    else if (Bool_ARG(YEARDAY))
        n = Julian_Date(VAL_DATE(OUT));
    else if (Bool_ARG(YEAR))
        n = VAL_YEAR(OUT);
    else if (Bool_ARG(MONTH))
        n = VAL_MONTH(OUT);
    else if (Bool_ARG(DAY))
        n = VAL_DAY(OUT);

    if (n > 0)
        Init_Integer(OUT, n);

    return OUT;
}


//
//  Milliseconds_From_Value: C
//
// Note that this routine is used by the SLEEP extension, as well as by WAIT.
//
REBLEN Milliseconds_From_Value(const Cell* v) {
    REBINT msec;

    switch (Type_Of(v)) {
    case TYPE_INTEGER:
        msec = 1000 * Int32(v);
        break;

    case TYPE_DECIMAL:
        msec = cast(REBINT, 1000 * VAL_DECIMAL(v));
        break;

    case TYPE_TIME:
        msec = cast(REBINT, VAL_NANO(v) / (SEC_SEC / 1000));
        break;

    default:
        panic (nullptr);  // avoid uninitialized msec warning
    }

    if (msec < 0)
        fail (Error_Out_Of_Range(KNOWN(v)));

    return cast(REBLEN, msec);
}


//
//  wait: native [
//
//  "Waits for a duration, port, or both."
//
//      value [~null~ any-number! time! port! block!]
//      /all "Returns all in a block"
//      /only "only check for ports given in the block to this function"
//  ]
//
DECLARE_NATIVE(WAIT)
{
    INCLUDE_PARAMS_OF_WAIT;

    REBLEN timeout = 0; // in milliseconds
    Array* ports = nullptr;
    REBINT n = 0;


    Cell* val;
    if (not Is_Block(ARG(VALUE)))
        val = ARG(VALUE);
    else {
        StackIndex base = TOP_INDEX;
        if (Reduce_To_Stack_Throws(OUT, ARG(VALUE)))
            return BOUNCE_THROWN;

        // !!! This takes the stack array and creates an unmanaged array from
        // it, which ends up being put into a value and becomes managed.  So
        // it has to be protected.
        //
        ports = Pop_Stack_Values(base);

        for (val = Array_Head(ports); NOT_END(val); val++) { // find timeout
            if (Pending_Port(KNOWN(val)))
                ++n;

            if (Is_Integer(val) or Is_Decimal(val) or Is_Time(val))
                break;
        }
        if (IS_END(val)) {
            if (n == 0) {
                Free_Unmanaged_Flex(ports);
                return nullptr; // has no pending ports!
            }
            timeout = ALL_BITS; // no timeout provided
        }
    }

    if (NOT_END(val)) {
        switch (Type_Of(val)) {
        case TYPE_INTEGER:
        case TYPE_DECIMAL:
        case TYPE_TIME:
            timeout = Milliseconds_From_Value(val);
            break;

        case TYPE_PORT:
            if (not Pending_Port(KNOWN(val)))
                return nullptr;
            ports = Make_Array(1);
            Append_Value(ports, KNOWN(val));
            timeout = ALL_BITS;
            break;

        case TYPE_BLANK:
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
        Init_Block(OUT, ports);

    // Process port events [stack-move]:
    if (Wait_Ports_Throws(OUT, ports, timeout, Bool_ARG(ONLY)))
        return BOUNCE_THROWN;

    assert(Is_Logic(OUT));

    if (IS_FALSEY(OUT)) { // timeout
        Sieve_Ports(nullptr);  // just reset the waked list
        return nullptr;
    }

    if (not ports)
        return nullptr;

    // Determine what port(s) waked us:
    Sieve_Ports(ports);

    if (not Bool_ARG(ALL)) {
        val = Array_Head(ports);
        if (not Is_Port(val))
            return nullptr;

        Copy_Cell(OUT, KNOWN(val));
    }

    return OUT;
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
DECLARE_NATIVE(WAKE_UP)
//
// Calls port update for native actors.
// Calls port awake function.
{
    INCLUDE_PARAMS_OF_WAKE_UP;

    FAIL_IF_BAD_PORT(ARG(PORT));

    VarList* ctx = Cell_Varlist(ARG(PORT));

    Value* actor = Varlist_Slot(ctx, STD_PORT_ACTOR);
    if (Is_Native_Port_Actor(actor)) {
        //
        // We don't pass `actor` or `event` in, because we just pass the
        // current call info.  The port action can re-read the arguments.
        //
        // !!! Most of the R3-Alpha event model is around just as "life
        // support".  Added assertion and convention here that this call
        // doesn't throw or return meaningful data... (?)
        //
        DECLARE_VALUE (verb);
        Init_Word(verb, CANON(ON_WAKE_UP));
        const Value* r = Do_Port_Action(level_, ARG(PORT), verb);
        assert(Is_Trash(r));
        UNUSED(r);
    }

    bool woke_up = true; // start by assuming success

    Value* awake = Varlist_Slot(ctx, STD_PORT_AWAKE);
    if (Is_Action(awake)) {
        const bool fully = true; // error if not all arguments consumed

        if (Apply_Only_Throws(OUT, fully, awake, ARG(EVENT), rebEND))
            fail (Error_No_Catch_For_Throw(OUT));

        if (not (Is_Logic(OUT) and VAL_LOGIC(OUT)))
            woke_up = false;
    }

    return Init_Logic(OUT, woke_up);
}


//
//  local-to-file: native [
//
//  {Converts a local system file path TEXT! to a Rebol FILE! path.}
//
//      return: [~null~ file!]
//          {The returned value should be a valid natural FILE! literal}
//      path [<maybe> text! file!]
//          {Path to convert (by default, only TEXT! for type safety)}
//      /pass
//          {Convert TEXT!, but pass thru FILE!, assuming it's canonized}
//      /dir
//          {Ensure input path is treated as a directory}
//  ]
//
DECLARE_NATIVE(LOCAL_TO_FILE)
{
    INCLUDE_PARAMS_OF_LOCAL_TO_FILE;

    Value* path = ARG(PATH);
    if (Is_File(path)) {
        if (not Bool_ARG(PASS))
            fail ("LOCAL-TO-FILE only passes through FILE! if /PASS used");

        return Init_File(
            OUT,
            Copy_Sequence_At_Len( // Copy (callers frequently modify result)
                Cell_Flex(path),
                VAL_INDEX(path),
                Cell_Series_Len_At(path)
            )
        );
    }

    return Init_File(
        OUT,
        To_REBOL_Path(path, Bool_ARG(DIR) ? PATH_OPT_SRC_IS_DIR : 0)
    );
}


//
//  file-to-local: native [
//
//  {Converts a Rebol FILE! path to TEXT! of the local system file path}
//
//      return: [~null~ text!]
//          {A TEXT! like "\foo\bar" is not a "natural" FILE! %\foo\bar}
//      path [<maybe> file! text!]
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
DECLARE_NATIVE(FILE_TO_LOCAL)
{
    INCLUDE_PARAMS_OF_FILE_TO_LOCAL;

    Value* path = ARG(PATH);
    if (Is_Text(path)) {
        if (not Bool_ARG(PASS))
            fail ("FILE-TO-LOCAL only passes through STRING! if /PASS used");

        return Init_Text(
            OUT,
            Copy_Sequence_At_Len( // Copy (callers frequently modify result)
                Cell_Flex(path),
                VAL_INDEX(path),
                Cell_Series_Len_At(path)
            )
        );
    }

    return Init_Text(
        OUT,
        To_Local_Path(
            path,
            REB_FILETOLOCAL_0
                | (Bool_ARG(FULL) ? REB_FILETOLOCAL_FULL : 0)
                | (Bool_ARG(NO_TAIL_SLASH) ? REB_FILETOLOCAL_NO_TAIL_SLASH : 0)
                | (Bool_ARG(WILD) ? REB_FILETOLOCAL_WILD : 0)
        )
    );
}


//
//  what-dir: native [
//  "Returns the current directory path."
//      ; No arguments
//  ]
//
DECLARE_NATIVE(WHAT_DIR)
{
    Value* current_path = Get_System(SYS_OPTIONS, OPTIONS_CURRENT_PATH);

    if (Is_File(current_path) || Is_Nulled(current_path)) {
        //
        // !!! Because of the need to track a notion of "current path" which
        // could be a URL! as well as a FILE!, the state is stored in the
        // system options.  For now--however--it is "duplicate" in the case
        // of a FILE!, because the OS has its own tracked state.  We let the
        // OS state win for files if they have diverged somehow--because the
        // code was already here and it would be more compatible.  But
        // reconsider the duplication.

        Value* refresh = OS_GET_CURRENT_DIR();
        Copy_Cell(current_path, refresh);
        rebRelease(refresh);
    }
    else if (not Is_Url(current_path)) {
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
        OUT,
        Type_Of(current_path),
        Copy_Non_Array_Flex_Core(Cell_Flex(current_path), NODE_FLAG_MANAGED),
        VAL_INDEX(current_path)
    );
}


//
//  change-dir: native [
//
//  {Changes the current path (where scripts with relative paths will be run).}
//
//      path [<maybe> file! url!]
//  ]
//
DECLARE_NATIVE(CHANGE_DIR)
{
    INCLUDE_PARAMS_OF_CHANGE_DIR;

    Value* arg = ARG(PATH);
    Value* current_path = Get_System(SYS_OPTIONS, OPTIONS_CURRENT_PATH);

    if (Is_Url(arg)) {
        // There is no directory listing protocol for HTTP (although this
        // needs to be methodized to work for SFTP etc.)  So this takes
        // your word for it for the moment that it's a valid "directory".
        //
        // !!! Should it at least check for a trailing `/`?
    }
    else {
        assert(Is_File(arg));

        bool success = OS_SET_CURRENT_DIR(arg);

        if (not success)
            fail (Error_Invalid(arg));
    }

    Copy_Cell(current_path, arg);

    RETURN (ARG(PATH));
}
