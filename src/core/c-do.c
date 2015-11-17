/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  c-do.c
**  Summary: the core interpreter - the heart of REBOL
**  Section: core
**  Author:  Carl Sassenrath
**  Notes:
**    WARNING WARNING WARNING
**    This is highly tuned code that should only be modified by experts
**    who fully understand its design. It is very easy to create odd
**    side effects so please be careful and extensively test all changes!
**
***********************************************************************/

#include "sys-core.h"

#include "tmp-evaltypes.h"

enum Reb_Param_Mode {
    PARAM_MODE_NORMAL, // ordinary arguments before any refinements seen
    PARAM_MODE_REFINE_PENDING, // picking up refinement arguments, none yet
    PARAM_MODE_REFINE_ARGS, // at least one refinement has been found
    PARAM_MODE_SCANNING, // looking for refinements (used out of order)
    PARAM_MODE_SKIPPING, // in the process of skipping an unused refinement
    PARAM_MODE_REVOKING // found an unset and aiming to revoke refinement use
};


//
//  Eval_Depth: C
//
REBINT Eval_Depth(void)
{
    REBINT depth = 0;
    struct Reb_Call *call;

    for (call = DSF; call != NULL; call = PRIOR_DSF(call), depth++);
    return depth;
}


//
//  Stack_Frame: C
//
struct Reb_Call *Stack_Frame(REBCNT n)
{
    struct Reb_Call *call = DSF;

    while (call) {
        if (n == 0) return call;

        --n;
        call = PRIOR_DSF(call);
    }

    return NULL;
}


//
//  trace: native [
//  
//  {Enables and disables evaluation tracing and backtrace.}
//  
//      mode [integer! logic!]
//      /back {Set mode ON to enable or integer for lines to display}
//      /function "Traces functions only (less output)"
//  ]
//
REBNATIVE(trace)
{
    REBVAL *arg = D_ARG(1);

    Check_Security(SYM_DEBUG, POL_READ, 0);

    // The /back option: ON and OFF, or INTEGER! for # of lines:
    if (D_REF(2)) { // /back
        if (IS_LOGIC(arg)) {
            Enable_Backtrace(VAL_LOGIC(arg));
        }
        else if (IS_INTEGER(arg)) {
            REBINT lines = Int32(arg);
            Trace_Flags = 0;
            if (lines < 0) {
                fail (Error_Invalid_Arg(arg));
                return R_UNSET;
            }

            Display_Backtrace(cast(REBCNT, lines));
            return R_UNSET;
        }
    }
    else Enable_Backtrace(FALSE);

    // Set the trace level:
    if (IS_LOGIC(arg)) {
        Trace_Level = VAL_LOGIC(arg) ? 100000 : 0;
    }
    else Trace_Level = Int32(arg);

    if (Trace_Level) {
        Trace_Flags = 1;
        if (D_REF(3)) SET_FLAG(Trace_Flags, 1); // function
        Trace_Depth = Eval_Depth() - 1; // subtract current TRACE frame
    }
    else Trace_Flags = 0;

    return R_UNSET;
}

static REBINT Init_Depth(void)
{
    // Check the trace depth is ok:
    REBINT depth = Eval_Depth() - Trace_Depth;
    if (depth < 0 || depth >= Trace_Level) return -1;
    if (depth > 10) depth = 10;
    Debug_Space(cast(REBCNT, 4 * depth));
    return depth;
}

#define CHECK_DEPTH(d) if ((d = Init_Depth()) < 0) return;\

void Trace_Line(REBSER *block, REBINT index, const REBVAL *value)
{
    int depth;

    if (GET_FLAG(Trace_Flags, 1)) return; // function
    if (ANY_FUNC(value)) return;

    CHECK_DEPTH(depth);

    Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,1)), index+1, value);
    if (IS_WORD(value) || IS_GET_WORD(value)) {
        value = GET_VAR(value);
        if (VAL_TYPE(value) < REB_NATIVE)
            Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,2)), value);
        else if (VAL_TYPE(value) >= REB_NATIVE && VAL_TYPE(value) <= REB_FUNCTION) {
            REBSER *words = List_Func_Words(value);
            Debug_Fmt_(
                cs_cast(BOOT_STR(RS_TRACE,3)), Get_Type_Name(value), words
            );
            Free_Series(words);
        }
        else
            Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,4)), Get_Type_Name(value));
    }
    /*if (ANY_WORD(value)) {
        word = value;
        if (IS_WORD(value)) value = GET_VAR(word);
        Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,2)), VAL_WORD_FRAME(word), VAL_WORD_INDEX(word), Get_Type_Name(value));
    }
    if (Trace_Stack) Debug_Fmt(cs_cast(BOOT_STR(RS_TRACE,3)), DSP, DSF);
    else
    */
    Debug_Line();
}

void Trace_Func(const REBVAL *word, const REBVAL *value)
{
    int depth;
    CHECK_DEPTH(depth);
    Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,5)), Get_Word_Name(word), Get_Type_Name(value));
    if (GET_FLAG(Trace_Flags, 1))
        Debug_Values(DSF_ARG(DSF, 1), DSF_NUM_ARGS(DSF), 20);
    else Debug_Line();
}

void Trace_Return(const REBVAL *word, const REBVAL *value)
{
    int depth;
    CHECK_DEPTH(depth);
    Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,6)), Get_Word_Name(word));
    Debug_Values(value, 1, 50);
}

void Trace_Arg(REBINT num, const REBVAL *arg, const REBVAL *path)
{
    int depth;
    if (IS_REFINEMENT(arg) && (!path || IS_END(path))) return;
    CHECK_DEPTH(depth);
    Debug_Fmt(cs_cast(BOOT_STR(RS_TRACE,6)), num+1, arg);
}


//
//  Trace_Value: C
//
void Trace_Value(REBINT n, const REBVAL *value)
{
    int depth;
    CHECK_DEPTH(depth);
    Debug_Fmt(cs_cast(BOOT_STR(RS_TRACE,n)), value);
}

//
//  Trace_String: C
//
void Trace_String(REBINT n, const REBYTE *str, REBINT limit)
{
    static char tracebuf[64];
    int depth;
    int len = MIN(60, limit);
    CHECK_DEPTH(depth);
    memcpy(tracebuf, str, len);
    tracebuf[len] = '\0';
    Debug_Fmt(cs_cast(BOOT_STR(RS_TRACE,n)), tracebuf);
}


//
//  Trace_Error: C
//
void Trace_Error(const REBVAL *value)
{
    int depth;
    CHECK_DEPTH(depth);
    Debug_Fmt(cs_cast(BOOT_STR(RS_TRACE, 10)), &VAL_ERR_VALUES(value)->type, &VAL_ERR_VALUES(value)->id);
}


//
//  Next_Path_Throws: C
// 
// Evaluate next part of a path.
//
REBFLG Next_Path_Throws(REBPVS *pvs)
{
    REBVAL *path;
    REBPEF func;
    REBVAL temp;

    // Path must have dispatcher, else return:
    func = Path_Dispatch[VAL_TYPE(pvs->value)];
    if (!func) return FALSE; // unwind, then check for errors

    pvs->path++;

    //Debug_Fmt("Next_Path: %r/%r", pvs->path-1, pvs->path);

    // object/:field case:
    if (IS_GET_WORD(path = pvs->path)) {
        pvs->select = GET_MUTABLE_VAR(path);
        if (IS_UNSET(pvs->select))
            fail (Error(RE_NO_VALUE, path));
    }
    // object/(expr) case:
    else if (IS_PAREN(path)) {

        if (DO_ARRAY_THROWS(&temp, path)) {
            *pvs->value = temp;
            return TRUE;
        }

        pvs->select = &temp;
    }
    else // object/word and object/value case:
        pvs->select = path;

    // Uses selector on the value.
    // .path - must be advanced as path is used (modified by func)
    // .value - holds currently evaluated path value (modified by func)
    // .select - selector on value
    // .store - storage (usually TOS) for constructed values
    // .setval - non-zero for SET-PATH (set to zero after SET is done)
    // .orig - original path for error messages
    switch (func(pvs)) {
    case PE_OK:
        break;
    case PE_SET: // only sets if end of path
        if (pvs->setval && IS_END(pvs->path+1)) {
            *pvs->value = *pvs->setval;
            pvs->setval = 0;
        }
        break;
    case PE_NONE:
        SET_NONE(pvs->store);
    case PE_USE:
        pvs->value = pvs->store;
        break;
    case PE_BAD_SELECT:
        fail (Error(RE_INVALID_PATH, pvs->orig, pvs->path));
    case PE_BAD_SET:
        fail (Error(RE_BAD_PATH_SET, pvs->orig, pvs->path));
    case PE_BAD_RANGE:
        fail (Error_Out_Of_Range(pvs->path));
    case PE_BAD_SET_TYPE:
        fail (Error(RE_BAD_FIELD_SET, pvs->path, Type_Of(pvs->setval)));
    default:
        assert(FALSE);
    }

    if (NOT_END(pvs->path + 1)) return Next_Path_Throws(pvs);

    return FALSE;
}


