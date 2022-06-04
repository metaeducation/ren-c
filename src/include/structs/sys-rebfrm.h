//
//  File: %sys-rebfrm.h
//  Summary: {REBFRM Structure Frame Definition}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This declares the structure used by frames, for use in other structs.
// See %sys-frame.h for a higher-level description.
//


// !!! A REBFRM* answers that it is a node, and a cell.  This is questionable
// and should be reviewed now that many features no longer depend on it.

#define EVAL_FLAG_0_IS_TRUE FLAG_LEFT_BIT(0) // IS a node
STATIC_ASSERT(EVAL_FLAG_0_IS_TRUE == NODE_FLAG_NODE);

#define EVAL_FLAG_1_IS_FALSE FLAG_LEFT_BIT(1) // is NOT free
STATIC_ASSERT(EVAL_FLAG_1_IS_FALSE == NODE_FLAG_STALE);


//=//// EVAL_FLAG_ALLOCATED_FEED //////////////////////////////////////////=//
//
// Some frame recursions re-use a feed that already existed, while others will
// allocate them.  This re-use allows recursions to keep index positions and
// fetched "gotten" values in sync.  The dynamic allocation means that feeds
// can be kept alive across contiuations--which wouldn't be possible if they
// were on the C stack.
//
// If a frame allocated a feed, then it has to be freed...which is done when
// the frame is dropped or aborted.
//
#define EVAL_FLAG_ALLOCATED_FEED \
    FLAG_LEFT_BIT(2)


//=//// EVAL_FLAG_3 ///////////////////////////////////////////////////////=//
//
#define EVAL_FLAG_3 \
    FLAG_LEFT_BIT(3)


//=//// EVAL_FLAG_CACHE_NO_LOOKAHEAD //////////////////////////////////////=//
//
// Without intervention, running an invisible will consume the state of the
// FEED_FLAG_NO_LOOKAHEAD.  That creates a problem for things like:
//
//     >> 1 + comment "a" comment "b" 2 * 3
//     == 7  ; you'll get 7 and not 9 if FEED_FLAG_NO_LOOKAHEAD is erased
//
// Originally invisible functions were pre-announced as purely invisible, and
// would un-set the flag while the invisible ran...then restore it to the
// previous state.  But this has changed to were it's not known until after
// a function has executed if it was invisible.
//
// The current logic is to cache the *feed* flag in this *frame* flag before
// each function runs, and then restore it in the event the execution turns
// out to be invisible.
//
// Note: This is the same flag value as FEED_FLAG_NO_LOOKAHEAD.
//
// !!! This could lead to "multiplying" the influences of the flag across
// several invisible evaluations; this should be reviewed to see if it makes
// any actual problems in practice.
//
#define EVAL_FLAG_CACHE_NO_LOOKAHEAD \
    FLAG_LEFT_BIT(4)

STATIC_ASSERT(EVAL_FLAG_CACHE_NO_LOOKAHEAD == FEED_FLAG_NO_LOOKAHEAD);


//=//// EVAL_FLAG_NO_EVALUATIONS //////////////////////////////////////////=//
//
// It might seem strange to have an evaluator mode in which no evaluations are
// performed.  However, this simplifies the implementation of operators such
// as ANY and ALL, which wish to run in an "inert" mode:
//
//     >> any [1 + 2]
//     == 3
//
//     >> any @[1 + 2]
//     == 1
//
// Inert operations wind up costing a bit more because they're pushing a frame
// when it seems "they don't need to"; but pushing a frame also locks the
// series in question against enumeration.
//
#define EVAL_FLAG_NO_EVALUATIONS \
    FLAG_LEFT_BIT(5)


//=//// EVAL_FLAG_6 ///////////////////////////////////////////////////////=//
//
#define EVAL_FLAG_6 \
    FLAG_LEFT_BIT(6)


// !!! Historically frames have identified as being "cells" even though they
// are not, in order to use that flag as a distinction when in bindings
// from the non-cell choices like contexts and paramlists.  This may not be
// the best way to flag frames; alternatives are in consideration.
//
#define EVAL_FLAG_7_IS_TRUE FLAG_LEFT_BIT(7)
STATIC_ASSERT(EVAL_FLAG_7_IS_TRUE == NODE_FLAG_CELL);


