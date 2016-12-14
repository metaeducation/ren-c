//
//  File: %reb-c.h
//  Summary: "General C definitions and constants"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
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
// Various configuration defines (from reb-config.h):
//
// HAS_LL_CONSTS - compiler allows 1234LL constants
// WEIRD_INT_64 - old MSVC typedef for 64 bit int
// OS_WIDE_CHAR - the OS uses wide chars (not UTF-8)
//


//
// STATIC ASSERT
//
// Some conditions can be checked at compile-time, instead of deferred to a
// runtime assert.  This macro triggers an error message at compile time.
// `static_assert` is an arity-2 keyword in C++11 (which was expanded in
// C++17 to have an arity-1 form).  This uses the name `static_assert_c` to
// implement a poor-man's version of the arity-1 form in C, that only works
// inside of function bodies.
//
// !!! This was the one being used, but review if it's the best choice:
//
// http://stackoverflow.com/questions/3385515/static-assert-in-c
//
#define static_assert_c(e) \
    do {(void)sizeof(char[1 - 2*!(e)]);} while(0)


//
// CASTING MACROS
//
// The following code and explanation is from "Casts for the Masses (in C)":
//
// http://blog.hostilefork.com/c-casts-for-the-masses/
//

#if !defined(__cplusplus)
    /* These macros are easier-to-spot variants of the parentheses cast.
     * The 'm_cast' is when getting [M]utablity on a const is okay (RARELY!)
     * Plain 'cast' can do everything else (except remove volatile)
     * The 'c_cast' helper ensures you're ONLY adding [C]onst to a value
     */
    #define m_cast(t,v)     ((t)(v))
    #define cast(t,v)       ((t)(v))
    #define c_cast(t,v)     ((t)(v))
    /*
     * Q: Why divide roles?  A: Frequently, input to cast is const but you
     * "just forget" to include const in the result type, gaining mutable
     * access.  Stray writes to that can cause even time-traveling bugs, with
     * effects *before* that write is made...due to "undefined behavior".
     */
#elif defined(__cplusplus) /* for gcc -Wundef */ && (__cplusplus < 201103L)
    /* Well-intentioned macros aside, C has no way to enforce that you can't
     * cast away a const without m_cast. C++98 builds can do that, at least:
     */
    #define m_cast(t,v)     const_cast<t>(v)
    #define cast(t,v)       ((t)(v))
    #define c_cast(t,v)     const_cast<t>(v)
#else
    /* __cplusplus >= 201103L has C++11's type_traits, where we get some
     * actual power.  cast becomes a reinterpret_cast for pointers and a
     * static_cast otherwise.  We ensure c_cast added a const and m_cast
     * removed one, and that neither affected volatility.
     */
    #include <type_traits>
    template<typename T, typename V>
    T m_cast_helper(V v) {
        static_assert(!std::is_const<T>::value,
            "invalid m_cast() - requested a const type for output result");
        static_assert(std::is_volatile<T>::value == std::is_volatile<V>::value,
            "invalid m_cast() - input and output have mismatched volatility");
        return const_cast<T>(v);
    }
    /* reinterpret_cast for pointer to pointer casting (non-class source)*/
    template<typename T, typename V,
        typename std::enable_if<
            !std::is_class<V>::value
            && (std::is_pointer<V>::value || std::is_pointer<T>::value)
        >::type* = nullptr>
                T cast_helper(V v) { return reinterpret_cast<T>(v); }
    /* static_cast for non-pointer to non-pointer casting (non-class source) */
    template<typename T, typename V,
        typename std::enable_if<
            !std::is_class<V>::value
            && (!std::is_pointer<V>::value && !std::is_pointer<T>::value)
        >::type* = nullptr>
                T cast_helper(V v) { return static_cast<T>(v); }
    /* use static_cast on all classes, to go through their cast operators */
    template<typename T, typename V,
        typename std::enable_if<
            std::is_class<V>::value
        >::type* = nullptr>
                T cast_helper(V v) { return static_cast<T>(v); }
    template<typename T, typename V>
    T c_cast_helper(V v) {
        static_assert(!std::is_const<T>::value,
            "invalid c_cast() - did not request const type for output result");
        static_assert(std::is_volatile<T>::value == std::is_volatile<V>::value,
            "invalid c_cast() - input and output have mismatched volatility");
        return const_cast<T>(v);
    }
    #define m_cast(t, v)    m_cast_helper<t>(v)
    #define cast(t, v)      cast_helper<t>(v)
    #define c_cast(t, v)    c_cast_helper<t>(v)
