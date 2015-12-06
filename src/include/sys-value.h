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
**  Summary: Value and Related Definitions
**  Module:  sys-value.h
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#ifndef VALUE_H
#define VALUE_H

/***********************************************************************
**
**  REBOL Value Type
**
**      This is used for all REBOL values. This is a forward
**      declaration. See end of this file for actual structure.
**
***********************************************************************/

#pragma pack(4)

// Note: b-init.c verifies that lower 8 bits is flags.opts, so that testing
// for an IS_END is merely testing for an even value of the header.
//
union Reb_Value_Flags {
    struct {
    #ifdef ENDIAN_LITTLE
        unsigned opts:8;    // special options
        unsigned type:8;    // datatype
        unsigned exts:8;    // extensions to datatype
        unsigned resv:8;    // reserved for future
    #else
        unsigned resv:8;    // reserved for future
        unsigned exts:8;    // extensions to datatype
        unsigned type:8;    // datatype
        unsigned opts:8;    // special options
    #endif
    } bitfields;

    REBCNT all;             // for setting all the flags at once
};

struct Reb_Value;
typedef struct Reb_Value REBVAL;
typedef struct Reb_Series REBSER;

// Value type identifier (generally, should be handled as integer):

// get and set only the type (not flags)

#ifdef NDEBUG
    #define VAL_TYPE(v)     cast(enum Reb_Kind, (v)->flags.bitfields.type)
#else
    // We want to be assured that we are not trying to take the type of a
    // value that is actually an END marker, because end markers chew out only
    // one bit--the rest is allowed to be anything (a pointer value, etc.)
    //
    #define VAL_TYPE(v)     VAL_TYPE_Debug(v)
#endif

#define SET_TYPE(v,t)   ((v)->flags.bitfields.type = (t))

// set type, clear all flags except for NOT_END
//
#define VAL_SET(v,t) \
    ((v)->flags.all = (1 << OPT_VALUE_NOT_END), \
     (v)->flags.bitfields.type = (t))

// !!! Questionable idea: does setting all bytes to zero of a type
// and then poking in a type indicator make the "zero valued"
// version of that type that you can compare against?  :-/
#define VAL_SET_ZEROED(v,t) (CLEAR((v), sizeof(REBVAL)), VAL_SET((v),(t)))

// Setting the END is optimized.  It is possible to signal an END via setting
// the low bit of any 32/64-bit value, including to add the bit to a pointer,
// but this routine will wipe all the other bits and just set it to 1.
//
#define SET_END(v)          ((v)->flags.all = 0)
#define IS_END(v)           ((v)->flags.all % 2 == 0)
#define NOT_END(v)          ((v)->flags.all % 2 == 1)
#define END_VALUE           PG_End_Val

// Value option flags:
enum {
    OPT_VALUE_NOT_END = 0,  // Not an END signal (so other header bits valid)
    OPT_VALUE_FALSE,        // Value is conditionally false (optimization)
    OPT_VALUE_LINE,         // Line break occurs before this value
    OPT_VALUE_THROWN,       // Value is /NAME of a THROW (arg via THROWN_ARG)
    OPT_VALUE_MAX
};

#define VAL_OPTS_DATA(v)    ((v)->flags.bitfields.opts)
#define VAL_SET_OPT(v,n)    SET_FLAG(VAL_OPTS_DATA(v), n)
#define VAL_GET_OPT(v,n)    GET_FLAG(VAL_OPTS_DATA(v), n)
#define VAL_CLR_OPT(v,n)    CLR_FLAG(VAL_OPTS_DATA(v), n)

// Used for 8 datatype-dependent flags (or one byte-sized data value)
#define VAL_EXTS_DATA(v)    ((v)->flags.bitfields.exts)
#define VAL_SET_EXT(v,n)    SET_FLAG(VAL_EXTS_DATA(v), n)
#define VAL_GET_EXT(v,n)    GET_FLAG(VAL_EXTS_DATA(v), n)
#define VAL_CLR_EXT(v,n)    CLR_FLAG(VAL_EXTS_DATA(v), n)

// All THROWN values have two parts: the REBVAL arg being thrown and
// a REBVAL indicating the /NAME of a labeled throw.  (If the throw was
// created with plain THROW instead of THROW/NAME then its name is NONE!).
// You cannot fit both values into a single value's bits of course, but
// since only one THROWN() value is supposed to exist on the stack at a
// time the arg part is stored off to the side when one is produced
// during an evaluation.  It must be processed before another evaluation
// is performed, and if the GC or DO are ever given a value with a
// THROWN() bit they will assert!
//
// A reason to favor the name as "the main part" is that having the name
// value ready-at-hand allows easy testing of it to see if it needs
// to be passed on.  That happens more often than using the arg, which
// will occur exactly once (when it is caught).

#ifdef NDEBUG
    #define CONVERT_NAME_TO_THROWN(name,arg) \
        do { \
            VAL_SET_OPT((name), OPT_VALUE_THROWN); \
            (TG_Thrown_Arg = *(arg)); \
        } while (0)

    #define CATCH_THROWN(arg,thrown) \
        do { \
            VAL_CLR_OPT((thrown), OPT_VALUE_THROWN); \
            (*(arg) = TG_Thrown_Arg); \
        } while (0)
#else
    #define CONVERT_NAME_TO_THROWN(n,a) \
        Convert_Name_To_Thrown_Debug(n, a)

    #define CATCH_THROWN(a,t) \
        Catch_Thrown_Debug(a, t)
#endif

#define THROWN(v)           (VAL_GET_OPT((v), OPT_VALUE_THROWN))


#define IS_SET(v)           (VAL_TYPE(v) > REB_UNSET)
#define IS_SCALAR(v)        (VAL_TYPE(v) <= REB_DATE)


/***********************************************************************
**
**  DATATYPE - Datatype or pseudo-datatype
**
**  !!! Consider rename to TYPE! once legacy TYPE? calls have been
**  converted to TYPE-OF.  Also consider a model where there are
**  user types, and hence TYPE? may be able to return more than just
**  one out of a set of 64 things.
**
***********************************************************************/

struct Reb_Datatype {
    enum Reb_Kind kind;
    REBSER  *spec;
//  REBINT  min_type;
//  REBINT  max_type;
};

#define VAL_TYPE_KIND(v)        ((v)->data.datatype.kind)
#define VAL_TYPE_SPEC(v)    ((v)->data.datatype.spec)

// %words.r is arranged so that symbols for types are at the start
// Although REB_TRASH is 0, the 0 REBCNT used for symbol IDs is reserved
// for "no symbol".  So there is no symbol for the "fake" type TRASH!
//
#define IS_KIND_SYM(s)      ((s) < REB_MAX + 1)
#define KIND_FROM_SYM(s)    cast(enum Reb_Kind, (s) - 1)
#define SYM_FROM_KIND(k)    cast(REBCNT, (k) + 1)
#define VAL_TYPE_SYM(v)     SYM_FROM_KIND((v)->data.datatype.kind)

//#define   VAL_MIN_TYPE(v) ((v)->data.datatype.min_type)
//#define   VAL_MAX_TYPE(v) ((v)->data.datatype.max_type)


/***********************************************************************
**
**  TRASH - Trash Value used in debugging cases where a cell is expected to
**  be overwritten.  By default, the garbage collector will raise an alert
**  if a trash value is not overwritten by the time it sees it...but there
**  are some uses of trash which the GC should be able to run while it is
**  extant.  For these cases, use SET_TRASH_SAFE.
**
**  The operations for setting trash are available in both debug and release
**  builds.  An unsafe trash set turns into a NOOP in release builds, while
**  a safe trash set turns into a SET_UNSET().  IS_TRASH_DEBUG() can be used
**  to test for trash in debug builds, but not in release builds.
**
**  Because the trash value saves the filename and line where it originated,
**  the REBVAL has that info in debug builds to inspect.
**
***********************************************************************/

#ifdef NDEBUG
    #define SET_TRASH_IF_DEBUG(v) NOOP

    #define SET_TRASH_SAFE(v) SET_UNSET(v)
#else
    enum {
        EXT_TRASH_SAFE = 0,     // GC safe trash (UNSET! in release build)
        EXT_TRASH_MAX
    };

    struct Reb_Trash {
        const char *filename;
        int line;
    };

    // Special type check...we don't want to use a VAL_TYPE() == REB_TRASH
    // because VAL_TYPE is supposed to assert on trash
    //
    #define IS_TRASH_DEBUG(v)         ((v)->flags.bitfields.type == REB_TRASH)

    #define SET_TRASH_IF_DEBUG(v) \
        ( \
            VAL_SET((v), REB_TRASH), \
            (v)->data.trash.filename = __FILE__, \
            (v)->data.trash.line = __LINE__, \
            cast(void, 0) \
        )

    #define SET_TRASH_SAFE(v) \
        ( \
            VAL_SET((v), REB_TRASH), \
            VAL_SET_EXT((v), EXT_TRASH_SAFE), \
            (v)->data.trash.filename = __FILE__, \
            (v)->data.trash.line = __LINE__, \
            cast(void, 0) \
        )
#endif


/***********************************************************************
**
**  NUMBERS - Integer and other simple scalars
**
***********************************************************************/

#define SET_UNSET(v)    VAL_SET(v, REB_UNSET)
#define UNSET_VALUE     ROOT_UNSET_VAL

#define SET_NONE(v) \
    ((v)->flags.all = 1 << OPT_VALUE_NOT_END | 1 << OPT_VALUE_FALSE, \
     (v)->flags.bitfields.type = REB_NONE)  // compound

#define NONE_VALUE      ROOT_NONE_VAL

// In legacy mode we still support the old convention that an IF that does
// not take its branch or a WHILE loop that never runs its body return a NONE!
// value instead of an UNSET!.  To track the locations where this decision is
// made more easily, SET_UNSET_UNLESS_LEGACY_NONE() is used.
//
#ifdef NDEBUG
    #define SET_UNSET_UNLESS_LEGACY_NONE(v) \
        SET_UNSET(v)
#else
    #define SET_UNSET_UNLESS_LEGACY_NONE(v) \
        (LEGACY(OPTIONS_NONE_INSTEAD_OF_UNSETS) ? SET_NONE(v) : SET_UNSET(v))
