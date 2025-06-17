//
//  file: %struct-char.h
//  summary: "Utf8 structure definitions preceding %tmp-internals.h"
//  project: "Ren-C Interpreter and Run-time"
//  homepage: https://github.com/metaeducation/ren-c/
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

    #define Utf8(star_or_const_star) \
        Byte star_or_const_star
#else
  // Debug mode uses templates to expand Utf8(*) and Utf8(const*) into pointer
  // classes.  This technique allows the simple C compilation too:
  //
  //   http://blog.hostilefork.com/kinda-smart-pointers-in-c/
  //
  // NOTE: If the core is built in this mode, it changes the interface of the
  // core, such that extensions using the internal API that are built without
  // it will be binary-incompatible.
  //
  // NOTE: THE NON-INLINED OVERHEAD IS RATHER HIGH IN UNOPTIMIZED BUILDS!
  // Checked builds don't inline these classes and functions.  So traversing
  // strings involves a lot of constructing objects and calling methods that
  // call methods.  Hence these classes are used only in non-debug (and
  // hopefully) optimized builds, where the inlining makes it equivalent to
  // the C version.  That allows for the compile-time type checking but no
  // added runtime overhead.

    template<typename T> struct ValidatedUtf8;
    #define Utf8(star_or_const_star) \
        ValidatedUtf8<Byte star_or_const_star>

  // 1. These constructors are explicit because we want conversions from char*
  //    and Byte* to require an explicit cast, to convey that "yes, I'm sure
  //    these bytes are valid UTF-8".
  //
  // 2. GCC is more willing to perform conversions to void* if you have
  //    something that converts to char* or Byte*.  This leads to ambiguities,
  //    so in order to compile with GCC we can't convert void* implicitly.
  //
  // 3. We want to implicitly convert from Utf8(*) to Byte* or char* where
  //    possible, because there's no harm in passing already validated UTF-8
  //    to functions that are expecting char* or Byte*.  GCC's willingness
  //    to do conversions it shouldn't leads to the ambiguity problems in [2],
  //    so we have to narrow it down to prefer either Byte* or char* as the
  //    implicit conversion target, but not both.  (We choose Byte*.)
  //
  // 4. In C++11 mode, GCC gives "reinterpret cast is not a constexpr" when
  //    you use u_cast()...which expands to a C-style parentheses cast, but
  //    that's the error it gives.
  //
  // 5. Wrapping the pointer in a class means incrementing or decrementing
  //    that pointer is disabled by default.  That's a good thing--since we
  //    want clients to go through helper routines that do decoding.  But we
  //    still want to do pointer comparison without needing to cast, and C++
  //    sadly makes us write this all out.

    template<>
    struct ValidatedUtf8<const Byte*> {
        const Byte* p;  // underlying pointer mutable if constructed mutable

        ValidatedUtf8 () = default;
        ValidatedUtf8 (nullptr_t n) : p (n) {}

        explicit ValidatedUtf8 (const Byte* p)  // [1]
            : p (p) {}
        explicit ValidatedUtf8 (const char *cstr)  // [1]
            : p (u_c_cast(Byte*, cstr)) {}

        operator const void*() = delete;  // don't add, GCC ambiguity [2]

        constexpr operator const Byte*() { return p; }  // [3]

        /*constexpr [4]*/ operator const char*()  // [3]
          { return u_c_cast(char*, p); }

        explicit operator bool() { return p != nullptr; }  // if() uses

        Size operator-(const Byte* rhs)
          { return p - rhs; }

        Size operator-(Utf8(const*) rhs)
          { return p - rhs.p; }

        bool operator==(Utf8(const*) &other)  // [5]
          { return p == other.p; }

        bool operator==(const Byte* other)  // [5]
          { return p == other; }

        bool operator!=(Utf8(const*) &other)  // [5]
          { return p != other.p; }

        bool operator!=(const Byte* other)  // [5]
          { return p != other; }

        bool operator>(const Utf8(const*) &other)  // [5]
          { return p > other.p; }

        bool operator<(const Byte* other)  // [5]
          { return p < other; }

        bool operator<=(Utf8(const*) &other)  // [5]
          { return p <= other.p; }

        bool operator>=(const Byte* other)  // [5]
          { return p >= other; }
    };

    template<>
    struct ValidatedUtf8<Byte*> : public ValidatedUtf8<const Byte*> {
        ValidatedUtf8 () = default;
        ValidatedUtf8 (nullptr_t n)
            : ValidatedUtf8<const Byte*>(n) {}

        explicit ValidatedUtf8 (Byte* bp)  // [1]
            : ValidatedUtf8<const Byte*> (bp) {}
        explicit ValidatedUtf8 (char *cstr)  // [1]
            : ValidatedUtf8<const Byte*> (cast(Byte*, cstr)) {}

        operator const void*() = delete;  // don't add, GCC ambiguity [2]

        explicit constexpr operator const Byte*()  // [3]
          { return const_cast<Byte*>(p); }
        constexpr operator Byte*()  // [3]
          { return const_cast<Byte*>(p); }

        explicit /*constexpr [4]*/ operator const char*()  // [3]
          { return u_cast(char*, p); }
        /*constexpr [4]*/ operator char*()  // [3]
          { return u_cast(char*, const_cast<Byte*>(p)); }
    };

  //=//// MUTABILITY CAST HELPERS /////////////////////////////////////////=//

  // Wrapper classes don't know how to do `const_cast<>`.  In the C standard
  // library  things like std::shared_ptr<> use std::const_cast_pointer.  But
  // Ren-C's m_cast() is smart enough to delegate to MutableCastHelper so that
  // m_cast(Utf8(*)) of a Utf8(const*) can be made to work.

    template<>
    inline Utf8(*) MutableCastHelper(Utf8(const*) utf8)  // [5]
      { return u_cast(Utf8(*), const_cast<Byte*>(utf8.p)); }

    template<>
    constexpr inline Utf8(*) MutableCastHelper(Utf8(*) utf8)  // [5]
      { return utf8; }

  //=//// CONST-PRESERVING CAST HELPERS ///////////////////////////////////=//

  // These helpers assist in c_cast() and u_c_cast(), and picks the right
  // output type to not lose constness in the conversion.  This allows for
  // more brevity and means macros can be made that "do the right thing"
  // with const without having to write overloaded functions.

    template<typename T>
    struct ConstPreservingCastHelper<Utf8(*), T*>
      { using type = Utf8(*); };

    template<typename T>
    struct ConstPreservingCastHelper<Utf8(*), const T*>
      { using type = Utf8(const*); };

    template<typename T>
    struct ConstPreservingCastHelper<T*, Utf8(const*)>
      { using type = const T*; };

    template<typename T>
    struct ConstPreservingCastHelper<T*, Utf8(*)>
      { using type = T*; };

#endif
