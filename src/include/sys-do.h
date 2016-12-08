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
// Ren-C can run the evaluator across a REBARR-style series of input based on
// index.  It can also enumerate through C's `va_list`, providing the ability
// to pass pointers as REBVAL* to comma-separated input at the source level.
// (Someday it may fetch values from a standard C array of REBVAL[] as well.) 
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

inline static void PUSH_CALL(REBFRM *f)
{
    f->prior = TG_Frame_Stack;
    TG_Frame_Stack = f;
    if (NOT(f->flags.bits & DO_FLAG_VA_LIST))
        if (ARR_SERIES(f->source.array)->header.bits & REBSER_FLAG_RUNNING)
            NOOP; // already temp-locked
        else {
            ARR_SERIES(f->source.array)->header.bits |= REBSER_FLAG_RUNNING;
            f->flags.bits |= DO_FLAG_TOOK_FRAME_LOCK;
        }
}

inline static void UPDATE_EXPRESSION_START(REBFRM *f) {
    assert(NOT(f->flags.bits & DO_FLAG_VA_LIST));
    f->expr_index = f->index;
}

inline static void DROP_CALL(REBFRM *f) {
    if (f->flags.bits & DO_FLAG_TOOK_FRAME_LOCK) {
        assert(
            ARR_SERIES(f->source.array)->header.bits & REBSER_FLAG_RUNNING
        );
        ARR_SERIES(f->source.array)->header.bits &=
            ~cast(REBUPT, REBSER_FLAG_RUNNING);
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

typedef REBFRM Reb_Enumerator;

inline static void PUSH_SAFE_ENUMERATOR(
    REBFRM *f,
    const REBVAL *v
) {
    SET_FRAME_VALUE(f, VAL_ARRAY_AT(v));
    f->source.array = VAL_ARRAY(v);

    Init_Header_Aliased(&f->flags, DO_FLAG_NORMAL);

    f->gotten = NULL; // tells ET_WORD and ET_GET_WORD they must do a get
    f->index = VAL_INDEX(v) + 1;
    f->specifier = VAL_SPECIFIER(v);
    f->eval_type = REB_MAX_VOID;
    f->pending = NULL;
    f->out = m_cast(REBVAL*, END_CELL); // no out here, but needs to be GC safe
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
inline static void FETCH_NEXT_ONLY_MAYBE_END(REBFRM *f) {
    TRACE_FETCH_DEBUG("FETCH_NEXT_ONLY_MAYBE_END", f, FALSE);

    // If f->value is pointing to f->cell, it's possible that it may wind up
    // with an END in it between fetches if f->cell gets reused (as in when
    // arguments are pushed for a function)
    //
    assert(NOT_END(f->value) || f->value == &f->cell);

    assert(f->gotten == NULL); // we'd be invalidating it!

    if (f->pending == NULL) {
        SET_FRAME_VALUE(f, ARR_AT(f->source.array, f->index));
        ++f->index;
    }
    else if (f->pending == VA_LIST_PENDING) {
        SET_FRAME_VALUE(f, va_arg(*f->source.vaptr, const REBVAL*));
        assert(
            IS_END(f->value)
            || (IS_VOID(f->value) && (f->flags.bits & DO_FLAG_NO_ARGS_EVALUATE))
            || !IS_RELATIVE(f->value)
        );
    }
    else {
        SET_FRAME_VALUE(f, f->pending);
        if (f->flags.bits & DO_FLAG_VA_LIST)
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
// However...it can look at the data stack and peek to find SET-WORD! or
// SET-PATH!s in progress.  They are not products of an evaluation--hence are
// safe to quote, allowing constructs like `x: ++ 1`
//
inline static void Lookback_For_Set_Word_Or_Set_Path(REBVAL *out, REBFRM *f)
{
    if (DSP == f->dsp_orig) {
        SET_END(out); // some <end> args are able to tolerate absences
        return;
    }

    enum Reb_Kind kind = VAL_TYPE(DS_TOP);
    if (kind == REB_SET_WORD) {
        *out = *DS_TOP;
        SET_VAL_FLAG(out, VALUE_FLAG_UNEVALUATED);
        VAL_SET_TYPE_BITS(DS_TOP, REB_GET_WORD); // See Do_Core/ET_SET_WORD
    }
    else if (kind == REB_SET_PATH) {
        //
        // The purpose of capturing a SET-PATH! on the left of a lookback
        // operation is to set it.  Currently the guarantee required is that
        // `x: x/y: z: m/n/o: whatever` will give all values the same thing,
        // and to assure that a GET is run after the operation.  But if there
        // are GROUP!s in the path, then it would evaluate twice.  Avoid it
        // by disallowing lookback capture of paths containing GROUP!
        //
        RELVAL *temp = VAL_ARRAY_AT(DS_TOP);
        for (; NOT_END(temp); ++temp)
            if (IS_GROUP(temp))
                fail (Error(RE_INFIX_PATH_GROUP, temp));

        *out = *DS_TOP;
        SET_VAL_FLAG(out, VALUE_FLAG_UNEVALUATED);
        VAL_SET_TYPE_BITS(DS_TOP, REB_GET_PATH); // See Do_Core/ET_SET_PATH
    }
    else {
        assert(FALSE); // !!! impossible?
    }
}


// Note that this operation may change variables such that something that
// was a FUNCTION! before is no longer, or something that isn't a function
// becomes one.  Set f->gotten to NULL in cases where it may affect it
//
// !!! This temporarily disallows throws from SET-PATH!s.  Longer term, the
// SET-PATH! will be evaluated into a "sink" and pushed, so left-to-right
// consistency is maintained, but in the meantime it will be an error if
// one tries to do `foo/baz/(either condition ['bar] [return 10]): 20`
//
inline static void Do_Pending_Sets_May_Invalidate_Gotten(
    REBVAL *out,
    REBFRM *f
) {
    while (DSP != f->dsp_orig) {
        switch (VAL_TYPE(DS_TOP)) {
        case REB_SET_WORD: {
            f->refine = GET_MUTABLE_VAR_MAY_FAIL(DS_TOP, SPECIFIED);
            *f->refine = *out;
            if (f->refine == f->gotten)
                f->gotten = NULL;
            break; }

        case REB_GET_WORD:
            //
            // If the evaluation did a "look back" and captured this SET-WORD!
            // then whatever it does to the word needs to stick as its value.
            // It will mutate the eval_type to ET_GET_WORD to tell us to
            // evaluate to whatever the variable's value is.  (In particular,
            // this allows ENFIX to do a SET/LOOKBACK on an operator and then
            // not be undone by overwriting it again.)
            //
            *out = *GET_OPT_VAR_MAY_FAIL(DS_TOP, SPECIFIED);
            break;

        case REB_SET_PATH: {
            REBVAL hack = *DS_TOP; // can't path eval from data stack, yet

            enum Reb_Kind eval_type_saved = f->eval_type;
            f->eval_type = REB_MAX_VOID; // for error handling

            if (Do_Path_Throws_Core(
                &f->cell, // output location if thrown
                NULL, // not requesting symbol means refinements not allowed
                &hack, // param is currently holding SET-PATH! we got in
                SPECIFIED, // needed to resolve relative array in path
                out
            )) {
                fail (Error_No_Catch_For_Throw(&f->cell));
            }

            f->eval_type = eval_type_saved;

            // Arbitrary code just ran.  Assume the worst, that it may have
            // changed gotten.  (Future model it may be easier to test this.)
            //
            f->gotten = NULL;

            // leave VALUE_FLAG_UNEVALUATED as is
            break; }

        case REB_GET_PATH: {
        #if !defined(NDEBUG)
            REBDSP dsp_before = DSP;
        #endif

            REBVAL hack = *DS_TOP; // can't path eval from data stack, yet

            if (Do_Path_Throws_Core(
                out, // output location if thrown
                NULL, // not requesting symbol means refinements not allowed
                &hack, // param is currently holding SET-PATH! we got in
                SPECIFIED, // needed to resolve relative array in path
                NULL // nothing provided to SET, so it's a GET
            )) {
                fail (Error_No_Catch_For_Throw(out));
            }

            // leave VALUE_FLAG_UNEVALUATED as is

            // We did not pass in a symbol, so not a call... hence we cannot
            // process refinements.  Should not get any back.
            //
            assert(DSP == dsp_before);
            break; }

        default:
            assert(FALSE);
        }

        DS_DROP;
    }
}


// Getting an a-priori answer for any argument fulfillment on whether that
// argument is the last one to the function is not necessarily easy in the
// general case, due to the way refinements and specializations affect the
// answer.  But it only needs to be known occasionally, when encountering an
// enfixed function that has a deferred left-hand side.  Since it can't be
// precomputed, this routine does the computation when needed.
//
// Falsification can be returned fairly quickly, based on finding *any*
// subsequent argument.  And the true case is generally quick as well, since
// it occurs at the end of the arguments.
//
inline static REBOOL Fulfilling_Last_Argument(struct Reb_Frame *f) {
    if (NOT(f->eval_type == REB_FUNCTION || f->eval_type == REB_0_LOOKBACK))
        return FALSE;

    // Function may be on the stack and calling the evaluator after its args
    // have already been fulfilled.
    //
    if (!Is_Function_Frame_Fulfilling(f))
        return FALSE;

    const REBVAL *special = f->special;
    assert(special != f->arg); // only true if R_REDO_CHECKED (has all args)

    const RELVAL *param = f->param;
    ++param; // special was already bumped if needed by Do_Core()

    assert(
        f->refine == EMPTY_BLOCK // just an ordinary argument
        || IS_CONDITIONAL_TRUE(f->refine) // refinement arg hasn't been revoked
        || f->refine == FALSE_VALUE // arg to revoked refinement (must be void)
    );

    // We may be currently fulfilling a refinement's arguments, but that's not
    // relevant.  All we're interested in are *subsequent* refinements, and
    // whether they are in use and have unspecialized arguments.  Note that
    // "subsequent refinements" may actually occur prior to our current
    // position in the traversal, e.g. in `foo/baz/bar a b c` we may be
    // fulfilling /BAZ first, even though it may come after /BAR in the
    // definition (with /BAR needing to be "picked up".)
    //
    const REBVAL *pickup = EMPTY_BLOCK;

    while (NOT_END(param)) {
        switch (VAL_PARAM_CLASS(param)) {
        case PARAM_CLASS_LOCAL:
        case PARAM_CLASS_RETURN:
        case PARAM_CLASS_LEAVE:
            // just skip locals (they're not arguments fulfilled at callsite)
            if (special != END_CELL)
                ++special;
            break;
        
        case PARAM_CLASS_REFINEMENT:
            if (f->dsp_orig == DSP)
                return TRUE; // no refinements left to consider or pick up

            if (pickup != NULL && pickup != EMPTY_BLOCK) {
                assert(IS_VARARGS(pickup));
                goto next_pickup; // finished a pickup, so find next one
            }

            // If we weren't doing a pickup, then we may hit a refinement that
            // is going to be in use in the original traversal (these will be
            // WORD! on the stack).  First see if it was specialized TRUE/FALSE

            if (special != END_CELL) {
                if (IS_VOID(special))
                    ++special; // unspecialized refinement, fall through
                else {
                    if (!IS_LOGIC(special))
                        fail (Error_Non_Logic_Refinement(f));

                    if (IS_CONDITIONAL_TRUE(special))
                        pickup = EMPTY_BLOCK;
                    else
                        pickup = NULL;

                    ++special;
                    goto next_param;
                }
            }

            // If the refinement isn't specialized, go ahead and look on the
            // stack to see if it was part of the invocation (via path, pushed
            // a word to the stack).

            pickup = DS_TOP;
            do {
                if (IS_WORD(pickup)) {
                    if (VAL_PARAM_CANON(param) == VAL_WORD_SPELLING(pickup)) {
                        pickup = EMPTY_BLOCK; // found, but not a "pickup"
                        goto next_param;
                    }
                }
                else {
                    assert(
                        IS_VARARGS(pickup) // for later pickups
                        || IS_SET_WORD(pickup) // pending SET-WORD!
                        || IS_SET_PATH(pickup) // pending SET-PATH!
                    );
                }

                --pickup;
            } while (pickup != DS_AT(f->dsp_orig));

            pickup = NULL; // refinement not in use
            break;

        case PARAM_CLASS_NORMAL:
        case PARAM_CLASS_HARD_QUOTE:
        case PARAM_CLASS_SOFT_QUOTE:
            if (GET_VAL_FLAG(f->param, TYPESET_FLAG_VARIADIC)) {
                //
                // Subsequent variadic parameters do not count; they will
                // always report themselves a "last argument".  So if foo has
                // one normal parameter and then a variadic parameter, then
                // `foo "normal" |> ...` will defer the operator as
                // `(foo "normal") |> ...` even though there are potentially
                // more variadic arguments, because the deferring operator
                // will be seen as an <end> by the variadic.
                //
                if (special != END_CELL)
                    ++special;
                goto next_param;
            }

            if (pickup) { // "in use"
                if (special == END_CELL)
                    return FALSE; // no specialization, so this is another arg!

                if (IS_VOID(special))
                    return FALSE; // specialization w/this arg unspecialized!

                ++special; // specialized argument, skip it...
            }
            else { // "not used"
                if (special != END_CELL)
                    ++special;
            }
            break;

        default:
            assert(FALSE);
        }

    next_param:
        ++param;
    }

    if (pickup == NULL || pickup == EMPTY_BLOCK)
        pickup = DS_TOP; // get first pickup candidate (may be stack bottom)
    else {
    next_pickup:
        assert(IS_VARARGS(pickup));
        --pickup; // get previous pickup candidate (or bottom)
    }

    // Figure out if candidate is actual pickup, and keep stepping backwards
    // in the stack until it is exhausted or pickup is found.
    //
    for (; pickup > DS_AT(f->dsp_orig); --pickup) {
        if (IS_VARARGS(pickup)) {
            param = pickup->payload.varargs.param;
            goto next_param;
        }
        assert(
            IS_WORD(pickup)
            || IS_SET_WORD(pickup)
            || IS_SET_PATH(pickup)
        );
    }

    return TRUE; // never found another arg to be fulfilled after current
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
    REBFRM *parent,
    REBUPT flags
){
    TRACE_FETCH_DEBUG("DO_NEXT_REFETCH_MAY_THROW", parent, FALSE);

    REBFRM child_frame;
    REBFRM *child = &child_frame;

    child->eval_type = VAL_TYPE(parent->value);

    // First see if it's one of the three categories we can optimize for
    // that don't necessarily require us to recurse: inert values (like
    // strings and integers and blocks), or a WORD! or GET-WORD!.  The work
    // we do with lookups here will be reused if we can't avoid a frame.
    //
    if (IS_KIND_INERT(child->eval_type)) {
        COPY_VALUE(out, parent->value, parent->specifier);
        SET_VAL_FLAG(out, VALUE_FLAG_UNEVALUATED);
    }
    else {
        switch (child->eval_type) {
        case REB_WORD: {
            if (parent->gotten == NULL) {
                child->gotten = Get_Var_Core(
                    &child->eval_type, // sets to ET_LOOKBACK or ET_FUNCTION
                    parent->value,
                    parent->specifier,
                    GETVAR_READ_ONLY
                );
            }
            else {
                child->eval_type = VAL_TYPE(parent->gotten);
                child->gotten = parent->gotten;
                parent->gotten = NULL;
            }

            if (IS_FUNCTION(child->gotten)) {
                if (child->eval_type == REB_0_LOOKBACK)
                    Lookback_For_Set_Word_Or_Set_Path(out, parent);
                else {
                    assert(child->eval_type == REB_FUNCTION);
                    SET_END(out);
                }
                goto no_optimization;
            }

            if (IS_VOID(child->gotten))
                fail (Error_No_Value_Core(parent->value, parent->specifier));

            *out = *child->gotten;
            CLEAR_VAL_FLAG(out, VALUE_FLAG_UNEVALUATED);

        #if !defined(NDEBUG)
            if (LEGACY(OPTIONS_LIT_WORD_DECAY) && IS_LIT_WORD(out))
                VAL_SET_TYPE_BITS(out, REB_WORD); // don't reset full header!
        #endif
            }
            break;

        case REB_GET_WORD: {
            *out = *Get_Var_Core(
                &child->eval_type, // sets to ET_LOOKBACK or ET_FUNCTION <ignored>
                parent->value,
                parent->specifier,
                GETVAR_READ_ONLY
            );
            CLEAR_VAL_FLAG(out, VALUE_FLAG_UNEVALUATED);
            }
            break;

        default:
            // Paths, literal functions, set-words, groups... these are all things
            // that currently need an independent frame from the parent in order
            // to hold their state.
            //
            child->gotten = NULL;
            SET_END(out);
            goto no_optimization;
        }
    }

#if !defined(NDEBUG)
    //if (SPORADICALLY(2)) goto no_optimization; // skip half the time to test
#endif

    FETCH_NEXT_ONLY_MAYBE_END(parent);
    if (IS_END(parent->value))
        return;

    child->eval_type = VAL_TYPE(parent->value);

    if (NOT(flags & DO_FLAG_NO_LOOKAHEAD) && (child->eval_type == REB_WORD)) {
        child->gotten = Get_Var_Core(
            &child->eval_type, // sets to REB_LOOKBACK or REB_FUNCTION
            parent->value,
            parent->specifier,
            GETVAR_READ_ONLY | GETVAR_UNBOUND_OK
        );

        // We only want to run the function if it is a lookback function,
        // otherwise we leave it prefetched in f->gotten.  It will be reused
        // on the next Do_Core call.
        //
        if (child->eval_type == REB_0_LOOKBACK) {
            assert(IS_FUNCTION(child->gotten));

            // Run the lookback if we can't afford to defer it -or- if it's
            // not the kind that is deferred.
            if (
                !GET_VAL_FLAG(child->gotten, FUNC_FLAG_DEFERS_LOOKBACK_ARG)
                || (
                    NOT(flags & DO_FLAG_VARIADIC_TAKE)
                    && NOT(Fulfilling_Last_Argument(parent))
                )
            ) {
                goto no_optimization;
            }
        }
        else
            child->eval_type = REB_WORD;
    }

    TRACE_FETCH_DEBUG("DO_NEXT_REFETCH_MAY_THROW", parent, TRUE);
    return;

no_optimization:
    child->out = out;

    child->source = parent->source;
    SET_FRAME_VALUE(child, parent->value);
    child->index = parent->index;
    child->specifier = parent->specifier;
    child->flags.bits = flags;
    child->pending = parent->pending;

    Do_Core(child);

    // It is technically possible to wind up with child->eval_type as
    // REB_0_LOOKBACK here if a lookback's first argument does not allow
    // lookahead.  e.g. `print 1 + 2 <| print 1 + 7` wishes to print 3
    // and then 8, rather than print 8 and then evaluate

    assert(
        (child->flags.bits & DO_FLAG_VA_LIST)
        || parent->index != child->index
        || THROWN(out)
    );
    parent->pending = child->pending;
    SET_FRAME_VALUE(parent, child->value);
    parent->index = child->index;
    parent->gotten = child->gotten;

    TRACE_FETCH_DEBUG("DO_NEXT_REFETCH_MAY_THROW", parent, TRUE);
}


inline static void QUOTE_NEXT_REFETCH(REBVAL *dest, REBFRM *f) {
    TRACE_FETCH_DEBUG("QUOTE_NEXT_REFETCH", f, FALSE);
    COPY_VALUE(dest, f->value, f->specifier);
    SET_VAL_FLAG(dest, VALUE_FLAG_UNEVALUATED);
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
    REBFRM frame;
    REBFRM *f = &frame;

    SET_FRAME_VALUE(f, ARR_AT(array, index));
    if (IS_END(f->value)) {
        SET_VOID(out);
        return END_FLAG;
    }

    f->source.array = array;
    f->specifier = specifier;
    f->index = index + 1;

    Init_Header_Aliased(&f->flags, 0); // ??? is this ever looked at?

    f->pending = NULL;
    f->gotten = NULL;
    f->eval_type = VAL_TYPE(f->value);

    DO_NEXT_REFETCH_MAY_THROW(out, f, DO_FLAG_NORMAL);

    if (THROWN(out))
        return THROWN_FLAG;

    if (IS_END(f->value))
        return END_FLAG;

    assert(f->index > 1);
    return f->index - 1;
}


// Most common case of evaluator invocation in Rebol: the data lives in an
// array series.  Generic routine takes flags and may act as either a DO
// or a DO/NEXT at the position given.  Option to provide an element that
// may not be resident in the array to kick off the execution.
//
inline static REBIXO Do_Array_At_Core(
    REBVAL *out,
    const RELVAL *opt_first, // must also be relative to specifier if relative
    REBARR *array,
    REBCNT index,
    REBCTX *specifier,
    REBFLGS flags
) {
    REBFRM f;

    if (opt_first) {
        SET_FRAME_VALUE(&f, opt_first);
        f.index = index;
    }
    else {
        // Do_Core() requires caller pre-seed first value, always
        //
        SET_FRAME_VALUE(&f, ARR_AT(array, index));
        f.index = index + 1;
    }

    if (IS_END(f.value)) {
        SET_VOID(out);
        return END_FLAG;
    }

    SET_END(out);
    f.out = out;

    f.source.array = array;
    f.specifier = specifier;

    Init_Header_Aliased(&f.flags, flags); // see notes on definition

    f.gotten = NULL; // so ET_WORD and ET_GET_WORD do their own Get_Var
    f.pending = NULL;

    f.eval_type = VAL_TYPE(f.value);

    Do_Core(&f);

    if (THROWN(f.out))
        return THROWN_FLAG; // !!! prohibits recovery from exits

    return IS_END(f.value) ? END_FLAG : f.index;
}


// !!! Not yet implemented--concept is to accept a REBVAL[] array, rather
// than a REBARR of values.
//
// !!! Considerations of this core interface are to see the values as being
// potentially in non-contiguous points in memory, and advanced with some
// skip length between them.  Additionally the idea of some kind of special
// Rebol value or "REB_INSTRUCTION" to say how far to skip is a possibility,
// which would be more general in the sense that it would allow the skip
// distances to be generalized, though this would cost a pointer size
// entity at each point.  The advantage of REB_INSTRUCTION is that only the
// clients using the esoteric ability would be paying anything for it or
// the API complexity, but if an important client like Ren-C++ it might
// be worth the savings.
//
// Note: Functionally it would be possible to assume a 0 index and require
// the caller to bump the value pointer as necessary.  But an index-based
// interface is likely useful to avoid the bookkeeping required for the caller.
//
/*inline static REBIXO Do_Values_At_Core(
    REBVAL *out,
    REBFLGS flags,
    const REBVAL *opt_head,
    const REBVAL values[],
    REBCNT index
) {
    fail (Error(RE_MISC));
}*/


//
//  Reify_Va_To_Array_In_Frame: C
//
// For performance and memory usage reasons, a variadic C function call that
// wants to invoke the evaluator with just a comma-delimited list of REBVAL*
// does not need to make a series to hold them.  Do_Core is written to use
// the va_list traversal as an alternate to DO-ing an ARRAY.
//
// However, va_lists cannot be backtracked once advanced.  So in a debug mode
// it can be helpful to turn all the va_lists into arrays before running
// them, so stack frames can be inspected more meaningfully--both for upcoming
// evaluations and those already past.
//
// A non-debug reason to reify a va_list into an array is if the garbage
// collector needs to see the upcoming values to protect them from GC.  In
// this case it only needs to protect those values that have not yet been
// consumed.
//
// Because items may well have already been consumed from the va_list() that
// can't be gotten back, we put in a marker to help hint at the truncation
// (unless told that it's not truncated, e.g. a debug mode that calls it
// before any items are consumed).
//
inline static void Reify_Va_To_Array_In_Frame(
    REBFRM *f,
    REBOOL truncated
) {
    REBDSP dsp_orig = DSP;

    assert(f->flags.bits & DO_FLAG_VA_LIST);

    if (truncated) {
        REBVAL temp;
        Val_Init_Word(&temp, REB_WORD, Canon(SYM___OPTIMIZED_OUT__));

        DS_PUSH(&temp);
    }

    if (NOT_END(f->value)) {
        do {
            DS_PUSH_RELVAL(f->value, f->specifier); // may be void
            FETCH_NEXT_ONLY_MAYBE_END(f);
        } while (NOT_END(f->value));

        if (truncated)
            f->index = 2; // skip the --optimized-out--
        else
            f->index = 1; // position at the start of the extracted values
    }
    else {
        // Leave at the END, but give back the array to serve as
        // notice of the truncation (if it was truncated)
        //
        f->index = 0;
    }

    f->source.array = Pop_Stack_Values(dsp_orig); // may contain voids
    MANAGE_ARRAY(f->source.array); // held alive while frame running
    SET_ARR_FLAG(f->source.array, ARRAY_FLAG_VOIDS_LEGAL);

    // The array just popped into existence, and it's tied to a running
    // frame...so safe to say we locked it.  (This would be more complex if
    // we reused the empty array if dsp_orig == DSP, since someone else
    // might have it locked...not worth the complexity.) 
    //
    ARR_SERIES(f->source.array)->header.bits |= REBSER_FLAG_RUNNING;
    f->flags.bits |= DO_FLAG_TOOK_FRAME_LOCK;

    if (truncated)
        SET_FRAME_VALUE(f, ARR_AT(f->source.array, 1)); // skip `--optimized--`
    else
        SET_FRAME_VALUE(f, ARR_HEAD(f->source.array));

    // We clear the DO_FLAG_VA_LIST, assuming that the truncation marker is
    // enough information to record the fact that it was a va_list (revisit
    // if there's another reason to know what it was...)

    f->flags.bits &= ~cast(REBUPT, DO_FLAG_VA_LIST);

    assert(f->pending == VA_LIST_PENDING);
    f->pending = NULL;
}


// (va_list by pointer: http://stackoverflow.com/a/3369762/211160)
//
// Central routine for doing an evaluation of an array of values by calling
// a C function with those parameters (e.g. supplied as arguments, separated
// by commas).  Uses same method to do so as functions like printf() do.
//
// In R3-Alpha this style of invocation was specifically used to call single
// Rebol functions.  It would use a list of REBVAL*s--each of which could
// come from disjoint memory locations and be passed directly with no
// evaluation.  Ren-C replaced this entirely by adapting the evaluator to
// use va_arg() lists for the same behavior as a DO of an ARRAY.
//
// The previously accomplished style of execution with a function which may
// not be in the arglist can be accomplished using `opt_first` to put that
// function into the optional first position.  To instruct the evaluator not
// to do any evaluation on the values supplied as arguments after that
// (corresponding to R3-Alpha's APPLY/ONLY) then DO_FLAG_NO_ARGS_EVALUATE
// should be used--otherwise they will be evaluated normally.
//
// NOTE: Ren-C no longer supports the built-in ability to supply refinements
// positionally, due to the brittleness of this approach (for both system
// and user code).  The `opt_head` value should be made a path with the
// function at the head and the refinements specified there.  Future
// additions could do this more efficiently by allowing the refinement words
// to be pushed directly to the data stack.
//
// !!! C's va_lists are very dangerous, there is no type checking!  The
// C++ build should be able to check this for the callers of this function
// *and* check that you ended properly.  It means this function will need
// two different signatures (and so will each caller of this routine).
//
// Returns THROWN_FLAG, END_FLAG, or VA_LIST_FLAG
//
inline static REBIXO Do_Va_Core(
    REBVAL *out,
    const REBVAL *opt_first,
    va_list *vaptr,
    REBFLGS flags
) {
    REBFRM f;

    if (opt_first)
        SET_FRAME_VALUE(&f, opt_first); // doesn't need specifier, not relative
    else {
        SET_FRAME_VALUE(&f, va_arg(*vaptr, const REBVAL*));
        assert(!IS_RELATIVE(f.value));
    }

    if (IS_END(f.value)) {
        SET_VOID(out);
        return END_FLAG;
    }

    SET_END(out);
    f.out = out;

#if !defined(NDEBUG)
    f.index = TRASHED_INDEX;
#endif
    f.source.vaptr = vaptr;
    f.gotten = NULL; // so ET_WORD and ET_GET_WORD do their own Get_Var
    f.specifier = SPECIFIED; // va_list values MUST be full REBVAL* already
    f.pending = VA_LIST_PENDING;

    Init_Header_Aliased(&f.flags, flags | DO_FLAG_VA_LIST); // see notes

    f.eval_type = VAL_TYPE(f.value);

    Do_Core(&f);

    if (THROWN(f.out))
        return THROWN_FLAG; // !!! prohibits recovery from exits

    return IS_END(f.value) ? END_FLAG : VA_LIST_FLAG;
}


// Wrapper around Do_Va_Core which has the actual variadic interface (as
// opposed to taking the `va_list` whicih has been captured out of the
// variadic interface).
//
inline static REBOOL Do_Va_Throws(REBVAL *out, ...)
{
    va_list va;
    va_start(va, out); // must mention last param before the "..."

#ifdef VA_END_IS_MANDATORY
    struct Reb_State state;
    REBCTX *error;

    PUSH_TRAP(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

    if (error) {
        va_end(va); // interject cleanup of whatever va_start() set up...
        fail (error); // ...then just retrigger error
    }
#endif

    REBIXO indexor = Do_Va_Core(
        out,
        NULL, // opt_first
        &va,
        DO_FLAG_TO_END
    );

    va_end(va);
    //
    // ^-- This va_end() will *not* be called if a fail() happens to longjmp
    // during the apply.  But is that a problem, you ask?  No survey has
    // turned up an existing C compiler where va_end() isn't a NOOP.
    //
    // But there's implementations we know of, then there's the Standard...
    //
    //    http://stackoverflow.com/a/32259710/211160
    //
    // The Standard is explicit: an implementation *could* require calling
    // va_end() if it wished--it's undefined behavior if you skip it.
    //
    // In the interests of efficiency and not needing to set up trapping on
    // each apply, our default is to assume the implementation does not
    // need the va_end() call.  But for thoroughness, VA_END_IS_MANDATORY is
    // outlined here to show the proper bracketing if it were ever needed.

#ifdef VA_END_IS_MANDATORY
    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);
#endif

    assert(indexor == THROWN_FLAG || indexor == END_FLAG);
    return LOGICAL(indexor == THROWN_FLAG);
}


// Takes a list of arguments terminated by END_CELL (or any IS_END) and
// will do something similar to R3-Alpha's "apply/only" with a value.  If
// that value is a function, it will be called...if it is a SET-WORD! it
// will be assigned, etc.
//
// This is equivalent to putting the value at the head of the input and
// then calling EVAL/ONLY on it.  If all the inputs are not consumed, an
// error will be thrown.
//
// The boolean result will be TRUE if an argument eval or the call created
// a THROWN() value, with the thrown value in `out`.
//
inline static REBOOL Apply_Only_Throws(
    REBVAL *out,
    REBOOL fully,
    const REBVAL *applicand,
    ...
) {
    va_list va;
    va_start(va, applicand); // must mention last param before the "..."

#ifdef VA_END_IS_MANDATORY
    struct Reb_State state;
    REBCTX *error;

    PUSH_TRAP(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

    if (error) {
        va_end(va); // interject cleanup of whatever va_start() set up...
        fail (error); // ...then just retrigger error
    }
#endif

    REBIXO indexor = Do_Va_Core(
        out,
        applicand, // opt_first
        &va,
        DO_FLAG_NO_ARGS_EVALUATE
    );

    if (fully && indexor == VA_LIST_FLAG) {
        //
        // Not consuming all the arguments given suggests a problem if `fully`
        // is passed in as TRUE.
        //
        fail (Error(RE_APPLY_TOO_MANY));
    }

    va_end(va); // see notes in Do_Va_Core RE: VA_END_IS_MANDATORY

#ifdef VA_END_IS_MANDATORY
    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);
#endif

    assert(
        indexor == THROWN_FLAG
        || indexor == END_FLAG
        || (NOT(fully) && indexor == VA_LIST_FLAG)
    );
    return LOGICAL(indexor == THROWN_FLAG);
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
            DO_FLAG_TO_END
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
            DO_FLAG_TO_END
        )
    );
}

#define EVAL_VALUE_THROWS(out,value) \
    EVAL_VALUE_CORE_THROWS((out), (value), SPECIFIED)


inline static REBOOL Run_Success_Branch_Throws(
    REBVAL *out,
    const REBVAL *branch,
    REBOOL only
) {
    assert(branch != out); // !!! review, CASE can perhaps do better...

    if (only) {
        *out = *branch;
    }
    else if (IS_BLOCK(branch)) {
        if (DO_VAL_ARRAY_AT_THROWS(out, branch))
            return TRUE;
    }
    else if (IS_FUNCTION(branch)) {
        //
        // The function is allowed to be arity-0, or arity-1 and called with
        // a LOGIC! (which it will ignore if arity 0)
        //
        if (Apply_Only_Throws(out, FALSE, branch, TRUE_VALUE, END_CELL))
            return TRUE;
    }
    else
        *out = *branch; // it's not code -- nothing to run

    return FALSE;
}


// Shared logic between EITHER and BRANCHER (BRANCHER is enfixed as ELSE)
//
inline static REB_R Either_Core(
    REBVAL *out,
    REBVAL *condition,
    REBVAL *true_branch,
    REBVAL *false_branch,
    REBOOL only
) {
    if (IS_CONDITIONAL_TRUE_SAFE(condition)) { // SAFE means no literal blocks
        if (Run_Success_Branch_Throws(out, true_branch, only))
            return R_OUT_IS_THROWN;
    }
    else {
        if (Run_Success_Branch_Throws(out, false_branch, only))
            return R_OUT_IS_THROWN;
    }

    return R_OUT;
}


// A "failing" branch is the untaken branch of an IF, UNLESS, a missed CASE
// or switch, etc.  This is distinguished from a branch which is simply false,
// such as the false branch of an EITHER.  As far as an EITHER is concerned,
// both of its branches are "success".
//
inline static REBOOL Maybe_Run_Failed_Branch_Throws(
    REBVAL *out,
    const REBVAL *branch,
    REBOOL only
) {
    if (
        NOT(only)
        && IS_FUNCTION(branch)
        && GET_VAL_FLAG(branch, FUNC_FLAG_MAYBE_BRANCHER)
    ){
        if (Apply_Only_Throws(
            out,
            TRUE, // error even if it doesn't consume the logic! FALSE
            branch,
            FALSE_VALUE,
            END_CELL
        )) {
            return TRUE;
        }
    }

    return FALSE;
}