#endif
#if defined(NDEBUG) || !defined(REB_DEF)
    /* These [S]tring and [B]inary casts are for "flips" between a 'char *'
     * and 'unsigned char *' (or 'const char *' and 'const unsigned char *').
     * Being single-arity with no type passed in, they are succinct to use:
     */
    #define s_cast(b)       ((char *)(b))
    #define cs_cast(b)      ((const char *)(b))
    #define b_cast(s)       ((unsigned char *)(s))
    #define cb_cast(s)      ((const unsigned char *)(s))
    /*
     * In C++ (or C with '-Wpointer-sign') this is powerful.  'char *' can
     * be used with string functions like strlen().  Then 'unsigned char *'
     * can be saved for things you shouldn't _accidentally_ pass to functions
     * like strlen().  (One GREAT example: encoded UTF-8 byte strings.)
     */
#else
    /* We want to ensure the input type is what we thought we were flipping,
     * particularly not the already-flipped type.  Instead of type_traits, 4
     * functions check in both C and C++ (here only during Debug builds):
     * (Definitions are in n-strings.c w/prototypes built by make-headers.r)
     */
    #define s_cast(b)       s_cast_(b)
    #define cs_cast(b)      cs_cast_(b)
    #define b_cast(s)       b_cast_(s)
    #define cb_cast(s)      cb_cast_(s)
#endif


//
// NOOP a.k.a. VOID GENERATOR
//
// Creating a void value conveniently is useful for a few reasons.  One is
// that it can serve as a NO-OP and suppress a compiler warning you might
// get if you try to use just ';' to do it.  Another is that there is a
// difference between C and C++ in parenthesized expressions, where
// '(foo(), bar())' will return the result of bar in C++ but not in C.
// So such a macro could be written as '(foo(), bar(), NOOP)' to avoid
// leaking the result.
//
// VOID would be a more purposeful name, but Windows headers define that
// for the type (as used in types like LPVOID)

#ifndef NOOP
    #define NOOP \
        ((void)(0))
#endif


/***********************************************************************
**
**  C-Code Types
**
**      One of the biggest flaws in the C language was not
**      to indicate bitranges of integers. So, we do that here.
**      You cannot "abstractly remove" the range of a number.
**      It is a critical part of its definition.
**
***********************************************************************/

#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
/* C-code types: use C99 */

#include <stdint.h>

typedef int8_t          i8;
typedef uint8_t         u8;
typedef int16_t         i16;
typedef uint16_t        u16;
typedef int32_t         i32;
typedef uint32_t        u32;
typedef int64_t         i64;
typedef uint64_t        u64;
typedef intptr_t        REBIPT;     // integral counterpart of void*
typedef uintptr_t       REBUPT;     // unsigned counterpart of void*

#define MAX_I32 INT32_MAX
#define MIN_I32 INT32_MIN
#define MAX_I64 INT64_MAX
#define MIN_I64 INT64_MIN

#define I8_C(c)         INT8_C(c)
#define U8_C(c)         UINT8_C(c)

#define I16_C(c)        INT16_C(c)
#define U16_C(c)        UINT16_C(c)

#define I32_C(c)        INT32_C(c)
#define U32_C(c)        UINT32_C(c)

#define I64_C(c)        INT64_C(c)
#define U64_C(c)        UINT64_C(c)

#else
/* C-code types: C99 definitions unavailable, do it ourselves */

typedef signed char     i8;
typedef unsigned char   u8;
#define I8(c)           c
#define U8(c)           c

typedef short           i16;
typedef unsigned short  u16;
#define I16(c)          c
#define U16(c)          c

#ifdef __LP64__
typedef int             i32;
typedef unsigned int    u32;
#else
typedef long            i32;
typedef unsigned long   u32;
#endif
#define I32_C(c) c
#define U32_C(c) c ## U

#ifdef WEIRD_INT_64       // Windows VC6 nonstandard typing for 64 bits
typedef _int64          i64;
typedef unsigned _int64 u64;
#define I64_C(c) c ## I64
#define U64_C(c) c ## U64
#else
typedef long long       i64;
typedef unsigned long long u64;
#define I64_C(c) c ## LL
#define U64_C(c) c ## ULL
#endif
#ifdef __LLP64__
typedef long long       REBIPT;     // integral counterpart of void*
typedef unsigned long long  REBUPT;     // unsigned counterpart of void*
#else
typedef long            REBIPT;     // integral counterpart of void*
typedef unsigned long   REBUPT;     // unsigned counterpart of void*
#endif

#define MAX_I32 I32_C(0x7fffffff)
#define MIN_I32 ((i32)I32_C(0x80000000)) //compiler treats the hex literal as unsigned without casting
#define MAX_I64 I64_C(0x7fffffffffffffff)
#define MIN_I64 ((i64)I64_C(0x8000000000000000)) //compiler treats the hex literal as unsigned without casting

