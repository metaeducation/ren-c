//
//  file: %mod-console.c
//  summary: "Read/Eval/Print Loop (REPL) Skinnable Console for Rebol"
//  section: Extension
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2018 Rebol Open Source Contributors
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

#ifdef TO_WINDOWS
    #undef _WIN32_WINNT // https://forum.rebol.info/t/326/4
    #define _WIN32_WINNT 0x0501 // Minimum API target: WinXP
    #include <windows.h>

    #undef OUT  // %minwindef.h defines this, we have a better use for it
    #undef VOID  // %winnt.h defines this, we have a better use for it
#else
    #include <signal.h> // needed for SIGINT, SIGTERM, SIGHUP
#endif

#include "sys-core.h"

#include "tmp-mod-console.h"


// Assume that Ctrl-C is enabled in a console application by default.
// (Technically it may be set to be ignored by a parent process or context,
// in which case conventional wisdom is that we should not be enabling it
// ourselves.)
//
bool ctrl_c_enabled = true;


#ifdef TO_WINDOWS

//
// This is the callback passed to `SetConsoleCtrlHandler()`.
//
BOOL WINAPI Halt_On_Ctrl_C_Or_Break(DWORD dwCtrlType)
{
    switch (dwCtrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
        rebHalt();
        return TRUE; // TRUE = "we handled it"

    case CTRL_CLOSE_EVENT:
        //
        // !!! Theoretically the close event could confirm that the user
        // wants to exit, if there is possible unsaved state.  As a UI
        // premise this is probably less good than persisting the state
        // and bringing it back.
        //
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        //
        // They pushed the close button, did a shutdown, etc.  Exit.
        //
        // !!! Review arbitrary "100" exit code here.
        //
        exit(100);

    default:
        return FALSE; // FALSE = "we didn't handle it"
    }
}

BOOL WINAPI Suppress_Ctrl_C(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_C_EVENT)
        return TRUE;

    return FALSE;
}

void Disable_Ctrl_C(void)
{
    assert(ctrl_c_enabled);

    SetConsoleCtrlHandler(Halt_On_Ctrl_C_Or_Break, FALSE);
    SetConsoleCtrlHandler(Suppress_Ctrl_C, TRUE);

    ctrl_c_enabled = false;
}

void Enable_Ctrl_C(void)
{
    assert(not ctrl_c_enabled);

    SetConsoleCtrlHandler(Halt_On_Ctrl_C_Or_Break, TRUE);
    SetConsoleCtrlHandler(Suppress_Ctrl_C, FALSE);

    ctrl_c_enabled = true;
}

#else

// SIGINT is the interrupt usually tied to "Ctrl-C".  Note that if you use
// just `signal(SIGINT, Handle_Signal);` as R3-Alpha did, this means that
// blocking read() calls will not be interrupted with EINTR.  One needs to
// use sigaction() if available...it's a slightly newer API.
//
// http://250bpm.com/blog:12
//
// !!! What should be done about SIGTERM ("polite request to end", default
// unix kill) or SIGHUP ("user's terminal disconnected")?  Is it useful to
// register anything for these?  R3-Alpha did, and did the same thing as
// SIGINT.  Not clear why.  It did nothing for SIGQUIT:
//
// SIGQUIT is used to terminate a program in a way that is designed to
// debug it, e.g. a core dump.  Receiving SIGQUIT is a case where
// program exit functions like deletion of temporary files may be
// skipped to provide more state to analyze in a debugging scenario.
//
// SIGKILL is the impolite signal for shutdown; cannot be hooked/blocked

static void Handle_Signal(int sig)
{
    UNUSED(sig);
    rebHalt();
}

struct sigaction old_action;

void Disable_Ctrl_C(void)
{
    assert(ctrl_c_enabled);

    sigaction(SIGINT, nullptr, &old_action); // fetch current handler
    if (old_action.sa_handler != SIG_IGN) {
        struct sigaction new_action;
        new_action.sa_handler = SIG_IGN;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction(SIGINT, &new_action, nullptr);
    }

    ctrl_c_enabled = false;
}

void Enable_Ctrl_C(void)
{
    assert(not ctrl_c_enabled);

    if (old_action.sa_handler != SIG_IGN) {
        struct sigaction new_action;
        new_action.sa_handler = &Handle_Signal;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction(SIGINT, &new_action, nullptr);
    }

    ctrl_c_enabled = true;
}

#endif



// Can't just use a TRAP when running user code, because it might legitimately
// evaluate to an ERROR! value, as well as PANIC.  Uses rebRescue().

static Value* Run_Sandboxed_Code(Value* group_or_block) {
    //
    // Don't want to use DO here, because that would add an extra stack
    // level of Rebol ACTION! in the backtrace.  See notes on rebValueInline()
    // for its possible future.
    //
    Value* result = rebValueInline(group_or_block);

    Value* out = rebValue(NAT_VALUE(LIFT), rebQ(result));
    rebRelease(result);
    return out;
}


