//
//  File: %sys-frame.h
//  Summary: {Evaluator "Do State"}
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
// The primary routine that performs DO and DO/NEXT is called Do_Core().  It
// takes a single parameter which holds the running state of the evaluator.
// This state may be allocated on the C variable stack.
//
// Do_Core() is written such that a longjmp up to a failure handler above it
// can run safely and clean up even though intermediate stacks have vanished.
// This is because Push_Frame and Drop_Frame maintain an independent global
// list of the frames in effect, so that the Fail_Core() routine can unwind
// all the associated storage and structures for each frame.
//
// Ren-C can not only run the evaluator across a REBARR-style series of
// input based on index, it can also enumerate through C's `va_list`,
// providing the ability to pass pointers as REBVAL* in a variadic function
// call from the C (comma-separated arguments, as with printf()).  Future data
// sources might also include a REBVAL[] raw C array.
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
// The default for a DO operation is just a single DO/NEXT, where args
// to functions are evaluated (vs. quoted), and lookahead is enabled.
//

#define DO_FLAG_NORMAL 0

// See Init_Endlike_Header() for why these are chosen the way they are.  This
// means that the Reb_Frame->flags field can function as an implicit END for
// Reb_Frame->cell, as well as be distinguished from a REBVAL*, a REBSER*, or
// a UTF8 string.
//
#define DO_FLAG_0_IS_TRUE FLAGIT_LEFT(0) // NODE_FLAG_NODE
#define DO_FLAG_1_IS_FALSE FLAGIT_LEFT(1) // NOT(NODE_FLAG_FREE)


//=//// DO_FLAG_TO_END ////////////////////////////////////////////////////=//
//
// As exposed by the DO native and its /NEXT refinement, a call to the
// evaluator can either run to the finish from a position in an array or just
// do one eval.  Rather than achieve execution to the end by iterative
// function calls to the /NEXT variant (as in R3-Alpha), Ren-C offers a
// controlling flag to do it from within the core evaluator as a loop.
//
// However: since running to the end follows a different code path than
// performing DO/NEXT several times, it is important to ensure they achieve
// equivalent results.  There are nuances to preserve this invariant and
// especially in light of interaction with lookahead.
//
#define DO_FLAG_TO_END \
    FLAGIT_LEFT(2)


//=//// DO_FLAG_VA_LIST ///////////////////////////////////////////////////=//
//
// Usually VA_LIST_FLAG is enough to tell when there is a source array to
// examine or not.  However, when the end is reached it is written over with
// END_FLAG and it's no longer possible to tell if there's an array available
// to inspect or not.  The few cases that "need to know" are things like
// error delivery, which want to process the array after expression evaluation
// is complete.  Review to see if they actually would rather know something
// else, but this is a cheap flag for now.
//
#define DO_FLAG_VA_LIST \
    FLAGIT_LEFT(3)


#define DO_FLAG_4_IS_TRUE FLAGIT_LEFT(4) // NODE_FLAG_END


//=//// DO_FLAG_TOOK_FRAME_HOLD ///////////////////////////////////////////=//
//
// While R3-Alpha permitted modifications of an array while it was being
// executed, Ren-C does not.  It takes a temporary read-only "hold" if the
// source is not already read only, and sets it back when Do_Core is
// finished (or on errors).  See SERIES_INFO_HOLD for more about this.
//
#define DO_FLAG_TOOK_FRAME_HOLD \
    FLAGIT_LEFT(5)


//=//// DO_FLAG_APPLYING ///.......////////////////////////////////////////=//
//
// Used to indicate that the Do_Core code is entering a situation where the
// frame was already set up.
//
#define DO_FLAG_APPLYING \
    FLAGIT_LEFT(6)


#define DO_FLAG_7_IS_FALSE FLAGIT_LEFT(7) // NOT(NODE_FLAG_CELL)


