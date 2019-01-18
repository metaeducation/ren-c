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
// The primary routine that handles DO and EVALUATE is Eval_Core_Throws().  It
// takes a single parameter which holds the running state of the evaluator.
// This state may be allocated on the C variable stack.
//
// Eval_Core_Throws() is written so that a longjmp to a failure handler above
// it can do cleanup safely even though intermediate stacks have vanished.
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
// pulled, allowing a free head value (such as an ACTION! REBVAL in a local
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



// Default for Eval_Core_Throws() operation is just a single EVALUATE, where
// args to functions are evaluated (vs. quoted), and lookahead is enabled.
//
#if defined(NDEBUG)
    #define DO_MASK_DEFAULT \
        DO_FLAG_CONST
#else
    #define DO_MASK_DEFAULT \
        (DO_FLAG_CONST | DO_FLAG_DEFAULT_DEBUG)
#endif


// See Endlike_Header() for why these are chosen the way they are.  This
// means that the Reb_Frame->flags field can function as an implicit END for
// Reb_Frame->cell, as well as be distinguished from a REBVAL*, a REBSER*, or
// a UTF8 string.
//
#define DO_FLAG_0_IS_TRUE FLAG_LEFT_BIT(0) // IS a node
STATIC_ASSERT(DO_FLAG_0_IS_TRUE == NODE_FLAG_NODE);

#define DO_FLAG_1_IS_FALSE FLAG_LEFT_BIT(1) // is NOT free
STATIC_ASSERT(DO_FLAG_1_IS_FALSE == NODE_FLAG_FREE);


//=//// DO_FLAG_TO_END ////////////////////////////////////////////////////=//
//
// As exposed by the DO native and its /NEXT refinement, a call to the
// evaluator can either run to the finish from a position in an array or just
// do one eval.  Rather than achieve execution to the end by iterative
// function calls to the /NEXT variant (as in R3-Alpha), Ren-C offers a
// controlling flag to do it from within the core evaluator as a loop.
//
// However: since running to the end follows a different code path than
// performing EVALUATE several times, it is important to ensure they achieve
// equivalent results.  There are nuances to preserve this invariant and
// especially in light of interaction with lookahead.
//
#define DO_FLAG_TO_END \
    FLAG_LEFT_BIT(2)


//=//// DO_FLAG_PRESERVE_STALE ////////////////////////////////////////////=//
//
// The evaluator tags the output value while running with OUT_FLAG_STALE
// to keep track of whether it can be valid input for an enfix operation.  So
// when you do `[1 () + 2]`, there can be an error even though the `()`
// vaporizes, as the 1 gets the flag..  If this bit weren't cleared, then
// doing `[1 ()]` would return a stale 1 value, and stale values cannot be
// the ->out result of an ACTION! dispatcher C function (checked by assert).
//
// Most callers of the core evaluator don't care about the stale bit.  But
// some want to feed it with a value, and then tell whether the value they
// fed in was overwritten--and this can't be done with just looking at the
// value content itself.  e.g. preloading the output with `3` and then wanting
// to differentiate running `[comment "no data"]` from `[1 + 2]`, to discern
// if the preloaded 3 was overwritten or not.
//
// This DO_FLAG has the same bit position as OUT_FLAG_STALE, allowing it to
// be bitwise-&'d out easily via masking with this bit.  This saves most
// callers the trouble of clearing it (though it's not copied in Move_Value(),
// it will be "sticky" to output cells returned by dispatchers, and it would
// be irritating for every evaluator call to clear it.)
//
#define DO_FLAG_PRESERVE_STALE \
    FLAG_LEFT_BIT(3) // same as OUT_FLAG_STALE (e.g. NODE_FLAG_MARKED)


//=//// DO_FLAG_REEVALUATE_CELL ///////////////////////////////////////////=//
//
// Function dispatchers have a special return value used by EVAL, which tells
// it to use the frame's cell as the head of the next evaluation (before
// what f->value would have ordinarily run.)  It used to have another mode
// which was able to request the frame to change its DO_FLAG_EXPLICIT_EVALUATE
// state for the duration of the next evaluation...a feature that was used
// by EVAL/ONLY.  The somewhat obscure feature was used to avoid needing to
// make a new frame to do that, but raised several questions about semantics.
//
// This allows EVAL/ONLY to be implemented by entering a new subframe with
// new flags, and may have other purposes as well.
//
#define DO_FLAG_REEVALUATE_CELL \
    FLAG_LEFT_BIT(4)


