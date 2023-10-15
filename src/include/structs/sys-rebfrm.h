//
//  File: %sys-rebfrm.h
//  Summary: {Reb_Frame Structure Definition}
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


// !!! A Frame(*) answers that it is a node, and a cell.  This is questionable
// and should be reviewed now that many features no longer depend on it.

#define FRAME_FLAG_0_IS_TRUE FLAG_LEFT_BIT(0) // IS a node
STATIC_ASSERT(FRAME_FLAG_0_IS_TRUE == NODE_FLAG_NODE);

#define FRAME_FLAG_1_IS_FALSE FLAG_LEFT_BIT(1) // is NOT free
STATIC_ASSERT(FRAME_FLAG_1_IS_FALSE == NODE_FLAG_STALE);


//=//// FRAME_FLAG_ALLOCATED_FEED //////////////////////////////////////////=//
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
// !!! Note that this is NODE_FLAG_MANAGED.  Right now, the concept of
// "managed" vs. "unmanaged" doesn't completely apply to frames--they are all
// basically managed, but references to them in values are done through a
// level of indirection (a varlist) which will be patched up to not point
// to them if they are freed.  So this bit is used for another purpose.
//
#define FRAME_FLAG_ALLOCATED_FEED \
    FLAG_LEFT_BIT(2)

STATIC_ASSERT(FRAME_FLAG_ALLOCATED_FEED == NODE_FLAG_MANAGED);  // should be ok

//=//// FRAME_FLAG_BRANCH ///////////////////////////////////////////////////=//
//
// If something is a branch and it is evaluating, then it cannot result in
// either a pure NULL or a void result.  So nulls must be turned into null
// isotopes and voids are turned into none (~) isotopes.
//
// This is done as a general service of the Trampoline...because if it did
// not, this would require a separate continuation callback to do it.  So
// routines like IF would not be able to just delegate to another frame.
//
#define FRAME_FLAG_BRANCH \
    FLAG_LEFT_BIT(3)


//=//// FRAME_FLAG_META_RESULT ////////////////////////////////////////////=//
//
// When this is applied, the Trampoline is asked to return an evaluator result
// in its ^META form.  Doing so saves on needing separate callback entry
// points for things like meta-vs-non-meta arguments, and is a useful
// general facility.
//
#define FRAME_FLAG_META_RESULT \
    FLAG_LEFT_BIT(4)


//=//// FRAME_FLAG_5 ///////////////////////////////////////////////////////=//
//
#define FRAME_FLAG_5 \
    FLAG_LEFT_BIT(5)


//=//// FRAME_FLAG_TRAMPOLINE_KEEPALIVE ////////////////////////////////////=//
//
// This flag asks the trampoline function to not call Drop_Frame() when it
// sees that the frame's `executor` has reached the `nullptr` state.  Instead
// it stays on the frame stack, and control is passed to the previous frame's
// executor (which will then be receiving its frame pointer parameter that
// will not be the current top of stack).
//
// It's a feature used by routines which want to make several successive
// requests on a frame (REDUCE, ANY, CASE, etc.) without tearing down the
// frame and putting it back together again.
//
#define FRAME_FLAG_TRAMPOLINE_KEEPALIVE \
    FLAG_LEFT_BIT(6)


// !!! Historically frames have identified as being "cells" even though they
// are not, in order to use that flag as a distinction when in bindings
// from the non-cell choices like contexts and paramlists.  This may not be
// the best way to flag frames; alternatives are in consideration.
//
#define FRAME_FLAG_7_IS_TRUE FLAG_LEFT_BIT(7)
STATIC_ASSERT(FRAME_FLAG_7_IS_TRUE == NODE_FLAG_CELL);


//=//// FLAGS 8-15 ARE USED FOR THE "STATE" byte ///////////////////////////=//
//
// One byte's worth is used to encode a "frame state" that can be used by
// natives or dispatchers, e.g. to encode which step they are on.
//
// By default, when a frame is initialized its state byte will be 0.  This
// lets the executing code know that it's getting control for the first time.

#define FLAG_STATE_BYTE(state) \
    FLAG_SECOND_BYTE(state)