//=//// FLAGS 8-15 ARE USED FOR THE STATE_BYTE() //////////////////////////=//
//
// One byte's worth is used to encode a "frame state" that can be used by
// natives or dispatchers, e.g. to encode which step they are on.
//

#undef EVAL_FLAG_8
#undef EVAL_FLAG_9
#undef EVAL_FLAG_10
#undef EVAL_FLAG_11
#undef EVAL_FLAG_12
#undef EVAL_FLAG_13
#undef EVAL_FLAG_14
#undef EVAL_FLAG_15


//=//// EVAL_FLAG_RUNNING_ENFIX ///////////////////////////////////////////=//
//
// Due to the unusual influences of partial refinement specialization, a frame
// may wind up with its enfix parameter as being something like the last cell
// in the argument list...when it has to then go back and fill earlier args
// as normal.  There's no good place to hold the memory that one is doing an
// enfix fulfillment besides a bit on the frame itself.
//
// It is also used to indicate to a ST_EVALUATOR_REEVALUATING frame whether
// to run an ACTION! cell as enfix or not.  The reason this may be overridden
// on what's in the action can be seen in the REBNATIVE(shove) code.
//
#define EVAL_FLAG_RUNNING_ENFIX \
    FLAG_LEFT_BIT(16)


//=//// EVAL_FLAG_DIDNT_LEFT_QUOTE_PATH ///////////////////////////////////=//
//
// There is a contention between operators that want to quote their left hand
// side and ones that want to quote their right hand side.  The left hand side
// wins in order for things like `help default` to work.  But deciding on
// whether the left hand side should win or not if it's a PATH! is a tricky
// case, as one must evaluate the path to know if it winds up producing a
// right quoting action or not.
//
// So paths win automatically unless a special (rare) override is used.  But
// if that path doesn't end up being a right quoting operator, it's less
// confusing to give an error message informing the user to use -> vs. just
// make it appear there was no left hand side.
//
#define EVAL_FLAG_DIDNT_LEFT_QUOTE_PATH \
    FLAG_LEFT_BIT(17)


//=//// EVAL_FLAG_18 ///////////////////////////////////////////////////////=//
//
#define EVAL_FLAG_18 \
    FLAG_LEFT_BIT(18)


//=//// EVAL_FLAG_ABRUPT_FAILURE ///////////////////////////////////////////=//
//
// !!! This is a current guess for how to handle the case of re-entering an
// executor when it fail()s abruptly.  We don't want to steal a STATE_BYTE
// for this in case the status of that state byte is important for cleanup.
//
#define EVAL_FLAG_ABRUPT_FAILURE \
    FLAG_LEFT_BIT(19)


//=//// EVAL_FLAG_TYPECHECK_ONLY //////////////////////////////////////////=//
//
// This is used by <blank> to indicate that once the frame is fulfilled, the
// only thing that should be done is typechecking...don't run the action.
//
#define EVAL_FLAG_TYPECHECK_ONLY \
    FLAG_LEFT_BIT(20)


//=//// EVAL_FLAG_21 //////////////////////////////////////////////////////=//
//
#define EVAL_FLAG_21 \
    FLAG_LEFT_BIT(21)


//=//// EVAL_FLAG_INERT_OPTIMIZATION //////////////////////////////////////=//
//
// If ST_EVALUATOR_LOOKING_AHEAD is being used due to an inert optimization,
// this flag is set, so that the quoting machinery can realize the lookback
// quote is not actually too late.
//
#define EVAL_FLAG_INERT_OPTIMIZATION \
    FLAG_LEFT_BIT(22)


//=//// EVAL_FLAG_ERROR_ON_DEFERRED_ENFIX /////////////////////////////////=//
//
// There are advanced features that "abuse" the evaluator, e.g. by making it
// create a specialization exemplar by example from a stream of code.  These
// cases are designed to operate in isolation, and are incompatible with the
// idea of enfix operations that stay pending in the evaluation queue, e.g.
//
//     match+ parse "aab" [some "a"] else [print "what should this do?"]
//
// MATCH+ is variadic, and in one step asks to make a frame from the right
// hand side.  But it's 99% likely intent of this was to attach the ELSE to
// the MATCH and not the PARSE.  That looks inconsistent, since the user
// imagines it's the evaluator running PARSE as a parameter to MATCH (vs.
// MATCH becoming the evaluator and running it).
//
// It would be technically possible to allow ELSE to bind to the MATCH in
// this case.  It might even be technically possible to give MATCH back a
// frame for a CHAIN of actions that starts with PARSE but includes the ELSE
// (which sounds interesting but crazy, considering that's not what people
// would want here, but maybe sometimes they would).
//
// The best answer for right now is just to raise an error.
//
#define EVAL_FLAG_ERROR_ON_DEFERRED_ENFIX \
    FLAG_LEFT_BIT(23)


