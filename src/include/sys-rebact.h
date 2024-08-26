//
//  File: %sys-action.h
//  Summary: "action! defs BEFORE %tmp-internals.h (see: %sys-action.h)"
//  Section: core
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
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


// Includes FLEX_FLAG_ALWAYS_DYNAMIC because an action's paramlist is always
// allocated dynamically, in order to make access to the archetype and the
// parameters faster than Array_At().  See code for ACT_PARAM(), etc.
//
// Includes FLEX_FLAG_FIXED_SIZE because for now, the user can't expand
// them (e.g. by APPENDing to a FRAME! value).  Also, no internal tricks
// for function composition expand them either at this time.
//
#define SERIES_MASK_ACTION \
    (NODE_FLAG_NODE | FLEX_FLAG_ALWAYS_DYNAMIC | FLEX_FLAG_FIXED_SIZE \
        | ARRAY_FLAG_PARAMLIST)


#if !defined(DEBUG_CHECK_CASTS) || (! CPLUSPLUS_11)

    #define ACT(p) \
        cast(REBACT*, (p))

#else

    template <class T>
    inline REBACT *ACT(T *p) {
        constexpr bool derived = std::is_same<REBACT, void>::value;

        constexpr bool base = std::is_same<T, void>::value
            or std::is_same<T, Node>::value
            or std::is_same<T, Stub>::value
            or std::is_same<T, Array>::value;

        static_assert(
            derived or base,
            "ACT() works on void/Node/Stub/Array/REBACT"
        );

        if (base)
            assert(
                SERIES_MASK_ACTION == (cast(Flex*, p)->header.bits & (
                    SERIES_MASK_ACTION
                        | NODE_FLAG_FREE
                        | NODE_FLAG_CELL
                        | ARRAY_FLAG_VARLIST
                        | ARRAY_FLAG_PAIRLIST
                ))
            );

        return reinterpret_cast<REBACT*>(p);
    }

#endif
