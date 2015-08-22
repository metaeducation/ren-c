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
**	REBOL Value Type
**
**		This is used for all REBOL values. This is a forward
**		declaration. See end of this file for actual structure.
**
***********************************************************************/

#pragma pack(4)

// Note: b-init.c verifies that lower 8 bits is flags.type
union Reb_Value_Flags {
	struct {
	#ifdef ENDIAN_LITTLE
		unsigned type:8;	// datatype
		unsigned opts:8;	// special options
		unsigned exts:8;	// extensions to datatype
		unsigned resv:8;	// reserved for future
	#else
		unsigned resv:8;	// reserved for future
		unsigned exts:8;	// extensions to datatype
		unsigned opts:8;	// special options
		unsigned type:8;	// datatype
	#endif
	} bitfields;

	REBCNT all;				// for setting all the flags at once
};

struct Reb_Value;
typedef struct Reb_Value REBVAL;
typedef struct Reb_Series REBSER;

// Value type identifier (generally, should be handled as integer):

// get and set only the type (not flags)
#define VAL_TYPE(v)		cast(enum Reb_Kind, (v)->flags.bitfields.type)
#define SET_TYPE(v,t)	((v)->flags.bitfields.type = (t))

// set type, clear all flags
#define VAL_SET(v,t)	((v)->flags.all = (t))

// !!! Questionable idea: does setting all bytes to zero of a type
// and then poking in a type indicator make the "zero valued"
// version of that type that you can compare against?  :-/
#define VAL_SET_ZEROED(v,t) (CLEAR((v), sizeof(REBVAL)), VAL_SET((v),(t)))

// Clear type identifier:
#define SET_END(v)			VAL_SET(v, 0)

// Value option flags:
enum {
	OPT_VALUE_LINE = 0,	// Line break occurs before this value
	OPT_VALUE_THROWN,	// Value is /NAME of a THROW (arg via THROWN_ARG)
	OPT_VALUE_MAX
};

#define VAL_OPTS_DATA(v)	((v)->flags.bitfields.opts)
#define VAL_SET_OPT(v,n)	SET_FLAG(VAL_OPTS_DATA(v), n)
#define VAL_GET_OPT(v,n)	GET_FLAG(VAL_OPTS_DATA(v), n)
#define VAL_CLR_OPT(v,n)	CLR_FLAG(VAL_OPTS_DATA(v), n)

// Used for 8 datatype-dependent flags (or one byte-sized data value)
#define VAL_EXTS_DATA(v)	((v)->flags.bitfields.exts)
#define VAL_SET_EXT(v,n)	SET_FLAG(VAL_EXTS_DATA(v), n)
#define VAL_GET_EXT(v,n)	GET_FLAG(VAL_EXTS_DATA(v), n)
#define VAL_CLR_EXT(v,n)	CLR_FLAG(VAL_EXTS_DATA(v), n)

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
// will occur exactly once (when it is caught).  Moreover, it is done
// this way because historically Rebol used an ERROR! where the name
// lived for this purpose.  The spot available in the error value only
// afforded only 32 bits, which could only hold a symbol ID and hence
// the name was presumed as corresponding to an unbound WORD!

#ifdef NDEBUG
	#define CONVERT_NAME_TO_THROWN(name,arg) \
		do { \
			VAL_SET_OPT((name), OPT_VALUE_THROWN); \
			(*TASK_THROWN_ARG = *(arg)); \
		} while (0)

	#define TAKE_THROWN_ARG(arg,thrown) \
		do { \
			assert(VAL_GET_OPT((thrown), OPT_VALUE_THROWN)); \
			VAL_CLR_OPT((thrown), OPT_VALUE_THROWN); \
			(*(arg) = *TASK_THROWN_ARG); \
		} while (0)
#else
	#define CONVERT_NAME_TO_THROWN(n,a)		Convert_Name_To_Thrown_Debug(n, a)
	#define TAKE_THROWN_ARG(a,t)			Take_Thrown_Arg_Debug(a, t)
#endif

#define THROWN(v)			(VAL_GET_OPT((v), OPT_VALUE_THROWN))


#define	IS_SET(v)			(VAL_TYPE(v) > REB_UNSET)
#define IS_SCALAR(v)		(VAL_TYPE(v) <= REB_DATE)


/***********************************************************************
**
**	DATATYPE - Datatype or pseudo-datatype
**
**	!!! Consider rename to TYPE! once legacy TYPE? calls have been
**	converted to TYPE-OF.  Also consider a model where there are
**	user types, and hence TYPE? may be able to return more than just
**	one out of a set of 64 things.
**
***********************************************************************/

struct Reb_Datatype {
	enum Reb_Kind kind;
	REBSER  *spec;
//	REBINT	min_type;
//	REBINT	max_type;
};

#define	VAL_TYPE_KIND(v)		((v)->data.datatype.kind)
#define	VAL_TYPE_SPEC(v)	((v)->data.datatype.spec)

//#define	VAL_MIN_TYPE(v)	((v)->data.datatype.min_type)
//#define	VAL_MAX_TYPE(v)	((v)->data.datatype.max_type)


/***********************************************************************
**
**	TRASH - Trash Value used in debugging cases where a cell is
**	expected to be overwritten.  The operations are available in
**	debug and release builds, except release builds cannot use
**	the IS_TRASH() test.  (Hence trash is not a real datatype,
**	just an invalid bit pattern used to mark value cells.)
**
**	Because the trash value saves the filename and line where it
**	originated, the REBVAL has that info under the debugger.
**
***********************************************************************/

#ifdef NDEBUG
	#define SET_TRASH(v) NOOP

	#define SET_TRASH_SAFE(v) SET_UNSET(v)
#else
	struct Reb_Trash {
		REBOOL safe; // if "safe" then will be UNSET! in a release build
		const char *filename;
		int line;
	};

	#define IS_TRASH(v) (VAL_TYPE(v) == REB_MAX + 1)

	#define VAL_TRASH_SAFE(v) ((v)->data.trash.safe)

	#define SET_TRASH(v) \
		( \
			VAL_SET((v), REB_MAX + 1), \
			(v)->data.trash.safe = FALSE, \
			(v)->data.trash.filename = __FILE__, \
			(v)->data.trash.line = __LINE__, \
			cast(void, 0) \
		)

	#define SET_TRASH_SAFE(v) \
		( \
			VAL_SET((v), REB_MAX + 1), \
			(v)->data.trash.safe = TRUE, \
			(v)->data.trash.filename = __FILE__, \
			(v)->data.trash.line = __LINE__, \
			cast(void, 0) \
		)
#endif


/***********************************************************************
**
**	NUMBERS - Integer and other simple scalars
**
***********************************************************************/

#define	SET_UNSET(v)	VAL_SET(v, REB_UNSET)
#define UNSET_VALUE		ROOT_UNSET_VAL

#define	SET_NONE(v)		VAL_SET(v, REB_NONE)
#define NONE_VALUE		ROOT_NONE_VAL

#define EMPTY_SERIES	VAL_SERIES(ROOT_EMPTY_BLOCK)

#define VAL_INT32(v)	(REBINT)((v)->data.integer)
#define VAL_INT64(v)	((v)->data.integer)
#define VAL_UNT64(v)	((v)->data.unteger)
#define	SET_INTEGER(v,n) VAL_SET(v, REB_INTEGER), ((v)->data.integer) = (n)
#define	SET_INT32(v,n)  ((v)->data.integer) = (REBINT)(n)

#define MAX_CHAR		0xffff
#define VAL_CHAR(v)		((v)->data.character)
#define SET_CHAR(v,n) \
	(VAL_SET((v), REB_CHAR), VAL_CHAR(v) = (n), NOOP)

#define IS_NUMBER(v)	(VAL_TYPE(v) == REB_INTEGER || VAL_TYPE(v) == REB_DECIMAL)


