//
//  file: %sys-node.h
//  summary:{Convenience routines for the Base "superclass" structure}
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
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
// This provides some convenience routines that require more definitions than
// are available when %sys-rebnod.h is being processed.  (e.g. Cell,
// Stub, Level...)
//
// See %sys-rebnod.h for what a "Base" means in this context.
//


#define FLAG_BASE_BYTE(byte)    FLAG_FIRST_BYTE(byte)

#define Is_Base(p) \
    (u_cast(Byte*, (p))[0] & BASE_BYTEMASK_0x80_BASE)

#define Is_Base_A_Cell(n)   (did (BASE_BYTE(n) & BASE_BYTEMASK_0x08_CELL))
#define Is_Base_A_Stub(n)   (not Is_Base_A_Cell(n))

// !!! There's currently no generic way to tell if a Base is a Level.  It has
// the cell flag set in its header, and uses all the other flags.  It's a lie
// to say it's a stub or a cell in any case--even if the layout were changed
// so the leading area was an actual stub or a cell with a special flavor or
// heart byte.  It hasn't been a problem because places Level can be seen
// can't generally hold cells, so the single flag is enough.  Calling out
// this test helps find places that rely on that behavior.
//
#define Is_Non_Cell_Base_A_Level Is_Base_A_Cell

#define Is_Base_Marked(n)   (did (BASE_BYTE(n) & BASE_BYTEMASK_0x01_MARKED))
#define Not_Base_Marked(n)  (not Is_Base_Marked(n))

#define Is_Base_Managed(n)  (did (BASE_BYTE(n) & BASE_BYTEMASK_0x04_MANAGED))
#define Not_Base_Managed(n) (not Is_Base_Managed(n))

#define Is_Base_Readable(n) \
    (not (BASE_BYTE(n) & BASE_BYTEMASK_0x40_UNREADABLE))

#define Not_Base_Readable(n) (not Is_Base_Readable(n))

// Is_Base_Root() sounds like it might be the only Base.
// Is_Base_A_Root() sounds like a third category vs Is_Base_A_Cell()/Stub()
//
#define Is_Base_Root_Bit_Set(n) \
    (did (BASE_BYTE(n) & BASE_BYTEMASK_0x02_ROOT))

#define Not_Base_Root_Bit_Set(n) \
    (not (BASE_BYTE(n) & BASE_BYTEMASK_0x02_ROOT))

// Add "_Bit" suffix to reinforce lack of higher level function.  (A macro
// with the name Set_Base_Managed() might sound like it does more, like
// removing from the manuals list the way Manage_Flex() etc. do)

#define Set_Base_Root_Bit(n) \
    BASE_BYTE(n) |= BASE_BYTEMASK_0x02_ROOT

#define Clear_Base_Root_Bit(n) \
    BASE_BYTE(n) &= (~ BASE_BYTEMASK_0x02_ROOT)

#define Set_Base_Marked_Bit(n) \
    BASE_BYTE(n) |= BASE_BYTEMASK_0x01_MARKED

#define Clear_Base_Marked_Bit(n) \
    BASE_BYTE(n) &= (~ BASE_BYTEMASK_0x01_MARKED)

#define Set_Base_Managed_Bit(n) \
    BASE_BYTE(n) |= BASE_BYTEMASK_0x04_MANAGED

#define Clear_Base_Managed_Bit(n) \
    BASE_BYTE(n) &= (~ BASE_BYTEMASK_0x04_MANAGED)

#define Set_Base_Unreadable_Bit(n) \
    BASE_BYTE(n) |= BASE_BYTEMASK_0x40_UNREADABLE

#define Clear_Base_Unreadable_Bit(n) \
    BASE_BYTE(n) &= (~ BASE_BYTEMASK_0x40_UNREADABLE)



#if NO_DEBUG_CHECK_CASTS

    #define BAS(p) \
        cast(Base*, (p)) // BAS() just does a cast (maybe with added checks)

#else

    template <typename T>
    INLINE Base* BAS(T *p) {
        constexpr bool derived =
            std::is_same<T, Value>::value
            or std::is_same<T, Stub>::value
            or std::is_same<T, Symbol>::value
            or std::is_same<T, Array>::value
            or std::is_same<T, VarList>::value
            or std::is_same<T, Error>::value
            or std::is_same<T, REBACT>::value
            or std::is_same<T, REBMAP>::value
            or std::is_same<T, Level>::value;

        constexpr bool base = std::is_same<T, void>::value;

        static_assert(
            derived or base,
            "BAS() works on void/Value/Stub/Symbol/Array/VarList/REBACT" \
               "/REBMAP/Level"
        );

        if (not p) { // !!! include a static check for nullptr/0?
            assert(!"Use cast(Base*, x) and not BAS(x) on null pointers");
        }
        else if (base)
            assert(
                (BASE_BYTE(p) & (
                    BASE_BYTEMASK_0x80_BASE | BASE_BYTEMASK_0x40_UNREADABLE
                )) == (
                    BASE_BYTEMASK_0x80_BASE
                )
            );

        return reinterpret_cast<Base*>(p);
    }
#endif