//
//  Do_Path: C
// 
// Evaluate a path value. Path_val is updated so
// result can be used for function refinements.
// If val is not zero, then this is a SET-PATH.
// Returns value only if result is a function,
// otherwise the result is on TOS.
//
REBVAL *Do_Path(REBVAL *out, const REBVAL **path_val, REBVAL *val)
{
    REBPVS pvs;

    // None of the values passed in can live on the data stack, because
    // they might be relocated during the path evaluation process.
    //
    assert(!IN_DATA_STACK(out));
    assert(!IN_DATA_STACK(*path_val));
    assert(!val || !IN_DATA_STACK(val));

    // Not currently robust for reusing passed in path or value as the output
    assert(out != *path_val && out != val);

    assert(!val || !THROWN(val));

    pvs.setval = val;       // Set to this new value
    pvs.store = out;        // Space for constructed results

    // Get first block value:
    pvs.orig = *path_val;
    pvs.path = VAL_BLK_DATA(pvs.orig);

    // Lookup the value of the variable:
    if (IS_WORD(pvs.path)) {
        pvs.value = GET_MUTABLE_VAR(pvs.path);
        if (IS_UNSET(pvs.value))
            fail (Error(RE_NO_VALUE, pvs.path));
    }
    else pvs.value = pvs.path;

    // Start evaluation of path:
    if (IS_END(pvs.path + 1)) {
        // If it was a single element path, return the value rather than
        // try to dispatch it (would cause a crash at time of writing)
        //
        // !!! Is this the desired behavior, or should it be an error?
    }
    else if (Path_Dispatch[VAL_TYPE(pvs.value)]) {
        REBFLG threw = Next_Path_Throws(&pvs);

        // !!! This assertion is triggering (sometimes) in release builds on:
        //
        //     t: now
        //     t/time: 10:20:03
        //
        /* assert(threw == THROWN(pvs.value)); */
        //
        // It thinks pvs.value has its THROWN bit set when it completed
        // successfully.  It was a PE_USE case where pvs.value was reset to
        // pvs.store, and pvs.store has its thrown bit set.  Valgrind does not
        // catch any uninitialized variables.
        //
        // Seems to point to a problem, but the code appears to work.  A
        // general review to come back and figure out what the path dispatch
        // is supposed to do is pending, so leaving this here for now.

        // Check for errors:
        if (NOT_END(pvs.path+1) && !ANY_FUNC(pvs.value)) {
            // Only function refinements should get by this line:
            fail (Error(RE_INVALID_PATH, pvs.orig, pvs.path));
        }
    }
    else if (!ANY_FUNC(pvs.value))
        fail (Error(RE_BAD_PATH_TYPE, pvs.orig, Type_Of(pvs.value)));

    // If SET then we don't return anything
    if (val) {
        return 0;
    } else {
        // If storage was not used, then copy final value back to it:
        if (pvs.value != pvs.store) *pvs.store = *pvs.value;
        // Return 0 if not function or is :path/word...
        if (!ANY_FUNC(pvs.value)) return 0;
        *path_val = pvs.path; // return new path (for func refinements)
        return pvs.value; // only used for functions
    }
}


//
//  Pick_Path: C
// 
// Lightweight version of Do_Path used for A_PICK actions.
// Does not do paren evaluation, hence not designed to throw.
//
void Pick_Path(REBVAL *out, REBVAL *value, REBVAL *selector, REBVAL *val)
{
    REBPVS pvs;
    REBPEF func;

    pvs.value = value;
    pvs.path = 0;
    pvs.select = selector;
    pvs.setval = val;
    pvs.store = out;        // Temp space for constructed results

    // Path must have dispatcher, else return:
    func = Path_Dispatch[VAL_TYPE(value)];
    if (!func) return; // unwind, then check for errors

    switch (func(&pvs)) {
    case PE_OK:
        break;
    case PE_SET: // only sets if end of path
        if (pvs.setval) *pvs.value = *pvs.setval;
        break;
    case PE_NONE:
        SET_NONE(pvs.store);
    case PE_USE:
        pvs.value = pvs.store;
        break;
    case PE_BAD_SELECT:
        fail (Error(RE_INVALID_PATH, pvs.value, pvs.select));
    case PE_BAD_SET:
        fail (Error(RE_BAD_PATH_SET, pvs.value, pvs.select));
    default:
        assert(FALSE);
    }
}


//
//  Do_Signals: C
// 
// Special events to process during evaluation.
// Search for SET_SIGNAL to find them.
//
void Do_Signals(void)
{
    REBCNT sigs;
    REBCNT mask;

    // Accumulate evaluation counter and reset countdown:
    if (Eval_Count <= 0) {
        //Debug_Num("Poll:", (REBINT) Eval_Cycles);
        Eval_Cycles += Eval_Dose - Eval_Count;
        Eval_Count = Eval_Dose;
        if (Eval_Limit != 0 && Eval_Cycles > Eval_Limit)
            Check_Security(SYM_EVAL, POL_EXEC, 0);
    }

    if (!(Eval_Signals & Eval_Sigmask)) return;

    // Be careful of signal loops! EG: do not PRINT from here.
    sigs = Eval_Signals & (mask = Eval_Sigmask);
    Eval_Sigmask = 0;   // avoid infinite loop
    //Debug_Num("Signals:", Eval_Signals);

    // Check for recycle signal:
    if (GET_FLAG(sigs, SIG_RECYCLE)) {
        CLR_SIGNAL(SIG_RECYCLE);
        Recycle();
    }

#ifdef NOT_USED_INVESTIGATE
    if (GET_FLAG(sigs, SIG_EVENT_PORT)) {  // !!! Why not used?
        CLR_SIGNAL(SIG_EVENT_PORT);
        Awake_Event_Port();
    }
#endif

    // Escape only allowed after MEZZ boot (no handlers):
    if (GET_FLAG(sigs, SIG_ESCAPE) && PG_Boot_Phase >= BOOT_MEZZ) {
        CLR_SIGNAL(SIG_ESCAPE);
        Eval_Sigmask = mask;
        fail (VAL_ERR_OBJECT(TASK_HALT_ERROR));
    }

    Eval_Sigmask = mask;
}


//
//  Dispatch_Call_Throws: C
// 
// Expects call frame to be ready with all arguments fulfilled.
//
REBFLG Dispatch_Call_Throws(struct Reb_Call *call)
{
#if !defined(NDEBUG)
    REBINT dsp_precall = DSP;

    // We keep track of the head of the list of series that are not tracked
    // by garbage collection at the outset of the call.  Then we ensure that
    // when the call is finished, no accumulation has happened.  So all
    // newly allocated series should either be (a) freed or (b) delegated
    // to management by the GC...else they'd represent a leak
    //
    REBCNT manuals_tail = SERIES_TAIL(GC_Manuals);

    REBCNT series_guard_tail = SERIES_TAIL(GC_Series_Guard);
    REBCNT value_guard_tail = SERIES_TAIL(GC_Value_Guard);

    const REBYTE *label_str = Get_Word_Name(DSF_LABEL(call));
#endif

    const REBVAL * const func = DSF_FUNC(call);
    REBVAL *out = DSF_OUT(call);

    REBFLG threw;

    // We need to save what the DSF was prior to our execution, and
    // cannot simply use our frame's prior...because our frame's
    // prior call frame may be a *pending* frame that we do not want
    // to put in effect when we are finished.
    //
    struct Reb_Call *dsf_precall = DSF;
    SET_DSF(call);

    // Write some garbage (that won't crash the GC) into the `out` slot in
    // the debug build.  This helps to catch functions that do not
    // at some point intentionally write an output value into the slot.
    //
    // Note: if they use that slot for temp space, it subverts this check.
    SET_TRASH_SAFE(out);

    if (Trace_Flags) Trace_Func(DSF_LABEL(call), func);

    switch (VAL_TYPE(func)) {
    case REB_NATIVE:
        threw = Do_Native_Throws(func);
        break;
    case REB_ACTION:
        threw = Do_Action_Throws(func);
        break;
    case REB_COMMAND:
        threw = Do_Command_Throws(func);
        break;
    case REB_CLOSURE:
        threw = Do_Closure_Throws(func);
        break;
    case REB_FUNCTION:
        threw = Do_Function_Throws(func);
        break;
    case REB_ROUTINE:
        threw = Do_Routine_Throws(func);
        break;
    default:
        assert(FALSE);
    }

    // Function execution should have written *some* actual output value
    // over the trash that we put in the return slot before the call.
    assert(!IS_TRASH(out));

    assert(VAL_TYPE(out) < REB_MAX); // cheap check

    ASSERT_VALUE_MANAGED(out);

    assert(threw == THROWN(out));

#if !defined(NDEBUG)
    assert(DSP >= dsp_precall);
    if (DSP > dsp_precall) {
        PROBE_MSG(DSF_WHERE(call), "UNBALANCED STACK TRAP!!!");
        panic (Error(RE_MISC));
    }

    MANUALS_LEAK_CHECK(manuals_tail, cs_cast(label_str));

    assert(series_guard_tail == SERIES_TAIL(GC_Series_Guard));
    assert(value_guard_tail == SERIES_TAIL(GC_Value_Guard));
#endif

    SET_DSF(dsf_precall);
    Free_Call(call);

    return threw;
}


