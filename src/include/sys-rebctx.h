//
//  file: %sys-rebctx.h
//  summary:{context! defs BEFORE %tmp-internals.h (see: %sys-context.h)}
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// can't be FLEX_FLAG_FIXED_SIZE, because most varlists can expand.
//
#define SERIES_MASK_CONTEXT \
    (BASE_FLAG_BASE | FLEX_FLAG_ALWAYS_DYNAMIC | ARRAY_FLAG_IS_VARLIST)


#if NO_DEBUG_CHECK_CASTS

    #define CTX(p) \
        cast(VarList*, (p))

#elif CPLUSPLUS_11

    template<typename T>
    INLINE VarList* CTX(T *p) {
        constexpr bool derived = std::is_same<T, VarList>::value;

        constexpr bool base = std::is_same<T, void>::value
            or std::is_same<T, Base>::value
            or std::is_same<T, Flex>::value
            or std::is_same<T, Array>::value;

        static_assert(
            derived or base,
            "CTX() works on Base/Flex/Array/VarList"
        );

        if (base)
            assert(
                (reinterpret_cast<Flex*>(p)->header.bits & (
                    BASE_FLAG_BASE | ARRAY_FLAG_IS_VARLIST
                        | BASE_FLAG_UNREADABLE
                        | BASE_FLAG_CELL
                        | ARRAY_FLAG_IS_PARAMLIST
                        | ARRAY_FLAG_IS_PAIRLIST
                )) == (
                    BASE_FLAG_BASE | ARRAY_FLAG_IS_VARLIST
                )
            );

        return reinterpret_cast<VarList*>(p);
    }

#endif
