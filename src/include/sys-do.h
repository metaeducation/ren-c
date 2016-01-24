//
// Rebol 3 Language Interpreter and Run-time Environment
// "Ren-C" branch @ https://github.com/metaeducation/ren-c
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
//  Summary: Evaluator "Do State" and Helpers
//  File: %sys-do.h
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


//
// DO_FLAGS
//
// Used by low level routines, these flags specify behaviors which are
// exposed at a higher level through EVAL, EVAL/ONLY, and EVAL/NOFIX
//
// The flags are specified either way for clarity.
//
enum {
    DO_FLAG_0 = 0, // unused

    // As exposed by the DO native and its /NEXT refinement, a call to the
    // evaluator can either run to the finish from a position in an array
    // or just do one eval.  Rather than achieve execution to the end by
    // iterative function calls to the /NEXT variant (as in R3-Alpha), Ren-C
    // offers a controlling flag to do it from within the core evaluator
    // as a loop.
    //
    // However: since running to the end follows a different code path than
    // performing DO/NEXT several times, it is important to ensure they
    // achieve equivalent results.  There are nuances to preserve this
    // invariant and especially in light of potential interaction with
    // DO_FLAG_LOOKAHEAD.
    //
    // NOTE: DO_FLAG_NEXT is *non-continuable* with varargs.  This is due to
    // contention with DO_FLAG_LOOKAHEAD which would not be able to "un-fetch"
    // in the case of a lookahead for infix that failed (and NO_LOOKAHEAD is
    // very rare with API clients).  But also, the va_list could need a
    // conversion to an array during evaluation...and any continuation would
    // need to be sensitive to this change, which is extra trouble for very
    // little likely benefit.
    //
    DO_FLAG_NEXT = 1 << 1,
    DO_FLAG_TO_END = 1 << 2,

    // When we're in mid-dispatch of an infix function, the precedence is such
    // that we don't want to do further infix lookahead while getting the
    // arguments.  (e.g. with `1 + 2 * 3` we don't want infix `+` to
    // "look ahead" past the 2 to see the infix `*`)
    //
    // Actions taken during lookahead may have no side effects.  If it's used
    // to evaluate a form of source input that cannot be backtracked (e.g.
    // a C variable argument list) then it will not be possible to resume.
    //
    DO_FLAG_LOOKAHEAD = 1 << 3,
    DO_FLAG_NO_LOOKAHEAD = 1 << 4,

    // Write more comments here when feeling in more of a commenting mood
    //
    DO_FLAG_EVAL_NORMAL = 1 << 5,
    DO_FLAG_EVAL_ONLY = 1 << 6,

    // Not all function invocations require there to be a persistent frame
    // that identifies it.  One will be needed if there are going to be
    // words bound into the frame (in a way that cannot be finessed through
    // relative binding)
    //
    // Note: This flag is not paired, but if it were the alternative would
    // be DO_FLAG_FRAME_CHUNK...which is the default assumption.
    //
    DO_FLAG_FRAME_CONTEXT = 1 << 7,

    // A pre-built frame can be executed "in-place" without a new allocation.
    // It will be type checked, and also any BAR! parameters will indicate
    // a desire to acquire that argument (permitting partial specialization).
    //
    DO_FLAG_EXECUTE_FRAME = 1 << 8
};


//=////////////////////////////////////////////////////////////////////////=//
//
//  DO INDEX OR FLAG (a.k.a. "INDEXOR")
//
//=////////////////////////////////////////////////////////////////////////=//
//
// * END_FLAG if end of series prohibited a full evaluation
//
// * THROWN_FLAG if the output is THROWN()--you MUST check!
//
// * ...or the next index position where one might continue evaluation
//
// ===========================((( IMPORTANT )))==============================
//
//      The THROWN_FLAG means your value does not represent a directly
//      usable value, so you MUST check for it.  It signifies getting
//      back a THROWN()--see notes in sys-value.h about what that means.
//      If you don't know how to handle it, then at least do:
//
//              fail (Error_No_Catch_For_Throw(out));
//
//      If you *do* handle it, be aware it's a throw label with
//      VALUE_FLAG_THROWN set in its header, and shouldn't leak to the
//      rest of the system.
//
// ===========================================================================
//
// Note that THROWN() is not an indicator of an error, rather something that
// ordinary language constructs might meaningfully want to process as they
// bubble up the stack.  Some examples would be BREAK, RETURN, and QUIT.
//
// Errors are handled with a different mechanism using longjmp().  So if an
// actual error happened during the DO then there wouldn't even *BE* a return
// value...because the function call would never return!  See PUSH_TRAP()
// and fail() for more information.
//

#define END_FLAG 0x80000000  // end of block as index
#define THROWN_FLAG (END_FLAG - 0x75) // throw as an index

// The VARARGS_FLAG is the index used when a C varargs list is the input.
// Because access to a `va_list` is strictly increasing through va_arg(),
// there is no way to track an index; fetches are indexed automatically
// and sequentially without possibility for mutation of the list.  Should
// this index be used it will always be the index of a DO_NEXT until either
// an END_FLAG or a THROWN_FLAG is reached.
//
#define VARARGS_FLAG (END_FLAG - 0xBD)

