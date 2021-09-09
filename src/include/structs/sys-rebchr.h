//
//  File: %sys-rebchr.h
//  Summary: {"Iterator" data type for characters verified as valid UTF-8}
//  Project: "Ren-C Interpreter and Run-time"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2019 Ren-C Open Source Contributors
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
// Ren-C exchanges UTF-8 data with the outside world via "char*".  But inside
// the code, REBYTE* is used for not-yet-validated bytes that are to be
// scanned as UTF-8.  When accessing an already-checked string, however,
// the REBCHR(*) type is used...signaling no error checking should need to be
// done while walking through the UTF-8 sequence.
//
// So for instance: instead of simply saying:
//
//     REBUNI *ptr = STR_HEAD(string_series);
//     REBUNI c = *ptr++;  // !!! invalid, treating UTF-8 like it's ASCII!
//
// ...one must instead write:
//
//     REBCHR(*) ptr = STR_HEAD(string_series);
//     REBUNI c;
//     ptr = NEXT_CHR(&c, ptr);  // ++ptr or ptr[n] will error in C++ build
//
// The code that runs behind the scenes is typical UTF-8 forward and backward
// scanning code, minus any need for error handling.
//

#if (! DEBUG_UTF8_EVERYWHERE)
    //
    // Plain build uses trivial expansion of REBCHR(*) and REBCHR(const*)
    //
    //          REBCHR(*) cp; => REBYTE * cp;
    //     REBCHR(const*) cp; => REBYTE const* cp;  // same as `const REBYTE*`
    //
    #define REBCHR(star_or_const_star) \
        REBYTE star_or_const_star

    #define const_if_unchecked_utf8 const
#else
    #if (! CPLUSPLUS_11)
        #error "DEBUG_UTF8_EVERYWHERE requires C++11 or higher"
    #endif

    // Debug mode uses templates to expand REBCHR(*) and REBCHR(const*) into
    // pointer classes.  This technique allows the simple C compilation too:
    //
    // http://blog.hostilefork.com/kinda-smart-pointers-in-c/
    //
    // NOTE: If the core is built in this mode, it changes the interface of
    // the core, such that extensions using the internal API that are built
    // without it will be binary-incompatible.
    //
    // NOTE: THE NON-INLINED OVERHEAD IS RATHER HIGH IN UNOPTIMIZED BUILDS!
    // debug build does not inline these classes and functions.  So traversing
    // strings involves a lot of constructing objects and calling methods that
    // call methods.  Hence these classes are used only in non-debug (and
    // hopefully) optimized builds, where the inlining makes it equivalent to
    // the C version.  That allows for the compile-time type checking but no
    // added runtime overhead.
    //
    template<typename T> struct RebchrPtr;
    #define REBCHR(star_or_const_star) \
        RebchrPtr<REBYTE star_or_const_star>

    #define const_if_unchecked_utf8

    // Primary purpose of the classes is to disable the ability to directly
    // increment or decrement pointers to REBYTE* without going through helper
    // routines that do decoding.  But we still want to do pointer comparison,
    // and C++ sadly makes us write this all out.

    template<>
    struct RebchrPtr<const REBYTE*> {
        const REBYTE *bp;  // will actually be mutable if constructed mutable

        RebchrPtr () {}
        RebchrPtr (nullptr_t n) : bp (n) {}
        explicit RebchrPtr (const REBYTE *bp) : bp (bp) {}
        explicit RebchrPtr (const char *cstr)
            : bp (reinterpret_cast<const REBYTE*>(cstr)) {}

        REBSIZ operator-(const REBYTE *rhs)
          { return bp - rhs; }

        REBSIZ operator-(RebchrPtr rhs)
          { return bp - rhs.bp; }

        bool operator==(const RebchrPtr<const REBYTE*> &other)
          { return bp == other.bp; }

        bool operator==(const REBYTE *other)
          { return bp == other; }

        bool operator!=(const RebchrPtr<const REBYTE*> &other)
          { return bp != other.bp; }

        bool operator!=(const REBYTE *other)
          { return bp != other; }

        bool operator>(const RebchrPtr<const REBYTE*> &other)
          { return bp > other.bp; }

        bool operator<(const REBYTE *other)
          { return bp < other; }

        bool operator<=(const RebchrPtr<const REBYTE*> &other)
          { return bp <= other.bp; }

        bool operator>=(const REBYTE *other)
          { return bp >= other; }

        operator bool() { return bp != nullptr; }  // implicit
        operator const void*() { return bp; }  // implicit
        operator const REBYTE*() { return bp; }  // implicit
        operator const char*()
          { return reinterpret_cast<const char*>(bp); }  // implicit
    };

    template<>
    struct RebchrPtr<REBYTE*> : public RebchrPtr<const REBYTE*> {
        RebchrPtr () : RebchrPtr<const REBYTE*>() {}
        RebchrPtr (nullptr_t n) : RebchrPtr<const REBYTE*>(n) {}
        explicit RebchrPtr (REBYTE *bp)
            : RebchrPtr<const REBYTE*> (bp) {}
        explicit RebchrPtr (char *cstr)
            : RebchrPtr<const REBYTE*> (reinterpret_cast<REBYTE*>(cstr)) {}

        static REBCHR(*) nonconst(REBCHR(const*) cp)
          { return RebchrPtr {const_cast<REBYTE*>(cp.bp)}; }

        operator void*() { return const_cast<REBYTE*>(bp); }  // implicit
        operator REBYTE*() { return const_cast<REBYTE*>(bp); }  // implicit
        explicit operator char*()
            { return const_cast<char*>(reinterpret_cast<const char*>(bp)); }
    };

  #if DEBUG_CHECK_CASTS
    //
    // const_cast<> and reinterpret_cast<> don't work with user-defined
    // conversion operators.  But since this codebase uses m_cast, we can
    // cheat when the class is being used with the helpers.
    //
    template <>
    inline REBCHR(*) m_cast_helper(REBCHR(const*) v)
      { return RebchrPtr<REBYTE*> {const_cast<REBYTE*>(v.bp)}; }

    template <>
    inline REBCHR(*) m_cast_helper(REBCHR(*) v)
      { return v; }  // m_cast() is supposed to be able to be a no-op
  #else
    #error "DEBUG_UTF8_EVERYWHERE currently requires DEBUG_CHECK_CASTS"
  #endif
#endif