//=//// EVAL_FLAG_24 //////////////////////////////////////////////////////=//
//
#define EVAL_FLAG_24 \
    FLAG_LEFT_BIT(24)


//=//// EVAL_FLAG_FULFILLING_ARG //////////////////////////////////////////=//
//
// Deferred lookback operations need to know when they are dealing with an
// argument fulfillment for a function, e.g. `summation 1 2 3 |> 100` should
// be `(summation 1 2 3) |> 100` and not `summation 1 2 (3 |> 100)`.  This
// also means that `add 1 <| 2` will act as an error.
//
#define EVAL_FLAG_FULFILLING_ARG \
    FLAG_LEFT_BIT(25)

STATIC_ASSERT(DETAILS_FLAG_IS_BARRIER == EVAL_FLAG_FULFILLING_ARG);


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
// important not only for Eval_Core() to know, but also the GC...since it
// means it must protect *all* of the arguments--not just up thru f->key.
//
#define EVAL_FLAG_DOING_PICKUPS \
    FLAG_LEFT_BIT(27)


//=//// EVAL_FLAG_28 //////////////////////////////////////////////////////=//
//
// Note: This bit is the same as CELL_FLAG_NOTE, which may be something that
// could be exploited for some optimization.
//
#define EVAL_FLAG_28 \
    FLAG_LEFT_BIT(28)

STATIC_ASSERT(EVAL_FLAG_28 == CELL_FLAG_NOTE);


//=//// EVAL_FLAG_BLAME_PARENT ////////////////////////////////////////////=//
//
// Marks an error to hint that a frame is internal, and that reporting an
// error on it probably won't give a good report.
//
// !!! Currently unused.  Was this an important idea?
//
#define EVAL_FLAG_BLAME_PARENT \
    FLAG_LEFT_BIT(29)


//=//// EVAL_FLAG_FULFILL_ONLY ////////////////////////////////////////////=//
//
// In some scenarios, the desire is to fill up the frame but not actually run
// an action.  At one point this was done with a special "dummy" action to
// dodge having to check the flag on every dispatch.  But in the scheme of
// things, checking the flag is negligible...and it's better to do it with
// a flag so that one does not lose the paramlist information one was working
// with (overwriting with a dummy action on FRM_PHASE() led to an inconsistent
// case that had to be accounted for, since the dummy's arguments did not
// line up with the frame being filled).
//
#define EVAL_FLAG_FULFILL_ONLY \
    FLAG_LEFT_BIT(30)


//=//// EVAL_FLAG_OVERLAP_OUTPUT //////////////////////////////////////////=//
//
// This is a flag that is passed to the evaluation to indicate that it should
// not assert if it finds a value in the ouput cell already.  It is only
// paid attention to in the debug build.
//
#define EVAL_FLAG_OVERLAP_OUTPUT \
    FLAG_LEFT_BIT(31)


STATIC_ASSERT(31 < 32);  // otherwise EVAL_FLAG_XXX too high


// All frames must include EVAL_MASK_DEFAULT in their flags.  This is not
// done automatically for two reasons: one is to make the calls more clear
// with `DECLARE_END_FRAME (f, EVAL_MASK_DEFAULT)` vs just saying 0.  Also,
// it would permit there to be negative-default flags if some efficiency
// trick favored the flag being truthy for its "unused" state, where you'd
// say `DECLARE_END_FRAME (f, EVAL_MASK_DEFAULT & ~EVAL_FLAG_SOME_SETTING)`.
//
#define EVAL_MASK_DEFAULT \
    (EVAL_FLAG_0_IS_TRUE | EVAL_FLAG_7_IS_TRUE)


