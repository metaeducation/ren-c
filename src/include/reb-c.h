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
typedef u16				REBUNI;     // unicode char

// REBCHR - only to refer to OS char strings (not internal strings)
#ifdef OS_WIDE_CHAR
typedef REBUNI          REBCHR;
#else
typedef REBYTE          REBCHR;
#endif

#define MAX_UNI ((1 << (8*sizeof(REBUNI))) - 1)

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
#define INT_TO_STR(n,s) _i64toa(n, s, 10)
#else
#define INT_TO_STR(n,s) Form_Int_Len(s, n, MAX_INT_LEN)
#endif

#ifdef ATOI64           // Ascii to integer conversion
#define CHR_TO_INT(s)   _atoi64(s)
#else
#define CHR_TO_INT(s)   strtoll(s, 0, 10)
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

// Memory related functions:
#define MAKE_MEM(n)     malloc(n)
#define MAKE_NEW(s)     malloc(sizeof(s))
#define FREE_MEM(m)     free(m)
#define CLEAR(m, s)     memset((void*)(m), 0, s);
#define CLEARS(m)       memset((void*)(m), 0, sizeof(*m));
#define COPY_MEM(t,f,l) memcpy((void*)(t), (void*)(f), l)
#define MOVE_MEM(t,f,l) memmove((void*)(t), (void*)(f), l)

// Byte string functions:
#define COPY_BYTES(t,f,l)   strncpy((char*)t, (char*)f, l)
// For APPEND_BYTES, l is the max-size allocated for t (dest)
#define APPEND_BYTES(t,f,l) strncat((char*)t, (char*)f, MAX((l)-strlen(t)-1, 0))
#define LEN_BYTES(s)        strlen((char*)s)
#define CMP_BYTES(s,t)      strcmp((char*)s, (char*)t)
#define BYTES(s) (REBYTE*)(s)

// OS has wide char string interfaces:
#ifdef OS_WIDE_CHAR
#define OS_WIDE TRUE
#define TXT(s) (L##s)
#define COPY_STR(t,f,l) wcsncpy(t, f, l)
#define JOIN_STR(d,s,l) wcsncat(d,s,l)
#define FIND_STR(d,s)   wcsstr(d,s)
#define FIND_CHR(d,s)   wcschr(d,s)
#define LEN_STR(s)      wcslen(s)
#define TO_OS_STR(s1,s2,l)   mbstowcs(s1,s2,l)
#define FROM_OS_STR(s1,s2,l) wcstombs(s1,s2,l)
#else
// OS has UTF-8 byte string interfaces:
#define OS_WIDE FALSE
#define TXT(s) (s)
#define COPY_STR(t,f,l) strncpy(t, f, l)
#define JOIN_STR(d,s,l) strncat(d,s,l)
#define FIND_STR(d,s)   strstr(d,s)
#define FIND_CHR(d,s)   strchr(d,s)
#define LEN_STR(s)      strlen(s)
#define TO_OS_STR(s1,s2,l)   strncpy(s1,s2,l)
#define FROM_OS_STR(s1,s2,l) strncpy(s1,s2,l)
#endif

#define MAKE_STR(n) (REBCHR*)(malloc((n) * sizeof(REBCHR)))  // OS chars!

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