//
//  Do_Core: C
// 
// Evaluate the code block until we have:
//     1. An irreducible value (return next index)
//     2. Reached the end of the block (return END_FLAG)
//     3. Encountered an error
// 
// Index is a zero-based index into the block.
// Op indicates infix operator is being evaluated (precedence);
// The value (or error) is placed on top of the data stack.
// 
// LOOKAHEAD:
// When we're in mid-dispatch of an infix function, the precedence
// is such that we don't want to do further infix lookahead while
// getting the arguments.  (e.g. with `1 + 2 * 3` we don't want
// infix `+` to look ahead past the 2 to see the infix `*`)
// 
// !!! IMPORTANT NOTE !!! => Changing the behavior of the function calling
// conventions and parameter fulfillment generally needs to mean changes to
// two other closely-related routines: Apply_Block_Throws() and
// Redo_Func_Throws().
//
REBCNT Do_Core(REBVAL * const out, REBFLG next, REBSER *block, REBCNT index, REBFLG lookahead)
{
    REBINT dsp_orig = DSP;

#if !defined(NDEBUG)
    static REBCNT count_static = 0;
    REBCNT count;

    // Debug builds that are emulating the old behavior of writing NONE into
    // refinement args need to know when they should be writing these nones
    // or leaving what's there during PARAM_MODE_SCANNING/PARAM_MODE_SKIPPING
    REBFLG write_none;
#endif

    const REBVAL *value;
    REBFLG infix;

    struct Reb_Call *call;

    // Functions don't have "names", though they can be assigned to words.
    // If a function invokes via word lookup (vs. a literal FUNCTION! value),
    // 'label' will be that WORD!, and NULL otherwise.
    const REBVAL *label;

    const REBVAL *refinements;
    enum Reb_Param_Mode mode; // before refinements, in refinement, skipping...

    // We use the convention that "param" refers to the word from the spec
    // of the function (a.k.a. the "formal" argument) and "arg" refers to
    // the evaluated value the function sees (a.k.a. the "actual" argument)
    REBVAL *param;
    REBVAL *arg;

    // The refinement currently being processed.  We have to remember its
    // address in case we want to revoke it.  (Note: can probably save on
    // having another pointer by merging with others above later.)
    REBVAL *refine;

    // Most of what this routine does can be done with value pointers and
    // the data stack.  Some operations need a unit of additional storage.
    // This is a one-REBVAL-sized cell for saving that data.
    REBVAL save;

    // Though we can protect the value written into the target pointer 'out'
    // from GC during the course of evaluation, we can't protect the
    // underlying value from relocation.  Technically this would be a problem
    // for any series which might be modified while this call is running, but
    // most notably it applies to the data stack--where output used to always
    // be returned.
    //
#ifdef STRESS_CHECK_DO_OUT_POINTER
    ASSERT_NOT_IN_SERIES_DATA(out);
#else
    assert(!IN_DATA_STACK(out));
#endif

    // Only need to check this once (C stack size would be the same each
    // time this line is run if it were in a loop)
    if (C_STACK_OVERFLOWING(&value)) Trap_Stack_Overflow();

do_at_index:
    assert(index != END_FLAG && index != THROWN_FLAG);
    SET_TRASH_SAFE(out);

#ifndef NDEBUG
    // This counter is helpful for tracking a specific invocation.
    // If you notice a crash, look on the stack for the topmost call
    // and read the count...then put that here and recompile with
    // a breakpoint set.  (The 'count_static' value is captured into a
    // local 'count' so you still get the right count after recursion.)
    //
    // We bound it at the max unsigned 32-bit because otherwise it would
    // roll over to zero and print a message that wasn't asked for, which
    // is annoying even in a debug build.
    //
    if (count_static < MAX_U32) {
        count = ++count_static;
        if (count ==
            // *** DON'T COMMIT THIS v-- KEEP IT AT ZERO! ***
                                      0
            // *** DON'T COMMIT THIS --^ KEEP IT AT ZERO! ***
        ) {
            Val_Init_Block_Index(&save, block, index);
            PROBE_MSG(&save, "Do_Core() count trap");
        }
    }
#endif

    value = BLK_SKIP(block, index);

    if (Trace_Flags) Trace_Line(block, index, value);

reevaluate:
    if (--Eval_Count <= 0 || Eval_Signals) Do_Signals();

    assert(!THROWN(value));
    assert(!VAL_GET_OPT(value, OPT_VALUE_REEVALUATE));
    ASSERT_VALUE_MANAGED(value);

    // Someday it may be worth it to micro-optimize these null assignments
    // so they don't happen each time through the loop.
    label = NULL;
    refinements = NULL;

    switch (VAL_TYPE(value)) {

    case REB_END:
        SET_UNSET(out);
        return END_FLAG;

    case REB_WORD:
        GET_VAR_INTO(out, value);

    do_fetched_word:
        if (IS_UNSET(out)) fail (Error(RE_NO_VALUE, value));

        if (ANY_FUNC(out)) {
            // We can only acquire an infix operator's first arg during the
            // "lookahead".  Here we are starting a brand new expression.
            if (VAL_GET_EXT(out, EXT_FUNC_INFIX))
                fail (Error(RE_NO_OP_ARG, value));

            // We will reuse the TOS for the OUT of the call frame
            label = value;

            value = out;

            if (Trace_Flags) Trace_Line(block, index, value);
            goto do_function_args;
        }

    #if !defined(NDEBUG)
        if (LEGACY(OPTIONS_LIT_WORD_DECAY) && IS_LIT_WORD(out))
            VAL_SET(out, REB_WORD);
    #endif

        index++;
        break;

    case REB_SET_WORD:
        // `index` and `out` are modified
        DO_NEXT_MAY_THROW_CORE(index, out, block, index + 1, TRUE);

        if (index == THROWN_FLAG) goto return_index;

        if (index == END_FLAG) {
            // `do [x:]` is not as purposefully an assignment of an unset as
            // something like `do [x: ()]`, so it's an error.
            assert(IS_UNSET(out));
            fail (Error(RE_NEED_VALUE, value));
        }

        if (IS_UNSET(out)) {
            // Treat direct assignments of an unset as unsetting the word
            REBVAL *var;

        #if !defined(NDEBUG)
            if (LEGACY(OPTIONS_CANT_UNSET_SET_WORDS))
                fail (Error(RE_NEED_VALUE, value));
        #endif

            if (!HAS_FRAME(value)) fail (Error(RE_NOT_BOUND, value));

            var = GET_MUTABLE_VAR(value);
            SET_UNSET(var);
        }
        else
            Set_Var(value, out);
        break;

    case REB_NATIVE:
    case REB_ACTION:
    case REB_COMMAND:
    case REB_CLOSURE:
    case REB_FUNCTION:

        // If we come across an infix function from do_at_index in the loop,
        // we can't actually run it.  It only runs after an evaluation has
        // yielded a value as part of a single "atomic" Do/Next step
        //
        // Note: Because this is a function value being hit literally in
        // a block, it does not have a name.  `label` is NULL.  The function
        // value may display strangely in the error...but that is a problem
        // to address in general with error display.
        //
        if (VAL_GET_EXT(value, EXT_FUNC_INFIX))
            fail (Error(RE_NO_OP_ARG, value));

    // Value must be the function when a jump here occurs
    do_function_args:
        assert(ANY_FUNC(value));
        index++;
        assert(DSP == dsp_orig);

        // `out` may contain the pending argument for an infix operation,
        // and it could also be the backing store of the `value` pointer
        // to the function.  So Make_Call() shouldn't overwrite it!
        //
        // Note: Although we create the call frame here, we cann't "put
        // it into effect" until all the arguments have been computed.
        // This is because recursive stack-relative bindings would wind
        // up reading variables out of the frame while it is still
        // being built, and that would be bad.

        call = Make_Call(out, block, index, label, value);

        // Make_Call() put a safe copy of the function value into the
        // call frame.  Refresh our value to point to that one (instead of
        // where it was possibly lingering in the `out` slot).

        value = DSF_FUNC(call);
        assert(ANY_FUNC(value));
        infix = VAL_GET_EXT(value, EXT_FUNC_INFIX);

        // If there are no arguments, just skip the next section
        if (DSF_NUM_ARGS(call) == 0) goto function_ready_to_call;

        // We assume you can enumerate both the formal parameters (in the
        // spec) and the actual arguments (in the call frame) using pointer
        // incrementation, that they are both terminated by REB_END, and
        // that there are an equal number of values in both.
        param = VAL_FUNC_PARAM(value, 1);
        arg = DSF_ARG(call, 1);

        // Fetch the first argument from output slot before overwriting
        // !!! Redundant check on REB_PATH branch (knows it's not infix)
        if (infix) {
            assert(index != 0);

            // If func is being called infix, prior evaluation loop has
            // already computed first argument, so it's sitting in `out`
            *arg = *out;
            if (!TYPE_CHECK(param, VAL_TYPE(arg)))
                fail (Error_Arg_Type(call, param, Type_Of(arg)));

            param++;
            arg++;
        }

        // !!! Currently, path evaluation "punts" on refinements.  When it
        // finds a function, it stops the path evaluation and passes back
        // the unprocessed remainder to the evaluator.  A more elegant
        // solution would be able to process and notice (for instance) that
        // `:APPEND/ONLY` should yield a function value that has been
        // specialized with a refinement.  Path chaining should thus be
        // able to effectively do this and give the refined function object
        // back to the evaluator or other client.
        //
        // That is not implemented yet, and even when it is there will be
        // a need to "cheat" such that temporary function objects are not
        // created when they would just be invoked and immediately GC'd.
        // A trick would be to have the path evaluation gather the refinements
        // on the data stack as it went and make a decision at the end
        // about how to handle it.
        //
        // This code simulates that path-processing-to-data-stack, but it
        // should really be something Do_Path does.  In any case, we put the
        // refinements on the data stack...and we know refinements are from
        // dsp_orig to DSP (thanks to accounting, all other operations
        // should balance!)

        for (; refinements && NOT_END(refinements); refinements++) {
            if (IS_PAREN(refinements)) {
                // It's okay for us to mess with the `out` slot here, since
                // any infix parameter or function value that had lived in
                // that slot has been safely moved to the call frame
                if (DO_ARRAY_THROWS(out, refinements)) {
                    Free_Call(call);
                    DS_DROP_TO(dsp_orig);
                    goto return_index;
                }
                DS_PUSH(out);
            }
            else if (IS_GET_WORD(refinements)) {
                *out = *GET_VAR(refinements);
                if (IS_NONE(out)) continue;
                if (!IS_WORD(out)) fail (Error(RE_BAD_REFINE, out));
                DS_PUSH(out);
            }
            else if (IS_WORD(refinements))
                DS_PUSH(refinements);
            else if (!IS_NONE(refinements))
                fail (Error(RE_BAD_REFINE, out));
        }

        // To make things easier for processing, reverse the refinements on
        // the data stack (we needed to evaluate them in forward order).
        // This way we can just pop them as we go, and know if they weren't
        // all consumed if it doesn't get back to `dsp_orig` by the end.

        if (dsp_orig != DSP) {
            REBVAL *temp = DS_AT(dsp_orig + 1);
            refine = DS_TOP;
            while (refine > temp) {
                *out = *refine;
                *refine = *temp;
                *temp = *out;

                refine--;
                temp++;
            }
        }

        // This loop goes through the parameter and argument slots, filling in
        // the arguments via recursive calls to the evaluator.
        //
        // Note that Make_Call initialized them all to UNSET.  This is needed
        // in order to allow skipping around, in particular so that a
        // refinement slot can be marked as processed or not processed, but
        // also because the garbage collector has to consider the slots "live"
        // as arguments are progressively fulfilled.

        mode = PARAM_MODE_NORMAL;

    #if !defined(NDEBUG)
        write_none = FALSE;
    #endif

        for (; NOT_END(param); param++, arg++) {
        no_advance:
            assert(IS_TYPESET(param));

            // *** PURE LOCALS => continue ***

            if (VAL_GET_EXT(param, EXT_WORD_HIDE)) {
                // When the spec contained a SET-WORD!, that was a "pure
                // local".  It corresponds to no argument and will not
                // appear in WORDS-OF.  Unlike /local, it cannot be used
                // for "locals injection".  Helpful when writing generators
                // because you don't have to go find /local (!), you can
                // really put it wherever is convenient--no position rule.

                // A special trick for functions marked EXT_FUNC_HAS_RETURN
                // puts a "magic" REBNATIVE(return) value into the arg slot
                // for pure locals named RETURN: ....used by FUNC and CLOS

                if (
                    VAL_GET_EXT(value, EXT_FUNC_HAS_RETURN)
                    && SAME_SYM(VAL_TYPESET_SYM(param), SYM_RETURN)
                ) {
                    *arg = *ROOT_RETURN_NATIVE;
                    VAL_FUNC_RETURN_TO(arg) = VAL_FUNC_PARAMLIST(value);
                }
                // otherwise leave it unset

                continue;
            }

            if (!VAL_GET_EXT(param, EXT_TYPESET_REFINEMENT)) {
                // Hunting a refinement?  Quickly disregard this if we are
                // doing such a scan and it isn't a refinement.
                if (mode == PARAM_MODE_SCANNING) {
                #if !defined(NDEBUG)
                    if (write_none)
                        SET_NONE(arg);
                #endif
                    continue;
                }
            }
            else {
                // *** REFINEMENTS => continue ***

                // Refinements are tricky because users can write:
                //
                //     foo: func [a /b c /d e] [...]
                //
                //     foo/b/d (1 + 2) (3 + 4) (5 + 6)
                //     foo/d/b (1 + 2) (3 + 4) (5 + 6)
                //
                // But we are marching across the parameters in order of their
                // *definition*.  Hence we may have to seek refinements ahead
                // or behind to know where to put the results we evaluate.

                if (mode == PARAM_MODE_SCANNING) {
                    if (
                        SAME_SYM(VAL_WORD_SYM(DS_TOP), VAL_TYPESET_SYM(param))
                    ) {
                        // If we seeked backwards to find a refinement and it's
                        // the one we are looking for, "consume" it off the
                        // data stack to say we found it in the frame
                        DS_DROP;

                        // Now as we switch to pending mode, change refine to
                        // point at the arg slot so we can revoke it if needed
                        mode = PARAM_MODE_REFINE_PENDING;
                        refine = arg;

                    #if !defined(NDEBUG)
                        write_none = FALSE;

                        if (TYPE_CHECK(param, REB_LOGIC)) {
                            // OPTIONS_REFINEMENTS_TRUE at function create
                            SET_TRUE(refine);
                        }
                        else
                    #endif
                            Val_Init_Word_Unbound(
                                refine, REB_WORD, VAL_TYPESET_SYM(param)
                            );

                        continue;
                    }

                    // ...else keep scanning, but if it's unset then set it
                    // to none because we *might* not revisit this spot again.
                    if (IS_UNSET(arg)) {
                        SET_NONE(arg);

                    #if !defined(NDEBUG)
                        if (TYPE_CHECK(param, REB_LOGIC))
                            write_none = TRUE;
                    #endif
                    }
                    else {
                    #if !defined(NDEBUG)
                        write_none = FALSE;
                    #endif
                    }
                    continue;
                }

                if (dsp_orig == DSP) {
                    // No refinements are left on the data stack, so if this
                    // refinement slot is still unset, skip the args and leave
                    // them as unsets (or set nones in legacy mode)
                    mode = PARAM_MODE_SKIPPING;
                    if (IS_UNSET(arg)) {
                        SET_NONE(arg);

                    #if !defined(NDEBUG)
                        // We need to know if we need to write nones into args
                        // based on the legacy switch capture.
                        if (TYPE_CHECK(param, REB_LOGIC))
                            write_none = TRUE;
                    #endif
                    }

                    continue;
                }

                // Should have only pushed words to the stack (or there would
                // have been an earlier error)
                assert(IS_WORD(DS_TOP));

                if (SAME_SYM(VAL_WORD_SYM(DS_TOP), VAL_TYPESET_SYM(param))) {
                    // We were lucky and the next refinement we wish to
                    // process lines up with this parameter slot.

                    mode = PARAM_MODE_REFINE_PENDING;
                    refine = arg;

                    DS_DROP;

                    #if !defined(NDEBUG)
                        if (TYPE_CHECK(param, REB_LOGIC)) {
                            // OPTIONS_REFINEMENTS_TRUE at function create
                            SET_TRUE(refine);
                        }
                        else
                    #endif
                            Val_Init_Word_Unbound(
                                refine, REB_WORD, VAL_TYPESET_SYM(param)
                            );

                    continue;
                }

                // We weren't lucky and need to scan
                mode = PARAM_MODE_SCANNING;

                assert(IS_WORD(DS_TOP));

                // We have to reset to the beginning if we are going to scan,
                // because we might have gone past the refinement on a prior
                // scan.  (Review if a bit might inform us if we've ever done
                // a scan before to get us started going out of order.  If not,
                // we would only need to look ahead.)

                param = VAL_FUNC_PARAM(value, 1);
                arg = DSF_ARG(call, 1);

            #if !defined(NDEBUG)
                write_none = FALSE;
            #endif

                // We might have a function with no normal args, where a
                // refinement is the first parameter...and we don't want to
                // run the loop's arg++/param++ we get if we `continue`
                goto no_advance;
            }

            if (mode == PARAM_MODE_SKIPPING) {
            #if !defined(NDEBUG)
                // In release builds we just skip because the args are already
                // UNSET.  But in debug builds we may need to overwrite the
                // unset default with none if this function was created while
                // legacy mode was on.
                if (write_none)
                    SET_NONE(arg);
            #endif
                continue;
            }

            assert(
                mode == PARAM_MODE_NORMAL
                || mode == PARAM_MODE_REFINE_PENDING
                || mode == PARAM_MODE_REFINE_ARGS
                || mode == PARAM_MODE_REVOKING
            );

            // *** QUOTED OR EVALUATED ITEMS ***

            if (VAL_GET_EXT(param, EXT_TYPESET_QUOTE)) {
                // Using a GET-WORD! in the function spec indicates that you
                // would like that argument to be EXT_TYPESET_QUOTE, e.g.
                // not evaluated at the callsite:
                //
                //     >> foo: function [:a] [print [{a is} a]
                //
                //     >> foo 1 + 2
                //     a is 1
                //
                //     >> foo (1 + 2)
                //     a is (1 + 2)
                //
                // Using a LIT-WORD in the function spec indicates that args
                // should be EXT_TYPESET_QUOTE but also EXT_TYPESET_EVALUATE
                // so that if they are "gets" or parens they still run:
                //
                //     >> foo: function ['a] [print [{a is} a]
                //
                //     >> foo 1 + 2
                //     a is 1
                //
                //     >> foo (1 + 2)
                //     a is 3
                //
                // This provides a convenient escape mechanism for the caller
                // to subvert quote-like behavior (which is an option that
                // one generally would like to give in a quote-like API).
                //
                // A special allowance is made that if a function quotes its
                // argument and the parameter is at the end of a series,
                // it will be treated as an UNSET!  (This is how HELP manages
                // to act as an arity 1 function as well as an arity 0 one.)
                // But to use this feature it must also explicitly accept
                // the UNSET! type (checked after the switch).
                //
                if (index < BLK_LEN(block)) {
                    REBVAL * const quoted = BLK_SKIP(block, index);
                    if (
                        VAL_GET_EXT(param, EXT_TYPESET_EVALUATE)
                        && (
                            IS_PAREN(quoted)
                            || IS_GET_WORD(quoted)
                            || IS_GET_PATH(quoted)
                        )
                    ) {
                        DO_NEXT_MAY_THROW_CORE(
                            index, arg, block, index, !infix
                        );
                        if (index == THROWN_FLAG) {
                            *out = *arg;
                            Free_Call(call);
                            // If we have refinements pending on the data
                            // stack we need to balance those...
                            DS_DROP_TO(dsp_orig);
                            goto return_index;
                        }
                        if (index == END_FLAG) {
                            // This is legal due to the series end UNSET! trick
                            // (we will type check if unset is actually legal
                            // in a moment below)
                            assert(IS_UNSET(arg));
                        }
                    }
                    else {
                        index++;
                        *arg = *quoted;
                    }
                }
                else {
                    // series end UNSET! trick; already unset...
                }
            }
            else {
                // !!! Note: ROUTINE! does not set any bits on the symbols
                // and will need to be made to...
                //
                // assert(VAL_GET_EXT(param, EXT_TYPESET_EVALUATE));

                // An ordinary WORD! in the function spec indicates that you
                // would like that argument to be evaluated normally.
                //
                //     >> foo: function [a] [print [{a is} a]
                //
                //     >> foo 1 + 2
                //     a is 3
                //
                DO_NEXT_MAY_THROW_CORE(index, arg, block, index, !infix);
                if (index == THROWN_FLAG) {
                    *out = *arg;
                    Free_Call(call);
                    // If we have refinements pending on the data
                    // stack we need to balance those...
                    DS_DROP_TO(dsp_orig);
                    goto return_index;
                }
                if (index == END_FLAG)
                    fail (Error_No_Arg(DSF_LABEL(call), param));
            }

            ASSERT_VALUE_MANAGED(arg);

            if (IS_UNSET(arg)) {
                if (mode == PARAM_MODE_REFINE_ARGS)
                    fail (Error(RE_BAD_REFINE_REVOKE));
                else if (mode == PARAM_MODE_REFINE_PENDING) {
                    mode = PARAM_MODE_REVOKING;

                #if !defined(NDEBUG)
                    // We captured the "true-valued-refinements" legacy switch
                    // and decided whether to make the refinement true or the
                    // word value of the refinement.  We use the same switch
                    // to cover whether the unused args are NONE or UNSET...
                    // but decide which it is by looking at what was filled
                    // in for the refinement.  UNSET is the default.

                    if (!IS_WORD(refine)) {
                        assert(IS_LOGIC(refine));
                        SET_NONE(arg);
                    }
                #endif

                    // Revoke the refinement.
                    SET_NONE(refine);
                }
                else if (mode == PARAM_MODE_REVOKING) {
                #ifdef NDEBUG
                    // Don't have to do anything, it's unset
                #else
                    // We have overwritten the refinement so we don't know if
                    // it was LOGIC! or WORD!, but we don't need to know that
                    // because we can just copy whatever the previous arg
                    // was set to...
                    *arg = *(arg - 1);
                #endif
                }
            }
            else {
                if (mode == PARAM_MODE_REVOKING)
                    fail (Error(RE_BAD_REFINE_REVOKE));
                else if (mode == PARAM_MODE_REFINE_PENDING)
                    mode = PARAM_MODE_REFINE_ARGS;
            }

            // If word is typed, verify correct argument datatype:
            if (mode != PARAM_MODE_REVOKING)
                if (!TYPE_CHECK(param, VAL_TYPE(arg)))
                    fail (Error_Arg_Type(call, param, Type_Of(arg)));
        }

        // If we were scanning and didn't find the refinement we were looking
        // for, then complain with an error.
        //
        // !!! This will complain differently than a proper "REFINED!" strategy
        // would complain, because if you do:
        //
        //     append/(second [only asdhjas])/(print "hi") [a b c] [d]
        //
        // ...it would never make it to the print.  Here we do all the path
        // and paren evals up front and check that things are words or NONE,
        // not knowing if a refinement isn't on the function until the end.

        if (mode == PARAM_MODE_SCANNING)
            fail (Error(RE_BAD_REFINE, DS_TOP));

        // In the case where the user has said foo/bar/baz, and bar was later
        // in the spec than baz, then we will have passed it.  We need to
        // restart the scan (which may wind up failing)

        if (DSP != dsp_orig) {
            mode = PARAM_MODE_SCANNING;
            param = VAL_FUNC_PARAM(value, 1);
            arg = DSF_ARG(call, 1);

        #if !defined(NDEBUG)
            write_none = FALSE;
        #endif

            goto no_advance;
        }

    function_ready_to_call:
        // Execute the function with all arguments ready
        if (Dispatch_Call_Throws(call)) {
            index = THROWN_FLAG;
            goto return_index;
        }

        if (Trace_Flags) Trace_Return(label, out);

        if (VAL_GET_OPT(out, OPT_VALUE_REEVALUATE)) {
            // The return value came from EVAL and we need to "activate" it.
            //
            // !!! As EVAL is the only way this can ever happen, the test
            // could be if the function is a NATIVE! and holds the C
            // function pointer to REBNATIVE(eval)--then reclaim the bit.

            VAL_CLR_OPT(out, OPT_VALUE_REEVALUATE);

            // The next evaluation we invoke expects to be able to write into
            // `out` (and not have `value` living in there), so move it!
            save = *out;
            value = &save;

            // act "as if" value had been in the last position of the last
            // function argument evaluated (or the function itself if no args)
            index--;

            goto reevaluate;
        }
        break;

    case REB_PATH:
        label = value;

        // returns in word the path item, DS_TOP has value
        value = Do_Path(out, &label, 0);
        if (THROWN(out)) {
            index = THROWN_FLAG;
            goto return_index;
        }

        // Value returned only for functions that need evaluation
        if (value && ANY_FUNC(value)) {
            // object/func or func/refinements or object/func/refinement:

            assert(label);

            // You can get an actual function value as a label if you use it
            // literally with a refinement.  Tricky to make it, but possible:
            //
            // do reduce [
            //     to-path reduce [:append 'only] [a] [b]
            // ]
            //
            // Hence legal, but we don't pass that into Make_Call.

            if (!IS_WORD(label) && !ANY_FUNC(label))
                fail (Error(RE_BAD_REFINE, label)); // CC#2226

            // We should only get a label that is the function if said label
            // is the function value itself.
            assert(!ANY_FUNC(label) || value == label);

            // Cannot handle infix because prior value is wiped out above
            // (Theoretically we could save it if we are DO-ing a chain of
            // values, and make it work.  But then, a loop of DO/NEXT
            // may not behave the same as DO-ing the whole block.  Bad.)

            if (VAL_GET_EXT(value, EXT_FUNC_INFIX))
                fail (Error_Has_Bad_Type(value));

            refinements = label + 1;

            // It's possible to put a literal function value into a path,
            // but the labeling mechanism currently expects a word or NULL
            // for what you dispatch from.

            if (ANY_FUNC(label))
                label = NULL;

            goto do_function_args;
        } else
            index++;
        break;

    case REB_GET_PATH:
        label = value;

        // returns in word the path item, DS_TOP has value
        value = Do_Path(out, &label, 0);

        // !!! Historically this just ignores a result indicating this is a
        // function with refinements, e.g. ':append/only'.  However that
        // ignoring seems unwise.  It should presumably create a modified
        // function in that case which acts as if it has the refinement.
        if (label && !IS_END(label + 1) && ANY_FUNC(out))
            fail (Error(RE_TOO_LONG));

        index++;
        break;

    case REB_SET_PATH:
        DO_NEXT_MAY_THROW_CORE(index, out, block, index + 1, TRUE);

        assert(index != END_FLAG || IS_UNSET(out)); // unset if END_FLAG
        if (IS_UNSET(out)) fail (Error(RE_NEED_VALUE, value));
        if (index == THROWN_FLAG) goto return_index;

        label = value;
        Do_Path(&save, &label, out);
        // !!! No guarantee that result of a set-path eval would put the
        // set value in out atm, so can't reverse this yet so that the
        // first Do is into 'save' and the second into 'out'.  (Review)
        break;

    case REB_PAREN:
        if (DO_ARRAY_THROWS(out, value)) {
            index = THROWN_FLAG;
            goto return_index;
        }
        index++;
        break;

    case REB_LIT_WORD:
        *out = *value;
        VAL_SET(out, REB_WORD);
        index++;
        break;

    case REB_GET_WORD:
        GET_VAR_INTO(out, value);
        index++;
        break;

    case REB_LIT_PATH:
        // !!! Aliases a REBSER under two value types, likely bad, see CC#2233
        *out = *value;
        VAL_SET(out, REB_PATH);
        index++;
        break;

    case REB_FRAME:
        // !!! Frame should be hidden from user visibility
        panic (Error(RE_BAD_EVALTYPE, Get_Type(VAL_TYPE(value))));

    default:
        // Most things just evaluate to themselves
        assert(!IS_TRASH(value));
        *out = *value;
        index++;
        break;
    }

    if (index >= BLK_LEN(block)) goto return_index;

    // Should not have accumulated any net data stack during the evaluation
    assert(DSP == dsp_orig);

    // Should not have a THROWN value if we got here
    assert(index != THROWN_FLAG && !THROWN(out));

    // We do not look ahead for infix dispatch if we are currently processing
    // an infix operation with higher precedence
    if (lookahead) {
        value = BLK_SKIP(block, index);

        // Literal infix function values may occur.
        if (VAL_GET_EXT(value, EXT_FUNC_INFIX)) {
            label = NULL;
            if (Trace_Flags) Trace_Line(block, index, value);
            goto do_function_args;
        }

        if (IS_WORD(value)) {
            // WORD! values may look up to an infix function

            GET_VAR_INTO(&save, value);
            if (VAL_GET_EXT(&save, EXT_FUNC_INFIX)) {
                label = value;
                value = &save;
                if (Trace_Flags) Trace_Line(block, index, value);
                goto do_function_args;
            }

            // Perhaps not an infix function, but we just paid for a variable
            // lookup.  If this isn't just a DO/NEXT, use the work!
            if (!next) {
                *out = save;
                goto do_fetched_word;
            }
        }
    }

    // Continue evaluating rest of block if not just a DO/NEXT
    if (!next) goto do_at_index;

return_index:
    assert(DSP == dsp_orig);
    assert(!IS_TRASH(out));
    assert((index == THROWN_FLAG) == THROWN(out));
    assert(index != END_FLAG || index >= BLK_LEN(block));
    assert(VAL_TYPE(out) < REB_MAX); // cheap check
    return index;
}


