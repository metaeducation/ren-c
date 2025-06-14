//
//  file: %sys-flags.h
//  summary: "Byte-Order Sensitive Bit Flags And Masking"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2024 Ren-C Open Source Contributors
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
// To facilitate the tricks of the Rebol Node, these macros are purposefully
// arranging bit flags with respect to the "leftmost" and "rightmost" bytes of
// the underlying platform, when encoding them into an unsigned integer the
// size of a platform pointer:
//
//     uintptr_t flags = FLAG_LEFT_BIT(0);
//     Byte byte = *cast(Byte*, &flags);
//
// In the code above, the leftmost bit of the flags has been set to 1, giving
// `byte == 128` on all supported platforms.
//
// These can form *compile-time constants*, which can be singly assigned to
// a uintptr_t in one instruction.  Quantities smaller than a byte can be
// mixed in on with bytes:
//
//    uintptr_t flags
//        = FLAG_LEFT_BIT(0) | FLAG_LEFT_BIT(1) | FLAG_SECOND_BYTE(13);
//
// They can be masked or shifted out efficiently:
//
//    Byte second = SECOND_BYTE(flags);  // == 13
//
// Other tools that might be tried with this all have downsides:
//
// * bitfields arranged in a `union` with integers have no layout guarantee
// * `#pragma pack` is not standard C98 or C99...nor is any #pragma
// * `char[4]` or `char[8]` targets don't usually assign in one instruction
//

#define PLATFORM_BITS \
    (sizeof(uintptr_t) * 8)

#if defined(ENDIAN_BIG)  // Byte w/most significant bit first

    // 63,62,61...or...31,30,20
    #define FLAG_LEFT_BIT(n) \
        (u_cast(uintptr_t, 1) << (PLATFORM_BITS - (n) - 1))

    #define FLAG_FIRST_BYTE(b) \
        (u_cast(uintptr_t, (b)) << (24 + (PLATFORM_BITS - 8)))

    #define FLAG_SECOND_BYTE(b) \
        (u_cast(uintptr_t, (b)) << (16 + (PLATFORM_BITS - 8)))

    #define FLAG_THIRD_BYTE(b) \
        (u_cast(uintptr_t, (b)) << (8 + (PLATFORM_BITS - 32)))

    #define FLAG_FOURTH_BYTE(b) \
        (u_cast(uintptr_t, (b)) << (0 + (PLATFORM_BITS - 32)))

#elif defined(ENDIAN_LITTLE)  // Byte w/least significant bit first (e.g. x86)

    // 7,6,..0|15,14..8|..
    #define FLAG_LEFT_BIT(n) \
        (u_cast(uintptr_t, 1) << (7 + ((n) / 8) * 8 - (n) % 8))

    #define FLAG_FIRST_BYTE(b)      u_cast(uintptr_t, (b))
    #define FLAG_SECOND_BYTE(b)     (u_cast(uintptr_t, (b)) << 8)
    #define FLAG_THIRD_BYTE(b)      (u_cast(uintptr_t, (b)) << 16)
    #define FLAG_FOURTH_BYTE(b)     (u_cast(uintptr_t, (b)) << 24)
#else
    // !!! There are macro hacks which can actually make reasonable guesses
    // at endianness, and should possibly be used in the config if nothing is
    // specified explicitly.
    //
    // http://stackoverflow.com/a/2100549/211160
    //
    #error "ENDIAN_BIG or ENDIAN_LITTLE must be defined"
    #include <stophere>  // https://stackoverflow.com/a/45661130
#endif


// Access memory at a pointer as a Byte.  Can act as a read, or a write when
// used as the left-hand side of an assignment.  Strict-aliasing safe.
//
//  https://en.cppreference.com/w/c/language/object.html
//
// 1. The macros are in all-caps to show they are "weird" and usable as
//    LValues.
//
// 2. u_c_cast() is used for "unchecked const-preserving casts" in the C++
//    build, so if (p) is const Byte* the Byte won't be mutable.  (The C
//    build throws away constness in u_c_cast(), since it can't "sense" it.)
//
// 3. Byte alias for `unsigned char` is used vs. `uint8_t`, due to strict
//    aliasing exemption for char types (some say uint8_t should count...).
//    This means supposedly, it doesn't matter what type the memory you are
//    reading from...you will get the correct up-to-date value of that byte.

#define FIRST_BYTE(p)       u_c_cast(Byte*, (p))[0]  // CAPS_NAME: LValue [1]
#define SECOND_BYTE(p)      u_c_cast(Byte*, (p))[1]  // const-preserving [2]
#define THIRD_BYTE(p)       u_c_cast(Byte*, (p))[2]  // Byte strict exempt [3]
#define FOURTH_BYTE(p)      u_c_cast(Byte*, (p))[3]


// There might not seem to be a good reason to keep the uint16_t variant in
// any particular order.  But if you cast a uintptr_t (or otherwise) to byte
// and then try to read it back as a uint16_t, compilers see through the
// cast and complain about strict aliasing.  Building it out of bytes makes
// these generic (so they work with uint_fast32_t, or uintptr_t, etc.) and
// as long as there has to be an order, might as well be platform-independent.

INLINE uint16_t FIRST_UINT16(const void* p) {
    const Byte* bp = c_cast(Byte*, p);
    return cast(uint16_t, bp[0] << 8) | bp[1];
}

INLINE uint16_t SECOND_UINT16(const void* p) {
    const Byte* bp = c_cast(Byte*, p);
    return cast(uint16_t, bp[2] << 8) | bp[3];
}

INLINE void SET_FIRST_UINT16(void *p, uint16_t u) {
    Byte* bp = cast(Byte*, p);
    bp[0] = u / 256;
    bp[1] = u % 256;
}

INLINE void SET_SECOND_UINT16(void* p, uint16_t u) {
    Byte* bp = cast(Byte*, p);
    bp[2] = u / 256;
    bp[3] = u % 256;
}

INLINE uintptr_t FLAG_FIRST_UINT16(uint16_t u)
  { return FLAG_FIRST_BYTE(u / 256) | FLAG_SECOND_BYTE(u % 256); }

INLINE uintptr_t FLAG_SECOND_UINT16(uint16_t u)
  { return FLAG_THIRD_BYTE(u / 256) | FLAG_FOURTH_BYTE(u % 256); }


// !!! SECOND_UINT32 should be defined on 64-bit platforms, for any enhanced
// features that might be taken advantage of when that storage is available.
