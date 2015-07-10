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
**  Author:  Carl Sassenrath, Ladislav Mecir, @HostileFork
**  Notes:
**      Various configuration defines (from reb-config.h):
**
**      HAS_LL_CONSTS - compiler allows 1234LL constants
**      ODD_INT_64 - old MSVC typedef for 64 bit int
**      OS_WIDE_CHAR - the OS uses wide chars (not UTF-8)
**
**	!!! Note: lengthy commentary in this include file added by
**	@HostileFork should be shortened and moved to wherever developer
**	documentation is agreed to be kept.
**
***********************************************************************/


/***********************************************************************
**
**	NARROWED (AND NOTICEABLE) CASTING MACROS
**
**		C has a single casting operator `(type)value`.  The severity
**		and pitfalls in using carte-blanche casts are VERY numerous;
**		with many C (and C++) programmers unaware of the precise rules
**		and consequences of how it can result in undefined behavior
**		or breaking optimized builds.  For instance, "strict aliasing":
**
**			http://dbp-consulting.com/tutorials/StrictAliasing.html
**
**		Despite being able to go very wrong and requiring heavy scrutiny,
**		C's cast operator can be hard to see among all the other uses of
**		parentheses.  They're hard to scan for visually, and a mechanical
**		search is hard as well.
**
**		For this reason, C++ separated C's universal casting operator
**		down into a handful of distinct operators.  Which cast you use
**		depends on exactly what you were trying to accomplish:
**
**			http://stackoverflow.com/questions/332030/
**
**		C++ casts were given long names to make them stand out.
**		One reason that this was considered acceptable is that in C++,
**		the use of casts is discouraged.  They are rarely necessary if a
**		design isn't having to do anything involving interfacing
**		with C code.
**
**		The macros below bring the benefits of granular casting to Rebol,
**		but without *requiring* it to be built with a C++ compiler.  The
**		shorthand of `cast(type, value)` can be used for a static_cast;
**		with r_cast for reinterpret_cast and c_cast for a const_cast.
**		Although they are preprocessor macros, being in lowercase seemed
**		appropriate and less disruptive.  (`assert()` is a macro, too...)
**
**		If someone is programming with a C compiler and doesn't know
**		in particular which cast to use, they can just use `cast()`
**		and someone doing a build as C++ can mark its proper category.
**
***********************************************************************/

#ifdef __cplusplus
	#define cast(t, v) \
		static_cast<t>(v)

	#define r_cast(t, v) \
		reinterpret_cast<t>(v)

	#define c_cast(t, v) \
		const_cast<t>(v)
#else
	#define cast(t, v) \
		((t)(v)) // "least dangerous" cast placeholder (static_cast)

	#define r_cast(t, v) \
		((t)(v)) // "r"einterpret_cast placeholder

	#define c_cast(t, v) \
		((t)(v)) // "c"onst_cast placeholder
#endif


/***********************************************************************
**
**	MEMORY ALLOCATION AND FREEING MACROS
**
**		Although C provides malloc() and free(), Rebol historically
**		did not trust these as providing adequate performance.  It
**		elected to do memory pooling... so it would do some large
**		block mallocs and then subdivide from that:
**
**			https://en.wikipedia.org/wiki/Memory_pool
**
**		Today's world has better allocators which can be substituted
**		for malloc() that do clever pooling under the hood.  `tcmalloc`
**		is popular:
**
**			http://jamesgolick.com/2013/5/19/how-tcmalloc-works.html
**
**		Important to note is that it would be naive to think that an
**		off-the-cuff implementation of a generalized memory pool
**		could perform better than well researched approaches, without
**		some very specific inside knowledge about the allocation
**		patterns:
**
**			http://goog-perftools.sourceforge.net/doc/tcmalloc.html
**
**		However, Rebol has two pieces of inside knowledge to exploit.
**		One is that it isn't multi-threaded for most intents and
**		purposes.  Hence a non-thread-safe allocator can be some
**		noticeable bit faster.  (Whether one sees this as an argument
**		for "just use malloc" or "TASK! wasn't meant to be anytime soon"
**		is the reader's choice.)
**
**		The second piece of knowledge that can be exploited is that
**		REBSER and REBGOB items are allocated more frequently than
**		other sizes, and it's also necessary for the garbage collector
**		to be able to enumerate through the list of allocated items.
**		With a custom pool, the management of blocks and freelists
**		can be tied in with that.
**
**		Hence for the moment, malloc() and free() are abstracted as
**		Alloc_Mem() and Free_Mem().  Of particular note is that
**		Free_Mem() insists you know the size of the memory you are
**		freeing, as it wants to be able to limit allocations and
**		know when memory is reaching a certain threshold to run
**		a garbage collection.
**
**		These macros help use Alloc_Mem and Free_Mem more safely,
**		and are modeled after C++'s new/delete and new[]/delete[].
**		Zero-filling helpers are provided to ease concern regarding
**		the loss of the old macros:
**
**			#define CLEAR(m,s) 	memset((m), 0, (s));
**			#define CLEARS(m)	memset((m), 0, sizeof(*m))
**
***********************************************************************/