//
//  Reduce_Block_Throws: C
// 
// Reduce block from the index position specified in the value.
// Collect all values from stack and make them a block.
//
REBFLG Reduce_Block_Throws(REBVAL *out, REBSER *block, REBCNT index, REBOOL into)
{
    REBINT dsp_orig = DSP;

    while (index < BLK_LEN(block)) {
        REBVAL reduced;
        DO_NEXT_MAY_THROW(index, &reduced, block, index);
        if (index == THROWN_FLAG) {
            *out = reduced;
            DS_DROP_TO(dsp_orig);
            return TRUE;
        }
        DS_PUSH(&reduced);
    }

    Pop_Stack_Values(out, dsp_orig, into);
    return FALSE;
}


//
//  Reduce_Only: C
// 
// Reduce only words and paths not found in word list.
//
void Reduce_Only(REBVAL *out, REBSER *block, REBCNT index, REBVAL *words, REBOOL into)
{
    REBINT dsp_orig = DSP;
    REBVAL *val;
    const REBVAL *v;
    REBSER *ser = 0;
    REBCNT idx = 0;

    if (IS_BLOCK(words)) {
        ser = VAL_SERIES(words);
        idx = VAL_INDEX(words);
    }

    for (val = BLK_SKIP(block, index); NOT_END(val); val++) {
        if (IS_WORD(val)) {
            // Check for keyword:
            if (ser && NOT_FOUND != Find_Word(ser, idx, VAL_WORD_CANON(val))) {
                DS_PUSH(val);
                continue;
            }
            v = GET_VAR(val);
            DS_PUSH(v);
        }
        else if (IS_PATH(val)) {
            const REBVAL *v;

            if (ser) {
                // Check for keyword/path:
                v = VAL_BLK_DATA(val);
                if (IS_WORD(v)) {
                    if (NOT_FOUND != Find_Word(ser, idx, VAL_WORD_CANON(v))) {
                        DS_PUSH(val);
                        continue;
                    }
                }
            }

            v = val;

            // pushes val on stack
            DS_PUSH_TRASH_SAFE;
            Do_Path(DS_TOP, &v, NULL);
        }
        else DS_PUSH(val);
        // No need to check for unwinds (THROWN) here, because unwinds should
        // never be accessible via words or paths.
    }

    Pop_Stack_Values(out, dsp_orig, into);

    assert(DSP == dsp_orig);
}