/***********************************************************************
**
**	DECIMAL -- Implementation-wise, a 'double'-precision floating
**	point number in C (typically 64-bit).
**
***********************************************************************/

#define VAL_DECIMAL(v)	((v)->data.decimal)
#define	SET_DECIMAL(v,n) VAL_SET(v, REB_DECIMAL), VAL_DECIMAL(v) = (n)


/***********************************************************************
**
**	MONEY -- Includes denomination and amount
**
**	!!! The naming of "deci" used by MONEY! as "decimal" is a very
**	bad overlap with DECIMAL! and also not very descriptive of what
**	the properties of a "deci" are.  Also, to be a useful money
**	abstraction it should store the currency type, e.g. the three
**	character ISO 4217 code (~15 bits to store)
**
**		https://en.wikipedia.org/wiki/ISO_4217
**
***********************************************************************/

struct Reb_Money {
	deci amount;
};

#define VAL_MONEY_AMOUNT(v)		((v)->data.money.amount)
#define SET_MONEY_AMOUNT(v,n) \
	(VAL_SET((v), REB_MONEY), VAL_MONEY_AMOUNT(v) = (n), NOOP)


/***********************************************************************
**
**	DATE and TIME
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
	REBI64 time;	// time in nanoseconds
	REBDAT date;
};

#define VAL_TIME(v)	((v)->data.time.time)
#define TIME_SEC(n)	((REBI64)(n) * 1000000000L)

#define MAX_SECONDS	(((i64)1<<31)-1)
#define MAX_HOUR	(MAX_SECONDS / 3600)
#define MAX_TIME	((REBI64)MAX_HOUR * HR_SEC)

#define NANO		1.0e-9
#define	SEC_SEC		((REBI64)1000000000L)
#define MIN_SEC		(60 * SEC_SEC)
#define HR_SEC		(60 * 60 * SEC_SEC)

#define SEC_TIME(n)  ((n) * SEC_SEC)
#define MIN_TIME(n)  ((n) * MIN_SEC)
#define HOUR_TIME(n) ((n) * HR_SEC)

#define SECS_IN(n) ((n) / SEC_SEC)
#define VAL_SECS(n) (VAL_TIME(n) / SEC_SEC)

#define DEC_TO_SECS(n) (i64)(((n) + 5.0e-10) * SEC_SEC)

#define SECS_IN_DAY 86400
#define TIME_IN_DAY (SEC_TIME((i64)SECS_IN_DAY))

#define NO_TIME		MIN_I64

#define	MAX_YEAR		0x3fff

#define VAL_DATE(v)		((v)->data.time.date)
#define VAL_YEAR(v)		((v)->data.time.date.date.year)
#define VAL_MONTH(v)	((v)->data.time.date.date.month)
#define VAL_DAY(v)		((v)->data.time.date.date.day)
#define VAL_ZONE(v)		((v)->data.time.date.date.zone)

#define ZONE_MINS 15
#define ZONE_SECS (ZONE_MINS*60)
#define MAX_ZONE (15 * (60/ZONE_MINS))


/***********************************************************************
**
**	TUPLE
**
***********************************************************************/

typedef struct Reb_Tuple {
	REBYTE tuple[12];
} REBTUP;

#define	VAL_TUPLE(v)	((v)->data.tuple.tuple+1)
#define	VAL_TUPLE_LEN(v) ((v)->data.tuple.tuple[0])
#define MAX_TUPLE 10


/***********************************************************************
**
**	PAIR
**
***********************************************************************/

#define	VAL_PAIR(v)		((v)->data.pair)
#define	VAL_PAIR_X(v)	((v)->data.pair.x)
#define	VAL_PAIR_Y(v) 	((v)->data.pair.y)
#define SET_PAIR(v,x,y)	(VAL_SET(v, REB_PAIR),VAL_PAIR_X(v)=(x),VAL_PAIR_Y(v)=(y))
#define	VAL_PAIR_X_INT(v) ROUND_TO_INT((v)->data.pair.x)
#define	VAL_PAIR_Y_INT(v) ROUND_TO_INT((v)->data.pair.y)


/***********************************************************************
**
**	EVENT
**
***********************************************************************/

#define	VAL_EVENT_TYPE(v)	((v)->data.event.type)  //(VAL_EVENT_INFO(v) & 0xff)
#define	VAL_EVENT_FLAGS(v)	((v)->data.event.flags) //((VAL_EVENT_INFO(v) >> 16) & 0xff)
#define	VAL_EVENT_WIN(v)	((v)->data.event.win)   //((VAL_EVENT_INFO(v) >> 24) & 0xff)
#define	VAL_EVENT_MODEL(v)	((v)->data.event.model)
#define	VAL_EVENT_DATA(v)	((v)->data.event.data)
#define	VAL_EVENT_TIME(v)	((v)->data.event.time)
#define	VAL_EVENT_REQ(v)	((v)->data.event.eventee.req)

// !!! Because 'eventee.ser' is exported to clients who may not have the full
// definitions of Rebol's internal types like REBSER available, it is defined
// as a 'void*'.  This "dereference a cast of an address as a double-pointer"
// trick allows us to use VAL_EVENT_SER on the left hand of an assignment,
// but means that 'v' cannot be const to use this on the right hand side.
// An m_cast will have to be used in those cases (or split up this macro)
#define	VAL_EVENT_SER(v) \
	(*cast(REBSER **, &(v)->data.event.eventee.ser))

#define IS_EVENT_MODEL(v,f)	(VAL_EVENT_MODEL(v) == (f))

#define SET_EVENT_INFO(val, type, flags, win) \
	VAL_EVENT_TYPE(val)=type, VAL_EVENT_FLAGS(val)=flags, VAL_EVENT_WIN(val)=win
	//VAL_EVENT_INFO(val) = (type | (flags << 16) | (win << 24))

#define	VAL_EVENT_X(v)		((REBINT) (short) (VAL_EVENT_DATA(v) & 0xffff))
#define	VAL_EVENT_Y(v) 		((REBINT) (short) ((VAL_EVENT_DATA(v) >> 16) & 0xffff))
#define VAL_EVENT_XY(v)		(VAL_EVENT_DATA(v))
#define SET_EVENT_XY(v,x,y) VAL_EVENT_DATA(v) = ((y << 16) | (x & 0xffff))

#define	VAL_EVENT_KEY(v)	(VAL_EVENT_DATA(v) & 0xffff)
#define	VAL_EVENT_KCODE(v)	((VAL_EVENT_DATA(v) >> 16) & 0xffff)
#define SET_EVENT_KEY(v,k,c) VAL_EVENT_DATA(v) = ((c << 16) + k)

#define	IS_KEY_EVENT(type)	0

#ifdef old_code
#define	TO_EVENT_XY(x,y)	(((y)<<16)|((x)&0xffff))
#define	SET_EVENT_INFO(v,t,k,c,w,f)	((VAL_FLAGS(v)=(VAL_FLAGS(v)&0x0f)|((f)&0xf0)),\
									(VAL_EVENT_INFO(v)=(((t)&0xff)|(((k)&0xff)<<8)|\
									(((c)&0xff)<<16)|(((w)&0xff)<<24))))
#endif


