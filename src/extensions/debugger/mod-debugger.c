//
//  File: %mod-debugger.c
//  Summary: "Native Functions for debugging"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
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
// One goal of Ren-C's debugger is to have as much of it possible written in
// usermode Rebol code, and be easy to hack on and automate.  Additionally it
// seeks to use a minimal set of hooks into the core evaluator, so that an
// interpreter can be built easily without debugging functions and then have
// debugging attached later as a DLL.
//
// This file contains interactive debugging support for breaking and
// resuming.  The instructions BREAKPOINT and PAUSE are natives which will
// call a host environment hook which can then begin an interactive debugging
// session.  During that time Rebol functions may continue to be called,
// though there is a sandbox which prevents the code from throwing or causing
// errors which will propagate past the breakpoint.  The only way to
// resume normal operation is with a "resume instruction".
//
// !!! Interactive debugging is a work in progress, and comments are in the
// functions below.
//

#include "sys-core.h"

#include "tmp-mod-debugger-first.h"


// Index values for the properties in a "resume instruction" (see notes on
// REBNATIVE(resume))
//
enum {
    RESUME_INST_MODE = 0,   // FALSE if /WITH, TRUE if /DO, NONE! if default
    RESUME_INST_PAYLOAD,    // code block to /DO or value of /WITH
    RESUME_INST_TARGET,     // unwind target, NONE! to return from breakpoint
    RESUME_INST_MAX
};


// Current stack level displayed in the REPL, where bindings are assumed to
// be made for evaluations.  So if the prompt reads `[3]>>`, and a string
// of text is typed in to be loaded as code, that code will be bound to
// the user context, then the lib context, then to the variables of whatever
// function is located at stack level 3.
//
extern REBCNT HG_Stack_Level;
REBCNT HG_Stack_Level = 1;

const REBVAL *HG_Host_Repl = NULL; // needs to be a GC-protecting reference


//
//  init-debugger: native/export [
//
//  {Tell the debugger what action to use as a REPL.}
//
//      return: [<opt>]
//      console [action!]
//  ]
//
static REBNATIVE(init_debugger)
{
    DEBUGGER_INCLUDE_PARAMS_OF_INIT_DEBUGGER;

    HG_Host_Repl = ACT_ARCHETYPE(VAL_ACTION(ARG(console)));
    return R_VOID;
}


// Forward-definition so that BREAKPOINT and RESUME can call it
//
REBOOL Do_Breakpoint_Throws(
    REBVAL *out,
    REBOOL interrupted, // Ctrl-C (as opposed to a BREAKPOINT)
    const REBVAL *default_value,
    REBOOL do_default
);


//
//  breakpoint: native/export [
//
//  "Signal breakpoint to the host, but do not participate in evaluation"
//
//      return: []
//          {Returns nothing, not even void ("invisible", like COMMENT)}
//  ]
//
static REBNATIVE(breakpoint)
//
// !!! Need definition to test for N_DEBUGGER_breakpoint function
{
    if (Do_Breakpoint_Throws(
        D_OUT,
        FALSE, // not a Ctrl-C, it's an actual BREAKPOINT
        VOID_CELL, // default result if RESUME does not override
        FALSE // !execute (don't try to evaluate the VOID_CELL)
    )){
        return R_OUT_IS_THROWN;
    }

    // !!! Should use a more specific protocol (e.g. pass in END).  But also,
    // this provides a possible motivating case for functions to be able to
    // return *either* a value or no-value...if breakpoint were variadic, it
    // could splice in a value in place of what comes after it.
    //
    if (not IS_VOID(D_OUT))
        fail ("BREAKPOINT is invisible, can't RESUME/WITH code (use PAUSE)");

    return R_INVISIBLE;
}


