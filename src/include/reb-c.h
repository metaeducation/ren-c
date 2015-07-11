/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Summary: General C definitions and constants
**  Module:  reb-c.h
**  Author:  Carl Sassenrath, Ladislav Mecir
**  Notes:
**      Various configuration defines (from reb-config.h):
**
**      HAS_LL_CONSTS - compiler allows 1234LL constants
**      ODD_INT_64 - old MSVC typedef for 64 bit int
**      OS_WIDE_CHAR - the OS uses wide chars (not UTF-8)
**
***********************************************************************/

#if defined(__clang__) || defined (__GNUC__)
# define ATTRIBUTE_NO_SANITIZE_ADDRESS __attribute__((no_sanitize_address))
#else
# define ATTRIBUTE_NO_SANITIZE_ADDRESS
#endif


/***********************************************************************
**
**	CASTING MACROS
**
**		The following code and explanation is taken from the article
**		"Casts for the Masses (in C)":
**
**		http://blog.hostilefork.com/c-casts-for-the-masses/
**
***********************************************************************/

#if !defined(__cplusplus)
	/* These macros are easier-to-spot variants of the parentheses cast.
	 * The 'm_cast' is when getting [M]utablity on a const is okay (RARELY!)
	 * Plain 'cast' can do everything else (except remove volatile)
	 * The 'c_cast' helper ensures you're ONLY adding [C]onst to a value
	 */
	#define m_cast(t,v)		((t)(v))
	#define cast(t,v)		((t)(v))
	#define c_cast(t,v)		((t)(v))
	/*
	 * Q: Why divide roles?  A: Frequently, input to cast is const but you
	 * "just forget" to include const in the result type, gaining mutable
	 * access.  Stray writes to that can cause even time-traveling bugs, with
	 * effects *before* that write is made...due to "undefined behavior".
	 */
#elif __cplusplus < 201103L
	/* Well-intentioned macros aside, C has no way to enforce that you can't
	 * cast away a const without m_cast. C++98 builds can do that, at least:
	 */
	#define m_cast(t,v)		const_cast<t>(v)
	#define cast(t,v)		((t)(v))
	#define c_cast(t,v)		const_cast<t>(v)
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
	template<typename T, typename V,
		typename std::enable_if<std::is_pointer<V>::value
			|| std::is_pointer<T>::value>::type* = nullptr>
				T cast_helper(V v) { return reinterpret_cast<T>(v); }
	template<typename T, typename V,
		typename std::enable_if<!std::is_pointer<V>::value
			&& !std::is_pointer<T>::value>::type* = nullptr>
				T cast_helper(V v) { return static_cast<T>(v); }
	template<typename T, typename V>
	T c_cast_helper(V v) {
		static_assert(!std::is_const<T>::value,
			"invalid c_cast() - did not request const type for output result");
		static_assert(std::is_volatile<T>::value == std::is_volatile<V>::value,
			"invalid c_cast() - input and output have mismatched volatility");
		return const_cast<T>(v);
	}
	#define m_cast(t, v)	m_cast_helper<t>(v)
	#define cast(t, v)		cast_helper<t>(v)
	#define c_cast(t, v)	c_cast_helper<t>(v)
#endif
#if defined(NDEBUG) || !defined(REB_DEF)
	/* These [S]tring and [B]inary casts are for "flips" between a 'char *'
	 * and 'unsigned char *' (or 'const char *' and 'const unsigned char *').
	 * Being single-arity with no type passed in, they are succinct to use:
	 */
	#define s_cast(b)		((char *)(b))
	#define cs_cast(b)		((const char *)(b))
	#define b_cast(s)		((unsigned char *)(s))
	#define cb_cast(s)		((const unsigned char *)(s))
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
	/*char *s_cast(unsigned char *b);
		{ return cast(char *, b); } */
	/*const char *cs_cast(const unsigned char *b);
		{ return cast(const char *, b); } */
	/*unsigned char *b_cast(char *s);
		{ return cast(unsigned char *, s); } */
	/*const unsigned char *cb_cast(const char *s);
		{ return cast(const unsigned char *, s); } */
#endif


#ifndef FALSE
#define FALSE 0
#define TRUE (!0)
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

typedef int8_t			i8;
typedef uint8_t			u8;
typedef int16_t			i16;
typedef uint16_t		u16;
typedef int32_t			i32;
typedef uint32_t		u32;
typedef int64_t			i64;
typedef uint64_t		u64;
typedef intptr_t		REBIPT;		// integral counterpart of void*
typedef uintptr_t		REBUPT;		// unsigned counterpart of void*

#define MAX_I32 INT32_MAX
#define MIN_I32 INT32_MIN
#define MAX_I64 INT64_MAX
#define MIN_I64 INT64_MIN

#define I8_C(c)			INT8_C(c)
#define U8_C(c)			UINT8_C(c)

#define I16_C(c)		INT16_C(c)
#define U16_C(c)		UINT16_C(c)

#define I32_C(c)		INT32_C(c)
#define U32_C(c)		UINT32_C(c)