#endif

#define EMPTY_BLOCK     ROOT_EMPTY_BLOCK
#define EMPTY_ARRAY     VAL_SERIES(ROOT_EMPTY_BLOCK)

#define VAL_INT32(v)    (REBINT)((v)->data.integer)
#define VAL_INT64(v)    ((v)->data.integer)
#define VAL_UNT64(v)    ((v)->data.unteger)
#define SET_INTEGER(v,n) VAL_SET(v, REB_INTEGER), ((v)->data.integer) = (n)
#define SET_INT32(v,n)  ((v)->data.integer) = (REBINT)(n)

#define MAX_CHAR        0xffff
#define VAL_CHAR(v)     ((v)->data.character)
#define SET_CHAR(v,n) \
    (VAL_SET((v), REB_CHAR), VAL_CHAR(v) = (n), NOOP)

#define IS_NUMBER(v)    (VAL_TYPE(v) == REB_INTEGER || VAL_TYPE(v) == REB_DECIMAL)


/***********************************************************************
**
**  DECIMAL -- Implementation-wise, a 'double'-precision floating
**  point number in C (typically 64-bit).
**
***********************************************************************/

#define VAL_DECIMAL(v)  ((v)->data.decimal)
#define SET_DECIMAL(v,n) VAL_SET(v, REB_DECIMAL), VAL_DECIMAL(v) = (n)


/***********************************************************************
**
**  MONEY -- Includes denomination and amount
**
**  !!! The naming of "deci" used by MONEY! as "decimal" is a very
**  bad overlap with DECIMAL! and also not very descriptive of what
**  the properties of a "deci" are.  Also, to be a useful money
**  abstraction it should store the currency type, e.g. the three
**  character ISO 4217 code (~15 bits to store)
**
**      https://en.wikipedia.org/wiki/ISO_4217
**
***********************************************************************/

struct Reb_Money {
    deci amount;
};

#define VAL_MONEY_AMOUNT(v)     ((v)->data.money.amount)
#define SET_MONEY_AMOUNT(v,n) \
    (VAL_SET((v), REB_MONEY), VAL_MONEY_AMOUNT(v) = (n), NOOP)


/***********************************************************************
**
**  DATE and TIME
**
***********************************************************************/

typedef struct reb_ymdz {
#ifdef ENDIAN_LITTLE
    REBINT zone:7;  // +/-15:00 res: 0:15
    REBCNT day:5;
    REBCNT month:4;
    REBCNT year:16;
#else
    REBCNT year:16;
    REBCNT month:4;
    REBCNT day:5;
    REBINT zone:7;  // +/-15:00 res: 0:15
#endif
} REBYMD;

typedef union reb_date {
    REBYMD date;
    REBCNT bits;
} REBDAT;

struct Reb_Time {
    REBI64 time;    // time in nanoseconds
    REBDAT date;
};

#define VAL_TIME(v) ((v)->data.time.time)
#define TIME_SEC(n) ((REBI64)(n) * 1000000000L)

#define MAX_SECONDS (((i64)1<<31)-1)
#define MAX_HOUR    (MAX_SECONDS / 3600)
#define MAX_TIME    ((REBI64)MAX_HOUR * HR_SEC)

#define NANO        1.0e-9
#define SEC_SEC     ((REBI64)1000000000L)
#define MIN_SEC     (60 * SEC_SEC)
#define HR_SEC      (60 * 60 * SEC_SEC)

#define SEC_TIME(n)  ((n) * SEC_SEC)
#define MIN_TIME(n)  ((n) * MIN_SEC)
#define HOUR_TIME(n) ((n) * HR_SEC)

#define SECS_IN(n) ((n) / SEC_SEC)
#define VAL_SECS(n) (VAL_TIME(n) / SEC_SEC)

#define DEC_TO_SECS(n) (i64)(((n) + 5.0e-10) * SEC_SEC)

#define SECS_IN_DAY 86400
#define TIME_IN_DAY (SEC_TIME((i64)SECS_IN_DAY))

#define NO_TIME     MIN_I64

#define MAX_YEAR        0x3fff

#define VAL_DATE(v)     ((v)->data.time.date)
#define VAL_YEAR(v)     ((v)->data.time.date.date.year)
#define VAL_MONTH(v)    ((v)->data.time.date.date.month)
#define VAL_DAY(v)      ((v)->data.time.date.date.day)
#define VAL_ZONE(v)     ((v)->data.time.date.date.zone)

#define ZONE_MINS 15
#define ZONE_SECS (ZONE_MINS*60)
#define MAX_ZONE (15 * (60/ZONE_MINS))


/***********************************************************************
**
**  TUPLE
**
***********************************************************************/

typedef struct Reb_Tuple {
    REBYTE tuple[12];
} REBTUP;

#define VAL_TUPLE(v)    ((v)->data.tuple.tuple+1)
#define VAL_TUPLE_LEN(v) ((v)->data.tuple.tuple[0])
#define MAX_TUPLE 10


/***********************************************************************
**
**  PAIR
**
***********************************************************************/

#define VAL_PAIR(v)     ((v)->data.pair)
#define VAL_PAIR_X(v)   ((v)->data.pair.x)
#define VAL_PAIR_Y(v)   ((v)->data.pair.y)
#define SET_PAIR(v,x,y) (VAL_SET(v, REB_PAIR),VAL_PAIR_X(v)=(x),VAL_PAIR_Y(v)=(y))
#define VAL_PAIR_X_INT(v) ROUND_TO_INT((v)->data.pair.x)
#define VAL_PAIR_Y_INT(v) ROUND_TO_INT((v)->data.pair.y)


/***********************************************************************
**
**  EVENT
**
***********************************************************************/

#define VAL_EVENT_TYPE(v)   ((v)->data.event.type)  //(VAL_EVENT_INFO(v) & 0xff)
#define VAL_EVENT_FLAGS(v)  ((v)->data.event.flags) //((VAL_EVENT_INFO(v) >> 16) & 0xff)
#define VAL_EVENT_WIN(v)    ((v)->data.event.win)   //((VAL_EVENT_INFO(v) >> 24) & 0xff)
#define VAL_EVENT_MODEL(v)  ((v)->data.event.model)
#define VAL_EVENT_DATA(v)   ((v)->data.event.data)
#define VAL_EVENT_TIME(v)   ((v)->data.event.time)
#define VAL_EVENT_REQ(v)    ((v)->data.event.eventee.req)

// !!! Because 'eventee.ser' is exported to clients who may not have the full
// definitions of Rebol's internal types like REBSER available, it is defined
// as a 'void*'.  This "dereference a cast of an address as a double-pointer"
// trick allows us to use VAL_EVENT_SER on the left hand of an assignment,
// but means that 'v' cannot be const to use this on the right hand side.
// An m_cast will have to be used in those cases (or split up this macro)
#define VAL_EVENT_SER(v) \
    (*cast(REBSER **, &(v)->data.event.eventee.ser))

#define IS_EVENT_MODEL(v,f) (VAL_EVENT_MODEL(v) == (f))

#define SET_EVENT_INFO(val, type, flags, win) \
    VAL_EVENT_TYPE(val)=type, VAL_EVENT_FLAGS(val)=flags, VAL_EVENT_WIN(val)=win
    //VAL_EVENT_INFO(val) = (type | (flags << 16) | (win << 24))

#define VAL_EVENT_X(v)      ((REBINT) (short) (VAL_EVENT_DATA(v) & 0xffff))
#define VAL_EVENT_Y(v)      ((REBINT) (short) ((VAL_EVENT_DATA(v) >> 16) & 0xffff))
#define VAL_EVENT_XY(v)     (VAL_EVENT_DATA(v))
#define SET_EVENT_XY(v,x,y) VAL_EVENT_DATA(v) = ((y << 16) | (x & 0xffff))

#define VAL_EVENT_KEY(v)    (VAL_EVENT_DATA(v) & 0xffff)
#define VAL_EVENT_KCODE(v)  ((VAL_EVENT_DATA(v) >> 16) & 0xffff)
#define SET_EVENT_KEY(v,k,c) VAL_EVENT_DATA(v) = ((c << 16) + k)

#define IS_KEY_EVENT(type)  0

#ifdef old_code
#define TO_EVENT_XY(x,y)    (((y)<<16)|((x)&0xffff))
#define SET_EVENT_INFO(v,t,k,c,w,f) ((VAL_FLAGS(v)=(VAL_FLAGS(v)&0x0f)|((f)&0xf0)),\
                                    (VAL_EVENT_INFO(v)=(((t)&0xff)|(((k)&0xff)<<8)|\
                                    (((c)&0xff)<<16)|(((w)&0xff)<<24))))
#endif


/***********************************************************************
**
*/  struct Reb_Series
/*
**      Series header points to data and keeps track of tail and size.
**      Additional fields can be used for attributes and GC. Every
**      string and block in REBOL uses one of these to permit GC
**      and compaction.
**
***********************************************************************/
{
    REBYTE  *data;      // series data head
#ifdef SERIES_LABELS
    const REBYTE  *label;       // identify the series
#endif

    REBCNT  tail;       // one past end of useful data
    REBCNT  rest;       // total number of units from bias to end
    REBINT  info;       // holds width and flags
#if defined(__LP64__) || defined(__LLP64__)
    REBCNT padding; /* make next pointer is naturally aligned */
#endif
    union {
        REBCNT size;    // used for vectors and bitsets
        REBSER *series; // MAP datatype uses this
        struct {
            REBCNT wide:16;
            REBCNT high:16;
        } area;
        REBFLG negated; // used by bitsets
        REBUPT all; /* for copying, must have the same size as the union */
    } misc;

// !!! There is an issue if this is put earlier in the structure that it
// mysteriously makes HTTPS reads start timing out.  So it's either alignment
// or some other issue, which will hopefully be ferreted out by more and
// stronger checks (ubsan, etc).  For now, putting it at the end seems to work,
// but it's sketchy so be forewarned, and test `read https://...` if it moves.
//
#if !defined(NDEBUG)
    REBINT *guard; // intentionally alloc'd and freed for use by Panic_Series
#endif
};

