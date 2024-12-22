//
//  File: %reb-process.h
//  Summary: "Header file for 'Process-oriented' extension module"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 Atronix Engineering
// Copyright 2012-2019 Ren-C Open Source Contributors
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


INLINE Bounce Delegate_Fail_Permission_Denied(void) {
    return "fail -{The process does not have enough permission}-";
}

INLINE Bounce Delegate_Fail_No_Process(const Value* arg) {
    return rebDelegate(
        "fail [-{The target process (group) does not exist:}-", arg, "]"
    );
}

#if TO_WINDOWS
    INLINE Bounce Delegate_Fail_Terminate_Failed(DWORD err) {  // GetLastError()
        return rebDelegate(
            "fail [-{Terminate failed with error number:}-", rebI(err), "]"
        );
    }
#endif


// !!! The original implementation of CALL from Atronix had to communicate
// between the CALL native (defined in the core) and the host routine
// Call_Core, which was not designed to operate on Rebol types.
// Hence if the user was passing in a BLOB! to which the data for the
// standard out or standard error was to be saved, it was produced in full
// in a buffer and returned, then appended.  This wastes space when compared
// to just appending to the string or binary itself.  With CALL rethought
// as an extension with access to the internal API, this could be changed...
// though for the moment, a malloc()'d buffer is expanded independently by
// BUF_SIZE_CHUNK and returned to CALL.
//
#define BUF_SIZE_CHUNK 4096

Bounce Call_Core(Level* level_);

INLINE char Get_Char_For_Stream_Mode(const RebolValue* mode) {
    return rebUnboxChar(
        "switch @", mode, "[",
            "'inherit [#i]",
            "'none [#n]",
            "fail -{WORD! for Stream Mode must be INHERIT or NONE}-",
        "]"
    );
}