/***********************************************************************
**
*/	struct Reb_Series
/*
**		Series header points to data and keeps track of tail and size.
**		Additional fields can be used for attributes and GC. Every
**		string and block in REBOL uses one of these to permit GC
**		and compaction.
**
***********************************************************************/
{
	REBYTE	*data;		// series data head
#ifdef SERIES_LABELS
	const REBYTE  *label;		// identify the series
#endif

	REBCNT	tail;		// one past end of useful data
	REBCNT	rest;		// total number of units from bias to end
	REBINT	info;		// holds width and flags
#if defined(__LP64__) || defined(__LLP64__)
	REBCNT padding; /* make next pointer is naturally aligned */
#endif
	union {
		REBCNT size;	// used for vectors and bitsets
		REBSER *series;	// MAP datatype uses this
		struct {
			REBCNT wide:16;
			REBCNT high:16;
		} area;
		REBUPT all; /* for copying, must have the same size as the union */
	} extra;

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

#define SERIES_TAIL(s)	 ((s)->tail)
#define SERIES_REST(s)	 ((s)->rest)
#define	SERIES_LEN(s)    ((s)->tail + 1) // Includes terminator
#define	SERIES_FLAGS(s)	 ((s)->info)
#define	SERIES_WIDE(s)	 (((s)->info) & 0xff)
#define SERIES_DATA(s)   ((s)->data)
#define	SERIES_SKIP(s,i) (SERIES_DATA(s) + (SERIES_WIDE(s) * i))

// These flags are returned from Do_Next_Core and DO_NEXT_MAY_THROW, in
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
#define	SERIES_FREED(s)  (!SERIES_WIDE(s))

// Size in bytes of memory allocated (including bias area):
#define SERIES_TOTAL(s) ((SERIES_REST(s) + SERIES_BIAS(s)) * (REBCNT)SERIES_WIDE(s))
// Size in bytes of series (not including bias area):
#define	SERIES_SPACE(s) (SERIES_REST(s) * (REBCNT)SERIES_WIDE(s))
// Size in bytes being used, including terminator:
#define SERIES_USED(s) (SERIES_LEN(s) * SERIES_WIDE(s))

// Optimized expand when at tail (but, does not reterminate)
#define EXPAND_SERIES_TAIL(s,l) if (SERIES_FITS(s, l)) s->tail += l; else Expand_Series(s, AT_TAIL, l)
#define RESIZE_SERIES(s,l) s->tail = 0; if (!SERIES_FITS(s, l)) Expand_Series(s, AT_TAIL, l); s->tail = 0
#define RESET_SERIES(s) s->tail = 0; TERM_SERIES(s)
#define RESET_TAIL(s) s->tail = 0

// Clear all and clear to tail:
#define CLEAR_SERIES(s) CLEAR(SERIES_DATA(s), SERIES_SPACE(s))
#define ZERO_SERIES(s) memset(SERIES_DATA(s), 0, SERIES_USED(s))
#define TERM_SERIES(s) memset(SERIES_SKIP(s, SERIES_TAIL(s)), 0, SERIES_WIDE(s))

// Returns space that a series has available (less terminator):
#define SERIES_FULL(s) (SERIES_LEN(s) >= SERIES_REST(s))
#define SERIES_AVAIL(s) (SERIES_REST(s) - SERIES_LEN(s))
#define SERIES_FITS(s,n) ((SERIES_TAIL(s) + (REBCNT)(n) + 1) < SERIES_REST(s))

// Flag used for extending series at tail:
#define	AT_TAIL	((REBCNT)(~0))	// Extend series at tail

// Is it a byte-sized series? (this works because no other odd size allowed)
#define BYTE_SIZE(s) (((s)->info) & 1)
#define VAL_BYTE_SIZE(v) (BYTE_SIZE(VAL_SERIES(v)))
#define VAL_STR_IS_ASCII(v) (VAL_BYTE_SIZE(v) && !Is_Not_ASCII(VAL_BIN_DATA(v), VAL_LEN(v)))

// Bias is empty space in front of head:
#define	SERIES_BIAS(s)	   (REBCNT)((SERIES_FLAGS(s) >> 16) & 0xffff)
#define MAX_SERIES_BIAS    0x1000
#define SERIES_SET_BIAS(s,b) (SERIES_FLAGS(s) = (SERIES_FLAGS(s) & 0xffff) | (b << 16))
#define SERIES_ADD_BIAS(s,b) (SERIES_FLAGS(s) += (b << 16))
#define SERIES_SUB_BIAS(s,b) (SERIES_FLAGS(s) -= (b << 16))

// Series Flags:
enum {
	SER_MARK		= 1 << 0,	// was found during GC mark scan.
	SER_KEEP		= 1 << 1,	// don't garbage collect even if unreferenced
	SER_LOCK		= 1 << 2,	// size is locked (do not expand it)
	SER_EXTERNAL	= 1 << 3,	// ->data is external, don't free() on GC
	SER_FLAG_4		= 1 << 4,	// UNUSED: was SER_FREE, but free if wide = 0
	SER_BLOCK		= 1 << 5,	// is sizeof(REBVAL) wide and has valid values
	SER_PROT		= 1 << 6,	// protected from modification
	SER_MON			= 1 << 7	// !!! Monitoring (?)
};

#define SERIES_SET_FLAG(s, f) cast(void, (SERIES_FLAGS(s) |= ((f) << 8)))
#define SERIES_CLR_FLAG(s, f) cast(void, (SERIES_FLAGS(s) &= ~((f) << 8)))
#define SERIES_GET_FLAG(s, f) (0 != (SERIES_FLAGS(s) & ((f) << 8)))

#define	IS_FREEABLE(s)    !SERIES_GET_FLAG(s, SER_MARK|SER_KEEP)
#define KEEP_SERIES(s,l)  do {SERIES_SET_FLAG(s, SER_KEEP); LABEL_SERIES(s,l);} while(0)
#define LOCK_SERIES(s)    SERIES_SET_FLAG(s, SER_LOCK)
#define IS_LOCK_SERIES(s) SERIES_GET_FLAG(s, SER_LOCK)
#define IS_BLOCK_SERIES(s) SERIES_GET_FLAG((s), SER_BLOCK)
#define PROTECT_SERIES(s) SERIES_SET_FLAG(s, SER_PROT)
#define UNPROTECT_SERIES(s)  SERIES_CLR_FLAG(s, SER_PROT)
#define IS_PROTECT_SERIES(s) SERIES_GET_FLAG(s, SER_PROT)

#define TRAP_PROTECT(s) if (IS_PROTECT_SERIES(s)) Trap_DEAD_END(RE_PROTECTED)

#ifdef SERIES_LABELS
#define LABEL_SERIES(s,l) s->label = (l)
#else
#define LABEL_SERIES(s,l)
#endif

#ifdef NDEBUG
	#define ASSERT_SERIES(s) cast(void, 0)
#else
	#define ASSERT_SERIES(s) Assert_Series_Core((s), REB_END)
#endif

#ifdef NDEBUG
	#define ASSERT_TYPED_SERIES(s,t) cast(void, 0)
#else
	#define ASSERT_TYPED_SERIES(s,t) Assert_Series_Core((s), (t))
#endif

#ifdef NDEBUG
	#define ASSERT_SERIES_TERM(s) cast(void, 0)
#else
	#define ASSERT_SERIES_TERM(s) Assert_Series_Term_Core(s)
#endif


//#define LABEL_SERIES(s,l) s->label = (l)

// !!! Remove if not used after port:
//#define	SERIES_SIDE(s)	 ((s)->link.side)
//#define	SERIES_FRAME(s)	 ((s)->link.frame)
//#define SERIES_NOT_REBOLS(s) SERIES_SET_FLAG(s, SER_XLIB)


/***********************************************************************
**
**	SERIES -- Generic series macros
**
***********************************************************************/

#pragma pack()
#include "reb-gob.h"
#pragma pack(4)

struct Reb_Position
{
	REBSER	*series;
	REBCNT	index;
};

#define VAL_SERIES(v)	    ((v)->data.position.series)
#define VAL_INDEX(v)	    ((v)->data.position.index)
#define	VAL_TAIL(v)		    (VAL_SERIES(v)->tail)
#define VAL_LEN(v)			(Val_Series_Len(v))

#define VAL_DATA(s)			(VAL_BIN_HEAD(s) + (VAL_INDEX(s) * VAL_SERIES_WIDTH(s)))

#define VAL_SERIES_WIDTH(v) (SERIES_WIDE(VAL_SERIES(v)))
#define VAL_LIMIT_SERIES(v) if (VAL_INDEX(v) > VAL_TAIL(v)) VAL_INDEX(v) = VAL_TAIL(v)

#define DIFF_PTRS(a,b) (REBCNT)((REBYTE*)a - (REBYTE*)b)


/***********************************************************************
**
**	STRINGS -- All string related values
**
***********************************************************************/

#define SET_STR_TYPE(t,v,s) \
	(VAL_SET((v), (t)), VAL_SERIES(v) = (s), VAL_INDEX(v) = 0, NOOP)

#define SET_STRING(v,s)		SET_STR_TYPE(REB_STRING, (v), (s))
#define SET_BINARY(v,s)		SET_STR_TYPE(REB_BINARY, (v), (s))
#define SET_FILE(v,s)		SET_STR_TYPE(REB_FILE, (v), (s))

#define SET_STR_END(s,n) (*STR_SKIP(s,n) = 0)

// Arg is a binary (byte) series:
#define	BIN_HEAD(s)		((REBYTE *)((s)->data))
#define BIN_DATA(s)		((REBYTE *)((s)->data))
#define	BIN_TAIL(s)		(REBYTE*)STR_TAIL(s)
#define BIN_SKIP(s, n)	(((REBYTE *)((s)->data))+(n))
#define	BIN_LEN(s)		(SERIES_TAIL(s))

// Arg is a unicode series:
#define UNI_HEAD(s)		((REBUNI *)((s)->data))
#define UNI_SKIP(s, n)	(((REBUNI *)((s)->data))+(n))
#define UNI_TAIL(s)		(((REBUNI *)((s)->data))+(s)->tail)
#define	UNI_LAST(s)		(((REBUNI *)((s)->data))+((s)->tail-1)) // make sure tail not zero
#define	UNI_LEN(s)		(SERIES_TAIL(s))
#define UNI_TERM(s)		(*UNI_TAIL(s) = 0)
#define UNI_RESET(s)	(UNI_HEAD(s)[(s)->tail = 0] = 0)

// Obsolete (remove after Unicode conversion):
#define STR_HEAD(s)		((REBYTE *)((s)->data))
#define STR_DATA(s)		((REBYTE *)((s)->data))
#define STR_SKIP(s, n)	(((REBYTE *)((s)->data))+(n))
#define STR_TAIL(s)		(((REBYTE *)((s)->data))+(s)->tail)
#define	STR_LAST(s)		(((REBYTE *)((s)->data))+((s)->tail-1)) // make sure tail not zero
#define	STR_LEN(s)		(SERIES_TAIL(s))
#define STR_TERM(s)		(*STR_TAIL(s) = 0)
#define STR_RESET(s)	(STR_HEAD(s)[(s)->tail = 0] = 0)

// Arg is a binary value:
#define VAL_BIN(v)		BIN_HEAD(VAL_SERIES(v))
#define VAL_BIN_HEAD(v)	BIN_HEAD(VAL_SERIES(v))
#define VAL_BIN_DATA(v)	BIN_SKIP(VAL_SERIES(v), VAL_INDEX(v))
#define VAL_BIN_SKIP(v,n) BIN_SKIP(VAL_SERIES(v), (n))
#define VAL_BIN_TAIL(v)	BIN_SKIP(VAL_SERIES(v), VAL_SERIES(v)->tail)

// Arg is a unicode value:
#define VAL_UNI(v)		UNI_HEAD(VAL_SERIES(v))
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

//#define VAL_STR_LAST(v)	STR_LAST(VAL_SERIES(v))
//#define	VAL_MEM_LEN(v)	(VAL_TAIL(v) * VAL_SERIES_WIDTH(v))


/***********************************************************************
**
**	IMAGES, QUADS - RGBA
**
***********************************************************************/

//typedef struct Reb_ImageInfo
//{
//	REBCNT width;
//	REBCNT height;
//	REBINT transp;
//} REBIMI;

#define QUAD_HEAD(s)	((REBYTE *)((s)->data))
#define QUAD_SKIP(s,n)	(((REBYTE *)((s)->data))+(n * 4))
#define QUAD_TAIL(s)	(((REBYTE *)((s)->data))+((s)->tail * 4))
#define	QUAD_LEN(s)		(SERIES_TAIL(s))

#define	IMG_SIZE(s)		((s)->extra.size)
#define	IMG_WIDE(s)		((s)->extra.area.wide)
#define	IMG_HIGH(s)		((s)->extra.area.high)
#define IMG_DATA(s)		((REBYTE *)((s)->data))

#define VAL_IMAGE_HEAD(v)	QUAD_HEAD(VAL_SERIES(v))
#define VAL_IMAGE_TAIL(v)	QUAD_SKIP(VAL_SERIES(v), VAL_SERIES(v)->tail)
#define VAL_IMAGE_DATA(v)	QUAD_SKIP(VAL_SERIES(v), VAL_INDEX(v))
#define VAL_IMAGE_BITS(v)	((REBCNT *)VAL_IMAGE_HEAD((v)))
#define	VAL_IMAGE_WIDE(v)	(IMG_WIDE(VAL_SERIES(v)))
#define	VAL_IMAGE_HIGH(v)	(IMG_HIGH(VAL_SERIES(v)))
#define	VAL_IMAGE_LEN(v)	VAL_LEN(v)

#define SET_IMAGE(v,s) VAL_SET(v, REB_IMAGE);VAL_SERIES(v)=s;VAL_INDEX(v) = 0;


//#define VAL_IMAGE_TRANSP(v) (VAL_IMAGE_INFO(v)->transp)
//#define VAL_IMAGE_TRANSP_TYPE(v) (VAL_IMAGE_TRANSP(v)&0xff000000)
//#define VITT_UNKNOWN	0x00000000
//#define VITT_NONE		0x01000000
//#define VITT_ALPHA		0x02000000
//#define	VAL_IMAGE_DEPTH(v)	((VAL_IMAGE_INFO(v)>>24)&0x3f)
//#define VAL_IMAGE_TYPE(v)		((VAL_IMAGE_INFO(v)>>30)&3)

// New Image Datatype defines:

//tuple to image! pixel order bytes
#define TO_PIXEL_TUPLE(t) TO_PIXEL_COLOR(VAL_TUPLE(t)[0], VAL_TUPLE(t)[1], VAL_TUPLE(t)[2], \
							VAL_TUPLE_LEN(t) > 3 ? VAL_TUPLE(t)[3] : 0xff)