//=//// DO_FLAG_FULFILLING_ARG ////////////////////////////////////////////=//
//
// Deferred lookback operations need to know when they are dealing with an
// argument fulfillment for a function, e.g. `summation 1 2 3 |> 100` should
// be `(summation 1 2 3) |> 100` and not `summation 1 2 (3 |> 100)`.  This
// also means that `add 1 <| 2` will act as an error.
//
#define DO_FLAG_FULFILLING_ARG \
    FLAGIT_LEFT(8)


//=//// DO_FLAG_NO_ARGS_EVALUATE //////////////////////////////////////////=//
//
// Sometimes a DO operation has already calculated values, and does not want
// to interpret them again.  e.g. the call to the function wishes to use a
// precalculated WORD! value, and not look up that word as a variable.  This
// is common when calling Rebol functions from C code when the parameters are
// known, or what R3-Alpha called "APPLY/ONLY"
//
// !!! It's questionable as to whether this flag needs to exist, or if C
// code should use some kind of special out of band quoting operator to mean
// "literally this value".  (The problem with using the QUOTE word or function
// in this capacity is that then functions that quote their arguments will
// receive the literal QUOTE word or function, but a variadic call from C
// could subvert that with an invisible instruction.)  Currently the existence
// of this mode is leaked to Rebol users through EVAL/ONLY, which may be
// unnecessary complexity to expose.
//
#define DO_FLAG_NO_ARGS_EVALUATE \
    FLAGIT_LEFT(9)


//=//// DO_FLAG_NO_LOOKAHEAD //////////////////////////////////////////////=//
//
// R3-Alpha had a property such that when it was in mid-dispatch of an infix
// function, it would suppress further infix lookahead while getting the
// arguments.  (e.g. with `1 + 2 * 3` it didn't want infix `+` to "look ahead"
// past the 2 to see the infix `*`)
//
// This amounted to what was basically another parameter acquisition mode for
// the right hand sides of OP!, which became named <tight>.  Because tight
// parameter fulfillment added variation into the evaluator, it is being
// replaced by a strategy to use the quoted or non-quoted status of the left
// hand argument of enfixed functions to guide evaluator behavior.  The worst
// case scenario will be that `1 + 2 * 3` becomes 7 instead of 9.
//
// !!! The flag will be needed as long as legacy support is required, because
// this fundamentally different mode of parameter acquisition is controlled at
// the frame level and can't be achieved (reasonably) by other means.
//
#define DO_FLAG_NO_LOOKAHEAD \
    FLAGIT_LEFT(10)


//=//// DO_FLAG_NATIVE_HOLD ///////////////////////////////////////////////=//
//
// When a REBNATIVE()'s code starts running, it means that the associated
// frame must consider itself locked to user code modification.  This is
// because native code does not check the datatypes of its frame contents,
// and if access through the debug API were allowed to modify those contents
// out from under it then it could crash.
//
// A native may wind up running in a reified frame from the get-go (e.g. if
// there is an ADAPT that created the frame and ran user code into it prior
// to the native.)  But the average case is that the native will run on a
// frame that is using the chunk stack, and has no varlist to lock.  But if
// a frame reification happens after the fact, it needs to know to take a
// lock if the native code has started running.
//
// The current solution is that all natives set this flag on the frame as
// part of their entry.  If they have a varlist, they will also lock that...
// but if they don't have a varlist, this flag controls the locking when
// the reification happens.
//
#define DO_FLAG_NATIVE_HOLD \
    FLAGIT_LEFT(11)


//=//// DO_FLAG_DAMPEN_DEFER //////////////////////////////////////////////=//
//
// If an enfixed function wishes to complete an expression on its left, it
// only wants to complete one of them.  `print if false ["a"] else ["b"]`
// is a case where the ELSE wants to allow `if false ["a"]` to complete,
// which it does by deferring its execution.  But when that step is finished,
// the landscape looks like `print *D_OUT* else ["b"]`, and if there is not
// some indication it might defer again, that would just lead print to
// continue the process of deferment, consuming the output for itself.
//
// This is a flag tagged on the parent frame the first time, so it knows to
// defer only once.
//
#define DO_FLAG_DAMPEN_DEFER \
    FLAGIT_LEFT(12)


