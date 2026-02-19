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


//=//// TRACK_FLAG_VALID_EVAL_TARGET //////////////////////////////////////=//
//
// We don't want every Cell to be considered a target for evaluation:
//
// * Evaluation can move memory, so anything that an arbitrary evaluation
//   could relocate is suspect (source array cells, object variables when
//   the object might expand, etc.)
//
// * Arbitrary evaulation can produce antiforms, so we wouldn't want Source
//   arrays to be targets, as their cells have to be Element*
//
// * Evaluation target cells can go through moments of arbitrary states like
//   being erased or unreadable.  These states should not become visible to
//   code or to debuggers.
//
// If this flag is set, then the Cell is a valid target for evaluations.
// It's set on the SPARE and SCRATCH cells, as well as those made by
// DECLARE_VALUE().  During frame fulfillment and before a FRAME!'s slots are
// exposed to users, the argument slots are temporarily made valid targets
// as well.
//
#define TRACK_FLAG_VALID_EVAL_TARGET \
    FLAG_LEFT_BIT(0)


//=//// TRACK_FLAG_SHIELD_FROM_WRITES /////////////////////////////////////=//
//
// While usermode variables can be protected from being written by a flag that
// is exclusive to VarList, there is no generic bit for stopping an arbitrary
// Cell (such as one in the middle of a source array) from being written by
// low-level code.  Such a bit would not be a good idea to expose--as it would
// cost on every cell write.  And it would use up a scarce CELL_FLAG_XXX bit.
//
// But for debugging it can be helpful.  So using the extra track_flags, we
// can update the Assert_Cell_Writable() in DEBUG_TRACK_EXTEND_CELLS builds
// to account for an extra flag.
//
#define TRACK_FLAG_SHIELD_FROM_WRITES \
    FLAG_LEFT_BIT(1)


#define TRACK_MASK_NONE  0


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
    INLINE T Force_Cell_Tracking(
        T&& cell,  // && to avoid Sink(T) "re-corrupting" (broken!)
        Flags track_flags,
        const char *file,
        int line
    ){
        cell->track_flags.bits = track_flags | FLAG_FOURTH_BYTE(0xBD);
        cell->file = file;
        cell->line = line;
        cell->tick = TICK;  // TICK is 0 if not TRAMPOLINE_COUNTS_TICKS [1]
        return cell;
    }

    template<typename T>
    INLINE T Update_Cell_File_Line_Tick(
        T&& cell,  // && to avoid Sink(T) "re-corrupting" (broken!)
        const char *file,
        int line
    ){
        assert(FOURTH_BYTE(&cell->track_flags.bits) == 0xBD);
        cell->file = file;
        cell->line = line;
        cell->tick = TICK;  // TICK is 0 if not TRAMPOLINE_COUNTS_TICKS [1]
        return cell;
    }
  #else
    INLINE Cell* Force_Cell_Tracking(
        Cell* cell,
        Flags track_flags,
        const char *file,
        int line
    ){
        cell->track_flags.bits = track_flags | FLAG_FOURTH_BYTE(0xBD);
        cell->file = file;
        cell->line = line;
        cell->tick = TICK;  // TICK is 0 if not TRAMPOLINE_COUNTS_TICKS [1]
        return cell;
    }

    INLINE Cell* Update_Cell_File_Line_Tick(
        Cell* cell,
        const char *file,
        int line
    ){
        assert(FOURTH_BYTE(&cell->track_flags.bits) == 0xBD);
        cell->file = file;
        cell->line = line;
        cell->tick = TICK;  // TICK is 0 if not TRAMPOLINE_COUNTS_TICKS [1]
        return cell;
    }
  #endif
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
        Update_Cell_File_Line_Tick((cell), __FILE__, __LINE__)

    #define FORCE_TRACK(cell,track_flags) \
        Force_Cell_Tracking((cell), (track_flags), __FILE__, __LINE__)
