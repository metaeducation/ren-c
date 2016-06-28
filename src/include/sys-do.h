//
//  File: %sys-do.h
//  Summary: {Evaluator Helper Functions and Macros}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2015 Rebol Open Source Contributors
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
// The primary routine that performs DO and DO/NEXT is called Do_Core().  It
// takes a single parameter which holds the running state of the evaluator.
// This state may be allocated on the C variable stack:  Do_Core() is
// written such that a longjmp up to a failure handler above it can run
// safely and clean up even though intermediate stacks have vanished.
//
// Ren-C can not only run the evaluator across a REBSER-style series of
// input based on index, it can also fetch those values from a standard C
// array of REBVAL[].  Alternately, it can enumerate through C's `va_list`,
// providing the ability to pass pointers as REBVAL* to comma-separated input
// at the source level.
//
// To provide even greater flexibility, it allows the very first element's
// pointer in an evaluation to come from an arbitrary source.  It doesn't
// have to be resident in the same sequence from which ensuing values are
// pulled, allowing a free head value (such as a FUNCTION! REBVAL in a local
// C variable) to be evaluated in combination from another source (like a
// va_list or series representing the arguments.)  This avoids the cost and
// complexity of allocating a series to combine the values together.
//
// These features alone would not cover the case when REBVAL pointers that
// are originating with C source were intended to be supplied to a function
// with no evaluation.  In R3-Alpha, the only way in an evaluative context
// to suppress such evaluations would be by adding elements (such as QUOTE).
// Besides the cost and labor of inserting these, the risk is that the
// intended functions to be called without evaluation, if they quoted
// arguments would then receive the QUOTE instead of the arguments.
//
// The problem was solved by adding a feature to the evaluator which was
// also opened up as a new privileged native called EVAL.  EVAL's refinements
// completely encompass evaluation possibilities in R3-Alpha, but it was also
// necessary to consider cases where a value was intended to be provided
// *without* evaluation.  This introduced EVAL/ONLY.
//


// Each iteration of DO bumps a global count, that in deterministic repro
// cases can be very helpful in identifying the "tick" where certain problems
// are occurring.  The SPORADICALLY() macro uses this to allow flipping
// between different behaviors in debug builds--usually to run the release
// behavior some of the time, and the debug behavior some of the time.  This
// exercises the release code path even when doing a debug build.
//
#ifdef NDEBUG
    #define SPORADICALLY(modulus) \
        FALSE
#else
    #define SPORADICALLY(modulus) \
        (TG_Do_Count % modulus == 0)
#endif

inline static REBOOL IS_QUOTABLY_SOFT(const RELVAL *v) {
    return LOGICAL(IS_GROUP(v) || IS_GET_WORD(v) || IS_GET_PATH(v));
}


inline static REBOOL Is_Any_Function_Frame(struct Reb_Frame *f) {
    return LOGICAL(f->eval_type == ET_FUNCTION || f->eval_type == ET_LOOKBACK);
}

// While a function frame is fulfilling its arguments, the `f->param` will
// be pointing to a typeset.  The invariant that is maintained is that
// `f->param` will *not* be a typeset when the function is actually in the
// process of running.  (So no need to set/clear/test another "mode".)
//
inline static REBOOL Is_Function_Frame_Fulfilling(struct Reb_Frame *f)
{
    assert(Is_Any_Function_Frame(f));
    return NOT_END(f->param);
}


// It's helpful when looking in the debugger to be able to look at a frame
// and see a cached string for the function it's running (if there is one).
// The release build only considers the frame symbol valid if ET_FUNCTION
//
inline static void SET_FRAME_LABEL(struct Reb_Frame *f, REBSTR *label) {
    assert(Is_Any_Function_Frame(f));
    f->label = label;
#if !defined(NDEBUG)
    f->label_debug = cast(const char*, STR_HEAD(label));
#endif
}

inline static void CLEAR_FRAME_LABEL(struct Reb_Frame *f) {
#if !defined(NDEBUG)
    f->label = NULL;
    f->label_debug = NULL;
#endif
}