// Currently the rightmost two bytes of the Reb_Frame->flags are not used,
// so the flags could theoretically go up to 31.  It could hold something
// like the ->eval_type, but performance is probably better to put such
// information in a platform aligned position of the frame.
//
#if defined(__cplusplus) && (__cplusplus >= 201103L)
    static_assert(12 < 32, "DO_FLAG_XXX too high");
#endif



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


#define IS_KIND_INERT(k) \
    LOGICAL((k) >= REB_BLOCK)


union Reb_Frame_Source {
    REBARR *array;
    va_list *vaptr;
};

// NOTE: The ordering of the fields in `Reb_Frame` are specifically done so
// as to accomplish correct 64-bit alignment of pointers on 64-bit systems.
//
// Because performance in the core evaluator loop is system-critical, this
// uses full platform `int`s instead of REBCNTs.
//
// If modifying the structure, be sensitive to this issue--and that the
// layout of this structure is mirrored in Ren-Cpp.
//
struct Reb_Frame {
    //
    // `cell`
    //
    // * This is where the EVAL instruction stores the temporary item that it
    //   splices into the evaluator feed, e.g. for `eval (first [x:]) 10 + 20`
    //   would be the storage for the `x:` SET-WORD! during the addition.
    //
    // * While a function is running, it is free to use it as a GC-safe spot,
    //   which is also implicitly terminated.  See D_CELL.
    //
    RELVAL cell; // can't be REBVAL in C++ build

    // `flags`
    //
    // These are DO_FLAG_XXX or'd together--see their documentation above.
    // A Reb_Header is used so that it can implicitly terminate `cell`,
    // giving natives an enumerable single-cell slot if they need it.
    // See Init_Endlike_Header()
    //
    struct Reb_Header flags;

    // `prior`
    //
    // The prior call frame (may be NULL if this is the topmost stack call).
    //
    // !!! Should there always be a known "top stack level" so prior does
    // not ever have to be tested for NULL from within Do_Core?
    //
    struct Reb_Frame *prior;

    // `dsp_orig`
    //
    // The data stack pointer captured on entry to the evaluation.  It is used
    // by debug checks to make sure the data stack stays balanced after each
    // sub-operation.  It's also used to measure how many refinements have
    // been pushed to the data stack by a path evaluation.
    //
    REBUPT dsp_orig; // type is REBDSP, but enforce alignment here

    // `out`
    //
    // This is where to write the result of the evaluation.  It should not be
    // in "movable" memory, hence not in a series data array.  Often it is
    // used as an intermediate free location to do calculations en route to
    // a final result, due to being GC-safe during function evaluation.
    //
    REBVAL *out;

    // `source.array`, `source.vaptr`
    //
    // This is the source from which new values will be fetched.  In addition
    // to working with an array, it is also possible to feed the evaluator
    // arbitrary REBVAL*s through a variable argument list on the C stack.
    // This means no array needs to be dynamically allocated (though some
    // conditions require the va_list to be converted to an array, see notes
    // on Reify_Va_To_Array_In_Frame().)
    //
    union Reb_Frame_Source source;

    // `specifier`
    //
    // This is used for relatively bound words to be looked up to become
    // specific.  Typically the specifier is extracted from the payload of the
    // ANY-ARRAY! value that provided the source.array for the call to DO.
    // It may also be NULL if it is known that there are no relatively bound
    // words that will be encountered from the source--as in va_list calls.
    //
    REBSPC *specifier;

    // `value`
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
    // !!! Review impacts on debugging; e.g. a debug mode should hold onto
    // the initial value in order to display full error messages.
    //
    const RELVAL *value;

    // `index`
    //
    // This holds the index of the *next* item in the array to fetch as
    // f->value for processing.  It's invalid if the frame is for a C va_list.
    //
    REBUPT index;

    // `expr_index`
    //
    // The error reporting machinery doesn't want where `index` is right now,
    // but where it was at the beginning of a single DO/NEXT step.
    //
    REBUPT expr_index;