//=//// DO_FLAG_POST_SWITCH ///////////////////////////////////////////////=//
//
// This jump allows a deferred lookback to compensate for the lack of the
// evaluator's ability to (easily) be psychic about when it is gathering the
// last argument of a function.  It allows re-entery to argument gathering at
// the point after the switch() statement, with a preloaded f->out.
//
#define DO_FLAG_POST_SWITCH \
    FLAG_LEFT_BIT(5)


//=//// DO_FLAG_FULFILLING_ARG ////////////////////////////////////////////=//
//
// Deferred lookback operations need to know when they are dealing with an
// argument fulfillment for a function, e.g. `summation 1 2 3 |> 100` should
// be `(summation 1 2 3) |> 100` and not `summation 1 2 (3 |> 100)`.  This
// also means that `add 1 <| 2` will act as an error.
//
#define DO_FLAG_FULFILLING_ARG \
    FLAG_LEFT_BIT(6)


#define DO_FLAG_7_IS_FALSE FLAG_LEFT_BIT(7) // is NOT a cell
STATIC_ASSERT(DO_FLAG_7_IS_FALSE == NODE_FLAG_CELL);


//=//// BITS 8-15 ARE 0 FOR END SIGNAL ////////////////////////////////////=//

// The flags are resident in the frame after the frame's cell.  In order to
// let the cell act like a terminated array (if one needs that), the flags
// have the byte for the IS_END() signal set to 0.  This sacrifices some
// flags, and may or may not be worth it for the feature.


//=//// DO_FLAG_FULFILLING_ENFIX //////////////////////////////////////////=//
//
// Due to the unusual influences of partial refinement specialization, a frame
// may wind up with its enfix parameter as being something like the last cell
// in the argument list...when it has to then go back and fill earlier args
// as normal.  There's no good place to hold the memory that one is doing an
// enfix fulfillment besides a bit on the frame itself.
//
#define DO_FLAG_FULFILLING_ENFIX \
    FLAG_LEFT_BIT(16)


//=//// DO_FLAG_NO_LOOKAHEAD //////////////////////////////////////////////=//
//
// Infix functions may (depending on the #tight or non-tight parameter
// acquisition modes) want to suppress further infix lookahead while getting
// a function argument.  This precedent was started in R3-Alpha, where with
// `1 + 2 * 3` it didn't want infix `+` to "look ahead" past the 2 to see the
// infix `*` when gathering its argument, that was saved until the `1 + 2`
// finished its processing.
//
// See REB_P_TIGHT for more explanation on the parameter class which
// adds this flag to its argument gathering call.
//
#define DO_FLAG_NO_LOOKAHEAD \
    FLAG_LEFT_BIT(17)


//=//// DO_FLAG_PROCESS_ACTION ////////////////////////////////////////////=//
//
// Used to indicate that the Eval_Core code is being jumped into directly to
// process an ACTION!, in a varlist that has already been set up.
//
#define DO_FLAG_PROCESS_ACTION \
    FLAG_LEFT_BIT(18)


//=//// DO_FLAG_NO_PATH_GROUPS ////////////////////////////////////////////=//
//
// This feature is used in PATH! evaluations to request no side effects.
// It prevents GET of a PATH! from running GROUP!s.
//
#define DO_FLAG_NO_PATH_GROUPS \
    FLAG_LEFT_BIT(19)


//=//// DO_FLAG_SET_PATH_ENFIXED //////////////////////////////////////////=//
//
// The way setting of paths is historically designed, it can't absolutely
// give back a location of a variable to be set...since sometimes the result
// is generated, or accessed as a modification of an immediate value.  This
// complicates the interface to where the path dispatcher must be handed
// the value to set and copy itself if necessary.  But CELL_MASK_COPIED does
// not carry forward VALUE_FLAG_ENFIXED in the assignment.  This flag tells
// a frame used with SET-PATH! semantics to make its final assignment enfix.
//
#define DO_FLAG_SET_PATH_ENFIXED \
    FLAG_LEFT_BIT(20)


//=//// DO_FLAG_EXPLICIT_EVALUATE /////////////////////////////////////////=//
//
// Sometimes a DO operation has already calculated values, and does not want
// to interpret them again.  e.g. the call to the function wishes to use a
// precalculated WORD! value, and not look up that word as a variable.  This
// is common when calling Rebol functions from C code when the parameters are
// known (also present in what R3-Alpha called "APPLY/ONLY")
//
// Special escaping operations must be used in order to get evaluation
// behavior.
//
#define DO_FLAG_EXPLICIT_EVALUATE \
    FLAG_LEFT_BIT(21)
