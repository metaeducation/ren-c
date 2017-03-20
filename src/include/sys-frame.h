//
//  File: %sys-frame.h
//  Summary: {Accessors and Argument Pushers/Poppers for Function Call Frames}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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


//
// Relative and specific values
//

inline static REBARR *VAL_BINDING(const RELVAL *v) {
    assert(
        ANY_ARRAY(v)
        || IS_FUNCTION(v)
        || ANY_CONTEXT(v)
        || IS_VARARGS(v)
        || ANY_WORD(v)
    );
    return v->extra.binding;
}

inline static void INIT_RELATIVE(RELVAL *v, REBFUN *func) {
    assert(GET_VAL_FLAG(v, VALUE_FLAG_RELATIVE));
    v->extra.binding = FUNC_PARAMLIST(func);
}

inline static void INIT_SPECIFIC(RELVAL *v, REBCTX *context) {
    assert(NOT_VAL_FLAG(v, VALUE_FLAG_RELATIVE));
    v->extra.binding = CTX_VARLIST(context);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  THROWN status
//
//=////////////////////////////////////////////////////////////////////////=//
//
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
// will occur exactly once (when it is caught).
//

#define THROWN(v) \
    GET_VAL_FLAG((v), VALUE_FLAG_THROWN)

static inline void CONVERT_NAME_TO_THROWN(
    REBVAL *name, const REBVAL *arg
){
    assert(!THROWN(name));
    SET_VAL_FLAG(name, VALUE_FLAG_THROWN);

    assert(IS_UNREADABLE_IF_DEBUG(&TG_Thrown_Arg));
    Move_Value(&TG_Thrown_Arg, arg);
}

static inline void CATCH_THROWN(REBVAL *arg_out, REBVAL *thrown) {
    //
    // Note: arg_out and thrown may be the same pointer
    //
    assert(NOT_END(thrown));
    assert(THROWN(thrown));
    CLEAR_VAL_FLAG(thrown, VALUE_FLAG_THROWN);

    assert(!IS_UNREADABLE_IF_DEBUG(&TG_Thrown_Arg));
    Move_Value(arg_out, &TG_Thrown_Arg);
    SET_UNREADABLE_BLANK(&TG_Thrown_Arg);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  LOW-LEVEL FRAME ACCESSORS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// !!! To be documented and reviewed.  Legacy naming conventions when the
// arguments to functions lived in the data stack gave the name "FS_TOP" for
// "(D)ata (S)tack (F)rame" which is no longer accurate, as well as the
// convention of prefix with a D_.  The new PARAM()/REFINE()/ARG()/REF()
// scheme has replaced most of these.
//

#define FS_TOP (TG_Frame_Stack + 0) // avoid assignment to FS_TOP via + 0

#define FRM_IS_VALIST(f) \
    LOGICAL((f)->flags.bits & DO_FLAG_VA_LIST)

inline static REBARR *FRM_ARRAY(REBFRM *f) {
    assert(!FRM_IS_VALIST(f));
    return f->source.array;
}

// !!! Though the evaluator saves its `index`, the index is not meaningful
// in a valist.  Also, if `opt_head` values are used to prefetch before an
// array, those will be lost too.  A true debugging mode would need to
// convert these cases to ordinary arrays before running them, in order
// to accurately present the errors.
//
inline static REBCNT FRM_INDEX(REBFRM *f) {
    assert(!FRM_IS_VALIST(f));
    return IS_END(f->value)
        ? ARR_LEN(f->source.array)
        : f->index - 1;
}

inline static REBCNT FRM_EXPR_INDEX(REBFRM *f) {
    assert(!FRM_IS_VALIST(f));
    return f->expr_index == END_FLAG
        ? ARR_LEN((f)->source.array)
        : f->expr_index - 1;
}

#define FRM_OUT(f) \
    cast(REBVAL * const, (f)->out) // writable Lvalue

// Note about FRM_NUM_ARGS: A native should generally not detect the arity it
// was invoked with, (and it doesn't make sense as most implementations get
// the full list of arguments and refinements).  However, ACTION! dispatch
// has several different argument counts piping through a switch, and often
// "cheats" by using the arity instead of being conditional on which action
// ID ran.  Consider when reviewing the future of ACTION!.
//
#define FRM_NUM_ARGS(f) \
    FUNC_FACADE_NUM_PARAMS((f)->func)

inline static REBVAL *FRM_CELL(REBFRM *f) {
    //
    // An earlier optimization would use the frame's cell if a function
    // took exactly one argument for that argument.  This meant it was not
    // available to those functions to use as a GC-protected temporary.  The
    // optimization made it complex for the generalized code that does
    // stack level discovery from a value pointer, and was removed.
    //
    return &f->cell; // otherwise, it's available...
}

#define FRM_PRIOR(f) \
    ((f)->prior)

#define FRM_LABEL(f) \
    ((f)->label)

#define FRM_FUNC(f) \
    ((f)->func)

#define FRM_DSP_ORIG(f) \
    ((f)->dsp_orig + 0) // Lvalue

// `arg` is in use to point at the arguments during evaluation, and `param`
// may hold a SET-WORD! or SET-PATH! available for a lookback to quote.
// But during evaluations, `refine` is free.
//
// Since the GC is aware of the pointers, it can protect whatever refine is
// pointing at.  This can be useful for routines that have a local
// memory cell.  This does not require a push or a pop of anything--it only
// protects as long as the native is running.  (This trick is available to
// the dispatchers as well.)
//
#define PROTECT_FRM_X(f,v) \
    ((f)->refine = (v))


// ARGS is the parameters and refinements
// 1-based indexing into the arglist (0 slot is for object/function value)
#ifdef NDEBUG
    #define FRM_ARG(f,n) \
        ((f)->args_head + (n) - 1)
#else
    inline static REBVAL *FRM_ARG(REBFRM *f, REBCNT n) {
        assert(n != 0 && n <= FRM_NUM_ARGS(f));

        REBVAL *var = &f->args_head[n - 1];

        assert(!THROWN(var));
        assert(NOT_VAL_FLAG(var, VALUE_FLAG_RELATIVE));
        return var;
    }
#endif


// Quick access functions from natives (or compatible functions that name a
// Reb_Frame pointer `frame_`) to get some of the common public fields.
//
#define D_OUT       FRM_OUT(frame_)         // GC-safe slot for output value
#define D_CELL      FRM_CELL(frame_)        // GC-safe cell if > 1 argument
#define D_ARGC      FRM_NUM_ARGS(frame_)        // count of args+refinements/args
#define D_ARG(n)    FRM_ARG(frame_, (n))    // pass 1 for first arg
#define D_FUNC      FRM_FUNC(frame_)        // REBVAL* of running function
#define D_LABEL_SYM FRM_LABEL(frame_)       // symbol or placeholder for call
#define D_DSP_ORIG  FRM_DSP_ORIG(frame_)    // Original data stack pointer

#define D_PROTECT_X(v)      PROTECT_FRM_X(frame_, (v))

#define REB_0_PICKUP REB_0

inline static REBOOL Is_Any_Function_Frame(REBFRM *f) {
    if (f->eval_type == REB_FUNCTION) {
        //
        // Do not count as a function frame unless it's gotten to the point
        // of setting the label.
        //
        return LOGICAL(f->label != NULL);
    }
    return FALSE;
}

// While a function frame is fulfilling its arguments, the `f->param` will
// be pointing to a typeset.  The invariant that is maintained is that
// `f->param` will *not* be a typeset when the function is actually in the
// process of running.  (So no need to set/clear/test another "mode".)
//
inline static REBOOL Is_Function_Frame_Fulfilling(REBFRM *f)
{
    assert(Is_Any_Function_Frame(f));
    return NOT_END(f->param);
}


// It's helpful when looking in the debugger to be able to look at a frame
// and see a cached string for the function it's running (if there is one).
// The release build only considers the frame symbol valid if ET_FUNCTION
//
inline static void SET_FRAME_LABEL(REBFRM *f, REBSTR *label) {
    assert(f->eval_type == REB_FUNCTION);
    f->label = label;
#if !defined(NDEBUG)
    f->label_debug = cast(const char*, STR_HEAD(label));
#endif
}

inline static void CLEAR_FRAME_LABEL(REBFRM *f) {
    f->label = NULL;
#if !defined(NDEBUG)
    f->label_debug = NULL;
#endif
}

inline static void SET_FRAME_VALUE(REBFRM *f, const RELVAL *value) {
    f->value = value;

#if !defined(NDEBUG)
    if (NOT_END(f->value))
        f->value_type = VAL_TYPE(f->value);
    else
        f->value_type = REB_MAX;
#endif
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  ARGUMENT AND PARAMETER ACCESS HELPERS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// These accessors are designed to make it convenient for natives written in
// C to access their arguments and refinements.  (They are what is behind the
// implementation of the INCLUDE_PARAMS_OF_XXX macros that are used in
// natives.)
//
// They are able to bind to the implicit Reb_Frame* passed to every
// REBNATIVE() and read the information out cleanly, like this:
//
//     PARAM(1, foo);
//     REFINE(2, bar);
//
//     if (IS_INTEGER(ARG(foo)) && REF(bar)) { ... }
//
// Though REF can only be used with a REFINE() declaration, ARG can be used
// with either.
//
// Under the hood `PARAM(1, foo)` and `REFINE(2, bar)` make const structs.
// In an optimized build, these structures disappear completely, with all
// addressing done directly into the call frame's cached `arg` pointer.
// It is also possible to get the typeset-with-symbol for a particular
// parameter or refinement, e.g. with `PAR(foo)` or `PAR(bar)`.
//
// The PARAM and REFINE macros use token pasting to name the variables they
// are declaring `p_name` instead of just `name`.  This prevents collisions
// with C++ identifiers, so PARAM(case) and REFINE(new) would make `p_case`
// and `p_new` instead of just `case` and `new` as the variable names.  (This
// is only visible in the debugger.)
//
// As a further aid, the debug build version of the structures contain the
// actual pointers to the arguments.  It also keeps a copy of a cache of the
// type for the arguments, because the numeric type encoding in the bits of
// the header requires a debug call (or by-hand-binary decoding) to interpret
// Whether a refinement was used or not at time of call is also cached.
//

#ifdef NDEBUG
    #define PARAM(n,name) \
        static const int p_##name = n

    #define REFINE(n,name) \
        static const int p_##name = n

    #define ARG(name) \
        FRM_ARG(frame_, (p_##name))

    #define PAR(name) \
        FUNC_PARAM(frame_->func, (p_##name)) /* a TYPESET! */

    #define REF(name) \
        IS_CONDITIONAL_TRUE(ARG(name))
#else
    struct Native_Param {
        enum Reb_Kind kind_cache;
        REBVAL *arg;
        const int num;
    };

    struct Native_Refine {
        REBOOL used_cache;
        REBVAL *arg;
        const int num;
    };

    #define PARAM(n,name) \
        const struct Native_Param p_##name = { \
            VAL_TYPE(FRM_ARG(frame_, (n))), /* watchlist cache */ \
            FRM_ARG(frame_, (n)), /* watchlist cache */ \
            (n) \
        }

    #define REFINE(n,name) \
        const struct Native_Refine p_##name = { \
            IS_CONDITIONAL_TRUE(FRM_ARG(frame_, (n))), /* watchlist cache */ \
            FRM_ARG(frame_, (n)), /* watchlist cache */ \
            (n) \
        }

    #define ARG(name) \
        FRM_ARG(frame_, (p_##name).num)

    #define PAR(name) \
        FUNC_PARAM(frame_->func, (p_##name).num) /* a TYPESET! */

    #define REF(name) \
        ((p_##name).used_cache /* used_cache use stops REF() on PARAM()s */ \
            ? IS_CONDITIONAL_TRUE(ARG(name)) \
            : IS_CONDITIONAL_TRUE(ARG(name)))
#endif


// The native entry prelude makes sure that once native code starts running,
// then a reified frame will be locked or a non-reified frame will be flagged
// in such a way as to indicate that it should be locked when reified.  This
// prevents a FRAME! generated for a native from being able to get write
// access to the variables, which could cause crashes, as raw C code is not
// insulated against having bit patterns for types in cells that aren't
// expected.
//
// !!! Debug injection of bad types into usermode code may cause havoc as
// well, and should be considered a security/permissions issue.  It just won't
// (or shouldn't) crash the evaluator itself.
//
// This is automatically injected by the INCLUDE_PARAMS_OF_XXX macros.  The
// reason this is done with code inlined into the native itself instead of
// based on an IS_NATIVE() test is to avoid the cost of the testing--which
// is itself a bit dodgy to tell a priori if a dispatcher is native or not.
// This way there is no test and only natives pay the cost of flag setting.
//
inline static void Enter_Native(REBFRM *f) {
    f->flags.bits |= DO_FLAG_NATIVE_HOLD;
    if (f->varlist != NULL)
        SET_SER_INFO(f->varlist, SERIES_INFO_RUNNING);
}


// Allocate the series of REBVALs inspected by a function when executed (the
// values behind ARG(name), REF(name), D_ARG(3),  etc.)
//
// This only allocates space for the arguments, it does not initialize.
// Do_Core initializes as it goes, and updates f->param so the GC knows how
// far it has gotten so as not to see garbage.  APPLY has different handling
//
// If the function is a specialization, then the parameter list of that
// specialization will have *fewer* parameters than the full function would.
// For this reason we push the arguments for the "underlying" function.
// Yet if there are specialized values, they must be filled in from the
// exemplar frame.
//
// Rather than "dig" through layers of functions to find the underlying
// function or the specialization's exemplar frame, those properties are
// cached during the creation process.
//
inline static void Push_Or_Alloc_Args_For_Underlying_Func(
    REBFRM *f,
    const REBVAL *gotten
){
    assert(IS_FUNCTION(gotten));

    // We need the actual REBVAL of the function here, and not just the REBFUN.
    // This is true even though you can get an archetype REBVAL from a function
    // pointer with FUNC_VALUE().  That archetype--as with RETURN and LEAVE--
    // will not carry the specific `binding` information of a value.
    //
    f->func = VAL_FUNC(gotten);
    f->binding = VAL_BINDING(gotten);

    // The underlying function is whose parameter list must be enumerated.
    // Even though this underlying function can have more arguments than the
    // "interface" function being called from gotten, any parameters more
    // than in that interface won't be gathered at the callsite because they
    // will not contain END markers.
    //
    // The "facade" is the interface this function uses, which must have the
    // same number of arguments and be compatible with the underlying
    // function.  At this point in time a facade might be a paramlist, but
    // it could also just be an array with an unreadable blank in slot 0.
    //
    REBCNT num_args = FUNC_FACADE_NUM_PARAMS(f->func);

    // Note: A previous optimization would use the frame's evaluation cell
    // for the argument in the case of an arity-1 function.  While this
    // avoided a chunk stack allocation, it complicates the nature of
    // looking backwards for a VALUE_FLAG_STACK's frame by introducing a
    // new parameter layout.  It also caused the code to branch more on both
    // the push and drop side, and made that cell unavailable for 1-argument
    // functions to use as a temporary.  So the optimization was removed.

    if (IS_FUNC_DURABLE(VAL_FUNC(gotten))) { // !!! Who decides durability?
        //
        // !!! It's hoped that stack frames can be "hybrids" with some pooled
        // allocated vars that survive a call, and some that go away when the
        // stack frame is finished.  The groundwork for this is laid but it's
        // not quite ready--so the classic interpretation is that it's all or
        // nothing (similar to FUNCTION! vs. CLOSURE! in this respect)
        //
        f->varlist = Make_Array(num_args + 1);
        TERM_ARRAY_LEN(f->varlist, num_args + 1);
        SET_SER_FLAG(f->varlist, SERIES_FLAG_FIXED_SIZE);

        // Skip the [0] slot which will be filled with the CTX_VALUE
        // !!! Note: Make_Array made the 0 slot an end marker
        //
        SET_TRASH_IF_DEBUG(ARR_AT(f->varlist, 0));
        f->args_head = SINK(ARR_AT(f->varlist, 1));

        // Similarly, it should not be possible to use CTX_FRAME_IF_ON_STACK
        // if the varlist does not become reified.
        //
        TRASH_POINTER_IF_DEBUG(AS_SERIES(f->varlist)->misc.f);
    }
    else {
        // We start by allocating the data for the args and locals on the chunk
        // stack.  However, this can be "promoted" into being the data for a
        // frame context if it becomes necessary to refer to the variables
        // via words or an object value.  That object's data will still be this
        // chunk, but the chunk can be freed...so the words can't be looked up.
        //
        // Note that chunks implicitly have an END at the end; no need to
        // put one there.
        //
        f->varlist = NULL;
        f->args_head = Push_Value_Chunk_Of_Length(num_args);
        assert(CHUNK_LEN_FROM_VALUES(f->args_head) == num_args);
    }

    REBCTX *exemplar = FUNC_EXEMPLAR(f->func);
    if (exemplar)
        f->special = CTX_VARS_HEAD(exemplar);
    else
        f->special = m_cast(REBVAL*, END); // literal pointer used as test

    // We want the cell to be GC safe; whether it's used by an argument or
    // not.  If it's being used as an argument then this just gets overwritten
    // but the 0 case would not initialize it...so cheaper to just set than
    // to check.  Note that this can only be done after extracting the function
    // properties, as f->gotten may be f->cell.
    //
    SET_END(&f->cell);
}


// This routine needs to be shared with the error handling code.  It would be
// nice if it were inlined into Do_Core...but repeating the code just to save
// the function call overhead is second-guessing the optimizer and would be
// a cause of bugs.
//
// Note that in response to an error, we do not want to drop the chunks,
// because there are other clients of the chunk stack that may be running.
// Hence the chunks will be freed by the error trap helper.
//
inline static void Drop_Function_Args_For_Frame_Core(
    REBFRM *f,
    REBOOL drop_chunks
) {
    // The frame may be reused for another function call, and that function
    // may not start with native code (or use native code at all).
    //
    // !!! Should the code be willing to drop the running flag off the varlist
    // as well if it is persistent, so that the values can be modified once
    // the native code is no longer running?
    //
    f->flags.bits &= ~DO_FLAG_NATIVE_HOLD;

    if (drop_chunks) {
        if (f->varlist == NULL) {
            Drop_Chunk_Of_Values(f->args_head);

            goto finished; // nothing else to do...
        }

        // A varlist may happen even with stackvars...if "singular" (e.g.
        // it's just a REBSER node for purposes of GC-referencing, but gets
        // its actual content from the stackvars.
        //
        if (ARR_LEN(f->varlist) == 1)
            Drop_Chunk_Of_Values(f->args_head);
    }
    else {
        if (f->varlist == NULL)
            goto finished;
    }

    assert(GET_SER_FLAG(f->varlist, SERIES_FLAG_ARRAY));

    if (NOT(IS_ARRAY_MANAGED(f->varlist))) {
        //
        // It's an array, but hasn't become managed yet...either because
        // it couldn't be (args still being fulfilled, may have bad cells) or
        // didn't need to be (no Context_For_Frame_May_Reify_Managed).  We
        // can just free it.
        //
        assert(IS_POINTER_TRASH_DEBUG(AS_SERIES(f->varlist)->misc.f));
        Free_Array(f->varlist);
        goto finished;
    }

    // The varlist is going to outlive this call, so the frame correspondence
    // in it needs to be cleared out, so callers will know the frame is dead.
    //
    assert(AS_SERIES(f->varlist)->misc.f == f);
    AS_SERIES(f->varlist)->misc.f = NULL;

    // The varlist might have been for indefinite extent variables, or it
    // might be a stub holder for a stack context.

    ASSERT_ARRAY_MANAGED(f->varlist);

    if (NOT(GET_SER_FLAG(f->varlist, CONTEXT_FLAG_STACK))) {
        //
        // If there's no stack memory being tracked by this context, it
        // has dynamic memory and is being managed by the garbage collector
        // so there's nothing to do.
        //
        assert(GET_SER_INFO(f->varlist, SERIES_INFO_HAS_DYNAMIC));
        goto finished;
    }

    // It's reified but has its data pointer into the chunk stack, which
    // means we have to free it and mark the array inaccessible.

    assert(GET_SER_FLAG(f->varlist, ARRAY_FLAG_VARLIST));
    assert(NOT_SER_INFO(f->varlist, SERIES_INFO_HAS_DYNAMIC));

    assert(NOT_SER_INFO(f->varlist, SERIES_INFO_INACCESSIBLE));
    SET_SER_INFO(f->varlist, SERIES_INFO_INACCESSIBLE);

finished:

    TRASH_POINTER_IF_DEBUG(f->args_head);
    TRASH_POINTER_IF_DEBUG(f->varlist);

    return; // needed for release build so `finished:` labels a statement
}


// This routine ensures that a valid REBCTX* (suitable for putting into a
// FRAME! REBVAL) exists for a Reb_Frame stack structure.
//
inline static REBCTX *Context_For_Frame_May_Reify_Managed(REBFRM *f)
{
    assert(NOT(Is_Function_Frame_Fulfilling(f)));

    if (f->varlist == NULL || NOT_SER_FLAG(f->varlist, ARRAY_FLAG_VARLIST))
        Reify_Frame_Context_Maybe_Fulfilling(f); // it's not fulfilling, here

    return AS_CONTEXT(f->varlist);
}