// Allocate a unit from a pool.  Client is responsible for initializing it,
// and can assume it has random bytes.
//
// All Base are 64-bit aligned.  This way, data allocated in bases can be
// structured to know where legal 64-bit alignment points would be.  This
// is required for correct functioning of some types.  (See notes on
// alignment in %sys-rebval.h.)
//
INLINE void *Alloc_Pooled(REBLEN pool_id)
{
    REBPOL *pool = &Mem_Pools[pool_id];
    if (not pool->first) // pool has run out of units
        Fill_Pool(pool); // refill it

    assert(pool->first);

    PoolUnit* unit = pool->first;

    pool->first = unit->next_if_free;
    if (unit == pool->last)
        pool->last = nullptr;

    pool->free--;

  #if DEBUG_MEMORY_ALIGNMENT
    if (i_cast(uintptr_t, unit) % sizeof(REBI64) != 0) {
        printf(
            "Base address %p not aligned to %d bytes\n",
            cast(void*, unit),
            cast(int, sizeof(REBI64))
        );
        printf("Pool address is %p and pool-first is %p\n",
            cast(void*, pool),
            cast(void*, pool->first)
        );
        crash (unit);
    }
  #endif

    assert(Not_Base_Readable(unit));  // client needs to change to non-free
    return cast(void*, unit);
}


// Free a unit, returning it to its pool.  Once it is freed, its header will
// have FREE_POOLUNIT_BYTE...which identifies the unit as not in use to anyone
// who enumerates the units in the pool (such as the garbage collector).
//
INLINE void Free_Pooled(REBLEN pool_id, void *p)
{
  #if DEBUG_MONITOR_STUB
    if (
        pool_id == STUB_POOL
        and not (cast(union HeaderUnion*, p)->bits & BASE_FLAG_CELL)
        and Get_Flex_Info(cast(Flex*, p), MONITOR_DEBUG)
    ){
        printf("Freeing series %p on tick #%d\n", p, cast(int, TICK));
        fflush(stdout);
    }
  #endif

    PoolUnit* unit = cast(PoolUnit*, p);

    FIRST_BYTE(unit) = FREE_POOLUNIT_BYTE;

    REBPOL *pool = &Mem_Pools[pool_id];

  #if NO_RUNTIME_CHECKS
    unit->next_if_free = pool->first;
    pool->first = unit;
  #else
    // !!! In R3-Alpha, the most recently freed unit would become the first
    // unit to hand out.  This is a simple and likely good strategy for
    // cache usage, but makes the "poisoning" nearly useless.
    //
    // This code was added to insert an empty segment, such that this unit
    // won't be picked by the next allocation.  That enlongates the poisonous
    // time of this area to catch stale pointers.  But doing this in the
    // debug build only creates a source of variant behavior.

    if (not pool->last) // Fill pool if empty
        Fill_Pool(pool);

    assert(pool->last);

    pool->last->next_if_free = unit;
    pool->last = unit;
    unit->next_if_free = nullptr;
  #endif

    pool->free++;
}


//=////////////////////////////////////////////////////////////////////////=//
//
// POINTER DETECTION (UTF-8, SERIES, FREED SERIES, END...)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Rebol's "Bases" all have a platform-pointer-sized header of bits, which
// is constructed using byte-order-sensitive bit flags (see FLAG_LEFT_BIT and
// related definitions).
//
// The values for the bits were chosen carefully, so that the leading byte of
// Rebol structures could be distinguished from the leading byte of a UTF-8
// string.  This is taken advantage of in the API.
//
// During startup, Assert_Pointer_Detection_Working() checks invariants that
// make this routine able to work.
//

enum PointerDetectEnum {
    DETECTED_AS_UTF8 = 1,
    DETECTED_AS_CELL,
    DETECTED_AS_STUB,
    DETECTED_AS_END,  // a rebEND signal (Note: has char* alignment!)
    DETECTED_AS_FREE
};

typedef enum PointerDetectEnum PointerDetect;

INLINE PointerDetect Detect_Rebol_Pointer(const void *p)
{
    Byte b = FIRST_BYTE(p);

    if (not (b & BASE_BYTEMASK_0x80_BASE))  // test for 1xxxxxxx
        return DETECTED_AS_UTF8;  // < 0x80 is string w/1st char in ASCII range

    if (not (b & BASE_BYTEMASK_0x40_UNREADABLE)) {  // test for 10xxxxxx
        if (SECOND_BYTE(p) == TYPE_0)  // !!! Wasteful legacy bootstrap idea
            return DETECTED_AS_END;  // !!! Modern EXE has no TYPE_END

        if (b & BASE_BYTEMASK_0x08_CELL)  // 10xxxxxx never starts UTF-8
            return DETECTED_AS_CELL;
        return DETECTED_AS_STUB;
    }

    if (  // we know it's 11xxxxxx... now test for 1111xxxx
        (b & (BASE_BYTEMASK_0x20_GC_ONE | BASE_BYTEMASK_0x10_GC_TWO))
            == (BASE_BYTEMASK_0x20_GC_ONE | BASE_BYTEMASK_0x10_GC_TWO)
    ){
        if (b & BASE_BYTEMASK_0x08_CELL)  // ...now test for 11111xxx
            return DETECTED_AS_CELL;  // 11111xxx never starts UTF-8!

        // There are 3 patterns of 0b11110xxx that are illegal in UTF-8:
        //
        //     0xF5 (11110101), 0xF6 (11110110), 0xF7 (11110111)
        //
        // Hence if the sixth bit is clear (0b111100xx) detect it as UTF-8.
        //
        if (not (b & BASE_BYTEMASK_0x04_MANAGED))
            return DETECTED_AS_UTF8;

        if (b == END_SIGNAL_BYTE) {  // 0xF7
            assert(SECOND_BYTE(p) == '\0');
            return DETECTED_AS_END;
        }

        if (b == FREE_POOLUNIT_BYTE)  // 0xF6
            return DETECTED_AS_FREE;

        if (b == BASE_BYTE_RESERVED)  // 0xF5
            panic ("BASE_BYTE_RESERVED Encountered in Detect_Rebol_Pointer()");

        return DETECTED_AS_STUB;
    }

    return DETECTED_AS_UTF8;
}
