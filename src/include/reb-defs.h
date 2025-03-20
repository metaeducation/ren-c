//
//  File: %reb-defs.h
//  Summary: "Miscellaneous structures and definitions"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// These are the forward declarations of datatypes used by %tmp-internals.h
// (the internal Rebol API).  They must be at least mentioned before that file
// will be able to compile, after which the structures are defined in order.
//
// Shouldn't depend on other include files before it (besides %c-enhanced.h)
//


//=//// 8-BIT UNSIGNED BYTE ///////////////////////////////////////////////=//
//
// Using unsigned characters helps convey information is not limited to
// textual data.  API-wise, ordinary `char`--marked neither signed nor
// unsigned--is used for UTF-8 text.  But internally Byte is used for UTF-8
// when encoding or decoding.
//
// Note: uint8_t may not be equivalent to unsigned char:
// https://stackoverflow.com/a/16138470/211160
//
// This is a macro, to make it possible to undefine out of the way for things
// that define it (most likely to a synonym, as zlib.h does)
//
#define Byte unsigned char  // don't change to uint8_t, see note


//=//// 2-BIT UNSIGNED TYPE ///////////////////////////////////////////////=//
//
// This is called a "crumb".  We could check it more rigorously in C++ to
// be sure it's only initialized with a value between 0 and 3, but for now
// it is just documentation.
//
#define Crumb unsigned char


//=//// FLAGS TYPE ////////////////////////////////////////////////////////=//
//
// !!! Originally the Flags type was a `uint_fast32_t`.  However, there were
// several cases of the type being used with macros that only work with
// platform sized ints.  But really, uintptr_t and uint_fast32_t are likely
// the same type on most platforms anyway.
//
typedef uintptr_t Flags;


//=//// REBOL NUMERIC TYPES ("REBXXX") ////////////////////////////////////=//
//
// The 64-bit build modifications to R3-Alpha after its open sourcing changed
// *pointers* internal to data structures to be 64-bit.  But indexes did not
// get changed to 64-bit: REBINT and REBLEN remained 32-bit.
//
// This meant there was often extra space in the structures used on 64-bit
// machines, and a possible loss of performance for forcing a platform to use
// a specific size int (instead of deferring to C's generic `int`).
//
// Hence Ren-C switches to using indexes that are provided by <stdint.h>
// that are deemed by the compiler to be the fastest representation for 32-bit
// integers...even if that might be larger.
//
typedef intptr_t REBINT; // series index, signed, at *least* 32 bits
typedef intptr_t REBIDX; // series index, signed, at *least* 32 bits
typedef intptr_t REBLEN; // series length, unsigned, at *least* 32 bits

// !!! These values are an attempt to differentiate 0-based indexing from
// 1-based indexing, and try to be type-incompatible.
//
typedef intptr_t Offset;
typedef intptr_t Length;
typedef intptr_t Count;

// Bjarne Stroustrup himself believes size_t being unsigned is a mistake
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1428r0.pdf
//
typedef intptr_t Size;  // Size (in bytes)
#define Size_Of(x) u_cast(Size, sizeof(x))

// For comparisons of mixed signage, prefer casting to signed just because
// signed is our status quo.
//
// !!! Should this be folded in as the behavior of `cast(intptr_t, u)`?
// For now, having it be "weird" calls out that you really should be working
// in signed integers where possible.
//
#if CPLUSPLUS_11 && RUNTIME_CHECKS
    INLINE intptr_t Cast_Signed(uintptr_t u) {
        assert(u <= INTPTR_MAX);
        return u;
    }
#else
    #define Cast_Signed(u) cast(intptr_t, (u))
#endif

typedef int64_t REBI64; // 64 bit integer
typedef uint64_t REBU64; // 64 bit unsigned integer
typedef float REBD32; // 32 bit decimal
typedef double REBDEC; // 64 bit decimal
typedef uintptr_t LineNumber; // type used to store line numbers in Rebol files

typedef uint64_t Tick;  // evaluator cycles; unsigned overflow is well defined


// https://github.com/LambdaSchool/CS-Wiki/wiki/Casting-Signed-to-Unsigned-in-C
//
#define NOT_FOUND (-1)

// R3-Alpha cast -1 to a a very large integer for limits and used that as a
// signal.  That is error prone, but we can't use Optional(Length) because
// the optional trick relies on making 0 the optional state...and 0 is a valid
// in-band value.
//
// So instead Ren-C uses Optional(Length*) instead...with nullptr serving as
// the special "no limit" state.  Debug builds then check that you don't
// accidentally use the limit value when it is null (see Optional's `unwrap`)
//
#define UNLIMITED cast(const intptr_t*, nullptr)


// !!! Review this choice from R3-Alpha:
//
// https://stackoverflow.com/q/1153548/
//
#define MIN_D64 ((double)-9.2233720368547758e18)
#define MAX_D64 ((double) 9.2233720368547758e18)


