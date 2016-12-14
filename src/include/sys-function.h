//
//  File: %sys-function.h
//  Summary: {Definitions for REBFUN}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
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
// Using a technique strongly parallel to CONTEXT, a function is identified
// by a series which acts as its paramlist, in which the 0th element is an
// ANY-FUNCTION! value.  Unlike a CONTEXT, a FUNC does not have values of its
// own... only parameter definitions (or "params").  The arguments ("args")
// come from finding a function instantiation on the stack.
//

struct Reb_Func {
    struct Reb_Array paramlist;
};

#ifdef NDEBUG
    #define AS_FUNC(s) \
        cast(REBFUN*, (s))
#else
    #define AS_FUNC(s) \
        cast(REBFUN*, (s)) // !!! worth it to add debug version that checks?
#endif

inline static REBARR *FUNC_PARAMLIST(REBFUN *f) {
#if !defined(NDEBUG)
    if (!GET_ARR_FLAG(&f->paramlist, ARRAY_FLAG_PARAMLIST))
        Panic_Array(&f->paramlist);
#endif
    return &f->paramlist;
}

inline static REBVAL *FUNC_VALUE(REBFUN *f) {
    return SER_AT(REBVAL, ARR_SERIES(FUNC_PARAMLIST(f)), 0);
}

inline static REBNAT FUNC_DISPATCHER(REBFUN *f) {
    return ARR_SERIES(
        FUNC_VALUE(f)->payload.function.body_holder
    )->misc.dispatcher;
}

inline static RELVAL *FUNC_BODY(REBFUN *f) {
    assert(ARR_LEN(FUNC_VALUE(f)->payload.function.body_holder) == 1);
    return ARR_HEAD(FUNC_VALUE(f)->payload.function.body_holder);
}

inline static REBVAL *FUNC_PARAM(REBFUN *f, REBCNT n) {
    assert(n != 0 && n < ARR_LEN(FUNC_PARAMLIST(f)));
    return SER_AT(REBVAL, ARR_SERIES(FUNC_PARAMLIST(f)), n);
}

inline static REBCNT FUNC_NUM_PARAMS(REBFUN *f) {
    return ARR_LEN(FUNC_PARAMLIST(f)) - 1;
}

inline static REBCTX *FUNC_META(REBFUN *f) {
    return ARR_SERIES(FUNC_PARAMLIST(f))->link.meta;
}

// Note: On Windows, FUNC_DISPATCH is already defined in the header files
//
#define FUNC_DISPATCHER(f) \
    (ARR_SERIES(FUNC_VALUE(f)->payload.function.body_holder)->misc.dispatcher)

// There is no binding information in a function parameter (typeset) so a
// REBVAL should be okay.
//
inline static REBVAL *FUNC_PARAMS_HEAD(REBFUN *f) {
    return SER_AT(REBVAL, ARR_SERIES(FUNC_PARAMLIST(f)), 1);
}

inline static REBRIN *FUNC_ROUTINE(REBFUN *f) {
    return VAL_ARRAY(FUNC_BODY(f));
}



//=////////////////////////////////////////////////////////////////////////=//
//
//  FUNCTION! (`struct Reb_Function`)
//
//=////////////////////////////////////////////////////////////////////////=//

#ifdef NDEBUG
    #define FUNC_FLAG(n) \
        HEADERFLAG(TYPE_SPECIFIC_BIT + (n))
#else
    #define FUNC_FLAG(n) \
        (HEADERFLAG(TYPE_SPECIFIC_BIT + (n)) | HEADERIZE_KIND(REB_FUNCTION))
#endif

// RETURN will always be in the last paramlist slot (if present)
//
#define FUNC_FLAG_RETURN FUNC_FLAG(0)

// LEAVE will always be in the last paramlist slot (if present)
//
#define FUNC_FLAG_LEAVE FUNC_FLAG(1)

// A "brancher" is a single arity function that is capable of taking a
// LOGIC! value.  Currently testing for this requires a bit of processing
// so it is done when the function is made, and then this flag is checked.
// It's set even if the function might not take logic or need more
// parameters, so that it can be called and cause an error if needed.
//
#define FUNC_FLAG_MAYBE_BRANCHER FUNC_FLAG(2)

// As with MAYBE_BRANCHER, the DEFERS_LOOKBACK_ARG flag is a cached property,
// which tells you whether a function defers its first real argument when
// used as a lookback.  Because lookback dispatches cannot use refinements
// at this time, the answer is static for invocation via a plain word.
//
#define FUNC_FLAG_DEFERS_LOOKBACK_ARG FUNC_FLAG(3)

// The COMPILE-NATIVES command wants to operate on user natives, and be able
// to recompile unchanged natives as part of a unit even after they were
// initially compiled.  But since that replaces their dispatcher with an
// arbitrary function, they can't be recognized to know they have the specific
// body structure of a user native.  So this flag is used.
//
#define FUNC_FLAG_USER_NATIVE FUNC_FLAG(4)