#endif

#define MAX_U32 U32_C(0xffffffff)
#define MAX_U64 U64_C(0xffffffffffffffff)


//
// BOOLEAN DEFINITION
//
// Rebol 3 historically built on C89 standard compilers, but the Ren-C branch
// advanced it to be able to build under C99=>C11 and C++98=>C++17 as well.
// There is a <stdbool.h> available in C99, but not in C89.  So unless the
// code abandons C89 support, a custom definition of boolean must be used,
// which is named REBOOL and uses the values TRUE and FALSE.
//
// The code takes advantage of the custom definition with a mode to build in
// that makes assignments to REBOOL reject integers entirely.  It still
// allows testing via if() and the logic operations, but merely disables
// direct assignments or passing integers as parameters to bools:
//
//     REBOOL b = 1 > 2;            // illegal: 1 > 2 is 0 (integer) in C
//     REBOOL b = LOGICAL(1 > 2);   // Ren-C legal form of assignment
//
// The macro LOGICAL() lets you convert any truthy C value to a REBOOL, and
// NOT() lets you do the inverse.  This is better than what was previously
// used often, a (REBOOL)cast_of_expression.  And it makes it much safer to
// use ordinary `&` operations to test for flags, more succinctly even:
//
//     REBOOL b = GET_FLAG(flags, SOME_FLAG_ORDINAL);
//     REBOOL b = !GET_FLAG(flags, SOME_FLAG_ORDINAL);
//
// vs.
//
//     REBOOL b = LOGICAL(flags & SOME_FLAG_BITWISE); // same
//     REBOOL b = NOT(flags & SOME_FLAG_BITWISE);     // 5 less chars
//
// (Bitwise vs. ordinal also permits initializing options by just |'ing them.)
//
// The compile-time checks for enforcing this don't lead to a working binary
// being built.  Hence the source will get out of sync with the check, so a
// CI build should be added to confirm STRICT_BOOL_COMPILER_TEST works.
//

#ifndef HAS_BOOL
    //
    // Some systems define a cpu-optimal BOOL already.  (Of course, all of
    // this should have been built into C in 1970.)  But if they don't, go
    // with whatever the compiler decides `int` is, as it is the default
    // "speedy choice" for modern CPUs
    //
    typedef int BOOL;
#endif

#ifdef STRICT_BOOL_COMPILER_TEST
    //
    // Force type errors on direct assignments of integers to booleans or
    // vice-versa (leading to a broken executable in the process).  Although
    // this catches some errors that wouldn't be caught otherwise, it notably
    // does not notice when a literal 0 is passed to a REBOOL, because that
    // is a valid pointer value.  However, this case is tested for by the
    // enum method of declaration in ordinary non-Windows builds.
    //
    struct Bool_Dummy { int dummy; };
    typedef struct Bool_Dummy *REBOOL;
    #define FALSE cast(struct Bool_Dummy*, 0x6466AE99)
    #define TRUE cast(struct Bool_Dummy*, 0x0421BD75)
#else
    #if (defined(FALSE) && (!FALSE)) && (defined(TRUE) && TRUE)

        #if defined(TO_WINDOWS) && !((FALSE == 0) && (TRUE == 1))
            //
            // The Windows API specifically mandates the value of TRUE as 1.
            // If you are compiling on Windows with something that has
            // predefined the constant as some other value, it will be
            // inconsistent...and won't work out.
            //
            #error "Compiler's FALSE != 0 or TRUE != 1, invalid for Win32"
        #else
            // Outside of Win32, assume any C truthy/falsey definition that
            // the compiler favors is all right.
        #endif

        // There's a FALSE and TRUE defined and they are logically false and
        // true respectively, so just use those definitions but make REBOOL
        //
        typedef BOOL REBOOL;

    #elif !defined(FALSE) && !defined(TRUE)
        //
        // An enum-based definition would prohibit the usage of TRUE and FALSE
        // in preprocessor macros, but offer some amount of type safety.
        //
        // http://stackoverflow.com/a/23666263/211160
        //
        // The tradeoff is worth it, but interferes with the hardcoded Windows
        // definitions of TRUE as 1 and FALSE as 0.  So only use it outside
        // of Windows.
        //
        #ifdef TO_WINDOWS
            #define FALSE 0
            #define TRUE 1

            typedef BOOL REBOOL;
        #else
            typedef enum {FALSE = 0, TRUE = !FALSE} REBOOL;
        #endif

    #else
        // TRUE and FALSE are defined but are not their logic meanings.
        //
        #error "Bad TRUE and FALSE definitions in compiler environment"
    #endif
#endif