#define ALLOC(t) \
	r_cast(t *, Alloc_Mem(sizeof(t)))

#define ALLOC_ZEROFILL(t) \
	r_cast(t *, memset(ALLOC(t), '\0', sizeof(t)))

#define ALLOC_ARRAY(t,n) \
	r_cast(t *, Alloc_Mem(sizeof(t) * (n)))

#define ALLOC_ARRAY_ZEROFILL(t,n) \
	r_cast(t *, memset(ALLOC_ARRAY(t, (n)), '\0', sizeof(t) * (n)))

#if defined(__cplusplus) && __cplusplus >= 201103L
	// In C++11, decltype lets us do a bit more sanity checking that the
	// C-oriented API is actually freeing the data type it thinks that it is

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


/***********************************************************************
**
**  "C CODE TYPES"
**
**		Rebol was first released in 1997, when the C language had nothing
**		in the way of guarantees regarding stable sizes for datatypes.
**		It also sought to compile on a large number of platforms,
**		while requiring precise control over memory layout.  Even as
**		years passed and other advancements were made in C and C++,
**		nothing happened on this front:
**      	
**			"One of the biggest flaws in the C language was not
**			 to indicate bitranges of integers.  You cannot
**			 'abstractly remove' the range of a number.  It is a
**			 critical part of its definition."
**				-- Carl Sassenrath
**				(comment from reb-c.h, in the 12-Dec-2012 open-sourcing)
**
**		It took a long long time (pun intended) but C99 eventually came
**		along with <stdint.h> to address the issue.  That is essentially
**		the same answer Rebol had, so read the documentation for that to
**		understand the below:
**
**			http://en.cppreference.com/w/c/types/integer
**
**			http://stackoverflow.com/questions/9834747/
**
**		So if <stdint.h> is available under a C99 compiler (or above),
**		the definitions are used.  Yet Rebol seeks to continue to build
**		and run on numerous legacy systems, in addition to modern ones.
**		Hence it has fallbacks to make the definitions it needs if 
**		there are no standard definitions.
**
**		!!! Note: One thing that's not entirely clear is that if the
**		definitions are standard and the same size, why wouldn't Rebol
**		use the same names (uint8_t instead of redefining that to u8?)
**		Is that just a product of predating stdint?  Is the
**		abbreviation worth it any longer?
**
***********************************************************************/

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
	// use C99 definitions from <stdint.h>

	#include <stdint.h>

	typedef int8_t i8;
	#define I8_C(c) INT8_C(c)

	typedef uint8_t u8;
	#define U8_C(c)	UINT8_C(c)

	typedef int16_t i16;
	#define I16_C(c) INT16_C(c)

	typedef uint16_t u16;
	#define U16_C(c) UINT16_C(c)
	
	typedef int32_t i32;
	#define I32_C(c) INT32_C(c)
	#define U32_C(c) UINT32_C(c)
	#define MAX_I32 INT32_MAX
	#define MIN_I32 INT32_MIN

	typedef uint32_t u32;

	typedef int64_t i64;
	#define I64_C(c) INT64_C(c)
	#define MAX_I64 INT64_MAX
	#define MIN_I64 INT64_MIN

	typedef uint64_t u64;
	#define U64_C(c) UINT64_C(c)

	// REBIPT is the integral counterpart of void*
	// REBUPT is the unsigned counterpart of void*

	typedef intptr_t REBIPT;
	typedef uintptr_t REBUPT;