#define SERIES_TAIL(s)   ((s)->tail)
#define SERIES_REST(s)   ((s)->rest)
#define SERIES_FLAGS(s)  ((s)->info)
#define SERIES_WIDE(s)   (((s)->info) & 0xff)
#define SERIES_DATA(s)   ((s)->data)
#define SERIES_SKIP(s,i) (SERIES_DATA(s) + (SERIES_WIDE(s) * i))

// !!! Ultimately this should replace SERIES_TAIL
#define SERIES_LEN(s)    SERIES_TAIL(s)

// These flags are returned from Do_Next_Core and Do_Next_May_Throw, in
// order to keep from needing another returned value in addition to the
// index (as they both imply that no "next index" exists to be returned)

#define END_FLAG 0x80000000  // end of block as index
#define THROWN_FLAG (END_FLAG - 1) // throw as an index

#ifdef SERIES_LABELS
#define SERIES_LABEL(s)  ((s)->label)
#define SET_SERIES_LABEL(s,l) (((s)->label) = (l))
#else
#define SERIES_LABEL(s)  "-"
#define SET_SERIES_LABEL(s,l)
#endif

// Flag: If wide field is not set, series is free (not used):
#define SERIES_FREED(s)  (!SERIES_WIDE(s))

// Size in bytes of memory allocated (including bias area):
#define SERIES_TOTAL(s) ((SERIES_REST(s) + SERIES_BIAS(s)) * (REBCNT)SERIES_WIDE(s))
// Size in bytes of series (not including bias area):
#define SERIES_SPACE(s) (SERIES_REST(s) * (REBCNT)SERIES_WIDE(s))
// Size in bytes being used, including terminator:
#define SERIES_USED(s) ((SERIES_LEN(s) + 1) * SERIES_WIDE(s))

// Optimized expand when at tail (but, does not reterminate)
#define EXPAND_SERIES_TAIL(s,l) if (SERIES_FITS(s, l)) s->tail += l; else Expand_Series(s, AT_TAIL, l)
#define RESIZE_SERIES(s,l) s->tail = 0; if (!SERIES_FITS(s, l)) Expand_Series(s, AT_TAIL, l); s->tail = 0
#define RESET_SERIES(s) s->tail = 0; TERM_SERIES(s)
#define RESET_TAIL(s) s->tail = 0

// Clear all and clear to tail:
#define CLEAR_SEQUENCE(s) \
    do { \
        assert(!Is_Array_Series(s)); \
        CLEAR(SERIES_DATA(s), SERIES_SPACE(s)); \
    } while (0)

#define TERM_SEQUENCE(s) \
    do { \
        assert(!Is_Array_Series(s)); \
        memset(SERIES_SKIP(s, SERIES_TAIL(s)), 0, SERIES_WIDE(s)); \
    } while (0)

// Returns space that a series has available (less terminator):
#define SERIES_FULL(s) (SERIES_LEN(s) + 1 >= SERIES_REST(s))
#define SERIES_AVAIL(s) (SERIES_REST(s) - (SERIES_LEN(s) + 1))
#define SERIES_FITS(s,n) ((SERIES_TAIL(s) + (REBCNT)(n) + 1) <= SERIES_REST(s))

// Flag used for extending series at tail:
#define AT_TAIL ((REBCNT)(~0))  // Extend series at tail

// Is it a byte-sized series? (this works because no other odd size allowed)
#define BYTE_SIZE(s) (((s)->info) & 1)
#define VAL_BYTE_SIZE(v) (BYTE_SIZE(VAL_SERIES(v)))
#define VAL_STR_IS_ASCII(v) \
    (VAL_BYTE_SIZE(v) && All_Bytes_ASCII(VAL_BIN_DATA(v), VAL_LEN(v)))

// Bias is empty space in front of head:
#define SERIES_BIAS(s)     (REBCNT)((SERIES_FLAGS(s) >> 16) & 0xffff)
#define MAX_SERIES_BIAS    0x1000
#define SERIES_SET_BIAS(s,b) (SERIES_FLAGS(s) = (SERIES_FLAGS(s) & 0xffff) | (b << 16))
#define SERIES_ADD_BIAS(s,b) (SERIES_FLAGS(s) += (b << 16))
#define SERIES_SUB_BIAS(s,b) (SERIES_FLAGS(s) -= (b << 16))

// Series Flags:
enum {
    SER_MARK        = 1 << 0,   // was found during GC mark scan.
    SER_FRAME       = 1 << 1,   // object frame (unsets legal, has key series)
    SER_LOCK        = 1 << 2,   // size is locked (do not expand it)
    SER_EXTERNAL    = 1 << 3,   // ->data is external, don't free() on GC
    SER_MANAGED     = 1 << 4,   // series is managed by garbage collection
    SER_ARRAY       = 1 << 5,   // is sizeof(REBVAL) wide and has valid values
    SER_PROTECT     = 1 << 6,   // protected from modification
    SER_POWER_OF_2  = 1 << 7    // true alloc size is rounded to power of 2
};

#define SERIES_SET_FLAG(s, f) cast(void, (SERIES_FLAGS(s) |= ((f) << 8)))
#define SERIES_CLR_FLAG(s, f) cast(void, (SERIES_FLAGS(s) &= ~((f) << 8)))
#define SERIES_GET_FLAG(s, f) (0 != (SERIES_FLAGS(s) & ((f) << 8)))

#define LOCK_SERIES(s)    SERIES_SET_FLAG(s, SER_LOCK)
#define IS_LOCK_SERIES(s) SERIES_GET_FLAG(s, SER_LOCK)
#define Is_Array_Series(s) SERIES_GET_FLAG((s), SER_ARRAY)

#define FAIL_IF_PROTECTED_SERIES(s) \
    if (SERIES_GET_FLAG(s, SER_PROTECT)) fail (Error(RE_PROTECTED))

#define FAIL_IF_PROTECTED_FRAME(f) \
    FAIL_IF_PROTECTED_SERIES(FRAME_VARLIST(f))

#ifdef SERIES_LABELS
#define LABEL_SERIES(s,l) s->label = (l)
#else
#define LABEL_SERIES(s,l)
#endif

#ifdef NDEBUG
    #define ASSERT_SERIES_TERM(s) cast(void, 0)
#else
    #define ASSERT_SERIES_TERM(s) Assert_Series_Term_Core(s)
#endif

#ifdef NDEBUG
    #define ASSERT_SERIES(s) cast(void, 0)
#else
    #define ASSERT_SERIES(s) \
        do { \
            if (Is_Array_Series(s)) \
                ASSERT_ARRAY(s); \
            else \
                ASSERT_SERIES_TERM(s); \
        } while (0)
#endif


//#define LABEL_SERIES(s,l) s->label = (l)

// !!! Remove if not used after port:
//#define   SERIES_SIDE(s)   ((s)->link.side)
//#define   SERIES_FRAME(s)  ((s)->link.frame)
//#define SERIES_NOT_REBOLS(s) SERIES_SET_FLAG(s, SER_XLIB)


/***********************************************************************
**
**  SERIES -- Generic series macros
**
***********************************************************************/

#pragma pack()
#include "reb-gob.h"
#pragma pack(4)

struct Reb_Position
{
    REBSER  *series;
    REBCNT  index;
};

#ifdef NDEBUG
    #define VAL_SERIES(v)   ((v)->data.position.series)
#else
    #define VAL_SERIES(v)   (*VAL_SERIES_Ptr_Debug(v))
#endif
#define VAL_INDEX(v)        ((v)->data.position.index)
#define VAL_TAIL(v)         (VAL_SERIES(v)->tail)
#define VAL_LEN(v)          (Val_Series_Len(v))

#define VAL_DATA(s)         (VAL_BIN_HEAD(s) + (VAL_INDEX(s) * VAL_SERIES_WIDTH(s)))

#define VAL_SERIES_WIDTH(v) (SERIES_WIDE(VAL_SERIES(v)))
#define VAL_LIMIT_SERIES(v) if (VAL_INDEX(v) > VAL_TAIL(v)) VAL_INDEX(v) = VAL_TAIL(v)

#define DIFF_PTRS(a,b) (REBCNT)((REBYTE*)a - (REBYTE*)b)

// Note: These macros represent things that used to sometimes be functions,
// and sometimes were not.  They could be done without a function call, but
// that would then make them unsafe to use with side-effects:
//
//     Val_Init_Block(Alloc_Tail_Array(parent), child);
//
// The repetitition of the value parameter would lead to the allocation
// running multiple times.  Hence we Caps_Words_With_Underscore to name
// these macros to indicate they are safe by not duplicating their args.
// If erring on the side of caution and making a function call turns out
// to be a problem in profiling, then on a case-by-case basis those
// bottlenecks can be replaced with something more like:
//
//     VAL_SET(value, REB_XXX);
//     ENSURE_SERIES_MANAGED(series);
//     VAL_SERIES(value) = series;
//     VAL_INDEX(value) = index;
//
// (Or perhaps just use proper inlining and support it in those builds.)

#define Val_Init_Series_Index(v,t,s,i) \
    Val_Init_Series_Index_Core((v), (t), (s), (i))

#define Val_Init_Series(v,t,s) \
    Val_Init_Series_Index((v), (t), (s), 0)

#define Val_Init_Block_Index(v,s,i) \
    Val_Init_Series_Index((v), REB_BLOCK, (s), (i))

#define Val_Init_Block(v,s) \
    Val_Init_Block_Index((v), (s), 0)


#define Copy_Values_Len_Shallow(v,l) \
    Copy_Values_Len_Shallow_Extra((v), (l), 0)

#define Copy_Array_Shallow(a) \
    Copy_Array_At_Shallow((a), 0)

#define Copy_Array_Deep_Managed(a) \
    Copy_Array_At_Extra_Deep_Managed((a), 0, 0)

#define Copy_Array_At_Deep_Managed(a,i) \
    Copy_Array_At_Extra_Deep_Managed((a), (i), 0)

#define Copy_Array_At_Shallow(a,i) \
    Copy_Array_At_Extra_Shallow((a), (i), 0)

#define Copy_Array_Extra_Shallow(a,e) \
    Copy_Array_At_Extra_Shallow((a), 0, (e))


