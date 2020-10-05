//
//  File: %sys-rebarr.h
//  Summary: {any-array! defs BEFORE %tmp-internals.h (see: %sys-array.h)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
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
// REBARR is an opaque type alias for REBSER.  The distinction of when a
// series node is specially chosen by having the SECOND_BYTE in the info bits
// (a.k.a. the WIDE_BYTE()) equal to zero.  This allows the info bits to
// serve as an implicit terminator if the array payload fits into the series
// node (a "singular array").
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * When checking for an ARRAY_FLAG_XXX on a series, you must be certain
//   that it is an array REBSER node...because non-arrays use the 16 bits for
//   array flags for other purposes.  An arbitrary REBSER tested for
//   ARRAY_FLAG_IS_VARLIST might alias with a UTF-8 symbol string whose symbol
//   number uses that bit.
//

struct Reb_Array {
    struct Reb_Series series;  // http://stackoverflow.com/a/9747062
};


// If a series is an array, then there are 16 free bits available for use
// in the SERIES_FLAG_XXX section.


//=//// ARRAY_FLAG_HAS_FILE_LINE_UNMASKED /////////////////////////////////=//
//
// The Reb_Series node has two pointers in it, ->link and ->misc, which are
// used for a variety of purposes (pointing to the keylist for an object,
// the C code that runs as the dispatcher for a function, etc.)  But for
// regular source series, they can be used to store the filename and line
// number, if applicable.
//
// Only arrays preserve file and line info, as UTF-8 strings need to use the
// ->misc and ->link fields for caching purposes in strings.
//
#define ARRAY_FLAG_HAS_FILE_LINE_UNMASKED \
    FLAG_LEFT_BIT(16)

#define ARRAY_MASK_HAS_FILE_LINE \
    (ARRAY_FLAG_HAS_FILE_LINE_UNMASKED | SERIES_FLAG_LINK_NODE_NEEDS_MARK)


//=//// ARRAY_FLAG_NULLEDS_LEGAL //////////////////////////////////////////=//
//
// Identifies arrays in which it is legal to have nulled elements.
// ARRAY_FLAG_IS_VARLIST also implies legality of nulleds, which in that case
// are used to represent unset variables.
//
// Note: This is a debug-only flag at this time.  At one point it was used in
// concert with reified `va_list` of values to indicate nulls that were safe
// for the evaluator, but that is now covered by QUOTED! nulls.
//
#define ARRAY_FLAG_NULLEDS_LEGAL \
    FLAG_LEFT_BIT(17)


//=//// ARRAY_FLAG_IS_PARAMLIST ///////////////////////////////////////////=//
//
// ARRAY_FLAG_IS_PARAMLIST indicates the array is the parameter list
// of a ACTION! (the first element will be a canon value of the function)
//
#define ARRAY_FLAG_IS_PARAMLIST \
    FLAG_LEFT_BIT(18)


//=//// ARRAY_FLAG_IS_VARLIST /////////////////////////////////////////////=//
//
// This indicates this series represents the "varlist" of a context (which is
// interchangeable with the identity of the varlist itself).  A second series
// can be reached from it via the `->misc` field in the series node, which is
// a second array known as a "keylist".
//
// See notes on REBCTX for further details about what a context is.
//
#define ARRAY_FLAG_IS_VARLIST \
    FLAG_LEFT_BIT(19)


//=//// ARRAY_FLAG_IS_PAIRLIST ////////////////////////////////////////////=//
//
// Indicates that this series represents the "pairlist" of a map, so the
// series also has a hashlist linked to in the series node.
//
#define ARRAY_FLAG_IS_PAIRLIST \
    FLAG_LEFT_BIT(20)


//=//// ARRAY_FLAG_NEWLINE_AT_TAIL ////////////////////////////////////////=//
//
// The mechanics of how Rebol tracks newlines is that there is only one bit
// per value to track the property.  Yet since newlines are conceptually
// "between" values, that's one bit too few to represent all possibilities.
//
// Ren-C carries a bit for indicating when there's a newline intended at the
// tail of an array.
//
#define ARRAY_FLAG_NEWLINE_AT_TAIL \
    FLAG_LEFT_BIT(21)


//=//// ARRAY_FLAG_CONST_SHALLOW //////////////////////////////////////////=//
//
// When a COPY is made of an ANY-ARRAY! that has CELL_FLAG_CONST, the new
// value shouldn't be const, as the goal of copying it is generally to modify.
// However, if you don't copy it deeply, then mere copying should not be
// giving write access to levels underneath it that would have been seen as
// const if they were PICK'd out before.  This flag tells the copy operation
// to mark any cells that are shallow references as const.  For convenience
// it is the same bit as the const flag one would find in the value.
//
#define ARRAY_FLAG_CONST_SHALLOW \
    FLAG_LEFT_BIT(22)
