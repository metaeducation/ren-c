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


//
//  Eval_Depth: C
//
REBINT Eval_Depth(void)
{
    REBINT depth = 0;
    struct Reb_Call *call = DSF;

    for (; call != NULL; call = PRIOR_DSF(call), depth++)
        NOOP;

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

void Trace_Line(
    union Reb_Call_Source source,
    REBIXO indexor,
    const REBVAL *value
) {
    int depth;

    if (GET_FLAG(Trace_Flags, 1)) return; // function
    if (ANY_FUNC(value)) return;

    CHECK_DEPTH(depth);

    if (indexor == END_FLAG) {
        Debug_Fmt_("END_FLAG...");
    }
    else if (indexor == VARARGS_FLAG) {
        Debug_Fmt_("VARARGS_FLAG...");
    }
    else {
        REBCNT index = cast(REBCNT, indexor);
        Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,1)), index, value);
    }

    if (IS_WORD(value) || IS_GET_WORD(value)) {
        value = GET_OPT_VAR_MAY_FAIL(value);
        if (VAL_TYPE(value) < REB_NATIVE)
            Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,2)), value);
        else if (VAL_TYPE(value) >= REB_NATIVE && VAL_TYPE(value) <= REB_FUNCTION) {
            REBARR *words = List_Func_Words(value);
            Debug_Fmt_(
                cs_cast(BOOT_STR(RS_TRACE,3)), Get_Type_Name(value), words
            );
            Free_Array(words);
        }
        else
            Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,4)), Get_Type_Name(value));
    }
    /*if (ANY_WORD(value)) {
        word = value;
        if (IS_WORD(value)) value = GET_OPT_VAR_MAY_FAIL(word);
        Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,2)), VAL_WORD_CONTEXT(word), VAL_WORD_INDEX(word), Get_Type_Name(value));
    }
    if (Trace_Stack) Debug_Fmt(cs_cast(BOOT_STR(RS_TRACE,3)), DSP, DSF);
    else
    */
    Debug_Line();
}

void Trace_Func(REBCNT label_sym, const REBVAL *value)
{
    int depth;
    CHECK_DEPTH(depth);
    Debug_Fmt_(
        cs_cast(BOOT_STR(RS_TRACE,5)),
        Get_Sym_Name(label_sym),
        Get_Type_Name(value)
    );
    if (GET_FLAG(Trace_Flags, 1))
        Debug_Values(DSF_ARG(DSF, 1), DSF_ARGC(DSF), 20);
    else Debug_Line();
}

void Trace_Return(REBCNT label_sym, const REBVAL *value)
{
    int depth;
    CHECK_DEPTH(depth);
    Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,6)), Get_Sym_Name(label_sym));
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
    Debug_Fmt(
        cs_cast(BOOT_STR(RS_TRACE, 11)),
        &VAL_ERR_VALUES(value)->type,
        &VAL_ERR_VALUES(value)->id
    );
}