// This is not an actual DO state flag that you would see in a Reb_Call's
// index, but it is a value that is returned in case a non-continuable
// DO_NEXT call is made on varargs.  One can make the observation that it
// is incomplete only--not resume.
//
#define VARARGS_INCOMPLETE_FLAG (END_FLAG - 0xAE)

// The C build simply defines a REBIXO as a synonym for an unsigned int.  But
// in the C++ build, the indexor is a more restrictive class...which redefines
// a subset of operations for integers but does *not* implicitly cast to one
// Hence if a THROWN_FLAG, END_FLAG, VARARGS_FLAG etc. is used with integer
// math or put into an `int` variable accidentally, this will be caught.
//
// Because indexors are not stored in REBVALs or places where memory usage
// outweighs the concern of the native performance, they use `unsigned int`
// instead of REBCNT.
//
#if defined(NDEBUG) || !defined(__cplusplus) || (__cplusplus < 201103L)
    typedef unsigned int REBIXO;
#else
    #include "sys-do-cpp.h"

    typedef Reb_Indexor REBIXO;
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  REBOL DO STATE (a.k.a. Reb_Call)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A Reb_Call structure represents the fixed-size portion for a function's
// call frame.  It is stack allocated, and is used by both Do and Apply.
// (If a dynamic allocation is necessary for the call frame, that dynamic
// portion is allocated as an array in `arglist`.)
//
// The contents of the call frame are all the input and output parameters
// for a call to the evaluator--as well as all of the internal state needed
// by the evaluator loop.  The reason that all the information is exposed
// in this way is to make it faster and easier to delegate branches in
// the Do loop--without bearing the overhead of setting up new stack state.
//
// A stack-allocated Reb_Call cannot contain anything that can't be freed by
// the PUSH_TRAP handling implicitly--so no malloc'd members, no cleanup
// needing imperative code, etc.  Any allocations must be of things that the
// GC or otherwise will take care of, because a Reb_Call will just vanish
// if there is an error while it's running that doesn't get trapped inside
// of it somewhere.
//

enum Reb_Call_Mode {
    CALL_MODE_GUARD_ARRAY_ONLY, // no special mode signal
    CALL_MODE_ARGS, // ordinary arguments before any refinements seen
    CALL_MODE_REFINE_PENDING, // picking up refinement arguments, none yet
    CALL_MODE_REFINE_ARGS, // at least one refinement has been found
    CALL_MODE_SEEK_REFINE_WORD, // looking for refinements (used out of order)
    CALL_MODE_REFINE_SKIP, // in the process of skipping an unused refinement
    CALL_MODE_REFINE_REVOKE, // found an unset and aiming to revoke refinement use
    CALL_MODE_FUNCTION, // running an ANY-FUNCTION!
    CALL_MODE_THROW_PENDING // function threw (sometimes may be intercepted)
};

union Reb_Call_Source {
    REBARR *array;
    va_list *varargs_ptr;
};

// NOTE: The ordering of the fields in `Reb_Call` are specifically done so
// as to accomplish correct 64-bit alignment of pointers on 64-bit systems
// (as long as REBCNT and REBINT are 32-bit on such platforms).  If modifying
// the structure, be sensitive to this issue.
//
// Because performance in the core evaluator loop is system-critical, this
// uses full platform `int`s instead of REBCNTs.
//
struct Reb_Call {
    //
    // `func` [INTERNAL, READ-ONLY, GC-PROTECTED]
    //
    // If a function call is currently in effect, `func` holds a pointer to
    // the function being run.  Because functions are identified and passed
    // by a platform pointer as their paramlist REBSER*, you must use
    // `FUNC_VALUE(c->func)` to get a pointer to a canon REBVAL representing
    // that function (to examine its function flags, for instance).
    //
    REBFUN *func;

    // `dsp_orig` [INTERNAL, READ-ONLY]
    //
    // The data stack pointer captured on entry to the evaluation.  It is used
    // by debug checks to make sure the data stack stays balanced after each
    // sub-operation.  Yet also, refinements are pushed to the data stack and
    // something to compare against to find out how many is needed.  At this
    // position to sync alignment with same-sized `flags`.
    //
    unsigned int dsp_orig; // type is REBDSP, but enforce alignment here

    // `flags` [INPUT, READ-ONLY (unless FRAMELESS signaling error)]
    //
    // These are DO_FLAG_xxx or'd together.  If the call is being set up
    // for an Apply as opposed to Do, this must be 0.
    //
    unsigned int flags; // type is REBFLGS, but enforce alignment here

    // `out` [INPUT pointer of where to write an OUTPUT, GC-SAFE cell]
    //
    // This is where to write the result of the evaluation.  It should not be
    // in "movable" memory, hence not in a series data array.  Often it is
    // used as an intermediate free location to do calculations en route to
    // a final result, due to being GC-safe
    //
    REBVAL *out;