//
//  Reduce_Block_No_Set_Throws: C
//
REBFLG Reduce_Block_No_Set_Throws(REBVAL *out, REBSER *block, REBCNT index, REBOOL into)
{
    REBINT dsp_orig = DSP;

    while (index < BLK_LEN(block)) {
        REBVAL *value = BLK_SKIP(block, index);
        if (IS_SET_WORD(value)) {
            DS_PUSH(value);
            index++;
        }
        else {
            REBVAL reduced;
            DO_NEXT_MAY_THROW(index, &reduced, block, index);
            if (index == THROWN_FLAG) {
                *out = reduced;
                DS_DROP_TO(dsp_orig);
                return TRUE;
            }
            DS_PUSH(&reduced);
        }
    }

    Pop_Stack_Values(out, dsp_orig, into);

    return FALSE;
}


//
//  Compose_Block_Throws: C
// 
// Compose a block from a block of un-evaluated values and
// paren blocks that are evaluated.  Performs evaluations, so
// if 'into' is provided, then its series must be protected from
// garbage collection.
// 
//     deep - recurse into sub-blocks
//     only - parens that return blocks are kept as blocks
// 
// Writes result value at address pointed to by out.
//
REBFLG Compose_Block_Throws(REBVAL *out, REBVAL *block, REBFLG deep, REBFLG only, REBOOL into)
{
    REBVAL *value;
    REBINT dsp_orig = DSP;

    for (value = VAL_BLK_DATA(block); NOT_END(value); value++) {
        if (IS_PAREN(value)) {
            REBVAL evaluated;

            if (DO_ARRAY_THROWS(&evaluated, value)) {
                *out = evaluated;
                DS_DROP_TO(dsp_orig);
                return TRUE;
            }

            if (IS_BLOCK(&evaluated) && !only) {
                // compose [blocks ([a b c]) merge] => [blocks a b c merge]
                Push_Stack_Values(
                    cast(REBVAL*, VAL_BLK_DATA(&evaluated)),
                    VAL_BLK_LEN(&evaluated)
                );
            }
            else if (!IS_UNSET(&evaluated)) {
                // compose [(1 + 2) inserts as-is] => [3 inserts as-is]
                // compose/only [([a b c]) unmerged] => [[a b c] unmerged]
                DS_PUSH(&evaluated);
            }
            else {
                // compose [(print "Unsets *vanish*!")] => []
            }
        }
        else if (deep) {
            if (IS_BLOCK(value)) {
                // compose/deep [does [(1 + 2)] nested] => [does [3] nested]
                REBVAL composed;

                if (Compose_Block_Throws(&composed, value, TRUE, only, into)) {
                    *out = composed;
                    DS_DROP_TO(dsp_orig);
                    return TRUE;
                }

                DS_PUSH(&composed);
            }
            else {
                DS_PUSH(value);
                if (ANY_ARRAY(value)) {
                    // compose [copy/(orig) (copy)] => [copy/(orig) (copy)]
                    // !!! path and second paren are copies, first paren isn't
                    VAL_SERIES(DS_TOP) = Copy_Array_Shallow(VAL_SERIES(value));
                    MANAGE_SERIES(VAL_SERIES(DS_TOP));
                }
            }
        }
        else {
            // compose [[(1 + 2)] (reverse "wollahs")] => [[(1 + 2)] "shallow"]
            DS_PUSH(value);
        }
    }

    Pop_Stack_Values(out, dsp_orig, into);

    return FALSE;
}