//
//  pause: native/export [
//
//  "Pause in the debugger before running the provided code"
//
//      return: [<opt> any-value!]
//          "Result of the code evaluation, or RESUME/WITH value if override"
//      :code [group!] ;-- or LIT-WORD! name or BLOCK! for dialect
//          "Run the given code if breakpoint does not override"
//  ]
//
static REBNATIVE(pause)
//
// !!! Need definition to test for N_DEBUGGER_pause function
{
    DEBUGGER_INCLUDE_PARAMS_OF_PAUSE;

    if (Do_Breakpoint_Throws(
        D_OUT,
        FALSE, // not a Ctrl-C, it's an actual BREAKPOINT
        ARG(code), // default result if RESUME does not override
        TRUE // execute (run the GROUP! as code, don't return as-is)
    )){
        return R_OUT_IS_THROWN;
    }

    return R_OUT;
}


//
//  Frame_For_Stack_Level: C
//
// Level can be a void, an INTEGER!, an ANY-ACTION!, or a FRAME!.  If
// level is void then it means give whatever the first call found is.
//
// Returns NULL if the given level number does not correspond to a running
// function on the stack.
//
// Can optionally give back the index number of the stack level (counting
// where the most recently pushed stack level is the lowest #)
//
// !!! Unfortunate repetition of logic inside of BACKTRACE.  Assertions
// are used to try and keep them in sync, by noticing during backtrace
// if the stack level numbers being handed out don't line up with what
// would be given back by this routine.  But it would be nice to find a way
// to unify the logic for omitting things like breakpoint frames, or either
// considering pending frames or not.
//
REBFRM *Frame_For_Stack_Level(
    REBCNT *number_out,
    const REBVAL *level,
    REBOOL skip_current
) {
    REBFRM *frame = FS_TOP;
    REBOOL first = TRUE;
    REBINT num = 0;

    if (IS_INTEGER(level)) {
        if (VAL_INT32(level) < 0) {
            //
            // !!! fail() here, or just return NULL?
            //
            return NULL;
        }
    }

    // We may need to skip some number of frames, if there have been stack
    // levels added since the numeric reference point that "level" was
    // supposed to refer to has changed.  For now that's only allowed to
    // be one level, because it's rather fuzzy which stack levels to
    // omit otherwise (pending? parens?)
    //
    if (skip_current)
        frame = frame->prior;

    for (; frame != NULL; frame = frame->prior) {
        if (not Is_Action_Frame(frame)) {
            //
            // Don't consider pending calls, or GROUP!, or any non-invoked
            // function as a candidate to target.
            //
            // !!! The inability to target a GROUP! by number is an artifact
            // of implementation, in that there's no hook in Do_Core() at
            // the point of group evaluation to process the return.  The
            // matter is different with a pending function call, because its
            // arguments are only partially processed--hence something
            // like a RESUME/AT or an EXIT/FROM would not know which array
            // index to pick up running from.
            //
            continue;
        }

        REBOOL pending = Is_Action_Frame_Fulfilling(frame);
        if (not pending) {
            if (first) {
                if (
                    ACT_DISPATCHER(frame->phase) == &N_DEBUGGER_pause
                    or ACT_DISPATCHER(frame->phase) == N_DEBUGGER_breakpoint
                ){
                    // this is considered the "0".  Return it only if 0
                    // requested specifically (you don't "count down to it")
                    //
                    if (IS_INTEGER(level) && num == VAL_INT32(level))
                        goto return_maybe_set_number_out;
                    else {
                        first = FALSE;
                        continue;
                    }
                }
                else {
                    ++num; // bump up from 0
                }
            }
        }

        first = FALSE;

        if (pending) continue;

        if (IS_INTEGER(level) && num == VAL_INT32(level))
            goto return_maybe_set_number_out;

        if (IS_VOID(level) || IS_BLANK(level)) {
            //
            // Take first actual frame if void or blank
            //
            goto return_maybe_set_number_out;
        }
        else if (IS_INTEGER(level)) {
            ++num;
            if (num == VAL_INT32(level))
                goto return_maybe_set_number_out;
        }
        else if (IS_FRAME(level)) {
            if (frame->varlist == CTX_VARLIST(VAL_CONTEXT(level))) {
                goto return_maybe_set_number_out;
            }
        }
        else {
            assert(IS_ACTION(level));
            if (VAL_ACTION(level) == frame->phase)
                goto return_maybe_set_number_out;
        }
    }

    // Didn't find it...
    //
    return NULL;

return_maybe_set_number_out:
    if (number_out)
        *number_out = num;
    return frame;
}