#define I64_C(c)		INT64_C(c)
#define U64_C(c)		UINT64_C(c)

#else
/* C-code types: C99 definitions unavailable, do it ourselves */

typedef char			i8;
typedef unsigned char	u8;
#define I8(c) 			c
#define U8(c) 			c

typedef short			i16;
typedef unsigned short	u16;
#define I16(c) 			c
#define U16(c) 			c

#ifdef __LP64__
typedef int				i32;
typedef unsigned int	u32;
#else
typedef long			i32;
typedef unsigned long	u32;
#endif
#define I32_C(c) c
#define U32_C(c) c ## U

#ifdef ODD_INT_64       // Windows VC6 nonstandard typing for 64 bits
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
typedef long long		REBIPT;		// integral counterpart of void*
typedef unsigned long long	REBUPT;		// unsigned counterpart of void*
#else
typedef long			REBIPT;		// integral counterpart of void*
typedef unsigned long	REBUPT;		// unsigned counterpart of void*
#endif

#define MAX_I32 I32_C(0x7fffffff)
#define MIN_I32 ((i32)I32_C(0x80000000)) //compiler treats the hex literal as unsigned without casting
#define MAX_I64 I64_C(0x7fffffffffffffff)
#define MIN_I64 ((i64)I64_C(0x8000000000000000)) //compiler treats the hex literal as unsigned without casting

#endif
/* C-code types */

#define MAX_U32 U32_C(0xffffffff)
#define MAX_U64 U64_C(0xffffffffffffffff)


#ifndef DEF_UINT		// some systems define it, don't define it again
typedef unsigned int    uint;
#endif

// Some systems define a cpu-optimal BOOL already. It is assumed that the
// R3 lib will use that same definition (so sizeof() is identical.)
// (Of course, all of this should have been built into C in 1970.)
#ifndef HAS_BOOL
typedef int BOOL;       // (int is used for speed in modern CPUs)
#endif

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

typedef i32				REBINT;     // 32 bit (64 bit defined below)
typedef u32				REBCNT;     // 32 bit (counting number)
typedef i64				REBI64;     // 64 bit integer
typedef u64				REBU64;     // 64 bit unsigned integer
typedef i8				REBOOL;     // 8  bit flag (for struct usage)
typedef u32				REBFLG;     // 32 bit flag (for cpu efficiency)
typedef float			REBD32;     // 32 bit decimal
typedef double			REBDEC;     // 64 bit decimal

typedef unsigned char	REBYTE;     // unsigned byte data

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

/***********************************************************************
**
**  Address and Function Pointers
**
***********************************************************************/

#ifdef TO_WIN32
typedef long (__stdcall *FUNCPTR)();
typedef void(__cdecl *CFUNC)(void *);
#else
typedef long (*FUNCPTR)();
typedef void(*CFUNC)(void *);
#endif


/***********************************************************************
**
**  TESTING IF A NUMBER IS FINITE
**
**		C89 and C++98 had no standard way of testing for if a number
**		was finite or not.  Windows and POSIX came up with their
**		own methods.  Finally it was standardized in C99 and C++11:
**
**			http://en.cppreference.com/w/cpp/numeric/math/isfinite
**
**		The name was changed to `isfinite()`.  And conforming C99
**		and C++11 compilers can omit the old versions, so one cannot
**		necessarily fall back on the old versions still being there.
**		Yet the old versions don't have isfinite, so those have to
**		be worked around here as well.
**
***********************************************************************/

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
	// C99 or later
	#define FINITE isfinite
#elif defined(__cplusplus) && __cplusplus >= 199711L
	// C++11 or later
	#define FINITE isfinite
#else
	// Other fallbacks...
	#ifdef TO_WIN32
		#define FINITE _finite // The usual answer for Windows
	#else
		#define FINITE finite // The usual answer for POSIX
	#endif
#endif


/***********************************************************************
**
**  UNICODE CHARACTER TYPE
**
**		REBUNI is a two-byte UCS-2 representation of a Unicode codepoint.
**		Some routines once errantly conflated wchar_t with REBUNI, but
**		a wchar_t is not 2 bytes on all platforms (it's 4 on GCC in
**		64-bit Linux, for instance).  Routines for handling UCS-2 must be
**		custom coded or come from a library.  (For example: you can't use
**		wcslen() so Strlen_Uni() is implemented inside of Rebol.)
**
**		Rebol is able to have its strings start out as UCS-1, with a
**		single byte per character.  For that it uses REBYTEs.  But when
**		you insert something requiring a higher codepoint, it goes
**		to UCS-2 with REBUNI and will not go back (at time of writing).
**
**		!!! BEWARE that several lower level routines don't do this
**		widening, so be sure that you check which are which.
**
**		Longer term, Rebol should seek to align with Red's Unicode
**		strategy, which would go further to UCS-4:
**
**		http://www.red-lang.org/2012/09/plan-for-unicode-support.html
** 
***********************************************************************/

typedef u16 REBUNI;

#define MAX_UNI ((1 << (8 * sizeof(REBUNI))) - 1)