STATIC_ASSERT(DO_FLAG_EXPLICIT_EVALUATE == VALUE_FLAG_EVAL_FLIP);


//=//// DO_FLAG_CONST /////////////////////////////////////////////////////=//
//
// The user is able to flip the constness flag explicitly with the CONST and
// MUTABLE functions explicitly.  However, if a frame is marked DO_FLAG_CONST,
// the system imposes it's own constness as part of the "wave of evaluation"
// it does.  While this wave starts out initially with frames demanding const
// marking, if it ever gets flipped (as with DO/MUTABLE) it will have to
// encounter an explicit CONST marking on a value before getting flipped back.
//
// (This behavior is designed to permit switching into a "mode" that is
// compatible with Rebol2/Red behavior, where "source code" is not read-only
// by default.)
//
#define DO_FLAG_CONST \
    FLAG_LEFT_BIT(22)
STATIC_ASSERT(DO_FLAG_CONST == VALUE_FLAG_CONST);


//=//// DO_FLAG_ALREADY_DEFERRED_ENFIX ////////////////////////////////////=//
//
// Ren-C introduced evaluative left hand sides that looked at more than one
// argument.  (Otherwise `IF CONDITION [...] ELSE [...]` would force ELSE to
// produce a result solely on seeing a block on its left.)  These evaluations
// only allow up to one function to run on their left, otherwise there would
// be problems e.g. with `RETURN IF CONDITION [...] ELSE [...]`, which then
// interprets as `(RETURN IF CONDITION [...]) ELSE [...]`.  This flag tracks
// the case that e.g. ELSE already yielded to IF, and shouldn't yield again.
//
#define DO_FLAG_ALREADY_DEFERRED_ENFIX \
    FLAG_LEFT_BIT(23)


//=//// DO_FLAG_BARRIER_HIT ///////////////////////////////////////////////=//
//
// Evaluation of arguments can wind up seeing a barrier and "consuming" it.
// This is true of a BAR!, but also GROUP!s which have no effective content:
//
//    >> 1 + (comment "vaporizes, but disrupts like a BAR! would") 2
//    ** Script Error: + is missing its value2 argument
//
// But the evaluation will advance the frame.  So if a function has more than
// one argument it has to remember that one of its arguments saw a "barrier",
// otherwise it would receive an end signal on an earlier argument yet then
// get a later argument fulfilled.
//
#define DO_FLAG_BARRIER_HIT \
    FLAG_LEFT_BIT(24)


//=//// DO_FLAG_FULLY_SPECIALIZED /////////////////////////////////////////=//
//
// When a null is seen in f->special, the question is whether that is an
// intentional "null specialization" or if it means the argument should be
// gathered normally (if applicable), as it would in a typical invocation.
// If the frame is considered fully specialized (as with DO F) then there
// will be no further argument gathered at the callsite, nulls are as-is.
//
#define DO_FLAG_FULLY_SPECIALIZED \
    FLAG_LEFT_BIT(25)


//=//// DO_FLAG_NO_RESIDUE ////////////////////////////////////////////////=//
//
// Sometimes a single step evaluation is done in which it would be considered
// an error if all of the arguments are not used.  This requests an error if
// the frame does not reach the end.
//
// !!! Interactions with ELIDE won't currently work with this, so evaluation
// would have to take this into account to greedily run ELIDEs if the flag
// is set.  However, it's only used in variadic apply at the moment with
// calls from the system that do not use ELIDE.  These calls may someday
// turn into rebRun(), in which case the mechanism would need rethinking.
//
// !!! A userspace tool for doing this was once conceived as `||`, which
// was variadic and would only allow one evaluation step after it, after
// which it would need to reach either an END or another `||`.
//
#define DO_FLAG_NO_RESIDUE \
    FLAG_LEFT_BIT(26)


//=//// DO_FLAG_DOING_PICKUPS /////////////////////////////////////////////=//
//
// If an ACTION! is invoked through a path and uses refinements in a different
// order from how they appear in the frame's parameter definition, then the
// arguments at the callsite can't be gathered in sequence.  Revisiting them
// will be necessary.  This flag is set while they are revisited, which is
// important not only for Eval_Core_Throws() to know, but also the GC...since
// it means it must protect *all* of the arguments--not just up thru f->param.
//
#define DO_FLAG_DOING_PICKUPS \
    FLAG_LEFT_BIT(27)


