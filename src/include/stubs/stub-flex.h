//
//  file: %stub-flex.h
//  summary: "Flex definitions AFTER %tmp-internals.h"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2024 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
// REBOL is a trademark of REBOL Technologies
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
// This file contains the basic API for dealing with a Flex.  It's similar
// to a resizable vector or array in other languages...though its
// implementation is currently more like a "deque" (double-ended-queue):
//
//   https://en.wikipedia.org/wiki/Double-ended_queue
//
// In any case, it is an abstraction which represents a contiguous region of
// memory containing equally-sized elements...but with several additional
// features that are specific to the needs of Rebol.  These features allow
// storing of a byte representing the "Flavor" of a Flex, as well as several
// hidden pointers (Misc, Link) and many Flags (Leader, Info).
//
// (see %struct-flex.h for a more detailed explanation of the implementation.)
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// * One particularly powerful feature that distinguishes Ren-C's Flex
//   implementation from the R3-Alpha "REBSER" counterpart is that if you
//   build the code as C++, the `const` qualifier is used to systemically
//   enforce explicit requests to gain mutable access to data.
//


//=//// FLEX ACCESSIBILITY ////////////////////////////////////////////////=//
//
// An inaccessible Flex is one which may still have extant references, but
// the data is no longer available.  Some internal mechanics can create this
// situation, such as EVAL of a FRAME! which steals the memory of the frame
// to execute it...leaving the old stub as inaccessible.  There is also a
// FREE operation that users can use to manually throw away data.
//
// It would be costly if all Flex access operations had to check the
// accessibility bit.  Instead, the general pattern is that code that extracts
// Flex from a cell, e.g. Cell_Array(), performs a check to make sure that
// the Flex is accessible at the time of extraction.  Subsequent access of
// the extracted Flex is then unchecked.
//
// When the GC runs, it canonizes all inaccessible Flexes to a single canon
// inaccessible stub.  This compacts memory of references that have expired.
//
// 1. We can't just set BASE_FLAG_UNREADABLE, because if the only flag that
//    was set in the Stub header was BASE_FLAG_BASE then this would give
//    us a bit pattern of 11000000, which is FREE_POOLUNIT_BYTE.  We want
//    the decayed state to be distinct and potentially encode more info,
//    so we push it out of the valid leading UTF-8 byte range...the patterns
//    that we actually have are:
//
//        0xF5 (11110101), 0xF6 (11110110), 0xF7 (11110111)
//

INLINE bool Is_Stub_Diminished(const Stub* s) {
    if (Is_Base_Readable(s))
        return false;
    BaseByte b = BASE_BYTE(s);
    assert(b == DIMINISHED_CANON_BYTE or b == DIMINISHED_NON_CANON_BYTE);
    UNUSED(b);
    return true;
}

#if CPLUSPLUS_11  // cast(Flex*, stub) of diminished Stub would assert
    bool Is_Stub_Diminished(const Flex*) = delete;
#endif

#define Not_Stub_Diminished(s) \
    (not Is_Stub_Diminished(s))

#define STUB_MASK_NON_CANON_UNREADABLE \
    (BASE_FLAG_BASE | BASE_FLAG_UNREADABLE | STUB_MASK_TASTE)

INLINE Stub* Set_Stub_Unreadable(Stub* s) {
    assert(Is_Base_Readable(s));
    s->header.bits = STUB_MASK_NON_CANON_UNREADABLE;
    assert(BASE_BYTE(s) == DIMINISHED_NON_CANON_BYTE);

    Corrupt_If_Needful(s->link.corrupt);
    Corrupt_If_Needful(s->misc.corrupt);
    Corrupt_If_Needful(s->content);
    Corrupt_If_Needful(s->info.corrupt);

    return s;
}


//=//// FLEX "FLAG" BITS //////////////////////////////////////////////////=//
//
// See definitions of FLEX_FLAG_XXX.
//
// Using token pasting macros achieves some brevity, but also helps to avoid
// mixups with FLEX_INFO_XXX!
//
// 1. Flex flags are managed distinctly from conceptual immutability of their
//    data, and so we we cast away constness.