//
//  Apply_Block_Throws: C
// 
// Invoke a function with arguments, either from a C va_list if
// varargs is non-NULL...or sourced from a block at a certain index.
// (If using varargs, then the block and the index passed in are
// used for backtrace purposes only.)  Type checking is performed.
// 
// This is the mechanism for calling Rebol functions from C (such as
// system support functions), and to implement the APPLY native:
// 
//     >> apply :append [[a b c] [d e]]
//     == [a b c d e]
// 
// Refinements are processed according to the positions in which they
// were defined in the function spec.  So for instance, /ONLY is in
// the 5th spot of APPEND (after /PART and its LIMIT):
// 
//     >> use-only: true
//     >> apply :append [[a b c] [d e] none none use-only]
//     == [a b c [d e]]
// 
// (Note: This is brittle, so it's better to use path dispatch
// when it's an option, e.g. `append/:use-only [a b c] [d e]`)
// 
// Any conditionally true value in a refinement position means the
// refinement will be considered present, while conditional
// falsehood means absent.  When the function runs, a present
// refinement's variable will hold its WORD! name unbound
// (for chaining, while still testing as TRUE?).  Absent refinements
// will be seen as NONE! from within the function body.
// 
// If 'reduce' (the default behavior of the native) then the block
// will be evaluated in steps via Do_Next_May_Throw, and the results
// passed as the arguments.  Otherwise it will be taken as literal
// values--which is the behavior of the native when /ONLY is used.
// (Using reduce is not allowed when input is coming from the
// va_list, as that would require copying them into a series first.)
// 
// * Arguments to an unused refinement will still be evaluated if
// 'reduce' is set, with the result discarded and passed as NONE!.
// 
// * The input block or varargs must fulfill the required args, and
// cannot cut off in the middle of refinement args to anything it
// has started.  But it may stop supplying arguments at the boundary
// of any transition to another refinement.
// 
// * If there are more processed values in the block than arguments,
// that will trigger an error.
// 
// * Infix functions are allowed, with the first item in the block
// presumed to be the left-hand side of the call.
// 
// The boolean result will be TRUE if an argument eval or the call
// created a THROWN() value, with the thrown value in `out`.
// Otheriwse it returns FALSE and `out` will hold the evaluation.
// 
// !!! NOTE: This code currently does not process revoking of
// refinements via unset as the path-based dispatch does in the
// Do_Core evaluator.  It probably should--though perhaps there
// should be a better sharing of implementations of the function
// dispatch in Do_Core and Apply_Block_Throws.
//
REBFLG Apply_Block_Throws(REBVAL *out, const REBVAL *func, REBSER *block, REBCNT index, REBFLG reduce, va_list *varargs)
{
    struct Reb_Call *call;

    REBVAL *param; // typeset parameter (w/symbol) in function words list
    REBVAL *arg; // value argument slot to fill in call frame for `param`

    // before refinements, in refinement, skipping...
    enum Reb_Param_Mode mode = PARAM_MODE_NORMAL;

    REBVAL *refine; // the refinement we may need to go back and revoke

    // `apply x y` behaves as if you wrote `apply/only x reduce y`.
    // Hence the following should return before erroring on too-many-args:
    //
    //     apply does [] [1 2 return 3 4]
    //
    // This flag reminds us to error *after* any reducing has finished.
    //
    REBFLG too_many = FALSE;

#if !defined(NDEBUG)
    if (varargs) assert(!reduce); // we'd have to put it in a series to reduce
#endif

    // !!! Should these be asserts?
    if (!ANY_FUNC(func)) fail (Error_Invalid_Arg(func));
    if (index > SERIES_TAIL(block)) index = SERIES_TAIL(block);

    call = Make_Call(out, block, index, NULL, func);

    assert(VAL_FUNC_NUM_PARAMS(func) == DSF_NUM_ARGS(call));

    // Get first parameter (or a REB_END if no parameters)
    if (VAL_FUNC_NUM_PARAMS(func) > 0)
        param = VAL_FUNC_PARAM(func, 1);
    else
        param = END_VALUE; // triggers `too_many` if loop is entered

    // Get slot to write actual argument for first parameter into (or NULL)
    arg = (DSF_NUM_ARGS(call) > 0) ? DSF_ARG(call, 1) : NULL;

    for (; varargs || index < BLK_LEN(block); param++, arg++) {
    no_advance:
        if (!too_many && IS_END(param)) {
            if (varargs && !va_arg(*varargs, REBVAL*)) break;

            too_many = TRUE;
            if (!reduce) break; // we can shortcut if reduce wasn't requested

            // When we have a valid place to reduce arguments into, `arg`
            // is the ideal spot to do it.  But for excess arguments we
            // need another place.  `out` is always available, so use that.
            arg = out;
        }

        if (varargs) {
            REBVAL* value = va_arg(*varargs, REBVAL*);
            if (!value) break; // our convention is "NULL signals no more"

            *arg = *value;
        }
        else if (reduce) {
            DO_NEXT_MAY_THROW(index, arg, block, index);
            if (index == THROWN_FLAG) {
                *out = *arg; // `arg` may be equal to `out` (if `too_many`)
                Free_Call(call);
                return TRUE;
            }
            if (too_many) continue; // don't test `arg` or advance `param`
        }
        else {
            assert(!too_many); // break above would have already given up
            *arg = *BLK_SKIP(block, index);
            index++;
        }

        // *** PURE LOCALS => continue ***

        while (VAL_GET_EXT(param, EXT_WORD_HIDE)) {
            // We need to skip over "pure locals", e.g. those created in
            // the spec with a SET-WORD!.  (They are useful for generators)
            //
            // The special exception is a RETURN: local, if it is "magic"
            // (e.g. FUNC and CLOS made it).  Then it gets a REBNATIVE(return)
            // that is "magic" and knows to return to *this* function...

            if (
                VAL_GET_EXT(func, EXT_FUNC_HAS_RETURN)
                && SAME_SYM(VAL_TYPESET_SYM(param), SYM_RETURN)
            ) {
                *arg = *ROOT_RETURN_NATIVE;
                VAL_FUNC_RETURN_TO(arg) = VAL_FUNC_PARAMLIST(func);
            }
            else {
            #if !defined(NDEBUG)
                // Frame used trash in debug builds
                SET_UNSET(arg);
            #endif
            }

            // We aren't actually consuming the evaluated item in the block,
            // so keep advancing the parameter as long as it's a pure local.
            param++;
            if (IS_END(param)) {
                if (reduce) {
                    // Note again that if we are reducing, we must reduce the
                    // entire block beore reporting the "too many" error!
                    too_many = TRUE;
                    goto no_advance;
                }
                fail (Error(RE_APPLY_TOO_MANY));
            }
        }

        // *** REFINEMENT => continue ***

        if (VAL_GET_EXT(param, EXT_TYPESET_REFINEMENT)) {
            // If we've found a refinement, this resets our ignore state
            // based on whether or not the arg suggests it is enabled

            refine = arg;

            if (IS_CONDITIONAL_TRUE(arg)) {
                mode = PARAM_MODE_REFINE_PENDING;

            #ifdef NDEBUG
                Val_Init_Word_Unbound(arg, REB_WORD, VAL_TYPESET_SYM(param));
            #else
                if (TYPE_CHECK(param, REB_LOGIC)) {
                    // OPTIONS_REFINEMENTS_TRUE at function create
                    SET_TRUE(arg);
                } else
                    Val_Init_Word_Unbound(
                        arg, REB_WORD, VAL_TYPESET_SYM(param)
                    );
            #endif
            }
            else {
                mode = PARAM_MODE_SKIPPING;
                SET_NONE(arg);
            }
            continue;
        }

        // *** QUOTED OR EVALUATED ITEMS ***
        // We are passing the value literally so it doesn't matter if it was
        // quoted or not in the spec...the value is taken verbatim

        if (mode == PARAM_MODE_SKIPPING) {
        #if !defined(NDEBUG)
            assert(IS_NONE(refine));

            // Unfortunately the refinement is none in this case, so to
            // figure out whether we need a NONE or an UNSET in the skipped
            // args we have to go back to the refine's param...

            if (
                TYPE_CHECK(
                    refine - DSF_ARG(call, 1) + VAL_FUNC_PARAM(func, 1),
                    REB_LOGIC
                )
            ) {
                SET_NONE(arg);
            }
            else
                SET_UNSET(arg);
        #endif

            continue;
        }

        // Verify allowed argument datatype:
        if (!TYPE_CHECK(param, VAL_TYPE(arg)))
            fail (Error_Arg_Type(call, param, Type_Of(arg)));
    }

    if (too_many)
        fail (Error(RE_APPLY_TOO_MANY));

    // Pad out any remaining parameters with unset

    while (!IS_END(param)) {
        if (VAL_GET_EXT(param, EXT_WORD_HIDE)) {
            // A true local...to be ignored as far as block args go.
            // Very likely to hit them at the end of the paramlist because
            // that's where the function generators tack on RETURN:

            if (
                VAL_GET_EXT(func, EXT_FUNC_HAS_RETURN)
                && SAME_SYM(VAL_TYPESET_SYM(param), SYM_RETURN)
            ) {
                *arg = *ROOT_RETURN_NATIVE;
                VAL_FUNC_RETURN_TO(arg) = VAL_FUNC_PARAMLIST(func);
            }
            else {
            #if !defined(NDEBUG)
                // Frame used trash in debug builds
                SET_UNSET(arg);
            #endif
            }
        }
        else if (VAL_GET_EXT(param, EXT_TYPESET_REFINEMENT)) {
            mode = PARAM_MODE_SKIPPING;
            SET_NONE(arg);
            refine = arg;
        }
        else {
            if (mode != PARAM_MODE_SKIPPING) {
                // If we aren't in ignore mode and we are dealing with
                // a non-refinement, then it's a situation of a required
                // argument missing or not all the args to a refinement given

                fail (Error_No_Arg(DSF_LABEL(call), param));
            }

        #if !defined(NDEBUG)
            assert(IS_NONE(refine));

            // Unfortunately the refinement is none in this case, so to
            // figure out whether we need a NONE or an UNSET in the skipped
            // args we have to go back to the refine's param...

            if (
                TYPE_CHECK(
                    refine - DSF_ARG(call, 1) + VAL_FUNC_PARAM(func, 1),
                    REB_LOGIC
                )
            ) {
                SET_NONE(arg);
            }
            else
                SET_UNSET(arg);
        #endif
        }

        arg++;
        param++;
    }

    // With the arguments processed and proxied into the call frame, invoke
    // the function body.

    return Dispatch_Call_Throws(call);
}