    // `value` [INPUT, REUSABLE, GC-PROTECTS pointed-to REBVAL]
    //
    // This is the value currently being processed.  Callers pass in the
    // first value pointer...which for any successive evaluations will be
    // updated via picking from `array` based on `index`.  But having the
    // caller pass in the initial value gives the *option* of that value
    // not being resident in the series.
    //
    // (Hence if one has the series `[[a b c] [d e]]` it would be possible to
    // have an independent path value `append/only` and NOT insert it in the
    // series, yet get the effect of `append/only [a b c] [d e]`.  This only
    // works for one value, but is a convenient no-cost trick for apply-like
    // situations...as insertions usually have to "slide down" the values in
    // the series and may also need to perform alloc/free/copy to expand.)
    //
    // !!! The ramifications of using a disconnected value on debugging
    // which is *not* part of the series is that the "where" will come up
    // with missing information.  This is not the only case where the
    // optimization of not making a series or not making a frame creates
    // a problem--and in general, optimization will interfere with almost any
    // debug feedback.  The proposed solution is to have a "debug mode" which
    // causes more conservatism--for instance, if it is noticed in an
    // evaluation that the value pointer does *not* line up at the head of
    // the series for the evaluation given, it would be cached somewhere...
    // then if any problem needing a where came up, a series would be made
    // to put it in.  (The where series is paying for a copy anyway.)
    //
    const REBVAL *value;

    // `eval_fetched` [INTERNAL, READ-ONLY, GC-PROTECTS pointed-to REBVAL]
    //
    // Mechanically speaking, running an EVAL has to overwrite `value` from
    // the natural pre-fetching course, so that the evaluated value can be
    // simulated as living in the line of execution.  Because fetching moves
    // forward only, we'd lose the next value if we didn't save it somewhere.
    //
    // This pointer saves the prefetched value that eval overwrites, and
    // by virtue of not being NULL signals to just use the value on the
    // next fetch instead of fetching again.
    //
    const REBVAL *eval_fetched;

    // source.array, source.varargs_ptr [INPUT, READ-ONLY, GC-PROTECTED]
    //
    // This is the source from which new values will be fetched.  The most
    // common dispatch of the evaluator is on values that live inside of a
    // Rebol BLOCK! or GROUP!...but something to know is that the `array`
    // could have come from any ANY-ARRAY! (e.g. a PATH!).  The fact that it
    // came from a value marked REB_PATH is not known here: value-bearing
    // series will all "evaluate like a block".
    //
    // In addition to working with a source of values in a traditional series,
    // in Ren-C it is also possible to feed the evaluator arbitrary REBVAL*s
    // through a variable argument list.  It is not necessary to dynamically
    // allocate an array to use as input for an impromptu evaluation: the
    // stack parameters of a function call are enumerated.  However...
    //
    // === The `varargs` list is *NOT* random access like an array is!!! ===
    //
    // http://en.cppreference.com/w/c/variadic
    //
    // See notes on `index` about how this is managed via VARARGS_FLAG.
    //
    // !!! It is extremely desirable to implicitly GC protect the C function
    // arguments but a bit difficult to do so; one good implementation idea
    // would be to merge it with the idea of a third category of using
    // an in-memory array block of values; where at the point of a GC
    // actually happening then *that* would be the opportunity where the
    // cost were paid to advance through the remaining arguments, copying
    // them into some safe location and then picking up the enumeration
    // through memory.
    //
    union Reb_Call_Source source;

    // `index` [INPUT, OUTPUT]
    //
    // Index into the array from which new values are fetched after the
    // initial `value`.  Successive fetching is always done by index and
    // not by incrementing `value` for several reasons, though one is to
    // avoid crashing if the input array is modified during the evaluation.
    //
    // !!! While it doesn't *crash*, a good user-facing explanation of
    // handling what it does instead seems not to have been articulated!  :-/
    //
    // At the end of the evaluation it's the index of the next expression
    // to be evaluated, THROWN_FLAG, or END_FLAG.
    //
    // What might happen if they are on a branch during the conversion where
    // they assumed it was vararg and it changes?
    //
    REBIXO indexor;

    // `label_sym` [INTERNAL, READ-ONLY]
    //
    // Functions don't have "names", though they can be assigned
    // to words.  If a function is invoked via word lookup (vs. a literal
    // FUNCTION! value), 'label_sym' will be that WORD!, and a placeholder
    // otherwise.  Placed here for 64-bit alignment after same-size `index`.
    //
    REBCNT label_sym;

    // `frame` [INTERNAL, VALUES MUTABLE and GC-SAFE if not "frameless"]
    //
    // The dynamic portion of the call frame has args with which a function is
    // being invoked (if it is not frameless).  The data is resident in the
    // "chunk stack".
    //
    // If a client of this array is a NATIVE!, then it will access the data
    // directly by offset index (e.g. `PARAM(3,...)`).  But if it is a
    // FUNCTION! implemented by the user, it has words for arguments and
    // locals to access by, and hence a FRAME!.  The frame is like an OBJECT!
    // but since its data also lives in the chunk stack, words bound into it
    // won't be able to fetch the data after the call has completed.
    //
    // !!! For debugging purposes, it will be necessary to request natives
    // to not "run framelessly" (though that means not-even-a-chunk frame).
    // This might also request not to run as "just a chunk" so that the
    // content of the native frame would be inspectable by words bound into
    // the frame by the debugger.
    //
    union {
        REBCTX *context;
        REBVAL *stackvars;
    } frame;