//
//  resume: native/export [
//
//  {Resume after a breakpoint, can evaluate code in the breaking context.}
//
//      /with
//          "Return the given value as return value from BREAKPOINT"
//      value [any-value!]
//          "Value to use"
//      /do
//          "Evaluate given code as return value from BREAKPOINT"
//      code [block!]
//          "Code to evaluate"
//      /at
//          "Return from another call up stack besides the breakpoint"
//      level [frame! action! integer!]
//          "Stack level to target in unwinding (can be BACKTRACE #)"
//  ]
//
static REBNATIVE(resume)
//
// The host breakpoint hook makes a wall to prevent arbitrary THROWs and
// FAILs from ending the interactive inspection.  But RESUME is special, and
// it makes a very specific instruction (with a throw /NAME of the RESUME
// native) to signal a desire to end the interactive session.
//
// When the BREAKPOINT native gets control back from the hook, it interprets
// and executes the instruction.  This offers the additional benefit that
// each host doesn't have to rewrite interpretation in the hook--they only
// need to recognize a RESUME throw and pass the argument back.
{
    DEBUGGER_INCLUDE_PARAMS_OF_RESUME;

    if (REF(with) && REF(do)) {
        //
        // /WITH and /DO both dictate a default return result, (/DO evaluates
        // and /WITH does not)  They are mutually exclusive.
        //
        fail (Error_Bad_Refines_Raw());
    }

    // We don't actually want to run the code for a /DO here.  If we tried
    // to run code from this stack level--and it failed or threw without
    // some special protocol--we'd stay stuck in the breakpoint's sandbox.
    //
    // The /DO code we received needs to actually be run by the host's
    // breakpoint hook, once it knows that non-local jumps to above the break
    // level (throws, returns, fails) actually intended to be "resuming".

    REBARR *instruction = Make_Array(RESUME_INST_MAX);

    if (REF(with)) {
        Init_Logic(ARR_AT(instruction, RESUME_INST_MODE), FALSE); // don't DO
        Move_Value(
            SINK(ARR_AT(instruction, RESUME_INST_PAYLOAD)), ARG(value)
        );
    }
    else if (REF(do)) {
        Init_Logic(ARR_AT(instruction, RESUME_INST_MODE), TRUE); // DO value
        Move_Value(
            SINK(ARR_AT(instruction, RESUME_INST_PAYLOAD)), ARG(code)
        );
    }
    else {
        Init_Blank(ARR_AT(instruction, RESUME_INST_MODE)); // use default

        // Even though this slot should be ignored, use BAR! to try and make
        // any attempts to use it more conspicuous (an unset wouldn't be)
        //
        Init_Bar(ARR_AT(instruction, RESUME_INST_PAYLOAD));
    }

    // We want BREAKPOINT to resume /AT a higher stack level (using the
    // same machinery that definitionally-scoped return would to do it).
    // Frames will be reified as necessary.
    //
    REBFRM *frame;

    if (REF(at)) {
        //
        // `level` is currently allowed to be anything that backtrace can
        // handle (integers, functions for most recent call, literal FRAME!)

        if (!(frame = Frame_For_Stack_Level(NULL, ARG(level), TRUE)))
            fail (Error_Invalid(ARG(level)));

        // !!! It's possible to specify a context to return at which is
        // "underneath" a breakpoint.  So being at a breakpoint and doing
        // `if true [resume/at :if]` would try and specify the IF running
        // in the interactive breakpoint session.  The instruction will
        // error with no breakpoint to catch the resume...but a better error
        // could be given here if the case were detected early.
    }
    else {
        // We just want a BREAKPOINT or PAUSE themselves to return, so find
        // the most recent one (if any, error if none found).

        frame = FS_TOP;
        for (; frame != NULL; frame = frame->prior) {
            if (not Is_Action_Frame(frame))
                continue;
            if (Is_Action_Frame_Fulfilling(frame))
                continue;

            if (
                ACT_DISPATCHER(frame->phase) == &N_DEBUGGER_pause
                || ACT_DISPATCHER(frame->phase) == &N_DEBUGGER_breakpoint
            ) {
                break;
            }
        }

        if (frame == NULL)
            fail (Error_No_Current_Pause_Raw());
    }

    Init_Any_Context(
        ARR_AT(instruction, RESUME_INST_TARGET),
        REB_FRAME,
        Context_For_Frame_May_Reify_Managed(frame)
    );

    TERM_ARRAY_LEN(instruction, RESUME_INST_MAX);

    // We put the resume instruction into a GROUP! just to make it a little
    // bit more unusual than a BLOCK!.  More hardened approaches might put
    // a special symbol as a "magic number" or somehow version the protocol,
    // but for now we'll assume that the only decoder is BREAKPOINT and it
    // will be kept in sync.
    //
    DECLARE_LOCAL (cell);
    Init_Group(cell, instruction);

    // Throw the instruction with the name of the RESUME function
    //
    Move_Value(D_OUT, ACT_ARCHETYPE(frame_->phase));
    CONVERT_NAME_TO_THROWN(D_OUT, cell);
    return R_OUT_IS_THROWN;
}