STATIC_ASSERT(ARRAY_FLAG_CONST_SHALLOW == CELL_FLAG_CONST);


// These flags are available for use by specific array subclasses (e.g. a
// PARAMLIST might use it for different things from a VARLIST)

#define ARRAY_FLAG_23 FLAG_LEFT_BIT(23)
#define ARRAY_FLAG_24 FLAG_LEFT_BIT(24)
#define ARRAY_FLAG_25 FLAG_LEFT_BIT(25)
#define ARRAY_FLAG_26 FLAG_LEFT_BIT(26)
#define ARRAY_FLAG_27 FLAG_LEFT_BIT(27)
#define ARRAY_FLAG_28 FLAG_LEFT_BIT(28)
#define ARRAY_FLAG_29 FLAG_LEFT_BIT(29)
#define ARRAY_FLAG_30 FLAG_LEFT_BIT(30)
#define ARRAY_FLAG_31 FLAG_LEFT_BIT(31)


//=//////////// ^-- STOP ARRAY FLAGS AT FLAG_LEFT_BIT(31) --^ /////////////=//

// Arrays can use all the way up to the 32-bit limit on the flags (since
// they're not using the arbitrary 16-bit number the way that a REBSTR is for
// storing the symbol).  64-bit machines have more space, but it shouldn't
// be used for anything but optimizations.


// These token-pasting based macros allow the callsites to be shorter, since
// they don't have to say ARRAY and FLAG twice.

#define SET_ARRAY_FLAG(s,name) \
    (m_cast(REBSER*, SER(ARR(s)))->header.bits |= ARRAY_FLAG_##name)

#define GET_ARRAY_FLAG(s,name) \
    ((cast(const REBSER*, ARR(s))->header.bits & ARRAY_FLAG_##name) != 0)

#define CLEAR_ARRAY_FLAG(s,name) \
    (m_cast(REBSER*, SER(ARR(s)))->header.bits &= ~ARRAY_FLAG_##name)

#define NOT_ARRAY_FLAG(s,name) \
    ((cast(const REBSER*, ARR(s))->header.bits & ARRAY_FLAG_##name) == 0)


// !!! While SERIES_INFO_XXX bits supposedly apply to any kind of series, they
// are less scarce than the FLAG bits and may have to be given multiple
// meanings based on series type in the long run.  For instance, right now
// there is a "INFO_MISC" bit needed due to array flag saturation.
//
#define ARRAY_INFO_MISC_VOIDER SERIES_INFO_MISC_BIT


// Ordinary source arrays use their ->link field to point to an interned file
// name string (or URL string) from which the code was loaded.  If a series
// was not created from a file, then the information from the source that was
// running at the time is propagated into the new second-generation series.
//
#define LINK_FILE_NODE(s)       LINK(s).custom.node
#define LINK_FILE(s)            STR(LINK_FILE_NODE(s))


#if !defined(DEBUG_CHECK_CASTS)

    #define ARR(p) \
        m_cast(REBARR*, (const REBARR*)(p))  // don't check const in C or C++

#else

    template <
        typename T,
        typename T0 = typename std::remove_const<T>::type,
        typename A = typename std::conditional<
            std::is_const<T>::value,  // boolean
            const REBARR,  // true branch
            REBARR  // false branch
        >::type
    >
    inline A *ARR(T *p) {
        constexpr bool derived = std::is_same<T0, REBARR>::value;

        constexpr bool base = std::is_same<T0, void>::value
            or std::is_same<T0, REBNOD>::value
            or std::is_same<T0, REBSER>::value;

        static_assert(
            derived or base,
            "ARR works on void/REBNOD/REBSER/REBARR"
        );

        bool b = base;  // needed to avoid compiler constexpr warning
        if (b and p and (reinterpret_cast<const REBSER*>(p)->header.bits & (
            NODE_FLAG_NODE | NODE_FLAG_FREE | NODE_FLAG_CELL
        )) != (
            NODE_FLAG_NODE
        )){
            panic (p);
        }

        if (p)
            assert(WIDE_BYTE_OR_0(reinterpret_cast<const REBSER*>(p)) == 0);

        return reinterpret_cast<A*>(p);
    }

#endif