#define SET_EVAL_FLAG(f,name) \
    (FRM(f)->flags.bits |= EVAL_FLAG_##name)

#define GET_EVAL_FLAG(f,name) \
    ((FRM(f)->flags.bits & EVAL_FLAG_##name) != 0)

#define CLEAR_EVAL_FLAG(f,name) \
    (FRM(f)->flags.bits &= ~EVAL_FLAG_##name)

#define NOT_EVAL_FLAG(f,name) \
    ((FRM(f)->flags.bits & EVAL_FLAG_##name) == 0)



// NOTE: The ordering of the fields in `Reb_Frame` are specifically done so
// as to accomplish correct 64-bit alignment of pointers on 64-bit systems.
//
// Because performance in the core evaluator loop is system-critical, this
// uses full platform `int`s instead of REBLENs.
//
// If modifying the structure, be sensitive to this issue--and that the
// layout of this structure is mirrored in Ren-Cpp.
//

#if CPLUSPLUS_11
    struct Reb_Frame : public Reb_Node
#else
    struct Reb_Frame
#endif
{
    //
    // These are EVAL_FLAG_XXX or'd together--see their documentation above.
    // A Reb_Header is used so that it can implicitly terminate `cell`, if
    // that comes in useful (e.g. there's an apparent END after cell)
    //
    // Note: In order to use the memory pools, this must be in first position,
    // and it must not have the NODE_FLAG_STALE bit set when in use.
    //
    union Reb_Header flags;

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
    REBFED *feed;

    // The frame's "spare" is used for different purposes.  PARSE uses it as a
    // scratch storage space.  Path evaluation uses it as where the calculated
    // "picker" goes (so if `foo/(1 + 2)`, the 3 would be stored there to be
    // used to pick the next value in the chain).
    //
    // The evaluator uses it as a general temporary place for evaluations, but
    // it is available for use by natives while they are running.  This is
    // particularly useful because it is GC guarded and also a valid target
    // location for evaluations.  (The argument cells of a native are *not*
    // legal evaluation targets, although they can be used as GC safe scratch
    // space for things other than evaluation.)
    //
    RELVAL spare;

    // The prior call frame.  This never needs to be checked against nullptr,
    // because the bottom of the stack is FS_BOTTOM which is allocated at
    // startup and never used to run code.
    //
    struct Reb_Frame *prior;

    // This is where to write the result of the evaluation.  It should not be
    // in "movable" memory, hence not in a series data array.  Often it is
    // used as an intermediate free location to do calculations en route to
    // a final result, due to being GC-safe during function evaluation.
    //
    REBVAL *out;

    // The error reporting machinery doesn't want where `index` is right now,
    // but where it was at the beginning of a single EVALUATE step.
    //
    uintptr_t expr_index;

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

    // Functions don't have "names", though they can be assigned to words.
    // However, not all function invocations are through words or paths, so
    // the label may not be known.  Mechanics with labeling try to make sure
    // that *some* name is known, but a few cases can't be, e.g.:
    //
    //     reeval func [x] [print "This function never got a label"]
    //
    // The evaluator only enforces that the symbol be set during function
    // calls--in the release build, it is allowed to be garbage otherwise.
    //
    option(const REBSYM*) label;

    // The varlist is where arguments for the frame are kept.  Though it is
    // ultimately usable as an ordinary CTX_VARLIST() for a FRAME! value, it
    // is different because it is built progressively, with random bits in
    // its pending capacity that are specifically accounted for by the GC...
    // which limits its marking up to the progress point of `f->key`.
    //
    // It starts out unmanaged, so that if no usages by the user specifically
    // ask for a FRAME! value, and the REBCTX* isn't needed to store in a
    // Derelativize()'d or Move_Velue()'d value as a binding, it can be
    // reused or freed.  See Push_Action() and Drop_Action() for the logic.
    //
    REBARR *varlist;
    REBVAL *rootvar; // cache of CTX_ARCHETYPE(varlist) if varlist is not null

    // We use the convention that "param" refers to the TYPESET! (plus symbol)
    // from the spec of the function--a.k.a. the "formal argument".  This
    // pointer is moved in step with `arg` during argument fulfillment.
    //
    // (Note: It is const because we don't want to be changing the params,
    // but also because it is used as a temporary to store value if it is
    // advanced but we'd like to hold the old one...this makes it important
    // to protect it from GC if we have advanced beyond as well!)
    //
    const REBKEY *key;
    const REBKEY *key_tail;

    // `arg is the "actual argument"...which holds the pointer to the
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

    // `special` may be the same as `param` (if fulfilling an unspecialized
    // function) or it may be the same as `arg` (if doing a typecheck pass).
    // Otherwise it points into values of a specialization or APPLY, where
    // non-null values are being written vs. acquiring callsite parameters.
    //
    // It is assumed that special, param, and arg may all be incremented
    // together at the same time...reducing conditionality (this is why it
    // is `param` and not nullptr when processing unspecialized).
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
    const REBPAR *param;

  union {
    // Used to slip cell to re-evaluate into Eval_Core()
    //
    struct {
        const REBVAL *value;
    } reval;
  } u;

    // The "baseline" is a digest of the state of global variables at the
    // beginning of a frame evaluation.  An example of one of the things the
    // baseline captures is the data stack pointer at the start of an
    // evaluation step...which allows the evaluator to know how much state
    // it has accrued cheaply that belongs to it (such as refinements on
    // the data stack.
    //
    // It may need to be updated.  For instance: if a frame gets pushed for
    // reuse by multiple evaluations (like REDUCE, which pushes a single frame
    // for its block traversal).  Then steps which accrue state in REDUCE must
    // bump the baseline to account for any pushes it does--lest the next
    // eval step in the subframe interpret what was pushed as its own data
    // (e.g. as a refinement usage).  Anything like a YIELD which detaches a
    // frame and then may re-enter it at a new global state must refresh
    // the baseline of any global state that may have changed.
    //
    // !!! Accounting for global state baselines is a work-in-progress.  The
    // mold buffer and manuals tracking are not currently covered.  This
    // will involve review, and questions about the total performance value
    // of global buffers (the data stack is almost certainly a win, but it
    // might be worth testing).
    //
    struct Reb_State baseline;

    // While a frame is executing, any Alloc_Value() calls are linked into
    // a doubly-linked list.  This keeps them alive, and makes it quick for
    // them to be released.  In the case of an abrupt fail() call, they will
    // be automatically freed.
    //
    // In order to make a handle able to find the frame whose linked list it
    // belongs to (in order to update the head of the list) the terminator on
    // the ends is not nullptr, but a pointer to the REBFRM* itself (which
    // can be noticed via NODE_FLAG_FRAME as not being an API handle).
    //
    REBNOD *alloc_value_list;

   #if DEBUG_COUNT_TICKS
    //
    // The expression evaluation "tick" where the Reb_Frame is starting its
    // processing.  This is helpful for setting breakpoints on certain ticks
    // in reproducible situations.
    //
    uintptr_t tick; // !!! Should this be in release builds, exposed to users?
  #endif

  #if DEBUG_FRAME_LABELS
    //
    // Knowing the label symbol is not as handy as knowing the actual string
    // of the function this call represents (if any).  It is in UTF8 format,
    // and cast to `char*` to help debuggers that have trouble with REBYTE.
    //
    const char *label_utf8;
  #endif

  #if !defined(NDEBUG)
    //
    // An emerging feature in the system is the ability to connect user-seen
    // series to a file and line number associated with their creation,
    // either their source code or some trace back to the code that generated
    // them.  As the feature gets better, it will certainly be useful to be
    // able to quickly see the information in the debugger for f->feed.
    //
    const char *file; // is REBYTE (UTF-8), but char* for debug watch
    int line;
  #endif

  #if DEBUG_ENSURE_FRAME_EVALUATES
    //
    // Originally in R3-Alpha the evaluator would be skipped for empty arrays.
    // This meant that `forever []` would never get a chance to run.  Ren-C
    // always runs the evaluator--see DEBUG_ENSURE_FRAME_EVALUATES definition.
    //
    bool was_eval_called;
  #endif
};

// These are needed protoyped by the array code because it wants to put file
// and line numbers into arrays based on the frame in effect at their time
// of allocation.

inline static const REBARR *FRM_ARRAY(REBFRM *f);
inline static bool FRM_IS_VARIADIC(REBFRM *f);


#define FS_TOP (TG_Top_Frame + 0) // avoid assign to FS_TOP via + 0
#define FS_BOTTOM (TG_Bottom_Frame + 0) // avoid assign to FS_BOTTOM via + 0