//
//  Host_Breakpoint_Quitting_Hook()
//
// This hook is registered with the core as the function that gets called
// when a breakpoint triggers.
//
// There are only two options for leaving the hook.  One is to return TRUE
// and thus signal a QUIT, where `instruction` is the value to quit /WITH.
// The other choice is to return FALSE, where `instruction` is a purposefully
// constructed "resume instruction".
//
// (Note: See remarks in the implementation of `REBNATIVE(resume)` for the
// format of resume instructions.  But generally speaking, the host does not
// need to know the details, as this represents a protocol that is supposed
// to only be between BREAKPOINT and RESUME.  So the host just needs to
// bubble up the argument to a throw that had the RESUME native's name on it,
// when that type of throw is caught.)
//
// The ways in which a breakpoint hook can be exited are constrained in
// order to "sandbox" it somewhat.  Though a nested REPL may be invoked in
// response to a breakpoint--as is done here--continuation should be done
// purposefully vs. "accidentally resuming" just because a FAIL or a THROW
// happened.  One does not want to hit a breakpoint, then mistype a variable
// name and trigger an error that does a longjmp that effectively cancels
// the interactive breakpoint session!
//
// Hence RESUME and QUIT should be the only ways to get out of the breakpoint.
// Note that RESUME/DO provides a loophole, where it's possible to run code
// that performs a THROW or FAIL which is not trapped by the sandbox.
//
REBOOL Host_Breakpoint_Quitting_Hook(
    REBVAL *instruction_out,
    REBOOL interrupted
){
    UNUSED(interrupted); // not passed to the REPL, should it be?

    // We save the stack level from before, so that we can put it back when
    // we resume.  Each new breakpoint nesting hit will default to debugging
    // stack level 1...e.g. the level that called breakpoint.
    //
    REBCNT old_stack_level = HG_Stack_Level;

    DECLARE_LOCAL (level);
    Init_Integer(level, 1);

    if (Frame_For_Stack_Level(NULL, level, FALSE) != NULL)
        HG_Stack_Level = 1;
    else
        HG_Stack_Level = 0; // Happens if you just type "breakpoint"

    //==//// SPAWN NESTED REPL ////////////////////////////////////////////=//

    // Note that to avoid spurious longjmp clobbering warnings, last_failed
    // cannot be stack allocated to act cumulatively across fail()s.  (Dumb
    // compilers can't tell that all longjmps set error to non-NULL and will
    // subsequently assign last_failed to TRUE_VALUE.)
    //
    const REBVAL **last_failed = cast(const REBVAL**, malloc(sizeof(REBVAL*)));
    *last_failed = BLANK_VALUE; // indicate first call to REPL

    Init_Void(instruction_out);

    DECLARE_LOCAL (frame);
    Init_Blank(frame);

    PUSH_GUARD_VALUE(frame);

    while (TRUE) {
    loop:
        //
        // When we're stopped at a breakpoint, then the REPL has a modality to
        // it of "which stack level you are examining".  The DEBUG command can
        // change this, so at the moment it has to be refreshed each time an
        // evaluation is performed.

        Init_Integer(level, HG_Stack_Level);

        REBFRM *f = Frame_For_Stack_Level(NULL, level, FALSE);
        assert(f != NULL);

        Init_Any_Context(
            frame,
            REB_FRAME,
            Context_For_Frame_May_Reify_Managed(f)
        );

        const REBOOL fully = TRUE; // error if not all arguments consumed

        // Generally speaking, we do not want the trace level to apply to the
        // REPL execution itself.
        //
        REBINT Save_Trace_Level = Trace_Level;
        REBINT Save_Trace_Depth = Trace_Depth;
        Trace_Level = 0;
        Trace_Depth = 0;

        DECLARE_LOCAL (code);
        if (Apply_Only_Throws(
            code, // where return value of HOST-REPL is saved
            fully,
            HG_Host_Repl, // HOST-REPL function to run
            instruction_out, // last-result (void on first run through loop)
            *last_failed, // TRUE, FALSE, BLANK! on first run, BAR! if HALT
            level, // focus-level
            frame, // focus-frame
            END
        )){
            // The REPL should not execute anything that should throw.
            // Determine graceful way of handling if it does.
            //
            panic (code);
        }

        Trace_Level = Save_Trace_Level;
        Trace_Depth = Save_Trace_Depth;

        if (not IS_BLOCK(code))
            panic (code);

        struct Reb_State state;
        REBCTX *error;

        // Breakpoint REPLs are nested, and we may wish to jump out of
        // them to the topmost level via a HALT.  However, all other
        // errors need to be confined, so that if one is doing evaluations
        // during the pause of a breakpoint an error doesn't "accidentally
        // resume" by virtue of jumping the stack out of the REPL.
        //
        // (The topmost layer REPL, however, needs to catch halts in order
        // to keep control and not crash out.
        //
        PUSH_TRAP(&error, &state);

    // The first time through the following code 'error' will be NULL, but...
    // `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

        if (error != NULL) {
            Init_Error(instruction_out, error);
            *last_failed = TRUE_VALUE;
            goto loop;
        }

        if (Do_Any_Array_At_Throws(instruction_out, code)) {
            if (
                IS_ACTION(instruction_out)
                && VAL_ACT_DISPATCHER(instruction_out) == &N_DEBUGGER_resume
            ){
                // This means we're done with the embedded REPL.  We want
                // to resume and may be returning a piece of code that
                // will be run by the finishing BREAKPOINT command in the
                // target environment.
                //
                // !!! Currently we do not catch the THROW here, because we
                // do not have the RESUME native function value on hand.  The
                // only way we get it is when the RESUME itself runs.  With
                // no NAT_VALUE(resume) available, we need to preserve the
                // one in this instruction to retransmit it.
                //
                DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);
                *last_failed = FALSE_VALUE;
                goto cleanup_and_return;
            }

            if (
                IS_ACTION(instruction_out)
                && VAL_ACT_DISPATCHER(instruction_out) == &N_quit
            ){
                // It would be frustrating if the system did not respond
                // to QUIT and forced you to do `resume/with [quit]`.  So
                // this is *not* caught, rather signaled to the calling core
                // by returning TRUE from the hook.
                //
                DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);
                CATCH_THROWN(instruction_out, instruction_out);
                *last_failed = TRUE_VALUE; // signal "quitting"
                goto cleanup_and_return;
            }

            fail (Error_No_Catch_For_Throw(instruction_out));
        }

        DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

        // NOTE: Although the operation has finished at this point, it may
        // be that a Ctrl-C set up a pending FAIL, which will be triggered
        // during output below.  See the PUSH_TRAP in the caller.

        // Result will be printed by next loop
        //
        *last_failed = FALSE_VALUE;
    }