    // `param` [INTERNAL, REUSABLE, GC-PROTECTS pointed-to REBVALs]
    //
    // We use the convention that "param" refers to the TYPESET! (plus symbol)
    // from the spec of the function--a.k.a. the "formal argument".  This
    // pointer is moved in step with `arg` during argument fulfillment.
    //
    // (Note: It is const because we don't want to be changing the params,
    // but also because it is used as a temporary to store value if it is
    // advanced but we'd like to hold the old one...this makes it important
    // to protect it from GC if we have advanced beyond as well!)
    //
    const REBVAL *param;

    // `arg` [INTERNAL, also CACHE of `ARR_HEAD(arglist)`]
    //
    // "arg" is the "actual argument"...which holds the pointer to the
    // REBVAL slot in the `arglist` for that corresponding `param`.  These
    // are moved in sync during parameter fulfillment.
    //
    // While a function is running, `arg` is a cache to the data pointer for
    // arglist.  It is used by the macros ARG() and PARAM()...which index
    // by integer constants and may be used several times.  Avoiding the
    // extra indirection can be beneficial.
    //
    REBVAL *arg;

    // `refine` [INTERNAL, REUSABLE, GC-PROTECTS pointed-to REBVAL]
    //
    // The `arg` slot of the refinement currently being processed.  We have to
    // remember its address in case we later see all its arguments are UNSET!,
    // and want to go back and "revoke" it by setting the arg to NONE!.
    //
    REBVAL *refine;

    // `prior` [INTERNAL, READ-ONLY]
    //
    // The prior call frame (may be NULL if this is the topmost stack call).
    //
    struct Reb_Call *prior;

    // `mode` [INTERNAL, READ-ONLY]
    //
    // State variable during parameter fulfillment.  So before refinements,
    // in refinement, skipping...etc.
    //
    // One particularly important usage is CALL_MODE_FUNCTION, which needs to
    // be checked by `Get_Var`.  This is a necessarily evil while FUNCTION!
    // does not have the semantics of CLOSURE!, because pathological cases
    // in "stack-relative" addressing can get their hands on "reused" bound
    // words during the formation process, e.g.:
    //
    //      leaker: func [/exec e /gimme g] [
    //          either gimme [return [g]] [reduce e]
    //      ]
    //
    //      leaker/exec reduce leaker/gimme 10
    //
    // Since a leaked word from another instance of a function can give
    // access to a call frame during its formation, we need a way to tell
    // when a call frame is finished forming...CALL_MODE_FUNCTION is it.
    //
    enum Reb_Call_Mode mode;

    // `expr_index` [INTERNAL, READ-ONLY]
    //
    // Although the evaluator has to know what the current `index` is, the
    // error reporting machinery typically wants to know where the index
    // was *before* the last evaluation started...in order to present an
    // idea of the expression that caused the error.  This is the index
    // of where the currently evaluating expression started.
    //
    REBIXO expr_index;

#if !defined(NDEBUG)
    //
    // `do_count` [INTERNAL, DEBUG, READ-ONLY]
    //
    // The `do_count` represents the expression evaluation "tick" where the
    // Reb_Call is starting its processing.  This is helpful for setting
    // breakpoints on certain ticks in reproducible situations.
    //
    REBCNT do_count;

    // `label_str` [INTERNAL, DEBUG, READ-ONLY]
    //
    // Knowing the label symbol is not as handy as knowing the actual string
    // of the function this call represents (if any).  It is in UTF8 format,
    // and cast to `char*` to help debuggers that have trouble with REBYTE.
    //
    const char *label_str;
#endif
};

// Each iteration of DO bumps a global count, that in deterministic repro
// cases can be very helpful in identifying the "tick" where certain problems
// are occurring.  The SPORADICALLY() macro uses this to allow flipping
// between different behaviors in debug builds--usually to run the release
// behavior some of the time, and the debug behavior some of the time.  This
// exercises the release code path even when doing a debug build.
//
#ifdef NDEBUG
    #define SPORADICALLY(modulus) FALSE
