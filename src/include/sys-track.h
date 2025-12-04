//
//  file: %sys-track.h
//  summary: "*VERY USEFUL* Debug Tracking Capabilities for Cell Payloads"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2025 Ren-C Open Source Contributors
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
// looking at the cell in a watchlist.  It is also reported by crash().
//


//=//// TRACKING FUNCTION ////////////////////////////////////////////////=//
//
// In the C++ build, the TRACK() macro is polymorphic, and gives you the
// same Cell* you passed in in terms of the type.  So if it was a Cell* it
// will come back a Cell*, etc.
//
// (In the C build, Value*, Stable*, Element*, OnStack(Element*), etc. are
// all just Cell* pointers.)
//
// 1. It's currently possible to enable or disable tick counting in the
//    Trampoline.  This is because while tick counting is very useful even
//    in release builds, it isn't free...and some configurations might not
//    want to pay for counting them.  If not enabled, the tick will be 0.
//
#if DEBUG_TRACK_EXTEND_CELLS

  POSSIBLY(TRAMPOLINE_COUNTS_TICKS);  // might not be enabled [1]

  #if CPLUSPLUS_11
    template<typename T>
    INLINE T Track_Cell_Debug(
        T cell,
        const char *file,
        int line
    ){
        cell->file = file;
        cell->line = line;
        cell->tick = TICK;  // TICK is 0 if not TRAMPOLINE_COUNTS_TICKS [1]
        cell->touch = 0;
        return cell;
    }
  #else
    INLINE Cell* Track_Cell_Debug(
        Cell* cell,
        const char *file,
        int line
    ){
        cell->file = file;
        cell->line = line;
        cell->tick = TICK;  // TICK is 0 if not TRAMPOLINE_COUNTS_TICKS [1]
        cell->touch = 0;
        return cell;
    }
  #endif
#endif


//=//// CELL "TOUCH" TICK MONITORING //////////////////////////////////////=//
//
// The 4th slot in the tracking information is used for another tick value,
// called "touch".  This can be used at arbitrary points in debugging to
// indicate the last time the Cell underwent a change being studied.
//
#if DEBUG_TRACK_EXTEND_CELLS && TRAMPOLINE_COUNTS_TICKS
    #define Touch_Cell(cell) \
        ((cell)->touch = TICK)

    #define Touch_Cell_If_Debug(cell) \
        Touch_Cell(cell)
#else
    #define Touch_Cell(cell) \
        STATIC_FAIL(need_DEBUG_TRACK_EXTEND_CELLS_and_TRAMPOLINE_COUNTS_TICKS)

    #define Touch_Cell_If_Debug(cell)  NOOP
#endif


//=//// TRACK MACRO ///////////////////////////////////////////////////////=//
//
// 1. There is no guarantee of evaluation order of function arguments in C.
//    So if there's code like:
//
//      #define Init_Logic(out,flag)  Init_Logic_Core(TRACK(out), (flag))
//
//    The tracking information may be put in the Cell* before* or *after* the
//    second argument is evaluated.  So imagine something like:
//
//      Init_Logic(OUT, not Cell_Logic(OUT));
//
//    So TRACK() can't do anything that would corrupt the release-build-bits
//    of `out`, since it might run first.  This is why the tracking info is
//    fully separate, and doesn't try to exploit that not all cell types use
//    all bits to hide more information.
//
#if DEBUG_TRACK_EXTEND_CELLS
    #define TRACK(cell) /* see important note [1] above! */ \
        Track_Cell_Debug((cell), __FILE__, __LINE__)
#else
    #define TRACK(cell)  (cell)
#endif
