//
//  file: %sys-frame.h
//  summary:{Evaluator "Do State"}
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// This is because Push_Level and Drop_Level maintain an independent global
// list of the frames in effect, so that the Panic_Core() routine can unwind
// all the associated storage and structures for each frame.
//
// Ren-C can not only run the evaluator across an Array-style series of
// input based on index, it can also enumerate through C's `va_list`,
// providing the ability to pass pointers as Value* in a variadic function
// call from the C (comma-separated arguments, as with printf()).  (Modern
// Ren-C, after this branch, can also traverse a Value[] raw C array.)
//
// To provide even greater flexibility, it allows the very first element's
// pointer in an evaluation to come from an arbitrary source.  It doesn't
// have to be resident in the same sequence from which ensuing values are
// pulled, allowing a free head value (such as an ACTION! cell in a local
// C variable) to be evaluated in combination from another source (like a
// va_list or series representing the arguments.)  This avoids the cost and
// complexity of allocating a series to combine the values together.
//
// These features alone would not cover the case when cell pointers that
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
#define DO_MASK_NONE 0

// See Endlike_Header() for why these are chosen the way they are.  This
// means that the Level->flags field can function as an implicit END for
// Level->cell, as well as be distinguished from a Value*, a Flex*, or
// a UTF8 string.
//
#define EVAL_FLAG_0_IS_TRUE FLAG_LEFT_BIT(0) // NODE_FLAG_NODE
#define EVAL_FLAG_1_IS_FALSE FLAG_LEFT_BIT(1) // NOT(NODE_FLAG_UNREADABLE)

STATIC_ASSERT(EVAL_FLAG_0_IS_TRUE == NODE_FLAG_NODE);
STATIC_ASSERT(EVAL_FLAG_1_IS_FALSE == NODE_FLAG_UNREADABLE);


//=//// EVAL_FLAG_TO_END //////////////////////////////////////////////////=//
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
#define EVAL_FLAG_TO_END \
    FLAG_LEFT_BIT(2)


//=//// EVAL_FLAG_PRESERVE_STALE //////////////////////////////////////////=//
//
// The evaluator tags the output value while running with OUT_FLAG_STALE
// to keep track of whether it can be valid input for an infix operation.  So
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
// callers the trouble of clearing it (though it's not copied in Copy_Cell(),
// it will be "sticky" to output cells returned by dispatchers, and it would
// be irritating for every evaluator call to clear it.)
//
#define EVAL_FLAG_PRESERVE_STALE \
    FLAG_LEFT_BIT(3) // same as OUT_FLAG_STALE (e.g. NODE_FLAG_MARKED)


//=//// EVAL_FLAG_4_IS_FALSE //////////////////////////////////////////////=//
//
// The second do byte is TYPE_0 to indicate an END.  That helps reads know
// there is an END for in-situ enumeration.  But as an added bit of safety,
// we make sure the bit pattern in the level header also doesn't look like
// a cell at all by having a 0 bit in the NODE_FLAG_CELL spot.
//
#define EVAL_FLAG_4_IS_FALSE \
    FLAG_LEFT_BIT(4)

STATIC_ASSERT(EVAL_FLAG_4_IS_FALSE == NODE_FLAG_CELL);


//=//// EVAL_FLAG_POST_SWITCH /////////////////////////////////////////////=//
//
// This jump allows a deferred lookback to compensate for the lack of the
// evaluator's ability to (easily) be psychic about when it is gathering the
// last argument of a function.  It allows re-entery to argument gathering at
// the point after the switch() statement, with a preloaded L->out.
//
#define EVAL_FLAG_POST_SWITCH \
    FLAG_LEFT_BIT(5)


//=//// EVAL_FLAG_FULFILLING_ARG //////////////////////////////////////////=//
//
// Deferred lookback operations need to know when they are dealing with an
// argument fulfillment for a function, e.g. `summation 1 2 3 |> 100` should
// be `(summation 1 2 3) |> 100` and not `summation 1 2 (3 |> 100)`.  This
// also means that `add 1 <| 2` will act as an error.
//
#define EVAL_FLAG_FULFILLING_ARG \
    FLAG_LEFT_BIT(6)