//=//// 1-BASED INDEX TYPE ////////////////////////////////////////////////=//
//
// The Index type is not allowed to be 0 unless it is an Optional(Index).
//
// 1. Due to the fact that Optional(Index) is just `Index` when not using the
//    CHECK_OPTIONAL_TYPEMACRO switch, we cannot enforce Index's "never 0"
//    property without that switch.

#if (! CHECK_OPTIONAL_TYPEMACRO)
    typedef intptr_t Index;
    #define Index_To_Offset(i) ((i) - 1)
    #define Offset_To_Index(i) ((i) + 1)
#else
    struct Index {
        intptr_t value;
        Index(intptr_t i) : value {i}  // explicit would be too painful
          { assert(i != 0); }  // can't do unless CHECK_OPTIONAL_TYPEMACRO [1]

        operator intptr_t() const
          { return value; }

        Index& operator++()  // prefix
          { ++value; return *this; }
        Index& operator--()  // prefix
          { --value; return *this; }
        Index operator++(int)  // postfix
          { Index temp = *this; ++value; return temp; }
        Index operator--(int)  // postifx
          { Index temp = *this; --value; return temp; }
    };

    INLINE Offset Index_To_Offset(Index i)
     { return i.value - 1; }

    INLINE Index Offset_To_Index(Offset o)
     { return Index {o + 1}; }

  #if CHECK_OPTIONAL_TYPEMACRO
    template<>
    struct OptionWrapper<Index> {  // bypass the 0 assert
        intptr_t wrapped;
        OptionWrapper(intptr_t i) : wrapped {i} {}  // no assert

        explicit operator bool() {
           // explicit exception in if https://stackoverflow.com/q/39995573/
           return wrapped ? true : false;
        }
    };

    INLINE uintptr_t operator<<(  // see definition of Option() for explanation
        const UnwrapHelper& left,
        const OptionWrapper<Index>& option
    ){
        UNUSED(left);
        assert(option.wrapped);  // non-0 check
        return option.wrapped;
    }

    INLINE uintptr_t operator<<(  // see definition of Option() for explanation
        const MaybeHelper& left,
        const OptionWrapper<Index>& option
    ){
        UNUSED(left);
        return option.wrapped;
    }
  #endif
#endif


//=//// UNICODE CODEPOINT /////////////////////////////////////////////////=//
//
// We use the <stdint.h> fast 32 bit unsigned for Codepoint, as it doesn't need
// to be a standardized size (not persisted in files, etc.)
//
// !!! Choosing an unsigned type seems to be what most people do, but it
// creates problems.  e.g. in the sorting code there was:
//
//      REBINT d = c1 - c2;  // c1 and c2 are codepoints
//      if (d != 0)
//         return d > 0 ? 1 : -1;
//
// When c2 > c1 the unsigned subtraction creates a large number...not a
// negative one.  It may be worth it to switch to signed values, but people
// definitely do use unsigned ones most of the time.
//
typedef uint_fast32_t Codepoint;


//=//// BOOKMARKS (codepoint position caches for UTF-8) ///////////////////=//

struct BookmarkStruct {
    REBLEN index;
    Size offset;
};
typedef struct BookmarkStruct Bookmark;


//=//// BINDING ///////////////////////////////////////////////////////////=//

struct BinderStruct;
typedef struct BinderStruct Binder;

struct CollectorStruct;
typedef struct CollectorStruct Collector;

typedef struct JumpStruct Jump;


//=//// DATA STACK ////////////////////////////////////////////////////////=//

typedef intptr_t StackIndex;  // 0 for empty stack ([0] entry poison)


//=//// Need(), Sink(), Init() Aliases ////////////////////////////////////=//
//
// If you globally declare a macro with a name like "Init" before you include
// all the include files you want, then that could cause problems if there's
// something in the system header files called Init (for instance).  We wait
// to define the core shorthands for these type macros until after all the
// including is done.

#define Need NeedTypemacro
#define Sink SinkTypemacro
#define Init InitTypemacro


//=//// STRING MODES //////////////////////////////////////////////////////=//
//
// Ren-C is prescriptive about disallowing 0 bytes in strings to more safely
// use the rebSpell() API, which only returns a pointer and must interoperate
// with C.  It enforces the use of BLOB! if you want to embed 0 bytes (and
// using the rebBytes() API, which always returns a size.)
//
// Additionally, it tries to build on Rebol's historical concept of unifying
// strings within the system to use LF-only.  But rather than try "magic" to
// filter out CR LF sequences (and "magically" put them back later), it adds
// in speedbumps to try and stop CR from casually getting into strings.  Then
// it encourages active involvement at the source level with functions like
// ENLINE and DELINE when a circumstance can't be solved by standardizing the
// data sources themselves:
//
// https://forum.rebol.info/t/1264
//
// Note: These policies may over time extend to adding more speedbumps for
// other invisibles, e.g. choosing prescriptivisim about tab vs. space also.
//