#else
    #define SPORADICALLY(modulus) (TG_Do_Count % modulus == 0)
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  DO's "FRAMELESS" API => *LOWEST* LEVEL EVALUATOR HOOKING
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This API is used internally in the implementation of Do_Core.  It does
// not speak in terms of arrays or indices, it works entirely by setting
// up a call frame (c), and threading that frame's state through successive
// operations, vs. setting it up and disposing it on each DO/NEXT step.
//
// Like higher level APIs that move through the input series, the frameless
// API can move at full DO/NEXT intervals.  Unlike the higher APIs, the
// possibility exists to move by single elements at a time--regardless of
// if the default evaluation rules would consume larger expressions.  Also
// making it different is the ability to resume after a DO/NEXT on value
// sources that aren't random access (such as C's va_arg list).
//
// One invariant of access is that the input may only advance.  Before any
// operations are called, any low-level client must have already seeded
// c->value with a valid "fetched" REBVAL*.  END is not valid, so callers
// beginning a Do_To_End must pre-check that condition themselves before
// calling Do_Core.  And if an operation sets the c->index to END_FLAG then
// that must be checked--it's not legal to call more operations on a call
// frame after a fetch reports the end.
//
// Operations are:
//
//  FETCH_NEXT_ONLY_MAYBE_END
//
//      Retrieve next pointer for examination to c->value.  The previous
//      c->value pointer is overwritten.  (No REBVAL bits are moved by
//      this operation, only the 'currently processing' pointer reassigned.)
//      c->index may be set to END_FLAG if the end of input is reached.
//
// DO_NEXT_REFETCH_MAY_THROW
//
//      Executes the already-fetched pointer, consuming as much of the input
//      as necessary to complete a /NEXT (or failing with an error).  This
//      writes the computed REBVAL into a destination location.  After the
//      operation, the next c->value pointer will already be fetched and
//      waiting for examination or use.  c->index may be set to either
//      THROWN_FLAG or END_FLAG by this operation.
//
// DO_NEXT_REFETCH_QUOTED
//
//      This operation is fairly trivial in the sense that it just assigns
//      the REBVAL bits pointed to by the current value to the destination
//      cell.  Then it does a simple fetch.  The main reason for making an
//      operation vs just having callers do the two steps is to monitor
//      when some of the input has been "consumed" vs. merely fetched.
//
//      !!! This is unenforceable in the C build, but in the C++ build it
//      could be ensured c->value was only dereferenced via * and stored
//      to targets through this routine.
//
// This is not intending to be a "published" API of Rebol/Ren-C.  But the
// privileged level of access can be used by natives that feel they can
// optimize performance by working with the evaluator directly.  Code written
// in a frameless native can be essentially equally as fast as writing code
// inside of the main `switch()` statement of Do_Core(), as they hook out
// quickly once a function is identified.
//
// (So for instance, the implementation of `QUOTE` does not require setting
// up memory on the stack, enumerating parameters and checking parameter
// counts, checking to see the parameter is quoted and doing a quote refetch
// into that slot...then dispatching the function, checking that value,
// moving it to the output, freeing the stack memory.  `QUOTE` can just
// make sure it's not the end of the input, then call DO_NEXT_REFETCH_QUOTED
// itself and be done.)
//
// !!! If a native works at the frameless API level, that foregoes automatic
// parameter checking, and the absence of a call frame for debugging or
// tracing insight.  Hence each frameless native must still be able to run
// in a "framed" mode if necessary.  Error reporting has some help to say
// whether the error came from what would correspond to parameter fulfillment
// vs. execution were it an ordinary native.
//
// !!! For better or worse, Do_Core does not lock the series it is iterating.
// This means any arbitrary user code (or system code) could theoretically
// disrupt a series out from under it, and then crash the system on the
// next fetch.  Hence an array and an index are used, and in terms of "crash
// avoidance" (albeit not necessarily "semantic sensibility) it's necessary
// to clip the index to be within the range of the series.  In theory it
// would be possible to track the cases where clipping the index was needed
// or not, though it might be better to just lock the series being evaluated
// currently...this is an open question.
//

//
// FETCH_NEXT_ONLY_MAYBE_END (see notes above)
//

#define FETCH_NEXT_ONLY_MAYBE_END_RAW(c) \
    do { \
        if ((c)->eval_fetched) { \
            if (IS_END((c)->eval_fetched)) \
                (c)->value = NULL; /* could be debug only */ \
            else \
                (c)->value = (c)->eval_fetched; \
            (c)->eval_fetched = NULL; \
            break; \
        } \
        if ((c)->indexor != VARARGS_FLAG) { \
            if ((c)->indexor < ARR_LEN((c)->source.array)) { \
                assert((c)->value != \
                    ARR_AT((c)->source.array, (c)->indexor)); \
                (c)->value = ARR_AT((c)->source.array, (c)->indexor); \
                (c)->indexor = (c)->indexor + 1; \
            } \
            else { \
                (c)->value = NULL; \
                (c)->indexor = END_FLAG; \
            } \
        } \
        else { \
            (c)->value = va_arg(*(c)->source.varargs_ptr, const REBVAL*); \
            if (IS_END((c)->value)) { \
                (c)->value = NULL; \
                (c)->indexor = END_FLAG; \
            } \
        } \
    } while (0)

#ifdef NDEBUG
    #define FETCH_NEXT_ONLY_MAYBE_END(c) \
        FETCH_NEXT_ONLY_MAYBE_END_RAW(c)
#else
    #define FETCH_NEXT_ONLY_MAYBE_END(c) \
        do { \
            Trace_Fetch_Debug("FETCH_NEXT_ONLY_MAYBE_END", (c), FALSE); \
            FETCH_NEXT_ONLY_MAYBE_END_RAW(c); \
            Trace_Fetch_Debug("FETCH_NEXT_ONLY_MAYBE_END", (c), TRUE); \
        } while (0)
#endif