//
//  Next_Path_Throws: C
// 
// Evaluate next part of a path.
//
REBOOL Next_Path_Throws(REBPVS *pvs)
{
    REBVAL *path;
    REBPEF func;

    REBVAL temp;
    VAL_INIT_WRITABLE_DEBUG(&temp);

    // Path must have dispatcher, else return:
    func = Path_Dispatch[VAL_TYPE_0(pvs->value)];
    if (!func) return FALSE; // unwind, then check for errors

    pvs->path++;

    //Debug_Fmt("Next_Path: %r/%r", pvs->path-1, pvs->path);

    // object/:field case:
    if (IS_GET_WORD(path = pvs->path)) {
        pvs->select = GET_MUTABLE_VAR_MAY_FAIL(path);
        if (IS_UNSET(pvs->select))
            fail (Error(RE_NO_VALUE, path));
    }
    // object/(expr) case:
    else if (IS_GROUP(path)) {
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
//  Do_Path_Throws: C
// 
// Evaluate a path value, given the first value in that path's series.  This
// evaluator may throw because parens are evaluated, e.g. `foo/(throw 1020)`
//
// If label_sym is passed in as being non-null, then the caller is implying
// readiness to process a path which may be a function with refinements.
// These refinements will be left in order on the data stack in the case
// that `out` comes back as ANY_FUNC().
//
// If a `val` is provided, it is assumed to be a set-path and is set to that
// value IF the path evaluation did not throw or error.  HOWEVER the set value
// is NOT put into `out`.  This provides more flexibility on performance in
// the evaluator, which may already have the `val` where it wants it, and
// so the extra assignment would just be overhead.
//
REBOOL Do_Path_Throws(REBVAL *out, REBCNT *label_sym, const REBVAL *path, REBVAL *val)
{
    REBPVS pvs;
    REBDSP dsp_orig = DSP;

    assert(ANY_PATH(path));

    // !!! There is a bug in the dispatch such that if you are running a
    // set path, it does not always assign the output, because it "thinks you
    // aren't going to look at it".  This presumably originated from before
    // parens were allowed in paths, and neglects cases like:
    //
    //     foo/(throw 1020): value
    //
    // We always have to check to see if a throw occurred.  Until this is
    // streamlined, we have to at minimum set it to something that is *not*
    // thrown so that we aren't testing uninitialized memory.  A safe trash
    // will do, which is unset in release builds.
    //
    if (val)
        SET_TRASH_SAFE(out);

    // None of the values passed in can live on the data stack, because
    // they might be relocated during the path evaluation process.
    //
    assert(!IN_DATA_STACK(out));
    assert(!IN_DATA_STACK(path));
    assert(!val || !IN_DATA_STACK(val));

    // Not currently robust for reusing passed in path or value as the output
    assert(out != path && out != val);

    assert(!val || !THROWN(val));

    pvs.setval = val;       // Set to this new value
    pvs.store = out;        // Space for constructed results

    // Get first block value:
    pvs.orig = path;
    pvs.path = VAL_ARRAY_AT(pvs.orig);

    // Lookup the value of the variable:
    if (IS_WORD(pvs.path)) {
        pvs.value = GET_MUTABLE_VAR_MAY_FAIL(pvs.path);
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
    else if (Path_Dispatch[VAL_TYPE_0(pvs.value)]) {
        REBOOL threw = Next_Path_Throws(&pvs);

        // !!! See comments about why the initialization of out is necessary.
        // Without it this assertion can change on some things:
        //
        //     t: now
        //     t/time: 10:20:03
        //
        // (It thinks pvs.value has its THROWN bit set when it completed
        // successfully.  It was a PE_USE case where pvs.value was reset to
        // pvs.store, and pvs.store has its thrown bit set.  Valgrind does not
        // catch any uninitialized variables.)
        //
        // There are other cases that do trip valgrind when omitting the
        // initialization, though not as clearly reproducible.
        //
        assert(threw == THROWN(pvs.value));

        if (threw) return TRUE;

        // Check for errors:
        if (NOT_END(pvs.path + 1) && !ANY_FUNC(pvs.value)) {
            // Only function refinements should get by this line:
            fail (Error(RE_INVALID_PATH, pvs.orig, pvs.path));
        }
    }
    else if (!ANY_FUNC(pvs.value))
        fail (Error(RE_BAD_PATH_TYPE, pvs.orig, Type_Of(pvs.value)));

    if (val) {
        // If SET then we don't return anything
        assert(IS_END(pvs.path) + 1);
        return FALSE;
    }

    // If storage was not used, then copy final value back to it:
    if (pvs.value != pvs.store) *pvs.store = *pvs.value;

    assert(!THROWN(out));

    // Return 0 if not function or is :path/word...
    if (!ANY_FUNC(pvs.value)) {
        assert(IS_END(pvs.path) + 1);
        return FALSE;
    }

    if (label_sym) {
        REBVAL refinement;
        VAL_INIT_WRITABLE_DEBUG(&refinement);

        // When a function is hit, path processing stops as soon as the
        // processed sub-path resolves to a function. The path is still sitting
        // on the position of the last component of that sub-path. Usually,
        // this last component in the sub-path is a word naming the function.
        //
        if (IS_WORD(pvs.path)) {
            *label_sym = VAL_WORD_SYM(pvs.path);
        }
        else {
            // In rarer cases, the final component (completing the sub-path to
            // the function to call) is not a word. Such as when you use a path
            // to pick by index out of a block of functions:
            //
            //      functions: reduce [:add :subtract]
            //      functions/1 10 20
            //
            // Or when you have an immediate function value in a path with a
            // refinement. Tricky to make, but possible:
            //
            //      do reduce [
            //          to-path reduce [:append 'only] [a] [b]
            //      ]
            //

            // !!! When a function was not invoked through looking up a word
            // (or a word in a path) to use as a label, there were once three
            // different alternate labels used.  One was SYM__APPLY_, another
            // was ROOT_NONAME, and another was to be the type of the function
            // being executed.  None are fantastic, we do the type for now.

            *label_sym = SYM_FROM_KIND(VAL_TYPE(pvs.value));
        }

        // Move on to the refinements (if any)
        pvs.path = pvs.path + 1;

        // !!! Currently, the mainline path evaluation "punts" on refinements.
        // When it finds a function, it stops the path evaluation and leaves
        // the position pvs.path before the list of refinements.
        //
        // A more elegant solution would be able to process and notice (for
        // instance) that `:APPEND/ONLY` should yield a function value that
        // has been specialized with a refinement.  Path chaining should thus
        // be able to effectively do this and give the refined function object
        // back to the evaluator or other client.
        //
        // If a label_sym is passed in, we recognize that a function dispatch
        // is going to be happening.  We do not want to pay to generate the
        // new series that would be needed to make a temporary function that
        // will be invoked and immediately GC'd  So we gather the refinements
        // on the data stack.
        //
        // This code simulates that path-processing-to-data-stack, but it
        // should really be something in dispatch iself.  In any case, we put
        // refinements on the data stack...and caller knows refinements are
        // from dsp_orig to DSP (thanks to accounting, all other operations
        // should balance!)

        for (; NOT_END(pvs.path); pvs.path++) { // "the refinements"
            if (IS_NONE(pvs.path)) continue;

            if (IS_GROUP(pvs.path)) {
                // Note it is not legal to use the data stack directly as the
                // output location for a DO (might be resized)

                if (DO_ARRAY_THROWS(&refinement, pvs.path)) {
                    *out = refinement;
                    DS_DROP_TO(dsp_orig);
                    return TRUE;
                }
                if (IS_NONE(&refinement)) continue;
                DS_PUSH(&refinement);
            }
            else if (IS_GET_WORD(pvs.path)) {
                DS_PUSH_TRASH;
                *DS_TOP = *GET_OPT_VAR_MAY_FAIL(pvs.path);
                if (IS_NONE(DS_TOP)) {
                    DS_DROP;
                    continue;
                }
            }
            else
                DS_PUSH(pvs.path);

            // Whatever we were trying to use as a refinement should now be
            // on the top of the data stack, and only words are legal ATM
            //
            if (!IS_WORD(DS_TOP))
                fail (Error(RE_BAD_REFINE, DS_TOP));

            // Go ahead and canonize the word symbol so we don't have to
            // do it each time in order to get a case-insenstive compare
            //
            INIT_WORD_SYM(DS_TOP, SYMBOL_TO_CANON(VAL_WORD_SYM(DS_TOP)));
        }

        // To make things easier for processing, reverse the refinements on
        // the data stack (we needed to evaluate them in forward order).
        // This way we can just pop them as we go, and know if they weren't
        // all consumed if it doesn't get back to `dsp_orig` by the end.

        if (dsp_orig != DSP) {
            REBVAL *bottom = DS_AT(dsp_orig + 1);
            REBVAL *top = DS_TOP;
            while (top > bottom) {
                refinement = *bottom;
                *bottom = *top;
                *top = refinement;

                top--;
                bottom++;
            }
        }
    }
    else {
        // !!! Historically this just ignores a result indicating this is a
        // function with refinements, e.g. ':append/only'.  However that
        // ignoring seems unwise.  It should presumably create a modified
        // function in that case which acts as if it has the refinement.
        //
        // If the caller did not pass in a label pointer we assume they are
        // likely not ready to process any refinements.
        //
        if (NOT_END(pvs.path + 1))
            fail (Error(RE_TOO_LONG)); // !!! Better error or add feature
    }

    return FALSE;
}


//
//  Pick_Path: C
// 
// Lightweight version of Do_Path used for A_PICK actions.
// Does not do GROUP! evaluation, hence not designed to throw.
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
    func = Path_Dispatch[VAL_TYPE_0(value)];
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
//  Do_Signals_Throws: C
// 
// Special events to process periodically during evaluation. Search for
// SET_SIGNAL to find them.  (Note: Not to be confused with SIGINT and unix
// signals, although possibly triggered by one.)
//
// Currently the ability of a signal to THROW comes from the processing of
// breakpoints.  The RESUME instruction is able to execute code with /DO,
// and that code may escape the
//
REBOOL Do_Signals_Throws(REBVAL *out)
{
    struct Reb_State state;
    REBCTX *error;

    REBCNT sigs;
    REBCNT mask;

    assert(Saved_State || PG_Boot_Phase < BOOT_MEZZ);

    // Accumulate evaluation counter and reset countdown:
    if (Eval_Count <= 0) {
        //Debug_Num("Poll:", (REBINT) Eval_Cycles);
        Eval_Cycles += Eval_Dose - Eval_Count;
        Eval_Count = Eval_Dose;
        if (Eval_Limit != 0 && Eval_Cycles > Eval_Limit)
            Check_Security(SYM_EVAL, POL_EXEC, 0);
    }

    if (!(Eval_Signals & Eval_Sigmask)) {
        SET_UNSET(out);
        return FALSE;
    }

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

    // Breaking only allowed after MEZZ boot
    //
    if (GET_FLAG(sigs, SIG_INTERRUPT) && PG_Boot_Phase >= BOOT_MEZZ) {
        CLR_SIGNAL(SIG_INTERRUPT);
        Eval_Sigmask = mask;

        if (Do_Breakpoint_Throws(out, TRUE, UNSET_VALUE, FALSE))
            return TRUE;

        return FALSE;
    }

    // Halting only allowed after MEZZ boot
    //
    if (GET_FLAG(sigs, SIG_HALT) && PG_Boot_Phase >= BOOT_MEZZ) {
        CLR_SIGNAL(SIG_HALT);
        Eval_Sigmask = mask;

        fail (VAL_CONTEXT(TASK_HALT_ERROR));
    }

    Eval_Sigmask = mask;

    SET_UNSET(out);
    return FALSE;
}


#if !defined(NDEBUG)

#include "debugbreak.h"


//
//  Trace_Fetch_Debug: C
//
// When down to the wire and wanting to debug the evaluator, it can be very
// useful to see the steps of the states it's going through to see what is
// wrong.  This routine hooks the individual fetch and writes at a more
// fine-grained level than a breakpoint at each DO/NEXT point.
//
void Trace_Fetch_Debug(const char* msg, struct Reb_Call *c, REBOOL after) {
    if (c->do_count == 0) {
        Debug_Fmt("%d - %s : %s", c->indexor, msg, after ? "AFTER" : "BEFORE");
        assert(c->value != NULL || (after && c->indexor == END_FLAG));
        if (c->value)
            PROBE(c->value);
    }
}


//
// The entry checks to DO are for verifying that the setup of the Reb_Call
// passed in was valid.  They run just once for each Do_Core() call, and
// are only in the debug build.
//
static REBCNT Do_Entry_Checks_Debug(struct Reb_Call *c)
{
    // The caller must preload ->value with the first value to process.  It
    // may be resident in the array passed that will be used to fetch further
    // values, or it may not.
    //
    assert(c->value);

    // All callers should ensure that the type isn't an END marker before
    // bothering to invoke Do_Core().
    //
    assert(NOT_END(c->value));

    // Though we can protect the value written into the target pointer 'out'
    // from GC during the course of evaluation, we can't protect the
    // underlying value from relocation.  Technically this would be a problem
    // for any series which might be modified while this call is running, but
    // most notably it applies to the data stack--where output used to always
    // be returned.
    //
#ifdef STRESS_CHECK_DO_OUT_POINTER
    ASSERT_NOT_IN_SERIES_DATA(c->out);
#else
    assert(!IN_DATA_STACK(c->out));
#endif

    // The DO_FLAGs were decided to come in pairs for clarity, to make sure
    // that each callsite of the core routines was clear on what it was
    // asking for.  This may or may not be overkill long term, but helps now.
    //
    assert(
        LOGICAL(c->flags & DO_FLAG_NEXT)
        != LOGICAL(c->flags & DO_FLAG_TO_END)
    );
    assert(
        LOGICAL(c->flags & DO_FLAG_LOOKAHEAD)
        != LOGICAL(c->flags & DO_FLAG_NO_LOOKAHEAD)
    );
    assert(
        LOGICAL(c->flags & DO_FLAG_EVAL_NORMAL)
        != LOGICAL(c->flags & DO_FLAG_EVAL_ONLY)
    );

    // This flag is managed solely by the frame code; shouldn't come in set
    //
    assert(NOT(c->flags & DO_FLAG_FRAME_CONTEXT));

#if !defined(NDEBUG)
    //
    // This has to be nulled out in the debug build by the code itself inline,
    // because sometimes one stackvars call ends and then another starts
    // before the debug preamble is run.  Give it an initial NULL here though.
    //
    c->frame.stackvars = NULL;
#endif

    // Snapshot the "tick count" to assist in showing the value of the tick
    // count at each level in a stack, so breakpoints can be strategically
    // set for that tick based on higher levels than the value you might
    // see during a crash.
    //
    c->do_count = TG_Do_Count;
    return c->do_count;
}


//
// The iteration preamble takes care of clearing out variables and preparing
// the state for a new "/NEXT" evaluation.  It's a way of ensuring in the
// debug build that one evaluation does not leak data into the next, and
// making the code shareable allows code paths that jump to later spots
// in the switch (vs. starting at the top) to reuse the work.
//
static REBCNT Do_Evaluation_Preamble_Debug(struct Reb_Call *c) {
    //
    // The ->mode is examined by parts of the system as a sign of whether
    // the stack represents a function invocation or not.  If it is changed
    // from CALL_MODE_GUARD_ARRAY_ONLY during an evaluation step, it must
    // be changed back before a next step is to run.
    //
    assert(c->mode == CALL_MODE_GUARD_ARRAY_ONLY);

    // We should not be linked into the call stack when a function is not
    // running (it is not if we're in this outer loop)
    //
    assert(c != CS_Running);

    // We checked for END when we entered Do_Core() and short circuited
    // that, but if we're running DO_FLAG_TO_END then the catch for that is
    // an index check.  We shouldn't go back and `do_at_index` on an end!
    //
    assert(c->value && NOT_END(c->value));
    assert(c->indexor != THROWN_FLAG);

    // Note that `c->indexor` *might* be END_FLAG in the case of an eval;
    // if you write `do [eval help]` then it will load help in as c->value
    // and retrigger, and `help` (for instance) is capable of handling a
    // prefetched input that is at end.  This is different from most cases
    // where END_FLAG directly implies prefetch input was exhausted and
    // c->value must be NULL.
    //
    assert(c->indexor != END_FLAG || IS_END(c->eval_fetched));

    // The value we are processing should not be THROWN() and any series in
    // it should be under management by the garbage collector.
    //
    // !!! THROWN() bit on individual values is in the process of being
    // deprecated, in favor of the evaluator being in a "throwing state".
    //
    assert(!THROWN(c->value));
    ASSERT_VALUE_MANAGED(c->value);

    // Trash call variables in debug build to make sure they're not reused.
    // Note that this call frame will *not* be seen by the GC unless it gets
    // chained in via a function execution, so it's okay to put "non-GC safe"
    // trash in at this point...though by the time of that call, they must
    // hold valid values.
    //
    c->func = cast(REBFUN*, 0xDECAFBAD);
    c->label_sym = SYM_0;
    c->label_str = "(no current label)";
    c->param = cast(REBVAL*, 0xDECAFBAD);
    c->arg = cast(REBVAL*, 0xDECAFBAD);
    c->refine = cast(REBVAL*, 0xDECAFBAD);

    // This counter is helpful for tracking a specific invocation.
    // If you notice a crash, look on the stack for the topmost call
    // and read the count...then put that here and recompile with
    // a breakpoint set.  (The 'TG_Do_Count' value is captured into a
    // local 'count' so you still get the right count after recursion.)
    //
    // We bound it at the max unsigned 32-bit because otherwise it would
    // roll over to zero and print a message that wasn't asked for, which
    // is annoying even in a debug build.
    //
    if (TG_Do_Count < MAX_U32) {
        c->do_count = ++TG_Do_Count;
        if (c->do_count ==
            // *** DON'T COMMIT THIS v-- KEEP IT AT ZERO! ***
                                      0
            // *** DON'T COMMIT THIS --^ KEEP IT AT ZERO! ***
        ) {
            if (c->indexor == VARARGS_FLAG) {
                //
                // !!! Can't fetch the next value here without destroying the
                // forward iteration.  Destructive debugging techniques could
                // be added here on demand, or non-destructive ones that
                // logged the va_list into a dynamically allocated array
                // could be put in the debug build, etc.  Add when necessary.
                //
                Debug_Fmt(
                    "Do_Core() count trap (va_list, no nondestructive fetch)"
                );
            }
            else if (c->indexor == END_FLAG) {
                assert(c->value != NULL);
                Debug_Fmt("Performing EVAL at end of array (no args)");
                PROBE_MSG(c->value, "Do_Core() count trap");
            }
            else {
                REBVAL dump;
                VAL_INIT_WRITABLE_DEBUG(&dump);

                assert(cast(REBCNT, c->indexor) > 0);
                PROBE_MSG(c->value, "Do_Core() count trap");
                Val_Init_Block_Index(&dump, c->source.array, c->indexor - 1);
                PROBE_MSG(&dump, "Do_Core() next up...");
            }
        }
    }

    return c->do_count;
}

// Needs a forward declaration, but makes more sense to locate after Do_Core()
//
static void Do_Exit_Checks_Debug(struct Reb_Call *c);

#endif


// Simple macro for wrapping (but not obscuring) a `goto` in the code below
//
#define NOTE_THROWING(g) \
    do { \
        assert(c->indexor == THROWN_FLAG); \
        assert(THROWN(c->out)); \
        g; /* goto statement left at callsite for readability */ \
    } while(0)


//
//  Do_Core: C
// 
// Evaluate the code block until we have:
//
//     1. An irreducible value (return next index)
//     2. Reached the end of the block (return END_FLAG)
//     3. Encountered an error
//
// For comprehensive notes on the input parameters, output parameters, and
// internal state variables...see %sys-do.h and `struct Reb_Call`.
// 
// NOTES:
//
// 1. This is a very long routine.  That is largely on purpose, because it
//    does not contain repeated portions...and is a critical performance
//    bottleneck in the system.  So dividing it for the sake of "having
//    more functions" wouldn't be a good idea, especially since the target
//    is compilers that are so simple they may not have proper inlining
//    (which is only a "hint" to the compiler even if it's supported).
//
// 2. Changing the behavior of the parameter fulfillment in this core routine
//    generally also means changes to two other semi-parallel routines:
//    `Apply_Block_Throws()` and `Redo_Func_Throws().`  Review the impacts
//    of any changes on all three.
//
// The evaluator only moves forward, and it consumes exactly one element from
// the input at a time.  This input may be a source where the index needs
// to be tracked and care taken to contain the index within its boundaries
// in the face of change (e.g. a mutable ARRAY).  Or it may be an entity
// which tracks its own position on each fetch and where it is immutable,
// where the "index" is serving as a flag and should be left static.
//
// !!! There is currently no "locking" or other protection on the arrays that
// are in the call stack and executing.  Each iteration must be prepared for
// the case that the array has been modified out from under it.  The code
// evaluator will not crash, but re-fetches...ending the evaluation if the
// array has been shortened to before the index, and using possibly new
// values.  The benefits of this self-modifying lenience should be reviewed
// to inform a decision regarding the locking of arrays during evaluation.
//
void Do_Core(struct Reb_Call * const c)
{
#if !defined(NDEBUG)
    //
    // Debug reuses PUSH_TRAP's snapshotting to check for leaks on each step
    //
    struct Reb_State state;

    // Cache of `c->do_count` (to make it quicker to see in the debugger)
    //
    REBCNT do_count;
#endif

    // It's necessary to track the running call frame (or is it?  could it
    // be searched and found?  natives have access as a parameter...)
    //
    struct Reb_Call *call_orig;

    // Definitional Return gives back a "corrupted" REBVAL of a return native,
    // whose body is actually an indicator of the return target.  The
    // Reb_Call only stores the FUNC so we must extract this body from the
    // value if it represents a exit_from
    //
    REBARR *exit_from;

    // See notes below on reference for why this is needed to implement eval.
    //
    REBOOL eval_normal; // EVAL/ONLY can trigger this to FALSE
    REBVAL eval;
    VAL_INIT_WRITABLE_DEBUG(&eval);

    // Check just once (stack level would be constant if checked in a loop)
    //
    if (C_STACK_OVERFLOWING(&c)) Trap_Stack_Overflow();

    // Chain the call state into the stack, and mark it as generally not
    // having valid fields to GC protect (more valid during function calls).
    //
    c->prior = TG_Do_Stack;
    TG_Do_Stack = c;
    c->mode = CALL_MODE_GUARD_ARRAY_ONLY;

#if !defined(NDEBUG)
    //
    // With TG_Do_Stack set up, we can snapshot the state that we'll be
    // checking for the balance of after each iteration.  This has a lot in
    // common with what's done during a PUSH_TRAP for restoring state in
    // errors, so code is shared...see %sys-state.h
    //
    SNAP_STATE(&state);

    // Entry verification checks, to make sure the caller provided good
    // parameters.  (Broken into separate routine
    //
    do_count = Do_Entry_Checks_Debug(c);
#endif

    // Capture the data stack pointer on entry (used by debug checks, but
    // also refinements are pushed to stack and need to be checked if there
    // are any that are not processed)
    //
    c->dsp_orig = DSP;

    // Indicate that we do not have a value already fetched by eval which is
    // pending to be the next fetch (after the eval's "slipstreamed" c->value
    // is done processing).
    //
    c->eval_fetched = NULL;

    // Write some garbage (that won't crash the GC) into the `out` slot in
    // the debug build.  We will assume that as the evaluator runs
    // it will never put anything in out that will crash the GC.
    //
    // !!! See remarks below which seem to think it necessary to do this
    // on each iteration...research why this was being done 3 times (?!)
    //
    /*SET_TRASH_SAFE(c->out);*/

value_ready_for_do_next:
    //
    // c->value is expected to be set here, as is c->index
    //
    // !!! are there more rules for the locations value can't point to?
    // Note that a fetched value pointer may be within a va_arg list.  Also
    // consider the GC implications of running ANY non-EVAL/ONLY scenario;
    // how do you know the values are safe?  (See ideas in %sys-do.h)
    //
    assert(c->value && !IS_END(c->value) && c->value != c->out);
    assert(c->indexor != END_FLAG && c->indexor != THROWN_FLAG);

    if (Trace_Flags) Trace_Line(c->source, c->indexor, c->value);

    // Save the index at the start of the expression in case it is needed
    // for error reporting.  DSF_INDEX can account for prefetching, but it
    // cannot know what a preloaded head value was unless it was saved
    // under a debug> mode.
    //
    if (c->indexor != VARARGS_FLAG) c->expr_index = c->indexor;

    // Make sure `eval` is trash in debug build if not doing a `reevaluate`.
    // It does not have to be GC safe (for reasons explained below).  We
    // also need to reset evaluation to normal vs. a kind of "inline quoting"
    // in case EVAL/ONLY had enabled that.
    //
    SET_TRASH_IF_DEBUG(&eval);
    eval_normal = LOGICAL(c->flags & DO_FLAG_EVAL_NORMAL);

    // If we're going to jump to the `reevaluate:` label below we should not
    // consider it a Recycle() opportunity.  The value residing in `eval`
    // is a local variable unseen by the GC *by design*--to avoid having
    // to initialize it or GC-safe de-initialize it each time through
    // the evaluator loop.  It will only be protected by the GC under
    // circumstances that wind up extracting its properties during a needed
    // evaluation (hence protected indirectly via `c->array` or `c->func`.)
    //
    if (--Eval_Count <= 0 || Eval_Signals) {
        //
        // Note that Do_Signals_Throws() may do a recycle step of the GC, or
        // it may spawn an entire interactive debugging session via
        // breakpoint before it returns.  It may also FAIL and longjmp out.
        //
        if (Do_Signals_Throws(c->out)) {
            c->indexor = THROWN_FLAG;
            NOTE_THROWING(goto return_indexor);
        }

        if (!IS_UNSET(c->out)) {
            //
            // !!! What to do with something like a Ctrl-C-based breakpoint
            // session that does something like `resume/with 10`?  We are
            // "in-between" evaluations, so that 10 really has no meaning
            // and is just going to get discarded.  FAIL for now to alert
            // the user that something is off, but perhaps the failure
            // should be contained in a sandbox and restart the break?
            //
            fail (Error(RE_MISC));
        }
    }

reevaluate:
    //
    // ^-- See why reevaluate must jump to be *after* a potentical GC point.
    // (We also want the debugger to consider the triggering EVAL as the
    // start of the expression, and don't want to advance `expr_index`).

    // Although in the outer loop this call frame is not threaded into the
    // call stack, the `out` value may be shared.  So it could be a value
    // pointed to at another level which *is* on the stack.  Hence we must
    // always write safe trash into it for this debug check.
    //
    // !!! REVIEW: This was overruling an earlier bit of writing that said:
    // "Every other call in the debug build we put safe trash in the output
    // slot.  This gives a spread of release build behavior (trusting that
    // the out value is GC safe from previous evaluations)...as well as a
    // debug behavior to catch those trying to inspect what is conceptually
    // supposed to be a garbage value."  Determine what the exact meaning
    // of this was, and why it had two contradicting calls.
    //
    /*if (SPORADICALLY(2)) SET_TRASH_SAFE(c->out);*/
    SET_TRASH_SAFE(c->out);

    // The "Preamble" is only in debug builds, and it is what trashes the
    // various state variables in order to ensure that one iteration of Do
    // doesn't leak its state to be observed by the next.
    //
    // (It also allows one to catch a certain tick count in the evaluator
    // with a probe and a breakpoint opportunity.)
    //
#if !defined(NDEBUG)
    do_count = Do_Evaluation_Preamble_Debug(c);
    cast(void, do_count); // suppress unused warning

    exit_from = cast(REBARR*, 0xDECAFBAD);
#endif

    switch (VAL_TYPE(c->value)) {
    //
    // [BAR! and LIT-BAR!]
    //
    // If an expression barrier is seen in between expressions, it is an
    // interstitial and is skipped.  It doesn't "count", so a DO/NEXT will
    // pass by as many REB_BARs as it has to in order to get to a "real"
    // evaluation point (or, to the end).
    //
    // It wipes the slate of any previous evaluations by clearing c->out.
    //
    // LIT-BAR! is the only way to pass a BAR!, because BAR! cannot be
    // picked up by quoting--even hard quoting.  It decays into an ordinary
    // BAR! as part of its evaluation, but in that decay it can suitably
    // be passed as a parameter.
    //
    case REB_BAR:
        SET_UNSET(c->out);
        FETCH_NEXT_ONLY_MAYBE_END(c);
        if (c->indexor != END_FLAG)
            goto value_ready_for_do_next; // keep going, even if [| | | ...]
        break;

    case REB_LIT_BAR:
        SET_BAR(c->out);
        FETCH_NEXT_ONLY_MAYBE_END(c);
        break;

    //
    // [WORD!]
    //
    case REB_WORD:
        *c->out = *GET_OPT_VAR_MAY_FAIL(c->value);

    do_retrieved_word:
        if (ANY_FUNC(c->out)) { // check before checking unset, for speed
            //
            // As mentioned in #1934, we only dispatch infix functions as
            // infix when they are looked up from words.  Yet that can only
            // happen after this switch during "lookahead" when the left hand
            // side is ready in `out`.  Here we are starting a brand new
            // expression, and may have nothing at all in `out` to use.
            //
            if (GET_VAL_FLAG(c->out, FUNC_FLAG_INFIX))
                fail (Error(RE_NO_OP_ARG, c->value));

            c->label_sym = VAL_WORD_SYM(c->value);

        #if !defined(NDEBUG)
            c->label_str = cast(const char*, Get_Sym_Name(c->label_sym));
        #endif

            // `do_function_maybe_end_ok` expects the function to be in `func`
            // and if it's a definitional return we extract its target
            //
            c->func = VAL_FUNC(c->out);
            if (c->func == PG_Leave_Func) {
                exit_from = VAL_FUNC_EXIT_FROM(c->out);
                goto do_definitional_exit_from;
            }
            if (c->func == PG_Return_Func)
                exit_from = VAL_FUNC_EXIT_FROM(c->out);
            else
                exit_from = NULL;

            if (Trace_Flags) Trace_Line(c->source, c->indexor, c->value);

            FETCH_NEXT_ONLY_MAYBE_END(c);
            goto do_function_maybe_end_ok;
        }

        if (IS_UNSET(c->out))
            fail (Error(RE_NO_VALUE, c->value));

    #if !defined(NDEBUG)
        if (LEGACY(OPTIONS_LIT_WORD_DECAY) && IS_LIT_WORD(c->out)) {
            //
            // Only set the type, not reset the whole header, because the
            // binding in the payload needs to be in sync with the header bits.
            //
            VAL_SET_TYPE_BITS(c->out, REB_WORD);
        }
    #endif

        FETCH_NEXT_ONLY_MAYBE_END(c);
        break;

    // [SET-WORD!]
    //
    // Does the evaluation into `out`, then gets the variable indicated by
    // the word and writes the result there as well.
    //
    case REB_SET_WORD:
        c->param = c->value; // fetch writes c->value, save SET-WORD! pointer

        FETCH_NEXT_ONLY_MAYBE_END(c);
        if (c->indexor == END_FLAG)
            fail (Error(RE_NEED_VALUE, c->param)); // e.g. `do [foo:]`

        if (eval_normal) { // not using EVAL/ONLY
            DO_NEXT_REFETCH_MAY_THROW(c->out, c, DO_FLAG_LOOKAHEAD);
            if (c->indexor == THROWN_FLAG)
                NOTE_THROWING(goto return_indexor);
        }
        else
            DO_NEXT_REFETCH_QUOTED(c->out, c);

        if (IS_UNSET(c->out))
            fail (Error(RE_NEED_VALUE, c->param)); // e.g. `foo: ()`

        *GET_MUTABLE_VAR_MAY_FAIL(c->param) = *c->out;
        break;

    // [ANY-FUNCTION!]
    //
    // If a function makes it to the SWITCH statement, that means it is either
    // literally a function value in the array (`do compose [(:+) 1 2]`)
    // or is being retriggered via EVAL.  Note that infix functions that
    // are encountered in this way will behave as prefix--their infix behavior
    // is only triggered when they are looked up from a word.  See #1934
    //
    // Most function evaluations are triggered from a SWITCH on a WORD! or
    // a PATH!, which jumps in at the `do_function_maybe_end_ok` label.
    //
    case REB_NATIVE:
    case REB_ACTION:
    case REB_COMMAND:
    case REB_FUNCTION:
        //
        // Note: Because this is a function value being hit literally in
        // a block, it does not have a name.  Use symbol of its VAL_TYPE
        //
        c->label_sym = SYM_FROM_KIND(VAL_TYPE(c->value));

    #if !defined(NDEBUG)
        c->label_str = cast(const char*, Get_Sym_Name(c->label_sym));
    #endif

        // `do_function_maybe_end_ok` expects the function to be in `func`,
        // and if it's a definitional return we need to extract its target.
        // (the REBVAL you get from FUNC_VALUE() does not have the exit_from
        // poked into it.)
        //
        // Note that you *can* have a 'literal' definitional return value,
        // because the user can compose it into a block like any function.
        //
        c->func = VAL_FUNC(c->value);
        if (c->func == PG_Leave_Func) {
            exit_from = VAL_FUNC_EXIT_FROM(c->value);
            goto do_definitional_exit_from;
        }
        if (c->func == PG_Return_Func)
            exit_from = VAL_FUNC_EXIT_FROM(c->value);
        else
            exit_from = NULL;

        FETCH_NEXT_ONLY_MAYBE_END(c);

    do_function_maybe_end_ok:
        //
        // Function to dispatch must be held in `func` on a jump here.
        // We are allowed to be at a END_FLAG position (such as if the
        // function has no arguments, or perhaps its first argument is
        // hard quoted as HELP's is and it can accept that.)
        //
        assert(ANY_FUNC(FUNC_VALUE(c->func)));
        ASSERT_ARRAY(FUNC_PARAMLIST(c->func));

        // There may be refinements pushed to the data stack to process, if
        // the call originated from a path dispatch.
        //
        assert(DSP >= c->dsp_orig);

        // The EVAL "native" is unique because it cannot be a function that
        // runs "under the evaluator"...because it *is the evaluator itself*.
        // Hence it is handled in a special way.
        //
        if (c->func == PG_Eval_Func) {
            //
            // EVAL will handle anything the evaluator can, including
            // an UNSET!, but it errors on END, e.g. `do [eval]`
            //
            if (c->indexor == END_FLAG)
                fail (Error_No_Arg(c->label_sym, FUNC_PARAM(PG_Eval_Func, 1)));

            // "DO/NEXT" full expression into the `eval` REBVAR slot
            // (updates index...).  (There is an /ONLY switch to suppress
            // normal evaluation but it does not apply to the value being
            // retriggered itself, just any arguments it consumes.)
            //
            DO_NEXT_REFETCH_MAY_THROW(&eval, c, DO_FLAG_LOOKAHEAD);

            if (c->indexor == THROWN_FLAG)
                NOTE_THROWING(goto return_indexor);

            // There's only one refinement to EVAL and that is /ONLY.  It can
            // push one refinement to the stack or none.  The state will
            // twist up the evaluator for the next evaluation only.
            //
            if (DSP > c->dsp_orig) {
                assert(DSP == c->dsp_orig + 1);
                assert(VAL_WORD_SYM(DS_TOP) == SYM_ONLY); // canonized on push
                DS_DROP;
                eval_normal = FALSE;
            }
            else
                eval_normal = TRUE;

            // Jumping to the `reevaluate:` label will skip the fetch from the
            // array to get the next `value`.  So seed it with the address of
            // eval result, and step the index back by one so the next
            // increment will get our position sync'd in the block.
            //
            // If there's any reason to be concerned about the temporary
            // item being GC'd, it should be taken care of by the implicit
            // protection from the Do Stack.  (e.g. if it contains a function
            // that gets evaluated it will wind up in c->func, if it's a
            // GROUP! or PATH!-containing-GROUP! it winds up in c->array...)
            //
            // Note that we may be at the end (which would usually be a NULL
            // case for c->value) but we are splicing in eval over that,
            // which keeps the switch from crashing.
            //
            if (c->value)
                c->eval_fetched = c->value;
            else
                c->eval_fetched = END_VALUE; // NULL means no eval_fetched :-/

            c->value = &eval;
            goto reevaluate; // we don't move index!
        }

        // If a native has no refinements to process, it is feasible to
        // allow it to run "frameless".  Even though the chunk stack is a
        // very cheap abstraction, it is not zero cost...and some functions
        // are better implemented as essentially inline hooks to the DO
        // evaluator.
        //
        // All frameless functions must still be able to run with a call
        // frame if requested, because debug scenarios would expect those
        // cells to be inspectable on the stack.  Hence, if there are any
        // trace flags set we fall back upon that implementation.
        //
        // (EVAL/ONLY also suppresses frameless abilities, because the
        // burden of the flag would be too much to pass through.)
        //
        if (
            !Trace_Flags
            && DSP == c->dsp_orig
            && GET_VAL_FLAG(FUNC_VALUE(c->func), FUNC_FLAG_FRAMELESS)
            && eval_normal // avoid framelessness if EVAL/ONLY used
            && !SPORADICALLY(2) // run it framed in DEBUG 1/2 of the time
        ) {
            REB_R ret;
            struct Reb_Call *prior_call = DSF;
            CS_Running = c;

            // A NULL arg signifies to the called function that it is being
            // run frameless.  If it had a frame, then it would be non-NULL
            // and the source of the frame values.
            //
            c->arg = NULL;

            // We might wind up invoking the GC, and we need to make sure
            // the reusable variables aren't bad data.  `value` should be
            // good but we don't know what's in the others.
            //
            c->param = NULL;
            c->refine = NULL;

            SET_TRASH_SAFE(c->out);

            c->mode = CALL_MODE_FUNCTION; // !!! separate "frameless" mode?

            if (IS_ACTION(FUNC_VALUE(c->func))) {
                //
                // At the moment, the type checking actions run framelessly,
                // while no other actions do.  These are things like STRING?
                // and INTEGER?
                //

                assert(FUNC_ACT(c->func) < REB_MAX_0);
                assert(FUNC_NUM_PARAMS(c->func) == 1);

                if (c->indexor == END_FLAG)
                    fail (Error_No_Arg(
                        c->label_sym, FUNC_PARAM(c->func, 1)
                    ));

                DO_NEXT_REFETCH_MAY_THROW(c->out, c, DO_FLAG_LOOKAHEAD);

                if (c->indexor == THROWN_FLAG)
                    ret = R_OUT_IS_THROWN;
                else {
                    if (VAL_TYPE_0(c->out) == FUNC_ACT(c->func))
                        SET_TRUE(c->out);
                    else
                        SET_FALSE(c->out);
                    ret = R_OUT;
                }
            }
            else {
                //
                // Beyond the type-checking actions, only NATIVE! can be
                // frameless...
                //
                assert(IS_NATIVE(FUNC_VALUE(c->func)));
                ret = (*FUNC_CODE(c->func))(c);
            }

            CS_Running = prior_call;

            // If frameless, use SET_UNSET(D_OUT) instead of R_UNSET, etc.
            //
            assert(ret == R_OUT || ret == R_OUT_IS_THROWN);

            if (ret == R_OUT_IS_THROWN) {
                assert(THROWN(c->out));

                // There are actually "two kinds of throws"...one that can't
                // be resumed (such as that which might happen during a
                // parameter fulfillment) and one that might be resumable
                // (like a throw during a DO_ARRAY of a fulfilled parameter).
                // A frameless native must make this distinction to line up
                // with the distinction from normal evaluation.
                //
                if (c->mode == CALL_MODE_THROW_PENDING) {
                    assert(c->indexor != THROWN_FLAG);
                    goto handle_possible_exit_thrown;
                }

                assert(c->indexor == THROWN_FLAG);
                NOTE_THROWING(goto return_indexor);
            }

            c->mode = CALL_MODE_GUARD_ARRAY_ONLY;

            // We're done!
            break;
        }

        // `out` may contain the pending argument for an infix operation,
        // and it could also be the backing store of the `value` pointer
        // to the function.  So Push_New_Arglist_For_Call() shouldn't
        // overwrite it!
        //
        // Note: Although we create the call frame here, we cannot "put
        // it into effect" until all the arguments have been computed.
        // This is because recursive stack-relative bindings would wind
        // up reading variables out of the frame while it is still
        // being built, and that would be bad.
        //
        Push_New_Arglist_For_Call(c);

        // We assume you can enumerate both the formal parameters (in the
        // spec) and the actual arguments (in the call frame) using pointer
        // incrementation, that they are both terminated by END, and
        // that there are an equal number of values in both.
        //
        c->param = FUNC_PARAMS_HEAD(c->func);

        if (IS_END(c->param)) {
            //
            // There are no arguments, so just skip the next section.  We
            // know that `param` contains an END marker so the GC
            // won't crash on it.  The Dispatch_Call() will ovewrite both
            // `arg` and `refine`.
            //
            goto function_ready_to_call;
        }

        // Since we know we're not going to just overwrite it, go ahead and
        // grab the arg head.  While fulfilling arguments the GC might be
        // invoked, so we have to initialize `refine` to something too...
        //
        c->arg = DSF_ARGS_HEAD(c);

    do_function_arglist_in_progress:
        //
        // c->out may have either contained the infix argument (if jumped in)
        // or if this was a fresh loop iteration, the debug build had
        // set c->out to a safe trash.  Using the statistical technique
        // again, we mimic the release build behavior of trust *half* the
        // time, and put in a trapping trash the other half...
        //
    #if !defined(NDEBUG)
        if (SPORADICALLY(2))
            SET_TRASH_SAFE(c->out);
    #endif

        // While fulfilling arguments the GC might be invoked, so we have to
        // initialize `refine` to something too...
        //
        c->refine = NULL;

        // This loop goes through the parameter and argument slots, filling in
        // the arguments via recursive calls to the evaluator.
        //
        // Note that Make_Call initialized them all to UNSET.  This is needed
        // in order to allow skipping around, in particular so that a
        // refinement slot can be marked as processed or not processed, but
        // also because the garbage collector has to consider the slots "live"
        // as arguments are progressively fulfilled.
        //
        c->mode = CALL_MODE_ARGS;

        for (; NOT_END(c->param); c->param++, c->arg++) {
        no_advance:
            assert(IS_TYPESET(c->param));

            // If the frame was pre-built then we want to skip most parameter
            // fulfillment logic.  However, refinements throw a wrench in
            // it because of specialization, because a refinement may be
            // specialized but an argument to it not, or vice-versa.  The
            // `c->mode` and `c->refine` must be updated to allow fallthrough
            // to normal fulfillment.
            //
            if ((c->flags & DO_FLAG_EXECUTE_FRAME)) {
                //
                // !!! Currently there is no support for calling a FRAME! with
                // additional refinements beyond the specialization.  So you
                // can't specialize `frame: :APPEND/ONLY` and then call it as
                // `frame/DUP/ONLY` which would require checking for duplicate
                // refinements and other things.  In the long term that should
                // be implemented.
                //
                assert(c->mode != CALL_MODE_SEEK_REFINE_WORD);

                if (IS_BAR(c->arg)) {
                    if (GET_VAL_FLAG(c->param, TYPESET_FLAG_HIDDEN)) {
                        //
                        // Pure local, so if told to fulfill ordinarily then
                        // that would just be an UNSET!
                        //
                        SET_UNSET(c->arg);
                        continue;
                    }

                    if (GET_VAL_FLAG(c->param, TYPESET_FLAG_REFINEMENT)) {
                        //
                        // With a BAR! in a refinement slot, we take that to
                        // mean that the refinement is not supplied.
                        //
                        SET_NONE(c->arg);
                        c->refine = c->arg;
                        c->mode = CALL_MODE_REFINE_SKIP;
                        continue;
                    }

                    if (
                        c->mode == CALL_MODE_REFINE_REVOKE
                        || c->mode == CALL_MODE_REFINE_SKIP
                    ) {
                        //
                        // A BAR! in a refinement slot where the refinement is
                        // not being taken...either because there was a BAR!
                        // or a FALSE in the refinement slot or it's being
                        // revoked due to an UNSET! being seen...will just
                        // act like an UNSET!
                        //
                        SET_UNSET(c->arg);
                        continue;
                    }

                    // Any other kind of argument we fall through to normal
                    // processing to either fulfill the argument or deliver
                    // the appropriate error.
                    //
                    // !!! Currently ordinary dispatch is written to expect
                    // that the frame was filled with UNSET!  Since ordinary
                    // dispatch is going to be more common than fulfilling a
                    // partially specialized arg, we restore that invariant.
                    //
                    SET_UNSET(c->arg);
                    goto fulfill_non_refinement_non_local;
                }

                // Otherwise, it's not a BAR!, but a value to use...

                if (GET_VAL_FLAG(c->param, TYPESET_FLAG_HIDDEN)) {
                    //
                    // "Pure locals" are expected by functions to be unset.
                    // If frame dispatch were allowed to poke values into
                    // that locals it is "locals injection" and would
                    // undermine the function's ability to assume that the
                    // local was UNSET! at function start.
                    //
                    // !!! This should be reviewed in terms of potential
                    // "continuation"--an issue that would also need to
                    // suppress type checking.  What if a FRAME! could be
                    // thrown and then later resumed with a memory of an
                    // intermediate state?  Such frames would need the ability
                    // to have any value in a locals slot.
                    //
                    if (!IS_UNSET(c->arg))
                        fail (Error_Local_Injection(c->label_sym, c->param));

                    continue;
                }

                // Refinements that are considered to be specified must be
                // coerced into the proper WORD! value of that refinement.
                // An UNSET! is not considered to be coercible.
                //
                if (GET_VAL_FLAG(c->param, TYPESET_FLAG_REFINEMENT)) {
                    if (IS_UNSET(c->arg)) {
                        //
                        // !!! More specific "refinement unset" error?
                        //
                        fail (Error_Arg_Type(
                            c->label_sym, c->param, Type_Of(c->arg))
                        );
                    }
                    else if (IS_CONDITIONAL_TRUE(c->arg)) {
                        Val_Init_Word(
                            c->arg, REB_WORD, VAL_TYPESET_SYM(c->param)
                        );
                        c->mode = CALL_MODE_REFINE_PENDING;
                        continue;
                    }
                    else {
                        SET_NONE(c->arg);
                        c->mode = CALL_MODE_REFINE_SKIP;
                        continue;
                    }
                }

                // If it's an ordinary arg then the revoking/pending logic
                // is repeated here.  This repetition is unfortunate, but if
                // control were to be passed to normal argument fulfillment
                // it would mean stuffing `c->value` from the `c->arg` (which
                // is dubious) as well as doing a mode check to know not to
                // do full evaluation.
                //
                if (
                    c->mode == CALL_MODE_REFINE_REVOKE
                    || c->mode == CALL_MODE_REFINE_SKIP
                ) {
                    //
                    // !!! Technically should it be different errors when you
                    // are given a conditionally false refinement and then
                    // a frame value, vs a conditionally true one and not all
                    // unset values in a revoke?
                    //
                    if (!IS_UNSET(c->arg))
                        fail (Error(RE_BAD_REFINE_REVOKE));

                    continue;
                }
                else if (c->mode == CALL_MODE_REFINE_PENDING) {
                    c->mode = CALL_MODE_REFINE_ARGS;
                    continue;
                }

                goto type_check_arg;
            }

            // *** PURE LOCALS => continue ***

            if (GET_VAL_FLAG(c->param, TYPESET_FLAG_HIDDEN)) {
                //
                // When the spec contained a SET-WORD!, that was a "pure
                // local".  It corresponds to no argument and will not
                // appear in WORDS-OF.  Unlike /local, it cannot be used
                // for "locals injection".  Helpful when writing generators
                // because you don't have to go find /local (!), you can
                // really put it wherever is convenient--no position rule.
                //
                // A trick for functions marked FUNC_FLAG_LEAVE_OR_RETURN
                // puts a "magic" REBNATIVE(return) value into the arg slot
                // for pure locals named RETURN: ....used by FUNC and CLOS
                //
                // Leave this arg value as an UNSET!
                //
                continue;
            }

            if (!GET_VAL_FLAG(c->param, TYPESET_FLAG_REFINEMENT)) {
                //
                // Hunting a refinement?  Quickly disregard this if we are
                // doing such a scan and it isn't a refinement.
                //
                if (c->mode == CALL_MODE_SEEK_REFINE_WORD)
                    continue;
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
                //
                if (c->mode == CALL_MODE_SEEK_REFINE_WORD) {
                    //
                    // Note that we have already canonized the path words for
                    // a case-insensitive-comparison to the symbol in the
                    // function's paramlist.  While it might be tempting to
                    // canonize those too, they should retain their original
                    // case for when that symbol is given back to the user to
                    // indicate a used refinement.
                    //
                    if (
                        VAL_WORD_SYM(DS_TOP) // ...was canonized when pushed
                        == SYMBOL_TO_CANON(VAL_TYPESET_SYM(c->param))
                    ) {
                        // If we seeked backwards to find a refinement and it's
                        // the one we are looking for, "consume" it off the
                        // data stack to say we found it in the frame
                        //
                        DS_DROP;

                        // Now as we switch to pending mode, change refine to
                        // point at the arg slot so we can revoke it if needed
                        //
                        c->mode = CALL_MODE_REFINE_PENDING;
                        c->refine = c->arg;

                        Val_Init_Word(
                            c->refine, REB_WORD, VAL_TYPESET_SYM(c->param)
                        );

                    #if !defined(NDEBUG)
                        if (GET_VAL_FLAG(
                            FUNC_VALUE(c->func), FUNC_FLAG_LEGACY
                        )) {
                            // OPTIONS_REFINEMENTS_TRUE at function create,
                            // so ovewrite the WORD! with TRUE
                            //
                            SET_TRUE(c->refine);
                        }
                    #endif

                        continue;
                    }

                    // ...else keep scanning, but if it's unset then set it
                    // to none because we *might* not revisit this spot again.
                    //
                    if (IS_UNSET(c->arg))
                        SET_NONE(c->arg);

                    continue;
                }

                if (c->dsp_orig == DSP) {
                    //
                    // No refinements are left on the data stack, so if this
                    // refinement slot is still unset, skip the args and leave
                    // them as unsets (or set nones in legacy mode)
                    //
                    c->mode = CALL_MODE_REFINE_SKIP;
                    if (IS_UNSET(c->arg))
                        SET_NONE(c->arg);

                    continue;
                }

                // Should have only pushed words to the stack (or there would
                // have been an earlier error)
                //
                assert(IS_WORD(DS_TOP));

                if (
                    VAL_WORD_SYM(DS_TOP) // ...was canonized when pushed
                    == SYMBOL_TO_CANON(VAL_TYPESET_SYM(c->param))
                ) {
                    // We were lucky and the next refinement we wish to
                    // process lines up with this parameter slot.

                    c->mode = CALL_MODE_REFINE_PENDING;
                    c->refine = c->arg;

                    DS_DROP;

                    Val_Init_Word(
                        c->refine, REB_WORD, VAL_TYPESET_SYM(c->param)
                    );

                #if !defined(NDEBUG)
                    if (GET_VAL_FLAG(FUNC_VALUE(c->func), FUNC_FLAG_LEGACY)) {
                        // OPTIONS_REFINEMENTS_TRUE at function create, so
                        // ovewrite the word we put in the refine slot
                        //
                        SET_TRUE(c->refine);
                    }
                #endif

                    continue;
                }

                // We weren't lucky and need to scan

                c->mode = CALL_MODE_SEEK_REFINE_WORD;
                assert(IS_WORD(DS_TOP));

                // We have to reset to the beginning if we are going to scan,
                // because we might have gone past the refinement on a prior
                // scan.  (Review if a bit might inform us if we've ever done
                // a scan before to get us started going out of order.  If not,
                // we would only need to look ahead.)
                //
                c->param = DSF_PARAMS_HEAD(c);
                c->arg = DSF_ARGS_HEAD(c);

                // We might have a function with no normal args, where a
                // refinement is the first parameter...and we don't want to
                // run the loop's arg++/param++ we get if we `continue`
                //
                goto no_advance;
            }

            if (c->mode == CALL_MODE_REFINE_SKIP) {
                //
                // Just skip because the args are already UNSET! (or NONE! if
                // we are in LEGACY_OPTIONS_REFINEMENT_TRUE mode
                //
                continue;
            }

        fulfill_non_refinement_non_local:
            assert(
                c->mode == CALL_MODE_ARGS
                || c->mode == CALL_MODE_REFINE_PENDING
                || c->mode == CALL_MODE_REFINE_ARGS
                || c->mode == CALL_MODE_REFINE_REVOKE
            );

            // No argument--quoted or otherwise--is allowed to be directly
            // filled by a literal expression barrier.  Not even if it is able
            // to accept the type BAR! (other means must be used, e.g.
            // LIT-BAR! decaying to a BAR! in the slot).
            //
            // Since we prefetched, this can look before a possible DO/NEXT.
            //
            // !!! Technically this is an additional check of c->indexor for
            // the END_FLAG, so it would suggest better performance if the
            // check were done on each non-END-flag-checked branch, but for
            // now it is checked in one place for simplicity.
            //
            if (c->indexor != END_FLAG && IS_BAR(c->value))
                fail (Error(RE_EXPRESSION_BARRIER));

            // *** QUOTED OR EVALUATED ITEMS ***

            if (GET_VAL_FLAG(c->param, TYPESET_FLAG_QUOTE)) {
                if (c->indexor == END_FLAG) {
                    //
                    // If a function has a quoted argument whose types permit
                    // unset, then that specific case is allowed, in order to
                    // implement console commands like HELP (which acts as
                    // arity 1 or 0, using this trick)
                    //
                    //     >> foo: func [:a [unset!]] [
                    //         if unset? :a ["special allowance"]
                    //     ]
                    //
                    //     >> do [foo]
                    //     == "special allowance"
                    //
                #if !defined(NDEBUG)
                    if (GET_VAL_FLAG(FUNC_VALUE(c->func), FUNC_FLAG_LEGACY))
                        SET_UNSET(c->arg); // was NONE by default...
                    else
                        assert(IS_UNSET(c->arg)); // already is unset...
                #endif

                    // Pre-empt the later type checking in order to inject a
                    // more specific message than "doesn't take UNSET!"
                    //
                    if (!TYPE_CHECK(c->param, REB_UNSET))
                        fail (Error_No_Arg(c->label_sym, c->param));
                }
                else if (
                    GET_VAL_FLAG(c->param, TYPESET_FLAG_EVALUATE) // soft quote
                    && eval_normal // we're not in never-evaluating EVAL/ONLY
                    && (
                        IS_GROUP(c->value)
                        || IS_GET_WORD(c->value)
                        || IS_GET_PATH(c->value)
                    )
                ) {
                    // These cases are "soft quoted", because both the flags
                    // TYPESET_FLAG_QUOTE and TYPESET_FLAG_EVALUATE are set.
                    //
                    //     >> foo: function ['a] [print [{a is} a]
                    //
                    //     >> foo 1 + 2
                    //     a is 1
                    //
                    //     >> foo (1 + 2)
                    //     a is 3
                    //
                    // This provides a convenient escape mechanism to allow
                    // callers to subvert quoting if they need to.
                    //
                    // These are "no-arg" evals so we do them isolated.  The
                    // `arg` slot is the input, and can't be output for the
                    // DO also...so use `out` instead.  (`out` is a decent
                    // choice because if a throw were to happen, that's where
                    // the thrown value would have to wind up in that case.)
                    //
                    if (DO_VALUE_THROWS(c->out, c->value)) {
                        //
                        // If we have refinements pending on the data
                        // stack we need to balance those...
                        //
                        DS_DROP_TO(c->dsp_orig);

                        c->indexor = THROWN_FLAG;
                        NOTE_THROWING(goto drop_call_and_return_thrown);
                    }

                    *c->arg = *c->out;

                    FETCH_NEXT_ONLY_MAYBE_END(c);
                }
                else {
                    // This is either not one of the "soft quoted" cases, or
                    // "hard quoting" was explicitly used with GET-WORD!:
                    //
                    //     >> foo: function [:a] [print [{a is} a]
                    //
                    //     >> foo 1 + 2
                    //     a is 1
                    //
                    //     >> foo (1 + 2)
                    //     a is (1 + 2)
                    //
                    DO_NEXT_REFETCH_QUOTED(c->arg, c);
                }
            }
            else {
                // !!! Note: ROUTINE! does not set any bits on the symbols
                // and will need to be made to...
                //
                // assert(GET_VAL_FLAG(param, TYPESET_FLAG_EVALUATE));

                if (c->indexor == END_FLAG)
                    fail (Error_No_Arg(DSF_LABEL_SYM(c), c->param));

                // An ordinary WORD! in the function spec indicates that you
                // would like that argument to be evaluated normally.
                //
                //     >> foo: function [a] [print [{a is} a]
                //
                //     >> foo 1 + 2
                //     a is 3
                //
                // Special outlier EVAL/ONLY can be used to subvert this:
                //
                //     >> eval/only :foo 1 + 2
                //     a is 1
                //     ** Script error: + operator is missing an argument
                //
                if (eval_normal) {
                    DO_NEXT_REFETCH_MAY_THROW(
                        c->arg,
                        c,
                        GET_VAL_FLAG(FUNC_VALUE(c->func), FUNC_FLAG_INFIX)
                            ? DO_FLAG_NO_LOOKAHEAD
                            : DO_FLAG_LOOKAHEAD
                    );

                    if (c->indexor == THROWN_FLAG) {
                        *c->out = *c->arg;

                        // If we have refinements pending on the data
                        // stack we need to balance those...
                        //
                        DS_DROP_TO(c->dsp_orig);

                        NOTE_THROWING(goto drop_call_and_return_thrown);
                    }
                }
                else
                    DO_NEXT_REFETCH_QUOTED(c->arg, c);
            }

            ASSERT_VALUE_MANAGED(c->arg);

            if (IS_UNSET(c->arg)) {
                if (c->mode == CALL_MODE_REFINE_ARGS)
                    fail (Error(RE_BAD_REFINE_REVOKE));
                else if (c->mode == CALL_MODE_REFINE_PENDING) {
                    c->mode = CALL_MODE_REFINE_REVOKE;

                #if !defined(NDEBUG)
                    //
                    // Sanity check that the refinement revoking type is good,
                    // whether legacy (true/false) or Ren-C (WORD! of the
                    // refinement itself).
                    //
                    if (GET_VAL_FLAG(FUNC_VALUE(c->func), FUNC_FLAG_LEGACY)) {
                        assert(IS_LOGIC(c->refine));
                        assert(IS_NONE(c->arg)); // is actually already none
                    }
                    else {
                        assert(IS_WORD(c->refine));
                        assert(IS_UNSET(c->arg));
                    }
                #endif

                    SET_NONE(c->refine); // ...revoke the refinement.
                }
                else if (c->mode == CALL_MODE_REFINE_REVOKE) {
                    //
                    // We are revoking arguments to a refinement that have
                    // never been filled, so they should be vacant.
                    //
                #if !defined(NDEBUG)
                    if (GET_VAL_FLAG(FUNC_VALUE(c->func), FUNC_FLAG_LEGACY))
                        assert(IS_NONE(c->arg));
                    else
                        assert(IS_UNSET(c->arg));
                #endif
                }
            }
            else {
                if (c->mode == CALL_MODE_REFINE_REVOKE)
                    fail (Error(RE_BAD_REFINE_REVOKE));
                else if (c->mode == CALL_MODE_REFINE_PENDING)
                    c->mode = CALL_MODE_REFINE_ARGS;
            }

            // Don't type check the argument if it's being revoked.
            //
            if (c->mode == CALL_MODE_REFINE_REVOKE) {
                assert(IS_UNSET(c->arg));
                continue;
            }

        type_check_arg:
            if (!TYPE_CHECK(c->param, VAL_TYPE(c->arg)))
                fail (Error_Arg_Type(c->label_sym, c->param, Type_Of(c->arg)));
        }

        // If we were scanning and didn't find the refinement we were looking
        // for, then complain with an error.
        //
        // !!! This will complain differently than a proper "REFINED!" strategy
        // would complain, because if you do:
        //
        //     append/(second [only asdhjas])/(print "hi") [a b c] [d]
        //
        // ...it would never make it to the print.  Here we do all the PATH!
        // and GROUP! evals up front and check that things are words or NONE,
        // not knowing if a refinement isn't on the function until the end.
        //
        if (c->mode == CALL_MODE_SEEK_REFINE_WORD)
            fail (Error(RE_BAD_REFINE, DS_TOP));

        // In the case where the user has said foo/bar/baz, and bar was later
        // in the spec than baz, then we will have passed it.  We need to
        // restart the scan (which may wind up failing)
        //
        if (DSP != c->dsp_orig) {
            c->mode = CALL_MODE_SEEK_REFINE_WORD;
            c->param = DSF_PARAMS_HEAD(c);
            c->arg = DSF_ARGS_HEAD(c);
            goto no_advance;
        }

    function_ready_to_call:
        //
        // Execute the function with all arguments ready.
        //
    #if !defined(NDEBUG)
        //
        // R3-Alpha DO acted like an "EVAL" when passed a function, hence it
        // would have an effective arity greater than 1 if that were the case.
        // It was the only function that could do this.  Ren-C isolates the
        // concept of a "re-evaluator" into a full-spectrum native that does
        // *only* that, and is known to be "un-wrappable":
        //
        // https://trello.com/c/YMAb89dv
        //
        // With the VALUE_FLAG_REEVALUATE bit (which had been a cost on every
        // REBVAL) now gone, we must hook the evaluator to implement the
        // legacy feature for DO.
        //
        if (
            LEGACY(OPTIONS_DO_RUNS_FUNCTIONS)
            && IS_NATIVE(FUNC_VALUE(c->func))
            && FUNC_CODE(c->func) == &N_do
            && ANY_FUNC(DSF_ARGS_HEAD(c))
        ) {
            // Grab the argument into the eval storage slot before abandoning
            // the arglist.
            //
            eval = *DSF_ARGS_HEAD(c);

            c->eval_fetched = c->value;
            c->value = &eval;

            c->mode = CALL_MODE_GUARD_ARRAY_ONLY;
            goto drop_call_for_legacy_do_function;
        }
    #endif

        if (exit_from) {
        do_definitional_exit_from:
            //
            // If it's a definitional return, then we need to do the throw
            // for the return, named by the value in the exit_from.  This
            // should be the RETURN native with 1 arg as the function, and
            // the native code pointer should have been replaced by a
            // REBFUN (if function) or REBCTX (if durable) to jump to.
            //
            // !!! Long term there will always be frames for user functions
            // where definitional returns are possible, but for now they
            // still only make them by default if <durable> requested)
            //
            // LEAVE jumps directly here, because it doesn't need to go
            // through any parameter evaluation.  (Note that RETURN can't
            // simply evaluate the next item without inserting an opportunity
            // for the debugger, e.g. `return (breakpoint)`...)
            //
            ASSERT_ARRAY(exit_from);

            // We only have a REBARR*, but want to actually THROW a full
            // REBVAL (FUNCTION! or FRAME! if it has a context) which matches
            // the paramlist.  In either case, the value comes from slot [0]
            // of the RETURN_FROM array, but in the debug build do an added
            // sanity check.
            //
            if (GET_ARR_FLAG(exit_from, SERIES_FLAG_CONTEXT)) {
                //
                // Request to exit from a specific FRAME!
                //
                *c->out = *CTX_VALUE(AS_CONTEXT(exit_from));
                assert(IS_FRAME(c->out));
                assert(CTX_VARLIST(VAL_CONTEXT(c->out)) == exit_from);
            }
            else {
                // Request to dynamically exit from first ANY-FUNCTION! found
                // that has a given parameter list
                //
                *c->out = *FUNC_VALUE(AS_FUNC(exit_from));
                assert(IS_FUNCTION(c->out));
                assert(VAL_FUNC_PARAMLIST(c->out) == exit_from);
            }

            c->indexor = THROWN_FLAG;

            if (c->func == PG_Leave_Func) {
                //
                // LEAVE never created an arglist, so it doesn't have to
                // free one.  Also, it wants to just return UNSET!
                //
                CONVERT_NAME_TO_THROWN(c->out, UNSET_VALUE, TRUE);
                NOTE_THROWING(goto return_indexor);
            }

            // On the other hand, RETURN did make an arglist that has to be
            // dropped from the chunk stack.
            //
            assert(FUNC_NUM_PARAMS(c->func) == 1);
            CONVERT_NAME_TO_THROWN(c->out, DSF_ARGS_HEAD(c), TRUE);
            NOTE_THROWING(goto drop_call_and_return_thrown);
        }


////////// ACTUAL FUNCTION DISPATCH (formerly Dispatch_Call_Throws)
//
// !!! There is a reason this is being integrated directly into what is
// already the longest function in the source.  (Patience!)
//

        // We need to save what the DSF was prior to our execution, and
        // cannot simply use our frame's prior...because our frame's
        // prior call frame may be a *pending* frame that we do not want
        // to put in effect when we are finished.
        //
        call_orig = CS_Running;
        CS_Running = c;

        assert(DSP == c->dsp_orig);

        // Although the Make_Call wrote safe trash into the output slot, we
        // need to do it again for the dispatch, since the spots are used to
        // do argument fulfillment into.
        //
        SET_TRASH_SAFE(c->out);

        assert(IS_END(c->param));
        c->refine = NULL;

        if (c->flags & DO_FLAG_FRAME_CONTEXT) {
            //
            // !!! For the moment we cache the series data pointer in arg.
            // For arbitrary series this is not legal to do, because a
            // resize could relocate it...but we know the argument list will
            // not expand in the current implementation.  However, a memory
            // compactor would likely prefer that there not be too many
            // locked series in order to more freely rearrange memory, so
            // this is a tradeoff.
            //
            assert(GET_ARR_FLAG(
                AS_ARRAY(c->frame.context), SERIES_FLAG_FIXED_SIZE
            ));
            c->arg = CTX_VARS_HEAD(c->frame.context);

            // If the function has a native-optimized version of definitional
            // return, the local for this return should so far have just been
            // ensured in last slot...and left unset by the arg filling.
            //
            // Now fill in the var for that local with a "hacked up" native
            // Note that FUNCTION! uses its PARAMLIST as the RETURN_FROM
            // usually, but not if it's reusing a frame.
            //
            if (GET_VAL_FLAG(FUNC_VALUE(c->func), FUNC_FLAG_LEAVE_OR_RETURN)) {
                REBVAL *last_arg = CTX_VAR(
                    c->frame.context,
                    CTX_LEN(c->frame.context)
                );
                REBVAL *last_param
                    = FUNC_PARAM(c->func, FUNC_NUM_PARAMS(c->func));

                assert(GET_VAL_FLAG(last_param, TYPESET_FLAG_HIDDEN));

            #if !defined(NDEBUG)
                if (GET_VAL_FLAG(FUNC_VALUE(c->func), FUNC_FLAG_LEGACY))
                    assert(IS_NONE(last_arg));
                else
                    assert(IS_UNSET(last_arg));
            #endif

                if (VAL_TYPESET_CANON(last_param) == SYM_RETURN)
                    *last_arg = *ROOT_RETURN_NATIVE;
                else {
                    assert(VAL_TYPESET_CANON(last_param) == SYM_LEAVE);
                    *last_arg = *ROOT_LEAVE_NATIVE;
                }

                VAL_FUNC_EXIT_FROM(last_arg)
                    = CTX_VARLIST(c->frame.context);
            }
        }
        else {
            // We cache the arglist's data pointer in `arg` for ARG().
            // Note that even if the frame becomes "reified" as a context, the
            // data pointer will be the same over the stack level lifetime.
            //
            c->arg = &c->frame.stackvars[0];

            if (GET_VAL_FLAG(FUNC_VALUE(c->func), FUNC_FLAG_LEAVE_OR_RETURN)) {
                REBVAL *last_arg = &c->arg[FUNC_NUM_PARAMS(c->func) - 1];
                REBVAL *last_param
                    = FUNC_PARAM(c->func, FUNC_NUM_PARAMS(c->func));

                assert(GET_VAL_FLAG(last_param, TYPESET_FLAG_HIDDEN));

            #if !defined(NDEBUG)
                if (GET_VAL_FLAG(FUNC_VALUE(c->func), FUNC_FLAG_LEGACY))
                    assert(IS_NONE(last_arg));
                else
                    assert(IS_UNSET(last_arg));
            #endif

                if (VAL_TYPESET_CANON(last_param) == SYM_RETURN)
                    *last_arg = *ROOT_RETURN_NATIVE;
                else {
                    assert(VAL_TYPESET_CANON(last_param) == SYM_LEAVE);
                    *last_arg = *ROOT_LEAVE_NATIVE;
                }

                VAL_FUNC_EXIT_FROM(last_arg) = FUNC_PARAMLIST(c->func);
            }
        }

        if (Trace_Flags) Trace_Func(c->label_sym, FUNC_VALUE(c->func));

        assert(c->indexor != THROWN_FLAG);

        c->mode = CALL_MODE_FUNCTION;

        // If the Do_XXX_Core function dispatcher throws, we can't let it
        // write `c->indexor` directly to become THROWN_FLAG because we may
        // "recover" from the throw by realizing it was a RETURN.  If that
        // is the case, the function we called is the one that returned...
        // so there could still be code after it to execute, and that index
        // will be needed.
        //
        // Rather than have a separate `REBOOL threw`, this goes ahead and
        // overwrites `c->mode` with a special state DO_MODE_THROWN.  It was
        // going to need to be updated anyway back to DO_MODE_0, so no harm
        // in reusing it for the indicator.
        //
        switch (VAL_TYPE(FUNC_VALUE(c->func))) {
        case REB_NATIVE:
            Do_Native_Core(c);
            break;

        case REB_ACTION:
            Do_Action_Core(c);
            break;

        case REB_COMMAND:
            Do_Command_Core(c);
            break;

        case REB_FUNCTION:
            Do_Function_Core(c);
            break;

        case REB_ROUTINE:
            Do_Routine_Core(c);
            break;

        default:
            fail (Error(RE_MISC));
        }

    #if !defined(NDEBUG)
        assert(
            c->mode == CALL_MODE_FUNCTION
            || c->mode == CALL_MODE_THROW_PENDING
        );
        assert(THROWN(c->out) == LOGICAL(c->mode == CALL_MODE_THROW_PENDING));
    #endif

        // Remove this call frame from the call stack (it will be dropped
        // from GC consideration when the args are freed).
        //
        CS_Running = call_orig;

#if !defined(NDEBUG)
    drop_call_for_legacy_do_function:
#endif

    drop_call_and_return_thrown:
        //
        // The same label is currently used for both these outcomes, and
        // which happens depends on whether eval_fetched is NULL or not
        //
        if (c->flags & DO_FLAG_FRAME_CONTEXT) {
            if (CTX_STACKVARS(c->frame.context) != NULL)
                Drop_Chunk(CTX_STACKVARS(c->frame.context));

            if (GET_ARR_FLAG(
                CTX_VARLIST(c->frame.context), SERIES_FLAG_MANAGED
            )) {
                // Context at some point became managed and hence may still
                // have outstanding references.  The accessible flag should
                // have been cleared by the drop chunk above.
                //
                assert(
                    !GET_ARR_FLAG(
                        CTX_VARLIST(c->frame.context), SERIES_FLAG_ACCESSIBLE
                    )
                );
            }
            else {
                // If nothing happened that might have caused the context to
                // become managed (e.g. Val_Init_Word() using it or a
                // Val_Init_Object() for the frame) then the varlist can just
                // go away...
                //
                Free_Array(CTX_VARLIST(c->frame.context));
                //
                // NOTE: Even though we've freed the pointer, we still compare
                // it for identity below when checking to see if this was the
                // stack level being thrown to!
            }
        }
        else
            Drop_Chunk(c->frame.stackvars);

        // A definitional return should only be intercepted if it was for this
        // particular function invocation.  Definitional return abilities have
        // been extended to natives and actions, in order to permit stack
        // control in debug situations (and perhaps some non-debug capabilities
        // will be discovered as well).
        //
    handle_possible_exit_thrown:
        if (
            c->mode == CALL_MODE_THROW_PENDING
            && GET_VAL_FLAG(c->out, VALUE_FLAG_EXIT_FROM)
        ) {
            if (IS_FRAME(c->out)) {
                //
                // This identifies an exit from a *specific* functiion
                // invocation.  We can only match it if we have a reified
                // frame context.
                //
                if (
                    (c->flags & DO_FLAG_FRAME_CONTEXT) &&
                    VAL_CONTEXT(c->out) == AS_CONTEXT(c->frame.context)
                ) {
                    CATCH_THROWN(c->out, c->out);
                    c->mode = CALL_MODE_GUARD_ARRAY_ONLY;
                }
            }
            else if (ANY_FUNC(c->out)) {
                //
                // This identifies an exit from whichever instance of the
                // function is most recent on the stack.  This can be used
                // to exit without reifying a frame.  If exiting dynamically
                // when all that was named was a function, but definitionally
                // scoped returns should ideally have a trick for having
                // the behavior of a reified frame without needing to do
                // so (for now, they use this path in FUNCTION!)
                //
                if (VAL_FUNC_PARAMLIST(c->out) == FUNC_PARAMLIST(c->func)) {
                    CATCH_THROWN(c->out, c->out);
                    c->mode = CALL_MODE_GUARD_ARRAY_ONLY;
                }
            }
            else if (IS_INTEGER(c->out)) {
                //
                // If it's an integer, we drop the value at each stack level
                // until 1 is reached...
                //
                if (VAL_INT32(c->out) == 1) {
                    CATCH_THROWN(c->out, c->out);
                    c->mode = CALL_MODE_GUARD_ARRAY_ONLY;
                }
                else {
                    // don't reset header (keep thrown flag as is), just bump
                    // the count down by one...
                    //
                    --c->out->payload.integer;
                    //
                    // ...and stay in thrown mode...
                }
            }
            else {
                assert(FALSE); // no other low-level EXIT/FROM supported
            }
        }

        // Here we know the function finished.  If it has a definitional
        // return we need to type check it--and if it has a leave we have
        // to squash whatever the last evaluative result was and replace it
        // with an UNSET!
        //
        if (GET_VAL_FLAG(FUNC_VALUE(c->func), FUNC_FLAG_LEAVE_OR_RETURN)) {
            REBVAL *last_param = FUNC_PARAM(c->func, FUNC_NUM_PARAMS(c->func));
            if (VAL_TYPESET_CANON(last_param) == SYM_LEAVE) {
                SET_UNSET(c->out);
            }
            else {
                // The type bits of the definitional return are not applicable
                // to the `return` word being associated with a FUNCTION!
                // vs. an INTEGER! (for instance).  It is where the type
                // information for the non-existent return function specific
                // to this call is hidden.
                //
                assert(VAL_TYPESET_CANON(last_param) == SYM_RETURN);
                if (!TYPE_CHECK(last_param, VAL_TYPE(c->out)))
                    fail (Error_Arg_Type(
                        SYM_RETURN, last_param, Type_Of(c->out))
                    );
            }
        }

        // If running a frame execution then clear that flag out.
        //
        c->flags &= ~DO_FLAG_EXECUTE_FRAME;

    #if !defined(NDEBUG)
        //
        // No longer need to check c->frame.context for thrown status if it
        // was used, so overwrite the dead pointer in the union.  Note there
        // are two entry points to Push_Arglist_For_Call at the moment,
        // so this clearing can't be done by the debug routine at top of loop.
        //
        c->frame.stackvars = NULL;
    #endif

    #if !defined(NDEBUG)
        if (c->eval_fetched) {
            //
            // All the eval wanted to do was get the call frame cleaned up.
            //
            // !!! This is only needed by the legacy implementation of DO
            // for EVAL of functions, because it has to drop the pushed
            // call with arguments.  Is there a cleaner way?
            //
            assert(LEGACY(OPTIONS_DO_RUNS_FUNCTIONS));
            assert(c->mode == CALL_MODE_GUARD_ARRAY_ONLY);
            assert(c->indexor != THROWN_FLAG);
            goto reevaluate;
        }
    #endif

        // If the throw wasn't intercepted as an exit from this function call,
        // accept the throw.  We only care about the mode getting set cleanly
        // back to CALL_MODE_GUARD_ARRAY_ONLY if evaluation continues...
        //
        if (c->mode == CALL_MODE_THROW_PENDING) {
            c->indexor = THROWN_FLAG;
            NOTE_THROWING(goto return_indexor);
        }
        else if (c->indexor == THROWN_FLAG)
            NOTE_THROWING(goto return_indexor);
        else
            c->mode = CALL_MODE_GUARD_ARRAY_ONLY;

//
//
////////// END ACTUAL FUNCTION DISPATCH (formerly Dispatch_Call_Throws)


        if (Trace_Flags) Trace_Return(c->label_sym, c->out);
        break;

    // [FRAME!]
    //
    // If a literal FRAME! is hit in the source, then its associated function
    // will be executed with the data.  Any BAR! in the frame will be treated
    // as if it is an unfulfilled parameter and go through ordinary parameter
    // fulfillment logic.
    //
    case REB_FRAME:
        //
        // While *technically* possible that a context could be in use by more
        // than one function at a time, this is a dangerous enough idea to
        // prohibit unless some special situation arises and it's explicitly
        // said what is meant to be done.
        //
        /*if (GET_VAL_FLAG(c->value, EXT_CONTEXT_RUNNING))
           fail (Error(RE_FRAME_ALREADY_USED, c->value)); */

        assert(c->frame.stackvars == NULL);
        c->frame.context = VAL_CONTEXT(c->value);
        c->func = VAL_CONTEXT_FUNC(c->value);

        if (GET_ARR_FLAG(
            CTX_VARLIST(VAL_CONTEXT(c->value)), SERIES_FLAG_STACK)
        ) {
            c->arg = VAL_CONTEXT_STACKVARS(c->value);
        }
        else
            c->arg = CTX_VARS_HEAD(VAL_CONTEXT(c->value));

        c->param = CTX_KEYS_HEAD(VAL_CONTEXT(c->value));

        c->flags |= DO_FLAG_FRAME_CONTEXT | DO_FLAG_EXECUTE_FRAME;

        FETCH_NEXT_ONLY_MAYBE_END(c);
        exit_from = NULL;
        goto do_function_arglist_in_progress;

    // [PATH!]
    //
    case REB_PATH:
        if (Do_Path_Throws(c->out, &c->label_sym, c->value, NULL)) {
            c->indexor = THROWN_FLAG;
            NOTE_THROWING(goto return_indexor);
        }

        if (ANY_FUNC(c->out)) {
            //
            // object/func or func/refinements or object/func/refinement
            //
            // Because we passed in a label symbol, the path evaluator was
            // willing to assume we are going to invoke a function if it
            // is one.  Hence it left any potential refinements on data stack.
            //
            assert(DSP >= c->dsp_orig);

            // Cannot handle infix because prior value is wiped out above
            // (Theoretically we could save it if we are DO-ing a chain of
            // values, and make it work.  But then, a loop of DO/NEXT
            // may not behave the same as DO-ing the whole block.  Bad.)
            //
            if (GET_VAL_FLAG(c->out, FUNC_FLAG_INFIX))
                fail (Error_Has_Bad_Type(c->out));

            // `do_function_maybe_end_ok` expects the function to be in `func`
            // and if a definitional return, we need to extract its target.
            //
            c->func = VAL_FUNC(c->out);
            if (c->func == PG_Leave_Func) {
                exit_from = VAL_FUNC_EXIT_FROM(c->out);
                goto do_definitional_exit_from;
            }
            if (c->func == PG_Return_Func)
                exit_from = VAL_FUNC_EXIT_FROM(c->out);
            else
                exit_from = NULL;

            FETCH_NEXT_ONLY_MAYBE_END(c);
            goto do_function_maybe_end_ok;
        }
        else {
            // Path should have been fully processed, no refinements on stack
            //
            assert(DSP == c->dsp_orig);
            FETCH_NEXT_ONLY_MAYBE_END(c);
        }
        break;

    // [GET-PATH!]
    //
    case REB_GET_PATH:
        //
        // returns in word the path item, DS_TOP has value
        //
        if (Do_Path_Throws(c->out, NULL, c->value, NULL)) {
            c->indexor = THROWN_FLAG;
            NOTE_THROWING(goto return_indexor);
        }

        // We did not pass in a symbol ID
        //
        assert(DSP == c->dsp_orig);
        FETCH_NEXT_ONLY_MAYBE_END(c);
        break;

    // [SET-PATH!]
    //
    case REB_SET_PATH:
        c->param = c->value; // fetch writes c->value, save SET-WORD! pointer

        FETCH_NEXT_ONLY_MAYBE_END(c);

        // `do [a/b/c:]` is not legal
        //
        if (c->indexor == END_FLAG)
            fail (Error(RE_NEED_VALUE, c->param));

        // We want the result of the set path to wind up in `out`, so go
        // ahead and put the result of the evaluation there.  Do_Path_Throws
        // will *not* put this value in the output when it is making the
        // variable assignment!
        //
        if (eval_normal) {
            DO_NEXT_REFETCH_MAY_THROW(c->out, c, DO_FLAG_LOOKAHEAD);

            if (c->indexor == THROWN_FLAG)
                NOTE_THROWING(goto return_indexor);
        }
        else {
            *(c)->out = *(c)->value;
            FETCH_NEXT_ONLY_MAYBE_END(c);
        }

        // `a/b/c: ()` is not legal (cannot assign path from unset)
        //
        if (IS_UNSET(c->out))
            fail (Error(RE_NEED_VALUE, c->param));

        // !!! The evaluation ordering of SET-PATH! evaluation seems to break
        // the "left-to-right" nature of the language:
        //
        //     >> foo: make object! [bar: 10]
        //
        //     >> foo/(print "left" 'bar): (print "right" 20)
        //     right
        //     left
        //     == 20
        //
        // In addition to seeming "wrong" it also necessitates an extra cell
        // of storage.  This should be reviewed along with Do_Path generally.
        {
            REBVAL temp;
            VAL_INIT_WRITABLE_DEBUG(&temp);
            if (Do_Path_Throws(&temp, NULL, c->param, c->out)) {
                c->indexor = THROWN_FLAG;
                *c->out = temp;
                NOTE_THROWING(goto return_indexor);
            }
        }

        // We did not pass in a symbol, so not a call... hence we cannot
        // process refinements.  Should not get any back.
        //
        assert(DSP == c->dsp_orig);
        break;

    // [GROUP!]
    //
    case REB_GROUP:
        if (DO_ARRAY_THROWS(c->out, c->value)) {
            c->indexor = THROWN_FLAG;
            NOTE_THROWING(goto return_indexor);
        }
        FETCH_NEXT_ONLY_MAYBE_END(c);
        break;

    // [LIT-WORD!]
    //
    // Note we only want to reset the type, not the whole header--because the
    // header bits contain information like WORD_FLAG_BOUND.
    //
    case REB_LIT_WORD:
        DO_NEXT_REFETCH_QUOTED(c->out, c);
        VAL_SET_TYPE_BITS(c->out, REB_WORD);
        break;

    // [GET-WORD!]
    //
    // A GET-WORD! does no checking for unsets, and will return an UNSET! if
    // that is what the variable is.
    //
    case REB_GET_WORD:
        *c->out = *GET_OPT_VAR_MAY_FAIL(c->value);
        FETCH_NEXT_ONLY_MAYBE_END(c);
        break;

    // [LIT-PATH!]
    //
    // We only set the type, in order to preserve the header bits... (there
    // currently aren't any for ANY-PATH!, but there might be someday.)
    //
    // !!! Aliases a REBSER under two value types, likely bad, see #2233
    //
    case REB_LIT_PATH:
        DO_NEXT_REFETCH_QUOTED(c->out, c);
        VAL_SET_TYPE_BITS(c->out, REB_PATH);
        break;

    // *** [ANY-(other)-TYPE!] ***
    //
    // Most things just evaluate to themselves.  See DO_NEXT_CORE_MAY_THROW
    // for the optimization that keeps us from bothering to set up a
    // recursive call if the only outcome will be reaching this point.
    //
    default:
        DO_NEXT_REFETCH_QUOTED(c->out, c);
        break;
    }

    // There shouldn't have been any "accumulated state", in the sense that
    // we should be back where we started in terms of the data stack, the
    // mold buffer position, the outstanding manual series allocations, etc.
    //
    ASSERT_STATE_BALANCED(&state);

    // Throws should have already returned at the time of throw, by jumping
    // to the `thrown_index` label
    //
    assert(c->indexor != THROWN_FLAG && !THROWN(c->out));

    // It's valid for the operations above to fall through after a fetch or
    // refetch that could have reached the end.
    //
    if (c->indexor == END_FLAG)
        goto return_indexor;

    if (c->flags & DO_FLAG_NO_LOOKAHEAD) {
        //
        // Don't do infix lookahead if asked *not* to look.  It's not typical
        // to be requested by callers (there is already no infix lookahead
        // by using DO_FLAG_EVAL_ONLY, so those cases don't need to ask.)
        // However, recursive cases of DO disable infix dispatch if they are
        // currently processing an infix operation.  The currently processing
        // operation is thus given "higher precedence" by this disablement.
        //
    }
    else {
        // Since we're not at an END, we know c->value has been prefetched,
        // so we can "peek" at it.
        //
        // If it is a WORD! that looks up to an infix function, we will use
        // the value sitting in `out` as the "left-hand-side" (parameter 1)
        // of that invocation.  (See #1934 for the resolution that literal
        // function values in the source will act as if they were prefix,
        // so word lookup is the only way to get infix behavior.)
        //
        if (IS_WORD(c->value)) {
            c->param = GET_OPT_VAR_MAY_FAIL(c->value);

            if (ANY_FUNC(c->param) && GET_VAL_FLAG(c->param, FUNC_FLAG_INFIX)) {
                c->label_sym = VAL_WORD_SYM(c->value);

            #if !defined(NDEBUG)
                c->label_str = cast(const char*, Get_Sym_Name(c->label_sym));
            #endif

                c->func = VAL_FUNC(c->param);

                // The warped function values used for definitional return
                // usually need their EXIT_FROMs extracted, but here we should
                // not worry about it as neither RETURN nor LEAVE are infix
                //
                assert(c->func != PG_Leave_Func);
                assert(c->func != PG_Return_Func);
                exit_from = NULL;

                if (Trace_Flags) Trace_Line(c->source, c->indexor, c->param);

                // We go ahead and start an arglist, and put our evaluated
                // result into it as the "left-hand-side" before calling into
                // the rest of function's behavior.
                //
                Push_New_Arglist_For_Call(c);

                // Infix functions must have at least arity 2 (exactly?)
                //
                assert(FUNC_NUM_PARAMS(c->func) >= 2);
                c->param = FUNC_PARAMS_HEAD(c->func);
                if (!TYPE_CHECK(c->param, VAL_TYPE(c->out)))
                    fail (Error_Arg_Type(
                        c->label_sym, c->param, Type_Of(c->out))
                    );

                // Take current `out` and use it as the first argument of the
                // infix function.
                //
                c->arg = DSF_ARGS_HEAD(c);
                *c->arg = *c->out;

                ++c->param;
                ++c->arg;

                FETCH_NEXT_ONLY_MAYBE_END(c);
                goto do_function_arglist_in_progress;
            }

            // Perhaps not an infix function, but we just paid for a variable
            // lookup.  If this isn't just a DO/NEXT, use the work!
            //
            if (c->flags & DO_FLAG_TO_END) {
                //
                // We need to update the `expr_index` since we are skipping
                // the whole `do_at_index` preparation for the next cycle,
                // and also need to run the "Preamble" in debug builds to
                // properly update the tick count and clear out state.
                //
                c->expr_index = c->indexor;
                *c->out = *c->param; // param is trashed by Preamble_Debug!

            #if !defined(NDEBUG)
                do_count = Do_Evaluation_Preamble_Debug(c);
            #endif

                goto do_retrieved_word; // word will handle FETCH_NEXT
            }
        }

        // Note: PATH! may contain parens, which would need to be evaluated
        // during lookahead.  This could cause side-effects if the lookahead
        // fails.  Consequently, PATH! should not be a candidate for doing
        // an infix dispatch.
    }

    // Continue evaluating rest of block if not just a DO/NEXT
    //
    if (c->flags & DO_FLAG_TO_END) goto value_ready_for_do_next;

return_indexor:
    //
    // Jumping here skips the natural check that would be done after the
    // switch on the value being evaluated, so we assert balance here too.
    //
    ASSERT_STATE_BALANCED(&state);

#if !defined(NDEBUG)
    Do_Exit_Checks_Debug(c);
#endif

    // Restore the top of stack (if there is a fail() and associated longjmp,
    // this restoration will be done by the Drop_Trap helper.)
    //
    TG_Do_Stack = c->prior;

    // Caller needs to inspect `index`, at minimum to know if it's THROWN_FLAG
}


#if !defined(NDEBUG)

//
// Putting the exit checks in their own routines makes a small attempt to
// pare down the total amount of code in Do_Core() for readability, while
// still having a place to put as many checks that might help verify that
// things are working properly.
//
static void Do_Exit_Checks_Debug(struct Reb_Call *c) {
    if (
        c->indexor != END_FLAG && c->indexor != THROWN_FLAG
        && c->indexor != VARARGS_FLAG
    ) {
        // If we're at the array's end position, then we've prefetched the
        // last value for processing (and not signaled end) but on the
        // next fetch we *will* signal an end.
        //
        assert(c->indexor <= ARR_LEN(c->source.array));
    }

    if (c->flags & DO_FLAG_TO_END)
        assert(c->indexor == THROWN_FLAG || c->indexor == END_FLAG);

    if (c->indexor == END_FLAG) {
        assert(c->value == NULL); // NULLing out value may become debug-only
        assert(NOT_END(c->out)); // series END marker shouldn't leak out
    }

    if (c->indexor == THROWN_FLAG)
        assert(THROWN(c->out));

    // Function execution should have written *some* actual output value
    // over the trash that we put in the return slot before the call.
    //
    assert(!IS_TRASH_DEBUG(c->out));
    assert(VAL_TYPE(c->out) < REB_MAX); // cheap check

    ASSERT_VALUE_MANAGED(c->out);
}

#endif


//
//  Do_Array_At_Core: C
//
// Most common case of evaluator invocation in Rebol: the data lives in an
// array series.  Generic routine takes flags and may act as either a DO
// or a DO/NEXT at the position given.  Option to provide an element that
// may not be resident in the array to kick off the execution.
//
REBIXO Do_Array_At_Core(
    REBVAL *out,
    const REBVAL *opt_first,
    REBARR *array,
    REBCNT index,
    REBFLGS flags
) {
    struct Reb_Call c;

    if (opt_first) {
        c.value = opt_first;
        c.indexor = index;
    }
    else {
        // Do_Core() requires caller pre-seed first value, always
        //
        c.value = ARR_AT(array, index);
        c.indexor = index + 1;
    }

    if (IS_END(c.value)) {
        SET_UNSET(out);
        return END_FLAG;
    }

    c.out = out;
    c.source.array = array;
    c.flags = flags;

    Do_Core(&c);

    return c.indexor;
}


//
//  Do_Varargs_Core: C
//
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
// (corresponding to R3-Alpha's APPLY/ONLY) then DO_FLAG_EVAL_ONLY should be
// used--otherwise they will be evaluated normally.
//
// NOTE: Ren-C no longer supports the built-in ability to supply refinements
// positionally, due to the brittleness of this approach (for both system
// and user code).  The `opt_head` value should be made a path with the
// function at the head and the refinements specified there.  Future
// additions could do this more efficiently by allowing the refinement words
// to be pushed directly to the data stack.
//
// !!! C vararg lists are very dangerous, there is no type checking!  The
// C++ build should be able to check this for the callers of this function
// *and* check that you ended properly.  It means this function will need
// two different signatures (and so will each caller of this routine).
//
// Returns THROWN_FLAG, END_FLAG--or if DO_FLAG_NEXT is used it may return
// VARARGS_INCOMPLETE_FLAG.
//
REBIXO Do_Varargs_Core(
    REBVAL *out,
    const REBVAL *opt_first,
    va_list *varargs_ptr,
    REBFLGS flags
) {
    struct Reb_Call c;

    if (opt_first)
        c.value = opt_first;
    else {
        // Do_Core() requires caller pre-seed first value, always
        //
        c.value = va_arg(*varargs_ptr, const REBVAL*);
    }

    if (IS_END(c.value)) {
        SET_UNSET(out);
        return END_FLAG;
    }

    c.out = out;
    c.indexor = VARARGS_FLAG;
    c.source.varargs_ptr = varargs_ptr;

    // !!! See notes in %m-gc.c about what needs to be done before it can
    // be safe to let arbitrary evaluations happen in varargs scenarios.
    // (This functionality coming soon, but it requires reifying the varargs
    // into an array if a GC incidentally happens during any vararg DOs.)
    //
    assert(flags & DO_FLAG_EVAL_ONLY);
    c.flags = flags;

    Do_Core(&c);

    if (flags & DO_FLAG_NEXT) {
        //
        // Infix lookahead causes a fetch that cannot be undone.  Hence
        // Varargs DO/NEXT can't be resumed -- see VARARGS_INCOMPLETE_FLAG.
        // For a resumable interface on varargs, see the lower level
        // frameless API.
        //
        if (c.indexor == VARARGS_FLAG) {
            //
            // Try one more fetch and see if it's at the end.  If not, we did
            // not consume all the input.
            //
            FETCH_NEXT_ONLY_MAYBE_END(&c);
            if (c.indexor != END_FLAG) {
                assert(c.indexor == VARARGS_FLAG); // couldn't throw!!
                return VARARGS_INCOMPLETE_FLAG;
            }
        }

        assert(c.indexor == THROWN_FLAG || c.indexor == END_FLAG);
    }

    return c.indexor;
}


//
//  Do_Values_At_Core: C
//
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
REBIXO Do_Values_At_Core(
    REBVAL *out,
    REBFLGS flags,
    const REBVAL *opt_head,
    const REBVAL values[],
    REBCNT index
) {
    fail (Error(RE_MISC));
}


//
//  Sys_Func: C
//
// Gets a system function with tolerance of it not being a function.
//
// (Extraction of a feature that formerly was part of a dedicated dual
// function to Apply_Func_Throws (Do_Sys_Func_Throws())
//
REBVAL *Sys_Func(REBCNT inum)
{
    REBVAL *value = CTX_VAR(Sys_Context, inum);

    if (!ANY_FUNC(value)) fail (Error(RE_BAD_SYS_FUNC, value));

    return value;
}


//
//  Apply_Only_Throws: C
//
// Takes a list of arguments terminated by END_VALUE (or any IS_END) and
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
REBOOL Apply_Only_Throws(REBVAL *out, const REBVAL *applicand, ...)
{
    REBIXO indexor;
    va_list varargs;

#ifdef VA_END_IS_MANDATORY
    struct Reb_State state;
    REBCTX *error;
#endif

    va_start(varargs, applicand); // must mention last param before the "..."

#ifdef VA_END_IS_MANDATORY
    PUSH_TRAP(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

    if (error) {
        va_end(varargs); // interject cleanup of whatever va_start() set up...
        fail (error); // ...then just retrigger error
    }
#endif

    indexor = Do_Varargs_Core(
        out,
        applicand, // opt_first
        &varargs,
        DO_FLAG_NEXT | DO_FLAG_LOOKAHEAD | DO_FLAG_EVAL_ONLY
    );

    if (indexor == VARARGS_INCOMPLETE_FLAG) {
        //
        // Not consuming all the arguments given suggests a problem as far
        // as this interface is concerned.  To tolerate incomplete states,
        // use Do_Varargs_Core() directly.
        //
        fail (Error(RE_APPLY_TOO_MANY));
    }

    va_end(varargs);
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


//
//  Redo_Func_Throws: C
//
// This code takes a running call frame that has been built for one function
// and then tries to map its parameters to another call.  It is used to
// dispatch some ACTION!s (an archetypal function spec with no implementation)
// from a native C invocation to be "bounced" out into user code.
//
// In the origins of this function's active usage in R3-Alpha, it was allowed
// for the target function to have a parameterization that was a superset of
// the original frame's function (adding refinements, etc.)  The greater
// intentions of how it was supposed to work are not known--as there was
// little error checking, given there were few instances.
//
// !!! Due to the historical brittleness of this function, very rare calls,
// and need for an additional repetition of dispatch logic from Do_Core,
// this code has been replaced with a straightforward implementation.  It
// builds a PATH! of the target function and refinements from the original
// frame.  Then it uses this in the DO_FLAG_EVAL_ONLY mode to suppress
// re-evaluation of the frame's "live" args.
//
// !!! This won't stand up in the face of targets that are "adversarial"
// to the archetype:
//
//     foo: func [a /b c] [...]  =>  bar: func [/b d e] [...]
//                    foo/b 1 2  =>  bar/b 1 2
//
// However, it is still *much* better than the R3-Alpha situation for error
// checking, and significantly less confusing.  A real solution to this kind
// of dispatch--if it is to be used--seems like it should be a language
// feature available to users themselves.  So leaning on the evaluator in
// one way or another is the best course to keep this functionality going.
//
REBOOL Redo_Func_Throws(struct Reb_Call *c, REBFUN *func_new)
{
    REBIXO indexor;

    // Upper bound on the length of the args we might need for a redo
    // invocation is the total number of parameters to the *old* function's
    // invocation (if it had no refinements or locals).
    //
    REBARR *code_array = Make_Array(FUNC_NUM_PARAMS(c->func));
    REBVAL *code = ARR_HEAD(code_array);

    // We'll walk through the original functions param and arglist only, and
    // accept the error-checking the evaluator provides at this time (types,
    // refinement presence or absence matching).
    //
    // !!! See note in function description about arity mismatches.
    //
    REBVAL *param = FUNC_PARAMS_HEAD(c->func);
    REBVAL *arg = DSF_ARGS_HEAD(c);
    REBOOL ignoring = FALSE;

    // The first element of our path will be the function, followed by its
    // refinements.  It has an upper bound on length that is to consider the
    // opposite case where it had only refinements and then the function
    // at the head...
    //
    REBARR *path_array = Make_Array(FUNC_NUM_PARAMS(c->func) + 1);
    REBVAL *path = ARR_HEAD(path_array);

    REBVAL first;
    VAL_INIT_WRITABLE_DEBUG(&first);

    *path = *FUNC_VALUE(func_new);
    ++path;

    for (; NOT_END(param); ++param, ++arg) {
        if (GET_VAL_FLAG(param, TYPESET_FLAG_HIDDEN)) {
             //
             // Pure local... don't add a code arg for it (can't)!
             //
             continue;
        }

        if (GET_VAL_FLAG(param, TYPESET_FLAG_REFINEMENT)) {
            if (IS_CONDITIONAL_FALSE(arg)) {
                //
                // If the refinement is not in use, do not add it and ignore
                // args until the next refinement.
                //
                ignoring = TRUE;
                continue;
            }

            // In use--and used refinements must be added to the PATH!
            //
            ignoring = FALSE;
            Val_Init_Word(path, REB_WORD, VAL_TYPESET_SYM(param));
            ++path;
            continue;
        }

        // Otherwise it should be a quoted or normal argument.  If ignoring
        // then pass on it, otherwise add the arg to the code as-is.
        //
        if (ignoring) continue;

        *code++ = *arg;
    }

    SET_END(code);
    SET_ARRAY_LEN(code_array, code - ARR_HEAD(code_array));
    MANAGE_ARRAY(code_array);

    SET_END(path);
    SET_ARRAY_LEN(path_array, path - ARR_HEAD(path_array));
    Val_Init_Array(&first, REB_PATH, path_array); // manages

    // Invoke DO with the special mode requesting non-evaluation on all
    // args, as they were evaluated the first time around.
    //
    indexor = Do_Array_At_Core(
        c->out,
        &first, // path not in array but will be "virtual" first array element
        code_array,
        0, // index
        DO_FLAG_TO_END | DO_FLAG_LOOKAHEAD | DO_FLAG_EVAL_ONLY
    );

    if (indexor != THROWN_FLAG && indexor != END_FLAG) {
        //
        // We may not have stopped the invocation by virtue of the args
        // all not getting consumed, but we can raise an error now that it
        // did not.
        //
        assert(FALSE);
        fail (Error(RE_MISC));
    }

    return LOGICAL(indexor == THROWN_FLAG);
}


//
// ??? ======================================= MOVE THE BELOW TO %c-reduce.c ?
//
// !!! Do COMPOSE and REDUCE belong in the same file?  Is there a name for
// the category of operations?  They should be unified and live in the
// same file as their natives.
//


//
//  Reduce_Array_Throws: C
//
// Reduce array from the index position specified in the value.
// Collect all values from stack and make them into a BLOCK! REBVAL.
//
// !!! Review generalization of this to produce an array and not a REBVAL
// of a particular kind.
//
REBOOL Reduce_Array_Throws(
    REBVAL *out,
    REBARR *array,
    REBCNT index,
    REBOOL into
) {
    REBDSP dsp_orig = DSP;
    REBIXO indexor = index;

    // Through the DO_NEXT_MAY_THROW interface, we can't tell the difference
    // between DOing an array that literally contains an UNSET! and an empty
    // array, because both give back an unset value and an end position.
    // We'd like REDUCE to treat `reduce []` and `reduce [#[unset!]]` in
    // a different way, so must do a special check to handle the former.
    //
    if (IS_END(ARR_AT(array, index))) {
        if (into)
            return FALSE;

        Val_Init_Block(out, Make_Array(0));
        return FALSE;
    }

    while (indexor != END_FLAG) {
        REBVAL reduced;
        VAL_INIT_WRITABLE_DEBUG(&reduced);

        DO_NEXT_MAY_THROW(indexor, &reduced, array, indexor);

        if (indexor == THROWN_FLAG) {
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
void Reduce_Only(
    REBVAL *out,
    REBARR *block,
    REBCNT index,
    REBVAL *words,
    REBOOL into
) {
    REBDSP dsp_orig = DSP;
    REBVAL *val;
    const REBVAL *v;
    REBARR *arr = 0;
    REBCNT idx = 0;

    if (IS_BLOCK(words)) {
        arr = VAL_ARRAY(words);
        idx = VAL_INDEX(words);
    }

    for (val = ARR_AT(block, index); NOT_END(val); val++) {
        if (IS_WORD(val)) {
            // Check for keyword:
            if (arr && NOT_FOUND != Find_Word_In_Array(arr, idx, VAL_WORD_CANON(val))) {
                DS_PUSH(val);
                continue;
            }
            v = GET_OPT_VAR_MAY_FAIL(val);
            DS_PUSH(v);
        }
        else if (IS_PATH(val)) {
            if (arr) {
                // Check for keyword/path:
                v = VAL_ARRAY_AT(val);
                if (IS_WORD(v)) {
                    if (NOT_FOUND != Find_Word_In_Array(arr, idx, VAL_WORD_CANON(v))) {
                        DS_PUSH(val);
                        continue;
                    }
                }
            }

            // pushes val on stack
            DS_PUSH_TRASH_SAFE;
            if (Do_Path_Throws(DS_TOP, NULL, val, NULL))
                fail (Error_No_Catch_For_Throw(DS_TOP));
        }
        else DS_PUSH(val);
        // No need to check for unwinds (THROWN) here, because unwinds should
        // never be accessible via words or paths.
    }

    Pop_Stack_Values(out, dsp_orig, into);

    assert(DSP == dsp_orig);
}


//
//  Reduce_Array_No_Set_Throws: C
//
REBOOL Reduce_Array_No_Set_Throws(
    REBVAL *out,
    REBARR *block,
    REBCNT index,
    REBOOL into
) {
    REBDSP dsp_orig = DSP;
    REBIXO indexor = index;

    while (index < ARR_LEN(block)) {
        REBVAL *value = ARR_AT(block, index);
        if (IS_SET_WORD(value)) {
            DS_PUSH(value);
            index++;
        }
        else {
            REBVAL reduced;
            VAL_INIT_WRITABLE_DEBUG(&reduced);

            DO_NEXT_MAY_THROW(indexor, &reduced, block, indexor);
            if (indexor == THROWN_FLAG) {
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
//  Compose_Values_Throws: C
// 
// Compose a block from a block of un-evaluated values and GROUP! arrays that
// are evaluated.  This calls into Do_Core, so if 'into' is provided, then its
// series must be protected from garbage collection.
// 
//     deep - recurse into sub-blocks
//     only - parens that return blocks are kept as blocks
// 
// Writes result value at address pointed to by out.
//
REBOOL Compose_Values_Throws(
    REBVAL *out,
    REBVAL value[],
    REBOOL deep,
    REBOOL only,
    REBOOL into
) {
    REBDSP dsp_orig = DSP;

    for (; NOT_END(value); value++) {
        if (IS_GROUP(value)) {
            REBVAL evaluated;
            VAL_INIT_WRITABLE_DEBUG(&evaluated);

            if (DO_ARRAY_THROWS(&evaluated, value)) {
                *out = evaluated;
                DS_DROP_TO(dsp_orig);
                return TRUE;
            }

            if (IS_BLOCK(&evaluated) && !only) {
                //
                // compose [blocks ([a b c]) merge] => [blocks a b c merge]
                //
                REBVAL *push = VAL_ARRAY_AT(&evaluated);
                while (!IS_END(push)) {
                    DS_PUSH(push);
                    push++;
                }
            }
            else if (!IS_UNSET(&evaluated)) {
                //
                // compose [(1 + 2) inserts as-is] => [3 inserts as-is]
                // compose/only [([a b c]) unmerged] => [[a b c] unmerged]
                //
                DS_PUSH(&evaluated);
            }
            else {
                //
                // compose [(print "Unsets *vanish*!")] => []
                //
            }
        }
        else if (deep) {
            if (IS_BLOCK(value)) {
                //
                // compose/deep [does [(1 + 2)] nested] => [does [3] nested]

                REBVAL composed;
                VAL_INIT_WRITABLE_DEBUG(&composed);

                if (Compose_Values_Throws(
                    &composed, VAL_ARRAY_HEAD(value), TRUE, only, into
                )) {
                    *out = composed;
                    DS_DROP_TO(dsp_orig);
                    return TRUE;
                }

                DS_PUSH(&composed);
            }
            else {
                DS_PUSH(value);
                if (ANY_ARRAY(value)) {
                    //
                    // compose [copy/(orig) (copy)] => [copy/(orig) (copy)]
                    // !!! path and second group are copies, first group isn't
                    //
                    VAL_ARRAY(DS_TOP) = Copy_Array_Shallow(VAL_ARRAY(value));
                    MANAGE_ARRAY(VAL_ARRAY(DS_TOP));
                }
            }
        }
        else {
            //
            // compose [[(1 + 2)] (reverse "wollahs")] => [[(1 + 2)] "shallow"]
            //
            DS_PUSH(value);
        }
    }

    Pop_Stack_Values(out, dsp_orig, into);

    return FALSE;
}


//
// ??? ======================================= MOVE THE BELOW TO %?????????
//


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
    REBDSP dsp_orig = DSP;

    REBVAL temp;
    VAL_INIT_WRITABLE_DEBUG(&temp);
    SET_NONE(&temp);

    // This routine reads values from the start to the finish, which means
    // that if it wishes to do `word1: word2: value` it needs to have some
    // way of getting to the value and then going back across the words to
    // set them.  One way of doing it would be to start from the end and
    // work backward, but this uses the data stack instead to gather set
    // words and then go back and set them all when a value is found.
    //
    // !!! This could also just remember the pointer of the first set
    // word in a run, but at time of writing this is just patching a bug.
    //
    for (; NOT_END(value); value++) {
        if (IS_SET_WORD(value)) {
            //
            // Remember this SET-WORD!.  Come back and set what it is
            // bound to, once a non-SET-WORD! value is found.
            //
            DS_PUSH(value);
            continue;
        }

        // If not a SET-WORD! then consider the argument to represent some
        // kind of value.
        //
        // !!! The historical default is to NONE!, and also to transform
        // what would be evaluative into non-evaluative.  So:
        //
        //     >> construct [a: b/c: d: append "Strange" <defaults>]
        //     == make object! [
        //         a: b/c:
        //         d: 'append
        //     ]
        //
        // A differing philosophy might be that the construction process
        // only tolerate input that would yield the same output if used
        // in an evaulative object creation.
        //
        if (IS_WORD(value)) {
            switch (VAL_WORD_CANON(value)) {
            case SYM_NONE:
                SET_NONE(&temp);
                break;

            case SYM_TRUE:
            case SYM_ON:
            case SYM_YES:
                SET_TRUE(&temp);
                break;

            case SYM_FALSE:
            case SYM_OFF:
            case SYM_NO:
                SET_FALSE(&temp);
                break;

            default:
                temp = *value;
                VAL_SET_TYPE_BITS(&temp, REB_WORD);
            }
        }
        else if (IS_LIT_WORD(value)) {
            temp = *value;
            VAL_SET_TYPE_BITS(&temp, REB_WORD);
        }
        else if (IS_LIT_PATH(value)) {
            temp = *value;
            VAL_SET_TYPE_BITS(&temp, REB_PATH);
        }
        else if (VAL_TYPE(value) >= REB_NONE) { // all valid values
            temp = *value;
        }
        else
            SET_NONE(&temp);

        // Set prior set-words:
        while (DSP > dsp_orig) {
            *GET_MUTABLE_VAR_MAY_FAIL(DS_TOP) = temp;
            DS_DROP;
        }
    }

    // All vars in the frame should have a default value if not set, so if
    // we reached the end with something like `[a: 10 b: c: d:]` just leave
    // the trailing words to that default.  However, we must balance the
    // stack to please the evaluator, so let go of the set-words that we
    // did not set.
    //
    DS_DROP_TO(dsp_orig);
}


//
//  Do_Min_Construct: C
// 
// Do no evaluation of the set values.
//
void Do_Min_Construct(REBVAL value[])
{
    REBVAL *temp;
    REBDSP ssp;  // starting stack pointer

    DS_PUSH_NONE;
    temp = DS_TOP;
    ssp = DSP;

    for (; NOT_END(value); value++) {
        if (IS_SET_WORD(value)) {
            // Next line not needed, because SET words are ALWAYS in frame.
            //if (VAL_WORD_INDEX(value) > 0 && VAL_WORD_CONTEXT(value) == frame)
                DS_PUSH(value);
        } else {
            // Get value:
            *temp = *value;
            // Set prior set-words:
            while (DSP > ssp) {
                *GET_MUTABLE_VAR_MAY_FAIL(DS_TOP) = *temp;
                DS_DROP;
            }
        }
    }
    DS_DROP; // temp
}


//
// ====================> MOVE THE BELOW TO %??? ?
//

//
//  Get_Simple_Value_Into: C
// 
// Does easy lookup, else just returns the value as is.
//
void Get_Simple_Value_Into(REBVAL *out, const REBVAL *val)
{
    if (IS_WORD(val) || IS_GET_WORD(val)) {
        *out = *GET_OPT_VAR_MAY_FAIL(val);
    }
    else if (IS_PATH(val) || IS_GET_PATH(val)) {
        if (Do_Path_Throws(out, NULL, val, NULL))
            fail (Error_No_Catch_For_Throw(out));
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
REBCTX *Resolve_Path(REBVAL *path, REBCNT *index)
{
    REBVAL *sel; // selector
    const REBVAL *val;
    REBARR *blk;
    REBCNT i;

    if (VAL_LEN_HEAD(path) < 2) return 0;
    blk = VAL_ARRAY(path);
    sel = ARR_HEAD(blk);
    if (!ANY_WORD(sel)) return 0;
    val = GET_OPT_VAR_MAY_FAIL(sel);

    sel = ARR_AT(blk, 1);
    while (TRUE) {
        if (!ANY_CONTEXT(val) || !IS_WORD(sel)) return 0;
        i = Find_Word_In_Context(VAL_CONTEXT(val), VAL_WORD_SYM(sel), FALSE);
        sel++;
        if (IS_END(sel)) {
            *index = i;
            return VAL_CONTEXT(val);
        }
    }

    return 0; // never happens
}