//=//// DO_FLAG_PATH_HARD_QUOTE ///////////////////////////////////////////=//
//
// If a path has GROUP!s in it, operations like DEFAULT do not want to run
// them twice...once on a get path and then on a set path.  This means the
// path needs to be COMPOSEd and then use GET/HARD and SET/HARD.
//
#define DO_FLAG_PATH_HARD_QUOTE \
    FLAG_LEFT_BIT(28)


//=//// DO_FLAG_PUSH_PATH_REFINEMENTS /////////////////////////////////////=//
//
// It is technically possible to produce a new specialized ACTION! each
// time you used a PATH!.  This is needed for `apdo: :append/dup/only` as a
// method of partial specialization, but would be costly if just invoking
// a specialization once.  So path dispatch can be asked to push the path
// refinements in the reverse order of their invocation.
//
// This mechanic is also used by SPECIALIZE, so that specializing refinements
// in order via a path and values via a block of code can be done in one
// step, vs needing to make an intermediate ACTION!.
//
#define DO_FLAG_PUSH_PATH_REFINEMENTS \
    FLAG_LEFT_BIT(29)


#if !defined(NDEBUG)

    //=//// DO_FLAG_FINAL_DEBUG ///////////////////////////////////////////=//
    //
    // It's assumed that each run through a frame will re-initialize the do
    // flags, and if a frame's memory winds up reused (e.g. by successive
    // calls in a reduce) that code has to reset the DO_FLAG_XXX each time.
    // To make sure this is the case, this is set on each exit from
    // Eval_Core_Throws(), and each entry checks to ensure it is not present.
    //
    #define DO_FLAG_FINAL_DEBUG \
        FLAG_LEFT_BIT(30)

    //=//// DO_FLAG_DEFAULT_DEBUG /////////////////////////////////////////=//
    //
    // It may be advantageous to have some bits set to true by default instead
    // of false, so all evaluations should describe their settings relative
    // to DO_MASK_DEFAULT, and purposefully mask out any truthy flags that
    // apply by default they don't want (e.g. DO_FLAG_CONST, which is included
    // to err on the side of caution).  The default mask includes this flag
    // just so the evaluator can make sure DO_MASK_DEFAULT was used.
    //
    #define DO_FLAG_DEFAULT_DEBUG \
        FLAG_LEFT_BIT(31)

#endif


STATIC_ASSERT(31 < 32); // otherwise DO_FLAG_XXX too high



//=////////////////////////////////////////////////////////////////////////=//
//
//  DO INDEX OR FLAG (a.k.a. "INDEXOR")
//
//=////////////////////////////////////////////////////////////////////////=//
//
// * END_FLAG if end of series prohibited a full evaluation
//
// * THROWN_FLAG if the output is Thrown--you MUST check!
//
// * ...or the next index position where one might continue evaluation
//
// ===========================((( IMPORTANT )))==============================
//
//      The THROWN_FLAG means your value does not represent a directly
//      usable value, so you MUST check for it.  See notes in %sys-frame.h
//      about what that means.  If you don't know how to handle it, then at
//      least do:
//
//              fail (Error_No_Catch_For_Throw(out));
//
// ===========================================================================
//
// Note that thrownness is not an indicator of an error, rather something that
// ordinary language constructs might meaningfully want to process as they
// bubble up the stack.  Some examples would be BREAK, RETURN, and QUIT.
//
// Errors are handled with a different mechanism using longjmp().  So if an
// actual error happened during the DO then there wouldn't even *BE* a return
// value...because the function call would never return!  See PUSH_TRAP()
// and fail() for more information.
//


#define IS_KIND_INERT(k) \
    ((k) >= REB_BLOCK)


struct Reb_Frame_Source {
    //
    // A frame may be sourced from a va_list of pointers, or not.  If this is
    // NULL it is assumed that the values are sourced from a simple array.
    //
    va_list *vaptr;

    // This contains an IS_END() marker if the next fetch should be an attempt
    // to consult the va_list (if any).  That end marker may be resident in
    // an array, or if it's a plain va_list source it may be the global END.
    //
    const RELVAL *pending;

    // If values are being sourced from an array, this holds the pointer to
    // that array.  By knowing the array it is possible for error and debug
    // messages to reach backwards and present more context of where the
    // error is located.
    //
    REBARR *array;