// This macro is the workhorse behind DO_NEXT_REFETCH_MAY_THROW.  It is also
// reused by the higher level DO_NEXT_MAY_THROW operation, because it does
// a useful trick.  It is able to do a quick test to see if a value has no
// evaluator behavior, and if so avoid a recursive call to Do_Core().
//
// However, "inert" values can have evaluator behavior--so this requires a
// lookahead check.  Using varargs has already taken one step further than
// it can by using a "prefetch", and it cannot lookahead again without
// saving the value in another location.  Hence the trick is not used with
// vararg input, and INTEGER!/BLOCK!/etc. go through Do_Core() in that case.
//
// IMPORTANT:
//
//  * `index_out` and `index_in` can be the same variable (and usually are)
//  * `value_out` and `value_in` can be the same variable (and usually are)
//
#define DO_CORE_REFETCH_MAY_THROW( \
    value_out,indexor_out,out_,source_,indexor_in,value_in,eval_fetched,flags_ \
) \
    do { \
        struct Reb_Call c_; \
        if (!eval_fetched && indexor_in != VARARGS_FLAG) { \
            if (SPORADICALLY(2)) { /* every OTHER execution fast if DEBUG */ \
                if ( \
                    !ANY_EVAL(value_in) \
                    && (IS_END((value_in) + 1) || !ANY_EVAL((value_in) + 1)) \
                ) { \
                    assert(!IS_TRASH_DEBUG(value_in)); \
                    *(out_) = *(value_in); \
                    assert(!IS_TRASH_DEBUG(out_)); \
                    (value_out) = ARR_AT((source_).array, (indexor_in)); \
                    if (IS_END(value_out)) { \
                        (indexor_out) = END_FLAG; \
                        (value_out) = NULL; /* this could be debug only */ \
                    } else \
                        (indexor_out) = (indexor_in) + 1; \
                    break; \
                } \
            } \
        } \
        c_.out = (out_); \
        c_.source = (source_); \
        c_.value = (value_in); \
        c_.indexor = (indexor_in); \
        c_.flags = DO_FLAG_EVAL_NORMAL | DO_FLAG_NEXT | (flags_); \
        Do_Core(&c_); \
        assert(c_.indexor == VARARGS_FLAG || (indexor_in) != c_.indexor); \
        (indexor_out) = c_.indexor; \
        (value_out) = c_.value; \
    } while (0)

//
// DO_NEXT_REFETCH_MAY_THROW (see notes above)
//

#ifdef NDEBUG
    #define DO_NEXT_REFETCH_MAY_THROW(dest,c,flags) \
        DO_CORE_REFETCH_MAY_THROW( \
            (c)->value, (c)->indexor, dest, /* outputs */ \
            (c)->source, (c)->indexor, (c)->value, (c)->eval_fetched, \
            flags /* inputs */) \

#else
    #define DO_NEXT_REFETCH_MAY_THROW(dest,c,flags) \
        do { \
            Trace_Fetch_Debug("DO_NEXT_REFETCH_MAY_THROW", (c), FALSE); \
            DO_CORE_REFETCH_MAY_THROW( \
                (c)->value, (c)->indexor, dest, /* outputs */ \
                (c)->source, (c)->indexor, (c)->value, (c)->eval_fetched, \
                flags /* inputs */); \
            Trace_Fetch_Debug("DO_NEXT_REFETCH_MAY_THROW", (c), TRUE); \
        } while (0)
#endif

//
// DO_NEXT_REFETCH_QUOTED (see notes above)
//

#ifdef NDEBUG
    #define DO_NEXT_REFETCH_QUOTED(dest,c) \
        do { \
            *dest = *(c)->value; \
            FETCH_NEXT_ONLY_MAYBE_END(c); \
        } while (0)

#else
    #define DO_NEXT_REFETCH_QUOTED(dest,c) \
        do { \
            Trace_Fetch_Debug("DO_NEXT_REFETCH_QUOTED", (c), FALSE); \
            *dest = *(c)->value; \
            FETCH_NEXT_ONLY_MAYBE_END(c); \
            Trace_Fetch_Debug("DO_NEXT_REFETCH_QUOTED", (c), TRUE); \
        } while (0)
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  BASIC API: DO_NEXT_MAY_THROW and DO_ARRAY_THROWS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This is an optimized wrapper for the basic building block of Rebol
// evaluation.  They are macros designed to minimize overhead and be
// guaranteed to be inlined at the callsite.  For the central functionality,
// see `Do_Core()`.
//
// The optimized version of the macro will never do an evaluation of a type
// it doesn't have to.  It uses ANY_EVAL() to see if it can get out of making
// a function call...sometimes it cannot because there may be an infix lookup
// possible (we don't know that `[3] + [4]` isn't ever going to work...)  For
// this reason the optimization cannot work with a varargs list, as the
// va_list in C cannot be "peeked ahead at" and then put back (while the
// Rebol array data is random access).
//
// The debug build exercises both code paths, by optimizing every other
// execution to bypass the evaluator if possible...and then throwing
// the code through Do_Core the other times.  It's a sampling test, but
// not a bad one for helping keep the methods in sync.
//
// DO_NEXT_MAY_THROW takes in an array and a REBCNT offset into that array
// of where to execute.  Although the return value is a REBCNT, it is *NOT*
// always a series index!!!  It may return:
//
// DO_ARRAY_THROWS is another helper for the frequent case where one has a
// BLOCK! or a GROUP! at an index which already indicates the point where
// execution is to start.
//
// (The "Throws" name is because it's expected to usually be used in an
// 'if' statement.  It cues you into realizing that it returns TRUE if a
// THROW interrupts this current DO_BLOCK execution--not asking about a
// "THROWN" that happened as part of a prior statement.)
//
// If it returns FALSE, then the DO completed successfully to end of input
// without a throw...and the output contains the last value evaluated in the
// block (empty blocks give UNSET!).  If it returns TRUE then it will be the
// THROWN() value.
//