#else
	// C99 definitions unavailable, do it ourselves

	typedef char i8;
	#define I8(c) c

	typedef unsigned char u8;
	#define U8(c) c

	typedef short i16;

	typedef unsigned short u16;

	#ifdef __LP64__
		typedef int	i32;
	#else
		typedef long i32;
	#endif
	#define I32_C(c) c
	#define MAX_I32 I32_C(0x7fffffff)
	#define MIN_I32 cast(i32, I32_C(0x80000000)) // unsigned if no cast

	#ifdef __LP64__
		typedef unsigned int u32;
	#else
		typedef unsigned long u32;
	#endif
	#define U32_C(c) c ## U

	#ifdef ODD_INT_64
		// Windows VC6 nonstandard typing for 64 bits
		typedef _int64 i64;
		#define I64_C(c) c ## I64

		typedef unsigned _int64 u64;
		#define U64_C(c) c ## U64
	#else
		typedef long long i64;
		#define I64_C(c) c ## LL

		typedef unsigned long long u64;
		#define U64_C(c) c ## ULL
	#endif

	#define MAX_I64 I64_C(0x7fffffffffffffff)
	#define MIN_I64 cast(i64, I64_C(0x8000000000000000)) // unsigned w/no cast

	// REBIPT => integral counterpart of void*
	// REBUPT => unsigned counterpart of void*
	#ifdef __LLP64__
		typedef long long REBIPT;		
		typedef unsigned long long REBUPT;
	#else
		typedef long REBIPT;
		typedef unsigned long REBUPT;
	#endif
#endif

#define MAX_U32 U32_C(0xffffffff)
#define MAX_U64 U64_C(0xffffffffffffffff)

#ifndef DEF_UINT
	// Only define `uint` if the system didn't already (some do)
	typedef unsigned int uint;
#endif

#ifndef HAS_BOOL
	// Some systems define a cpu-optimal BOOL already. It is assumed that
	// the R3 lib will use that same definition (so sizeof() is identical.)
	// (Of course, all of this should have been built into C in 1970.)
	// But we define it if we must, and use int for speed in modern CPUs.
	typedef int BOOL;
#endif

#ifndef FALSE
	#define FALSE 0
	#define TRUE (!0)
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

#define MAX_INT_LEN 20
#define MAX_HEX_LEN 16

// Integer to ascii conversion
#ifdef ITOA64
	#define INT_TO_STR(n,s) _i64toa(n, s, 10)
#else
	#define INT_TO_STR(n,s) Form_Int_Len(s, n, MAX_INT_LEN)
#endif

// Ascii to integer conversion
#ifdef ATOI64
	#define CHR_TO_INT(s) _atoi64(s)
#else
	#define CHR_TO_INT(s) strtoll(s, 0, 10)
#endif

#define LDIV lldiv
#define LDIV_T lldiv_t


/***********************************************************************
**
**  ADDRESS AND FUNCTION POINTERS
**
**		Note that you *CANNOT* cast something like a `void *` to
**		(or from) a function pointer.  Pointers to functions are not
**		guaranteed to be the same size as a data pointer, in either C
**		or C++.
**
**		To give an example of how much freedom the compiler has: it
**		*might* count the total number of functions in your program...
**		and make a function "pointer" just a byte...that it looks up in
**		a table to dispatch!
**
**			http://stackoverflow.com/questions/3941793/
**
**		So if you want something to hold either a function pointer or
**		a data pointer, you have to implement that as a union...and
**		know what you're doing when writing and reading it.
**
**		!!! That explains why there is a need for a CFUNC type of some
**		kind as the "analogue of void* for data".  It doesn't explain
**		what FUNCPTR is for or why it is here, but it apparently
**		relates to something about how Rebol implemented ROUTINE!.
**		Here's some notes on __cdecl vs. __stdcall on Windows:
**
**			http://stackoverflow.com/questions/3404372/
**
***********************************************************************/