    // SERIES_INFO_HOLD is used to make a temporary read-only lock of an array
    // while it is running.  Since the same array can wind up on multiple
    // levels of the stack (e.g. recursive functions), the source must be
    // connected with a bit saying whether it was the level that protected it,
    // so it can know to release the hold when it's done.
    //
    bool took_hold;

    // This holds the index of the *next* item in the array to fetch as
    // f->value for processing.  It's invalid if the frame is for a C va_list.
    //
    REBCNT index;
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
    // The frame's cell is used for different purposes.  PARSE uses it as a
    // scratch storage space.  Path evaluation uses it as where the calculated
    // "picker" goes (so if `foo/(1 + 2)`, the 3 would be stored there to be
    // used to pick the next value in the chain).
    //
    // Eval_Core_Throws() uses it to implement the SHOVE() operation, which
    // needs a calculated ACTION! value (including binding) to have a stable
    // location which f->gotten can point to during arbitrary left-hand-side
    // evaluations.
    //
    RELVAL cell;

    // `shove`
    //
    // The SHOVE operation is used to push values from the left--which may
    // need further evaluation if tight or enfix normal--in to act as the left
    // hand side of an operation, e.g.:
    //
    //      add 1 2 -> lib/(print "Hi!" first [multiply]) 10
    //
    // The right side of the operator can do arbitrary evaluation, producing
    // a synthetic ACTION! as the target.  To make matters worse, once this
    // synthetic value is made it still may be necessary to go back and run
    // more code on the left side (e.g. enfix only saw the `2` on the left
    // when the `->` was first encountered, and it has to run the add BEFORE
    // feeding it into the multiply.)
    //
    // In addition, the need to run arbitrary code on the left means there
    // could be multiple shoves in effect.  This requires a GC-safe cell which
    // can't really be used for anything else.  However, there's no need to
    // initialize it until f->gotten indicates it...which only happens with
    // shove operations.
    //
    RELVAL shove;

    // `flags`
    //
    // These are DO_FLAG_XXX or'd together--see their documentation above.
    // A Reb_Header is used so that it can implicitly terminate `shove`,
    // which isn't necessarily that useful...but putting it after `cell`
    // would throw off the alignment for shove.
    //
    union Reb_Header flags; // See Endlike_Header()

    // `prior`
    //
    // The prior call frame.  This never needs to be checked against nullptr,
    // because the bottom of the stack is FS_BOTTOM which is allocated at
    // startup and never used to run code.
    //
    struct Reb_Frame *prior;

    // `dsp_orig`
    //
    // The data stack pointer captured on entry to the evaluation.  It is used
    // by debug checks to make sure the data stack stays balanced after each
    // sub-operation.  It's also used to measure how many refinements have
    // been pushed to the data stack by a path evaluation.
    //
    uintptr_t dsp_orig; // type is REBDSP, but enforce alignment here

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
    // Since frames may share source information, this needs to be done with
    // a dereference.
    //
    struct Reb_Frame_Source *source;

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
    // This is the "prefetched" value being processed.  Entry points to the
    // evaluator must load a first value pointer into it...which for any
    // successive evaluations will be updated via Fetch_Next_In_Frame()--which
    // retrieves values from arrays or va_lists.  But having the caller pass
    // in the initial value gives the option of that value being out of band.
    //
    // (Hence if one has the series `[[a b c] [d e]]` it would be possible to
    // have an independent path value `append/only` and NOT insert it in the
    // series, yet get the effect of `append/only [a b c] [d e]`.  This only
    // works for one value, but is a convenient no-cost trick for apply-like
    // situations...as insertions usually have to "slide down" the values in
    // the series and may also need to perform alloc/free/copy to expand.
    // It also is helpful since in C, variadic functions must have at least
    // one non-variadic parameter...and one might want that non-variadic
    // parameter to be blended in with the variadics.)
    //
    // !!! Review impacts on debugging; e.g. a debug mode should hold onto
    // the initial value in order to display full error messages.
    //
    const RELVAL *value;

    // `expr_index`
    //
    // The error reporting machinery doesn't want where `index` is right now,
    // but where it was at the beginning of a single EVALUATE step.
    //
    uintptr_t expr_index;