    // `eval_type`
    //
    // This is the enumerated type upon which the evaluator's main switch
    // statement is driven, to indicate what the frame is actually doing.
    // e.g. REB_FUNCTION means "running a function".
    //
    // It may not always tell the whole story due to frame reuse--a running
    // state may have stored enough information to not worry about a recursion
    // overwriting it.  See Do_Next_Mid_Frame_Throws() for that case.
    //
    // Additionally, the actual dispatch may not have started, so if a fail()
    // or other operation occurs it may not be able to assume that eval_type
    // of REB_FUNCTION implies that the arguments have been pushed yet.
    // See Is_Function_Frame() for notes on this detection.
    //
    enum Reb_Kind eval_type;

    // `gotten`
    //
    // There is a lookahead step to see if the next item in an array is a
    // WORD!.  If so it is checked to see if that word is a "lookback word"
    // (e.g. one that was SET/LOOKBACK to serve as an infix function).
    // Performing that lookup has the same cost as getting the variable value.
    // Considering that the value will need to be used anyway--infix or not--
    // the pointer is held in this field for WORD!s (and sometimes FUNCTION!)
    //
    // This carries a risk if a DO_NEXT is performed--followed by something
    // that changes variables or the array--followed by another DO_NEXT.
    // There is an assert to check this, and clients wishing to be robust
    // across this (and other modifications) need to use the INDEXOR-based API.
    //
    const REBVAL *gotten;

    // `pending`
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
    const RELVAL *pending;

    // `phase` and `original`
    //
    // If a function call is currently in effect, `phase` holds a pointer to
    // the function being run.  Because functions are identified and passed
    // by a platform pointer as their paramlist REBSER*, you must use
    // `FUNC_VALUE(c->phase)` to get a pointer to a canon REBVAL representing
    // that function (to examine its function flags, for instance).
    //
    // Compositions of functions (adaptations, specializations, hijacks, etc)
    // update `f->phase` in their dispatcher and then signal to resume the
    // evaluation in that same frame in some way.  The `original` function
    //
    REBFUN *original;
    REBFUN *phase;

    // `binding`
    //
    // A REBFUN* alone is not enough to fully specify a function, because
    // it may be an "archetype".  For instance, the archetypal RETURN native
    // doesn't have enough specific information in it to know *which* function
    // to exit.  The additional pointer of context is binding, and it is
    // extracted from the function REBVAL.
    //
    REBNOD *binding; // either a varlist of a FRAME! or function paramlist

    // `opt_label`
    //
    // Functions don't have "names", though they can be assigned to words.
    // However, not all function invocations are through words or paths, so
    // the label may not be known.  It is NULL to indicate anonymity.
    //
    // The evaluator only enforces that the symbol be set during function
    // calls--in the release build, it is allowed to be garbage otherwise.
    //
    REBSTR *opt_label;

    // `varlist`
    //
    // For functions with "indefinite extent", the varlist is the CTX_VARLIST
    // of a FRAME! context in which the function's arguments live.  It is
    // also possible for this varlist to come into existence even for functions
    // like natives, if the frame's context is "reified" (e.g. by the debugger)
    // If neither of these conditions are true, it will be NULL
    //
    // This can contain END markers at any position during arg fulfillment,
    // and this means it cannot have a MANAGE_ARRAY call until that is over.
    //
    REBARR *varlist;

    // `param`
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
    // Made relative just to have another RELVAL on hand.
    //
    const RELVAL *param;

    // `args_head`
    //
    // For functions without "indefinite extent", the invocation arguments are
    // stored in the "chunk stack", where allocations are fast, address stable,
    // and implicitly terminated.  If a function has indefinite extent, this
    // will be set to NULL.
    //
    // This can contain END markers at any position during arg fulfillment,
    // but must all be non-END when the function actually runs.
    //
    // If a function is indefinite extent, this just points to the front of
    // the head of varlist.
    //
    REBVAL *args_head;

    // `arg`
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