#define LOGICAL(x) \
    ((x) ? TRUE : FALSE)
#define NOT(x) \
    ((x) ? FALSE : TRUE)

typedef i8 REBOOL8; // Small for struct packing (memory optimization vs CPU)



// Used for cases where we need 64 bits, even in 32 bit mode.
// (Note: compatible with FILETIME used in Windows)
#pragma pack(4)
typedef struct sInt64 {
    i32 l;
    i32 h;
} I64;
#pragma pack()

/***********************************************************************
**
**  REBOL Code Types
**
***********************************************************************/

typedef i32             REBINT;     // 32 bit (64 bit defined below)
typedef u32             REBCNT;     // 32 bit (counting number)
typedef i64             REBI64;     // 64 bit integer
typedef u64             REBU64;     // 64 bit unsigned integer
typedef float           REBD32;     // 32 bit decimal
typedef double          REBDEC;     // 64 bit decimal

typedef unsigned char   REBYTE;     // unsigned byte data

#define MIN_D64 ((double)-9.2233720368547758e18)
#define MAX_D64 ((double) 9.2233720368547758e18)

// Useful char constants:
enum {
    BEL =   7,
    BS  =   8,
    LF  =  10,
    CR  =  13,
    ESC =  27,
    DEL = 127
};

// Used for MOLDing:
#define MAX_DIGITS 17   // number of digits
#define MAX_NUMCHR 32   // space for digits and -.e+000%

/***********************************************************************
**
**  64 Bit Integers - Now supported in REBOL 3.0
**
***********************************************************************/

#define MAX_INT_LEN     21
#define MAX_HEX_LEN     16

#ifdef ITOA64           // Integer to ascii conversion
#define INT_TO_STR(n,s) _i64toa(n, s_cast(s), 10)
#else
#define INT_TO_STR(n,s) Form_Int_Len(s, n, MAX_INT_LEN)
#endif

#ifdef ATOI64           // Ascii to integer conversion
#define CHR_TO_INT(s)   _atoi64(cs_cast(s))
#else
#define CHR_TO_INT(s)   strtoll(cs_cast(s), 0, 10)
#endif

#define LDIV            lldiv
#define LDIV_T          lldiv_t


//
// C FUNCTION TYPE (__cdecl)
//
// Note that you *CANNOT* cast something like a `void *` to (or from) a
// function pointer.  Pointers to functions are not guaranteed to be the same
// size as to data, in either C or C++.  A compiler might count the number of
// functions in your program, find less than 255, and use bytes for function
// pointers:
//
// http://stackoverflow.com/questions/3941793/
//
// So if you want something to hold either a function pointer or a data
// pointer, you have to implement that as a union...and know what you're doing
// when writing and reading it.
//
// For info on the difference between __stdcall and __cdecl:
//
// http://stackoverflow.com/questions/3404372/
//
//
#ifdef TO_WINDOWS
    typedef void (__cdecl CFUNC)(void);
#else
    typedef void (CFUNC)(void);
#endif


//
// TESTING IF A NUMBER IS FINITE
//
// C89 and C++98 had no standard way of testing for if a number was finite or
// not.  Windows and POSIX came up with their own methods.  Finally it was
// standardized in C99 and C++11:
//
// http://en.cppreference.com/w/cpp/numeric/math/isfinite
//
// The name was changed to `isfinite()`.  And conforming C99 and C++11
// compilers can omit the old versions, so one cannot necessarily fall back on
// the old versions still being there.  Yet the old versions don't have
// isfinite(), so those have to be worked around here as well.
//
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
    // C99 or later
    #define FINITE isfinite
#elif defined(__cplusplus) && __cplusplus >= 199711L
    // C++11 or later
    #define FINITE isfinite
#else
    // Other fallbacks...
    #ifdef TO_WINDOWS
        #define FINITE _finite // The usual answer for Windows
    #else
        #define FINITE finite // The usual answer for POSIX
    #endif
#endif


//
// UNICODE CHARACTER TYPE
//
// REBUNI is a two-byte UCS-2 representation of a Unicode codepoint.  Some
// routines once errantly conflated wchar_t with REBUNI, but a wchar_t is not
// 2 bytes on all platforms (it's 4 on GCC in 64-bit Linux, for instance).
// Routines for handling UCS-2 must be custom-coded or come from a library.
// (For example: you can't use wcslen() so Strlen_Uni() is implemented inside
// of Rebol.)
//
// Rebol is able to have its strings start out as UCS-1, with a single byte
// per character.  For that it uses REBYTEs.  But when you insert something
// requiring a higher codepoint, it goes to UCS-2 with REBUNI and will not go
// back (at time of writing).
//
// !!! BEWARE that several lower level routines don't do this widening, so be
// sure that you check which are which.
//
// Longer term, the growth of emoji usage in Internet communication has led
// to supporting higher "astral" codepoints as being a priority.  This means
// either being able to "double-widen" to UCS-4, as is Red's strategy:
//
// http://www.red-lang.org/2012/09/plan-for-unicode-support.html
//
// Or it could also mean shifting to "UTF-8 everywhere":
//
// http://utf8everywhere.org
//