    // `gotten`
    //
    // There is a lookahead step to see if the next item in an array is a
    // WORD!.  If so it is checked to see if that word is a "lookback word"
    // (e.g. one that refers to an ACTION! value set with SET/ENFIX).
    // Performing that lookup has the same cost as getting the variable value.
    // Considering that the value will need to be used anyway--infix or not--
    // the pointer is held in this field for WORD!s (and sometimes ACTION!)
    //
    // This carries a risk if a DO_NEXT is performed--followed by something
    // that changes variables or the array--followed by another DO_NEXT.
    // There is an assert to check this, and clients wishing to be robust
    // across this (and other modifications) need to use the INDEXOR-based API.
    //
    const REBVAL *gotten;

    // `original`
    //
    // If a function call is currently in effect, FRM_PHASE() is how you get
    // at the current function being run.  This is the action that started
    // the process.
    //
    // Compositions of functions (adaptations, specializations, hijacks, etc)
    // update the FRAME!'s payload in the f->varlist archetype to say what
    // the current "phase" is.  The reason it is updated there instead of
    // as a REBFRM field is because specifiers use it.  Similarly, that is
    // where the binding is stored.
    //
    REBACT *original;

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
    // The varlist is where arguments for the frame are kept.  Though it is
    // ultimately usable as an ordinary CTX_VARLIST() for a FRAME! value, it
    // is different because it is built progressively, with random bits in
    // its pending capacity that are specifically accounted for by the GC...
    // which limits its marking up to the progress point of `f->param`.
    //
    // It starts out unmanaged, so that if no usages by the user specifically
    // ask for a FRAME! value, and the REBCTX* isn't needed to store in a
    // Derelativize()'d or Move_Velue()'d value as a binding, it can be
    // reused or freed.  See Push_Action() and Drop_Action() for the logic.
    //
    REBARR *varlist;
    REBVAL *rootvar; // cache of CTX_ARCHETYPE(varlist) if varlist is not null

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

    // `arg`
    //
    // "arg" is the "actual argument"...which holds the pointer to the
    // REBVAL slot in the `arglist` for that corresponding `param`.  These
    // are moved in sync.  This movement can be done for typechecking or
    // fulfillment, see In_Typecheck_Mode()
    //
    // If arguments are actually being fulfilled into the slots, those
    // slots start out as trash.  Yet the GC has access to the frame list,
    // so it can examine f->arg and avoid trying to protect the random
    // bits that haven't been fulfilled yet.
    //
    REBVAL *arg;

    // `special`
    //
    // The specialized argument parallels arg if non-NULL, and contains the
    // value to substitute in the case of a specialized call.  It is NULL
    // if no specialization in effect, else it parallels arg (so it may be
    // incremented on a common code path) if arguments are just being checked
    // vs. fulfilled.
    //
    // However, in PATH! frames, `special` is non-NULL if this is a SET-PATH!,
    // and it is the value to ultimately set the path to.  The set should only
    // occur at the end of the path, so most setters should check
    // `IS_END(pvs->value + 1)` before setting.
    //
    // !!! See notes at top of %c-path.c about why the path dispatch is more
    // complicated than simply being able to only pass the setval to the last
    // item being dispatched (which would be cleaner, but some cases must
    // look ahead with alternate handling).
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
    // * If NULLED_CELL, then refinements are being skipped and the arguments
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
    // * If EMPTY_TEXT, the evaluator's next argument fulfillment is the
    //   left-hand argument of a lookback operation.  After that fulfillment,
    //   it will be transitioned to EMPTY_BLOCK.
    //
    // Because of how this lays out, IS_TRUTHY() can be used to determine if
    // an argument should be type checked normally...while IS_FALSEY() means
    // that the arg must be a NULL.
    //
    // In path processing, ->refine points to the soft-quoted product of the
    // current path item (the "picker").  So on the second step of processing
    // foo/(1 + 2) it would be 3.
    //
    REBVAL *refine;

  union {
    // `deferred`
    //
    // The deferred pointer is used to mark an argument cell which *might*
    // need to do more enfix processing in the frame--but only if it turns out
    // to be the last argument being processed.  For instance, in both of
    // these cases the AND finds itself gathering an argument to a function
    // where there is an evaluated 10 on the left hand side:
    //
    //    x: 10
    //
    //    if block? x and ... [...]
    //
    //    if x and ... [...]
    //
    // In the former case, the evaluated 10 is fulfilling the one and only
    // argument to BLOCK?.  The latter case has it fulfilling the *first* of
    // two arguments to IF.  Since AND has REB_P_NORMAL for its left
    // argument (as opposed to REB_P_TIGHT), it wishes to interpret the
    // first case as `if (block? 10) and ... [...], but still let the second
    // case work too.  Yet discerning these in advance is costly/complex.
    //
    // The trick used is to not run the AND, go ahead and let the cell fill
    // the frame either way, and set `deferred` in the frame above to point
    // at the cell.  If the function finishes gathering arguments and deferred
    // wasn't cleared by some other operation (like in the `if x` case), then
    // that cell is re-dispatched with DO_FLAG_POST_SWITCH to give the
    // impression that the AND had "tightly" taken the argument all along.
    //
    struct {
        REBVAL *arg;
        const RELVAL *param;
        REBVAL *refine;
    } defer;