#define Append_Value(a,v) \
    (*Alloc_Tail_Array((a)) = *(v), NOOP)


/***********************************************************************
**
**  STRINGS -- All string related values
**
***********************************************************************/

#define Val_Init_String(v,s) \
    Val_Init_Series((v), REB_STRING, (s))

#define Val_Init_Binary(v,s) \
    Val_Init_Series((v), REB_BINARY, (s))

#define Val_Init_File(v,s) \
    Val_Init_Series((v), REB_FILE, (s))

#define Val_Init_Tag(v,s) \
    Val_Init_Series((v), REB_TAG, (s))

#define Val_Init_Bitset(v,s) \
    Val_Init_Series((v), REB_BITSET, (s))

#define SET_STR_END(s,n) (*STR_SKIP(s,n) = 0)

// Arg is a binary (byte) series:
#define BIN_HEAD(s)     ((REBYTE *)((s)->data))
#define BIN_DATA(s)     ((REBYTE *)((s)->data))
#define BIN_TAIL(s)     (REBYTE*)STR_TAIL(s)
#define BIN_SKIP(s, n)  (((REBYTE *)((s)->data))+(n))
#define BIN_LEN(s)      (SERIES_TAIL(s))

// Arg is a unicode series:
#define UNI_HEAD(s)     ((REBUNI *)((s)->data))
#define UNI_SKIP(s, n)  (((REBUNI *)((s)->data))+(n))
#define UNI_TAIL(s)     (((REBUNI *)((s)->data))+(s)->tail)
#define UNI_LAST(s)     (((REBUNI *)((s)->data))+((s)->tail-1)) // make sure tail not zero
#define UNI_LEN(s)      (SERIES_TAIL(s))
#define UNI_TERM(s)     (*UNI_TAIL(s) = 0)
#define UNI_RESET(s)    (UNI_HEAD(s)[(s)->tail = 0] = 0)

// Obsolete (remove after Unicode conversion):
#define STR_HEAD(s)     ((REBYTE *)((s)->data))
#define STR_DATA(s)     ((REBYTE *)((s)->data))
#define STR_SKIP(s, n)  (((REBYTE *)((s)->data))+(n))
#define STR_TAIL(s)     (((REBYTE *)((s)->data))+(s)->tail)
#define STR_LAST(s)     (((REBYTE *)((s)->data))+((s)->tail-1)) // make sure tail not zero
#define STR_LEN(s)      (SERIES_TAIL(s))
#define STR_TERM(s)     (*STR_TAIL(s) = 0)
#define STR_RESET(s)    (STR_HEAD(s)[(s)->tail = 0] = 0)

// Arg is a binary value:
#define VAL_BIN(v)      BIN_HEAD(VAL_SERIES(v))
#define VAL_BIN_HEAD(v) BIN_HEAD(VAL_SERIES(v))
#define VAL_BIN_DATA(v) BIN_SKIP(VAL_SERIES(v), VAL_INDEX(v))
#define VAL_BIN_SKIP(v,n) BIN_SKIP(VAL_SERIES(v), (n))
#define VAL_BIN_TAIL(v) BIN_SKIP(VAL_SERIES(v), VAL_SERIES(v)->tail)

// Arg is a unicode value:
#define VAL_UNI(v)      UNI_HEAD(VAL_SERIES(v))
#define VAL_UNI_HEAD(v) UNI_HEAD(VAL_SERIES(v))
#define VAL_UNI_DATA(v) UNI_SKIP(VAL_SERIES(v), VAL_INDEX(v))

// Get a char, from either byte or unicode string:
#define GET_ANY_CHAR(s,n) \
    cast(REBUNI, BYTE_SIZE(s) ? BIN_HEAD(s)[n] : UNI_HEAD(s)[n])

#define SET_ANY_CHAR(s,n,c) \
    (BYTE_SIZE(s) \
        ? (BIN_HEAD(s)[n]=(cast(REBYTE, (c)))) \
        : (UNI_HEAD(s)[n]=(cast(REBUNI, (c)))) \
    )

#define VAL_ANY_CHAR(v) GET_ANY_CHAR(VAL_SERIES(v), VAL_INDEX(v))

//#define VAL_STR_LAST(v)   STR_LAST(VAL_SERIES(v))
//#define   VAL_MEM_LEN(v)  (VAL_TAIL(v) * VAL_SERIES_WIDTH(v))


/***********************************************************************
**
**  IMAGES, QUADS - RGBA
**
***********************************************************************/

//typedef struct Reb_ImageInfo
//{
//  REBCNT width;
//  REBCNT height;
//  REBINT transp;
//} REBIMI;

#define QUAD_HEAD(s)    ((REBYTE *)((s)->data))
#define QUAD_SKIP(s,n)  (((REBYTE *)((s)->data))+(n * 4))
#define QUAD_TAIL(s)    (((REBYTE *)((s)->data))+((s)->tail * 4))
#define QUAD_LEN(s)     (SERIES_TAIL(s))

#define IMG_SIZE(s)     ((s)->misc.size)
#define IMG_WIDE(s)     ((s)->misc.area.wide)
#define IMG_HIGH(s)     ((s)->misc.area.high)
#define IMG_DATA(s)     ((REBYTE *)((s)->data))

#define VAL_IMAGE_HEAD(v)   QUAD_HEAD(VAL_SERIES(v))
#define VAL_IMAGE_TAIL(v)   QUAD_SKIP(VAL_SERIES(v), VAL_SERIES(v)->tail)
#define VAL_IMAGE_DATA(v)   QUAD_SKIP(VAL_SERIES(v), VAL_INDEX(v))
#define VAL_IMAGE_BITS(v)   ((REBCNT *)VAL_IMAGE_HEAD((v)))
#define VAL_IMAGE_WIDE(v)   (IMG_WIDE(VAL_SERIES(v)))
#define VAL_IMAGE_HIGH(v)   (IMG_HIGH(VAL_SERIES(v)))
#define VAL_IMAGE_LEN(v)    VAL_LEN(v)

#define Val_Init_Image(v,s) \
    Val_Init_Series((v), REB_IMAGE, (s));


//#define VAL_IMAGE_TRANSP(v) (VAL_IMAGE_INFO(v)->transp)
//#define VAL_IMAGE_TRANSP_TYPE(v) (VAL_IMAGE_TRANSP(v)&0xff000000)
//#define VITT_UNKNOWN  0x00000000
//#define VITT_NONE     0x01000000
//#define VITT_ALPHA        0x02000000
//#define   VAL_IMAGE_DEPTH(v)  ((VAL_IMAGE_INFO(v)>>24)&0x3f)
//#define VAL_IMAGE_TYPE(v)     ((VAL_IMAGE_INFO(v)>>30)&3)

// New Image Datatype defines:

//tuple to image! pixel order bytes
#define TO_PIXEL_TUPLE(t) TO_PIXEL_COLOR(VAL_TUPLE(t)[0], VAL_TUPLE(t)[1], VAL_TUPLE(t)[2], \
                            VAL_TUPLE_LEN(t) > 3 ? VAL_TUPLE(t)[3] : 0xff)
//tuple to RGBA bytes
#define TO_COLOR_TUPLE(t) TO_RGBA_COLOR(VAL_TUPLE(t)[0], VAL_TUPLE(t)[1], VAL_TUPLE(t)[2], \
                            VAL_TUPLE_LEN(t) > 3 ? VAL_TUPLE(t)[3] : 0xff)

/***********************************************************************
**
**  Logic and Logic Bits
**
**  For purposes of optimization, logical falsehood is set as one of the
**  value option bits--as opposed to in a separate place from the header.
**
**  Conditional truth and falsehood allows an interpretation where a NONE!
**  is a "falsey" value as well as logic false.  Unsets are neither
**  conditionally true nor conditionally false, and so debug builds will
**  complain if you try to determine which it is--since it likely means
**  a mistake was made on a decision regarding whether something should be
**  an "opt out" or an error.
**
***********************************************************************/

#define SET_TRUE(v) \
    ((v)->flags.all = (1 << OPT_VALUE_NOT_END), \
     (v)->flags.bitfields.type = REB_LOGIC)  // compound

#define SET_FALSE(v) \
    ((v)->flags.all = (1 << OPT_VALUE_NOT_END) | (1 << OPT_VALUE_FALSE), \
     (v)->flags.bitfields.type = REB_LOGIC)  // compound

#define SET_LOGIC(v,n)  ((n) ? SET_TRUE(v) : SET_FALSE(v))

#define VAL_LOGIC(v)    !VAL_GET_OPT((v), OPT_VALUE_FALSE)

// !!! The logic used to be an I32 but now it's folded in as a value flag
#define VAL_I32(v)      ((v)->data.rebcnt)   // used for handles, etc.

#ifdef NDEBUG
    #define IS_CONDITIONAL_FALSE(v) \
        VAL_GET_OPT((v), OPT_VALUE_FALSE)
#else
    // In a debug build, we want to make sure that UNSET! is never asked
    // about its conditional truth or falsehood; it's neither.
    //
    #define IS_CONDITIONAL_FALSE(v) \
        IS_CONDITIONAL_FALSE_Debug(v)
#endif

#define IS_CONDITIONAL_TRUE(v) !IS_CONDITIONAL_FALSE(v)


/***********************************************************************
**
**  BIT_SET -- Bit sets
**
***********************************************************************/

#define VAL_BITSET(v)   VAL_SERIES(v)

#define VAL_BIT_DATA(v) VAL_BIN(v)

#define SET_BIT(d,n)    ((d)[(n) >> 3] |= (1 << ((n) & 7)))
#define CLR_BIT(d,n)    ((d)[(n) >> 3] &= ~(1 << ((n) & 7)))
#define IS_BIT(d,n)     ((d)[(n) >> 3] & (1 << ((n) & 7)))


/***********************************************************************
**
**  BLOCKS -- Block is a terminated string of values
**
***********************************************************************/

// Arg is a series:
#define BLK_HEAD(s)     ((REBVAL *)((s)->data))
#define BLK_SKIP(s, n)  (((REBVAL *)((s)->data))+(n))
#define BLK_TAIL(s)     (((REBVAL *)((s)->data))+(s)->tail)
#define BLK_LAST(s)     (((REBVAL *)((s)->data))+((s)->tail-1)) // make sure tail not zero
#define BLK_LEN(s)      (SERIES_TAIL(s))
#define BLK_RESET(b)    (b)->tail = 0, SET_END(BLK_HEAD(b))

