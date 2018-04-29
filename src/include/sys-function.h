//
//  File: %sys-function.h
//  Summary: {Definitions for REBACT}
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
// Using a technique strongly parallel to contexts, an action is identified
// by a series which is also its paramlist, in which the 0th element is an
// archetypal value of that ACTION!.  Unlike contexts, an action does not
// have values of its own...only parameter definitions (or "params").  The
// arguments ("args") come from finding an action's instantiation on the
// stack, and can be viewed as a context using a FRAME!.
//

struct Reb_Action {
    struct Reb_Array paramlist;
};

#if !defined(NDEBUG) && defined(CPLUSPLUS_11)
    template <class T>
    inline REBACT *ACT(T *p) {
        static_assert(
            std::is_same<T, void>::value
            or std::is_same<T, REBNOD>::value
            or std::is_same<T, REBSER>::value
            or std::is_same<T, REBARR>::value,
            "ACT() works on: void*, REBNOD*, REBSER*, REBARR*"
        );
        assert(
            (NODE_FLAG_NODE | SERIES_FLAG_ARRAY | ARRAY_FLAG_PARAMLIST)
            == (reinterpret_cast<REBSER*>(p)->header.bits & (
                NODE_FLAG_NODE | SERIES_FLAG_ARRAY | ARRAY_FLAG_PARAMLIST
                | NODE_FLAG_FREE | NODE_FLAG_CELL | NODE_FLAG_END // bad!
            ))
        );
        return reinterpret_cast<REBACT*>(p);
    }
#else
    #define ACT(p) \
        cast(REBACT*, (p))
#endif


inline static REBARR *ACT_PARAMLIST(REBACT *a) {
    assert(GET_SER_FLAG(&a->paramlist, ARRAY_FLAG_PARAMLIST));
    return &a->paramlist;
}

#define ACT_ARCHETYPE(a) \
    SER_AT(REBVAL, SER(ACT_PARAMLIST(a)), 0) // binding should be UNBOUND

// Functions hold their flags in their canon value, some of which are cached
// flags put there during Make_Action().
//
// !!! Review if (and how) a HIJACK might affect these flags (?)
//
#define GET_ACT_FLAG(fun, flag) \
    GET_VAL_FLAG(ACT_ARCHETYPE(fun), (flag))

#define ACT_DISPATCHER(a) \
    (MISC(ACT_ARCHETYPE(a)->payload.action.body_holder).dispatcher)

#define ACT_BODY(a) \
    ARR_SINGLE(ACT_ARCHETYPE(a)->payload.action.body_holder)

inline static REBVAL *ACT_PARAM(REBACT *a, REBCNT n) {
    assert(n != 0 and n < ARR_LEN(ACT_PARAMLIST(a)));
    return SER_AT(REBVAL, SER(ACT_PARAMLIST(a)), n);
}

#define ACT_NUM_PARAMS(a) \
    (ARR_LEN(ACT_PARAMLIST(a)) - 1)

#define ACT_META(a) \
    (MISC(ACT_PARAMLIST(a)).meta)


// *** These ACT_FACADE fetchers are called VERY frequently, so it is best
// to keep them light (as the debug build does not inline).  Integrity checks
// of the facades are deferred to the GC, see the REB_ACTION case in the
// switch(), and don't turn these into inline functions without a really good
// reason...and seeing the impact on the debug build!!! ***

#define ACT_FACADE(a) \
    LINK(ACT_PARAMLIST(a)).facade

#define ACT_FACADE_NUM_PARAMS(a) \
    (ARR_LEN(ACT_FACADE(a)) - 1)

#define ACT_FACADE_HEAD(a) \
    KNOWN(ARR_AT(ACT_FACADE(a), 1))


// The concept of the "underlying" function is that which has the right
// number of arguments for the frame to be built--and which has the actual
// correct paramlist identity to use for binding in adaptations.
//
// So if you specialize a plain function with 2 arguments so it has just 1,
// and then specialize the specialization so that it has 0, your call still
// needs to be building a frame with 2 arguments.  Because that's what the
// code that ultimately executes--after the specializations are peeled away--
// will expect.
//
// And if you adapt an adaptation of a function, the keylist referred to in
// the frame has to be the one for the inner function.  Using the adaptation's
// parameter list would write variables the adapted code wouldn't read.
//
// For efficiency, the underlying pointer can be derived from the "facade".
// Though the facade may not be the underlying paramlist (it could have its
// parameter types tweaked for the purposes of that composition), it will
// always have an ACTION! value in its 0 slot as the underlying function.
//
#define ACT_UNDERLYING(a) \
    ACT(ARR_HEAD(ACT_FACADE(a))->payload.action.paramlist)

#define ACT_EXEMPLAR(a) \
    (LINK(ACT_ARCHETYPE(a)->payload.action.body_holder).exemplar)