#if !defined(NDEBUG)
    //
    // This flag is set on the canon function value when a proxy for a
    // hijacking is made.  The main use is to disable the assert that the
    // underlying function cached at the top level matches the actual
    // function implementation after digging through the layers...because
    // proxies must have new (cloned) paramlists but use the original bodies.
    //
    #define FUNC_FLAG_PROXY_DEBUG FUNC_FLAG(5)

    // BLANK! ("none!") for unused refinements instead of FALSE
    // Also, BLANK! for args of unused refinements instead of not set
    //
    #define FUNC_FLAG_LEGACY_DEBUG FUNC_FLAG(6)

    // If a function is a native then it may provide return information as
    // documentation, but not want to pay for the run-time check of whether
    // the type is correct or not.  In the debug build though, it's good
    // to double-check.  So when MKF_FAKE_RETURN is used in a debug build,
    // it leaves this flag on the function.
    //
    #define FUNC_FLAG_RETURN_DEBUG FUNC_FLAG(7)
#endif


inline static REBFUN *VAL_FUNC(const RELVAL *v) {
    assert(IS_FUNCTION(v));
    return AS_FUNC(v->payload.function.paramlist);
}

inline static REBARR *VAL_FUNC_PARAMLIST(const RELVAL *v)
    { return FUNC_PARAMLIST(VAL_FUNC(v)); }

inline static REBCNT VAL_FUNC_NUM_PARAMS(const RELVAL *v)
    { return FUNC_NUM_PARAMS(VAL_FUNC(v)); }

inline static REBVAL *VAL_FUNC_PARAMS_HEAD(const RELVAL *v)
    { return FUNC_PARAMS_HEAD(VAL_FUNC(v)); }

inline static REBVAL *VAL_FUNC_PARAM(const RELVAL *v, REBCNT n)
    { return FUNC_PARAM(VAL_FUNC(v), n); }

inline static RELVAL *VAL_FUNC_BODY(const RELVAL *v)
    { return ARR_HEAD(v->payload.function.body_holder); }

inline static REBNAT VAL_FUNC_DISPATCHER(const RELVAL *v)
    { return ARR_SERIES(v->payload.function.body_holder)->misc.dispatcher; }

inline static REBCTX *VAL_FUNC_META(const RELVAL *v)
    { return ARR_SERIES(v->payload.function.paramlist)->link.meta; }

inline static REBOOL IS_FUNCTION_INTERPRETED(const RELVAL *v) {
    //
    // !!! Review cases where this is supposed to matter, because they are
    // probably all bad.  With the death of function categories, code should
    // be able to treat functions as "black boxes" and not know which of
    // the dispatchers they run on...with only the dispatch itself caring.
    //
    return LOGICAL(
        VAL_FUNC_DISPATCHER(v) == &Noop_Dispatcher
        || VAL_FUNC_DISPATCHER(v) == &Unchecked_Dispatcher
        || VAL_FUNC_DISPATCHER(v) == &Voider_Dispatcher
        || VAL_FUNC_DISPATCHER(v) == &Returner_Dispatcher
    );
}

inline static REBOOL IS_FUNCTION_ACTION(const RELVAL *v)
    { return LOGICAL(VAL_FUNC_DISPATCHER(v) == &Action_Dispatcher); }

inline static REBOOL IS_FUNCTION_COMMAND(const RELVAL *v)
    { return LOGICAL(VAL_FUNC_DISPATCHER(v) == &Command_Dispatcher); }

inline static REBOOL IS_FUNCTION_SPECIALIZER(const RELVAL *v)
    { return LOGICAL(VAL_FUNC_DISPATCHER(v) == &Specializer_Dispatcher); }

inline static REBOOL IS_FUNCTION_CHAINER(const RELVAL *v)
    { return LOGICAL(VAL_FUNC_DISPATCHER(v) == &Chainer_Dispatcher); }

inline static REBOOL IS_FUNCTION_ADAPTER(const RELVAL *v)
    { return LOGICAL(VAL_FUNC_DISPATCHER(v) == &Adapter_Dispatcher); }

inline static REBOOL IS_FUNCTION_RIN(const RELVAL *v)
    { return LOGICAL(VAL_FUNC_DISPATCHER(v) == &Routine_Dispatcher); }

inline static REBOOL IS_FUNCTION_HIJACKER(const RELVAL *v)
    { return LOGICAL(VAL_FUNC_DISPATCHER(v) == &Hijacker_Dispatcher); }

inline static REBRIN *VAL_FUNC_ROUTINE(const RELVAL *v) {
    return VAL_ARRAY(VAL_FUNC_BODY(v));
}


// !!! At the moment functions are "all durable" or "none durable" w.r.t. the
// survival of their arguments and locals after the call.
//
inline static REBOOL IS_FUNC_DURABLE(REBFUN *f) {
    return LOGICAL(
        FUNC_NUM_PARAMS(f) != 0
        && GET_VAL_FLAG(FUNC_PARAM(f, 1), TYPESET_FLAG_DURABLE)
    );
}

// Native values are stored in an array at boot time.  This is a convenience
// accessor for getting the "FUNC" portion of the native--e.g. the paramlist.
// It should compile to be as efficient as fetching any global pointer.

#define NAT_VALUE(name) \
    (&Natives[N_##name##_ID])

#define NAT_FUNC(name) \
    VAL_FUNC(NAT_VALUE(name))



// Gets a system function with tolerance of it not being a function.
//
// (Extraction of a feature that formerly was part of a dedicated dual
// function to Apply_Func_Throws (Do_Sys_Func_Throws())
//
inline static REBVAL *Sys_Func(REBCNT inum)
{
    REBVAL *value = CTX_VAR(Sys_Context, inum);

    if (!IS_FUNCTION(value))
        fail (Error(RE_BAD_SYS_FUNC, value));

    return value;
}
