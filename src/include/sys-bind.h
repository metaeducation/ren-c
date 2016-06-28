//
//  File: %sys-bind.h
//  Summary: "System Binding Include"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
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
// R3-Alpha had a per-thread "bind table"; a large and sparsely populated hash
// into which index numbers would be placed, for what index those words would
// have as keys or parameters.  Ren-C's strategy is that binding information
// is wedged into REBSER nodes that represent the canon words themselves.
//
// This would create problems if multiple threads were trying to bind at the
// same time.  While threading was never realized in R3-Alpha, Ren-C doesn't
// want to have any "less of a plan".  So the Reb_Binder is used by binding
// clients as a placeholder for whatever actual state would be used to augment
// the information in the canon word series about which client is making a
// request.  This could be coupled with some kind of lockfree adjustment
// strategy whereby a word that was contentious would cause a structure to
// "pop out" and be pointed to by some atomic thing inside the word.
//
// For the moment, a binder has some influence by saying whether the high 16
// bits or low 16 bits of the canon's misc.index are used.  If the index
// were atomic this would--for instance--allow two clients to bind at once.
// It's just a demonstration of where more general logic using atomics
// that could work for N clients would be.
//
// The debug build also adds another feature, that makes sure the clear count
// matches the set count.
//

// Modes allowed by Bind related functions:
enum {
    BIND_0 = 0, // Only bind the words found in the context.
    BIND_DEEP = 1 << 1, // Recurse into sub-blocks.
    BIND_FUNC = 1 << 2 // Recurse into functions.
};


struct Reb_Binder {
    REBOOL high;
#if !defined(NDEBUG)
    REBCNT count;
#endif
};


inline static void INIT_BINDER(struct Reb_Binder *binder) {
    binder->high = TRUE; //LOGICAL(SPORADICALLY(2)); sporadic?
#if !defined(NDEBUG)
    binder->count = 0;
#endif
}


inline static void SHUTDOWN_BINDER(struct Reb_Binder *binder) {
    assert(binder->count == 0);
}


// Tries to set the binder index, but return false if already there.
//
inline static REBOOL Try_Add_Binder_Index(
    struct Reb_Binder *binder,
    REBSTR *canon,
    REBINT index
){
    assert(index != 0);
    assert(GET_SER_FLAG(canon, STRING_FLAG_CANON));
    if (binder->high) {
        if (canon->misc.bind_index.high != 0)
            return FALSE;
        canon->misc.bind_index.high = index;
    }
    else {
        if (canon->misc.bind_index.low != 0)
            return FALSE;
        canon->misc.bind_index.low = index;
    }

#if !defined(NDEBUG)
    ++binder->count;
#endif
    return TRUE;
}


inline static void Add_Binder_Index(
    struct Reb_Binder *binder,
    REBSTR *canon,
    REBINT index
){
    REBOOL success = Try_Add_Binder_Index(binder, canon, index);
    assert(success);
}


inline static REBINT Try_Get_Binder_Index( // 0 if not present
    struct Reb_Binder *binder,
    REBSTR *canon
){
    assert(GET_SER_FLAG(canon, STRING_FLAG_CANON));

    if (binder->high)
        return canon->misc.bind_index.high;
    else
        return canon->misc.bind_index.low;
}


inline static REBINT Try_Remove_Binder_Index( // 0 if failure, else old index
    struct Reb_Binder *binder,
    REBSTR *canon
){
    assert(GET_SER_FLAG(canon, STRING_FLAG_CANON));

    REBINT old_index;
    if (binder->high) {
        old_index = canon->misc.bind_index.high;
        if (old_index == 0)
            return 0;
        canon->misc.bind_index.high = 0;
    }
    else {
        old_index = canon->misc.bind_index.low;
        if (old_index == 0)
            return 0;
        canon->misc.bind_index.low = 0;
    }

#if !defined(NDEBUG)
    --binder->count;
#endif
    return old_index;
}


inline static void Remove_Binder_Index(
    struct Reb_Binder *binder,
    REBSTR *canon
){
    REBINT old_index = Try_Remove_Binder_Index(binder, canon);
    assert(old_index != 0);
}


// Modes allowed by Collect keys functions:
enum {
    COLLECT_ONLY_SET_WORDS = 0,
    COLLECT_ANY_WORD = 1 << 1,
    COLLECT_DEEP = 1 << 2,
    COLLECT_NO_DUP = 1 << 3, // Do not allow dups during collection (for specs)
    COLLECT_ENSURE_SELF = 1 << 4 // !!! Ensure SYM_SELF in context (temp)
};