//
//  Apply_Function_Throws: C
// 
// (va_list by pointer: http://stackoverflow.com/a/3369762/211160)
// 
// Applies function from args provided by C call.  Although the
// function knows how many total args + refinements + refinement
// args it takes, it's desirable to be able to pass fewer and
// have the remainder padded with NONE!, so the convention used
// is that the varargs must be NULL-terminated.
// 
// returns TRUE if out is THROWN()
//
REBFLG Apply_Function_Throws(REBVAL *out, const REBVAL *func, va_list *varargs)
{
    REBSER *block;
    REBCNT index;

    // For debug backtracing, the evaluator wants to know what our
    // block and position are.  We have to make something up, because
    // this call is originating from C code (not Rebol code).

    if (DSF) {
        // Some function is on the stack, so fabricate our execution
        // position by copying the block and position it was at.

        block = VAL_SERIES(DSF_WHERE(DSF));
        index = VAL_INDEX(DSF_WHERE(DSF));
    }
    else if (IS_FUNCTION(func) || IS_CLOSURE(func)) {
        // Stack is empty, so offer up the body of the function itself
        // (if it has a body!)

        block = VAL_FUNC_BODY(func);
        index = 0;
    }
    else {
        // We got nothin'.  Give back the specially marked "top level"
        // empty block just to provide something in the slot
        // !!! Could use more sophisticated backtracing in general

        block = EMPTY_ARRAY;
        index = 0;
    }

    // Re-use the code powering the APPLY native for handling blocks.
    // As we pass in a non-NULL va_list*, it gets the values from there.
    //
    return Apply_Block_Throws(out, func, block, index, FALSE, varargs);
}


//
//  Apply_Func_Throws: C
// 
// Applies function from args provided by C call. Zero terminated.
// 
// returns TRUE if out is THROWN()
//
REBFLG Apply_Func_Throws(REBVAL *out, REBVAL *func, ...)
{
    REBFLG result;
    va_list args;

    va_start(args, func);
    result = Apply_Function_Throws(out, func, &args);

    // !!! An issue was uncovered here that there can be problems if a
    // failure is caused inside the Apply_Function_Throws() which does
    // a longjmp() and never returns.  The C standard is explicit that
    // you cannot dodge the call to va_end(), and that call can only be
    // in *this* function (as it passes a local variable directly, and
    // its implementation is opaque):
    //
    //    http://stackoverflow.com/a/32259710/211160
    //
    // This means either the entire Apply logic has to be duplicated
    // inside of this function with shutdowns -OR- the varargs need to
    // be unpacked into a series managed by GC or the data stack -OR-
    // the Apply logic has to be parameterized to return errors to be
    // raised and a call frame to be executed, without failing or
    // calling itself.
    //
    // On most architectures this will not be a problem, but most does
    // not mean all... so it should be tended to at some point.
    //
    va_end(args);

    return result;
}


