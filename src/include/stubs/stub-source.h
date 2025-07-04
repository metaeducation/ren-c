//
//  file: %stub-source.h
//  summary: "Definitions for the Source Array subclass"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2024 Ren-C Open Source Contributors
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
// Source is an array subclass that is suitable for backing a BLOCK!, GROUP!,
// FENCE!, etc.  It enforces that it doesn't hold antiforms, and it also
// has special interpretation of the LINK and MISC nodes to hold file and
// line information.
//


// These flags are only for checking "plain" array flags...so not varlists
// or paramlists or anything that isn't just an ordinary source-level array
// (like you'd find in a BLOCK!)
//
// 1. See mutability notes on Set_Flex_Flag() / Clear_Flex_Flag()

#define Get_Source_Flag(a,flag) \
    Get_Flavor_Flag(SOURCE, ensure(const Source*, (a)), flag)

#define Not_Source_Flag(a,flag) \
    Not_Flavor_Flag(SOURCE, ensure(const Source*, (a)), flag)

#define Set_Source_Flag(a,flag) \
    Set_Flavor_Flag(SOURCE, ensure(const Source*, (a)), flag)

#define Clear_Source_Flag(a,flag) \
    Clear_Flavor_Flag(SOURCE, ensure(const Source*, (a)), flag)


#define STUB_MASK_UNMANAGED_SOURCE \
    FLAG_FLAVOR(FLAVOR_SOURCE)

#define STUB_MASK_MANAGED_SOURCE \
    (FLAG_FLAVOR(FLAVOR_SOURCE) | BASE_FLAG_MANAGED)

#define Make_Source(capacity) \
    cast(Source*, Make_Array_Core(STUB_MASK_UNMANAGED_SOURCE, (capacity)))

#define Make_Source_Managed(capacity) \
    cast(Source*, Make_Array_Core(STUB_MASK_MANAGED_SOURCE, (capacity)))


//=//// MIRROR BYTE ///////////////////////////////////////////////////////=//
//
// There's a very narrow optimization made, where arrays have SECOND_BYTE()
// in the info available regardless of whether they are dynamic or not.  This
// is due to the fact that when they're using the small series optimization,
// they don't need the USED_BYTE() because they can use a poisoned cell to
// tell the difference between the only two possible used lengths: 1 and 0.
//
// This is taken advantage of by when sequences hold only a list (and a space),
// to put the list type into the array, so the array itself can be the payload
// of the sequence.  The heart of the cell is the sequence heart (TYPE_CHAIN,
// TYPE_PATH, TYPE_TUPLE...) but then the implied heart of the contained list
// comes out of the array.  This works most of the time (unless the array is
// aliased via AS as another type that's also put in a sequence, which forces
// an allocation of a stub to hold the aliased array).
//

#define MIRROR_BYTE_RAW(source) \
    SECOND_BYTE(&FLEX_INFO(source))

#if (! DEBUG_HOOK_MIRROR_BYTE)
    #define MIRROR_BYTE(source) \
        MIRROR_BYTE_RAW(ensure(const Source*, (source)))
#else
    struct MirrorHolder {
        Source* & ref;

        MirrorHolder(const Source* const& ref)
            : ref (const_cast<Source* &>(ref))
          {}

        operator Byte() const {  // implicit cast, add any read checks here
            return MIRROR_BYTE_RAW(ref);
        }

        void operator=(Byte right) {  // add any write checks you want here
            MIRROR_BYTE_RAW(ref) = right;
        }

        void operator=(const MirrorHolder& right)  // must write explicitly
          { *this = u_cast(Byte, right); }

        template <typename T, EnableIfSame<T,
            HeartEnum, Heart
        > = nullptr>
        void operator=(T right)
          { *this = u_cast(Byte, right); }  // inherit operator= Byte checks

        template <typename T, EnableIfSame<T,
            HeartEnum, Heart
        > = nullptr>
        explicit operator T() const  // inherit Byte() cast extraction checks
          { return u_cast(T, u_cast(Byte, *this)); }
    };

    INLINE bool operator==(const MirrorHolder& holder, HeartEnum h)
      { return MIRROR_BYTE_RAW(holder.ref) == cast(Byte, h); }

    INLINE bool operator==(HeartEnum h, const MirrorHolder& holder)
      { return cast(Byte, h) == MIRROR_BYTE_RAW(holder.ref); }

    INLINE bool operator!=(const MirrorHolder& holder, HeartEnum h)
      { return MIRROR_BYTE_RAW(holder.ref) != cast(Byte, h); }

    INLINE bool operator!=(HeartEnum h, const MirrorHolder& holder)
      { return cast(Byte, h) != MIRROR_BYTE_RAW(holder.ref); }

    #define MIRROR_BYTE(source) \
        MirrorHolder{source}
#endif

#define Mirror_Of(source) \
    u_cast(Option(Heart), MIRROR_BYTE_RAW(source))


// !!! Currently, many bits of code that make copies don't specify if they are
// copying an array to turn it into a paramlist or varlist, or to use as the
// kind of array the use might see.  If we used plain Make_Source() then it
// would add a flag saying there were line numbers available, which may
// compete with the usage of the ->misc and ->link fields of the Stub Base
// for internal arrays.
//
INLINE Array* Make_Array_For_Copy(
    Flags flags,
    const Array* original,
    REBLEN capacity
){
    if (
        original
        and Stub_Flavor(original) == FLAVOR_SOURCE
        and Get_Source_Flag(c_cast(Source*, original), NEWLINE_AT_TAIL)
    ){
        //
        // All of the newline bits for cells get copied, so it only makes
        // sense that the bit for newline on the tail would be copied too.
        //
        flags |= SOURCE_FLAG_NEWLINE_AT_TAIL;
    }

    Option(const Strand*) filename;
    if (
        Flavor_From_Flags(flags) == FLAVOR_SOURCE
        and original
        and Stub_Flavor(original) == FLAVOR_SOURCE
        and (filename = Link_Filename(c_cast(Source*, original)))
    ){
        Source* a = cast(Source*, Make_Array_Core(flags, capacity));
        Tweak_Link_Filename(a, filename);
        MISC_SOURCE_LINE(a) = MISC_SOURCE_LINE(original);
        return a;
    }

    return Make_Array_Core(flags, capacity);
}