    // References are used by path dispatch.
    //
    struct {
        RELVAL *cell;
        REBSPC *specifier;
    } ref;

    // Used to slip cell to re-evaluate into Eval_Core_Throws()
    //
    struct {
        const REBVAL *value;
    } reval;
  } u;

   #if defined(DEBUG_COUNT_TICKS)
    //
    // `tick` [DEBUG]
    //
    // The expression evaluation "tick" where the Reb_Frame is starting its
    // processing.  This is helpful for setting breakpoints on certain ticks
    // in reproducible situations.
    //
    uintptr_t tick; // !!! Should this be in release builds, exposed to users?
  #endif

  #if defined(DEBUG_FRAME_LABELS)
    //
    // `label` [DEBUG]
    //
    // Knowing the label symbol is not as handy as knowing the actual string
    // of the function this call represents (if any).  It is in UTF8 format,
    // and cast to `char*` to help debuggers that have trouble with REBYTE.
    //
    const char *label_utf8;
  #endif

  #if !defined(NDEBUG)
    //
    // `file` [DEBUG]
    //
    // An emerging feature in the system is the ability to connect user-seen
    // series to a file and line number associated with their creation,
    // either their source code or some trace back to the code that generated
    // them.  As the feature gets better, it will certainly be useful to be
    // able to quickly see the information in the debugger for f->source.
    //
    const char *file; // is REBYTE (UTF-8), but char* for debug watch
    int line;
  #endif

  #if defined(DEBUG_BALANCE_STATE)
    //
    // `state` [DEBUG]
    //
    // Debug reuses PUSH_TRAP's snapshotting to check for leaks at each stack
    // level.  It can also be made to use a more aggresive leak check at every
    // evaluator step--see BALANCE_CHECK_EVERY_EVALUATION_STEP.
    //
    struct Reb_State state;
  #endif

  #if defined(DEBUG_EXPIRED_LOOKBACK)
    //
    // On each call to Fetch_Next_In_Frame, it's possible to ask it to give
    // a pointer to a cell with equivalent data to what was previously in
    // f->value, but that might not be f->value.  So for all practical
    // purposes, one is to assume that the f->value pointer died after the
    // fetch.  If clients are interested in doing "lookback" and examining
    // two values at the same time (or doing a GC and expecting to still
    // have the old f->current work), then they must not use the old f->value
    // but request the lookback pointer from Fetch_Next_In_Frame().
    //
    // To help stress this invariant, frames will forcibly expire REBVAL
    // cells, handing out disposable lookback pointers on each eval.
    //
    // !!! Test currently leaks on shutdown, review how to not leak.
    //
    RELVAL *stress;
  #endif
};


// It is more pleasant to have a uniform way of speaking of frames by pointer,
// so this macro sets that up for you, the same way DECLARE_LOCAL does.  The
// optimizer should eliminate the extra pointer.
//
// Just to simplify matters, the frame cell is set to a bit pattern the GC
// will accept.  It would need stack preparation anyway, and this simplifies
// the invariant so if a recycle happens before Eval_Core_Throws() gets to its
// body, it's always set to something.  Using an unreadable blank means we
// signal to users of the frame that they can't be assured of any particular
// value between evaluations; it's not cleared.
//

#define DECLARE_FRAME_CORE(name, source_ptr) \
    REBFRM name##struct; \
    name##struct.source = (source_ptr); \
    REBFRM * const name = &name##struct; \
    Prep_Stack_Cell(&name->cell); \
    Init_Unreadable_Blank(&name->cell); \
    name->dsp_orig = DSP;