#define Get_Flex_Flag(f,name) \
    ((ensure(Flex*, (f))->header.bits & FLEX_FLAG_##name) != 0)

#define Not_Flex_Flag(f,name) \
    ((ensure(Flex*, (f))->header.bits & FLEX_FLAG_##name) == 0)

#define Set_Flex_Flag(f,name) \
    (m_cast(Flex*, ensure(Flex*, (f)))->header.bits |= FLEX_FLAG_##name)

#define Clear_Flex_Flag(f,name) \
    (m_cast(Flex*, ensure(Flex*, (f)))->header.bits &= (~ FLEX_FLAG_##name))


//=//// FLEX "INFO" BITS (or INODE) ///////////////////////////////////////=//
//
// See definitions of FLEX_INFO_XXX.
//
// Using token pasting macros helps avoid mixups with FLEX_FLAG_XXX!
//
// Not all Flex Stubs have info bits, as some use Stub.node to store a GC
// markable Base.
//
// 1. See mutability notes on Set_Flex_Flag()/Get_Flex_Flag().  The same
//    applies to the info flags.
//
// 2. We check that the info is being used for bits, not an "INODE".
//    Assume Flavor has INFO_NEEDS_MARK right.
//
#if NO_CPLUSPLUS_11
    #define FLEX_INFO(f) \
        m_cast(Flex*, ensure(Flex*, (f)))->info.flags  // [1]
#else
    INLINE uintptr_t &FLEX_INFO(const Flex* f) {
        assert(Not_Stub_Flag(f, INFO_NEEDS_MARK));  // [2]
        return m_cast(Flex*, f)->info.flags;  // [1]
    }
#endif

#define Get_Flex_Info(f,name) \
    ((FLEX_INFO(f) & FLEX_INFO_##name) != 0)

#define Not_Flex_Info(f,name) \
    ((FLEX_INFO(f) & FLEX_INFO_##name) == 0)

#define Set_Flex_Info(f,name) \
    FLEX_INFO(f) |= FLEX_INFO_##name

#define Clear_Flex_Info(f,name) \
    FLEX_INFO(f) &= ~FLEX_INFO_##name


//=//// USED_BYTE (same location as MIRROR_BYTE) //////////////////////////=//
//
// This byte is used by non-dynamic Flex to store the small lengths.  Arrays
// do not use it (they indicate the length 0 or 1 by whether the cell is
// poison when non-dynamic).
//
// See MIRROR_BYTE() for the application of this to source arrays.
//

#define USED_BYTE(f) \
    SECOND_BYTE(&FLEX_INFO(f))


//=//// FLEX CAPACITY AND TOTAL SIZE //////////////////////////////////////=//
//
// See documentation of `bias` and `rest` in %struct-stub.h
//
// In R3-Alpha, the bias was not a full REBLEN but was limited in range to
// 16 bits or so.  This means 16 info bits are likely available if needed
// for a dynamic Flex...though it would complicate the logic for biasing
// to have to notice when you TAKE 65535 units from the head of a larger
// Flex and need to allocate a new pointer (though this needs to be
// done anyway, otherwise memory is wasted).
//

INLINE bool Is_Flex_Biased(const Flex* f) {
    assert(Get_Stub_Flag(f, DYNAMIC));
    return not Is_Stub_Varlist(f);
}

INLINE Length Flex_Bias(const Flex* f) {
    if (not Is_Flex_Biased(f))
        return 0;
    return (BONUS_FLEX_BIAS(f) >> 16) & 0xffff;
}

#define MAX_FLEX_BIAS 0x1000

INLINE void Set_Flex_Bias(Flex* f, REBLEN bias) {
    assert(Is_Flex_Biased(f));
    BONUS_FLEX_BIAS(f) = (BONUS_FLEX_BIAS(f) & 0xffff) | (bias << 16);
}

INLINE void Add_Flex_Bias(Flex* f, REBLEN b) {
    assert(Is_Flex_Biased(f));
    BONUS_FLEX_BIAS(f) += b << 16;
}

INLINE void Subtract_Flex_Bias(Flex* f, REBLEN b) {
    assert(Is_Flex_Biased(f));
    BONUS_FLEX_BIAS(f) -= b << 16;
}

INLINE Length Flex_Rest(const Flex* f) {
    if (Get_Stub_Flag(f, DYNAMIC))
        return f->content.dynamic.rest;

    if (Stub_Holds_Cells(f))
        return 1;  // capacity of singular non-dynamic arrays is exactly 1

    assert(sizeof(f->content) % Flex_Wide(f) == 0);
    return sizeof(f->content) / Flex_Wide(f);
}

INLINE Size Flex_Total(const Flex* f)
  { return (Flex_Rest(f) + Flex_Bias(f)) * Flex_Wide(f); }


//=//// NUMBER OF WIDTH-SIZED UNITS "USED" IN FLEX ////////////////////////=//
//
// There is an optimization based on STUB_FLAG_DYNAMIC that allows data
// which is sizeof(Cell) or smaller to fit directly in the Flex Stub.
//
// 1. If a Flex is dynamically allocated out of the memory pools, then
//    without the data itself taking up the StubContent, there's room for a
//    full used count in the content.
//
// 2. A non-dynamic Array can store one or zero cells in the StubContent.
//    We report the units used as being 0 if it's the distinguished case of
//    a poisoned cell (added benefit: catches stray writes).
//
// 3. Other non-dynamic Flexes are short, and so they use a byte out of the
//    Flex Info to store the units used.  (This byte is currently free for
//    other purposes in cases [1] and [2].)
//

INLINE Length Flex_Used(const Flex* f) {
    if (Get_Stub_Flag(f, DYNAMIC))
        return f->content.dynamic.used;  // length stored in header [1]
    if (Stub_Holds_Cells(f)) {
        if (Is_Cell_Poisoned(&f->content.fixed.cell))  // empty singular [2]
            return 0;
        return 1;  // one-element singular array [2]
    }
    return USED_BYTE(f);  // small Flex length < sizeof(StubContent) [3]
}

INLINE Length Flex_Dynamic_Used(const Flex* f) {
    assert(Get_Stub_Flag(f, DYNAMIC));
    return f->content.dynamic.used;
}

#define Is_Flex_Full(f) \
    (Flex_Used(f) + 1 >= Flex_Rest(f))

#define Flex_Available_Space(f) \
    (Flex_Rest(f) - (Flex_Used(f) + 1))  // space minus a terminator

#define Flex_Fits(f,n) \
    ((Flex_Used(f) + (n) + 1) <= Flex_Rest(f))


//=//// FLEX DATA ACCESSORS ///////////////////////////////////////////////=//
//
// 1. Callers like Cell_Strand() or Cell_Array() are expected to test for
//    BASE_FLAG_UNREADABLE and panic before ever calling these routines.
//
// 2. Because these inline functions are called so often, Flex_Data_At()
//    duplicates the code in Flex_Data() rather than call it.  Be sure
//    to change both routines if changing one of them.
//
// 3. Note that Flex indexing in C is zero based.  So as far as Flex is
//    concerned, `Flex_Head(T, s)` is the same as `Flex_At(T, s, 0)`
//

MUTABLE_IF_C(Byte*, INLINE) Flex_Data(CONST_IF_C(Flex*) flex)
{
    CONSTABLE(Flex*) f = m_cast(Flex*, flex);

    dont(assert(Is_Base_Readable(flex)));  // assert is slow, assume valid [1]
    return Get_Stub_Flag(f, DYNAMIC)  // inlined in Flex_Data_At() [2]
        ? u_cast(Byte*, f->content.dynamic.data)
        : u_cast(Byte*, &f->content);
}

MUTABLE_IF_C(Byte*, INLINE) Flex_Data_At(
    Byte w,
    CONST_IF_C(Flex*) flex,
    REBLEN i
){
    CONSTABLE(Flex*) f = m_cast(Flex*, flex);

  #if RUNTIME_CHECKS
    if (w != Flex_Wide(f)) {
        if (BASE_BYTE(f) == FREE_POOLUNIT_BYTE)
            printf("Flex_Data_At() asked on free PoolUnit\n");
        else if (Not_Base_Readable(f))
            printf("Flex_Data_At() asked on diminished Flex\n");
        else
            printf(
                "Flex_Data_At() asked %d on width=%d\n",
                w,
                cast(int, Flex_Wide(f))
            );
        crash (f);
    }
  #endif

    assert(i <= Flex_Used(f));

    return ((w) * (i)) + (  // v-- inlining of Flex_Data() [2]
        Get_Stub_Flag(f, DYNAMIC)
            ? u_cast(Byte*, f->content.dynamic.data)
            : u_cast(Byte*, &f->content)
        );
}

#define Flex_At(T,f,i) \
    u_cast(T*, Flex_Data_At(sizeof(T), (f), (i)))  // zero-based [3]

#if RUNTIME_CHECKS
    #define Flex_Head(T,f) \
        Flex_At(T, (f), 0)  // Flex_Data() doesn't check width, _At() does
#else
    #define Flex_Head(T,f) \
        u_cast(T*, Flex_Data(f))  // slightly faster, but no width check
#endif

#define Flex_Head_Dynamic(T,f) \
    u_cast(T*, (f)->content.dynamic.data)  // faster if you know it's dynamic

#define Flex_Data_Tail(w,f) \
    u_cast(Byte*, Flex_Data_At((w), (f), Flex_Used(f)))

#define Flex_Tail(T,f) \
    u_cast(T*, Flex_Data_Tail(sizeof(T), (f)))

MUTABLE_IF_C(Byte*, INLINE) Flex_Data_Last(
    size_t wide,
    CONST_IF_C(Flex*) flex
){
    CONSTABLE(Flex*) f = m_cast(Flex*, flex);
    assert(Flex_Used(f) != 0);
    return Flex_Data_At(wide, f, Flex_Used(f) - 1);
}

#define Flex_Last(T,f) \
    u_cast(T*, Flex_Data_Last(sizeof(T), (f)))


//=//// FLEX TERMINATION //////////////////////////////////////////////////=//
//
// R3-Alpha had a concept of termination which was that all Flexes had one
// full-sized unit at their tail which was set to zero bytes.  Ren-C moves
// away from this concept...it only has terminating '\0' on UTF-8 Strings,
// a reserved terminating *position* on Blobs (in case they become
// aliased as UTF-8 Strings), and the checked build terminates Arrays in order
// to catch out-of-bounds accesses more easily:
//
// https://forum.rebol.info/t/1445
//
// Under this strategy, most of the termination is handled by the functions
// that deal with their specific subclass (e.g. Make_Strand()).  But some
// generic routines that memcpy() data behind the scenes needs to be sure it
// maintains the invariant that the higher level routines want.
//
// 1. A Binary alias of a String must have all modifications keep it as valid
//    UTF-8, and it must maintain a `\0` terminator.  Because all Binary
//    are candidates for being aliased as String, they reserve a byte at
//    their tail.  This debug setting helps ensure that Blobs are setting
//    the '\0' tail intentionally when appropriate by poisoning the byte.
//
// 2. There's a difference with how byte buffers are handled vs. Array, in
//    that Arrays have to be expanded before they are written to, so that
//    the Cells are formatted.  Byte strings don't have that requirement,
//    so the code isn't stylized to set the used size first and then put
//    data into the buffer.  So it wouldn't do any good to put a poison
//    byte at the head of a Flex allocation and expect to be able to see
//    it before the termination.  Review if callers can/should be changed.
//

#if DEBUG_POISON_FLEX_TAILS
    #define ONE_IF_POISON_TAILS 1

    #define BINARY_BAD_UTF8_TAIL_BYTE 0xFE  // Blobs reserve tail byte [1]

    INLINE void Poison_Or_Unpoison_Tail_Debug(Flex* f, bool poison) {
        if (Stub_Holds_Bytes(f)) {  // presume BLOB! or ANY-STRING? (?)
            Byte* tail = Flex_Tail(Byte, f);
            if (poison)
                *tail = BINARY_BAD_UTF8_TAIL_BYTE;
            else {
                dont(assert(  // doesn't have an invariant [2]
                    *tail == BINARY_BAD_UTF8_TAIL_BYTE or *tail == '\0'
                ));
            }
        }
        else if (Stub_Holds_Cells(f) and Get_Stub_Flag(f, DYNAMIC)) {
            Cell* tail = Flex_At(Cell, f, f->content.dynamic.used);
            if (poison)
                Poison_Cell(tail);
            else {
                assert(Is_Cell_Poisoned(tail));
                Erase_Cell(tail);
            }
        }
    }

    #define Poison_Flex_Tail_If_Debug(f) \
        Poison_Or_Unpoison_Tail_Debug((f), true)

    #define Unpoison_Flex_Tail_If_Debug(s) \
        Poison_Or_Unpoison_Tail_Debug((f), false)
#else
    #define ONE_IF_POISON_TAILS 0

    #define Poison_Flex_Tail_If_Debug(f) NOOP
    #define Unpoison_Flex_Tail_If_Debug(f) NOOP
#endif

INLINE void Term_Flex_If_Necessary(Flex* f)
{
    if (Stub_Holds_Bytes(f)) {
        if (Is_Stub_Strand(f))
            *Flex_Tail(Byte, f) = '\0';
        else {
          #if DEBUG_POISON_FLEX_TAILS
            *Flex_Tail(Byte, f) = BINARY_BAD_UTF8_TAIL_BYTE;
          #endif
        }
    }
    else if (Get_Stub_Flag(f, DYNAMIC) and Stub_Holds_Cells(f)) {
      #if DEBUG_POISON_FLEX_TAILS
        Poison_Cell(Flex_Tail(Cell, f));
      #endif
    }
}

#if NO_RUNTIME_CHECKS
    #define Assert_Flex_Term_If_Needed(f)  NOOP
#else
    #define Assert_Flex_Term_If_Needed(f) \
        Assert_Flex_Term_Core(f)
#endif

#define Note_Flex_Maybe_Term(f) \
    possibly(f)  // no-op, validates expr


//=//// SETTING FLEX LENGTH/SIZE //////////////////////////////////////////=//
//
// 1. Right now FLEX_FLAG_FIXED_SIZE merely means they can't expand, but
//    they set the flag before initializing things like termination and the
//    length.  If this routine were to disallow it, then the flag wouldn't
//    be passed into Flex creation but could only be added afterward.
//
// 2. UTF-8 Strings maintain a length in codepoints (in misc.num_codpoints),
//    plus the size in bytes (as "used").  It's expected that both will be
//    updated together--see Term_Strand_Len_Size().  But sometimes the used
//    field is updated solo by a Binary-based routine in an intermediate step.
//    That's okay so long as the length is not consulted before the String
//    handling code finalizes it.  DEBUG_UTF8_EVERYWHERE makes violations
//    obvious by corrupting the length.

INLINE void Set_Flex_Used_Internal(Flex* f, Count used) {
    /* assert(Not_Flex_Flag(s, FIXED_SIZE)); */  // [1]
    if (Get_Stub_Flag(f, DYNAMIC))
        f->content.dynamic.used = used;  // USED_BYTE() acts as MIRROR_BYTE()
    else {
        assert(used < Size_Of(f->content));

        if (Stub_Holds_Cells(f)) {  // content used by cell, no room for length
            if (used == 0)
                Poison_Cell(&f->content.fixed.cell);  // poison means 0 used
            else {
                assert(used == 1);  // any non-poison will mean length 1
                if (not Is_Cell_Poisoned(&f->content.fixed.cell)) {
                    // it was already length 1, leave the cell alone
                } else
                    Erase_Cell(&f->content.fixed.cell);
            }
        }
        else
            USED_BYTE(f) = used;
    }

  #if DEBUG_UTF8_EVERYWHERE
    if (Is_Stub_Non_Symbol(f)) {
        Corrupt_If_Needful(MISC_STRAND_NUM_CODEPOINTS(f));  // catch violators [2]
        Touch_Stub_If_Debug(f);
    }
  #endif
}

INLINE void Set_Flex_Used(Flex* f, Count used) {
    Unpoison_Flex_Tail_If_Debug(f);
    Set_Flex_Used_Internal(f, used);
    Poison_Flex_Tail_If_Debug(f);
}

INLINE void Set_Flex_Len(Flex* f, Length len) {
    assert(not Is_Stub_Strand(f));  // use _Len_Size() instead [2]
    Set_Flex_Used(f, len);
}

#if CPLUSPLUS_11  // catch cases when calling on Strand* directly
    INLINE void Set_Flex_Len(Strand* s, Length len) = delete;
#endif

// Optimized expand when at tail (but, does not reterminate)
//
INLINE Result(Zero) Expand_Flex_Tail_And_Update_Used(Flex* f, REBLEN delta) {
    if (Flex_Fits(f, delta))
        Set_Flex_Used(f, Flex_Used(f) + delta);  // no termination implied
    else {
        trap (  // comment said "currently terminates"...does it?
          Expand_Flex_At_Index_And_Update_Used(f, Flex_Used(f), delta)
        );
    }
    return zero;
}


// If the data is tiny enough, it will be fit into the Flex Stub itself.
// A Small Flex will be allocated from a memory pool.
// A Large Flex will be allocated from system memory.
//
// 1. It is more efficient if you know a Flex is going to become managed to
//   create it in the managed state.  But be sure no evaluations are called
//   before it's made reachable by the GC, or use Push_Lifeguard().
//
INLINE Result(Flex*) Make_Flex_Into(
    Flags flags,
    Result(void*) preallocated,
    REBLEN capacity
){
    require (
      void* pre = preallocated
    );

    Flavor flavor = Flavor_From_Flags(flags);
    assert(flavor != FLAVOR_0 and flavor <= MAX_FLAVOR);

    size_t wide = Wide_For_Flavor(flavor);
    if (cast(REBU64, capacity) * wide > INT32_MAX) {
        Size requested_size = cast(REBU64, capacity) * wide;
        UNUSED(requested_size);  // can't alloc memory, how to report?
        return fail (Cell_Error(g_error_no_memory));
    }

    assume (
      Flex* s = u_downcast Prep_Stub(flags, pre)
    );

    if (
        (flags & STUB_FLAG_DYNAMIC)  // inlining will constant fold
        or (capacity * wide > sizeof(s->content))  // data won't fit in stub
    ){
        Set_Stub_Flag(s, DYNAMIC);

        Flex_Data_Alloc(s, capacity) except (Error* e) {
            Clear_Base_Managed_Bit(s);
            Set_Stub_Unreadable(s);
            GC_Kill_Stub(s);

            return fail (e);
        }

      #if DEBUG_COLLECT_STATS
        g_mem.flex_memory += capacity * wide;
      #endif
    }

    if (not (flags & BASE_FLAG_MANAGED)) {  // more efficient if managed [1]
        if (Is_Flex_Full(g_gc.manuals)) {
            Extend_Flex_If_Necessary_But_Dont_Change_Used(
                g_gc.manuals, 8
            ) except (Error* e) {
                Free_Unmanaged_Flex(s);
                return fail (e);
            }
        }

        cast(Flex**, g_gc.manuals->content.dynamic.data)[
            g_gc.manuals->content.dynamic.used++
        ] = s;  // will need to find/remove from this list later
    }

    return s;
}

#define Make_Flex(flags, capacity) \
    Make_Flex_Into((flags), Raw_Pooled_Alloc(STUB_POOL), (capacity))

//=//// FLEX MONITORING ///////////////////////////////////////////////////=//
//
// This once used a Flex flag in checked builds to tell whether a Flex was
// monitored or not.  But Flex flags are scarce, so the feature was scaled
// back to just monitoring a single node.  It could also track a list--but the
// point is just that stealing a flag is wasteful.
//
#if DEBUG_MONITOR_FLEX
    INLINE void Debug_Monitor_Flex(void *p) {
        printf("Adding monitor to %p on TICK %" PRIu64 "\n", p, TICK);
        fflush(stdout);
        g_mem.monitoring = cast(Flex*, p);
    }
#endif