typedef u16 REBUNI;

#define MAX_UNI \
    ((1 << (8 * sizeof(REBUNI))) - 1)


//
// MEMORY ALLOCATION AND FREEING MACROS
//
// Rebol's internal memory management is done based on a pooled model, which
// use Alloc_Mem and Free_Mem instead of calling malloc directly.  (See the
// comments on those routines for explanations of why this was done--even in
// an age of modern thread-safe allocators--due to Rebol's ability to exploit
// extra data in its pool block when a series grows.)
//
// Since Free_Mem requires the caller to pass in the size of the memory being
// freed, it can be tricky.  These macros are modeled after C++'s new/delete
// and new[]/delete[], and allocations take either a type or a type and a
// length.  The size calculation is done automatically, and the result is cast
// to the appropriate type.  The deallocations also take a type and do the
// calculations.
//
// In a C++11 build, an extra check is done to ensure the type you pass in a
// FREE or FREE_N lines up with the type of pointer being freed.
//

#define ALLOC(t) \
    cast(t *, Alloc_Mem(sizeof(t)))

#define ALLOC_ZEROFILL(t) \
    cast(t *, memset(ALLOC(t), '\0', sizeof(t)))

#define ALLOC_N(t,n) \
    cast(t *, Alloc_Mem(sizeof(t) * (n)))

#define ALLOC_N_ZEROFILL(t,n) \
    cast(t *, memset(ALLOC_N(t, (n)), '\0', sizeof(t) * (n)))

#if defined(__cplusplus) && __cplusplus >= 201103L
    #include <type_traits>

    #define FREE(t,p) \
        do { \
            static_assert( \
                std::is_same<decltype(p), std::add_pointer<t>::type>::value, \
                "mismatched FREE type" \
            ); \
            Free_Mem(p, sizeof(t)); \
        } while (0)

    #define FREE_N(t,n,p)   \
        do { \
            static_assert( \
                std::is_same<decltype(p), std::add_pointer<t>::type>::value, \
                "mismatched FREE_N type" \
            ); \
            Free_Mem(p, sizeof(t) * (n)); \
        } while (0)
#else
    #define FREE(t,p) \
        Free_Mem((p), sizeof(t))

    #define FREE_N(t,n,p)   \
        Free_Mem((p), sizeof(t) * (n))
#endif

#define CLEAR(m, s) \
    memset((void*)(m), 0, s)

#define CLEARS(m) \
    memset((void*)(m), 0, sizeof(*m))


//
// MEMORY POISONING and POINTER TRASHING
//
// If one wishes to indicate a region of memory as being "off-limits", modern
// tools like Address Sanitizer allow instrumented builds to augment reads
// from memory to check to see if that region is in a blacklist.
//
// These "poisoned" areas are generally sub-regions of valid malloc()'d memory
// that contain bad data.  Yet they cannot be free()d because they also
// contain some good data.  (Or it is merely desirable to avoid freeing and
// then re-allocating them for performance reasons, yet a debug build still
// would prefer to intercept accesses as if they were freed.)
//
// Also, in order to overwrite a pointer with garbage, the historical method
// of using 0xBADF00D or 0xDECAFBAD is formalized with TRASH_POINTER_IF_DEBUG.
// This makes the instances easier to find and standardizes how it is done.
//
#ifdef HAVE_ASAN_INTERFACE_H
    #include <sanitizer/asan_interface.h>

    // <IMPORTANT> Address sanitizer's memory poisoning must not have two
    // threads both poisoning/unpoisoning the same addresses at the same time.

    #define POISON_MEMORY(reg, mem_size) \
        ASAN_POISON_MEMORY_REGION(reg, mem_size)

    #define UNPOISON_MEMORY(reg, mem_size) \
        ASAN_UNPOISON_MEMORY_REGION(reg, mem_size)
#else
    // !!! @HostileFork wrote a tiny C++ "poor man's memory poisoner" that
    // uses XOR to poison bits and then unpoison them back.  This might be
    // useful to instrument C++-based DEBUG builds on platforms that did not
    // have address sanitizer (if that ever becomes interesting).

    #define POISON_MEMORY(reg, mem_size) \
        NOOP

    #define UNPOISON_MEMORY(reg, mem_size) \
        NOOP