/***********************************************************************
**
**	MEMORY ALLOCATION AND FREEING MACROS
**
**		Rebol's internal memory management is done based on a pooled
**		model, which use Alloc_Mem and Free_Mem instead of calling
**		malloc directly.  (See the comments on those routines for
**		explanations of why this makes sense--even in an age of
**		modern thread-safe allocators--due to Rebol's ability to
**		exploit extra data in its pool block when a series grows.)
**
**		Since Free_Mem requires the caller to pass in the size of
**		the memory being freed, it can be tricky.  These macros are
**		are modeled after C++'s new/delete and new[]/delete[], and
**		allocations take either a type or a type and a length.  The
**		size calculation is done automatically, and the result is
**		cast to the appropriate type.  The deallocations also take
**		a type and do the calculations.
**
**		In a C++11 build, an extra check is done to ensure the type
**		you pass in a FREE or FREE_ARRAY lines up with the type of
**		pointer being passed in to be freed.
**
***********************************************************************/

#define ALLOC(t) \
	cast(t *, Alloc_Mem(sizeof(t)))

#define ALLOC_ZEROFILL(t) \
	cast(t *, memset(ALLOC(t), '\0', sizeof(t)))

#define ALLOC_ARRAY(t,n) \
	cast(t *, Alloc_Mem(sizeof(t) * (n)))

#define ALLOC_ARRAY_ZEROFILL(t,n) \
	cast(t *, memset(ALLOC_ARRAY(t, (n)), '\0', sizeof(t) * (n)))

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

	#define FREE_ARRAY(t,n,p)	\
		do { \
			static_assert( \
				std::is_same<decltype(p), std::add_pointer<t>::type>::value, \
				"mismatched FREE_ARRAY type" \
			); \
			Free_Mem(p, sizeof(t) * (n)); \
		} while (0)
#else
	#define FREE(t,p) \
		Free_Mem((p), sizeof(t))

	#define FREE_ARRAY(t,n,p)	\
		Free_Mem((p), sizeof(t) * (n))
#endif

// Memory clearing macros:
#define CLEAR(m, s)     memset((void*)(m), 0, s);
#define CLEARS(m)       memset((void*)(m), 0, sizeof(*m));


/***********************************************************************
**
**  Useful Macros
**
***********************************************************************/

#define FLAGIT(f)           (1<<(f))
#define GET_FLAG(v,f)       (((v) & (1<<(f))) != 0)
#define GET_FLAGS(v,f,g)    (((v) & ((1<<(f)) | (1<<(g)))) != 0)
#define SET_FLAG(v,f)       ((v) |= (1<<(f)))
#define CLR_FLAG(v,f)       ((v) &= ~(1<<(f)))
#define CLR_FLAGS(v,f,g)    ((v) &= ~((1<<(f)) | (1<<(g))))

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
#ifdef NDEBUG
	#define LEN_BYTES(s) \
		strlen((char*)(s))
	#define COPY_BYTES(d,s,n) \
		strncpy((char*)(d), (char*)(s), (n))
	#define COMPARE_BYTES(l,r) \
		strcmp((char*)(l), (char*)(r))
	#define APPEND_BYTES_LIMIT(d,s,m) \
		strncat((char*)d, (char*)s, MAX((m) - strlen(d) - 1, 0))
#else
	// Debug build uses function stubs to ensure you pass in REBYTE *
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

//global pixelformat setup for REBOL image!, image loaders, color handling, tuple! conversions etc.
//the graphics compositor code should rely on this setting(and do specific conversions if needed)
//notes:
//TO_RGBA_COLOR always returns 32bit RGBA value, converts R,G,B,A components to native RGBA order
//TO_PIXEL_COLOR must match internal image! datatype byte order, converts R,G,B,A components to native image format
// C_R, C_G, C_B, C_A Maps color components to correct byte positions for image! datatype byte order

#ifdef ENDIAN_BIG

#define TO_RGBA_COLOR(r,g,b,a) (REBCNT)((r)<<24 | (g)<<16 | (b)<<8 |  (a))

//ARGB pixelformat used on big endian systems
#define C_A 0
#define C_R 1
#define C_G 2
#define C_B 3

#define TO_PIXEL_COLOR(r,g,b,a) (REBCNT)((a)<<24 | (r)<<16 | (g)<<8 |  (b))

#else

#define TO_RGBA_COLOR(r,g,b,a) (REBCNT)((a)<<24 | (b)<<16 | (g)<<8 |  (r))

//we use RGBA pixelformat on Android
#ifdef TO_ANDROID_ARM
#define C_R 0
#define C_G 1
#define C_B 2
#define C_A 3
#define TO_PIXEL_COLOR(r,g,b,a) (REBCNT)((a)<<24 | (b)<<16 | (g)<<8 |  (r))
#else
//BGRA pixelformat is used on Windows
#define C_B 0
#define C_G 1
#define C_R 2
#define C_A 3
#define TO_PIXEL_COLOR(r,g,b,a) (REBCNT)((a)<<24 | (r)<<16 | (g)<<8 |  (b))
#endif

#endif