cleanup_and_return:
    DROP_GUARD_VALUE(frame);

    // Restore stack level, which is presumably still valid (there shouldn't
    // have been any way to "delete levels from the stack above" while we
    // were nested).
    //
    // !!! It might be nice if the prompt had a way of conveying that you were
    // in nested breaks, and give the numberings of them adjusted:
    //
    //     |14|6|1|>> ...
    //
    // Or maybe that's TMI?
    //
    HG_Stack_Level = old_stack_level;

    // Due to conservative longjmp clobber warnings in some gcc versions,
    // quitting is not maintained as a separate boolean across the PUSH_TRAP,
    // but conveyed here through `last_failed`, which has already been
    // dynamically allocated to avoid the warning.
    //
    REBOOL quitting = VAL_LOGIC(*last_failed);
    free(m_cast(REBVAL**, last_failed)); // pointer to mutable in free(), C++
    assert(THROWN(instruction_out) || quitting);
    return quitting;
}


//
//  Do_Breakpoint_Throws: C
//
// A call to Do_Breakpoint_Throws does delegation to a hook in the host, which
// (if registered) will generally start an interactive session for probing the
// environment at the break.  The RESUME native cooperates by being able to
// give back a value (or give back code to run to produce a value) that the
// call to breakpoint returns.
//
// RESUME has another feature, which is to be able to actually unwind and
// simulate a return /AT a function *further up the stack*.  (This may be
// switched to a feature of a "step out" command at some point.)
//
REBOOL Do_Breakpoint_Throws(
    REBVAL *out,
    REBOOL interrupted, // Ctrl-C (as opposed to a BREAKPOINT)
    const REBVAL *default_value,
    REBOOL do_default
){
    const REBVAL *target = BLANK_VALUE;

    DECLARE_LOCAL (temp);

    // We call the breakpoint hook in a loop, in order to keep running if any
    // inadvertent FAILs or THROWs occur during the interactive session.
    // Only a conscious call of RESUME speaks the protocol to break the loop.
    //
    while (TRUE) {
        struct Reb_State state;
        REBCTX *error;

    push_trap:
        PUSH_TRAP(&error, &state);

        // The host may return a block of code to execute, but cannot
        // while evaluating do a THROW or a FAIL that causes an effective
        // "resumption".  HALT and QUIT are exceptions, where a desire to quit
        // is indicated by the thrown value of the breakpoint hook (which may
        // or may not decide to request a quit based on QUIT being run).
        //
        // The core doesn't want to get involved in presenting UI, so if
        // an error makes it here and wasn't trapped by the host first that
        // is a bug in the host.  It should have done its own PUSH_TRAP.
        //
        if (error) {
          #if !defined(NDEBUG)
            printf("Error not trapped during breakpoint\n");
            panic (error);
          #endif

            // In release builds, if an error managed to leak out of the
            // host's breakpoint hook somehow...just re-push the trap state
            // and try it again.
            //
            goto push_trap;
        }

        // Call the host's breakpoint hook.
        //
        // The DECLARE_LOCAL is here and not outside the loop
        // due to wanting to avoid "longjmp clobbering" warnings
        // (seen in optimized builds on Android).
        //
        DECLARE_LOCAL (inst);
        if (Host_Breakpoint_Quitting_Hook(inst, interrupted)) {
            //
            // If a breakpoint hook returns TRUE that means it wants to quit.
            // The value should be the /WITH value (as in QUIT/WITH), so
            // not actually a "resume instruction" in this case.
            //
            assert(!THROWN(inst));
            Move_Value(out, NAT_VALUE(quit));
            CONVERT_NAME_TO_THROWN(out, inst);
            return TRUE; // TRUE = threw
        }

        // If a breakpoint handler returns FALSE, then it should have passed
        // back a "resume instruction" triggered by a call like:
        //
        //     resume/do [fail "This is how to fail from a breakpoint"]
        //
        // So now that the handler is done, we will allow any code handed back
        // to do whatever FAIL it likes vs. trapping that here in a loop.
        //
        DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

        // Decode and process the "resume instruction"

        #if !defined(NDEBUG)
            REBOOL found = FALSE;
        #endif

        assert(THROWN(inst) && IS_ACTION(inst));

        DECLARE_LOCAL(resume_native);
        Move_Value(resume_native, inst);
        CLEAR_VAL_FLAG(resume_native, VALUE_FLAG_THROWN);

        CATCH_THROWN(inst, inst);

        assert(IS_GROUP(inst));
        assert(VAL_LEN_HEAD(inst) == RESUME_INST_MAX);

        // The instruction was built from raw material, non-relative
        //
        REBVAL *mode = KNOWN(VAL_ARRAY_AT_HEAD(inst, RESUME_INST_MODE));
        REBVAL *payload
            = KNOWN(VAL_ARRAY_AT_HEAD(inst, RESUME_INST_PAYLOAD));
        target = KNOWN(VAL_ARRAY_AT_HEAD(inst, RESUME_INST_TARGET));

        assert(IS_FRAME(target));

        // The first thing we need to do is determine if the target we
        // want to return to has another breakpoint sandbox blocking
        // us.  If so, what we need to do is actually retransmit the
        // resume instruction so it can break that wall, vs. transform
        // it into an EXIT/FROM that would just get intercepted.
        //
        REBFRM *frame;
        for (frame = FS_TOP; frame != NULL; frame = frame->prior) {
            if (not Is_Action_Frame(frame))
                continue;
            if (Is_Action_Frame_Fulfilling(frame))
                continue;

            if (
                frame != FS_TOP
                && (
                    ACT_DISPATCHER(frame->phase) == &N_DEBUGGER_pause
                    || ACT_DISPATCHER(frame->phase) == &N_DEBUGGER_breakpoint
                )
            ) {
                // We hit a breakpoint (that wasn't this call to
                // breakpoint, at the current FS_TOP) before finding
                // the sought after target.  Retransmit the resume
                // instruction so that level will get it instead.
                //
                Move_Value(out, resume_native);
                CONVERT_NAME_TO_THROWN(out, inst);
                return TRUE; // TRUE = thrown
            }

            // If the frame were the one we were looking for, it would be
            // reified (so it would have a context to match)
            //
            if (frame->varlist == NULL)
                continue;

            if (VAL_CONTEXT(target) == CTX(frame->varlist)) {
                // Found a match before hitting any breakpoints, so no
                // need to retransmit.
                //
            #if !defined(NDEBUG)
                found = TRUE;
            #endif
                break;
            }
        }

        // RESUME should not have been willing to use a target that
        // is not on the stack.
        //
    #if !defined(NDEBUG)
        assert(found);
    #endif

        if (IS_BLANK(mode)) {
            //
            // If the resume instruction had no /DO or /WITH of its own,
            // then it doesn't override whatever the breakpoint provided
            // as a default.  (If neither the breakpoint nor the resume
            // provided a /DO or a /WITH, result will be void.)
            //
            goto return_default; // heeds `target`
        }

        assert(IS_LOGIC(mode));

        if (VAL_LOGIC(mode)) {
            if (Do_Any_Array_At_Throws(temp, payload)) {
                //
                // Throwing is not compatible with /AT currently.
                //
                if (!IS_BLANK(target))
                    fail (Error_No_Catch_For_Throw(temp));

                // Just act as if the BREAKPOINT call itself threw
                //
                Move_Value(out, temp);
                return TRUE; // TRUE = thrown
            }

            // Ordinary evaluation result...
        }
        else
            Move_Value(temp, payload);

        // The resume instruction will be GC'd.
        //
        goto return_temp;
    }

    DEAD_END;

return_default:

    if (do_default) {
        if (Do_Any_Array_At_Throws(temp, default_value)) {
            //
            // If the code throws, we're no longer in the sandbox...so we
            // bubble it up.  Note that breakpoint runs this code at its
            // level... so even if you request a higher target, any throws
            // will be processed as if they originated at the BREAKPOINT
            // frame.  To do otherwise would require the EXIT/FROM protocol
            // to add support for DO-ing at the receiving point.
            //
            Move_Value(out, temp);
            return TRUE; // TRUE = thrown
        }
    }
    else
        Move_Value(temp, default_value); // generally void if no /WITH

return_temp:
    //
    // If the target is a function, then we're looking to simulate a return
    // from something up the stack.  This uses the same mechanic as
    // definitional returns--a throw named by the function frame.
    //
    // !!! There is a weak spot in definitional returns for ACTION! that
    // they can only return to the most recent invocation; which is a weak
    // spot of ACTION! in general with stack relative variables.  Also,
    // natives do not currently respond to definitional returns...though
    // they can do so just as well as ACTION! can.
    //
    Make_Thrown_Unwind_Value(out, target, temp, NULL);
    return TRUE; // TRUE = thrown
}