#define DO_NEXT_MAY_THROW(indexor_out,out,array_in,index) \
    do { \
        union Reb_Call_Source source; \
        REBIXO indexor_ = index + 1; \
        REBVAL *value_ = ARR_AT((array_in), (index)); \
        const REBVAL *dummy; /* need for varargs continuation, not array */ \
        if (IS_END(value_)) { \
            SET_UNSET(out); \
            (indexor_out) = END_FLAG; \
            break; \
        } \
        source.array = (array_in); \
        DO_CORE_REFETCH_MAY_THROW( \
            dummy, (indexor_out), (out), \
            (source), indexor_, value_, NULL, \
            DO_FLAG_LOOKAHEAD \
        ); \
        if ((indexor_out) != END_FLAG && (indexor_out) != THROWN_FLAG) { \
            assert((indexor_out) > 1); \
            (indexor_out) = (indexor_out) - 1; \
        } \
        (void)dummy; \
    } while (0)

// Note: It is safe for `out` and `array` to be the same variable.  The
// array and index are extracted, and will be protected from GC by the DO
// state...so it is legal to DO_ARRAY_THROWS(D_OUT, D_OUT) for instance.
//
#define DO_ARRAY_THROWS(out,array) \
    Do_At_Throws((out), VAL_ARRAY(m_cast(REBVAL*, array)), VAL_INDEX(array))

// Lowercase, because doesn't repeat array parameter.  If macro picked head
// off itself, it would need to be uppercase!
//

#define Do_At_Throws(out,array,index) \
    LOGICAL(THROWN_FLAG == Do_Array_At_Core( \
        (out), NULL, (array), (index), \
        DO_FLAG_TO_END | DO_FLAG_EVAL_NORMAL | DO_FLAG_LOOKAHEAD \
    ))

// Because Do_Core can seed with a single value, we seed with our value and
// an EMPTY_ARRAY.  Revisit if there's a "best" dispatcher...
//
#define DO_VALUE_THROWS(out,value) \
    LOGICAL(THROWN_FLAG == Do_Array_At_Core((out), (value), EMPTY_ARRAY, 0, \
        DO_FLAG_TO_END | DO_FLAG_LOOKAHEAD | DO_FLAG_EVAL_NORMAL))


//=////////////////////////////////////////////////////////////////////////=//
//
//  PATH EVALUATION "PVS"
//
//=////////////////////////////////////////////////////////////////////////=//
//
// !!! This structure and the code using it has not been given a very
// thorough review under Ren-C yet.  It pertains to the dispatch of paths,
// where types can chain an evaluation from one to the next.
//

typedef struct Reb_Path_Value {
    REBVAL *value;  // modified
    REBVAL *select; // modified
    REBVAL *path;   // modified
    REBVAL *store;  // modified (holds constructed values)
    REBVAL *setval; // static
    const REBVAL *orig; // static
} REBPVS;

enum Path_Eval_Result {
    PE_OK,
    PE_SET,
    PE_USE,
    PE_NONE,
    PE_BAD_SELECT,
    PE_BAD_SET,
    PE_BAD_RANGE,
    PE_BAD_SET_TYPE
};

typedef REBINT (*REBPEF)(REBPVS *pvs); // Path evaluator function

typedef REBINT (*REBCTF)(const REBVAL *a, const REBVAL *b, REBINT s);


//=////////////////////////////////////////////////////////////////////////=//
//
//  ARGUMENT AND PARAMETER ACCESS HELPERS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// These accessors are designed to make it convenient for natives and actions
// written in C to access their arguments and refinements.  They are able
// to bind to the implicit Reb_Call* passed to every REBNATIVE() and read
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
    // Capture the argument for debug inspection.  Be sensitive to frameless
    // usage so that parameters may be declared and used with PAR() even if
    // they cannot be used with ARG()
    //
    #define PARAM(n,name) \
        const struct Native_Param p_##name = { \
            call_->arg ? VAL_TYPE(call_->arg + (n) - 1) : REB_TRASH, \
            call_->arg ? call_->arg + (n) - 1 : NULL, \
            (n) \
        }

    // As above, do a cache and be tolerant of framelessness.  The seeming odd
    // choice to lie and say a refinement is present in the frameless case is
    // actually to make any frameless native that tries to use REF() get
    // confused and hopefully crash...saying FALSE might make the debug build
    // get cozy with the idea that REF() is legal in a frameless native.
    //
    #define REFINE(n,name) \
        const struct Native_Refine p_##name = { \
            call_->arg ? NOT(IS_NONE(call_->arg + (n) - 1)) : TRUE, \
            call_->arg ? call_->arg + (n) - 1 : NULL, \
            (n) \
        }
#endif