#define TERM_ARRAY(s) \
    do { \
        assert(Is_Array_Series(s)); \
        SET_END(BLK_TAIL(s)); \
    } while (0)

#define TERM_SERIES(s) \
    Is_Array_Series(s) \
        ? cast(void, SET_END(BLK_TAIL(s))) \
        : cast(void, memset(SERIES_SKIP(s, SERIES_TAIL(s)), 0, SERIES_WIDE(s)))

// Arg is a value:
#define VAL_BLK_HEAD(v) BLK_HEAD(VAL_SERIES(v))
#define VAL_BLK_DATA(v) BLK_SKIP(VAL_SERIES(v), VAL_INDEX(v))
#define VAL_BLK_SKIP(v,n)   BLK_SKIP(VAL_SERIES(v), (n))
#define VAL_BLK_TAIL(v) BLK_SKIP(VAL_SERIES(v), VAL_SERIES(v)->tail)
#define VAL_BLK_LEN(v)  VAL_LEN(v)
#define VAL_TERM_ARRAY(v)   TERM_ARRAY(VAL_SERIES(v))

#define IS_EMPTY(v)     (VAL_INDEX(v) >= VAL_TAIL(v))

#ifdef NDEBUG
    #define ASSERT_ARRAY(s) cast(void, 0)
#else
    #define ASSERT_ARRAY(s) Assert_Array_Core(s)
#endif


/***********************************************************************
**
**  SYMBOLS -- Used only for symbol tables
**
***********************************************************************/

struct Reb_Symbol {
    REBCNT  canon;  // Index of the canonical (first) word
    REBCNT  alias;  // Index to next alias form
    REBCNT  name;   // Index into PG_Word_Names string
};

// Arg is value:
#define VAL_SYM_NINDEX(v)   ((v)->data.symbol.name)
#define VAL_SYM_NAME(v)     (STR_HEAD(PG_Word_Names) + VAL_SYM_NINDEX(v))
#define VAL_SYM_CANON(v)    ((v)->data.symbol.canon)
#define VAL_SYM_ALIAS(v)    ((v)->data.symbol.alias)

// Return the CANON value for a symbol number:
#define SYMBOL_TO_CANON(sym) (VAL_SYM_CANON(BLK_SKIP(PG_Word_Table.series, sym)))
// Return the CANON value for a word value:
#define WORD_TO_CANON(w) (VAL_SYM_CANON(BLK_SKIP(PG_Word_Table.series, VAL_WORD_SYM(w))))


/***********************************************************************
**
**  WORDS -- All word related types
**
***********************************************************************/

struct Reb_Word {
    REBSER *target; // Frame (or VAL_FUNC_PARAMLIST) where word is defined
    REBINT index;   // Index of word in frame (if it's not NULL)
    REBCNT sym;     // Index of the word's symbol
};

#define IS_SAME_WORD(v, n)      (IS_WORD(v) && VAL_WORD_CANON(v) == n)

#ifdef NDEBUG
    #define VAL_WORD_SYM(v) ((v)->data.word.sym)
#else
    // !!! Due to large reorganizations, it may be that VAL_WORD_SYM and
    // VAL_TYPESET_SYM calls were swapped.  In the aftermath of reorganization
    // this check is prudent (until further notice...)
    #define VAL_WORD_SYM(v) (*Val_Word_Sym_Ptr_Debug(v))
#endif

#define VAL_WORD_INDEX(v)       ((v)->data.word.index)
#define VAL_WORD_TARGET(v)      ((v)->data.word.target)
#define HAS_TARGET(v)            (VAL_WORD_TARGET(v) != NULL)

#ifdef NDEBUG
    #define UNBIND_WORD(v) \
        (VAL_WORD_TARGET(v)=NULL)
#else
    #define WORD_INDEX_UNBOUND MIN_I32
    #define UNBIND_WORD(v) \
        (VAL_WORD_TARGET(v)=NULL, VAL_WORD_INDEX(v)=WORD_INDEX_UNBOUND)
#endif

#define VAL_WORD_CANON(v)       VAL_SYM_CANON(BLK_SKIP(PG_Word_Table.series, VAL_WORD_SYM(v)))
#define VAL_WORD_NAME(v)        VAL_SYM_NAME(BLK_SKIP(PG_Word_Table.series, VAL_WORD_SYM(v)))
#define VAL_WORD_NAME_STR(v)    STR_HEAD(VAL_WORD_NAME(v))

#define VAL_WORD_TARGET_WORDS(v) VAL_WORD_TARGET(v)->words
#define VAL_WORD_TARGET_VALUES(v) VAL_WORD_TARGET(v)->values

// Is it the same symbol? Quick check, then canon check:
#define SAME_SYM(s1,s2) \
    ((s1) == (s2) \
    || ( \
        VAL_SYM_CANON(BLK_SKIP(PG_Word_Table.series, (s1))) \
        == VAL_SYM_CANON(BLK_SKIP(PG_Word_Table.series, (s2))) \
    ))


/***********************************************************************
**
**	CONTEXTS
**
**	The Reb_Context is the basic struct used currently for OBJECT!,
**	MODULE!, ERROR!, and PORT!...providing behaviors common to ANY-CONTEXT!
**
**	It implements a key/value pairing via two parallel series, whose indices
**	line up in a correspondence.  The "keylist" series contains REBVALs that
**	are symbol IDs encoded as an extra piece information inside of a TYPESET!.
**	The "value" REBVALs are in a series called the "frame", which lines up at
**	the index appropriate for the key.  The index into these series is used
**	in the "binding" of a WORD! for cached lookup so that the symbol does not
**	need to be searched for each time.
**
**	!!! This "caching" mechanism is not actually "just a cache".  Once bound
**	the index is treated as permanent.  This is why objects are "append only"
**	because disruption of the index numbers would break the extant words
**	with index numbers to that position.  Ren-C intends to undo this by
**	paying for the check of the symbol number at the time of lookup, and if
**	it does not match consider it a cache miss and re-lookup...adjusting the
**	index inside of the word.  For efficiency, some objects could be marked
**	as not having this property, but it may be just as efficient to check
**	the symbol match as that bit.
**
**	The indices start at 1, which leaves an open slot at the zero position in
**	both the keylist and the frame.  The frame uses this slot to hold the
**	value of the OBJECT! itself.  This trick allows the single frame REBSER
**	pointer to be passed around rather than the REBVAL struct which is 4x
**	larger, yet still reconstitute the REBVAL if it is needed.
**
**	Because a REBSER which contains an object at its head is uniquely capable
**	of retrieving the keylist by digging into its implicit first OBJECT!
**	value, it is often considered a unique type called a "frame", and
**  passed around as a type that checks differently known as a REBFRM.
**
***********************************************************************/

typedef struct Reb_Frame {
    REBSER series; // keylist is held in REBSER.misc.series
} REBFRM;

#ifdef NDEBUG
    #define ASSERT_FRAME(f) cast(void, 0)
#else
    #define ASSERT_FRAME(f) Assert_Frame_Core(f)
#endif

// Series-to-Frame cocercion
//
// !!! This cast is of a pointer to a type to a pointer to a struct with
// just that type in it may not technically be legal in the standard w.r.t.
// strict aliasing.  Review this mechanic.  There's probably a legal way
// of working around it.  But if worst comes to worst, it can be disabled
// and the two can be made equivalent--except when doing a build for
// type checking purposes.
//
#ifdef NDEBUG
    #define AS_FRAME(s)     cast(REBFRM*, (s))
#else
    // Put a debug version here that asserts.
    #define AS_FRAME(s)     cast(REBFRM*, (s))
#endif

struct Reb_Context {
    REBFRM *frame;
    REBFRM *spec; // optional (currently only used by modules)
    REBSER *body; // optional (currently not used at all)
};

// Context components
//
#ifdef NDEBUG
    #define VAL_FRAME(v)            ((v)->data.context.frame)
#else
    #define VAL_FRAME(v)            (*VAL_FRAME_Ptr_Debug(v))
#endif
#define VAL_CONTEXT_SPEC(v)         ((v)->data.context.spec)
#define VAL_CONTEXT_BODY(v)         ((v)->data.context.body)

// Special property: keylist pointer is stored in the misc field of REBSER
//
#define FRAME_VARLIST(f)            (&(f)->series)
#define FRAME_KEYLIST(f)            ((f)->series.misc.series)

// The keys and vars are accessed by positive integers starting at 1.  If
// indexed access is used then the debug build will check to be sure that
// the indexing is legal.  To get a pointer to the first key or value
// regardless of length (e.g. will be an END if 0 keys/vars) use HEAD
//
#define FRAME_KEYS_HEAD(f)          BLK_SKIP(FRAME_KEYLIST(f), 1)
#define FRAME_VARS_HEAD(f)          BLK_SKIP(FRAME_VARLIST(f), 1)
#ifdef NDEBUG
    #define FRAME_KEY(f,n)          BLK_SKIP(FRAME_KEYLIST(f), (n))
    #define FRAME_VAR(f,n)          BLK_SKIP(FRAME_VARLIST(f), (n))
#else
    #define FRAME_KEY(f,n)          FRAME_KEY_Debug((f), (n))
    #define FRAME_VAR(f,n)          FRAME_VAR_Debug((f), (n))
#endif
#define FRAME_KEY_SYM(f,n)          VAL_TYPESET_SYM(FRAME_KEY((f), (n)))

// Navigate from frame series to context components.  Note that the frame's
// "length" does not count the [0] cell of either the varlist or the keylist.
// Hence it must subtract 1.  Internally to the frame building code, the
// real length of the two series must be accounted for...so the 1 gets put
// back in, but most clients are only interested in the number of keys/values
// (and getting an answer for the length back that was the same as the length
// requested in frame creation).
//
#define FRAME_LEN(f)                (SERIES_LEN(FRAME_VARLIST(f)) - 1)
#define FRAME_CONTEXT(f)            BLK_HEAD(FRAME_VARLIST(f))
#define FRAME_ROOTKEY(f)            BLK_HEAD(FRAME_KEYLIST(f))
#define FRAME_TYPE(f)               VAL_TYPE(FRAME_CONTEXT(f))
#define FRAME_SPEC(f)               VAL_CONTEXT_SPEC(FRAME_CONTEXT(f))
#define FRAME_BODY(f)               VAL_CONTEXT_BODY(FRAME_CONTEXT(f))