//=//// EVAL_FLAG_REEVALUATE_CELL /////////////////////////////////////////=//
//
// Function dispatchers have a special return value used by EVAL, which tells
// it to use the frame's cell as the head of the next evaluation (before
// what L->value would have ordinarily run.)  It used to have another mode
// which was able to request the frame to change EVAL_FLAG_EXPLICIT_EVALUATE
// state for the duration of the next evaluation...a feature that was used
// by EVAL/ONLY.  The somewhat obscure feature was used to avoid needing to
// make a new frame to do that, but raised several questions about semantics.
//
// This allows EVAL/ONLY to be implemented by entering a new subframe with
// new flags, and may have other purposes as well.
//
#define EVAL_FLAG_REEVALUATE_CELL \
     FLAG_LEFT_BIT(7)


//=//// BITS 8-15 ARE 0 FOR END SIGNAL ////////////////////////////////////=//

// The flags are resident in the frame after the frame's cell.  In order to
// let the cell act like a terminated array (if one needs that), the flags
// have the byte for the IS_END() signal set to 0.  This sacrifices some
// flags, and may or may not be worth it for the feature.


//=//// EVAL_FLAG_TOOK_FRAME_HOLD /////////////////////////////////////////=//
//
// While R3-Alpha permitted modifications of an array while it was being
// executed, Ren-C does not.  It takes a temporary read-only "hold" if the
// source is not already read only, and sets it back when Eval_Core is
// finished (or on errors).  See FLEX_INFO_HOLD for more about this.
//
#define EVAL_FLAG_TOOK_FRAME_HOLD \
    FLAG_LEFT_BIT(16)


//=//// EVAL_FLAG_NO_LOOKAHEAD ////////////////////////////////////////////=//
//
// Infix functions traditionally suppress further infix lookahead while getting
// a function argument.  This precedent was started in R3-Alpha, where with
// `1 + 2 * 3` it didn't want infix `+` to "look ahead" past the 2 to see the
// infix `*` when gathering its argument, that was saved until the `1 + 2`
// finished its processing.
//
#define EVAL_FLAG_NO_LOOKAHEAD \
    FLAG_LEFT_BIT(17)


//=//// EVAL_FLAG_PROCESS_ACTION //////////////////////////////////////////=//
//
// Used to indicate that the Eval_Core code is being jumped into directly to
// process an ACTION!, in a varlist that has already been set up.
//
#define EVAL_FLAG_PROCESS_ACTION \
    FLAG_LEFT_BIT(18)


//=//// EVAL_FLAG_NO_PATH_GROUPS //////////////////////////////////////////=//
//
// This feature is used in PATH! evaluations to request no side effects.
// It prevents GET of a PATH! from running GROUP!s.
//
#define EVAL_FLAG_NO_PATH_GROUPS \
    FLAG_LEFT_BIT(19)


//=//// EVAL_FLAG_RUNNING_AS_INFIX ////////////////////////////////////////=//
//
// This flag is held onto for the duration of running an infix function, so
// that the evaluator knows not to eagerly consume more infix.  This is so
// that (1 + 2 * 3) gives 9.
//
#define EVAL_FLAG_RUNNING_AS_INFIX \
    FLAG_LEFT_BIT(20)


//=//// EVAL_FLAG_PARSE_FRAME /////////////////////////////////////////////=//
//
// This flag is set when a Level* is being used to hold the state of the
// PARSE stack.  One application of knowing this is that PARSE wasn't really
// written to use frames, and doesn't follow the same rules as the evaluator;
// so the debugging checks have to be more lax;
//
#define EVAL_FLAG_PARSE_FRAME \
    FLAG_LEFT_BIT(21)


//=//// EVAL_FLAG_22 //////////////////////////////////////////////////////=//
//
#define EVAL_FLAG_22 \
    FLAG_LEFT_BIT(22)


//=//// EVAL_FLAG_PUSH_PATH_REFINEMENTS ///////////////////////////////////=//
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
#define EVAL_FLAG_PUSH_PATH_REFINEMENTS \
    FLAG_LEFT_BIT(23)


//=//// EVAL_FLAG_24 //////////////////////////////////////////////////////=//
//
#define EVAL_FLAG_24 \
    FLAG_LEFT_BIT(24)


//=//// EVAL_FLAG_25 //////////////////////////////////////////////////////=//
//
#define EVAL_FLAG_25 \
    FLAG_LEFT_BIT(25)