//
//  Do_Sys_Func_Throws: C
// 
// Evaluates a SYS function and out contains the result.
//
REBFLG Do_Sys_Func_Throws(REBVAL *out, REBCNT inum, ...)
{
    REBFLG result;
    va_list args;
    REBVAL *value = FRM_VALUE(Sys_Context, inum);

    if (!ANY_FUNC(value)) fail (Error(RE_BAD_SYS_FUNC, value));

    va_start(args, inum);
    result = Apply_Function_Throws(out, value, &args);

    // !!! See notes in Apply_Func_Throws about va_end() and longjmp()
    va_end(args);

    return result;
}


//
//  Do_Construct: C
// 
// Do a block with minimal evaluation and no evaluation of
// functions. Used for things like script headers where security
// is important.
// 
// Handles cascading set words:  word1: word2: value
//
void Do_Construct(REBVAL value[])
{
    REBVAL *temp;
    REBINT ssp;  // starting stack pointer

    DS_PUSH_NONE;
    temp = DS_TOP;
    ssp = DSP;

    for (; NOT_END(value); value++) {
        if (IS_SET_WORD(value)) {
            // Next line not needed, because SET words are ALWAYS in frame.
            //if (VAL_WORD_INDEX(value) > 0 && VAL_WORD_FRAME(value) == frame)
                DS_PUSH(value);
        } else {
            // Get value:
            if (IS_WORD(value)) {
                switch (VAL_WORD_CANON(value)) {
                case SYM_NONE:
                    SET_NONE(temp);
                    break;
                case SYM_TRUE:
                case SYM_ON:
                case SYM_YES:
                    SET_TRUE(temp);
                    break;
                case SYM_FALSE:
                case SYM_OFF:
                case SYM_NO:
                    SET_FALSE(temp);
                    break;
                default:
                    *temp = *value;
                    VAL_SET(temp, REB_WORD);
                }
            }
            else if (IS_LIT_WORD(value)) {
                *temp = *value;
                VAL_SET(temp, REB_WORD);
            }
            else if (IS_LIT_PATH(value)) {
                *temp = *value;
                VAL_SET(temp, REB_PATH);
            }
            else if (VAL_TYPE(value) >= REB_NONE) { // all valid values
                *temp = *value;
            }
            else
                SET_NONE(temp);

            // Set prior set-words:
            while (DSP > ssp) {
                Set_Var(DS_TOP, temp);
                DS_DROP;
            }
        }
    }
    DS_DROP; // temp
}


//
//  Do_Min_Construct: C
// 
// Do no evaluation of the set values.
//
void Do_Min_Construct(REBVAL value[])
{
    REBVAL *temp;
    REBINT ssp;  // starting stack pointer

    DS_PUSH_NONE;
    temp = DS_TOP;
    ssp = DSP;

    for (; NOT_END(value); value++) {
        if (IS_SET_WORD(value)) {
            // Next line not needed, because SET words are ALWAYS in frame.
            //if (VAL_WORD_INDEX(value) > 0 && VAL_WORD_FRAME(value) == frame)
                DS_PUSH(value);
        } else {
            // Get value:
            *temp = *value;
            // Set prior set-words:
            while (DSP > ssp) {
                Set_Var(DS_TOP, temp);
                DS_DROP;
            }
        }
    }
    DS_DROP; // temp
}


//
//  Redo_Func_Throws: C
// 
// This code takes a call frame that has been built for one function and
// then uses it to build a call frame to call another.  (The source call
// frame is implicitly the currently running one.)
// 
// This function is currently used only by Do_Port_Action, where ACTION! (an
// archetypal function spec with no implementation) is "bounced out" of
// the C code into user code.  All the other ACTION!-handling code is
// implemented in C with switch statements.
// 
// The archetypal frame cannot be used directly, because it can be different.
// That difference could be as simple as `foo: action [port [port!]]` vs.
// `foo-impl: func [port [port!] /local x y z]`.  However, other tricks
// are allowed--such as for the implementation to offer refinements which
// were not present in the archetype.
// 
// Returns TRUE if result is THROWN()
//
REBFLG Redo_Func_Throws(REBVAL *func_val)
{
    REBSER *paramlist_src = VAL_FUNC_PARAMLIST(DSF_FUNC(DSF));
    REBSER *paramlist_new = VAL_FUNC_PARAMLIST(func_val);
    REBCNT isrc;        // index position in source frame
    REBCNT inew;        // index position in target frame
    REBVAL *param;
    REBVAL *param2;

    struct Reb_Call *call;
    REBVAL *arg;

    // As part of the "Redo" we are not adding a new function location,
    // label, or place to write the output.  We are substituting new code
    // and perhaps adjusting the arguments in our re-doing call.

    call = Make_Call(
        DSF_OUT(DSF),
        VAL_SERIES(DSF_WHERE(DSF)),
        VAL_INDEX(DSF_WHERE(DSF)),
        DSF_LABEL(DSF),
        func_val
    );

    // Foreach arg of the target, copy to source until refinement.
    arg = DSF_ARG(call, 1);
    isrc = inew = FIRST_PARAM_INDEX;

    for (; inew < BLK_LEN(paramlist_new); inew++, isrc++, arg++) {
        param = BLK_SKIP(paramlist_new, inew);
        assert(IS_TYPESET(param));

        if (VAL_GET_EXT(param, EXT_WORD_HIDE)) {
            if (
                VAL_GET_EXT(func_val, EXT_FUNC_HAS_RETURN)
                && SAME_SYM(VAL_TYPESET_SYM(param), SYM_RETURN)
            ) {
                // This pure local is a special magic "definitional return"
                // (see comments on VAL_FUNC_RETURN_TO)
                *arg = *ROOT_RETURN_NATIVE;
                VAL_FUNC_RETURN_TO(arg) = VAL_FUNC_PARAMLIST(func_val);
            }
            else {
                // This pure local is not special, so leave as UNSET
            }
            continue;
        }

        if (isrc >= BLK_LEN(paramlist_src)) {
            isrc = BLK_LEN(paramlist_src);
            param2 = NULL;
        }
        else {
            param2 = BLK_SKIP(paramlist_src, isrc);
            assert(IS_TYPESET(param2));
        }

        if (VAL_GET_EXT(param, EXT_TYPESET_REFINEMENT)) {
            // At refinement, search for it in source, then continue with words.

            // Are we aligned on the refinement already? (a common case)
            if (
                param2
                && VAL_GET_EXT(param2, EXT_TYPESET_REFINEMENT)
                && (
                    VAL_TYPESET_CANON(param2)
                    == VAL_TYPESET_CANON(param)
                )
            ) {
                *arg = *DSF_ARG(DSF, isrc);
            }
            else {
                // No, we need to search for it:
                isrc = FIRST_PARAM_INDEX;
                for (; isrc < BLK_LEN(paramlist_src); isrc++) {
                    param2 = BLK_SKIP(paramlist_src, isrc);
                    if (
                        VAL_GET_EXT(param2, EXT_TYPESET_REFINEMENT)
                        && (
                            VAL_TYPESET_CANON(param2)
                            == VAL_TYPESET_CANON(param)
                        )
                    ) {
                        *arg = *DSF_ARG(DSF, isrc);
                        break;
                    }
                }
                // !!! The function didn't have the refinement so skip
                // it and leave as unset.
                // But what will happen now with the arguments?
                /* if (isrc >= BLK_LEN(wsrc)) fail (Error_Invalid_Arg(word)); */
            }
        }
        else {
            if (
                param2
                && (
                    VAL_GET_EXT(param, EXT_TYPESET_QUOTE)
                    == VAL_GET_EXT(param2, EXT_TYPESET_QUOTE)
                ) && (
                    VAL_GET_EXT(param, EXT_TYPESET_EVALUATE)
                    == VAL_GET_EXT(param2, EXT_TYPESET_EVALUATE)
                )
            ) {
                *arg = *DSF_ARG(DSF, isrc);
                // !!! Should check datatypes for new arg passing!
            }
            else {
                // !!! Why does this allow the bounced-to function to have
                // a different type, and be unset instead of erroring?

                // !!! Technically speaking this should honor the
                // LEGACY(OPTIONS_REFINEMENTS_TRUE) switch and SET_NONE if so,
                // but that would apply to a very small amount of "third
                // party actor code" from R3-Alpha.  Both the actor code and
                // this branch are suspect in terms of long term value, so
                // we just leave it as unset.
            }
        }
    }

    return Dispatch_Call_Throws(call);
}


//
//  Get_Simple_Value_Into: C
// 
// Does easy lookup, else just returns the value as is.
//
void Get_Simple_Value_Into(REBVAL *out, const REBVAL *val)
{
    if (IS_WORD(val) || IS_GET_WORD(val)) {
        GET_VAR_INTO(out, val);
    }
    else if (IS_PATH(val) || IS_GET_PATH(val)) {
        // !!! Temporary: make a copy to pass mutable value to Do_Path
        REBVAL path = *val;
        const REBVAL *v = &path;
        Do_Path(out, &v, 0);
    }
    else {
        *out = *val;
    }
}


//
//  Resolve_Path: C
// 
// Given a path, return a context and index for its terminal.
//
REBSER *Resolve_Path(REBVAL *path, REBCNT *index)
{
    REBVAL *sel; // selector
    const REBVAL *val;
    REBSER *blk;
    REBCNT i;

    if (VAL_TAIL(path) < 2) return 0;
    blk = VAL_SERIES(path);
    sel = BLK_HEAD(blk);
    if (!ANY_WORD(sel)) return 0;
    val = GET_VAR(sel);

    sel = BLK_SKIP(blk, 1);
    while (TRUE) {
        if (!ANY_OBJECT(val) || !IS_WORD(sel)) return 0;
        i = Find_Word_Index(VAL_OBJ_FRAME(val), VAL_WORD_SYM(sel), FALSE);
        sel++;
        if (IS_END(sel)) {
            *index = i;
            return VAL_OBJ_FRAME(val);
        }
    }

    return 0; // never happens
}