//tuple to RGBA bytes
#define TO_COLOR_TUPLE(t) TO_RGBA_COLOR(VAL_TUPLE(t)[0], VAL_TUPLE(t)[1], VAL_TUPLE(t)[2], \
							VAL_TUPLE_LEN(t) > 3 ? VAL_TUPLE(t)[3] : 0xff)

/***********************************************************************
**
**	Logic and Logic Bits
**
***********************************************************************/

#define VAL_LOGIC(v)	((v)->data.logic)
#define	SET_LOGIC(v,n)	VAL_SET(v, REB_LOGIC), VAL_LOGIC(v) = ((n)!=0) //, VAL_LOGIC_WORDS(v)=0
#define SET_TRUE(v)		SET_LOGIC(v, TRUE)  // compound statement
#define SET_FALSE(v)	SET_LOGIC(v, FALSE) // compound statement
#define VAL_I32(v)		((v)->data.logic)	// used for handles, etc.

// Conditional truth and falsehood allows an interpretation where a NONE! is
// a FALSE value.  These macros (like many others in the codebase) capture
// their parameters multiple times, so multiple evaluations can happen!

#define IS_CONDITIONAL_FALSE(v) \
	(IS_NONE(v) || (IS_LOGIC(v) && !VAL_LOGIC(v)))