//=//// EVAL_FLAG_NO_RESIDUE //////////////////////////////////////////////=//
//
// Sometimes a single step evaluation is done in which it would be considered
// an error if all of the arguments are not used.  This requests an error if
// the frame does not reach the end.
//
// !!! Interactions with ELIDE won't currently work with this, so evaluation
// would have to take this into account to greedily run ELIDEs if the flag
// is set.  However, it's only used in variadic apply at the moment with
// calls from the system that do not use ELIDE.  These calls may someday
// turn into rebValue(), in which case the mechanism would need rethinking.
//
// !!! A userspace tool for doing this was once conceived as `||`, which
// was variadic and would only allow one evaluation step after it, after
// which it would need to reach either an END or another `||`.
//
#define EVAL_FLAG_NO_RESIDUE \
    FLAG_LEFT_BIT(26)


//=//// EVAL_FLAG_DOING_PICKUPS ///////////////////////////////////////////=//
//
// If an ACTION! is invoked through a path and uses refinements in a different
// order from how they appear in the frame's parameter definition, then the
// arguments at the callsite can't be gathered in sequence.  Revisiting them
// will be necessary.  This flag is set while they are revisited, which is
// important not only for Eval_Core_Throws() to know, but also the GC...since
// it means it must protect *all* of the arguments--not just up thru L->param.
//
#define EVAL_FLAG_DOING_PICKUPS \
    FLAG_LEFT_BIT(27)


#if RUNTIME_CHECKS

//=//// EVAL_FLAG_FINAL_DEBUG /////////////////////////////////////////////=//
//
// It is assumed that each run through a frame will re-initialize the do
// flags, and if a frame's memory winds up getting reused (e.g. by successive
// calls in a reduce) that code is responsible for resetting the EVAL_FLAG_XXX
// each time.  To make sure this is the case, this is set on each exit from
// Eval_Core_Throws(), and each entry checks to make sure it is not present.
//

#define EVAL_FLAG_FINAL_DEBUG \
    FLAG_LEFT_BIT(28)

#endif


#if CPLUSPLUS_11
    static_assert(28 < 32, "EVAL_FLAG_XXX too high");
#endif