// A fully constructed frames can reconstitute the context REBVAL that it is
// a frame for from a single pointer...the REBVAL sitting in the 0 slot
// of the frame's varlist.  In a debug build we check to make sure the
// type of the embedded value matches the type of what is intended (so
// someone who thinks they are initializing a REB_OBJECT from a FRAME does
// not accidentally get a REB_ERROR, for instance.)
//
#if FALSE && defined(NDEBUG)
    //
    // !!! Currently Val_Init_Context_Core does not require the passed in
    // frame to already be managed.  If it did, then it could be this
    // simple and not be a "bad macro".  Review if it's worthwhile to change
    // the prerequisite that this is only called on managed frames.
    //
    #define Val_Init_Context(o,t,f,s,b) \
        (*(o) = *FRAME_CONTEXT(f))
#else
    #define Val_Init_Context(o,t,f,s,b) \
        Val_Init_Context_Core((o), (t), (f), (s), (b))
#endif

// Because information regarding reconstituting an object from a frame
// existed (albeit partially) in a FRAME! in R3-Alpha, the choice was made
// to have the keylist[0] hold a word that would let you refer to the
// object itself.  This "SELF" keyword concept is deprecated, and the
// slot will likely be used for another purpose after a "definitional self"
// solution (like "definitional return") removes the need for it.
//
#define IS_SELFLESS(f) \
    (IS_CLOSURE(FRAME_ROOTKEY(f)) \
        || VAL_TYPESET_SYM(FRAME_ROOTKEY(f)) == SYM_0)

// Convenience macros to speak in terms of object values instead of the frame
//
#define VAL_CONTEXT_VALUE(v,n)      FRAME_VAR(VAL_FRAME(v), (n))
#define VAL_CONTEXT_KEY(v,n)        FRAME_KEY(VAL_FRAME(v), (n))
#define VAL_CONTEXT_KEY_SYM(v,n)    FRAME_KEY_SYM(VAL_FRAME(v), (n))

#define Val_Init_Object(v,f) \
    Val_Init_Context((v), REB_OBJECT, (f), NULL, NULL)


/***********************************************************************
**
**  MODULES - Code isolation units
**
**  http://www.rebol.com/r3/docs/concepts/modules-defining.html
**
***********************************************************************/

#define VAL_MOD_SPEC(v)     VAL_CONTEXT_SPEC(v)
#define VAL_MOD_BODY(v)     VAL_CONTEXT_BODY(v)

#define Val_Init_Module(v,f,s,b) \
    Val_Init_Context((v), REB_MODULE, (f), (s), (b))


/***********************************************************************
**
**  PORTS - External series interface
**
***********************************************************************/

#define Val_Init_Port(v,f) \
    Val_Init_Context((v), REB_PORT, (f), NULL, NULL)


/***********************************************************************
**
**  ERRORS - Error values
**
**  At the present time, all ERROR! frames follow an identical
**  fixed layout.  That layout is in %sysobj.r as standard/error.
**
**  Errors can have a maximum of 3 arguments (named arg1, arg2, and
**  arg3).  There is also an error code which is used to look up
**  a formatting block that shows where the args are to be inserted
**  into a message.  The formatting block to use is looked up by
**  a numeric code established in that table.
**
**  !!! The needs of user errors to carry custom information with
**  custom field names means this rigid design will need to be
**  enhanced.  System error arguments will likely be named more
**  meaningfully, but will still use ordering to bridge from the
**  C calls that create them.
**
***********************************************************************/

#define ERR_VALUES(frame)   cast(ERROR_OBJ*, BLK_HEAD(FRAME_VARLIST(frame)))
#define ERR_NUM(frame)      cast(REBCNT, VAL_INT32(&ERR_VALUES(frame)->code))

#define VAL_ERR_VALUES(v)   ERR_VALUES(VAL_FRAME(v))
#define VAL_ERR_NUM(v)      ERR_NUM(VAL_FRAME(v))

#define Val_Init_Error(o,f) \
    Val_Init_Context((o), REB_ERROR, (f), NULL, NULL)



/***********************************************************************
**
**  VARIABLE ACCESS
**
**  When a word is bound to a frame by an index, it becomes a means of
**  reading and writing from a persistent storage location.  The term
**  "variable" is used to refer to a REBVAL slot reached through a
**  binding in this way.
**
**  All variables can be in a protected state where they cannot be
**  written.  Hence const access is the default, and a const pointer is
**  given back which may be inspected but the contents not modified.  If
**  mutable access is required, one may either demand write access
**  (and get a failure and longjmp'd error if not possible) or ask
**  more delicately with a TRY.
**
***********************************************************************/

// Gives back a const pointer to var itself, raises error on failure
// (Failure if unbound or stack-relative with no call on stack)
#define GET_VAR(w) \
    c_cast(const REBVAL*, Get_Var_Core((w), TRUE, FALSE))

// Gives back a const pointer to var itself, returns NULL on failure
// (Failure if unbound or stack-relative with no call on stack)
#define TRY_GET_VAR(w) \
    c_cast(const REBVAL*, Get_Var_Core((w), FALSE, FALSE))

// Gets mutable pointer to var itself, raises error on failure
// (Failure if protected, unbound, or stack-relative with no call on stack)
#define GET_MUTABLE_VAR(w) \
    (Get_Var_Core((w), TRUE, TRUE))

// Gets mutable pointer to var itself, returns NULL on failure
// (Failure if protected, unbound, or stack-relative with no call on stack)
#define TRY_GET_MUTABLE_VAR(w) \
    (Get_Var_Core((w), FALSE, TRUE))

// Makes a copy of the var's value, raises error on failure.
// (Failure if unbound or stack-relative with no call on stack)
// Copy means you can change it and not worry about PROTECT status of the var
// NOTE: *value* itself may carry its own PROTECT status if series/object
#define GET_VAR_INTO(v,w) \
    (Get_Var_Into_Core((v), (w)))

/***********************************************************************
**
**  GOBS - Graphic Objects
**
***********************************************************************/

struct Reb_Gob {
    REBGOB *gob;
    REBCNT index;
};

#define VAL_GOB(v)          ((v)->data.gob.gob)
#define VAL_GOB_INDEX(v)    ((v)->data.gob.index)
#define SET_GOB(v,g)        VAL_SET(v, REB_GOB), VAL_GOB(v)=g, VAL_GOB_INDEX(v)=0


/***********************************************************************
**
**  FUNCTIONS - Natives, actions, operators, and user functions
**
**  NOTE: make-headers.r will skip specs with the "REBNATIVE(" in them
**  REBTYPE macros are used and expanded in tmp-funcs.h
**
***********************************************************************/

enum {
    EXT_FUNC_INFIX = 0,     // called with "infix" protocol
    EXT_FUNC_HAS_RETURN,    // function "fakes" a definitionally scoped return
    EXT_FUNC_FRAMELESS,     // native hooks into DO state and does own arg eval
    EXT_FUNC_MAX
};

struct Reb_Call;

// enums in C have no guaranteed size, yet Rebol wants to use known size
// types in its interfaces.  Hence REB_R is a REBCNT from reb-c.h (and not
// this enumerated type containing its legal values).
enum {
    R_OUT = 0,

    // !!! The open-sourced Rebol3 of 12-Dec-2012 had the concept that
    // "thrown-ness" was a property of a value (in particular, certain kinds
    // of ERROR! which were not to ever be leaked directly to userspace).
    // Ren/C modifications extended THROW to allow a /NAME that could be
    // a full REBVAL (instead of a selection from a limited set of words)
    // hence making it possible to identify a throw by an object, function,
    // fully bound word, etc.  Yet still the "thrown-ness" was a property
    // of the throw-name REBVAL, and by virtue of being a property on a
    // value *it could be dropped on the floor and ignored*.  There were
    // countless examples of this.
    //
    // As part of the process of stamping out the idea that thrownness comes
    // from a value, all routines that can potentially return thrown values
    // have been adapted to return a boolean and adopt the XXX_Throws()
    // naming convention, so one can write:
    //
    //     if (XXX_Throws()) {
    //        /* handling code */
    //     }
    //
    // This forced every caller to consciously have a code path dealing with
    // potentially thrown values, reigning in the previous problems.  Yet
    // native function implementations didn't have a way to signal that
    // return result when the stack passed through them.
    //
    // R_OUT_IS_THROWN is a test of that signaling mechanism.  It is currently
    // being kept in parallel with the THROWN() bit and ensured as matching.
    // Being in the state of doing a stack unwind will likely be knowable
    // through other mechanisms even once the thrown bit on the value is
    // gone...so it may not be the case that natives are asked to do their
    // own separate indication, so this may wind up replaced with R_OUT.  For
    // the moment it is good as a double-check.

    R_OUT_IS_THROWN,

    // !!! These R_ values are somewhat superfluous...and actually inefficient
    // because they have to be checked by the caller in a switch statement
    // to take the equivalent action.  They have a slight advantage in
    // hand-written C code for making it more clear that if you have used
    // the D_OUT return slot for temporary work that you explicitly want
    // to specify another result...this cannot be caught by the REB_TRASH
    // trick for detecting an unwritten D_OUT.

    R_UNSET, // => SET_UNSET(D_OUT); return R_OUT;
    R_NONE, // => SET_NONE(D_OUT); return R_OUT;
    R_TRUE, // => SET_TRUE(D_OUT); return R_OUT;
    R_FALSE, // => SET_FALSE(D_OUT); return R_OUT;
    R_ARG1, // => *D_OUT = *D_ARG(1); return R_OUT;
    R_ARG2, // => *D_OUT = *D_ARG(2); return R_OUT;
    R_ARG3 // => *D_OUT = *D_ARG(3); return R_OUT;
};
typedef REBCNT REB_R;