#define IS_CONDITIONAL_TRUE(v) \
	!IS_CONDITIONAL_FALSE(v)


/***********************************************************************
**
**	BIT_SET -- Bit sets
**
***********************************************************************/

#define	VAL_BITSET(v)	VAL_SERIES(v)

#define	VAL_BIT_DATA(v)	VAL_BIN(v)

#define	SET_BIT(d,n)	((d)[(n) >> 3] |= (1 << ((n) & 7)))
#define	CLR_BIT(d,n)	((d)[(n) >> 3] &= ~(1 << ((n) & 7)))
#define	IS_BIT(d,n)		((d)[(n) >> 3] & (1 << ((n) & 7)))


/***********************************************************************
**
**	BLOCKS -- Block is a terminated string of values
**
***********************************************************************/

#define NOT_END(v)		(!IS_END(v))

// Arg is a series:
#define BLK_HEAD(s)		((REBVAL *)((s)->data))
#define BLK_SKIP(s, n)	(((REBVAL *)((s)->data))+(n))
#define	BLK_TAIL(s)		(((REBVAL *)((s)->data))+(s)->tail)
#define	BLK_LAST(s)		(((REBVAL *)((s)->data))+((s)->tail-1)) // make sure tail not zero
#define	BLK_LEN(s)		(SERIES_TAIL(s))
#define BLK_TERM(s)		SET_END(BLK_TAIL(s))
#define BLK_RESET(b)	(b)->tail = 0, SET_END(BLK_HEAD(b))

// Arg is a value:
#define VAL_BLK(v)		BLK_HEAD(VAL_SERIES(v))
#define VAL_BLK_DATA(v)	BLK_SKIP(VAL_SERIES(v), VAL_INDEX(v))
#define VAL_BLK_SKIP(v,n)	BLK_SKIP(VAL_SERIES(v), (n))
#define VAL_BLK_TAIL(v)	BLK_SKIP(VAL_SERIES(v), VAL_SERIES(v)->tail)
#define	VAL_BLK_LEN(v)	VAL_LEN(v)
#define VAL_BLK_TERM(v)	BLK_TERM(VAL_SERIES(v))

#define	COPY_VALUES(f,t,l) memcpy(t, f, (l) * sizeof(REBVAL))

#define COPY_BLK_PART(d, s, n) memcpy((d)->data, s, (n) * sizeof(REBVAL)); SERIES_TAIL(d) = n; BLK_TERM(d)

#define IS_EMPTY(v)		(VAL_INDEX(v) >= VAL_TAIL(v))

#ifdef NDEBUG
	#define ASSERT_BLK(s) cast(void, 0)
	#define ASSERT_TYPED_WORDS_BLOCK(s) cast(void, 0)
#else
	#define ASSERT_BLK(s) Assert_Blk_Core(s, FALSE)
	#define ASSERT_TYPED_WORDS_BLOCK(s) Assert_Blk_Core(s, TRUE)
#endif


/***********************************************************************
**
**	SYMBOLS -- Used only for symbol tables
**
***********************************************************************/

struct Reb_Symbol {
	REBCNT	canon;	// Index of the canonical (first) word
	REBCNT	alias;	// Index to next alias form
	REBCNT	name;	// Index into PG_Word_Names string
};

// Arg is value:
#define VAL_SYM_NINDEX(v)	((v)->data.symbol.name)
#define VAL_SYM_NAME(v)		(STR_HEAD(PG_Word_Names) + VAL_SYM_NINDEX(v))
#define VAL_SYM_CANON(v)	((v)->data.symbol.canon)
#define VAL_SYM_ALIAS(v)	((v)->data.symbol.alias)

// Return the CANON value for a symbol number:
#define SYMBOL_TO_CANON(sym) (VAL_SYM_CANON(BLK_SKIP(PG_Word_Table.series, sym)))
// Return the CANON value for a word value:
#define WORD_TO_CANON(w) (VAL_SYM_CANON(BLK_SKIP(PG_Word_Table.series, VAL_WORD_SYM(w))))

#define IS_STAR(v) (IS_WORD(v) && VAL_WORD_CANON(v) == SYM__P)


/***********************************************************************
**
**	WORDS -- All word related types
**
***********************************************************************/

// Word option flags:
enum {
	EXT_WORD_LOCK = 0,	// Lock word from modification
	EXT_WORD_TYPED,		// Word holds a typeset instead of binding
	EXT_WORD_HIDE,		// Hide the word
	EXT_WORD_MAX
};

union Reb_Word_Extra {
	// ...when EXT_WORD_TYPED
	REBU64 typebits;

	// ...when not EXT_WORD_TYPED
	struct {
		REBSER *frame;	// Frame (or VAL_FUNC_WORDS) where word is defined
		REBINT index;	// Index of word in frame (if it's not NULL)
	} binding;
};

struct Reb_Word {
	REBCNT sym;			// Index of the word's symbol (and pad for 64 bits)

	union Reb_Word_Extra extra;
};

#define IS_SAME_WORD(v, n)		(IS_WORD(v) && VAL_WORD_CANON(v) == n)

#define VAL_WORD_SYM(v)			((v)->data.word.sym)
#define VAL_WORD_INDEX(v)		((v)->data.word.extra.binding.index)
#define VAL_WORD_FRAME(v)		((v)->data.word.extra.binding.frame)
#define HAS_FRAME(v)			VAL_WORD_FRAME(v)

#ifdef NDEBUG
	#define UNBIND(v) \
		(VAL_WORD_FRAME(v)=NULL)
#else
	#define WORD_INDEX_UNBOUND MIN_I32
	#define UNBIND(v) \
		(VAL_WORD_FRAME(v)=NULL, VAL_WORD_INDEX(v)=WORD_INDEX_UNBOUND)
#endif

#define VAL_WORD_CANON(v)		VAL_SYM_CANON(BLK_SKIP(PG_Word_Table.series, VAL_WORD_SYM(v)))
#define	VAL_WORD_NAME(v)		VAL_SYM_NAME(BLK_SKIP(PG_Word_Table.series, VAL_WORD_SYM(v)))
#define	VAL_WORD_NAME_STR(v)	STR_HEAD(VAL_WORD_NAME(v))

// When words are used in frame word lists, fields get a different meaning:
#define VAL_BIND_SYM(v)			((v)->data.word.sym)
#define VAL_BIND_CANON(v)		VAL_SYM_CANON(BLK_SKIP(PG_Word_Table.series, VAL_BIND_SYM(v))) //((v)->data.wordspec.index)
#define VAL_BIND_TYPESET(v)		((v)->data.word.extra.typebits)
#define VAL_WORD_FRAME_WORDS(v) VAL_WORD_FRAME(v)->words
#define VAL_WORD_FRAME_VALUES(v) VAL_WORD_FRAME(v)->values

// Is it the same symbol? Quick check, then canon check:
#define SAME_SYM(a,b) (VAL_WORD_SYM(a)==VAL_BIND_SYM(b)||VAL_WORD_CANON(a)==VAL_BIND_CANON(b))

/***********************************************************************
**
**	Frame -- Used to bind words to values.
**
**		This type of value is used at the head of a frame block.
**		It should appear in no other place.
**
***********************************************************************/

struct Reb_Frame {
	REBSER	*words;
	REBSER	*spec;
//	REBSER	*parent;
};

// Value to frame fields:
#define	VAL_FRM_WORDS(v)	((v)->data.frame.words)
#define	VAL_FRM_SPEC(v)		((v)->data.frame.spec)
//#define	VAL_FRM_PARENT(v)	((v)->data.frame.parent)

// Word number array (used by Bind_Table):
#define WORDS_HEAD(w)		((REBINT *)(w)->data)
#define WORDS_LAST(w)		(((REBINT *)(w)->data)+(w)->tail-1) // (tail never zero)