#endif

#ifdef NDEBUG
    #define TRASH_POINTER_IF_DEBUG(p) \
        NOOP
#else
    #if defined(__cplusplus)
        template<class T>
        inline static void TRASH_POINTER_IF_DEBUG(T* &p) {
            p = reinterpret_cast<T*>(static_cast<REBUPT>(0xDECAFBAD));
        }

        template<class T>
        inline static REBOOL IS_POINTER_TRASH_DEBUG(T* p) {
            return LOGICAL(
                p == reinterpret_cast<T*>(static_cast<REBUPT>(0xDECAFBAD))
            );
        }
    #else
        #define TRASH_POINTER_IF_DEBUG(p) \
            ((p) = cast(void*, cast(REBUPT, 0xDECAFBAD)))

        #define IS_POINTER_TRASH_DEBUG(p) \
            LOGICAL((p) == cast(void*, cast(REBUPT, 0xDECAFBAD)))
    #endif
#endif


/***********************************************************************
**
**  ATTRIBUTES
**
**      The __attribute__ feature is non-standard and only available
**      in some compilers.  Individual attributes themselves are
**      also available on a case-by-case basis.
**
**      Note: Placing the attribute after the prototype seems to lead
**      to complaints, and technically there is a suggestion you may
**      only define attributes on prototypes--not definitions:
**
**          http://stackoverflow.com/q/23917031/211160
**
**      Putting the attribute *before* the prototype seems to allow
**      it on both the prototype and definition in gcc, however.
**
***********************************************************************/

#ifndef __has_builtin
    #define __has_builtin(x) 0
#endif

#ifndef __has_feature
    #define __has_feature(x) 0
#endif

#ifdef __GNUC__
    #define GCC_VERSION_AT_LEAST(m, n) \
        (__GNUC__ > (m) || (__GNUC__ == (m) && __GNUC_MINOR__ >= (n)))
#else
    #define GCC_VERSION_AT_LEAST(m, n) 0
#endif

#if __has_feature(address_sanitizer) || GCC_VERSION_AT_LEAST(4, 8)
    #define ATTRIBUTE_NO_SANITIZE_ADDRESS __attribute__ ((no_sanitize_address))
#else
    #define ATTRIBUTE_NO_SANITIZE_ADDRESS
#endif

#if defined(__clang__) || GCC_VERSION_AT_LEAST(2, 5)
    #define ATTRIBUTE_NO_RETURN __attribute__ ((noreturn))
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    #define ATTRIBUTE_NO_RETURN _Noreturn
#elif defined(_MSC_VER)
    #define ATTRIBUTE_NO_RETURN __declspec(noreturn)
#else
    #define ATTRIBUTE_NO_RETURN
#endif

#if __has_builtin(__builtin_unreachable) || GCC_VERSION_AT_LEAST(4, 5)
    #define DEAD_END __builtin_unreachable()
#elif defined(_MSC_VER)
    __declspec(noreturn) static inline void msvc_unreachable() {
        while (TRUE) { }
    }
    #define DEAD_END msvc_unreachable()
#else
    #define DEAD_END
#endif



//=////////////////////////////////////////////////////////////////////////=//
//
// BIT FLAGS & MASKING
//
//=////////////////////////////////////////////////////////////////////////=//
//
// When flags are needed, the platform-natural unsigned integer is used
// (REBUPT, a `uintptr_t` equivalent).
//
// The 64-bit macro is used to get a 64-bit flag even on 32-bit platforms.
// Hence it should be stored in a REBU64 and not in a REBFLGS.
//

typedef REBUPT REBFLGS;

#define FLAGIT(f) \
    ((REBUPT)1 << (f))

#define FLAGIT_64(n) \
    ((REBU64)1 << (n))

 // !!! These are leftovers from old code which used integers instead of
// masks to indicate flags.  Using masks then it's easy enough to read using
// C's plain bit masking operators.
//
#define GET_FLAG(v,f)       LOGICAL((v) & (1u << (f)))
#define GET_FLAGS(v,f,g)    LOGICAL((v) & ((1u << (f)) | (1u << (g))))
#define SET_FLAG(v,f)       cast(void, (v) |= (1u << (f)))
#define CLR_FLAG(v,f)       cast(void, (v) &= ~(1u << (f)))
#define CLR_FLAGS(v,f,g)    cast(void, (v) &= ~((1u << (f)) | (1u << (g))))


