//
//  File: %struct-char.h
//  Summary: "Utf8 structure definitions preceding %tmp-internals.h"
//  Project: "Ren-C Interpreter and Run-time"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2023 Ren-C Open Source Contributors
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
// Ren-C exchanges UTF-8 data with the outside world via "char*".  But within
// the core, Byte* is used for not-yet-validated bytes that are to be
// scanned as UTF-8, since it's less error-prone to do math on unsigned bytes.
//
// But there's a different datatype for accessing an already-validated string!
// The Utf8(*) type is used...signaling no error checking should need to be
// done while walking through the UTF-8 sequence.  It also protects against
// naive `char` accesses and single-byte incrementation of UTF-8 data.
//
// So for instance: instead of simply saying:
//
//     Codepoint* ptr = String_Head(string_flex);
//     Codepoint c = *ptr++;  // !!! invalid, treating UTF-8 like it's ASCII!
//
// ...one must instead write:
//
//     Utf8(*) ptr = String_Head(string_flex);
//     Codepoint c;
//     ptr = Utf8_Next(&c, ptr);  // ++ptr or ptr[n] will error in C++ build
//
// The code that runs behind the scenes is typical UTF-8 forward and backward
// scanning code, minus any need for error handling.
//

#if (! DEBUG_UTF8_EVERYWHERE)
    //
    // Plain build uses trivial expansion of Utf8(*) and Utf8(const*)
    //
    //          Utf8(*) cp; => Byte*  cp;
    //     Utf8(const*) cp; => Byte const* cp;  // same as `const Byte*`
    //
    #define Utf8(star_or_const_star) \
        Byte star_or_const_star
#else
  #if (! CPLUSPLUS_11)
    #error "DEBUG_UTF8_EVERYWHERE requires C++11 or higher"
    #include <stophere>  // https://stackoverflow.com/a/45661130
  #endif

    // Debug mode uses templates to expand Utf8(*) and Utf8(const*) into
    // pointer classes.  This technique allows the simple C compilation too:
    //
    // http://blog.hostilefork.com/kinda-smart-pointers-in-c/
    //
    // NOTE: If the core is built in this mode, it changes the interface of
    // the core, such that extensions using the internal API that are built
    // without it will be binary-incompatible.
    //
    // NOTE: THE NON-INLINED OVERHEAD IS RATHER HIGH IN UNOPTIMIZED BUILDS!
    // Checked builds don't inline these classes and functions.  So traversing
    // strings involves a lot of constructing objects and calling methods that
    // call methods.  Hence these classes are used only in non-debug (and
    // hopefully) optimized builds, where the inlining makes it equivalent to
    // the C version.  That allows for the compile-time type checking but no
    // added runtime overhead.
    //
    template<typename T> struct ValidatedUtf8;
    #define Utf8(star_or_const_star) \
        ValidatedUtf8<Byte star_or_const_star>

    // 1. These constructors are explicit because we want conversions from
    //    char* and Byte* to require an explicit cast, to convey that "yes,
    //    I'm sure these bytes are valid UTF-8".
    //
    // 2. Conversion operators from Utf8(*) are implicit, because there's no
    //    harm in passing already validated UTF-8 to functions that are
    //    expecting char* or Byte*.
    //
    // 3. Wrapping the pointer in a class means incrementing or decrementing
    //    that pointer is disabled by default.  That's a good thing--since
    //    we want clients to go through helper routines that do decoding.
    //    But we still want to do pointer comparison without needing to cast,
    //    and C++ sadly makes us write this all out.
    //
    // 4. These helpers assist in `const-preserving casts` where the input
    //    type can be either Utf8(*) or Utf8(const*), and it picks the
    //    result char or Byte constness that is appropriate.  This allows
    //    for more brevity and means macros can be made that "do the right
    //    thing" with const without having to write overloaded functions.
    //
    // 5. Wrapper classes don't know how to do `const_cast<>`, so things
    //    like std::shared_ptr<> have std::const_cast_pointer.  Ren-C's
    //    m_cast() is smart enough to delegate to m_cast_helper so that
    //    m_cast(Utf8(*)) of a Utf8(const*) can be made to work.

    template<>
    struct ValidatedUtf8<const Byte*> {
        const Byte* p;  // underlying pointer mutable if constructed mutable

        ValidatedUtf8 () = default;
        constexpr ValidatedUtf8 (nullptr_t n) : p (n) {}

        explicit constexpr ValidatedUtf8 (const Byte* p)  // [1]
            : p (p) {}
        explicit constexpr ValidatedUtf8 (const char *cstr)  // [1]
            : p (c_cast(Byte*, cstr)) {}

        constexpr operator const void*() { return p; }  // [2]
        constexpr operator const Byte*() { return p; }  // [2]
        constexpr operator const char*() { return c_cast(char*, p); }  // [2]

        explicit operator bool() { return p != nullptr; }  // if() uses

        Size operator-(const Byte* rhs)
          { return p - rhs; }

        Size operator-(Utf8(const*) rhs)
          { return p - rhs.p; }

        bool operator==(Utf8(const*) &other)  // [3]
          { return p == other.p; }

        bool operator==(const Byte* other)  // [3]
          { return p == other; }

        bool operator!=(Utf8(const*) &other)  // [3]
          { return p != other.p; }

        bool operator!=(const Byte* other)  // [3]
          { return p != other; }

        bool operator>(const Utf8(const*) &other)  // [3]
          { return p > other.p; }

        bool operator<(const Byte* other)  // [3]
          { return p < other; }

        bool operator<=(Utf8(const*) &other)  // [3]
          { return p <= other.p; }

        bool operator>=(const Byte* other)  // [3]
          { return p >= other; }
    };

    template<>
    struct ValidatedUtf8<Byte*> : public ValidatedUtf8<const Byte*> {
        ValidatedUtf8 () = default;
        constexpr ValidatedUtf8 (nullptr_t n)
            : ValidatedUtf8<const Byte*>(n) {}

        explicit ValidatedUtf8 (Byte* bp)  // [1]
            : ValidatedUtf8<const Byte*> (bp) {}
        explicit ValidatedUtf8 (char *cstr)  // [1]
            : ValidatedUtf8<const Byte*> (cast(Byte*, cstr)) {}

        constexpr operator void*() { return m_cast(Byte*, p); }  // [2]
        constexpr operator Byte*() { return m_cast(Byte*, p); }  // [2]
        constexpr operator char*()  // [2]
          { return m_cast(char*, c_cast(char*, p)); }
    };

    template<>
    struct c_cast_helper<char*, Utf8(const*)>  // [4]
      { typedef const char* type; };

    template<>
    struct c_cast_helper<char*, Utf8(*)>  // [4]
      { typedef char* type; };

    template<>
    struct c_cast_helper<Byte*, Utf8(const*)>  // [4]
      { typedef const Byte* type; };

    template<>
    struct c_cast_helper<Byte*, Utf8(*)>  // [4]
      { typedef Byte* type; };

    template<>
    struct c_cast_helper<Utf8(*), const Byte*>  // [4]
      { typedef Utf8(const*) type; };

    template<>
    struct c_cast_helper<Utf8(*), Byte*>  // [4]
      { typedef Utf8(*) type; };

    template<>
    inline Utf8(*) m_cast_helper(Utf8(const*) utf8)  // [5]
      { return cast(Utf8(*), m_cast(Byte*, utf8.p)); }

    template<>
    constexpr inline Utf8(*) m_cast_helper(Utf8(*) utf8)  // [5]
      { return utf8; }
#endif