#ifdef TO_WIN32
	typedef long (__stdcall *FUNCPTR)();
	typedef void (__cdecl *CFUNC)(void *);
#else
	typedef long (*FUNCPTR)();
	typedef void (*CFUNC)(void *);
#endif


/***********************************************************************
**
**  "REBOL CODE TYPES"
**
**		After doing all the work on defining a standard interface
**		for types and sizes, it might seem a bit weird to create
**		*another* level of indirection macros for naming them again...
**
**		@HostileFork reasons there are three likely motivations:
**
**		The first motivation is that Rebol C code and APIs have a lot
**		of types in all caps, with lowercase variable and parameter
**		names.  The interface looks more consistent when it's all
**		caps, and lowercase types break the rhythm.
**
**		There's also the chance to give semantic meaning to the same
**		type if its used in different ways.  So a REBCNT can convey
**		specifically the notion that it is used as a count (in the
**		spirit of something like C's `size_t`), which simply using
**		a `u32` might not convey.
**
**		Finally: putting REB in front of the name creates a kind of
**		namespacing for APIs that do not include reb-c.h (such as
**		hostkit, in theory).  Other C programs might have a different
**		definition for BOOL in terms of behavior and bit size.  The
**		REB names are unlikely to overlap.
**
***********************************************************************/

// 8-bit flag (for struct usage)
typedef i8 REBOOL;

// 32-bit flag (for cpu efficiency)
typedef u32 REBFLG;

// 32-bit signed integer (default for indexes))
typedef i32 REBI32;
typedef i32 REBINT;

// 32-bit unsigned integer (default for counting numbers)
// !!! Rename to REBLEN?
typedef u32 REBU32;
typedef u32 REBCNT;

// 64-bit signed integer
typedef i64 REBI64;

// 64-bit unsigned integer
typedef u64 REBU64;

// 32-bit decimal type
// !!! How do you know it's 32-bit? Should sanity check it:
//     http://stackoverflow.com/questions/752309/
typedef float REBD32;

// 64-bit decimal type
// !!! Beyond the note above about sanity checking the size, REBD64 would be
// a more consistent name.  But DECIMAL! is being renamed to FLOAT! in Red.
// So perhaps it should be REBF32/REBF64?
typedef double REBDEC; 
#define MIN_D64 cast(double, -9.2233720368547758e18)
#define MAX_D64 cast(double, 9.2233720368547758e18)


/***********************************************************************
**
**  DEFINITIONS FOR `char` vs. `REBYTE`
**
**		One thing in C that was always known and set in stone is that
**		`sizeof(signed char) == 1` and `sizeof(unsigned char) == 1`.
**		Yet even with this seeming anchor there is a catch: there's
**		no guarantee on whether a plain `char` is signed or unsigned.
**		It has to do with performance on certain architectures:
**
**			http://stackoverflow.com/questions/914242/
**
**		So in the Rebol codebase, REBYTE was standardized to be an
**		unsigned char, and used instead of the "less-reliable" `char`.
**		It also tried to redefine macros as wrappers over functions
**		like strlen() and strchr(), which was a type-unsafe subset
**		of <string.h> that looked like:
**
**			#define COPY_BYTES(t,f,l) strncpy((char*)t, (char*)f, l)
**			#define LEN_BYTES(s) strlen((char*)s)
**
**		Because when you enable -Wpointer-sign in C (or any time in
**		C++) you have type incompatibilities and cannot write:
**
**			const REBYTE *data = "some text"; 
**
**		A better idea emerged to use the incompatibility as a feature.
**		When data might be encoded in a form that a string function like
**		strlen() wouldn't make sense on, it would use REBYTE...for instance
**		UTF-8 encoded data.  But make it easy to switch:
**
**			strlen(rebyte_ptr) => strlen(s_cast(rebyte_ptr))
**			Utf8_Func(char_ptr) => Utf8_Func(b_cast(char_ptr))
**
**		These are "string cast" and "byte cast" (not to confuse s_cast
**		C++'s static_cast...and you can't because it's single arity.)
**
**		In the release builds these are simply casts.  But the
**		Debug build uses functions that specifically only let you go
**		from char* to REBYTE* and back.  There are also const variants
**		cb_cast() and cs_cast() which require both const ins and
**		outs.
**
**		The previous name for macros serving this purpose was TXT and
**		bytes, but they were specifically framed as casts because--
**		like a cast--they show a point that should call attention.
**		Noticing the points of conversion is a good place to ask if
**		you can really be sure a REBYTE* can have its strlen() taken
**		or not...
**
***********************************************************************/

