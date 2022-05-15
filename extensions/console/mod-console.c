//
//  File: %mod-console.c
//  Summary: "Read/Eval/Print Loop (REPL) Skinnable Console for Rebol"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2019 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "reb-config.h"

#if TO_WINDOWS

    #undef _WIN32_WINNT  // https://forum.rebol.info/t/326/4
    #define _WIN32_WINNT 0x0501  // Minimum API target: WinXP
    #define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
    #include <windows.h>

    #undef IS_ERROR  // %windows.h defines this, but so does %sys-core.h

#elif defined(TO_EMSCRIPTEN)
    //
    // Nothing needed here yet...
    //
#else

    #include <signal.h>  // needed for SIGINT, SIGTERM, SIGHUP

#endif


#include "sys-core.h"

#include "tmp-mod-console.h"


//=//// USER-INTERRUPT/HALT HANDLING (Ctrl-C, Escape, etc.) ///////////////=//
//
// There's clearly contention for what a user-interrupt key sequence should
// be, given that "Ctrl-C" is copy in GUI applications.  Yet handling escape
// is not necessarily possible on all platforms and situations.
//
// For console applications, we assume that the program starts with user
// interrupting enabled by default...so we have to ask for it not to be when
// it would be bad to have the Rebol stack interrupted--during startup, or
// when in the "kernel" of the host console.
//
// (Note: If halting is done via Ctrl-C, technically it may be set to be
// ignored by a parent process or context, in which case conventional wisdom
// is that we should not be enabling it ourselves.  Review.)
//

bool halting_enabled = true;

#if TO_EMSCRIPTEN  //=//// EMSCRIPTEN //////////////////////////////////////=//

// !!! Review how an emscripten console extension should be hooking something
// like a keyboard shortcut for breaking.  With the pthread model, there may
// be shared memory for the GUI to be able to poke a value in that the running
// code can see to perceive a halt.

void Disable_Halting(void) {}
void Enable_Halting(void) {}


#elif TO_WINDOWS  //=//// WINDOWS //////////////////////////////////////////=//

// Windows handling is fairly simplistic--this is the callback passed to
// `SetConsoleCtrlHandler()`.  The most annoying thing about cancellation in
// windows is the limited signaling possible in the terminal's readline.
//
BOOL WINAPI Handle_Break(DWORD dwCtrlType)
{
    switch (dwCtrlType) {
      case CTRL_C_EVENT:
      case CTRL_BREAK_EVENT:
        rebHalt();
        return TRUE;  // TRUE = "we handled it"

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
        return FALSE;  // FALSE = "we didn't handle it"
    }
}

BOOL WINAPI Handle_Nothing(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_C_EVENT)
        return TRUE;

    return FALSE;
}

void Disable_Halting(void)
{
    assert(halting_enabled);

    SetConsoleCtrlHandler(Handle_Break, FALSE);
    SetConsoleCtrlHandler(Handle_Nothing, TRUE);

    halting_enabled = false;
}

void Enable_Halting(void)
{
    assert(not halting_enabled);

    SetConsoleCtrlHandler(Handle_Break, TRUE);
    SetConsoleCtrlHandler(Handle_Nothing, FALSE);

    halting_enabled = true;
}

#else  //=//// POSIX, LINUX, MAC, etc. ////////////////////////////////////=//

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

void Disable_Halting(void)
{
    assert(halting_enabled);

    sigaction(SIGINT, nullptr, &old_action); // fetch current handler
    if (old_action.sa_handler != SIG_IGN) {
        struct sigaction new_action;
        new_action.sa_handler = SIG_IGN;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction(SIGINT, &new_action, nullptr);
    }

    halting_enabled = false;
}

void Enable_Halting(void)
{
    assert(not halting_enabled);

    if (old_action.sa_handler != SIG_IGN) {
        struct sigaction new_action;
        new_action.sa_handler = &Handle_Signal;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction(SIGINT, &new_action, nullptr);
    }

    halting_enabled = true;
}

#endif  //=///////////////////////////////////////////////////////////////=//



// This is called when either the console is running some untrusted skin code
// for its own implementation, or when it wants to execute code on the user's
// behalf.  If the code is on the user's behalf, then any tracing or debug
// hooks will have been enabled before the rebRescue() call invoking this.
//
//
static REBVAL *Run_Sandboxed_Group(REBVAL *group) {
    //
    // DON'T ADD ANY MORE LIBREBOL CODE HERE.  If this is a user-requested
    // evaluation, then any extra libRebol code run here will wind up being
    // shown in a TRACE.  The only thing that's acceptable to see in the
    // backtrace is the GROUP! itself that we are running.  (If we didn't
    // want that, getting rid of it would take some magic).
    //
    // So don't add superfluous libRebol calls here, except to debug.
    //
    // The "^" helps us discern if the value vaporizes completely or not.
    // The group means code cannot look backwards and quote the ^.
    //
    return rebValueInterruptible("^", group);  // ownership gets proxied
}