#define STATE_0 0  // use macro vs. just hardcoding 0 around the system

#undef FRAME_FLAG_8
#undef FRAME_FLAG_9
#undef FRAME_FLAG_10
#undef FRAME_FLAG_11
#undef FRAME_FLAG_12
#undef FRAME_FLAG_13
#undef FRAME_FLAG_14
#undef FRAME_FLAG_15


//=//// FRAME_FLAG_FAILURE_RESULT_OK ///////////////////////////////////////=//
//
// The special ISOTOPE_0 quotelevel will trip up code that isn't expecting
// it, so most frames do not want to receive these "isotopic forms of error!"
// This flag can be used with FRAME_FLAG_META_RESULT or without it, to say
// that the caller is planning on dealing with the special case.
//
// Note: This bit is the same as CELL_FLAG_NOTE, which may be something that
// could be exploited for some optimization.
//
#define FRAME_FLAG_FAILURE_RESULT_OK \
    FLAG_LEFT_BIT(16)


//=//// FRAME_FLAG_17 //////////////////////////////////////////////////////=//
//
#define FRAME_FLAG_17 \
    FLAG_LEFT_BIT(17)


//=//// FRAME_FLAG_ABRUPT_FAILURE ///////////////////////////////////////////=//
//
// !!! This is a current guess for how to handle the case of re-entering an
// executor when it fail()s abruptly.  We don't want to steal a STATE byte
// for this in case the status of that state byte is important for cleanup.
//
#define FRAME_FLAG_ABRUPT_FAILURE \
    FLAG_LEFT_BIT(18)


//=//// FRAME_FLAG_NOTIFY_ON_ABRUPT_FAILURE ////////////////////////////////=//
//
// Most frames don't want to be told about the errors that they themselves...
// and if they have cleanup to do, they could do that cleanup before calling
// the fail().  However, some code calls nested C stacks which use fail() and
// it's hard to hook all the cases.  So this flag can be used to tell the
// trampoline to give a callback even if the frame itself caused the problem.
//
// To help avoid misunderstandings, trying to read the STATE byte when in the
// abrupt failure case causes an assert() in the C++ build.
//
#define FRAME_FLAG_NOTIFY_ON_ABRUPT_FAILURE \
    FLAG_LEFT_BIT(19)


//=//// FRAME_FLAG_BLAME_PARENT ////////////////////////////////////////////=//
//
// Marks an error to hint that a frame is internal, and that reporting an
// error on it probably won't give a good report.
//
#define FRAME_FLAG_BLAME_PARENT \
    FLAG_LEFT_BIT(20)


//=//// FRAME_FLAG_ROOT_FRAME //////////////////////////////////////////////=//
//
// This frame is the root of a trampoline stack, and hence it cannot be jumped
// past by something like a YIELD, return, or other throw.  This would mean
// crossing C stack levels that the interpreter does not control (e.g. some
// code that called into Rebol as a library.)
//
#define FRAME_FLAG_ROOT_FRAME \
    FLAG_LEFT_BIT(21)


//=//// FRAME_FLAG_22 /////////////////////////////////////////////////////=//
//
#define FRAME_FLAG_22 \
    FLAG_LEFT_BIT(22)


//=//// FRAME_FLAG_23 //////////////////////////////////////////////////////=//
//
#define FRAME_FLAG_23 \
    FLAG_LEFT_BIT(23)


//=//// BITS 24-31: EXECUTOR FLAGS ////////////////////////////////////////=//
//
// These flags are those that differ based on which executor is in use.
//
// Use the Get_Executor_Flag()/Set_Executor_Flag()/Clear_Executor_Flag()
// functions to access these.
//

#define FRAME_FLAG_24    FLAG_LEFT_BIT(24)
#define FRAME_FLAG_25    FLAG_LEFT_BIT(25)
#define FRAME_FLAG_26    FLAG_LEFT_BIT(26)
#define FRAME_FLAG_27    FLAG_LEFT_BIT(27)
#define FRAME_FLAG_28    FLAG_LEFT_BIT(28)
STATIC_ASSERT(FRAME_FLAG_28 == CELL_FLAG_NOTE);  // useful for optimization?
#define FRAME_FLAG_29    FLAG_LEFT_BIT(29)
#define FRAME_FLAG_30    FLAG_LEFT_BIT(30)
#define FRAME_FLAG_31    FLAG_LEFT_BIT(31)