// Frame series to frame components:
#define FRM_WORD_SERIES(c)	VAL_FRM_WORDS(BLK_HEAD(c))
#define FRM_WORDS(c)		BLK_HEAD(FRM_WORD_SERIES(c))
#define FRM_VALUES(c)		BLK_HEAD(c)
#define FRM_VALUE(c,n)		BLK_SKIP(c,(n))
#define FRM_WORD(c,n)		BLK_SKIP(FRM_WORD_SERIES(c),(n))
#define FRM_WORD_SYM(c,n)	VAL_BIND_SYM(FRM_WORD(c,n))

#define VAL_FRM_WORD(v,n)	BLK_SKIP(FRM_WORD_SERIES(VAL_OBJ_FRAME(v)),(n))

// Object field (series, index):
#define OFV(s,n)			BLK_SKIP(s,n)

#define SET_FRAME(v, s, w) \
	VAL_FRM_SPEC(v) = (s); \
	VAL_FRM_WORDS(v) = (w); \
	VAL_SET(v, REB_FRAME)

#define IS_SELFLESS(f) (VAL_BIND_SYM(FRM_WORDS(f)) == SYM_NOT_USED)


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
**	OBJECTS - Object Support
**
***********************************************************************/

struct Reb_Object {
	REBSER	*frame;
	REBSER	*body;		// module body
};

#define SET_OBJECT(v,f) \
	(VAL_SET((v), REB_OBJECT), VAL_OBJ_FRAME(v) = (f), NOOP)

#define SET_MODULE(v,f) \
	(VAL_SET((v), REB_MODULE), VAL_OBJ_FRAME(v) = (f), NOOP)

#define VAL_OBJ_FRAME(v)	((v)->data.object.frame)
#define VAL_OBJ_VALUES(v)	FRM_VALUES((v)->data.object.frame)
#define VAL_OBJ_VALUE(v,n)	FRM_VALUE((v)->data.object.frame, n)
#define VAL_OBJ_WORDS(v)	FRM_WORD_SERIES((v)->data.object.frame)
#define VAL_OBJ_WORD(v,n)	BLK_SKIP(VAL_OBJ_WORDS(v), (n))
//#define VAL_OBJ_SPEC(v)		((v)->data.object.spec)
#define	CLONE_OBJECT(c)		Copy_Series(c)

#ifdef NDEBUG
	#define ASSERT_FRAME(f) cast(void, 0)
#else
	#define ASSERT_FRAME(f) Assert_Frame_Core(f)
#endif

#define	VAL_MOD_FRAME(v)	((v)->data.object.frame)
#define VAL_MOD_BODY(v)		((v)->data.object.body)
#define VAL_MOD_SPEC(v)		VAL_FRM_SPEC(VAL_OBJ_VALUES(v))


/***********************************************************************
**
**	PORTS - External series interface
**
***********************************************************************/

#define	VAL_PORT(v)			VAL_OBJ_FRAME(v)
#define SET_PORT(v,s)		VAL_SET(v, REB_PORT), VAL_PORT(v) = s


/***********************************************************************
**
**	ERRORS - Error values (see %boot/errors.r)
**
**	Errors do double-duty as a type, because they are also used for
**	an internal pseudo-type to implement THROW/CATCH/BREAK/etc.  The
**	rationale for not making a separate THROW! type is that there
**	are only 64 bits for typesets.  Hence if an internal type can
**	be finessed another way it is.  (It also confuses the users less
**	by not seeing an internal type "leak" into their consciousness.)
**
**	The way it is decided if an error is a real ERROR! or a "THROW!" is
**	based on the value of 'num'.  Low numbers indicate that the payload
**	is a Rebol Value being thrown, and higher numbers indicate that
**	the payload is an error object frame.
**
**	For an actual THROW instruction, there is an optional piece of
**	information, which is the symbol with which the throw was "named".
**	A RETURN instruction uses its optional piece of information to
**	hold the identifying series of the stack it wishes to unwind to
**	and actually return from (for definitionally-scoped RETURN).
**
***********************************************************************/

union Reb_Error_Data {
	REBSER *frame;      // error object frame if user-facing ERROR!
	/* ... */			// THROWN() errors could put something else here
};

union Reb_Error_Extra {
	REBSER *unwind;     // identify function series to RETURN from
};

struct Reb_Error {
	// Possibly nothing in this slot (e.g. for CONTINUE)
	// Note: all user exposed errors can act like ANY-OBJECT!, hence the
	// 'frame' field must be at the same offest as Reb_Object's 'frame'.
	union Reb_Error_Data data;

	 // dictates meaning of fields above (and below)
	REBCNT num;

	// (nothing in this slot if not THROW or RETURN)
	union Reb_Error_Extra extra;
};

// Value Accessors:
#define	VAL_ERR_NUM(v)		((v)->data.error.num)
#define VAL_ERR_OBJECT(v)	((v)->data.error.data.frame)
#define VAL_ERR_UNWIND(v)   ((v)->data.error.extra.unwind)

#define VAL_ERR_VALUES(v)	cast(ERROR_OBJ*, FRM_VALUES(VAL_ERR_OBJECT(v)))
#define	VAL_ERR_ARG1(v)		(&VAL_ERR_VALUES(v)->arg1)
#define	VAL_ERR_ARG2(v)		(&VAL_ERR_VALUES(v)->arg2)

// Error Object (frame) Accessors:
#define ERR_VALUES(frame)	cast(ERROR_OBJ*, FRM_VALUES(frame))
#define	ERR_NUM(frame)		VAL_INT32(&ERR_VALUES(frame)->code)

#ifdef NDEBUG
	#define ASSERT_ERROR(e) \
		cast(void, 0)
#else
	#define ASSERT_ERROR(e) \
		Assert_Error_Debug(e)
#endif


/***********************************************************************
**
**	GOBS - Graphic Objects
**
***********************************************************************/

struct Reb_Gob {
	REBGOB *gob;
	REBCNT index;
};

#define	VAL_GOB(v)			((v)->data.gob.gob)
#define	VAL_GOB_INDEX(v)	((v)->data.gob.index)
#define SET_GOB(v,g)		VAL_SET(v, REB_GOB), VAL_GOB(v)=g, VAL_GOB_INDEX(v)=0


/***********************************************************************
**
**	FUNCTIONS - Natives, actions, operators, and user functions
**
**	NOTE: make-headers.r will skip specs with the "REBNATIVE(" in them
**	REBTYPE macros are used and expanded in tmp-funcs.h
**
***********************************************************************/

enum {
	EXT_FUNC_INFIX = 0,		// called with "infix" protocol
	EXT_FUNC_TRANSPARENT,	// no Definitionally Scoped return, ignores non-DS
	EXT_FUNC_RETURN,		// function is a definitionally scoped return
	EXT_FUNC_REDO,			// Reevaluate result value
	EXT_FUNC_MAX
};

struct Reb_Call;