//
//  export console: native [
//
//  {Runs customizable Read-Eval-Print Loop, may "provoke" code before input}
//
//      return: "Exit code, RESUME instruction, or handle to evaluator hook"
//          [integer! meta-group! handle!]
//      /provoke "Block must return a console state, group is cancellable"
//          [block! group!]
//      /resumable "Allow RESUME instruction (will return a META-GROUP!)"
//      /skin "File containing console skin, or MAKE CONSOLE! derived object"
//          [file! object!]
//  ]
//
REBNATIVE(console)
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

    // !!! The initial usermode console implementation was geared toward a
    // single `system.console` object.  But the debugger raised the issue of
    // nested sessions which might have a different skin.  So save whatever
    // the console object was if it is being overridden.

    REBVAL *old_console = rebValue(":system.console");
    if (REF(skin))
        rebElide("system.console: _");  // !!! needed for now

    // We only enable halting (e.g. Ctrl-C, or Escape, or whatever) when user
    // code is running...not when the HOST-CONSOLE function itself is, or
    // during startup.  (Enabling it during startup would require a special
    // "kill" mode that did not call rebHalt(), as basic startup cannot
    // meaningfully be halted--the system would be in an incomplete state.)
    //
    bool was_halting_enabled = halting_enabled;
    if (was_halting_enabled)
        Disable_Halting();

    REBVAL *result = nullptr;
    bool no_recover = false;  // allow one try at HOST-CONSOLE internal error

    REBVAL *code;
    if (REF(provoke)) {
        code = rebArg("provoke");  // fetch as an API handle
        goto provoked;
    }
    else {
        code = rebBlank();
        result = rebValue("'~startup~");  // signal starting
    }

    while (true) {
       assert(not halting_enabled);  // not while HOST-CONSOLE is on the stack

      recover: ;  // Note: semicolon needed as next statement is declaration

        // This runs the HOST-CONSOLE, which returns *requests* to execute
        // arbitrary code by way of its return results.  The ENTRAP is thus
        // here to intercept bugs *in HOST-CONSOLE itself*.  Any evaluations
        // for the user (or on behalf of the console skin) are done in
        // Run_Sandboxed_Group().
        //
        // !!! We use rebQ() here and not "@" due to the current behavior of
        // @ which will make BAD-WORD!s into isotopes.  That behavior is to
        // help with treatment of ~null~, but perhaps it should be exclusive
        // to ~null~.  Either way, rebQ() would be needed if the distinction
        // were to be important.
        //
        REBVAL *trapped;  // Note: goto would cross initialization
        trapped = rebValue("entrap [",
            "ext-console-impl",  // action! that takes 4 args, run it
                rebQ(code),  // group! or block! executed prior (or blank!)
                rebQ(result),  // prior result quoted, or error (or blank!)
                "did", rebQ(REF(resumable)),
                rebQ(REF(skin)),
        "]");

        rebRelease(code);
        rebRelease(result);

        if (rebUnboxLogic("error? @", trapped)) {
            //
            // If the HOST-CONSOLE function has any of its own implementation
            // that could raise an error (or act as an uncaught throw) it
            // *should* be returned as a BLOCK!.  This way the "console skin"
            // can be reset to the default.  If HOST-CONSOLE itself fails
            // (e.g. a typo in the implementation) there's probably not much
            // use in trying again...but give it a chance rather than just
            // crash.  Pass it back something that looks like an instruction
            // it might have generated (a BLOCK!) asking itself to crash.

            if (no_recover)
                rebJumps("panic @", trapped);

            code = rebValue("[#host-console-error]");
            result = trapped;
            no_recover = true;  // no second chances until user code runs
            goto recover;
        }

        code = rebValue("unquote @", trapped);  // entrap quotes non-error
        rebRelease(trapped); // don't need the outer block any more

      provoked:

        if (rebUnboxLogic("integer? @", code))
            break;  // when HOST-CONSOLE returns INTEGER! it means exit code

        if (rebDid("match [meta-group! handle!] @", code)) {
            assert(REF(resumable));
            break;
        }

        bool is_console_instruction = rebUnboxLogic("block? @", code);
        REBVAL *group;

        if (is_console_instruction) {
            group = rebValue("as group! @", code);  // to run without DO
        }
        else {
            group = rebValue("@", code);  // rebRelease() w/o affecting code

            // If they made it to a user mode instruction, the console skin
            // must not be broken beyond all repair.  So re-enable recovery.
            //
            no_recover = false;
        }

        // Both console-initiated and user-initiated code is cancellable with
        // Ctrl-C (though it's up to HOST-CONSOLE on the next iteration to
        // decide whether to accept the cancellation or consider it an error
        // condition or a reason to fall back to the default skin).
        //
        Enable_Halting();
        result = rebRescue(cast(REBDNG*, &Run_Sandboxed_Group), group);
        rebRelease(group);  // Note: does not release `code`
        Disable_Halting();
    }

    // Exit code is now an INTEGER! or a resume instruction PATH!

    if (was_halting_enabled)
        Enable_Halting();

    rebElide("system.console: @", rebR(old_console));

    return code;  // http://stackoverflow.com/q/1101957/
}
