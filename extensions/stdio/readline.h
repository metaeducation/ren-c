//
//  File: %readline.h
//  Summary: "Shared Definitions for Windows/POSIX Console Line Reading"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2020 Ren-C Open Source Contributors
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
// Windows has a monolithic facility for reading a line of input from the
// user.  This single command call is blocking (also known as "cooked" as
// opposed to "raw") and very limited.  As an initial goal of updating some
// of the very old R3-Alpha input code, the more granular POSIX code for
// implementing a "GNU libreadline"-type facility is being abstracted to
// share pieces of implementation with Windows.
//
// This file will attempt to define the hooks that are shared between the
// Windows and POSIX smart consoles.
//


// !!! Note: %reb-c.h must be included prior to this for proper CPLUSPLUS_11
// detection (MSVC doesn't do this correctly).  We do not do it here because
// of lack of include guards, but maybe we should put include guards in?
//
/* #include %reb-c.h */


#if (! TO_WINDOWS) && defined(NO_TTY_ATTRIBUTES)
    // ...Linux with no terminal functions ...
#else

#define REBOL_SMART_CONSOLE

#include "rebol.h"

// !!! The history mechanism will be disconnected from the line editing
// mechanism--but for the moment, the line editing is the only place we
// get an Init() and Shutdown() opportunity.
//
extern REBVAL *Line_History;  // BLOCK! of TEXT!s


// The terminal is an opaque type which varies per operating system.  This
// is in C for now, but what it should evolve into is some kind of terminal
// PORT! which would have asynchronous events and behavior.
//
struct Reb_Terminal_Struct;
typedef struct Reb_Terminal_Struct STD_TERM;


extern int Term_Pos(STD_TERM *t);
extern REBVAL *Term_Buffer(STD_TERM *t);

extern STD_TERM *Init_Terminal();

extern void Term_Insert(STD_TERM *t, const REBVAL *v);
extern void Term_Seek(STD_TERM *t, unsigned int pos);
extern void Move_Cursor(STD_TERM *t, int count);
extern void Delete_Char(STD_TERM *t, bool back);
extern void Term_Clear_To_End(STD_TERM *t);

extern void Term_Beep(STD_TERM *t);

extern void Quit_Terminal(STD_TERM *t);

// This attempts to get one unit of "event" from the console.  It does not
// use the Rebol EVENT! datatype at this time.  Instead it returns:
//
//    CHAR!, TEXT! => printable characters (includes space, but not newline)
//    WORD! => keystroke or control code
//    BAD-WORD! => interrupted by HALT or Ctrl-C
//
// It does not do any printing or handling while fetching the event.
//
// The reason it returns accrued TEXT! in runs (vs. always returning each
// character individually) is because of pasting.  Taking the read() buffer
// in per-line chunks is much faster than trying to process each character
// insertion with its own code (it's noticeably slow).  But at typing speed
// it's fine.
//
// Note Ctrl-C comes from the SIGINT signal and not from the physical detection
// of the key combination "Ctrl + C", which this routine should not receive
// due to deferring to the default UNIX behavior for that (otherwise, scripts
// could not be cancelled unless they were waiting at an input prompt).
//
// !!! The idea is that if there is no event available, this routine will
// return a nullptr.  That would allow some way of exiting the read() to
// do another operation (process network requests for a real-time chat, etc.)
// This is at the concept stage at the moment.
//
extern REBVAL *Try_Get_One_Console_Event(
    STD_TERM *t,
    bool buffered,
    int timeout
);

// !!! This is what ESCAPE does; it's probably something that should be
// done at a more granular level of spooling ahead "peeked" console events
// vs. needing a separate API entry point.
//
extern void Term_Abandon_Pending_Events(STD_TERM *t);

extern STD_TERM *Term_IO;

extern REBVAL *Read_Line(STD_TERM *t);  // defined in %p-stdio.c for now

#endif  // end smart console branch
