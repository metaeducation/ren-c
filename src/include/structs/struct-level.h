//
//  File: %struct-level.h
//  Summary: "Level structure definitions preceding %tmp-internals.h"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2019-2023 Ren-C Open Source Contributors
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
// This declares the Level structure used for recursions in the trampoline.
// Levels are allocated out of their own memory pool.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Due to contention with the usermode datatype FRAME!, stack levels of the
//   trampoline are called "Levels" as opposed to "Frames".  This is actually
//   a good distinction, as levels are much more broad than function frames.
//
// * Because lowercase "L" looks too much like a number 1, the shorthand for
//   level variables is uppercase L.
//

typedef struct RebolLevelStruct Level;


// !!! A Level* answers that it is a node, and a cell.  This is questionable
// and should be reviewed now that many features no longer depend on it.

#define LEVEL_FLAG_0_IS_TRUE FLAG_LEFT_BIT(0)  // IS a node
STATIC_ASSERT(LEVEL_FLAG_0_IS_TRUE == NODE_FLAG_NODE);

#define LEVEL_FLAG_1_IS_FALSE FLAG_LEFT_BIT(1)  // is NOT free
STATIC_ASSERT(LEVEL_FLAG_1_IS_FALSE == NODE_FLAG_UNREADABLE);


//=//// LEVEL_FLAG_2 //////////////////////////////////////////////////////=//
//
#define LEVEL_FLAG_2 \
    FLAG_LEFT_BIT(2)


//=//// LEVEL_FLAG_BRANCH /////////////////////////////////////////////////=//
//
// If something is a branch and it is evaluating, then it cannot result in
// either a pure null or void result.  So they have to be put in a PACK!.
//
// This is done as a general service of the Trampoline...because if it did
// not, this would require a separate continuation callback to do it.  So
// routines like IF would not be able to just delegate to another level.
//
#define LEVEL_FLAG_BRANCH \
    FLAG_LEFT_BIT(3)


//=//// LEVEL_FLAG_4_IS_TRUE //////////////////////////////////////////////=//
//
// !!! Historically levels have identified as being "cells" even though they
// are not, in order to use that flag as a distinction when in bindings
// from the non-cell choices like contexts and paramlists.  This may not be
// the best way to flag levels; alternatives are in consideration.
//
#define LEVEL_FLAG_4_IS_TRUE \
    FLAG_LEFT_BIT(4)

STATIC_ASSERT(LEVEL_FLAG_4_IS_TRUE == NODE_FLAG_CELL);


//=//// LEVEL_FLAG_5 ///////////////////////////////////////////////////////=//
//
// Temporarily ACTION_EXECUTOR_FLAG_DOING_PICKUPS because action executor
// flags are scarce.  The action executor design needs review to see if it
// can use fewer flags.
//
#define LEVEL_FLAG_5 \
    FLAG_LEFT_BIT(5)


//=//// LEVEL_FLAG_TRAMPOLINE_KEEPALIVE ////////////////////////////////////=//
//
// This flag asks the trampoline function to not call Drop_Level() when it
// sees that the level's `executor` has reached the `nullptr` state.  Instead
// it stays on the level stack, and control is passed to the previous level's
// executor (which will then be receiving its level pointer parameter that
// will not be the current top of stack).
//
// It's a feature used by routines which want to make several successive
// requests on a level (REDUCE, ANY, CASE, etc.) without tearing down the
// level and putting it back together again.
//
#define LEVEL_FLAG_TRAMPOLINE_KEEPALIVE \
    FLAG_LEFT_BIT(6)


//=//// LEVEL_FLAG_META_RESULT ////////////////////////////////////////////=//
//
// When this is applied, the Trampoline is asked to return an evaluator result
// in its ^META form.  Doing so saves on needing separate callback entry
// points for things like meta-vs-non-meta arguments, and is a useful
// general facility.
//
#define LEVEL_FLAG_META_RESULT \
    FLAG_LEFT_BIT(7)


//=//// FLAGS 8-15 ARE USED FOR THE "STATE" byte ///////////////////////////=//
//
// One byte's worth is used to encode a "level state" that can be used by
// natives or dispatchers, e.g. to encode which step they are on.
//
// By default, when a level is initialized its state byte will be 0.  This
// lets the executing code know that it's getting control for the first time.

#define FLAG_STATE_BYTE(state) \
    FLAG_SECOND_BYTE(state)

INLINE Byte State_Byte_From_Flags(Flags flags)
  { return SECOND_BYTE(&flags); }


#define STATE_0 0  // use macro vs. just hardcoding 0 around the system