inline static void SET_FRAME_VALUE(struct Reb_Frame *f, const RELVAL *value) {
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
//  DO's LOWEST-LEVEL EVALUATOR HOOKING
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This API is used internally in the implementation of Do_Core.  It does
// not speak in terms of arrays or indices, it works entirely by setting
// up a call frame (f), and threading that frame's state through successive
// operations, vs. setting it up and disposing it on each DO/NEXT step.
//
// Like higher level APIs that move through the input series, this low-level
// API can move at full DO/NEXT intervals.  Unlike the higher APIs, the
// possibility exists to move by single elements at a time--regardless of
// if the default evaluation rules would consume larger expressions.  Also
// making it different is the ability to resume after a DO/NEXT on value
// sources that aren't random access (such as C's va_arg list).
//
// One invariant of access is that the input may only advance.  Before any
// operations are called, any low-level client must have already seeded
// f->value with a valid "fetched" REBVAL*.  END is not valid input, so
// callers beginning a Do_To_End must pre-check that condition themselves
// before calling Do_Core.  And if an operation sets the c->index to END_FLAG
// then that must be checked--it's not legal to call more operations on a
// call frame after a fetch reports the end.
//
// Operations are:
//
//  FETCH_NEXT_ONLY_MAYBE_END
//
//      Retrieve next pointer for examination to f->value.  The previous
//      f->value pointer is overwritten.  (No REBVAL bits are moved by
//      this operation, only the 'currently processing' pointer reassigned.)
//      f->value may become an END marker...test with IS_END()
//
// DO_NEXT_REFETCH_MAY_THROW
//
//      Executes the already-fetched pointer, consuming as much of the input
//      as necessary to complete a /NEXT (or failing with an error).  This
//      writes the computed REBVAL into a destination location.  After the
//      operation, the next c->value pointer will already be fetched and
//      waiting for examination or use.  The returned value may be THROWN(),
//      and the f->value may IS_END().
//
// QUOTE_NEXT_REFETCH
//
//      This operation is fairly trivial in the sense that it just assigns
//      the REBVAL bits pointed to by the current value to the destination
//      cell.  Then it does a simple fetch.  The main reason for making an
//      operation vs just having callers do the two steps is to monitor
//      when some of the input has been "consumed" vs. merely fetched.
//
// This is not intending to be a "published" API of Rebol/Ren-C.  But the
// privileged level of access can be used by natives that feel they can
// optimize performance by working with the evaluator directly.
//

inline static void PUSH_CALL(struct Reb_Frame *f)
{
    f->prior = TG_Frame_Stack;
    TG_Frame_Stack = f;
    if (NOT(f->flags & DO_FLAG_VA_LIST))
        if (!GET_ARR_FLAG(f->source.array, SERIES_FLAG_LOCKED)) {
            SET_ARR_FLAG(f->source.array, SERIES_FLAG_LOCKED);
            f->flags |= DO_FLAG_TOOK_FRAME_LOCK;
        }
}

inline static void UPDATE_EXPRESSION_START(struct Reb_Frame *f) {
    assert(NOT(f->flags & DO_FLAG_VA_LIST));
    f->expr_index = f->index;
}

inline static void DROP_CALL(struct Reb_Frame *f) {
    if (f->flags & DO_FLAG_TOOK_FRAME_LOCK) {
        assert(GET_ARR_FLAG(f->source.array, SERIES_FLAG_LOCKED));
        CLEAR_ARR_FLAG(f->source.array, SERIES_FLAG_LOCKED);
    }
    assert(TG_Frame_Stack == f);
    TG_Frame_Stack = f->prior;
}


//
// Code that walks across Rebol arrays and performs evaluations must consider
// that arbitrary user code may disrupt the array being enumerated.  If the
// array is to expand, it might have a different data pointer entirely.
//
// The Reb_Enumerator abstracts whatever mechanism is used to deal with this
// problem.  That could include doing nothing at all about it and just
// incrementing the pointer and hoping for the best (R3-Alpha would do this
// often).  However Ren-C reuses the same mechanism as its evaluator, which
// is to hold a temporary lock on the array.
//

typedef struct Reb_Frame Reb_Enumerator;

inline static void PUSH_SAFE_ENUMERATOR(
    struct Reb_Frame *f,
    const REBVAL *v
) {
    SET_FRAME_VALUE(f, VAL_ARRAY_AT(v));
    f->source.array = VAL_ARRAY(v);
    f->flags = DO_FLAG_NEXT | DO_FLAG_ARGS_EVALUATE; // !!! review
    f->gotten = NULL; // tells ET_WORD and ET_GET_WORD they must do a get
    f->index = VAL_INDEX(v) + 1;
    f->specifier = VAL_SPECIFIER(v);
    f->eval_type = ET_SAFE_ENUMERATOR;
    f->pending = NULL;
    PUSH_CALL(f);
}

#define DROP_SAFE_ENUMERATOR(f) \
    DROP_CALL(f)


#if 0 && !defined(NDEBUG)
    // For detailed debugging of the fetching; coarse tool used only in very
    // deep debugging of the evaluator.
    //
    #define TRACE_FETCH_DEBUG(m,f,a) \
        Trace_Fetch_Debug((m), (f), (a))
#else
    #define TRACE_FETCH_DEBUG(m,f,a) \
        NOOP
#endif

#define VA_LIST_PENDING cast(const RELVAL*, &PG_Va_List_Pending)

//
// FETCH_NEXT_ONLY_MAYBE_END (see notes above)
//
// This routine is optimized assuming the common case is that values are
// being read out of an array.  Whether to read out of a C va_list or to use
// a "virtual" next value (e.g. an old value saved by EVAL) are both indicated
// by f->pending, hence a NULL test of that can be executed quickly.
//
inline static void FETCH_NEXT_ONLY_MAYBE_END(struct Reb_Frame *f) {
    TRACE_FETCH_DEBUG("FETCH_NEXT_ONLY_MAYBE_END", f, FALSE);

    assert(NOT_END(f->value));
    assert(f->gotten == NULL); // we'd be invalidating it!

    if (f->pending == NULL) {
        SET_FRAME_VALUE(f, ARR_AT(f->source.array, f->index));
        ++f->index;
    }
    else if (f->pending == VA_LIST_PENDING) {
        SET_FRAME_VALUE(f, va_arg(*f->source.vaptr, const REBVAL*));
        assert(
            IS_END(f->value)
            || (IS_VOID(f->value) && NOT((f)->flags & DO_FLAG_ARGS_EVALUATE))
            || !IS_RELATIVE(f->value)
        );
    }
    else {
        SET_FRAME_VALUE(f, f->pending);
        if (f->flags & DO_FLAG_VA_LIST)
            f->pending = VA_LIST_PENDING;
        else
            f->pending = NULL;
    }

    TRACE_FETCH_DEBUG("FETCH_NEXT_ONLY_MAYBE_END", f, TRUE);
}


// Things like the `case ET_WORD` run at the start of a new evaluation cycle.
// It could be the very first element evaluated, hence it might seem not
// meaningful to say it has a "left hand side" in f->out to give an infix
// (prefix, etc.) lookback function.
//
// However...it can climb the stack and peek at the eval_type of the parent to
// find SET-WORD! or SET-PATH!s in progress.  They are not products of an
// evaluation--hence are safe to quote, allowing constructs like `x: ++ 1`
//
inline static void Try_Lookback_In_Prior_Frame(
    REBVAL *out,
    struct Reb_Frame *prior
){
    switch (prior->eval_type) {
    case ET_SET_WORD:
        COPY_VALUE(out, prior->param, prior->specifier);
        assert(IS_SET_WORD(out));
        CLEAR_VAL_FLAG(out, VALUE_FLAG_EVALUATED);
        break;

    case ET_SET_PATH:
        COPY_VALUE(out, prior->param, prior->specifier);
        assert(IS_SET_PATH(out));
        CLEAR_VAL_FLAG(out, VALUE_FLAG_EVALUATED);
        break;

    default:
        SET_END(out); // some <end> args are able to tolerate absences
    }
}


//
// DO_NEXT_REFETCH_MAY_THROW provides some slick optimization for the
// evaluator, by taking the simpler cases which can be done without a nested
// frame and handing them back more immediately.
//
// Just skipping the evaluator for ET_INERT types that evaluate to themselves
// is not possible--due to the role of infix.  So the code has to be somewhat
// clever about testing for that.  Also, it is written in a way that if an
// optimization cannot be done, the work it did to find out that fact can
// be reused by Do_Core.
//
// The debug build exercises both code paths, by optimizing every other
// execution to bypass the evaluator if possible...and then throwing
// the code through Do_Core the other times.  It's a sampling test, but
// not a bad one for helping keep the methods in sync.
//
inline static void DO_NEXT_REFETCH_MAY_THROW(
    REBVAL *out,
    struct Reb_Frame *parent,
    REBUPT flags
){
    TRACE_FETCH_DEBUG("DO_NEXT_REFETCH_MAY_THROW", parent, FALSE);

    struct Reb_Frame child_frame;
    struct Reb_Frame *child = &child_frame;

    child->eval_type = Eval_Table[VAL_TYPE(parent->value)];

    // First see if it's one of the three categories we can optimize for
    // that don't necessarily require us to recurse: inert values (like
    // strings and integers and blocks), or a WORD! or GET-WORD!.  The work
    // we do with lookups here will be reused if we can't avoid a frame.
    //
    switch (child->eval_type) {
    case ET_WORD: {
        if (parent->gotten == NULL) {
            child->gotten = Get_Var_Core(
                &child->eval_type, // sets to ET_LOOKBACK or ET_FUNCTION
                parent->value,
                parent->specifier,
                GETVAR_READ_ONLY
            );
        }
        else {
            child->eval_type = Eval_Table[VAL_TYPE(parent->gotten)];
            child->gotten = parent->gotten;
            parent->gotten = NULL;
        }

        if (IS_FUNCTION(child->gotten)) {
            if (child->eval_type == ET_LOOKBACK)
                Try_Lookback_In_Prior_Frame(out, parent);
            else {
                assert(child->eval_type == ET_FUNCTION);
            }
            goto no_optimization;
        }

        if (IS_VOID(child->gotten))
            fail (Error_No_Value_Core(parent->value, parent->specifier));

        *out = *child->gotten;
        SET_VAL_FLAG(out, VALUE_FLAG_EVALUATED);

    #if !defined(NDEBUG)
        if (LEGACY(OPTIONS_LIT_WORD_DECAY) && IS_LIT_WORD(out))
            VAL_SET_TYPE_BITS(out, REB_WORD); // don't reset full header!
    #endif
        }
        break;

    case ET_GET_WORD: {
        *out = *Get_Var_Core(
            &child->eval_type, // sets to ET_LOOKBACK or ET_FUNCTION <ignored>
            parent->value,
            parent->specifier,
            GETVAR_READ_ONLY
        );
        SET_VAL_FLAG(out, VALUE_FLAG_EVALUATED);
        }
        break;

    case ET_INERT:
        COPY_VALUE(out, parent->value, parent->specifier);
        CLEAR_VAL_FLAG(out, VALUE_FLAG_EVALUATED);
        break;

    default:
        // Paths, literal functions, set-words, groups... these are all things
        // that currently need an independent frame from the parent in order
        // to hold their state.
        //
        child->gotten = NULL;
        goto no_optimization;
    }

#if !defined(NDEBUG)
    //if (SPORADICALLY(2)) goto no_optimization; // skip half the time to test
#endif

    FETCH_NEXT_ONLY_MAYBE_END(parent);
    if (IS_END(parent->value))
        return;

    child->eval_type = Eval_Table[VAL_TYPE(parent->value)];

    if ((flags & DO_FLAG_LOOKAHEAD) && (child->eval_type == ET_WORD)) {
        child->gotten = Get_Var_Core(
            &child->eval_type, // sets to ET_LOOKBACK or ET_FUNCTION
            parent->value,
            parent->specifier,
            GETVAR_READ_ONLY
        );

        // We only want to run the function if it is a lookback function,
        // otherwise we leave it prefetched in f->gotten.  It will be reused
        // on the next Do_Core call.
        //
        if (child->eval_type == ET_LOOKBACK) {
            assert(IS_FUNCTION(child->gotten));
            goto no_optimization;
        }

        child->eval_type = ET_WORD;
    }

    TRACE_FETCH_DEBUG("DO_NEXT_REFETCH_MAY_THROW", parent, TRUE);
    return;

no_optimization:
    child->out = out;
    child->source = parent->source;
    SET_FRAME_VALUE(child, parent->value);
    child->index = parent->index;
    child->specifier = parent->specifier;
    child->flags = DO_FLAG_ARGS_EVALUATE | DO_FLAG_NEXT | flags;
    child->pending = parent->pending;

    Do_Core(child);

    assert(child->eval_type != ET_LOOKBACK);
    assert(
        (child->flags & DO_FLAG_VA_LIST)
        || parent->index != child->index
    );
    parent->pending = child->pending;
    SET_FRAME_VALUE(parent, child->value);
    parent->index = child->index;
    parent->gotten = child->gotten;

    TRACE_FETCH_DEBUG("DO_NEXT_REFETCH_MAY_THROW", parent, TRUE);
}


inline static void QUOTE_NEXT_REFETCH(RELVAL *dest, struct Reb_Frame *f) {
    TRACE_FETCH_DEBUG("QUOTE_NEXT_REFETCH", f, FALSE);
    COPY_VALUE(dest, f->value, f->specifier);
    CLEAR_VAL_FLAG(dest, VALUE_FLAG_EVALUATED);
    f->gotten = NULL;
    FETCH_NEXT_ONLY_MAYBE_END(f);
    TRACE_FETCH_DEBUG("QUOTE_NEXT_REFETCH", (f), TRUE);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  BASIC API: DO_NEXT_MAY_THROW and DO_ARRAY_THROWS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This is a wrapper for a single evaluation.  If one is planning to do
// multiple evaluations, it is not as efficient as creating a frame and then
// doing `Do_Core()` calls into it.
//
// DO_NEXT_MAY_THROW takes in an array and a REBCNT offset into that array
// of where to execute.  Although the return value is a REBCNT, it is *NOT*
// always a series index!!!  It may return:
//
// DO_VAL_ARRAY_AT_THROWS is another helper for the frequent case where one
// has a BLOCK! or a GROUP! REBVAL at an index which already indicates the
// point where execution is to start.
//
// (The "Throws" name is because it's expected to usually be used in an
// 'if' statement.  It cues you into realizing that it returns TRUE if a
// THROW interrupts this current DO_BLOCK execution--not asking about a
// "THROWN" that happened as part of a prior statement.)
//
// If it returns FALSE, then the DO completed successfully to end of input
// without a throw...and the output contains the last value evaluated in the
// block (empty blocks give void).  If it returns TRUE then it will be the
// THROWN() value.
//
inline static REBIXO DO_NEXT_MAY_THROW(
    REBVAL *out,
    REBARR *array,
    REBCNT index,
    REBCTX *specifier
){
    struct Reb_Frame frame;
    struct Reb_Frame *f = &frame;

    SET_FRAME_VALUE(f, ARR_AT(array, index));
    if (IS_END(f->value)) {
        SET_VOID(out);
        return END_FLAG;
    }

    f->source.array = array;
    f->specifier = specifier;
    f->index = index + 1;
    f->flags = 0;
    f->pending = NULL;
    f->gotten = NULL;
    f->eval_type = Eval_Table[VAL_TYPE(f->value)];

    DO_NEXT_REFETCH_MAY_THROW(out, f, DO_FLAG_LOOKAHEAD);

    if (THROWN(out))
        return THROWN_FLAG;

    if (IS_END(f->value))
        return END_FLAG;

    assert(f->index > 1);
    return f->index - 1;
}

inline static REBOOL Do_At_Throws(
    REBVAL *out,
    REBARR *array,
    REBCNT index,
    REBCTX *specifier
){
    return LOGICAL(
        THROWN_FLAG == Do_Array_At_Core(
            out,
            NULL,
            array,
            index,
            specifier,
            DO_FLAG_TO_END | DO_FLAG_ARGS_EVALUATE | DO_FLAG_LOOKAHEAD
        )
    );
}

// Note: It is safe for `out` and `array` to be the same variable.  The
// array and index are extracted, and will be protected from GC by the DO
// state...so it is legal to e.g DO_VAL_ARRAY_AT_THROWS(D_OUT, D_OUT).
//
inline static REBOOL DO_VAL_ARRAY_AT_THROWS(
    REBVAL *out,
    const REBVAL *any_array
){
    return Do_At_Throws(
        out,
        VAL_ARRAY(any_array),
        VAL_INDEX(any_array),
        VAL_SPECIFIER(any_array)
    );
}

// Because Do_Core can seed with a single value, we seed with our value and
// an EMPTY_ARRAY.  Revisit if there's a "best" dispatcher.  Note this is
// an EVAL and not a DO...hence if you pass it a block, then the block will
// just evaluate to itself!
//
inline static REBOOL EVAL_VALUE_CORE_THROWS(
    REBVAL *out,
    const RELVAL *value,
    REBCTX *specifier
){
    return LOGICAL(
        THROWN_FLAG == Do_Array_At_Core(
            out,
            value,
            EMPTY_ARRAY,
            0,
            specifier,
            DO_FLAG_TO_END | DO_FLAG_ARGS_EVALUATE | DO_FLAG_LOOKAHEAD
        )
    );
}

#define EVAL_VALUE_THROWS(out,value) \
    EVAL_VALUE_CORE_THROWS((out), (value), SPECIFIED)


//=////////////////////////////////////////////////////////////////////////=//
//
//  PATH VALUE STATE "PVS"
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The path value state structure is used by `Do_Path_Throws()` and passed
// to the dispatch routines.  See additional comments in %c-path.c.
//

struct Reb_Path_Value_State {
    //
    // `item` is the current element within the path that is being processed.
    // It is advanced as the path is consumed.
    //
    const RELVAL *item;

    // A specifier is needed because the PATH! is processed by incrementing
    // through values, which may be resident in an array that was part of
    // the cloning of a function body.  The specifier allows the path
    // evaluation to disambiguate which variable a word's relative binding
    // would match.
    //
    REBCTX *item_specifier;

    // `selector` is the result of evaluating the current path item if
    // necessary.  So if the path is `a/(1 + 2)` and processing the second
    // `item`, then the selector would be the computed value `3`.
    //
    // (This is what the individual path dispatchers should use.)
    //
    const REBVAL *selector;

    // !!! `selector_temp` was added as a patch to push the temporary
    // variable used to hold evaluated selectors into the PVS, when it was
    // observed that callers of Next_Path() were expecting the selector to
    // survive the call.  That meant it couldn't be in a C stack temporary.
    // The method needs serious review, but keeping the temporary used for
    // the selector in the PVS doesn't incur any more storage and bridges
    // past the problem for now.
    //
    REBVAL selector_temp;

    // `value` holds the path value that should be chained from.  (It is the
    // type of `value` that dictates which dispatcher is given the `selector`
    // to get the next step.)  This has to be a relative value in order to
    // use the SET_IF_END option which writes into arrays.
    //
    RELVAL *value;

    // `value_specifier` has to be updated whenever value is updated
    //
    REBCTX *value_specifier;

    // `store` is the storage for constructed values, and also where any
    // thrown value will be written.
    //
    REBVAL *store;

    // `setval` is non-NULL if this is a SET-PATH!, and it is the value to
    // ultimately set the path to.  The set should only occur at the end
    // of the path, so most setters should check `IS_END(pvs->item + 1)`
    // before setting.
    //
    // !!! See notes in %c-path.c about why the path dispatch is more
    // complicated than simply being able to only pass the setval to the last
    // item being dispatched (which would be cleaner, but some cases must
    // look ahead with alternate handling).
    //
    const REBVAL *opt_setval;

    // `orig` original path input, saved for error messages
    //
    const RELVAL *orig;
};


enum Path_Eval_Result {
    PE_OK, // pvs->value points to the element to take the next selector
    PE_SET_IF_END, // only sets if end of path
    PE_USE_STORE, // set pvs->value to be pvs->store
    PE_NONE // set pvs->store to NONE and then pvs->value to pvs->store
};

typedef REBINT (*REBPEF)(REBPVS *pvs); // Path evaluator function

typedef REBINT (*REBCTF)(const RELVAL *a, const RELVAL *b, REBINT s);


//=////////////////////////////////////////////////////////////////////////=//
//
//  ARGUMENT AND PARAMETER ACCESS HELPERS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// These accessors are designed to make it convenient for natives and actions
// written in C to access their arguments and refinements.  They are able
// to bind to the implicit Reb_Frame* passed to every REBNATIVE() and read
// the information out cleanly, like this:
//
//     PARAM(1, foo);
//     REFINE(2, bar);
//
//     if (IS_INTEGER(ARG(foo)) && REF(bar)) { ... }
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

struct Native_Param {
#if !defined(NDEBUG)
    enum Reb_Kind kind_cache;
    REBVAL *arg;
#endif
    int num;
};

struct Native_Refine {
#if !defined(NDEBUG)
    REBOOL used_cache;
    REBVAL *arg;
#endif
    int num;
};

#ifdef NDEBUG
    #define PARAM(n,name) \
        const struct Native_Param p_##name = {n}

    #define REFINE(n,name) \
        const struct Native_Refine p_##name = {n}
#else
    // Capture the argument (and its type) for debug inspection.
    //
    #define PARAM(n,name) \
        const struct Native_Param p_##name = { \
            VAL_TYPE(frame_->arg + (n) - 1), \
            frame_->arg + (n) - 1, \
            (n) \
        }

    // As above, do a cache and be tolerant of framelessness.
    //
    #define REFINE(n,name) \
        const struct Native_Refine p_##name = { \
            IS_CONDITIONAL_TRUE(frame_->arg + (n) - 1), \
            frame_->arg + (n) - 1, \
            (n) \
        }