// Though REF can only be used with a REFINE() declaration, ARG can be used
// with either.
//
#define ARG(name) \
    (call_->arg + (p_##name).num - 1)

#define PAR(name) \
    FUNC_PARAM(call_->func, (p_##name).num) // a TYPESET!

#ifdef NDEBUG
    #define REF(name) \
        NOT(IS_NONE(ARG(name)))
#else
    // An added useless ?: helps check in debug build to make sure we do not
    // try to use REF() on something defined as PARAM(), but only REFINE()
    //
    #define REF(name) \
        ((p_##name).used_cache \
            ? NOT(IS_NONE(ARG(name))) \
            : NOT(IS_NONE(ARG(name))))
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  CALL FRAME ACCESS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// !!! To be documented and reviewed.  Legacy naming conventions when the
// arguments to functions lived in the data stack gave the name "DSF" for
// "(D)ata (S)tack (F)rame" which is no longer accurate, as well as the
// convention of prefix with a D_.  The new PARAM()/REFINE()/ARG()/REF()
// scheme is coming up on replacing a lot of it, so these will be needing
// a tune up and choices of better names once that is sorted out.  It
// may be as simple as changing these to C_ or CSP for "call stack pointer"
//

#define DSF (CS_Running + 0) // avoid assignment to DSF via + 0

#define DSF_IS_VARARGS(c) \
    ((c)->indexor == VARARGS_FLAG)

#define DSF_ARRAY(c) \
    (assert(!DSF_IS_VARARGS(c)), (c)->source.array)

#define DSF_INDEX(c) \
    (assert(!DSF_IS_VARARGS(c)), (c)->indexor == END_FLAG \
        ? ARR_LEN((c)->source.array) : (c)->indexor - 1)

#define DSF_OUT(c)          cast(REBVAL * const, (c)->out) // writable Lvalue
#define PRIOR_DSF(c)        ((c)->prior)
#define DSF_LABEL_SYM(c)    ((c)->label_sym + 0) // Lvalue
#define DSF_FUNC(c)         ((c)->func)
#define DSF_DSP_ORIG(c)     ((c)->dsp_orig + 0) // Lvalue

#define DSF_PARAMS_HEAD(c)  FUNC_PARAMS_HEAD((c)->func)

// Though `arg` is in use to point at the arguments during evaluation, the
// other parameter fulfillment pointers `param` and `refine` are free for
// use.  Since the GC is aware of these pointers, it can protect whatever
// they are pointing at.  This can be useful for routines that have a local
// memory cell.  This does not require a push or a pop of anything--it only
// protects as long as the native is running.  (This trick is available to
// the dispatchers as well.)
//
// Using X and Y instead of 1 and 2 to avoid confusing with PARAM(#)s
//
#define DSF_PROTECT_X(c,v) \
    ((c)->param = (v))

#define DSF_PROTECT_Y(c,v) \
    ((c)->refine = (v))

// It's not clear exactly in which situations one might be using this; while
// it seems that when filling function args you could just assume it hasn't
// been reified, there may be "pre-reification" in the future, and also a
// tail call optimization or some other "reuser" of a frame may jump in and
// reuse a frame that's been reified after its initial "chunk only" state.
// For now check the flag and don't just assume it's a raw frame.
//
#define DSF_ARGS_HEAD(c) \
    (((c)->flags & DO_FLAG_FRAME_CONTEXT) \
        ? CTX_VARS_HEAD((c)->frame.context) \
        : &(c)->frame.stackvars[0])

// ARGS is the parameters and refinements
// 1-based indexing into the arglist (0 slot is for object/function value)
#ifdef NDEBUG
    #define DSF_ARG(c,n)    ((c)->arg + (n) - 1)
#else
    #define DSF_ARG(c,n)    DSF_ARG_Debug((c), (n)) // checks arg index bound
#endif

#define DSF_FRAMELESS(c) \
    ((c)->arg ? FALSE : (assert(!DSF_IS_VARARGS(c)), TRUE))

// Note about D_ARGC: A native should generally not detect the arity it
// was invoked with, (and it doesn't make sense as most implementations get
// the full list of arguments and refinements).  However, ACTION! dispatch
// has several different argument counts piping through a switch, and often
// "cheats" by using the arity instead of being conditional on which action
// ID ran.  Consider when reviewing the future of ACTION!.
//
#define DSF_ARGC(c) \
    cast(REBCNT, FUNC_NUM_PARAMS((c)->func))

// Quick access functions from natives (or compatible functions that name a
// Reb_Call pointer `call_`) to get some of the common public fields.
//
#define D_OUT       DSF_OUT(call_)          // GC-safe slot for output value
#define D_ARGC      DSF_ARGC(call_)         // count of args+refinements/args
#define D_ARG(n)    DSF_ARG(call_, (n))     // pass 1 for first arg
#define D_REF(n)    NOT(IS_NONE(D_ARG(n)))  // D_REFinement (not D_REFerence)
#define D_FUNC      DSF_FUNC(call_)         // REBVAL* of running function
#define D_LABEL_SYM DSF_LABEL_SYM(call_)    // symbol or placeholder for call
#define D_DSP_ORIG  DSF_DSP_ORIG(call_)     // Original data stack pointer

#define D_PROTECT_X(v)      DSF_PROTECT_X(call_, (v))
#define D_PROTECT_Y(v)      DSF_PROTECT_Y(call_, (v))

#define D_FRAMELESS DSF_FRAMELESS(call_)    // Native running w/no call frame

// Frameless native access
//
// !!! Should `call_` just be renamed to `c_` to make this briefer and be used
// directly?  It is helpful to have macros to find the usages, however.
//
#define D_CALL      call_
#define D_ARRAY     (call_->source.array)
#define D_INDEXOR   (call_->indexor)
#define D_VALUE     (call_->value)
#define D_MODE      (call_->mode)
