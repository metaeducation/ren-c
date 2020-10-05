//
//  File: %sys-rebctx.h
//  Summary: {context! defs BEFORE %tmp-internals.h (see: %sys-context.h)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Ren-C Open Source Contributors
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


#define BUF_COLLECT TG_Buf_Collect


// A context's varlist is always allocated dynamically, in order to speed
// up variable access--no need to test LEN_BYTE_OR_255 for 255.
//
// !!! Ideally this would carry a flag to tell a GC "shrinking" process not
// to reclaim the dynamic memory to make a singular cell...but that flag
// can't be SERIES_FLAG_FIXED_SIZE, because most varlists can expand.
//
#define SERIES_MASK_VARLIST \
    (NODE_FLAG_NODE | SERIES_FLAG_ALWAYS_DYNAMIC \
        | SERIES_FLAG_LINK_NODE_NEEDS_MARK  /* keysource */ \
        | SERIES_FLAG_MISC_NODE_NEEDS_MARK  /* meta */ \
        | ARRAY_FLAG_IS_VARLIST)

#define SERIES_MASK_KEYLIST \
    (NODE_FLAG_NODE | SERIES_FLAG_ALWAYS_DYNAMIC \
        | SERIES_FLAG_LINK_NODE_NEEDS_MARK  /* ancestor */ )

struct Reb_Context {
    struct Reb_Array varlist;  // keylist is held in ->link.keysource
};


#if !defined(DEBUG_CHECK_CASTS)

    #define CTX(p) \
        m_cast(REBCTX*, (const REBCTX*)(p))  // don't check const in C or C++

#else

    template <
        typename T,
        typename T0 = typename std::remove_const<T>::type,
        typename C = typename std::conditional<
            std::is_const<T>::value,  // boolean
            const REBCTX,  // true branch
            REBCTX  // false branch
        >::type
    >
    inline static C *CTX(T *p) {
        constexpr bool derived = std::is_same<T0, REBCTX>::value;

        constexpr bool base = std::is_same<T0, void>::value
            or std::is_same<T0, REBNOD>::value
            or std::is_same<T0, REBSER>::value
            or std::is_same<T0, REBARR>::value;

        static_assert(
            derived or base,
            "CTX() works on REBNOD/REBSER/REBARR/REBCTX"
        );

        bool b = base;  // needed to avoid compiler constexpr warning
        if (b and p and (reinterpret_cast<const REBNOD*>(p)->header.bits & (
            NODE_FLAG_NODE | NODE_FLAG_FREE | NODE_FLAG_CELL
                | ARRAY_FLAG_IS_VARLIST
                | ARRAY_FLAG_IS_PARAMLIST
                | ARRAY_FLAG_IS_PAIRLIST
                | ARRAY_FLAG_HAS_FILE_LINE_UNMASKED
        )) != (
            NODE_FLAG_NODE | ARRAY_FLAG_IS_VARLIST
        )){
            panic (p);
        }

        return reinterpret_cast<C*>(p);
    }

#endif