#define Get_Eval_Flag(L,name) \
    (((L)->flags.bits & EVAL_FLAG_##name) != 0)

#define Not_Eval_Flag(L,name) \
    (((L)->flags.bits & EVAL_FLAG_##name) == 0)

#define Set_Eval_Flag(L,name) \
    m_cast(union HeaderUnion*, &(L)->flags)->bits |= EVAL_FLAG_##name

#define Clear_Eval_Flag(L,name) \
    m_cast(union HeaderUnion*, &(L)->flags)->bits &= ~EVAL_FLAG_##name


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
//              panic (Error_No_Catch_For_Throw(out));
//
//      If you *do* handle it, be aware it's a throw label with
//      CELL_FLAG_THROW_SIGNAL set in its header, and shouldn't leak to the
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
// and panic() for more information.
//


#define IS_KIND_INERT(k) \
    ((k) >= TYPE_BLOCK)


struct Reb_Level_Source {
    //
    // A frame may be sourced from a va_list of pointers, or not.  If this is
    // nullptr it's assumed that the values are sourced from a simple array.
    //
    va_list *vaptr;

    // This contains an IS_END() marker if the next fetch should be an attempt
    // to consult the va_list (if any).  That end marker may be resident in
    // an array, or if it's a plain va_list source it may be the global END.
    //
    const Cell* pending;

    // If values are being sourced from an array, this holds the pointer to
    // that array.  By knowing the array it is possible for error and debug
    // messages to reach backwards and present more context of where the
    // error is located.
    //
    Array* array;

    // `index`
    //
    // This holds the index of the *next* item in the array to fetch as
    // L->value for processing.  It's invalid if the frame is for a C va_list.
    //
    REBLEN index;

    // This is set to true when an infix deferral has been requested.  If
    // this is seen as true, that means it's the second visit.
    //
    bool deferring_infix;
};


// NOTE: The ordering of the fields in LevelStruct are specifically done so
// as to accomplish correct 64-bit alignment of pointers on 64-bit systems.
//
// Because performance in the core evaluator loop is system-critical, this
// uses full platform `int`s instead of REBCNTs.
//
// If modifying the structure, be sensitive to this issue--and that the
// layout of this structure is mirrored in Ren-Cpp.
//
struct LevelStruct {
    //
    // `spare`
    //
    // The frame's spare is used for different purposes.  PARSE uses it as a
    // scratch storage space.  Path evaluation uses it as where the calculated
    // "picker" goes (so if `foo/(1 + 2)`, the 3 would be stored there to be
    // used to pick the next value in the chain).
    //
    // Eval_Core_Throws() uses it to implement the SHOVE() operation, which
    // needs a calculated ACTION! value (including binding) to have a stable
    // location which L->gotten can point to during arbitrary left-hand-side
    // evaluations.
    //
    Cell spare;

    // `flags`
    //
    // These are EVAL_FLAG_XXX or'd together--see their documentation above.
    // A HeaderUnion is used so that it can implicitly terminate `shove`,
    // which isn't necessarily that useful...but putting it after `cell`
    // would throw off the alignment for shove.
    //
    union HeaderUnion flags; // See Endlike_Header()

    // `prior`
    //
    // The prior call frame.  This never needs to be checked against nullptr,
    // because the bottom of the stack is BOTTOM_LEVEL which is allocated at
    // startup and never used to run code.
    //
    Level* prior;

    // `base`
    //
    // The data stack pointer captured on entry to the evaluation.  It is used
    // by debug checks to make sure the data stack stays balanced after each
    // sub-operation.  It's also used to measure how many refinements have
    // been pushed to the data stack by a path evaluation.
    //
    uintptr_t stack_base;  // type is StackIndex, but enforce alignment here

    // `out`
    //
    // This is where to write the result of the evaluation.  It should not be
    // in "movable" memory, hence not in a series data array.  Often it is
    // used as an intermediate free location to do calculations en route to
    // a final result, due to being GC-safe during function evaluation.
    //
    Value* out;

    // `source.array`, `source.vaptr`
    //
    // This is the source from which new values will be fetched.  In addition
    // to working with an array, it is also possible to feed the evaluator
    // arbitrary Value*s through a variable argument list on the C stack.
    // This means no array needs to be dynamically allocated (though some
    // conditions require the va_list to be converted to an array, see notes
    // on Reify_Va_To_Array_In_Level().)
    //
    // Since frames may share source information, this needs to be done with
    // a dereference.
    //
    struct Reb_Level_Source *source;

    // `specifier`
    //
    // This is used for relatively bound words to be looked up to become
    // specific.  Typically the specifier is extracted from the payload of the
    // ANY-ARRAY! value that provided the source.array for the call to DO.
    // It may be nullptr if it's known that there are no relatively bound
    // words that will be encountered from the source--as in va_list calls.
    //
    Specifier* specifier;

    // `value`
    //
    // This is the "prefetched" value being processed.  Entry points to the
    // evaluator must load a first value pointer into it...which for any
    // successive evaluations will be updated via Fetch_Next_In_Level()--which
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
    const Cell* value;

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
    // (e.g. one that refers to an ACTION! value set with the INFIX flag).
    // Performing that lookup has the same cost as getting the variable value.
    // Considering that the value will need to be used anyway--infix or not--
    // the pointer is held in this field for WORD!s (and sometimes ACTION!)
    //
    // This carries a risk if a DO_NEXT is performed--followed by something
    // that changes variables or the array--followed by another DO_NEXT.
    // There is an assert to check this, and clients wishing to be robust
    // across this (and other modifications) need to use the INDEXOR-based API.
    //
    const Value* gotten;

    // `original`
    //
    // If a function call is currently in effect, Level_Phase() is how you get
    // at the curren function being run.  Because functions are identified and
    // passed by a platform pointer as their paramlist Array*, you must use
    // `ACT_ARCHETYPE(Level_Phase(L))` to get a pointer to a canon cell
    // representing that function (to examine its value flags, for instance).
    //
    // !!! CELL_FLAG_ACTION_XXX should probably be used for frequently checks.
    //
    // Compositions of functions (adaptations, specializations, hijacks, etc)
    // update the FRAME!'s payload in the L->varlist archetype to say what
    // the current "phase" is.  The reason it is updated there instead of
    // as a LevelStruct field is because specifiers use it.  Similarly, that is
    // where the binding is stored.
    //
    REBACT *original;

    // `opt_label`
    //
    // Functions don't have "names", though they can be assigned to words.
    // However, not all function invocations are through words or paths, so
    // the label may not be known.  It is nullptr to indicate anonymity.
    //
    // The evaluator only enforces that the symbol be set during function
    // calls--in the release build, it is allowed to be garbage otherwise.
    //
    Symbol* opt_label;

    // `varlist`
    //
    // The varlist is where arguments for the frame are kept.  Though it is
    // ultimately usable as an ordinary Varlist_Array() for a FRAME! value, it
    // is different because it is built progressively, with random bits in
    // its pending capacity that are specifically accounted for by the GC...
    // which limits its marking up to the progress point of `L->param`.
    //
    // It starts out unmanaged, so that if no usages by the user specifically
    // ask for a FRAME! value, and the VarList* isn't needed to store in a
    // Derelativize()'d or Move_Velue()'d value as a binding, it can be
    // reused or freed.  See Push_Action() and Drop_Action() for the logic.
    //
    Array* varlist;
    Value* rootvar; // cache of Varlist_Archetype(varlist) if varlist is not null

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
    // Made relative just to have another Cell on hand.
    //
    const Cell* param;

    // `arg`
    //
    // "arg" is the "actual argument"...which holds the pointer to the
    // Value slot in the `arglist` for that corresponding `param`.  These
    // are moved in sync.  This movement can be done for typechecking or
    // fulfillment, see In_Typecheck_Mode()
    //
    // If arguments are actually being fulfilled into the slots, those
    // slots start out as trash.  Yet the GC has access to the frame list,
    // so it can examine L->arg and avoid trying to protect the random
    // bits that haven't been fulfilled yet.
    //
    Value* arg;

    // `special`
    //
    // The specialized argument parallels arg if non-nullptr, and contains
    // what to substitute in the case of a specialized call.  It is nullptr
    // if no specialization in effect, else it parallels arg (so it may be
    // incremented on a common code path) if arguments are just being checked
    // vs. fulfilled.
    //
    // But in PATH! frames, `special` is non-nullptr if this is a SET-PATH!,
    // and it is the value to ultimately set the path to.  The set should only
    // occur at the end of the path, so most setters should check
    // `IS_END(pvs->value + 1)` before setting.
    //
    // !!! See notes at top of %c-path.c about why the path dispatch is more
    // complicated than simply being able to only pass the setval to the last
    // item being dispatched (which would be cleaner, but some cases must
    // look ahead with alternate handling).
    //
    const Value* special;

    // `refine`
    //
    // During parameter fulfillment, this might point to the `arg` slot
    // of a refinement which is having its arguments processed.  Or it may
    // point to another *read-only* value whose content signals information
    // about how arguments should be handled.  The specific address of the
    // value can be used to test without typing, but then can also be
    // checked with conditional truth and falsehood.
    //
    // See notes on SKIPPING_REFINEMENT_ARGS, etc. for details.
    //
    // In path processing, ->refine points to the soft-quoted product of the
    // current path item (the "picker").  So on the second step of processing
    // foo/(1 + 2) it would be 3.
    //
    Value* refine;

  union {
    // References are used by path dispatch.
    //
    struct {
        Cell* cell;
        Specifier* specifier;
    } ref;

    // Used to slip cell to re-evaluate into Eval_Core_Throws()
    //
    struct {
        const Value* value;
    } reval;
  } u;

   #if DEBUG_COUNT_TICKS
    //
    // `tick`
    //
    // The expression evaluation "tick" where the Level is starting its
    // processing.  This is helpful for setting breakpoints on certain ticks
    // in reproducible situations.
    //
    uintptr_t tick; // !!! Should this be in release builds, exposed to users?
  #endif

  #if DEBUG_FRAME_LABELS
    //
    // `label`
    //
    // Knowing the label symbol is not as handy as knowing the actual string
    // of the function this call represents (if any).  It is in UTF8 format,
    // and cast to `char*` to help debuggers that have trouble with Byte.
    //
    const char *label_utf8;
  #endif

  #if RUNTIME_CHECKS
    //
    // `file`
    //
    // An emerging feature in the system is the ability to connect user-seen
    // series to a file and line number associated with their creation,
    // either their source code or some trace back to the code that generated
    // them.  As the feature gets better, it will certainly be useful to be
    // able to quickly see the information in the debugger for L->source.
    //
    Ucs2Unit* file_ucs2;  // is wide char, unfortunately, in this old branch
    int line;
  #endif

  #if DEBUG_BALANCE_STATE
    //
    // `state`
    //
    // Debug reuses PUSH_TRAP's snapshotting to check for leaks at each stack
    // level.  It can also be made to use a more aggresive leak check at every
    // evaluator step--see BALANCE_CHECK_EVERY_EVALUATION_STEP.
    //
    struct Reb_State state;
  #endif

  #if DEBUG_EXPIRED_LOOKBACK
    //
    // On each call to Fetch_Next_In_Level, it's possible to ask it to give
    // a pointer to a cell with equivalent data to what was previously in
    // L->value, but that might not be L->value.  So for all practical
    // purposes, one is to assume that the L->value pointer died after the
    // fetch.  If clients are interested in doing "lookback" and examining
    // two values at the same time (or doing a GC and expecting to still
    // have the old f->current work), then they must not use the old L->value
    // but request the lookback pointer from Fetch_Next_In_Level().
    //
    // To help stress this invariant, frames will forcibly expire value
    // cells, handing out disposable lookback pointers on each eval.
    //
    // !!! Test currently leaks on shutdown, review how to not leak.
    //
    Cell* stress;
  #endif
};


// It is more pleasant to have a uniform way of speaking of frames by pointer,
// so this macro sets that up for you, the same way DECLARE_VALUE does.  The
// optimizer should eliminate the extra pointer.
//
// Just to simplify matters, the frame spare is set to a bit pattern the GC
// will accept.  It would need stack preparation anyway, and this simplifies
// the invariant so if a recycle happens before Eval_Core_Throws() gets to its
// body, it's always set to something.  Using an unreadable blank means we
// signal to users of the frame that they can't be assured of any particular
// value between evaluations; it's not cleared.
//

#define DECLARE_LEVEL_CORE(name, source_ptr) \
    Level name##_struct; \
    name##_struct.source = (source_ptr); \
    Level* const name = &name##_struct; \
    Erase_Cell(&name->spare); \
    Init_Unreadable(&name->spare); \
    name->stack_base = TOP_INDEX;

#define DECLARE_LEVEL(name) \
    struct Reb_Level_Source name##source; \
    DECLARE_LEVEL_CORE(name, &name##source)

#define DECLARE_END_LEVEL(name) \
    DECLARE_LEVEL_CORE(name, &TG_Level_Source_End)

#define DECLARE_SUBLEVEL(name, parent) \
    DECLARE_LEVEL_CORE(name, (parent)->source)


#define TOP_LEVEL (TG_Top_Level + 0)  // avoid assign to via + 0
#define BOTTOM_LEVEL (TG_Bottom_Level + 0)  // avoid assign via + 0


//=////////////////////////////////////////////////////////////////////////=//
//
// SPECIAL VALUE MODES FOR (Level*)->REFINE
//
//=////////////////////////////////////////////////////////////////////////=//
//
// L->refine is a bit tricky.  If it Is_Logic() and TRUE, then this means that
// a refinement is active but revokable, having its arguments gathered.  So
// it actually points to the L->arg of the active refinement slot.  If
// evaluation of an argument in this state produces no value, the refinement
// must be revoked, and its value mutated to be FALSE.
//
// But all the other values that L->refine can hold are read-only pointers
// that signal something about the argument gathering state:
//
// * If nullptr, then refinements are being skipped, and the following
//   arguments should not be written to.
//
// * If FALSE_VALUE, this is an arg to a refinement that was not used in
//   the invocation.  No consumption should be performed, arguments should
//   be written as unset, and any non-unset specializations of arguments
//   should trigger an error.
//
// * If NULLED_CELL, this is an arg to a refinement that was used in the
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
// like `L->refine == BLANK_VALUE` are faster than `Is_Blank(L->refine)`.
//
// !!! ^-- While that's presumably true, it would be worth testing if a
// dereference of the single byte via Type_Of() is ever faster.
//

#define SKIPPING_REFINEMENT_ARGS \
    nullptr // 0 pointer comparison generally faster than to arbitrary pointer

#define ARG_TO_UNUSED_REFINEMENT \
    m_cast(Value*, BLANK_VALUE)

#define ARG_TO_IRREVOCABLE_REFINEMENT \
    m_cast(Value*, OKAY_VALUE)

#define ARG_TO_REVOKED_REFINEMENT \
    m_cast(Value*, NULLED_CELL)

#define ORDINARY_ARG \
    m_cast(Value*, EMPTY_BLOCK)

#define LOOKBACK_ARG \
    m_cast(Value*, EMPTY_TEXT)


#if NO_DEBUG_CHECK_CASTS

    #define LVL(p) \
        cast(Level*, (p))  // LVL() just does a cast (maybe with added checks)

#else

    template <class T>
    inline Level* LVL(T *p) {
        constexpr bool base = std::is_same<T, void>::value
            or std::is_same<T, Node>::value;

        static_assert(base, "LVL() works on void/Node");

        if (base)
            assert(
                (NODE_BYTE(p) & (
                    NODE_BYTEMASK_0x80_NODE | NODE_BYTEMASK_0x08_CELL
                )) == (
                    NODE_BYTEMASK_0x80_NODE | NODE_BYTEMASK_0x08_CELL
                )
            );

        return reinterpret_cast<Level*>(p);
    }

#endif
