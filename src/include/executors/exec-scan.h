//
//  File: %exec-scan.h
//  Summary: {Flags and Frame State for Scanner_Executor()}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2022 Ren-C Open Source Contributors
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The executor state has to be defined in order to be used (easily) in the
// union of the Reb_Frame.
//

// (Note: %sys-do.h needs to call into the scanner if Fetch_Next_In_Frame() is
// to be inlined at all--at its many time-critical callsites--so the scanner
// has to be in the internal API)
//
#include "sys-scan.h"


#define EXECUTOR_SCAN &Scanner_Executor  // shorthand in Xxx_Executor_Flag()


//=//// SCAN_EXECUTOR_FLAG_NEWLINE_PENDING ////////////////////////////////=//
//
// CELL_FLAG_LINE appearing on a value means that there is a line break
// *before* that value.  Hence when a newline is seen, it means the *next*
// value to be scanned will receive the flag.
//
#define SCAN_EXECUTOR_FLAG_NEWLINE_PENDING \
    EVAL_FLAG_24


//=//// SCAN_EXECUTOR_FLAG_25 /////////////////////////////////////////////=//
//
#define SCAN_EXECUTOR_FLAG_25 \
    EVAL_FLAG_25


//=//// SCAN_EXECUTOR_FLAG_26 /////////////////////////////////////////////=//
//
#define SCAN_EXECUTOR_FLAG_26 \
    EVAL_FLAG_26


//=//// SCAN_EXECUTOR_FLAG_27 /////////////////////////////////////////////=//
//
#define SCAN_EXECUTOR_FLAG_27 \
    EVAL_FLAG_27


//=//// SCAN_EXECUTOR_FLAG_27 /////////////////////////////////////////////=//
//
#define SCAN_EXECUTOR_FLAG_27 \
    EVAL_FLAG_27


//=//// SCAN_EXECUTOR_FLAG_24 /////////////////////////////////////////////=//
//
#define SCAN_EXECUTOR_FLAG_28 \
    EVAL_FLAG_28


//=//// SCAN_EXECUTOR_FLAG_29 /////////////////////////////////////////////=//
//
#define SCAN_EXECUTOR_FLAG_29 \
    EVAL_FLAG_29


//=//// SCAN_EXECUTOR_FLAG_30 /////////////////////////////////////////////=//
//
#define SCAN_EXECUTOR_FLAG_30 \
    EVAL_FLAG_30


//=//// SCAN_EXECUTOR_FLAG_31 /////////////////////////////////////////////=//
//
#define SCAN_EXECUTOR_FLAG_31 \
    EVAL_FLAG_31


typedef struct rebol_scan_state {  // shared across all levels of a scan
    //
    // Beginning and end positions of currently processed token.
    //
    const REBYTE *begin;
    const REBYTE *end;

    // If feed is NULL, then it is assumed that the `begin` is the source of
    // the UTF-8 data to scan.  Otherwise, it is a variadic feed of UTF-8
    // strings and values that are spliced in.
    //
    REBFED *feed;

    // Module to bind words to while scanning.  Splices from the feed will
    // not count...only words bound from text portions of the scan.
    //
    option(REBCTX*) context;

    const REBSTR *file;  // file currently being scanned (or anonymous)

    REBLIN line;  // line number where current scan position is
    const REBYTE *line_head;  // pointer to head of current line (for errors)

    // The "limit" feature was not implemented, scanning just stopped at '\0'.
    // It may be interesting in the future, but it doesn't mix well with
    // scanning variadics which merge REBVAL and UTF-8 strings together...
    //
    /* const REBYTE *limit; */

    // !!! R3-Alpha had a /RELAX mode for TRANSCODE, which offered the ability
    // to get a partial scan with an error on a token.  An error propagating
    // out via fail() would not allow a user to get such partial results
    // (unless they were parameters to the error).  The feature was not really
    // specified well...but without some more recoverable notion of state in a
    // nested parse, only errors at the topmost level can be meaningful.  So
    // Ren-C has this flag which is set by the scanner on failure.  A better
    // notion would likely integrate with PARSE.  In any case, we track the
    // depth so that a failure can potentially be recovered from at 0.
    //
    REBLEN depth;
} SCAN_STATE;

typedef struct rebol_scan_level {  // each array scan corresponds to a level
    SCAN_STATE *ss;  // shared state of where the scanner head currently is

    // '\0' => top level scan
    // ']' => this level is scanning a block
    // ')' => this level is scanning a group
    // '/' => this level is scanning a path
    // '.' => this level is scanning a tuple
    //
    // (Chosen as the terminal character to use in error messages for the
    // character we are seeking to find a match for).
    //
    REBYTE mode;

    REBLEN start_line;
    const REBYTE *start_line_head;

    REBFLGS opts;

    // !!! Before stackless, these were locals in Scan_To_Stack()
    //
    bool just_once;
    REBLEN quotes_pending;
    enum Reb_Token token;
    enum Reb_Token prefix_pending;

} SCAN_LEVEL;