enum Reb_Strmode {
    STRMODE_ALL_CODEPOINTS,  // all codepoints allowed but 0
    STRMODE_NO_CR,  // carriage returns not legal
    STRMODE_CRLF_TO_LF,  // convert CR LF to LF (error on isolated CR or LF)
    STRMODE_LF_TO_CRLF  // convert plain LF to CR LF (error on stray CR)
};


//=//// MOLDING ///////////////////////////////////////////////////////////=//
//
struct MolderStruct;
typedef struct MolderStruct Molder;


//=//// VARIADIC OPERATIONS ///////////////////////////////////////////////=//
//
// These 3 operations are the current legal set of what can be done with a
// VARARG!.  They integrate with Eval_Core()'s limitations in the prefetch
// evaluator--such as to having one unit of lookahead.
//
// While it might seem natural for this to live in %sys-varargs.h, the enum
// type is used by a function prototype in %tmp-internals.h...hence it must be
// defined before that is included.
//
enum Reb_Vararg_Op {
    VARARG_OP_TAIL_Q, // tail?
    VARARG_OP_FIRST, // "lookahead"
    VARARG_OP_TAKE // doesn't modify underlying data stream--advances index
};

struct LevelStruct;
typedef struct LevelStruct Level;

// C function implementing a native ACTION!
//
typedef Bounce (Executor)(Level* level_);
typedef Executor Dispatcher;  // sub-dispatched in Action_Executor()

// There are up to 255 built-in type predicates, which can be efficiently
// referred to by byte and tested quickly.  The acceleration either involves
// testing for whether something is a member of up to 31 typesets in the
// g_sparse_memberships[] table, or a range of Kind values.
//
// These aliases are used to help find places that use the optimization.
//
typedef Byte TypesetByte;
typedef Flags TypesetFlags;  // Ranged or sparse bitset of typesets

typedef Byte WildTwo[2];


//=//// INFIX MODES ///////////////////////////////////////////////////////=//
//
// * PREFIX_0 - this is not infix at all... so standard prefix.
//
// * INFIX_TIGHT - this corresponds to the traditional idea where infix
//   will run greedily as part of the same evaluation step as the thing to
//   its left:
//
//       >> add 1 2 * 3  ; multiply runs greedily when it sees the 2
//       == 7
//
//   Despite that greediness, an already in progress infix operation will be
//   allowed to complete before another starts:
//
//       >> 1 + 2 * 3  ; plus sets FEED_FLAG_NO_LOOKAHEAD, suppresses multiply
//       == 9
//
// * INFIX_DEFER - this mode of infix doesn't run immediately the first
//   time it can after an evaluation on the left, but it runs a step after.
//   This is how things like (all [...] then [...]) allow the THEN to see
//   the result of the ALL, instead of acting as (all ([...] then [...]))
//   as traditional greedy infix would.
//
// * INFIX_POSTPONE - postponing causes everything on the left of an
//   operator to run before it will.  Like a deferring operator, it is only
//   allowed to appear after the last parameter of an expression except it
//   closes out *all* the parameters on the stack vs. just one.
//
enum InfixModeEnum {
    PREFIX_0 = 0,  // zero so you can test (not infix_mode) for prefix
    INFIX_TIGHT,  // "normal"? "immediate"?
    INFIX_DEFER,
    INFIX_POSTPONE,
    INFIX_MODE_MAX = INFIX_POSTPONE
};
typedef enum InfixModeEnum InfixMode;
STATIC_ASSERT(INFIX_MODE_MAX == 3);  // must fit in Crumb


typedef struct rebol_time_fields {
    REBLEN h;
    REBLEN m;
    REBLEN s;
    REBLEN n;
} REB_TIMEF;

#include "sys-deci.h"

enum act_modify_mask {
    AM_PART = 1 << 0,
    AM_SPLICE = 1 << 1,
    AM_LINE = 1 << 2
};

enum act_find_mask {
    AM_FIND_CASE = 1 << 1,
    AM_FIND_MATCH = 1 << 2
};

// Flags used for Protect functions
//
enum {
    PROT_SET = 1 << 0,
    PROT_DEEP = 1 << 1,
    PROT_HIDE = 1 << 2,
    PROT_WORD = 1 << 3,
    PROT_FREEZE = 1 << 4
};


// Modes allowed by Collect keys functions:
enum {
    COLLECT_ONLY_SET_WORDS = 0,
    COLLECT_ANY_WORD = 1 << 1,
    COLLECT_ANY_LIST_DEEP = 1 << 2,
    COLLECT_DEEP_BLOCKS = 1 << 3,  // only deep if BLOCK!
    COLLECT_DEEP_FENCES = 1 << 4,
    COLLECT_NO_DUP = 1 << 5,  // Do not allow dups during collection (for specs)
    COLLECT_TOLERATE_PREBOUND = 1 << 6  // don't error if set words prebound
};
typedef Flags CollectFlags;
