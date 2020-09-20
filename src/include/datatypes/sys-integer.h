//
//  File: %sys-integer.h
//  Summary: "INTEGER! Datatype Header"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
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
// Integers in Rebol were standardized to use a compiler-provided 64-bit
// value.  This was formally added to the spec in C99, but many compilers
// supported it before that.
//
// !!! 64-bit extensions were added by the "rebolsource" fork, with much of
// the code still written to operate on 32-bit values.  Since the standard
// unit of indexing and block length counts remains 32-bit in that 64-bit
// build at the moment, many lingering references were left that operated
// on 32-bit values.  To make this clearer, the macros have been renamed
// to indicate which kind of integer they retrieve.  However, there should
// be a general review for reasoning, and error handling + overflow logic
// for these cases.
//

// note that MBEDTLS_HAVE_INT64 is not defined by the default config; we need
// to make the config match the Rebol setting, and have this line up when
// building the extensions as well as the core.
//
#define MBEDTLS_CONFIG_FILE "mbedtls/mbedtls-rebol-config.h"
#include "mbedtls/bignum.h"

typedef intptr_t REBSML;  // "small" integer (e.g. not a REBBIG)
#if defined(small)
    //
    // In an example of being egregiously bad for an OS header, Windows.h
    // pulls in a macro called `small`.  Undefining it shouldn't be a problem
    // for things using %sys-core.h (e.g. extensions).
    //
    // https://stackoverflow.com/a/27794577/
    //
    #ifdef TO_WINDOWS
        #undef small
    #else
        #pragma message "Non-Windows build defining 'small', look into it"
    #endif
#endif

// We need to detect when the platform integer overflows, so we know to
// expand and use a BigNum.  Originally Rebol did this in a custom way, but
// Atronix changed it so that it would take advantage of builtin functions
// provided by the compiler if available.
//
// There aren't really any cross-platform libraries for wrapping the compiler
// built-ins to use vs. having custom code.  The various functions are
// difficult to wrap generically, even in C++:
//
// https://lists.zephyrproject.org/g/devel/topic/wrapping_compiler_builtins/31422932
//
// So most C code either doesn't handle integer overflows, or if it's a
// programming language it implements it by hand (like Python).  We assume for
// the moment that whatever was in R3-Alpha + Atronix's code is good enough
// since tests passed with it...but it's a complex topic which should be
// reviewed.  (e.g. see Python's `intobj.c`)
//
// https://stackoverflow.com/questions/199333/
//
#include "sys-int-funcs.h"

// This extracts a struct laid out as expected for mbedTLS `bignum.h`.  The
// actual storage is in the series, but the code is written to expect a
// certain struct layout...so this inexpensive transformation must be done.
//
inline static mbedtls_mpi *Mpi_From_Bigint(mbedtls_mpi *mpi, REBBIG *big) {
    mpi->s = MISC(big).sign;  // -1 if mpi is negative, 1 otherwise
    mpi->n = SER_USED(big);
    mpi->p = cast(mbedtls_mpi_uint*, SER_DATA(big));  // pointer to limbs

    // This field is at the tail of the mpi in a custom modification to
    // `bignum.c`.  Other hooks in that code allow us to reach back and
    // update the series on expansion/etc.
    //
    mpi->hookdata = big;
    return mpi;
}

// If you are capable of handling a bignum, you call this routine.
//
inline static const REBBIG *VAL_INT_BIGNUM(REBCEL(const*) v) {
    assert(CELL_KIND(v) == REB_INTEGER);
    return cast(REBBIG*, VAL_NODE(v));
}

inline static REBVAL *Init_Integer_Bignum(RELVAL *out, REBBIG *big) {
    RESET_CELL(out, REB_INTEGER, CELL_FLAG_SECOND_IS_NODE);
    INIT_VAL_NODE(out, big);
    return cast(REBVAL*, out);
}


#if defined(NDEBUG) || !defined(CPLUSPLUS_11) 
    #define VAL_INT_SMALL(v) \
        PAYLOAD(Any, (v)).second.i
#else
    // C++ reference allows an assert, but also lvalue: `VAL_INTPTR(v) = xxx`
    //
    inline static intptr_t VAL_INT_SMALL(REBCEL(const*) v) {
        assert(CELL_KIND(v) == REB_INTEGER);
        return PAYLOAD(Any, v).second.i;
    }

    inline static intptr_t & VAL_INT_SMALL(RELVAL *v) {
        assert(CELL_KIND(VAL_UNESCAPED(v)) == REB_INTEGER);
        return PAYLOAD(Any, v).second.i;
    }
#endif

// !!! This is actually `Init_Integer_Small_Frozen()` or somesuch.  All the
// data is represented in the cell, and the user can't mutate it.  It may
// be common enough that leaving it at this name and making the other
// variants more decorated names is the best idea.
//
inline static REBVAL *Init_Integer(RELVAL *out, REBSML small) {
    RESET_CELL(out, REB_INTEGER, CELL_MASK_NONE);
    PAYLOAD(Any, out).second.i = small;
    return cast(REBVAL*, out);
}

inline static int64_t VAL_INT64(REBCEL(const*) v) {
    REBSML i = VAL_INT_SMALL(v);
    return i;
}

inline static int32_t VAL_INT32(REBCEL(const*) v) {
    REBSML i = VAL_INT_SMALL(v);
    if (i > INT32_MAX or i < INT32_MIN)
        fail (Error_Out_Of_Range(SPECIFIC(CELL_TO_VAL(v))));
    return i;
}

inline static uint32_t VAL_UINT32(REBCEL(const*) v) {
    REBSML i = VAL_INT_SMALL(v);
    if (i < 0 or i > UINT32_MAX)
        fail (Error_Out_Of_Range(SPECIFIC(CELL_TO_VAL(v))));
    return cast(uint32_t, i);
}

inline static REBYTE VAL_UINT8(REBCEL(const*) v) {
    REBSML i = VAL_INT_SMALL(v);
    if (i > 255 or i < 0)
        fail (Error_Out_Of_Range(SPECIFIC(CELL_TO_VAL(v))));
    return cast(REBYTE, i);
}


#define ROUND_TO_INT(d) \
    cast(int32_t, floor((MAX(INT32_MIN, MIN(INT32_MAX, d))) + 0.5))
