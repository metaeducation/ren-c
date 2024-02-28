//
//  File: %sys-rebctx.h
//  Summary: {context! defs BEFORE %tmp-internals.h (see: %sys-context.h)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
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


// A context's varlist is always allocated dynamically, in order to speed
// up variable access--no need to test LEN_BYTE_OR_255 for 255.
//
// !!! Ideally this would carry a flag to tell a GC "shrinking" process not
// to reclaim the dynamic memory to make a singular cell...but that flag
// can't be SERIES_FLAG_FIXED_SIZE, because most varlists can expand.
//
#define SERIES_MASK_CONTEXT \
    (NODE_FLAG_NODE | SERIES_FLAG_ALWAYS_DYNAMIC | ARRAY_FLAG_VARLIST)


struct Reb_Context {
    struct Reb_Array varlist; // keylist is held in ->link.keylist
};


#if !defined(DEBUG_CHECK_CASTS) || (! CPLUSPLUS_11)

    #define CTX(p) \
        cast(REBCTX*, (p))

#elif CPLUSPLUS_11

    template<typename T>
    INLINE REBCTX *CTX(T *p) {
        constexpr bool derived = std::is_same<T, REBCTX>::value;

        constexpr bool base = std::is_same<T, void>::value
            or std::is_same<T, REBNOD>::value
            or std::is_same<T, REBSER>::value
            or std::is_same<T, Array>::value;

        static_assert(
            derived or base,
            "CTX() works on REBNOD/REBSER/Array/REBCTX"
        );

        if (base)
            assert(
                (reinterpret_cast<REBNOD*>(p)->header.bits & (
                    NODE_FLAG_NODE | ARRAY_FLAG_VARLIST
                        | NODE_FLAG_FREE
                        | NODE_FLAG_CELL
                        | ARRAY_FLAG_PARAMLIST
                        | ARRAY_FLAG_PAIRLIST
                )) == (
                    NODE_FLAG_NODE | ARRAY_FLAG_VARLIST
                )
            );

        return reinterpret_cast<REBCTX*>(p);
    }

#endif