    // `special`
    //
    // The specialized argument parallels arg if non-NULL, and contains the
    // value to substitute in the case of a specialized call.  It is END
    // if no specialization in effect, and parallels arg (so it may be
    // incremented on a common code path) if arguments are just being checked
    // vs. fulfilled.
    //
    const REBVAL *special;

    // `refine`
    //
    // During parameter fulfillment, this might point to the `arg` slot
    // of a refinement which is having its arguments processed.  Or it may
    // point to another *read-only* value whose content signals information
    // about how arguments should be handled.  The specific address of the
    // value can be used to test without typing, but then can also be
    // checked with conditional truth and falsehood.
    //
    // * If VOID_CELL, then refinements are being skipped and the arguments
    //   that follow should not be written to.
    //
    // * If BLANK_VALUE, this is an arg to a refinement that was not used in
    //   the invocation.  No consumption should be performed, arguments should
    //   be written as unset, and any non-unset specializations of arguments
    //   should trigger an error.
    //
    // * If FALSE_VALUE, this is an arg to a refinement that was used in the
    //   invocation but has been *revoked*.  It still consumes expressions
    //   from the callsite for each remaining argument, but those expressions
    //   must not evaluate to any value.
    //
    // * If IS_TRUE() the refinement is active but revokable.  So if evaluation
    //   produces no value, `refine` must be mutated to be FALSE.
    //
    // * If EMPTY_BLOCK, it's an ordinary arg...and not a refinement.  It will
    //   be evaluated normally but is not involved with revocation.
    //
    // * If EMPTY_STRING, the evaluator's next argument fulfillment is the
    //   left-hand argument of a lookback operation.  After that fulfillment,
    //   it will be transitioned to EMPTY_BLOCK.
    //
    // Because of how this lays out, IS_TRUTHY() can be used to determine if
    // an argument should be type checked normally...while IS_FALSEY() means
    // that the arg's bits must be set to void.
    //
    REBVAL *refine;
    REBOOL doing_pickups; // want to encode

#if !defined(NDEBUG)
    //
    // `label_debug` [DEBUG]
    //
    // Knowing the label symbol is not as handy as knowing the actual string
    // of the function this call represents (if any).  It is in UTF8 format,
    // and cast to `char*` to help debuggers that have trouble with REBYTE.
    //
    const char *label_debug;

    // `file_debug` [DEBUG]
    //
    // An emerging feature in the system is the ability to connect user-seen
    // series to a file and line number associated with their creation,
    // either their source code or some trace back to the code that generated
    // them.  As the feature gets better, it will certainly be useful to be
    // able to quickly see the information in the debugger for f->source.
    //
    const char *file_debug;
    int line_debug;

    // `kind_debug` [DEBUG]
    //
    // The fetching mechanics cache the type of f->value
    //
    enum Reb_Kind kind_debug;

    // `do_count_debug` [DEBUG]
    //
    // The `do_count` represents the expression evaluation "tick" where the
    // Reb_Frame is starting its processing.  This is helpful for setting
    // breakpoints on certain ticks in reproducible situations.
    //
    REBUPT do_count_debug; // !!! Should this be available in release builds?

    // `state_debug` [DEBUG]
    //
    // Debug reuses PUSH_TRAP's snapshotting to check for leaks at each stack
    // level.  It can also be made to use a more aggresive leak check at every
    // evaluator step--see BALANCE_CHECK_EVERY_EVALUATION_STEP.
    //
    struct Reb_State state_debug;
#endif
};


// It is more pleasant to have a uniform way of speaking of frames by pointer,
// so this macro sets that up for you, the same way DECLARE_LOCAL does.  The
// optimizer should eliminate the extra pointer.
//
#define DECLARE_FRAME(name) \
    REBFRM name##struct; \
    REBFRM * const name = &name##struct; \
    Prep_Stack_Cell(&name->cell)


// Hookable "Rebol DO Function" and "Rebol APPLY Function".  See PG_Do and
// PG_Apply for usage.
//
typedef void (*REBDOF)(REBFRM * const);
typedef REB_R (*REBAPF)(REBFRM * const);
