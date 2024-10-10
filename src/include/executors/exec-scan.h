//
//  File: %exec-scan.h
//  Summary: {Flags and Level State for Scanner_Executor()}
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
// union of the Level.
//

// (Note: %sys-do.h needs to call into the scanner if Fetch_Next_In_Feed() is
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
    LEVEL_FLAG_24


//=//// SCAN_EXECUTOR_FLAG_JUST_ONCE //////////////////////////////////////=//
//
// Supporting flag for TRANSCODE:NEXT
//
#define SCAN_EXECUTOR_FLAG_JUST_ONCE \
    LEVEL_FLAG_25


//=//// SCAN_EXECUTOR_FLAG_NULLEDS_LEGAL //////////////////////////////////=//
//
// NULL splice in top level of rebValue()
//
// !!! No longer used; null antiforms now always splice as ~null~ quasiform
//
#define SCAN_EXECUTOR_FLAG_NULLEDS_LEGAL \
    LEVEL_FLAG_26


//=//// SCAN_EXECUTOR_FLAG_LOCK_SCANNED ///////////////////////////////////=//
//
// Lock Flexes as they are loaded.
//
// !!! This also does not seem to be used, likely supplanted by CONST.
//
#define SCAN_EXECUTOR_FLAG_LOCK_SCANNED \
    LEVEL_FLAG_27


//=//// SCAN_EXECUTOR_FLAG_INTERSTITIAL_SCAN //////////////////////////////=//
//
// We know from the Level_State_Byte() of the scanner that it's interstitial
// if the character is "." or ":" or "/", but instead of comparing that
// state byte against those three characters we can do it in one quick flag
// check by putting this flag on the scan.
//
#define SCAN_EXECUTOR_FLAG_INTERSTITIAL_SCAN \
    LEVEL_FLAG_28


//=//// SCAN_EXECUTOR_FLAG_29 /////////////////////////////////////////////=//
//
#define SCAN_EXECUTOR_FLAG_29 \
    LEVEL_FLAG_29


//=//// SCAN_EXECUTOR_FLAG_30 /////////////////////////////////////////////=//
//
#define SCAN_EXECUTOR_FLAG_30 \
    LEVEL_FLAG_30


//=//// SCAN_EXECUTOR_FLAG_31 /////////////////////////////////////////////=//
//
#define SCAN_EXECUTOR_FLAG_31 \
    LEVEL_FLAG_31


// Flags that should be preserved when recursing the scanner
//
#define SCAN_EXECUTOR_MASK_RECURSE \
    (SCAN_EXECUTOR_FLAG_NULLEDS_LEGAL \
        | SCAN_EXECUTOR_FLAG_LOCK_SCANNED)


struct TranscodeStateStruct {  // shared across all levels of a scan
    Option(const String*) file;  // currently scanning (or anonymous)

    LineNumber line;  // line number where current scan position is
    const Byte* line_head;  // pointer to head of current line (for errors)

    const Byte* at;  // where the next ScanState should consume material from

    // The "limit" feature was not implemented, scanning just stopped at '\0'.
    // It may be interesting in the future, but it doesn't mix well with
    // scanning variadics which merge cells and UTF-8 strings together...
    //
    /* const Byte* limit; */
};

typedef struct TranscodeStateStruct TranscodeState;


struct ScannerExecutorStateStruct {  // each array scan has a level
    TranscodeState* ss;  // shared state of where the scanner head currently is

    // Beginning and end positions of currently processed token.
    //
    const Byte* begin;
    const Byte* end;

    LineNumber start_line;
    const Byte* start_line_head;

    Count quotes_pending;
    Option(Sigil) sigil_pending;  // includes SIGIL_QUASI
};

typedef struct ScannerExecutorStateStruct ScanState;


//=//// SCANNER LEVEL STATE BYTES /////////////////////////////////////////=//
//
// To try and squeak out a little bit of efficiency, the state byte for the
// level is equal to the character mode of the scan.  It's grafted onto the
// flags when the level is created (already a sunk cost).  And it keeps from
// needing to find another place to put the byte.
//
// 1. Because you can't run BOUNCE_CONTINUE while the state is zero, we have
//    a non-zero state used as the generic state for the outermost scan.
//    And since the state byte is used to encode the current scanning mode,
//    it can't be overwritten during a continuation to be a signal to jump
//    to a different location in the code when the continuation finishes.
//    Instead, the decision is based made on whether there's a sublevel
//    pushed or not.  (If it isn't, then it's an initial entry.  If it is,
//    then a child scan was running.)
//
// 2. For lists, we use the terminal character--which makes it fast to compare
//    with what we are seeking to find a match for (and also more useful in
//    error messages)
//
enum {
    ST_SCANNER_UNUSED_STATE = STATE_0,  // won't work with BOUNCE_CONTINUE [1]

    ST_SCANNER_OUTERMOST_SCAN,  // used instead of STATE_0 [1]

    ST_SCANNER_GROUP_MODE = 41,  // ')' terminal character [2]
    ST_SCANNER_TUPLE_MODE = 46,  // '.'
    ST_SCANNER_PATH_MODE = 47,  // '/'
    ST_SCANNER_CHAIN_MODE = 58,  // ':'
    ST_SCANNER_BLOCK_MODE = 93,  // ']' terminal character [2]
    ST_SCANNER_FENCE_MODE = 125  // '}'
};
