//
//  File: %sys-rebchr.h
//  Summary: {"Iterator" data type for characters verified as valid UTF-8}
//  Project: "Ren-C Interpreter and Run-time"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2022 Ren-C Open Source Contributors
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
// the code, Byte* is used for not-yet-validated bytes that are to be
// scanned as UTF-8, since it's less error-prone to do math on unsigned bytes.
//
// But there's a different datatype for accessing an already-validated string!
// The Utf8(*) type is used...signaling no error checking should need to be
// done while walking through the UTF-8 sequence.  It also protects against
// naive `char` accesses and single-byte incrementation of UTF-8 data.
//
// So for instance: instead of simply saying:
//
//     Codepoint *ptr = String_Head(string_series);
//     Codepoint c = *ptr++;  // !!! invalid, treating UTF-8 like it's ASCII!
//
// ...one must instead write:
//
//     Utf8(*) ptr = String_Head(string_series);
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
    // debug build does not inline these classes and functions.  So traversing
    // strings involves a lot of constructing objects and calling methods that
    // call methods.  Hence these classes are used only in non-debug (and
    // hopefully) optimized builds, where the inlining makes it equivalent to
    // the C version.  That allows for the compile-time type checking but no
    // added runtime overhead.
    //
    template<typename T> struct Utf8Ptr;
    #define Utf8(star_or_const_star) \
        Utf8Ptr<Byte star_or_const_star>

    // Primary purpose of the classes is to disable the ability to directly
    // increment or decrement pointers to Byte* without going through helper
    // routines that do decoding.  But we still want to do pointer comparison,
    // and C++ sadly makes us write this all out.

    template<>
    struct Utf8Ptr<const Byte*> {
        const Byte* bp;  // will actually be mutable if constructed mutable

        Utf8Ptr () {}
        Utf8Ptr (nullptr_t n) : bp (n) {}
        explicit Utf8Ptr (const Byte* bp) : bp (bp) {}
        explicit Utf8Ptr (const char *cstr)
            : bp (cast(const Byte*, cstr)) {}

        Size operator-(const Byte* rhs)
          { return bp - rhs; }

        Size operator-(Utf8Ptr rhs)
          { return bp - rhs.bp; }

        bool operator==(const Utf8Ptr<const Byte*> &other)
          { return bp == other.bp; }

        bool operator==(const Byte* other)
          { return bp == other; }

        bool operator!=(const Utf8Ptr<const Byte*> &other)
          { return bp != other.bp; }

        bool operator!=(const Byte* other)
          { return bp != other; }

        bool operator>(const Utf8Ptr<const Byte*> &other)
          { return bp > other.bp; }

        bool operator<(const Byte* other)
          { return bp < other; }

        bool operator<=(const Utf8Ptr<const Byte*> &other)
          { return bp <= other.bp; }

        bool operator>=(const Byte* other)
          { return bp >= other; }

        operator bool() { return bp != nullptr; }  // implicit
        operator const void*() { return bp; }  // implicit
        operator const Byte*() { return bp; }  // implicit
        operator const char*() { return cast(const char*, bp); }  // implicit

        explicit operator Byte*() {  // explicit, does not require m_cast
            return m_cast(Byte*, bp);
        }
    };

    template<>
    struct Utf8Ptr<Byte*> : public Utf8Ptr<const Byte*> {
        Utf8Ptr () : Utf8Ptr<const Byte*>() {}
        Utf8Ptr (nullptr_t n) : Utf8Ptr<const Byte*>(n) {}
        explicit Utf8Ptr (Byte* bp)
            : Utf8Ptr<const Byte*> (bp) {}
        explicit Utf8Ptr (char *cstr)
            : Utf8Ptr<const Byte*> (cast(Byte*, cstr)) {}

        operator void*() { return m_cast(Byte*, bp); }  // implicit
        operator Byte*() { return m_cast(Byte*, bp); }  // implicit
        operator char*() { return x_cast(char*, bp); }  // implicit
    };
#endif