//
//  backtrace-index: native/export [
//
//  "Get the index of a given frame or function as BACKTRACE shows it"
//
//      return: [integer! blank!]
//          {BLANK! if no match}
//      level [action! frame!]
//          {The action or frame to get an index for}
//  ]
//
static REBNATIVE(backtrace_index)
{
    DEBUGGER_INCLUDE_PARAMS_OF_BACKTRACE_INDEX;

    REBCNT number;

    if (NULL != Frame_For_Stack_Level(&number, ARG(level), TRUE)) {
        Init_Integer(D_OUT, number);
        return R_OUT;
    }

    return R_BLANK;
}


//
//  Init_Debugger: C
//
void Init_Debugger(void)
{
    // !!! Register EXPERIMENTAL breakpoint hook.
    //
    PG_Breakpoint_Hook = &Do_Breakpoint_Throws;
}


//
//  Shutdown_Debugger: C
//
void Shutdown_Debugger(void)
{
    PG_Breakpoint_Hook = NULL;
}


//
//  debug: native/export [
//
//  {Dialect for interactive debugging, see documentation for details}
//
//      'value [<opt> integer! frame! action! block!]
//          {Stack level to inspect or dialect block, or enter debug mode}
//
//  ]
//
static REBNATIVE(debug)
//
// The DEBUG command modifies state that is specific to controlling variables
// and behaviors in the REPL.  At the moment, all it does is change which
// stack level is being inspected in the REPL.
//
{
    DEBUGGER_INCLUDE_PARAMS_OF_DEBUG;

    REBVAL *value = ARG(value);

    if (IS_VOID(value)) {
        //
        // e.g. just `>> debug` and [enter] in the console.  Ideally this
        // would shift the REPL into a mode where all commands issued were
        // assumed to be in the debug dialect, similar to Ren Garden's
        // modalities like `debug>>`.
        //
        Debug_Fmt("Sorry, there is no debug>> 'mode' yet in the console.");
        goto modify_with_confidence;
    }

    if (IS_INTEGER(value) || IS_FRAME(value) || IS_ACTION(value)) {
        REBFRM *frame;

        // We pass TRUE here to account for an extra stack level... the one
        // added by DEBUG itself, which presumably should not count.
        //
        if (!(frame = Frame_For_Stack_Level(&HG_Stack_Level, value, TRUE)))
            fail (Error_Invalid(value));

        Init_Near_For_Frame(D_OUT, frame);
        return R_OUT;
    }

    assert(IS_BLOCK(value));

    Debug_Fmt(
        "Sorry, but the `debug [...]` dialect is not defined yet.\n"
        "Change the stack level (integer!, frame!, action!)\n"
        "Or try out these commands:\n"
        "\n"
        "    BREAKPOINT, RESUME, BACKTRACE\n"
    );

modify_with_confidence:
    Debug_Fmt(
        "(Note: Ren-C is 'modify-with-confidence'...so just because a debug\n"
        "feature you want isn't implemented doesn't mean you can't add it!)\n"
    );

    return R_BLANK;
}

#include "tmp-mod-debugger-last.h"
