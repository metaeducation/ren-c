//
//  File: %sys-track.h
//  Summary: "*VERY USEFUL* Debug Tracking Capabilities for Cell Payloads"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
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
// Using the build setting DEBUG_TRACK_EXTEND_CELLS, cells are doubled in
// size...and carry the file, line, and tick where they were initialized.
//
// The information should be viewable in the C/C++ debug inspector when
// looking at the cell in a watchlist.  It is also reported by panic().
//

#define Is_Fresh(v) \
    (0 == ((v)->header.bits & (FLAG_KIND3Q_BYTE(255) | FLAG_HEART_BYTE(255))))

#if defined(DEBUG_TRACK_EXTEND_CELLS)  // assume DEBUG_COUNT_TICKS

    #define TOUCH_CELL(c) \
        ((c)->touch = TG_Tick)

    inline static REBVAL *Track_Cell_Debug(
        RELVAL *v,
        const char *file,
        int line
    ){
      #if !defined(NDEBUG)
        if (not Is_Fresh(v))
            panic (v);
      #endif
        v->file = file;
        v->line = line;
        v->tick = TG_Tick;
        v->touch = 0;
        return v;
    }

  #ifdef CPLUSPLUS_11
    inline static REBVAL *Track_Cell_Debug(
        REBVAL *v,
        const char *file,
        int line
    ){
      #if !defined(NDEBUG)
        if (not Is_Fresh(v))
            panic (v);
      #endif
        v->file = file;
        v->line = line;
        v->tick = TG_Tick;
        v->touch = 0;
        return v;
    }
  #endif

    // NOTE: There is no guarantee of evaluation order of function arguments
    // in C.  So if there's code like:
    //
    //    #define Init_Logic(out,flag)  Init_Logic_Core(TRACK(out), (flag))
    //
    // The tracking information may be put in the cell *before* or *after*
    // the right hand side is evaluated.  So imagine something like:
    //
    //     Init_Logic(D_OUT, not VAL_LOGIC(D_OUT));
    //
    // So TRACK() can't do anything that would corrupt the release-build-bits
    // of `out`, since it might run first.  This is why the tracking info is
    // fully separate, and doesn't try to exploit that not all cell types use
    // all bits to hide more information.
    //
    // !!! Update: The point is moot now that Init_Xxx() routines require
    // cells to be cleared out, because a RESET() can't be put in the init
    // for similar reasons.  Debug tracking is still separate for simplicity.
    //
    #define TRACK(v) \
        Track_Cell_Debug((v), __FILE__, __LINE__)

#else

    #define TRACK(v) (v)

#endif