// NATIVE! function
typedef REB_R (*REBFUN)(struct Reb_Call *call_);
#define REBNATIVE(n) \
    REB_R N_##n(struct Reb_Call *call_)

// ACTION! function (one per each DATATYPE!)
typedef REB_R (*REBACT)(struct Reb_Call *call_, REBCNT a);
#define REBTYPE(n) \
    REB_R T_##n(struct Reb_Call *call_, REBCNT action)

// PORT!-action function
typedef REB_R (*REBPAF)(struct Reb_Call *call_, REBFRM *p, REBCNT a);

// COMMAND! function
typedef REB_R (*CMD_FUNC)(REBCNT n, REBSER *args);

typedef struct Reb_Routine_Info REBRIN;

struct Reb_Function {
    REBSER  *spec;  // Spec block for function
    REBSER  *args;  // Block of Wordspecs (with typesets)
    union Reb_Func_Code {
        REBFUN  code;
        REBSER  *body;
        REBCNT  act;
        REBRIN  *info;
    } func;
};

/* argument to these is a pointer to struct Reb_Function */
#define FUNC_SPEC(v)      ((v)->spec)   // a series
#define FUNC_SPEC_BLK(v)  BLK_HEAD((v)->spec)
#define FUNC_ARGS(v)      ((v)->args)
#define FUNC_WORDS(v)     FUNC_ARGS(v)
#define FUNC_CODE(v)      ((v)->func.code)
#define FUNC_BODY(v)      ((v)->func.body)
#define FUNC_ACT(v)       ((v)->func.act)
#define FUNC_INFO(v)      ((v)->func.info)
#define FUNC_ARGC(v)      SERIES_TAIL((v)->args)

// !!! In the original formulation, the first parameter in the VAL_FUNC_WORDS
// started at 1.  The zero slot was left empty, in order for the function's
// word frames to line up to object frames where the zero slot is SELF.
// The pending implementation of definitionally scoped return bumps this
// number to 2, so we establish it as a named constant anticipating that.
#define FIRST_PARAM_INDEX 1

/* argument is of type REBVAL* */
#define VAL_FUNC(v)           ((v)->data.func)
#define VAL_FUNC_SPEC(v)      ((v)->data.func.spec) // a series
#define VAL_FUNC_SPEC_BLK(v)  BLK_HEAD((v)->data.func.spec)
#define VAL_FUNC_PARAMLIST(v)     ((v)->data.func.args)

#define VAL_FUNC_PARAM(v,p) \
    BLK_SKIP(VAL_FUNC_PARAMLIST(v), FIRST_PARAM_INDEX + (p) - 1)

#define VAL_FUNC_NUM_PARAMS(v) \
    (SERIES_TAIL(VAL_FUNC_PARAMLIST(v)) - FIRST_PARAM_INDEX)

#define VAL_FUNC_CODE(v)      ((v)->data.func.func.code)
#define VAL_FUNC_BODY(v)      ((v)->data.func.func.body)
#define VAL_FUNC_ACT(v)       ((v)->data.func.func.act)
#define VAL_FUNC_INFO(v)      ((v)->data.func.func.info)

// EXT_FUNC_HAS_RETURN functions use the RETURN native's function value to give
// the definitional return its prototype, but overwrite its code pointer to
// hold the paramlist of the target.
//
// Do_Native_Throws() sees when someone tries to execute one of these "native
// returns"...and instead interprets it as a THROW whose /NAME is the function
// value.  The paramlist has that value (it's the REBVAL in slot #0)
//
#define VAL_FUNC_RETURN_TO(v) VAL_FUNC_BODY(v)

typedef struct Reb_Path_Value {
    REBVAL *value;  // modified
    REBVAL *select; // modified
    REBVAL *path;   // modified
    REBVAL *store;  // modified (holds constructed values)
    REBVAL *setval; // static
    const REBVAL *orig; // static
} REBPVS;

enum Path_Eval_Result {
    PE_OK,
    PE_SET,
    PE_USE,
    PE_NONE,
    PE_BAD_SELECT,
    PE_BAD_SET,
    PE_BAD_RANGE,
    PE_BAD_SET_TYPE
};

typedef REBINT (*REBPEF)(REBPVS *pvs); // Path evaluator function

typedef REBINT (*REBCTF)(REBVAL *a, REBVAL *b, REBINT s);


/***********************************************************************
**
**  HANDLE
**
**  Type for holding an arbitrary code or data pointer inside
**  of a Rebol data value.  What kind of function or data is not
**  known to the garbage collector, so it ignores it.
**
**  !!! Review usages of this type where they occur
**
***********************************************************************/

struct Reb_Handle {
    union {
        CFUNC *code;
        void *data;
    } thing;
};

#define VAL_HANDLE_CODE(v) \
    ((v)->data.handle.thing.code)

#define VAL_HANDLE_DATA(v) \
    ((v)->data.handle.thing.data)

#define SET_HANDLE_CODE(v,c) \
    (VAL_SET((v), REB_HANDLE), VAL_HANDLE_CODE(v) = (c))

#define SET_HANDLE_DATA(v,d) \
    (VAL_SET((v), REB_HANDLE), VAL_HANDLE_DATA(v) = (d))


/***********************************************************************
**
**  LIBRARY -- External library management structures
**
***********************************************************************/

typedef struct Reb_Library_Handle {
    void * fd;
    REBFLG flags;
} REBLHL;

struct Reb_Library {
    REBLHL *handle;
    REBSER *spec;
};

#define LIB_FD(v)           ((v)->fd)
#define LIB_FLAGS(v)        ((v)->flags)

#define VAL_LIB(v)          ((v)->data.library)
#define VAL_LIB_SPEC(v)     ((v)->data.library.spec)
#define VAL_LIB_HANDLE(v)   ((v)->data.library.handle)
#define VAL_LIB_FD(v)       ((v)->data.library.handle->fd)
#define VAL_LIB_FLAGS(v)    ((v)->data.library.handle->flags)

enum {
    LIB_MARK = 1,       // library was found during GC mark scan.
    LIB_USED = 1 << 1,
    LIB_CLOSED = 1 << 2
};

#define LIB_SET_FLAG(s, f) (LIB_FLAGS(s) |= (f))
#define LIB_CLR_FLAG(s, f) (LIB_FLAGS(s) &= ~(f))
#define LIB_GET_FLAG(s, f) (LIB_FLAGS(s) &  (f))

#define MARK_LIB(s)    LIB_SET_FLAG(s, LIB_MARK)
#define UNMARK_LIB(s)  LIB_CLR_FLAG(s, LIB_MARK)
#define IS_MARK_LIB(s) LIB_GET_FLAG(s, LIB_MARK)

#define USE_LIB(s)     LIB_SET_FLAG(s, LIB_USED)
#define UNUSE_LIB(s)   LIB_CLR_FLAG(s, LIB_USED)
#define IS_USED_LIB(s) LIB_GET_FLAG(s, LIB_USED)

#define IS_CLOSED_LIB(s)    LIB_GET_FLAG(s, LIB_CLOSED)
#define CLOSE_LIB(s)        LIB_SET_FLAG(s, LIB_CLOSED)
#define OPEN_LIB(s)         LIB_CLR_FLAG(s, LIB_CLOSED)

/***********************************************************************
**
**  STRUCT -- C Structures
**
***********************************************************************/

typedef struct Reb_Struct {
    REBSER  *spec;
    REBSER  *fields;    // fields definition
    REBSER  *data;
} REBSTU;

#define VAL_STRUCT(v)       ((v)->data.structure)
#define VAL_STRUCT_SPEC(v)  ((v)->data.structure.spec)
#define VAL_STRUCT_FIELDS(v)  ((v)->data.structure.fields)
#define VAL_STRUCT_DATA(v)  ((v)->data.structure.data)
#define VAL_STRUCT_DP(v)    (STR_HEAD(VAL_STRUCT_DATA(v)))


/***********************************************************************
**
**  ROUTINE -- External library routine structures
**
***********************************************************************/
struct Reb_Routine_Info {
    union {
        struct {
            REBLHL  *lib;
            CFUNC *funcptr;
        } rot;
        struct {
            void *closure;
            struct Reb_Function func;
            void *dispatcher;
        } cb;
    } info;
    void    *cif;
    REBSER  *arg_types; /* index 0 is the return type, */
    REBSER  *fixed_args;
    REBSER  *all_args;
    REBSER  *arg_structs; /* for struct arguments */
    REBSER  *extra_mem; /* extra memory that needs to be free'ed */
    REBINT  abi;
    REBFLG  flags;
};

typedef struct Reb_Function REBROT;

enum {
    ROUTINE_MARK = 1,       // routine was found during GC mark scan.
    ROUTINE_USED = 1 << 1,
    ROUTINE_CALLBACK = 1 << 2, //this is a callback
    ROUTINE_VARARGS = 1 << 3 //this is a function with varargs
};

/* argument is REBFCN */
#define ROUTINE_SPEC(v)             FUNC_SPEC(v)
#define ROUTINE_INFO(v)             FUNC_INFO(v)
#define ROUTINE_ARGS(v)             FUNC_ARGS(v)
#define ROUTINE_FUNCPTR(v)          (ROUTINE_INFO(v)->info.rot.funcptr)
#define ROUTINE_LIB(v)              (ROUTINE_INFO(v)->info.rot.lib)
#define ROUTINE_ABI(v)              (ROUTINE_INFO(v)->abi)
#define ROUTINE_FFI_ARG_TYPES(v)    (ROUTINE_INFO(v)->arg_types)
#define ROUTINE_FIXED_ARGS(v)       (ROUTINE_INFO(v)->fixed_args)
#define ROUTINE_ALL_ARGS(v)         (ROUTINE_INFO(v)->all_args)
#define ROUTINE_FFI_ARG_STRUCTS(v)  (ROUTINE_INFO(v)->arg_structs)
#define ROUTINE_EXTRA_MEM(v)        (ROUTINE_INFO(v)->extra_mem)
#define ROUTINE_CIF(v)              (ROUTINE_INFO(v)->cif)
#define ROUTINE_RVALUE(v)           VAL_STRUCT(BLK_HEAD(ROUTINE_FFI_ARG_STRUCTS(v)))
#define ROUTINE_CLOSURE(v)          (ROUTINE_INFO(v)->info.cb.closure)
#define ROUTINE_DISPATCHER(v)       (ROUTINE_INFO(v)->info.cb.dispatcher)
#define CALLBACK_FUNC(v)            (ROUTINE_INFO(v)->info.cb.func)