#else
    #define TRACK(cell)  PASSTHRU(cell)

    #define FORCE_TRACK(cell,track_flags)  PASSTHRU(cell)
#endif

#define FORCE_TRACK_0(cell) \
    FORCE_TRACK((cell), TRACK_MASK_NONE)

#define FORCE_TRACK_VALID_EVAL_TARGET(cell) \
    FORCE_TRACK((cell), TRACK_FLAG_VALID_EVAL_TARGET)


#if DEBUG_TRACK_COPY_PRESERVES
    #define MAYBE_TRACK(cell)  PASSTHRU(cell)
#else
    #define MAYBE_TRACK(cell)  TRACK(cell)
#endif


//=//// CELL "TOUCH" //////////////////////////////////////////////////////=//
//
// This can be used at arbitrary points in debugging to indicate the last
// time the Cell underwent a change, that wasn't being copied.
//
// Note: `touch` used to be an independent field from `tick`, but it was
// determined that the lack of use of the feature meant it would be better
// to use a 4th Cell slot for more useful flags.
//
#if DEBUG_TRACK_EXTEND_CELLS && TRAMPOLINE_COUNTS_TICKS
    #define Touch_Cell(cell) \
        ((cell)->tick = TICK)

    #define Touch_Cell_If_Debug(cell) \
        Touch_Cell(cell)
#else
    #define Touch_Cell(cell) \
        STATIC_FAIL(need_DEBUG_TRACK_EXTEND_CELLS_and_TRAMPOLINE_COUNTS_TICKS)

    #define Touch_Cell_If_Debug(cell)  USED(cell)
#endif


//=//// TRACK FLAG HELPERS ////////////////////////////////////////////////=//

#if DEBUG_TRACK_EXTEND_CELLS
    #define Get_Track_Flag(c,name) \
        (((c)->track_flags.bits & TRACK_FLAG_##name) != 0)

    #define Not_Track_Flag(c,name) \
        (((c)->track_flags.bits & TRACK_FLAG_##name) == 0)

    #define Set_Track_Flag(c,name) /* cast away const [1] */ \
        m_cast(HeaderUnion*, &(c)->track_flags)->bits \
            |= TRACK_FLAG_##name

    #define Clear_Track_Flag(c,name) /* cast away const [1] */ \
        m_cast(HeaderUnion*, &(c)->track_flags)->bits \
            &= ~TRACK_FLAG_##name
#endif


//=//// CELL "SHIELD" /////////////////////////////////////////////////////=//
//
// If we are doing a build where every attempt to a cell write is checked,
// and we have the Cell `track_flags` available, we have the advantage of
// being able to guard arbitrary cells against writes.
//
// 1. We want to do USED(cell) here instead of a NOOP, because even though
//    the shield macros are no-ops in builds without the tracking, the
//    expression that produced the cell may have side effects we need.

#if DEBUG_TRACK_EXTEND_CELLS && DEBUG_CELL_READ_WRITE
   INLINE void Track_Shield_Cell(Cell* cell) {
       assert(Not_Track_Flag(cell, SHIELD_FROM_WRITES));
       Set_Track_Flag(cell, SHIELD_FROM_WRITES);
   }

   INLINE void Track_Unshield_Cell(Cell* cell) {
       assert(Get_Track_Flag(cell, SHIELD_FROM_WRITES));
       Clear_Track_Flag(cell, SHIELD_FROM_WRITES);
   }

   #define Track_Clear_Cell_Shield(cell) \
       Clear_Track_Flag((cell), SHIELD_FROM_WRITES);

    #define Assert_Cell_Shielded_If_Tracking(cell) \
        assert(Get_Track_Flag((cell), SHIELD_FROM_WRITES))
#else
    #define Track_Shield_Cell(cell)  USED(cell)  // may have effects [1]
    #define Track_Unshield_Cell(cell)  USED(cell)
    #define Track_Clear_Cell_Shield(cell)  USED(cell)
    #define Assert_Cell_Shielded_If_Tracking(cell)  NOOP  // no effects allowed
#endif