//
//  export console: native [
//
//  {Runs an instance of a customizable Read-Eval-Print Loop}
//
//      return: "Integer if QUIT result, path if RESUME instruction"
//          [integer! path!]
//      /provoke "Give the console some code to run before taking user input"
//      provocation "Block must return a console state, group is cancellable"
//          [block! group!]
//      /resumable "Allow RESUME instruction (will return a PATH!)"
//  ]
//
DECLARE_NATIVE(CONSOLE)
//
// !!! The idea behind the console is that it can be called with skinning;
// so that if BREAKPOINT wants to spin up a console, it can...but with a
// little bit of injected information like telling you the current stack
// level it's focused on.  How that's going to work is still pretty up
// in the air.
//
// What it will return will be either an exit code (INTEGER!), a signal for
// cancellation (BLANK!), or a debugging instruction (BLOCK!).
{
    CONSOLE_INCLUDE_PARAMS_OF_CONSOLE;

    // We only enable Ctrl-C when user code is running...not when the
    // HOST-CONSOLE function itself is, or during startup.  (Enabling it
    // during startup would require a special "kill" mode that did not
    // call rebHalt(), as basic startup cannot meaningfully be halted.)
    //
    bool was_ctrl_c_enabled = ctrl_c_enabled;
    if (was_ctrl_c_enabled)
        Disable_Ctrl_C();

    Value* result = nullptr;
    bool no_recover = false; // allow one try at HOST-CONSOLE internal error

    Value* code;
    if (Bool_ARG(PROVOKE)) {
        code = rebValue(rebQ(ARG(PROVOCATION)));  // turn into API value
        goto provoked;
    }
    else
        code = rebBlank();

    while (true) {
       assert(not ctrl_c_enabled); // not while HOST-CONSOLE is on the stack

    recover:;

        // This runs the HOST-CONSOLE, which returns *requests* to execute
        // arbitrary code by way of its return results.  The ENRESCUE call
        // is thus here to intercept bugs *in HOST-CONSOLE itself*.  Any
        // evaluations for the user (or on behalf of the console skin) are
        // done in Run_Sandboxed_Code().
        //
        Value* trapped; // goto crosses initialization
        Value* resumable;
        resumable = rebLogic(Bool_ARG(RESUMABLE));
        trapped = rebValue(
            "sys/util/enrescue [",
                "ext-console-impl", // action! that takes 2 args, run it
                rebQ(code), // group!/block! executed prior (or blank!)
                rebQ(result), // prior result lift'd, or error
                rebQ(resumable),
            "]"
        );

        rebRelease(resumable);
        rebRelease(code);
        rebRelease(result);

        if (rebDid("lib/error?", rebQ(trapped))) {
            //
            // If the HOST-CONSOLE function has any of its own implementation
            // that could raise an error (or act as an uncaught throw) it
            // *should* be returned as a BLOCK!.  This way the "console skin"
            // can be reset to the default.  If HOST-CONSOLE itself fails
            // (e.g. a typo in the implementation) there's probably not much
            // use in trying again...but give it a chance rather than just
            // crash.  Pass it back something that looks like an instruction
            // it might have generated (a BLOCK!) asking itself to crash.

            if (no_recover) {
                rebElide("print {** CONSOLE INTERNAL ERROR **}");
                rebElide("print mold", trapped);
                rebJumps("crash", trapped);
            }

            code = rebValue("[#host-console-error]");
            result = trapped;
            no_recover = true; // no second chances until user code runs
            goto recover;
        }

        code = rebValue(trapped); // enrescue metas the output
        rebRelease(trapped); // don't need the outer block any more

      provoked:;
        if (rebDid("integer?", rebQ(code)))
            break; // when HOST-CONSOLE returns INTEGER! it means an exit code

        if (rebDid("path?", rebQ(code))) {
            assert(Bool_ARG(RESUMABLE));
            break;
        }

        bool is_console_instruction = rebDid("block?", rebQ(code));

        if (not is_console_instruction) {
            //
            // If they made it to a user mode instruction, re-enable recovery.
            //
            no_recover = false;
        }

        // Both GROUP! and BLOCK! code is cancellable with Ctrl-C (though it's
        // up to HOST-CONSOLE on the next iteration to decide whether to
        // accept the cancellation or consider it an error condition or a
        // reason to fall back to the default skin).
        //
        Enable_Ctrl_C();
        result = rebRescue(f_cast(REBDNG*, &Run_Sandboxed_Code), code);
        Disable_Ctrl_C();
    }

    // Exit code is now an INTEGER! or a resume instruction PATH!

    if (was_ctrl_c_enabled)
        Enable_Ctrl_C();

    return code; // http://stackoverflow.com/q/1101957/
}