// enums in C have no guaranteed size, yet Rebol wants to use known size
// types in its interfaces.  Hence REB_R is a REBCNT from reb-c.h (and not
// this enumerated type containing its legal values).
enum {
	R_OUT = 0,
	R_NONE,
	R_UNSET,
	R_TRUE,
	R_FALSE,
	R_ARG1,
	R_ARG2,
	R_ARG3
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
typedef REB_R (*REBPAF)(struct Reb_Call *call_, REBSER *p, REBCNT a);

// COMMAND! function
typedef REB_R (*CMD_FUNC)(REBCNT n, REBSER *args);

typedef struct Reb_Routine_Info REBRIN;

struct Reb_Function {
	REBSER	*spec;	// Spec block for function
	REBSER	*args;	// Block of Wordspecs (with typesets)
	union Reb_Func_Code {
		REBFUN	code;
		REBSER	*body;
		REBCNT	act;
		REBRIN	*info;
	} func;
};

/* argument to these is a pointer to struct Reb_Function */
#define FUNC_SPEC(v)	  ((v)->spec)	// a series
#define FUNC_SPEC_BLK(v)  BLK_HEAD((v)->spec)
#define FUNC_ARGS(v)	  ((v)->args)
#define FUNC_WORDS(v)     FUNC_ARGS(v)
#define FUNC_CODE(v)	  ((v)->func.code)
#define FUNC_BODY(v)	  ((v)->func.body)
#define FUNC_ACT(v)       ((v)->func.act)
#define FUNC_INFO(v)      ((v)->func.info)
#define FUNC_ARGC(v)	  SERIES_TAIL((v)->args)

// !!! In the original formulation, the first parameter in the VAL_FUNC_WORDS
// started at 1.  The zero slot was left empty, in order for the function's
// word frames to line up to object frames where the zero slot is SELF.
// The pending implementation of definitionally scoped return bumps this
// number to 2, so we establish it as a named constant anticipating that.
#define FIRST_PARAM_INDEX 1

/* argument is of type REBVAL* */
#define VAL_FUNC(v)			  ((v)->data.func)
#define VAL_FUNC_SPEC(v)	  ((v)->data.func.spec)	// a series
#define VAL_FUNC_SPEC_BLK(v)  BLK_HEAD((v)->data.func.spec)
#define VAL_FUNC_WORDS(v)     ((v)->data.func.args)

#define VAL_FUNC_NUM_WORDS(v) \
	(SERIES_TAIL(VAL_FUNC_WORDS(v)) - 1)

#define VAL_FUNC_PARAM(v,p) \
	BLK_SKIP(VAL_FUNC_WORDS(v), FIRST_PARAM_INDEX + (p) - 1)

#define VAL_FUNC_NUM_PARAMS(v) \
	(SERIES_TAIL(VAL_FUNC_WORDS(v)) - FIRST_PARAM_INDEX - 1)

#define VAL_FUNC_RETURN_WORD(v) \
	coming@soon

#define VAL_FUNC_CODE(v)	  ((v)->data.func.func.code)
#define VAL_FUNC_BODY(v)	  ((v)->data.func.func.body)
#define VAL_FUNC_ACT(v)       ((v)->data.func.func.act)
#define VAL_FUNC_INFO(v)      ((v)->data.func.func.info)

typedef struct Reb_Path_Value {
	REBVAL *value;	// modified
	REBVAL *select;	// modified
	REBVAL *path;	// modified
	REBVAL *store;  // modified (holds constructed values)
	REBVAL *setval;	// static
	const REBVAL *orig;	// static
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
**	HANDLE
**
**	Type for holding an arbitrary code or data pointer inside
**	of a Rebol data value.  What kind of function or data is not
**	known to the garbage collector, so it ignores it.
**
**	!!! Review usages of this type where they occur
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
	VAL_SET(v, REB_HANDLE), VAL_HANDLE_CODE(v) = (c)

#define SET_HANDLE_DATA(v,d) \
	VAL_SET(v, REB_HANDLE), VAL_HANDLE_DATA(v) = (d)


/***********************************************************************
**
**	LIBRARY -- External library management structures
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

#define LIB_FD(v) 			((v)->fd)
#define LIB_FLAGS(v) 		((v)->flags)

#define VAL_LIB(v)        	((v)->data.library)
#define VAL_LIB_SPEC(v)     ((v)->data.library.spec)
#define VAL_LIB_HANDLE(v) 	((v)->data.library.handle)
#define VAL_LIB_FD(v) 		((v)->data.library.handle->fd)
#define VAL_LIB_FLAGS(v) 	((v)->data.library.handle->flags)

enum {
	LIB_MARK = 1,		// library was found during GC mark scan.
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

#define IS_CLOSED_LIB(s) 	LIB_GET_FLAG(s, LIB_CLOSED)
#define CLOSE_LIB(s) 		LIB_SET_FLAG(s, LIB_CLOSED)
#define OPEN_LIB(s) 		LIB_CLR_FLAG(s, LIB_CLOSED)

/***********************************************************************
**
**	STRUCT -- C Structures
**
***********************************************************************/

typedef struct Reb_Struct {
	REBSER	*spec;
	REBSER	*fields;	// fields definition
	REBSER	*data;
} REBSTU;

#define VAL_STRUCT(v)       ((v)->data.structure)
#define VAL_STRUCT_SPEC(v)  ((v)->data.structure.spec)
#define VAL_STRUCT_FIELDS(v)  ((v)->data.structure.fields)
#define VAL_STRUCT_DATA(v)  ((v)->data.structure.data)
#define VAL_STRUCT_DP(v)    (STR_HEAD(VAL_STRUCT_DATA(v)))


/***********************************************************************
**
**	ROUTINE -- External library routine structures
**
***********************************************************************/
struct Reb_Routine_Info {
	union {
		struct {
			REBLHL	*lib;
			CFUNC *funcptr;
		} rot;
		struct {
			void *closure;
			struct Reb_Function func;
			void *dispatcher;
		} cb;
	} info;
	void	*cif;
	REBSER  *arg_types; /* index 0 is the return type, */
	REBSER	*fixed_args;
	REBSER	*all_args;
	REBSER  *arg_structs; /* for struct arguments */
	REBSER	*extra_mem; /* extra memory that needs to be free'ed */
	REBINT	abi;
	REBFLG	flags;
};

typedef struct Reb_Function REBROT;

enum {
	ROUTINE_MARK = 1,		// routine was found during GC mark scan.
	ROUTINE_USED = 1 << 1,
	ROUTINE_CALLBACK = 1 << 2, //this is a callback
	ROUTINE_VARARGS = 1 << 3 //this is a function with varargs
};

/* argument is REBFCN */
#define ROUTINE_SPEC(v)				FUNC_SPEC(v)
#define ROUTINE_INFO(v)				FUNC_INFO(v)
#define ROUTINE_ARGS(v)				FUNC_ARGS(v)
#define ROUTINE_FUNCPTR(v)			(ROUTINE_INFO(v)->info.rot.funcptr)
#define ROUTINE_LIB(v)				(ROUTINE_INFO(v)->info.rot.lib)
#define ROUTINE_ABI(v)  			(ROUTINE_INFO(v)->abi)
#define ROUTINE_FFI_ARG_TYPES(v)  	(ROUTINE_INFO(v)->arg_types)
#define ROUTINE_FIXED_ARGS(v)  		(ROUTINE_INFO(v)->fixed_args)
#define ROUTINE_ALL_ARGS(v)  		(ROUTINE_INFO(v)->all_args)
#define ROUTINE_FFI_ARG_STRUCTS(v)  (ROUTINE_INFO(v)->arg_structs)
#define ROUTINE_EXTRA_MEM(v) 		(ROUTINE_INFO(v)->extra_mem)
#define ROUTINE_CIF(v) 				(ROUTINE_INFO(v)->cif)
#define ROUTINE_RVALUE(v) 			VAL_STRUCT(BLK_HEAD(ROUTINE_FFI_ARG_STRUCTS(v)))
#define ROUTINE_CLOSURE(v)			(ROUTINE_INFO(v)->info.cb.closure)
#define ROUTINE_DISPATCHER(v)		(ROUTINE_INFO(v)->info.cb.dispatcher)
#define CALLBACK_FUNC(v)  			(ROUTINE_INFO(v)->info.cb.func)

/* argument is REBRIN */

#define RIN_FUNCPTR(v)				((v)->info.rot.funcptr)
#define RIN_LIB(v)					((v)->info.rot.lib)
#define RIN_CLOSURE(v)				((v)->info.cb.closure)
#define RIN_FUNC(v)					((v)->info.cb.func)
#define RIN_ARGS_STRUCTS(v)			((v)->arg_structs)
#define RIN_RVALUE(v)				VAL_STRUCT(BLK_HEAD(RIN_ARGS_STRUCTS(v)))

#define ROUTINE_FLAGS(s)	   ((s)->flags)
#define ROUTINE_SET_FLAG(s, f) (ROUTINE_FLAGS(s) |= (f))
#define ROUTINE_CLR_FLAG(s, f) (ROUTINE_FLAGS(s) &= ~(f))
#define ROUTINE_GET_FLAG(s, f) (ROUTINE_FLAGS(s) &  (f))

#define IS_CALLBACK_ROUTINE(s) ROUTINE_GET_FLAG(s, ROUTINE_CALLBACK)

/* argument is REBVAL */
#define VAL_ROUTINE(v)          	VAL_FUNC(v)
#define VAL_ROUTINE_SPEC(v) 		VAL_FUNC_SPEC(v)
#define VAL_ROUTINE_INFO(v) 		VAL_FUNC_INFO(v)
#define VAL_ROUTINE_ARGS(v) 		VAL_FUNC_WORDS(v)
#define VAL_ROUTINE_FUNCPTR(v)  	(VAL_ROUTINE_INFO(v)->info.rot.funcptr)
#define VAL_ROUTINE_LIB(v)  		(VAL_ROUTINE_INFO(v)->info.rot.lib)
#define VAL_ROUTINE_ABI(v)  		(VAL_ROUTINE_INFO(v)->abi)
#define VAL_ROUTINE_FFI_ARG_TYPES(v)	(VAL_ROUTINE_INFO(v)->arg_types)
#define VAL_ROUTINE_FIXED_ARGS(v)  	(VAL_ROUTINE_INFO(v)->fixed_args)
#define VAL_ROUTINE_ALL_ARGS(v)  	(VAL_ROUTINE_INFO(v)->all_args)
#define VAL_ROUTINE_FFI_ARG_STRUCTS(v)  (VAL_ROUTINE_INFO(v)->arg_structs)
#define VAL_ROUTINE_EXTRA_MEM(v) 	(VAL_ROUTINE_INFO(v)->extra_mem)
#define VAL_ROUTINE_CIF(v) 			(VAL_ROUTINE_INFO(v)->cif)
#define VAL_ROUTINE_RVALUE(v) 		VAL_STRUCT((REBVAL*)SERIES_DATA(VAL_ROUTINE_INFO(v)->arg_structs))

#define VAL_ROUTINE_CLOSURE(v)  	(VAL_ROUTINE_INFO(v)->info.cb.closure)
#define VAL_ROUTINE_DISPATCHER(v)  	(VAL_ROUTINE_INFO(v)->info.cb.dispatcher)
#define VAL_CALLBACK_FUNC(v)  		(VAL_ROUTINE_INFO(v)->info.cb.func)


/***********************************************************************
**
**	TYPESET - Collection of up to 64 types
**
***********************************************************************/

struct Reb_Typeset {
	REBCNT pad;			// Pad for U64 alignment (and common with Reb_Word)
	REBU64 typebits;	// Bitset with one bit for each DATATYPE!
};

#define VAL_TYPESET(v)  ((v)->data.typeset.typebits)
#define TYPE_CHECK(v,n) ((VAL_TYPESET(v) & ((REBU64)1 << (n))) != (REBU64)0)
#define TYPE_SET(v,n)   (VAL_TYPESET(v) |= ((REBU64)1 << (n)))
#define EQUAL_TYPESET(v,w) (VAL_TYPESET(v) == VAL_TYPESET(w))
#define TYPESET(n) ((REBU64)1 << (n))


/***********************************************************************
**
**	UTYPE - User defined types
**
***********************************************************************/

struct Reb_Utype {
	REBSER	*func;	// func object
	REBSER	*data;	// data object
};

#define VAL_UTYPE_FUNC(v)	((v)->data.utype.func)
#define VAL_UTYPE_DATA(v)	((v)->data.utype.data)



/***********************************************************************
**
**	REBVAL (a.k.a. struct Reb_Value)
**
**		The structure/union for all REBOL values. It is designed to
**		be four C pointers in size (so 16 bytes on 32-bit platforms
**		and 32 bytes on 64-bit platforms).  Operation will be most
**		efficient with those nice even sizes, but the rest of the
**		code in the system should be able to work even if the size
**		turns out to be different.
**
**		Of the four 16/32 bit slots that each value has, one of them
**		is used for the value's "Flags".  This includes the data
**		type, such as REB_INTEGER, REB_BLOCK, REB_STRING, etc.  Then
**		there are 8 bits which are for general purposes that could
**		apply equally well to any type of value (including whether
**		the value should have a new-line after it when molded out
**		inside of a block).  There are 8 bits which are custom to
**		each type--for instance whether a function is infix or not.
**		Then there are 8 bits reserved for future use.
**
**		(Technically speaking this means a 64-bit build has an
**		extra 32-bit value it might find a use for.  But it's hard
**		to think of what feature you'd empower specifically on a
**		64-bit builds.)
**
**		The remaining three pointer-sized things are used to hold
**		whatever representation that value type needs to express
**		itself.  Perhaps obviously, an arbitrarily long string will
**		not fit into 3*32 bits, or even 3*64 bits!  You can fit the
**		data for an INTEGER or DECIMAL in that, but not a BLOCK
**		or a FUNCTION (for instance).  So those pointers are used
**		to point to things, and often they will point to one or
**		more Rebol Series (REBSER).
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
	struct Reb_Position position;
	REBCNT logic;
	REBI64 integer;
	REBU64 unteger;
	REBDEC decimal; // actually a C 'double', typically 64-bit
	REBUNI character; // It's CHAR! (for now), but 'char' is a C keyword
	struct Reb_Error error;
	struct Reb_Datatype datatype;
	struct Reb_Frame frame;
	struct Reb_Typeset typeset;
	struct Reb_Symbol symbol;
	struct Reb_Time time;
	struct Reb_Tuple tuple;
	struct Reb_Function func;
	struct Reb_Object object;
	struct Reb_Pair pair;
	struct Reb_Event event;
	struct Reb_Library library;
	struct Reb_Struct structure; // It's STRUCT!, but 'struct' is a C keyword
	struct Reb_Gob gob;
	struct Reb_Utype utype;
	struct Reb_Money money;
	struct Reb_Handle handle;
	struct Reb_All all;
#ifndef NDEBUG
	struct Reb_Trash trash; // not an actual Rebol value type; debug only
#endif
};

struct Reb_Value
{
	union Reb_Value_Data data;
	union Reb_Value_Flags flags;
};

#define ANY_SERIES(v)		(VAL_TYPE(v) >= REB_BINARY && VAL_TYPE(v) <= REB_LIT_PATH)
#define ANY_STR(v)			(VAL_TYPE(v) >= REB_STRING && VAL_TYPE(v) <= REB_TAG)
#define ANY_BINSTR(v)		(VAL_TYPE(v) >= REB_BINARY && VAL_TYPE(v) <= REB_TAG)
#define ANY_BLOCK(v)		(VAL_TYPE(v) >= REB_BLOCK  && VAL_TYPE(v) <= REB_LIT_PATH)
#define	ANY_WORD(v)			(VAL_TYPE(v) >= REB_WORD   && VAL_TYPE(v) <= REB_ISSUE)
#define	ANY_PATH(v)			(VAL_TYPE(v) >= REB_PATH   && VAL_TYPE(v) <= REB_LIT_PATH)
#define ANY_FUNC(v)			(VAL_TYPE(v) >= REB_NATIVE && VAL_TYPE(v) <= REB_FUNCTION)
#define ANY_EVAL_BLOCK(v)	(VAL_TYPE(v) >= REB_BLOCK  && VAL_TYPE(v) <= REB_PAREN)
#define ANY_OBJECT(v)		(VAL_TYPE(v) >= REB_OBJECT && VAL_TYPE(v) <= REB_PORT)

#pragma pack()

#endif // value.h