typedef unsigned char REBYTE;

#ifdef NDEBUG
	// Release build uses reinterpret casts
	// So no error on b_cast(42157) etc.
	#define b_cast(s)		r_cast(REBYTE *, (s))
	#define cb_cast(s)		r_cast(const REBYTE *, (s))
	#define s_cast(s)		r_cast(char *, (s))
	#define cs_cast(s)		r_cast(const char *, (s))
#else
	REBYTE *b_cast(char *);
	const REBYTE *cb_cast(const char *);
	char *s_cast(REBYTE *);
	const char *cs_cast(const REBYTE *);
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
**		For contrast, see Red's Unicode strategy which would go further
**		to UCS-4:
**
**			http://www.red-lang.org/2012/09/plan-for-unicode-support.html
**
**		There's a lot that could be said, but a section is made just for
**		this...to point out that a REBUNI is guaranteed to be 2 bytes.
** 
***********************************************************************/

typedef u16 REBUNI;

#define MAX_UNI ((1 << (8 * sizeof(REBUNI))) - 1)


/***********************************************************************
**
**  "USEFUL MACROS AND HELPERS"
**
**		These should probably be in another file, left here for the
**		current moment.
**
***********************************************************************/
// Used for MOLDing:
#define MAX_DIGITS 17   // number of digits
#define MAX_NUMCHR 32   // space for digits and -.e+000%

// These cannot be "const char" and be "constant" in C lingo:
//
// 		http://stackoverflow.com/questions/3025050/

#define NUL 0 	// Helps avoid confusing '\0' and '0'
#define BEL 7
#define BS 8
#define LF 10
#define CR 13
#define ESC 27
#define DEL 127

#define FLAGIT(f)           (1 << (f))
#define GET_FLAG(v,f)       (((v) & (1 << (f))) != 0)
#define GET_FLAGS(v,f,g)    (((v) & ((1 << (f)) | (1 << (g)))) != 0)
#define SET_FLAG(v,f)       ((v) |= (1 << (f)))
#define CLR_FLAG(v,f)       ((v) &= ~(1 << (f)))
#define CLR_FLAGS(v,f,g)    ((v) &= ~((1 << (f)) | (1 << (g))))

#ifdef min
	#define MIN(a,b) 		min((a),(b))
	#define MAX(a,b) 		max((a),(b))
#else
	// !!! Semantically this has the classic problem that if there are any
	// side-effects of evaluating a or b, those end up being evaluated twice.
	#define MIN(a,b) 		(((a) < (b)) ? (a) : (b))
	#define MAX(a,b) 		(((a) > (b)) ? (a) : (b))
#endif

#define ROUND_TO_INT(d)	cast(REBINT, floor((d) + 0.5))

#if defined(__clang__) || defined (__GNUC__)
# define ATTRIBUTE_NO_SANITIZE_ADDRESS __attribute__((no_sanitize_address))
#else
# define ATTRIBUTE_NO_SANITIZE_ADDRESS
#endif

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