//=////////////////////////////////////////////////////////////////////////=//
//
// BYTE-ORDER SENSITIVE BIT FLAGS & MASKING
//
//=////////////////////////////////////////////////////////////////////////=//
//
// These macros are for purposefully arranging bit flags with respect to the
// "leftmost" and "rightmost" bytes of the underlying platform:
//
//     REBFLGS flags = FLAGIT_LEFT(0);
//     unsigned char *ch = (unsigned char*)&flags;
//
// In the code above, the leftmost bit of the flags has been set to 1,
// resulting in `ch == 128` on all supported platforms.
//
// Quantities smaller than a byte can be mixed in on the right with flags
// from the left.  These form single optimized constants, which can be
// assigned to an integer.  They can be masked or shifted out efficiently:
//
//    REBFLGS flags = FLAGIT_LEFT(0) | FLAGIT_LEFT(1) | FLAGVAL_RIGHT(13);
//
//    REBCNT left = LEFT_N_BITS(flags, 3); // == 6 (binary `110`)
//    REBCNT right = RIGHT_N_BITS(flags, 3); // == 5 (binary `101`)
//
// `left` gets `110` because it asked for the left 3 bits, of which only
// the first and the second had been set.
//
// `right` gets `101` because 13 was binary `1101` that was added into the
// value.  Yet only the rightmost 3 bits were requested.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Note: It is simpler to not worry about the underlying bytes and just use
// ordinary bit masking.  But this is used for an important feature (the
// discernment of a `void*` to a REBVAL from that of a valid UTF-8 string).
// Other tools that might be tried with this all have downsides:
//
// * bitfields arranged in a `union` with integers have no layout guarantee
// * `#pragma pack` is not standard C98 or C99...nor is any #pragma
// * `char[4]` or `char[8]` can't generally be assigned in one instruction
//

#if defined(__LP64__) || defined(__LLP64__)
    #define PLATFORM_BITS 64
#else
    #define PLATFORM_BITS 32
#endif

#if defined(ENDIAN_BIG) // Byte w/most significant bit first

    #define FLAGIT_LEFT(n) \
        ((REBUPT)1 << PLATFORM_BITS - (n) - 1) // 63,62,61.. or 32,31,30..

    #define FLAGVAL_RIGHT(val) \
        ((REBUPT)val) // little endian needs val <= 255

    #define FLAGVAL_MID(val) \
        (((REBUPT)val) << 8) // little endian needs val <= 255

#elif defined(ENDIAN_LITTLE) // Byte w/least significant bit first (e.g. x86)

    #define FLAGIT_LEFT(n) \
        ((REBUPT)1 << 7 + ((n) / 8) * 8 - (n) % 8) // 7,6,5..0,15,14..8,23..

    #define FLAGVAL_RIGHT(val) \
        ((REBUPT)(val) << (PLATFORM_BITS - 8)) // val <= 255

    #define FLAGVAL_MID(val) \
        ((REBUPT)(val) << (PLATFORM_BITS - 16)) // val <= 255
#else
    // !!! There are macro hacks which can actually make reasonable guesses
    // at endianness, and should probably be used in the config if nothing is
    // specified explicitly.
    //
    // http://stackoverflow.com/a/2100549/211160
    //
    #error "ENDIAN_BIG or ENDIAN_LITTLE must be defined"
#endif

// These specialized extractions of N bits out of the leftmost, rightmost,
// or "middle" byte (one step to the left of rightmost) can be expressed in
// a platform-agnostic way.  The constructions by integer to establish these
// positions are where the the difference is.
//
// !!! It would be possible to do this with integer shifting on big endian
// in a "simpler" way, e.g.:
//
//    #define LEFT_N_BITS(flags,n) \
//      ((flags) >> PLATFORM_BITS - (n))
//
// But in addition to big endian platforms being kind of rare, it's not clear
// that would be faster than a byte operation, especially with optimization.
//

#define LEFT_N_BITS(flags,n) \
    (((REBYTE*)&flags)[0] >> 8 - (n)) // n <= 8

#define RIGHT_N_BITS(flags,n) \
    (((REBYTE*)&flags)[sizeof(REBUPT) - 1] & ((1 << (n)) - 1)) // n <= 8

#define CLEAR_N_RIGHT_BITS(flags,n) \
    (((REBYTE*)&flags)[sizeof(REBUPT) - 1] &= ~((1 << (n)) - 1)) // n <= 8

#define MID_N_BITS(flags,n) \
    (((REBYTE*)&flags)[sizeof(REBUPT) - 2] & ((1 << (n)) - 1)) // n <= 8

#define CLEAR_N_MID_BITS(flags,n) \
    (((REBYTE*)&flags)[sizeof(REBUPT) - 2] &= ~((1 << (n)) - 1)) // n <= 8      



/***********************************************************************
**
**  Useful Macros
**
***********************************************************************/