// There is no binding information in a function parameter (typeset) so a
// REBVAL should be okay.
//
#define ACT_PARAMS_HEAD(a) \
    SER_AT(REBVAL, SER(ACT_PARAMLIST(a)), 1)



//=////////////////////////////////////////////////////////////////////////=//
//
//  ACTION!
//
//=////////////////////////////////////////////////////////////////////////=//

#ifdef NDEBUG
    #define ACTION_FLAG(n) \
        FLAGIT_LEFT(TYPE_SPECIFIC_BIT + (n))
#else
    #define ACTION_FLAG(n) \
        (FLAGIT_LEFT(TYPE_SPECIFIC_BIT + (n)) | HEADERIZE_KIND(REB_ACTION))
#endif

// RETURN will always be in the last paramlist slot (if present)
//
#define ACTION_FLAG_RETURN ACTION_FLAG(0)

// LEAVE will always be in the last paramlist slot (if present)
//
#define ACTION_FLAG_LEAVE ACTION_FLAG(1)

// DEFERS_LOOKBACK_ARG flag is a cached property, which tells you whether a
// function defers its first real argument when used as a lookback.  Because
// lookback dispatches cannot use refinements at this time, the answer is
// static for invocation via a plain word.  This property is calculated at
// the time of Make_Action().
//
#define ACTION_FLAG_DEFERS_LOOKBACK ACTION_FLAG(2)

// This is another cached property, needed because lookahead/lookback is done
// so frequently, and it's quicker to check a bit on the function than to
// walk the parameter list every time that function is called.
//
#define ACTION_FLAG_QUOTES_FIRST_ARG ACTION_FLAG(3)

// The COMPILE-NATIVES command wants to operate on user natives, and be able
// to recompile unchanged natives as part of a unit even after they were
// initially compiled.  But since that replaces their dispatcher with an
// arbitrary function, they can't be recognized to know they have the specific
// body structure of a user native.  So this flag is used.
//
#define ACTION_FLAG_USER_NATIVE ACTION_FLAG(4)

// This flag is set when the native (e.g. extensions) can be unloaded
//
#define ACTION_FLAG_UNLOADABLE_NATIVE ACTION_FLAG(5)

// An "invisible" function is one that does not touch its frame output cell,
// leaving it completely alone.  This is how `10 comment ["hi"] + 20` can
// work...if COMMENT destroyed the 10 in the output cell it would be lost and
// the addition could no longer work.
//
// !!! One property considered for invisible items was if they might not be
// quoted in soft-quoted positions.  This would require fetching something
// that might not otherwise need to be fetched, to test the flag.  Review.
//
#define ACTION_FLAG_INVISIBLE ACTION_FLAG(6)

#if !defined(NDEBUG)
    //
    // If a function is a native then it may provide return information as
    // documentation, but not want to pay for the run-time check of whether
    // the type is correct or not.  In the debug build though, it's good
    // to double-check.  So when MKF_FAKE_RETURN is used in a debug build,
    // it leaves this flag on the function.
    //
    #define ACTION_FLAG_RETURN_DEBUG ACTION_FLAG(7)
#endif

// These are the flags which are scanned for and set during Make_Action
//
#define ACTION_FLAG_CACHED_MASK \
    (ACTION_FLAG_DEFERS_LOOKBACK | ACTION_FLAG_QUOTES_FIRST_ARG \
        | ACTION_FLAG_INVISIBLE)


inline static REBACT *VAL_ACTION(const RELVAL *v) {
    assert(IS_ACTION(v));
    return ACT(v->payload.action.paramlist);
}

#define VAL_ACT_PARAMLIST(v) \
    ACT_PARAMLIST(VAL_ACTION(v))

#define VAL_ACT_NUM_PARAMS(v) \
    ACT_NUM_PARAMS(VAL_ACTION(v))

#define VAL_ACT_PARAMS_HEAD(v) \
    ACT_PARAMS_HEAD(VAL_ACTION(v))

#define VAL_ACT_PARAM(v,n) \
    ACT_PARAM(VAL_ACTION(v), n)

inline static RELVAL *VAL_ACT_BODY(const RELVAL *v) {
    assert(IS_ACTION(v));
    return ARR_HEAD(v->payload.action.body_holder);
}

inline static REBNAT VAL_ACT_DISPATCHER(const RELVAL *v) {
    assert(IS_ACTION(v));
    return MISC(v->payload.action.body_holder).dispatcher;
}

inline static REBCTX *VAL_ACT_META(const RELVAL *v) {
    assert(IS_ACTION(v));
    return MISC(v->payload.action.paramlist).meta;
}


// Native values are stored in an array at boot time.  These are convenience
// routines for accessing them, which should compile to be as efficient as
// fetching any global pointer.

#define NAT_VALUE(name) \
    (&Natives[N_##name##_ID])

#define NAT_ACTION(name) \
    VAL_ACTION(NAT_VALUE(name))