#define DECLARE_FRAME(name) \
    struct Reb_Frame_Source name##source; \
    DECLARE_FRAME_CORE(name, &name##source)

#define DECLARE_END_FRAME(name) \
    DECLARE_FRAME_CORE(name, &TG_Frame_Source_End)

#define DECLARE_SUBFRAME(name, parent) \
    DECLARE_FRAME_CORE(name, (parent)->source)


#define FS_TOP (TG_Top_Frame + 0) // avoid assign to FS_TOP via + 0
#define FS_BOTTOM (TG_Bottom_Frame + 0) // avoid assign to FS_BOTTOM via + 0


// Hookable evaluator core function (see PG_Eval_Throws, Eval_Core_Throws())
// Unlike a dispatcher, its result is always in the frame's ->out cell, and
// the boolean result only tells you whether or not it threw.
//
typedef bool (*REBEVL)(REBFRM * const);


//=////////////////////////////////////////////////////////////////////////=//
//
// SPECIAL VALUE MODES FOR (REBFRM*)->REFINE
//
//=////////////////////////////////////////////////////////////////////////=//
//
// f->refine is a bit tricky.  If it IS_LOGIC() and TRUE, then this means that
// a refinement is active but revokable, having its arguments gathered.  So
// it actually points to the f->arg of the active refinement slot.  If
// evaluation of an argument in this state produces no value, the refinement
// must be revoked, and its value mutated to be FALSE.
//
// But all the other values that f->refine can hold are read-only pointers
// that signal something about the argument gathering state:
//
// * If NULL, then refinements are being skipped, and the following arguments
//   should not be written to.
//
// * If FALSE_VALUE, this is an arg to a refinement that was not used in
//   the invocation.  No consumption should be performed, arguments should
//   be written as unset, and any non-unset specializations of arguments
//   should trigger an error.
//
// * If BLANK_VALUE, this is an arg to a refinement that was used in the
//   invocation but has been *revoked*.  It still consumes expressions
//   from the callsite for each remaining argument, but those expressions
//   must not evaluate to any value.
//
// * If EMPTY_BLOCK, it's an ordinary arg...and not a refinement.  It will
//   be evaluated normally but is not involved with revocation.
//
// * If EMPTY_TEXT, the evaluator's next argument fulfillment is the
//   left-hand argument of a lookback operation.  After that fulfillment,
//   it will be transitioned to EMPTY_BLOCK.
//
// Because of how this lays out, IS_TRUTHY() can be used to determine if an
// argument should be type checked normally...while IS_FALSEY() means that the
// arg's bits must be set to null.  Since the skipping-refinement-args case
// doesn't write to arguments at all, it doesn't get to the point where the
// decision of type checking needs to be made...so using C's nullptr for that
// means the comparison is a little bit faster.
//
// These special values are all pointers to read-only cells, but are cast to
// mutable in order to be held in the same pointer that might write to a
// refinement to revoke it.  Note that since literal pointers are used, tests
// like `f->refine == BLANK_VALUE` are faster than `IS_BLANK(f->refine)`.
//
// !!! ^-- While that's presumably true, it would be worth testing if a
// dereference of the single byte via VAL_TYPE() is ever faster.
//

#define SKIPPING_REFINEMENT_ARGS \
    nullptr // 0 pointer comparison generally faster than to arbitrary pointer

#define ARG_TO_UNUSED_REFINEMENT \
    m_cast(REBVAL*, FALSE_VALUE)

#define ARG_TO_IRREVOCABLE_REFINEMENT \
    m_cast(REBVAL*, TRUE_VALUE)

#define ARG_TO_REVOKED_REFINEMENT \
    m_cast(REBVAL*, BLANK_VALUE)

#define ORDINARY_ARG \
    m_cast(REBVAL*, EMPTY_BLOCK)


#if !defined(DEBUG_CHECK_CASTS) || !defined(CPLUSPLUS_11)

    #define FRM(p) \
        cast(REBFRM*, (p)) // FRM() just does a cast (maybe with added checks)

#else

    template <class T>
    inline REBFRM *FRM(T *p) {
        constexpr bool base = std::is_same<T, void>::value
            or std::is_same<T, REBNOD>::value;

        static_assert(base, "FRM() works on void/REBNOD");

        if (base)
            assert(
                (reinterpret_cast<REBNOD*>(p)->header.bits & (
                    NODE_FLAG_NODE | NODE_FLAG_FREE | NODE_FLAG_CELL
                )) == (
                    NODE_FLAG_NODE | NODE_FLAG_CELL
                )
            );

        return reinterpret_cast<REBFRM*>(p);
    }

#endif