/* argument is REBRIN */

#define RIN_FUNCPTR(v)              ((v)->info.rot.funcptr)
#define RIN_LIB(v)                  ((v)->info.rot.lib)
#define RIN_CLOSURE(v)              ((v)->info.cb.closure)
#define RIN_FUNC(v)                 ((v)->info.cb.func)
#define RIN_ARGS_STRUCTS(v)         ((v)->arg_structs)
#define RIN_RVALUE(v)               VAL_STRUCT(BLK_HEAD(RIN_ARGS_STRUCTS(v)))

#define ROUTINE_FLAGS(s)       ((s)->flags)
#define ROUTINE_SET_FLAG(s, f) (ROUTINE_FLAGS(s) |= (f))
#define ROUTINE_CLR_FLAG(s, f) (ROUTINE_FLAGS(s) &= ~(f))
#define ROUTINE_GET_FLAG(s, f) (ROUTINE_FLAGS(s) &  (f))

#define IS_CALLBACK_ROUTINE(s) ROUTINE_GET_FLAG(s, ROUTINE_CALLBACK)

/* argument is REBVAL */
#define VAL_ROUTINE(v)              VAL_FUNC(v)
#define VAL_ROUTINE_SPEC(v)         VAL_FUNC_SPEC(v)
#define VAL_ROUTINE_INFO(v)         VAL_FUNC_INFO(v)
#define VAL_ROUTINE_ARGS(v)         VAL_FUNC_PARAMLIST(v)
#define VAL_ROUTINE_FUNCPTR(v)      (VAL_ROUTINE_INFO(v)->info.rot.funcptr)
#define VAL_ROUTINE_LIB(v)          (VAL_ROUTINE_INFO(v)->info.rot.lib)
#define VAL_ROUTINE_ABI(v)          (VAL_ROUTINE_INFO(v)->abi)
#define VAL_ROUTINE_FFI_ARG_TYPES(v)    (VAL_ROUTINE_INFO(v)->arg_types)
#define VAL_ROUTINE_FIXED_ARGS(v)   (VAL_ROUTINE_INFO(v)->fixed_args)
#define VAL_ROUTINE_ALL_ARGS(v)     (VAL_ROUTINE_INFO(v)->all_args)
#define VAL_ROUTINE_FFI_ARG_STRUCTS(v)  (VAL_ROUTINE_INFO(v)->arg_structs)
#define VAL_ROUTINE_EXTRA_MEM(v)    (VAL_ROUTINE_INFO(v)->extra_mem)
#define VAL_ROUTINE_CIF(v)          (VAL_ROUTINE_INFO(v)->cif)
#define VAL_ROUTINE_RVALUE(v)       VAL_STRUCT((REBVAL*)SERIES_DATA(VAL_ROUTINE_INFO(v)->arg_structs))

#define VAL_ROUTINE_CLOSURE(v)      (VAL_ROUTINE_INFO(v)->info.cb.closure)
#define VAL_ROUTINE_DISPATCHER(v)   (VAL_ROUTINE_INFO(v)->info.cb.dispatcher)
#define VAL_CALLBACK_FUNC(v)        (VAL_ROUTINE_INFO(v)->info.cb.func)


/***********************************************************************
**
**  TYPESET - Collection of up to 64 types
**
**  Though available to the user to manipulate directly as a TYPESET!,
**  REBVALs of this type have another use in describing the fields of
**  objects or parameters of function frames.  When used for that
**  purpose, they not only list the legal types...but also hold a
**  symbol for naming the field or parameter.
**
**  !!! At present, a TYPESET! created with MAKE TYPESET! cannot set
**  the internal symbol.  Nor can it set the extended flags, though
**  that might someday be allowed with a syntax like:
**
**      make typeset! [<hide> <quoted> string! integer!]
**
***********************************************************************/

// Option flags used with VAL_GET_EXT().  These describe properties of
// a value slot when it's constrained to the types in the typeset
enum {
    EXT_TYPESET_QUOTE = 0,  // Quoted (REDUCE paren/get-word|path if EVALUATE)
    EXT_TYPESET_EVALUATE,   // DO/NEXT performed at callsite when setting
    EXT_TYPESET_REFINEMENT, // Value indicates an optional switch
    EXT_WORD_LOCK,  // Can't be changed (set with PROTECT)
    EXT_WORD_HIDE,      // Can't be reflected (set with PROTECT/HIDE)
    EXT_TYPESET_MAX
};

struct Reb_Typeset {
    REBCNT sym;         // Symbol (if a key of object or function param)

    // Note: `sym` is first so that the value's 32-bit Reb_Flags header plus
    // the 32-bit REBCNT will pad `bits` to a REBU64 alignment boundary

    REBU64 bits;        // One bit for each DATATYPE! (use with FLAGIT_64)
};

// Operations when typeset is done with a bitset (currently all typesets)

#define VAL_TYPESET_BITS(v) ((v)->data.typeset.bits)

#define TYPE_CHECK(v,n) \
    ((VAL_TYPESET_BITS(v) & FLAGIT_64(n)) != 0)

#define TYPE_SET(v,n) \
    ((VAL_TYPESET_BITS(v) |= FLAGIT_64(n)), NOOP)

#define EQUAL_TYPESET(v,w) \
    (VAL_TYPESET_BITS(v) == VAL_TYPESET_BITS(w))

// Symbol is SYM_0 unless typeset in object keylist or func paramlist

#ifdef NDEBUG
    #define VAL_TYPESET_SYM(v) ((v)->data.typeset.sym)
#else
    // !!! Due to large reorganizations, it may be that VAL_WORD_SYM and
    // VAL_TYPESET_SYM calls were swapped.  In the aftermath of reorganization
    // this check is prudent (until further notice...)
    #define VAL_TYPESET_SYM(v) (*Val_Typeset_Sym_Ptr_Debug(v))
#endif

#define VAL_TYPESET_CANON(v) \
    VAL_SYM_CANON(BLK_SKIP(PG_Word_Table.series, VAL_TYPESET_SYM(v)))

// Word number array (used by Bind_Table):
#define WORDS_HEAD(w) \
    cast(REBINT *, (w)->data)

#define WORDS_LAST(w) \
    (WORDS_HEAD(w) + (w)->tail - 1) // (tail never zero)


/***********************************************************************
**
**  REBVAL (a.k.a. struct Reb_Value)
**
**      The structure/union for all REBOL values. It is designed to
**      be four C pointers in size (so 16 bytes on 32-bit platforms
**      and 32 bytes on 64-bit platforms).  Operation will be most
**      efficient with those nice even sizes, but the rest of the
**      code in the system should be able to work even if the size
**      turns out to be different.
**
**      Of the four 16/32 bit slots that each value has, one of them
**      is used for the value's "Flags".  This includes the data
**      type, such as REB_INTEGER, REB_BLOCK, REB_STRING, etc.  Then
**      there are 8 bits which are for general purposes that could
**      apply equally well to any type of value (including whether
**      the value should have a new-line after it when molded out
**      inside of a block).  There are 8 bits which are custom to
**      each type--for instance whether a function is infix or not.
**      Then there are 8 bits reserved for future use.
**
**      (Technically speaking this means a 64-bit build has an
**      extra 32-bit value it might find a use for.  But it's hard
**      to think of what feature you'd empower specifically on a
**      64-bit builds.)
**
**      The remaining three pointer-sized things are used to hold
**      whatever representation that value type needs to express
**      itself.  Perhaps obviously, an arbitrarily long string will
**      not fit into 3*32 bits, or even 3*64 bits!  You can fit the
**      data for an INTEGER or DECIMAL in that, but not a BLOCK
**      or a FUNCTION (for instance).  So those pointers are used
**      to point to things, and often they will point to one or
**      more Rebol Series (REBSER).
**
***********************************************************************/

// Reb_All is a structure type designed specifically for getting at
// the underlying bits of whichever union member is in effect inside
// the Reb_Value_Data.  This is in order to hash the values in a
// generic way that can use the bytes and doesn't have to be custom
// to each type.  Though many traditional methods of doing this "type
// punning" might generate arbitrarily broken code, this is being
// done through a union, for which C99 expanded the "legal" uses:
//
//     http://stackoverflow.com/questions/11639947/i
//
// !!! Why is Reb_All defined this weird way?
//
struct Reb_All {
#if defined(__LP64__) || defined(__LLP64__)
    REBCNT bits[6];
    REBINT padding; //make sizeof(REBVAL) 32 bytes
#else
    REBCNT bits[3];
#endif
};

#define VAL_ALL_BITS(v) ((v)->data.all.bits)

union Reb_Value_Data {
    struct Reb_Word word;
    struct Reb_Position position; // ANY-STRING!, ANY-ARRAY!, BINARY!, VECTOR!
    REBCNT rebcnt;
    REBI64 integer;
    REBU64 unteger;
    REBDEC decimal; // actually a C 'double', typically 64-bit
    REBUNI character; // It's CHAR! (for now), but 'char' is a C keyword
    struct Reb_Datatype datatype;
    struct Reb_Typeset typeset;
    struct Reb_Symbol symbol;
    struct Reb_Time time;
    struct Reb_Tuple tuple;
    struct Reb_Function func;
    struct Reb_Context context; // ERROR!, OBJECT!, PORT!, MODULE!, (TASK!?)
    struct Reb_Pair pair;
    struct Reb_Event event;
    struct Reb_Library library;
    struct Reb_Struct structure; // It's STRUCT!, but 'struct' is a C keyword
    struct Reb_Gob gob;
    struct Reb_Money money;
    struct Reb_Handle handle;
    struct Reb_All all;
#ifndef NDEBUG
    struct Reb_Trash trash; // not an actual Rebol value type; debug only
#endif
};

struct Reb_Value
{
    union Reb_Value_Flags flags;
    union Reb_Value_Data data;
};

#pragma pack()

#endif // value.h