#endif

// Though REF can only be used with a REFINE() declaration, ARG can be used
// with either.
//
#define ARG(name) \
    (frame_->arg + (p_##name).num - 1)

#define PAR(name) \
    FUNC_PARAM(frame_->func, (p_##name).num) // a TYPESET!

#ifdef NDEBUG
    #define REF(name) \
        IS_CONDITIONAL_TRUE(ARG(name))
#else
    // An added useless ?: helps check in debug build to make sure we do not
    // try to use REF() on something defined as PARAM(), but only REFINE()
    //
    #define REF(name) \
        ((p_##name).used_cache \
            ? IS_CONDITIONAL_TRUE(ARG(name)) \
            : IS_CONDITIONAL_TRUE(ARG(name)))
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  CALL FRAME ACCESS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// !!! To be documented and reviewed.  Legacy naming conventions when the
// arguments to functions lived in the data stack gave the name "FS_TOP" for
// "(D)ata (S)tack (F)rame" which is no longer accurate, as well as the
// convention of prefix with a D_.  The new PARAM()/REFINE()/ARG()/REF()
// scheme is coming up on replacing a lot of it, so these will be needing
// a tune up and choices of better names once that is sorted out.  It
// may be as simple as changing these to FS/FSP for "frame stack pointer"
//

#define FS_TOP (TG_Frame_Stack + 0) // avoid assignment to FS_TOP via + 0

#define FRM_IS_VALIST(f) \
    LOGICAL((f)->flags & DO_FLAG_VA_LIST)

#define FRM_ARRAY(f) \
    (assert(!FRM_IS_VALIST(f)), (f)->source.array)

// !!! Though the evaluator saves its `indexor`, the index is not meaningful
// in a valist.  Also, if `opt_head` values are used to prefetch before an
// array, those will be lost too.  A true debugging mode would need to
// convert these cases to ordinary arrays before running them, in order
// to accurately present the errors.
//
#define FRM_INDEX(f) \
    (assert(!FRM_IS_VALIST(f)), IS_END((f)->value) \
        ? ARR_LEN((f)->source.array) : (f)->index - 1)

#define FRM_EXPR_INDEX(f) \
    (assert(!FRM_IS_VALIST(f)), (f)->expr_index == END_FLAG \
        ? ARR_LEN((f)->source.array) : (f)->expr_index - 1)

#define FRM_OUT(f)          cast(REBVAL * const, (f)->out) // writable Lvalue
#define FRM_PRIOR(f)        ((f)->prior)
#define FRM_LABEL(f)        ((f)->label)

#define FRM_FUNC(f)         ((f)->func)
#define FRM_DSP_ORIG(f)     ((f)->dsp_orig + 0) // Lvalue

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

// It's not clear exactly in which situations one might be using this; while
// it seems that when filling function args you could just assume it hasn't
// been reified, there may be "pre-reification" in the future, and also a
// tail call optimization or some other "reuser" of a frame may jump in and
// reuse a frame that's been reified after its initial "chunk only" state.
// For now check the flag and don't just assume it's a raw frame.
//
// Uses ARR_AT instead of CTX_VAR because the varlist may not be finished.
//
#define FRM_ARGS_HEAD(f) \
    ((f)->stackvars ? &(f)->stackvars[0] : KNOWN(ARR_AT((f)->varlist, 1)))

// ARGS is the parameters and refinements
// 1-based indexing into the arglist (0 slot is for object/function value)
#ifdef NDEBUG
    #define FRM_ARG(f,n)    ((f)->arg + (n) - 1)
#else
    #define FRM_ARG(f,n)    FRM_ARG_Debug((f), (n)) // checks arg index bound
#endif

// Note about D_NUM_ARGS: A native should generally not detect the arity it
// was invoked with, (and it doesn't make sense as most implementations get
// the full list of arguments and refinements).  However, ACTION! dispatch
// has several different argument counts piping through a switch, and often
// "cheats" by using the arity instead of being conditional on which action
// ID ran.  Consider when reviewing the future of ACTION!.
//
#define FRM_NUM_ARGS(f) \
    cast(REBCNT, FUNC_NUM_PARAMS((f)->func))

// Quick access functions from natives (or compatible functions that name a
// Reb_Frame pointer `frame_`) to get some of the common public fields.
//
#define D_OUT       FRM_OUT(frame_)         // GC-safe slot for output value
#define D_ARGC      FRM_NUM_ARGS(frame_)        // count of args+refinements/args
#define D_ARG(n)    FRM_ARG(frame_, (n))    // pass 1 for first arg
#define D_REF(n)    IS_CONDITIONAL_TRUE(D_ARG(n))  // REFinement (!REFerence)
#define D_FUNC      FRM_FUNC(frame_)        // REBVAL* of running function
#define D_LABEL_SYM FRM_LABEL(frame_)       // symbol or placeholder for call
#define D_DSP_ORIG  FRM_DSP_ORIG(frame_)    // Original data stack pointer

#define D_PROTECT_X(v)      PROTECT_FRM_X(frame_, (v))
#define D_PROTECT_Y(v)      PROTECT_FRM_Y(frame_, (v))

// Frameless native access
//
// !!! Should `frame_` just be renamed to `c_` to make this briefer and be used
// directly?  It is helpful to have macros to find the usages, however.
//
#define D_FRAME      frame_
#define D_ARRAY     (frame_->source.array)
#define D_INDEXOR   (frame_->indexor)
#define D_VALUE     (frame_->value)
#define D_MODE      (frame_->mode)