#undef LEVEL_FLAG_8
#undef LEVEL_FLAG_9
#undef LEVEL_FLAG_10
#undef LEVEL_FLAG_11
#undef LEVEL_FLAG_12
#undef LEVEL_FLAG_13
#undef LEVEL_FLAG_14
#undef LEVEL_FLAG_15


//=//// LEVEL_FLAG_RAISED_RESULT_OK ///////////////////////////////////////=//
//
// The special ANTIFORM_0 quotelevel will trip up code that isn't expecting
// it, so most levels do not want to receive these "antiform forms of error!"
// This flag can be used with LEVEL_FLAG_META_RESULT or without it, to say
// that the caller is planning on dealing with the special case.
//
// Note: This bit is the same as CELL_FLAG_NOTE, which may be something that
// could be exploited for some optimization.
//
#define LEVEL_FLAG_RAISED_RESULT_OK \
    FLAG_LEFT_BIT(16)


//=//// LEVEL_FLAG_17 //////////////////////////////////////////////////////=//
//
#define LEVEL_FLAG_17 \
    FLAG_LEFT_BIT(17)


//=//// LEVEL_FLAG_DISPATCHING_INTRINSIC //////////////////////////////////=//
//
// Intrinsics can be run without creating levels for them, if they do not
// use refinements, and if you're not using a debug mode which mandates that
// levels always be created.  In this case there is no Level* to pass to the
// native, so a parent level is passed (which may be a Stepper_Executor(),
// for instance, instead of an Action_Executor())
//
// The parent's OUT can be used, but the macro for getting the argument will
// look for that argument in the SPARE cell.  If the level is being dispatched
// normally, the argument will be in the frame as usual.  A value for the
// action that is currently running will be in SCRATCH.
//
#define LEVEL_FLAG_DISPATCHING_INTRINSIC \
    FLAG_LEFT_BIT(18)


//=//// LEVEL_FLAG_19 /////////////////////////////////////////////////////=//
//
#define LEVEL_FLAG_19 \
    FLAG_LEFT_BIT(19)


//=//// LEVEL_FLAG_20 /////////////////////////////////////////////////////=//
//
#define LEVEL_FLAG_20 \
    FLAG_LEFT_BIT(20)


//=//// LEVEL_FLAG_ROOT_LEVEL /////////////////////////////////////////////=//
//
// This level is the root of a trampoline stack, and hence it cannot be jumped
// past by something like a YIELD, return, or other throw.  This would mean
// crossing C stack levels that the interpreter does not control (e.g. some
// code that called into Rebol as a library.)
//
#define LEVEL_FLAG_ROOT_LEVEL \
    FLAG_LEFT_BIT(21)


//=//// LEVEL_FLAG_UNINTERRUPTIBLE ////////////////////////////////////////=//
//
// Levels inherit the uninteruptibility flag of their parent when they are
// pushed.  You can clear it after the push if you want an interruptible
// level underneath an uninterruptible one.
//
#define LEVEL_FLAG_UNINTERRUPTIBLE \
    FLAG_LEFT_BIT(22)


//=//// LEVEL_FLAG_MISCELLANEOUS //////////////////////////////////////////=//
//
// Because ACTION_EXECUTOR_FLAG_XXX are hard to come by, this flag is given
// to natives and non-ACTION-executors for miscellaneous purposes.
//
#define LEVEL_FLAG_MISCELLANEOUS \
    FLAG_LEFT_BIT(23)


//=//// BITS 24-31: EXECUTOR FLAGS ////////////////////////////////////////=//
//
// These flags are those that differ based on which executor is in use.
//
// See notes on ensure_executor() for why the generic routines for
// Get_Executor_Flag()/Set_Executor_Flag()/Clear_Executor_Flag() were axed
// in favor of putting executor-specific defines at the top of each file,
// like Get_Action_Executor_Flag() / Get_Eval_Executor_Flag() etc.
//

#define LEVEL_FLAG_24    FLAG_LEFT_BIT(24)
#define LEVEL_FLAG_25    FLAG_LEFT_BIT(25)
#define LEVEL_FLAG_26    FLAG_LEFT_BIT(26)
#define LEVEL_FLAG_27    FLAG_LEFT_BIT(27)
#define LEVEL_FLAG_28    FLAG_LEFT_BIT(28)
STATIC_ASSERT(LEVEL_FLAG_28 == CELL_FLAG_NOTE);  // useful for optimization?
#define LEVEL_FLAG_29    FLAG_LEFT_BIT(29)
#define LEVEL_FLAG_30    FLAG_LEFT_BIT(30)
#define LEVEL_FLAG_31    FLAG_LEFT_BIT(31)