// Skip to the specified byte but not past the provided end
// pointer of the byte string.  Return NULL if byte is not found.
//
inline static const REBYTE *Skip_To_Byte(
    const REBYTE *cp,
    const REBYTE *ep,
    REBYTE b
) {
    while (cp != ep && *cp != b) cp++;
    if (*cp == b) return cp;
    return 0;
}


// It is common for MIN and MAX to be defined in C to macros; and equally
// common to assume that undefining them and redefining them to something
// that acts like one would expect is "probably ok".  :-/
//
#undef MIN
#undef MAX
#ifdef min
    #define MIN(a,b) min(a,b)
    #define MAX(a,b) max(a,b)
#else
    #define MIN(a,b) (((a) < (b)) ? (a) : (b))
    #define MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif


// Byte string functions:
// Use these when you semantically are talking about unsigned REBYTEs
//
// (e.g. if you want to count unencoded chars in 'char *' use strlen(), and
// the reader will know that is a count of letters.  If you have something
// like UTF-8 with more than one byte per character, use LEN_BYTES.)
//
// For APPEND_BYTES_LIMIT, m is the max-size allocated for d (dest)
#if defined(NDEBUG) || !defined(REB_DEF)
    #define LEN_BYTES(s) \
        strlen((const char*)(s))
    #define COPY_BYTES(d,s,n) \
        strncpy((char*)(d), (const char*)(s), (n))
    #define COMPARE_BYTES(l,r) \
        strcmp((const char*)(l), (const char*)(r))
    #define APPEND_BYTES_LIMIT(d,s,m) \
        strncat((char*)d, (const char*)s, MAX((m) - strlen((char*)d) - 1, 0))
#else
    // Debug build uses function stubs to ensure you pass in REBYTE *
    // (But only if building in Rebol Core, host doesn't get the exports)
    #define LEN_BYTES(s) \
        LEN_BYTES_(s)
    #define COPY_BYTES(d,s,n) \
        COPY_BYTES_((d), (s), (n))
    #define COMPARE_BYTES(l,r) \
        COMPARE_BYTES_((l), (r))
    #define APPEND_BYTES_LIMIT(d,s,m) \
        APPEND_BYTES_LIMIT_((d), (s), (m))
#endif

#define ROUND_TO_INT(d) (REBINT)(floor((MAX(MIN_I32, MIN(MAX_I32, d))) + 0.5))

// Global pixel format setup for REBOL image!, image loaders, color handling,
// tuple! conversions etc.  The graphics compositor code should rely on this
// setting(and do specific conversions if needed)
//
// TO_RGBA_COLOR always returns 32bit RGBA value, converts R,G,B,A
// components to native RGBA order
//
// TO_PIXEL_COLOR must match internal image! datatype byte order, converts
// R,G,B,A components to native image format
//
// C_R, C_G, C_B, C_A Maps color components to correct byte positions for
// image! datatype byte order

#ifdef ENDIAN_BIG // ARGB pixel format on big endian systems
    #define TO_RGBA_COLOR(r,g,b,a) \
        (cast(REBCNT, (r)) << 24 \
        | cast(REBCNT, (g)) << 16 \
        | cast(REBCNT, (b)) << 8 \
        | cast(REBCNT, (a)))

    #define C_A 0
    #define C_R 1
    #define C_G 2
    #define C_B 3

    #define TO_PIXEL_COLOR(r,g,b,a) \
        (cast(REBCNT, (a)) << 24 \
        | cast(REBCNT, (r)) << 16 \
        | cast(REBCNT, (g)) << 8 \
        | cast(REBCNT, (b)))
#else
    #define TO_RGBA_COLOR(r,g,b,a) \
        (cast(REBCNT, (a)) << 24 \
        | cast(REBCNT, (b)) << 16 \
        | cast(REBCNT, (g)) << 8 \
        | cast(REBCNT, (r)))

    #ifdef TO_ANDROID_ARM // RGBA pixel format on Android
        #define C_R 0
        #define C_G 1
        #define C_B 2
        #define C_A 3

        #define TO_PIXEL_COLOR(r,g,b,a) \
            (cast(REBCNT, (a)) << 24 \
            | cast(REBCNT, (b)) << 16 \
            | cast(REBCNT, (g)) << 8 \
            | cast(REBCNT, (r)))

    #else // BGRA pixel format on Windows
        #define C_B 0
        #define C_G 1
        #define C_R 2
        #define C_A 3

        #define TO_PIXEL_COLOR(r,g,b,a) \
            (cast(REBCNT, (a)) << 24 \
            | cast(REBCNT, (r)) << 16 \
            | cast(REBCNT, (g)) << 8 \
            | cast(REBCNT, (b)))
    #endif
#endif