STATIC_ASSERT(31 < 32);  // otherwise FRAME_FLAG_XXX too high


// Note: It was considered to force clients to include a FRAME_MASK_DEFAULT
// when OR'ing together flags, to allow certain flag states to be favored
// as truthy for the "unused" state, in case that helped some efficiency
// trick.  This made the callsites much more noisy, so FRAME_MASK_NONE is used
// solely to help call out places that don't have other flags.
//
#define FRAME_MASK_NONE \
    FLAG_STATE_BYTE(STATE_0)  // note that the 0 state is implicit most places


#define Set_Frame_Flag(f,name) \
    (FRM(f)->flags.bits |= FRAME_FLAG_##name)

#define Get_Frame_Flag(f,name) \
    ((FRM(f)->flags.bits & FRAME_FLAG_##name) != 0)

#define Clear_Frame_Flag(f,name) \
    (FRM(f)->flags.bits &= ~FRAME_FLAG_##name)

#define Not_Frame_Flag(f,name) \
    ((FRM(f)->flags.bits & FRAME_FLAG_##name) == 0)


// !!! It was thought that a standard layout struct with just {REBVAL *p} in
// it would be compatible as a return result with plain REBVAL *p.  That does
// not seem to be the case...because when an extension-defined dispatcher is
// defined to return REBVAL*, it is incompatible with callers expecting a
// Bounce struct as defined below.
//
// It would be nice to have the added typechecking on the Bounce types; this
// would prevent states like BOUNCE_THROWN from accidentally being passed
// somewhere that took REBVAL* only.  But not so important to hold up the idea
// of extensions that only speak in REBVAL*.  Review when there's time.
//
#if 1  /* CPLUSPLUS_11 == 0 || defined(NDEBUG) */
    typedef REBVAL* Bounce;
#else
    struct Bounce {
        REBVAL *p;
        Bounce () {}
        Bounce (REBVAL *v) : p (v) {}
        bool operator==(REBVAL *v) { return v == p; }
        bool operator!=(REBVAL *v) { return v != p; }

        bool operator==(const Bounce& other) { return other.p == p; }
        bool operator!=(const Bounce& other) { return other.p != p; }

        explicit operator REBVAL* () { return p; }
        explicit operator void* () { return p; }
    };
#endif


// These definitions are needed in %sys-rebval.h, and can't be put in
// %sys-rebact.h because that depends on Raw_Array, which depends on
// Raw_Series, which depends on values... :-/

// C function implementing a native ACTION!
//
typedef Bounce (Executor)(Frame(*) frame_);
typedef Executor Dispatcher;  // sub-dispatched in Action_Executor()

// Intrinsics are a special form of implementing natives that do not need
// to instantiate a frame.  See Intrinsic_Dispatcher().
//
typedef void (Intrinsic)(Value(*) out, Value(*) arg);

// This is for working around pedantic C and C++ errors, when an extension
// that doesn't use %sys-core.h tries to redefine dispatcher in terms of
// taking a void* and returning a REBVAL*.
//
#ifdef __cplusplus
    #define dispatcher_cast(ptr) \
        cast(Dispatcher*, cast(void*, (ptr)))
#else
    #define dispatcher_cast(ptr) \
        cast(Dispatcher*, (ptr))
#endif

#include "executors/exec-eval.h"
#include "executors/exec-action.h"
#include "executors/exec-scan.h"


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
    struct Reb_Frame : public Raw_Node
#else
    struct Reb_Frame
#endif
{
    // These are FRAME_FLAG_XXX or'd together--see their documentation above.
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
    // on Reify_Variadic_Feed_As_Array_Feed().)
    //
    // Since frames may share source information, this needs to be done with
    // a dereference.
    //
    Feed(*) feed;

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
    Reb_Cell spare;

    // Each executor subclass can store specialized information in the frame.
    // We place it here up top where we've been careful to make sure the
    // `spare` is on a (2 * sizeof(uintptr_t)) alignment, in case there are
    // things in the state that also require alignment (e.g. the eval state
    // uses its space for an extra "scratch" GC-safe cell)
    //
  union {
    struct Reb_Eval_Executor_State eval;

    struct Reb_Action_Executor_State action;

    struct {
        Frame(*) main_frame;
        bool changed;
    } compose;

    struct rebol_scan_level scan;
  } u;

    // !!! The "executor" is an experimental new concept in the frame world,
    // for who runs the continuation.  This was controlled with flags before,
    // but the concept is that it be controlled with functions matching the
    // signature of natives and dispatchers.
    //
    Executor* executor;

    // The prior call frame.  This never needs to be checked against nullptr,
    // because the bottom of the stack is BOTTOM_FRAME which is allocated at
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

    // Functions don't have "names", though they can be assigned to words.
    // However, not all function invocations are through words or paths, so
    // the label may not be known.  Mechanics with labeling try to make sure
    // that *some* name is known, but a few cases can't be, e.g.:
    //
    //     run func [x] [print "This function never got a label"]
    //
    // The evaluator only enforces that the symbol be set during function
    // calls--in the release build, it is allowed to be garbage otherwise.
    //
    option(Symbol(const*)) label;

    // The varlist is where arguments for the frame are kept.  Though it is
    // ultimately usable as an ordinary CTX_VARLIST() for a FRAME! value, it
    // is different because it is built progressively, with random bits in
    // its pending capacity that are specifically accounted for by the GC...
    // which limits its marking up to the progress point of `key`.
    //
    // It starts out unmanaged, so that if no usages by the user specifically
    // ask for a FRAME! value, and the Context(*) isn't needed to store in a
    // Derelativize()'d or Move_Velue()'d value as a binding, it can be
    // reused or freed.  See Push_Action() and Drop_Action() for the logic.
    //
    Array(*) varlist;
    REBVAL *rootvar; // cache of CTX_ARCHETYPE(varlist) if varlist is not null

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
    // the ends is not nullptr, but a pointer to the Frame(*) itself (which
    // can be noticed via NODE_FLAG_FRAME as not being an API handle).
    //
    Node* alloc_value_list;

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
    // and cast to `char*` to help debuggers that have trouble with Byte.
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
    const char *file; // is Byte (UTF-8), but char* for debug watch
    int line;
  #endif
};

// These are needed protoyped by the array code because it wants to put file
// and line numbers into arrays based on the frame in effect at their time
// of allocation.

inline static Array(const*) FRM_ARRAY(Frame(*) f);
inline static bool FRM_IS_VARIADIC(Frame(*) f);


#define TOP_FRAME (TG_Top_Frame + 0) // avoid assign to TOP_FRAME via + 0
#define BOTTOM_FRAME (TG_Bottom_Frame + 0) // avoid assign to BOTTOM_FRAME via + 0


#if defined(NDEBUG)
    #define ensure_executor(executor,f) (f)  // no-op in release build
#else
    inline static Frame(*) ensure_executor(Executor *executor, Frame(*) f) {
        if (f->executor != executor)
            assert(!"Wrong executor for flag tested");
        return f;
    }
#endif


#define Get_Executor_Flag(executor,f,name) \
    ((ensure_executor(EXECUTOR_##executor, (f))->flags.bits \
        & executor##_EXECUTOR_FLAG_##name) != 0)

#define Not_Executor_Flag(executor,f,name) \
    ((ensure_executor(EXECUTOR_##executor, (f))->flags.bits \
        & executor##_EXECUTOR_FLAG_##name) == 0)

#define Set_Executor_Flag(executor,f,name) \
    (ensure_executor(EXECUTOR_##executor, (f))->flags.bits \
        |= executor##_EXECUTOR_FLAG_##name)

#define Clear_Executor_Flag(executor,f,name) \
    (ensure_executor(EXECUTOR_##executor, (f))->flags.bits \
        &= ~executor##_EXECUTOR_FLAG_##name)