STATIC_ASSERT(31 < 32);  // otherwise LEVEL_FLAG_XXX too high


// Note: It was considered to force clients to include a LEVEL_MASK_DEFAULT
// when OR'ing together flags, to allow certain flag states to be favored
// as truthy for the "unused" state, in case that helped some efficiency
// trick.  This made the callsites much more noisy, so LEVEL_MASK_NONE is used
// solely to help call out places that don't have other flags.
//
#define LEVEL_MASK_NONE \
    FLAG_STATE_BYTE(STATE_0)  // note that the 0 state is implicit most places


#define Set_Level_Flag(L,name) \
    ((L)->flags.bits |= LEVEL_FLAG_##name)

#define Get_Level_Flag(L,name) \
    (((L)->flags.bits & LEVEL_FLAG_##name) != 0)

#define Clear_Level_Flag(L,name) \
    ((L)->flags.bits &= ~LEVEL_FLAG_##name)

#define Not_Level_Flag(L,name) \
    (((L)->flags.bits & LEVEL_FLAG_##name) == 0)


// C function implementing a native ACTION!
//
typedef Bounce (Executor)(Level* level_);
typedef Executor Dispatcher;  // sub-dispatched in Action_Executor()

// Deciders are a narrow kind of boolean predicate used in type checking.
//
typedef bool (Decider)(const Value* arg);

// This is for working around pedantic C and C++ errors, when an extension
// that doesn't use %sys-core.h tries to redefine dispatcher in terms of
// taking a void* and returning a Value*.
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


// NOTE: The ordering of the fields in `Reb_Level` are specifically done so
// as to accomplish correct 64-bit alignment of pointers on 64-bit systems.
//
// Because performance in the core evaluator loop is system-critical, this
// uses full platform `int`s instead of REBLENs.
//
// If modifying the structure, be sensitive to this issue.
//

#if CPLUSPLUS_11
    struct RebolLevelStruct : public Node
#else
    struct RebolLevelStruct
#endif
{
    // These are LEVEL_FLAG_XXX or'd together--see their documentation above.
    //
    // Note: In order to use the memory pools, this must be in first position,
    // and it must not have the NODE_FLAG_UNREADABLE bit set when in use.
    //
    union HeaderUnion flags;

    // This is the source from which new values will be fetched.  In addition
    // to working with an array, it is also possible to feed the evaluator
    // arbitrary Value*s through a variable argument list on the C stack.
    // This means no array needs to be dynamically allocated (though some
    // conditions require the va_list to be converted to an array, see notes
    // on Reify_Variadic_Feed_As_Array_Feed().)
    //
    // Since levels may share source information, this needs to be done with
    // a dereference.
    //
    Feed* feed;

    // Executors use SPARE as a general temporary place for evaluations, but
    // it is available for use by native Dispatchers while they are running.
    // It's particularly useful because it is GC guarded, and a valid target
    // location for evaluations.  (The argument cells of a native are *not*
    // legal evaluation targets...because a debugger that is triggered while
    // a nested level is running might expose intermediate bad states.  The
    // argument cells can be used to hold other fully formed cells.)
    //
    Cell spare;

    // A second GC-safe cell is available, but with a particular purpose in
    // the evaluator.  It stores a copy of the current cell being evaluated.
    // That can't be the Feed->p cell, because the evaluator has to seek ahead
    // one unit to find lookback quoters, such as `x: default [...]`, where
    // DEFAULT wants to quote the X: to its left.
    //
    // (An attempt was made to optimize this by multiplexing the OUT cell for
    // this purpose...after all, inert items want to wind up in the output cell
    // anyway.  But besides obfuscating the code, it was slower, since the
    // output cell involves a level of indirection to address.)
    //
    // Other executors can use this for what they want -but- if you use
    // LEVEL_FLAG_DISPATCHING_INTRINSIC then current must hold the cell of
    // the intrinsic being run.
    //
    Cell scratch;  // raw vs. derived class due to union/destructor combo

    // Each executor subclass can store specialized information in the level.
    // We place it here up top where we've been careful to make sure the
    // `spare` is on a (2 * sizeof(uintptr_t)) alignment, in case there are
    // things in the state that also require alignment (e.g. the eval state
    // uses its space for an extra "scratch" GC-safe cell)
    //
  union {
    struct EvaluatorExecutorStateStruct eval;

    struct ActionExecutorStateStruct action;

    struct {
        Level* main_level;
        bool changed;
    } compose;

    struct ScannerExecutorStateStruct scan;  // !! Fairly fat, trim down?
  } u;

    // The "executor" is the function the Trampoline delegates to for running
    // the continuations in the level.  Some executors dispatch further--for
    // instance the Action_Executor() will call Dispatcher* functions to
    // implement actions.
    //
    // Each executor can put custom information in the `u` union.
    //
    Executor* executor;

    // The prior level.  This never needs to be checked against nullptr,
    // because the bottom of the stack is BOTTOM_LEVEL which is allocated at
    // startup and never used to run code.
    //
    Level* prior;

    // This is where to write the result of the evaluation.  It should not be
    // in "movable" memory, hence usually not in an Array Flex's data.  Often
    // it is used as an intermediate free location to do calculations en route
    // to a final result, due to being GC-safe during function evaluation.
    //
    Atom* out;

    // The varlist is where arguments for FRAME! are kept.  Though it is
    // ultimately usable as an ordinary Varlist_Array() for a FRAME! value, it
    // is different because it is built progressively, with random bits in
    // its pending capacity that are specifically accounted for by the GC...
    // which limits its marking up to the progress point of `key`.
    //
    // It starts out unmanaged, so that if no usages by the user specifically
    // ask for a FRAME! value, and the VarList* isn't needed to store in a
    // Derelativize()'d or Move_Cell()'d value as a binding, it can be
    // reused or freed.  See Push_Action() and Drop_Action() for the logic.
    //
    // !!! Only Action_Executor() uses this at the moment, but FRAME! may
    // grow to be able to capture evaluator state as a reified notion to
    // automate in debugging.  That's very speculative, but, possible.
    //
    Array* varlist;  // must be Array, isn't legit VarList* while being built
    Element* rootvar;  // cached Varlist_Archetype() if varlist is not null

    // The "baseline" is a digest of the state of global variables at the
    // beginning of a level evaluation.  An example of one of the things the
    // baseline captures is the data stack pointer at the start of an
    // evaluation step...which allows the evaluator to know how much state
    // it has accrued cheaply that belongs to it (such as refinements on
    // the data stack.
    //
    // It may need to be updated.  For instance: if a level gets pushed for
    // reuse by multiple evaluations (like REDUCE, which pushes a single level
    // for its block traversal).  Then steps which accrue state in REDUCE must
    // bump the baseline to account for any pushes it does--lest the next
    // eval step in the sublevel interpret what was pushed as its own data
    // (e.g. as a refinement usage).  Anything like a YIELD which detaches a
    // level and then may re-enter it at a new global state must refresh
    // the baseline of any global state that may have changed.
    //
    // !!! Accounting for global state baselines is a work-in-progress.  The
    // mold buffer and manuals tracking are not currently covered.  This
    // will involve review, and questions about the total performance value
    // of global buffers (the data stack is almost certainly a win, but it
    // might be worth testing).
    //
    struct Reb_State baseline;

    // While a level is executing, any Alloc_Value() calls are linked into
    // a doubly-linked list.  This keeps them alive, and makes it quick for
    // them to be released.  In the case of an abrupt fail() call, they will
    // be automatically freed.
    //
    // In order to make a handle able to find the level whose linked list it
    // belongs to (in order to update the head of the list) the terminator on
    // the ends is not nullptr, but a pointer to the Level* itself (which
    // can be noticed as not being an API handle).
    //
    Node* alloc_value_list;

   #if TRAMPOLINE_COUNTS_TICKS
    //
    // The expression evaluation "tick" where the Level is starting its
    // processing.  This is helpful for setting breakpoints on certain ticks
    // in reproducible situations.
    //
    uintptr_t tick; // !!! Should this be in release builds, exposed to users?
  #endif

  #if DEBUG_LEVEL_LABELS
    //
    // Knowing the label symbol is not as handy as knowing the actual string
    // of the function this call represents (if any).  It is in UTF8 format,
    // and cast to `char*` to help debuggers that have trouble with Byte.
    //
    // (Only Action_Executor() levels can have a label in L->u.action.label,
    // but this debug field is in the Level struct for all levels, because it
    // is a pain in C watchlists to have to drill down into u.action.)
    //
    const char *label_utf8;
  #endif

  #if RUNTIME_CHECKS  // mirror Level's file and line number for C debugging
    const char *file; // char* more reliable than Byte* for UTF-8 in gdb/etc.
    int line;
  #endif
};

// These are needed protoyped by the array code because it wants to put file
// and line numbers into arrays based on the frame in effect at their time
// of allocation.

INLINE const Source* Level_Array(Level* L);
INLINE bool Level_Is_Variadic(Level* L);

#define TOP_LEVEL (g_ts.top_level + 0)  // avoid assign to TOP_LEVEL via + 0
#define BOTTOM_LEVEL (g_ts.bottom_level + 0)  // avoid assign to BOTTOM_LEVEL
