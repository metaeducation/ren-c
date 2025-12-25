//
//  file: %mod-console.c
//  summary: "[Read Eval Print] Loop (REPL) Skinnable Console for Rebol"
//  section: extension
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2025 Ren-C Open Source Contributors
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
// On POSIX systems it uses <termios.h> to implement line editing:
//
//   http://pubs.opengroup.org/onlinepubs/7908799/xbd/termios.html
//
// On Windows it uses the Console API:
//
//   https://msdn.microsoft.com/en-us/library/ms682087.aspx
//


#include "reb-config.h"

#if TO_WINDOWS

    #undef _WIN32_WINNT  // https://forum.rebol.info/t/326/4
    #define _WIN32_WINNT 0x0501  // Minimum API target: WinXP
    #define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
    #include <windows.h>

#else

    #include <signal.h>  // needed for SIGINT, SIGTERM, SIGHUP

#endif


#include "assert-fix.h"
#include "needful/needful.h"
#include "c-extras.h"  // for EXTERN_C, nullptr, etc.

#include "rebol.h"
#include "tmp-mod-console.h"

typedef RebolValue Value;


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

bool ctrl_c_enabled = true;

#if TO_EMSCRIPTEN || TO_WASI //=////////////////////////////////////////////=//

// !!! The WASI-SDK has something called WASI_EMULATED_SIGNAL, but if you try
// compile the POSIX branch of this #if it will say that sigaction is an
// incomplete type.

void Disable_Ctrl_C(void) { ctrl_c_enabled = false; }
void Enable_Ctrl_C(void) { ctrl_c_enabled = true; }


#elif TO_WINDOWS  //=//// WINDOWS //////////////////////////////////////////=//

// Windows handling is fairly simplistic--this is the callback passed to
// `SetConsoleCtrlHandler()`.  The most annoying thing about cancellation in
// windows is the limited signaling possible in the terminal's readline.
//
BOOL WINAPI Halt_On_Ctrl_C_Or_Break(DWORD dwCtrlType)
{
    switch (dwCtrlType) {
      case CTRL_C_EVENT:
      case CTRL_BREAK_EVENT:
        rebRequestHalt();
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

BOOL WINAPI Suppress_Ctrl_C(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_C_EVENT)  // should it suppress BREAK?
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

#else  //=//// POSIX, LINUX, MAC, etc. ////////////////////////////////////=//

// SIGINT is the interrupt usually tied to "Ctrl-C".  Note that if you use
// just `signal(SIGINT, Handle_Signal);` as R3-Alpha did, this means that
// blocking read() calls will not be interrupted with EINTR.  One needs to
// use sigaction() if available...it's a slightly newer API.
//
//   http://250bpm.com/blog:12
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

static void Handle_SIGINT(int sig)
{
    UNUSED(sig);
    rebRequestHalt();
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
        new_action.sa_handler = &Handle_SIGINT;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction(SIGINT, &new_action, nullptr);
    }

    ctrl_c_enabled = true;
}

#endif  //=///////////////////////////////////////////////////////////////=//


//
//  export console: native [
//
//  "Runs customizable Read-Eval-Print Loop, may 'provoke' code before input"
//
//      return: [
//          integer! "Exit code: 0 for clean exit, non-zero for errors"
//      ]
//      :provoke "Block must return a console state, group is cancellable"
//          [block! group!]
//      :resumable "Allow RESUME instruction (will return a ^GROUP!)"
//      :skin "File containing console skin, or MAKE CONSOLE! derived object"
//          [file! object!]
//      {
//          old-console
//          was-ctrl-c-enabled
//          can-recover
//          code
//          result'  ; intentionally lifted, to discern PANIC from ERROR! [1]
//          state
//      }
//  ]
//
DECLARE_NATIVE(CONSOLE)
//
// 1. In much of the system, you don't need to store variables in lifted form,
//    because (^var: whatever) can really store anything, and (^var) can then
//    read back anything.  But in the console, we want to store unstable
//    ERROR! antiforms -and- we want to know if a PANIC was intercepted.  It
//    would be possible to do this with a separate flag, but storing normal
//    results (including "normal" ERROR! antiform results) in lifted form
//    means we can store the PANIC as a WARNING! in the unlifted form.
//
// !!! The idea behind the console is that it can be called with skinning;
// so that if BREAKPOINT wants to spin up a console, it can...but with a
// little bit of injected information like telling you the current stack
// level it's focused on.  How that's going to work is still pretty up
// in the air.
{
    INCLUDE_PARAMS_OF_CONSOLE;

    switch (rebUnboxInteger("case [",
        "not state [0]",  // initial entry
        "state = 'running-request [1]",
        "panic -[Invalid CONSOLE state]-",
    "]")){
      case 0:
        goto initial_entry;

      case 1: {
        Disable_Ctrl_C();  // remove hook calling rebRequestHalt() on Ctrl-C
        goto run_skin; }

      default:
        assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

  // 1. The initial usermode console implementation was geared toward a single
  //    `system.console` object.  But the debugger raised the issue of nested
  //    sessions which might have a different skin.  So save whatever the
  //    console object was if it is being overridden.
  //
  // 2. We only enable halting (e.g. Ctrl-C, or Escape, or whatever) when
  //    console requests or user requests are running...not when HOST-CONSOLE
  //    itself is running, or during startup.  (Enabling it during startup
  //    would require a special "kill" mode that didn't call rebRequestHalt(),
  //    as basic startup cannot meaningfully be halted.  The system would be
  //    in an incomplete state.)

    rebElide("^old-console: ^system.console");  // !!! for debug [1]

    rebElide(
        "if skin [system.console: null]",  // !!! needed for now
        "was-ctrl-c-enabled:", rebQ(rebL(ctrl_c_enabled))
    );
    if (ctrl_c_enabled)
        Disable_Ctrl_C();

    if (rebUnboxLogic(
        "result': null",  // invalid "meta" result, but first call expects
        "can-recover: 'yes",  // one chance at HOST-CONSOLE internal error
        "null <> code: provoke"
    )){
        goto provoked;
    }

} run_skin: {  ///////////////////////////////////////////////////////////////

  // 1. This runs CONSOLE*, which returns *requests* to execute arbitrary
  //    code by way of its return results.  rebRecover() is thus here to catch
  //    bugs in CONSOLE* itself.  Any evaluations for the user (or on behalf
  //    of the console) are in their own separate step with rebContinue()
  //
  // 2. If the CONSOLE* function has any of its own implementation that could
  //    panic (or act as an uncaught throw) then that code should be returned
  //    as a BLOCK!.  This way the "console skin" can be reset to the default.
  //    If CONSOLE* itself panics (e.g. a typo in the implementation) there's
  //    probably not much use in trying again...but give it a chance rather
  //    than just crash.  Pass it back a thing that looks like an instruction
  //    it might have generated (a BLOCK!) asking itself to report an error
  //    more gracefully.

    assert(not ctrl_c_enabled);  // not while CONSOLE* is on the stack

  recover: { /////////////////////////////////////////////////////////////////

    Value* code;
    Value* warning = rebRecover(  // Recover catches buggy CONSOLE* [1]
        &code,
        "console*",  // action that takes 4 args, run it
            "opt code",  // group! or block! executed prior (or null)
            "opt result'",  // prior result lifted, or error (or null)
            "to-yesno resumable",
            "opt skin"
    );

    if (warning) {  // panic happened in CONSOLE* code itself [2]
        if (rebUnboxLogic("no? can-recover")) {
            unnecessary(rebQ(warning));  // CRASH takes arg literally
            return rebDelegate("crash", rebR(warning));
        }

        rebElide(
            "code: [#host-console-error]",
            "result':", warning,
            "can-recover: 'no"  // unrecoverable until user can request eval
        );
        goto recover;
    }

    rebElide("code: @", code);  // lifts non-error
    rebRelease(code); // don't need the outer block any more

}} provoked: {  //////////////////////////////////////////////////////////////

  // 1. Both console-initiated and user-initiated code is cancellable with
  //    Ctrl-C (though it's up to HOST-CONSOLE on the next iteration to decide
  //    whether to accept the cancellation or consider it an error condition
  //    or a reason to fall back to the default skin).
  //
  // 2. If the user was able to get to the point of requesting evaluation,
  //    then the console skin must not be broken beyond all repair.  So
  //    re-enable recovery.
  //
  // 3. This once used a ^GROUP! to reduce the amount of code on the stack
  //    which the user might see in a backtrace.  So instead of:
  //
  //        ^result': eval [print "hi"]
  //
  //    It would just execute the code directly:
  //
  //        result': ^(print "hi")  ; BUT ^(...) no longer means LIFT
  //
  //    That might be a nice idea, but as it turns out there's no mechanism
  //    for rescuing abrupt panics in the API...and I'm not entirely sure
  //    what a good version of that would wind up looking like.  Internal
  //    natives use DISPATCHER_CATCHES but it is very easy to screw it up or
  //    overlook it, and we don't have a way to tunnel that value into a
  //    callback from a continuation.  For the moment, just to get things
  //    working, we give in and use SYS.UTIL/ENRESCUE, along with other
  //    functions that are necessary.
  //
  // 4. Under the new understanding of definitional quits, a QUIT is just a
  //    function that throws a value specifically to the "generator" of the
  //    QUIT.  In the case of the console, that means each time we run code,
  //    a new QUIT needs to be created.  It's poked into the same place every
  //    time--the user context--but it's a new function.
  //
  //    (This idea that quits expire actually makes a lot of sense--e.g. when
  //    you think about running a module, it should only be able to quit
  //    during its initialization.  After that moment the module system isn't
  //    on the stack and dealing with it, so really it can only call the
  //    SYS.UTIL/EXIT function and exit the interpreter completely.)

    if (rebUnboxLogic("integer? code"))
        goto finished;  // if HOST-CONSOLE returns INTEGER! it means exit code

    Enable_Ctrl_C();  // add hook that will call rebRequestHalt() on Ctrl-C

    return rebContinueInterruptible(  // allows abrupt fail from HALT [1]
        "assert [match [block! group!] code]",
        "if group? code [can-recover: 'yes]",  // user could make request [2]

        "state: 'running-request",

        "sys.util/recover [",  // pollutes stack trace [3]
            "catch* 'quit* [",  // definitional quit (customized THROW) [4]
                "sys.contexts.user.quit: sys.util/make-quit:console quit*/",
                "result': lift eval code",
            "] then caught -> [",  // QUIT wraps QUIT* to only throw integers
                "result': caught",  // INTEGER! due to :CONSOLE, out of band
            "]",
        "] then warning -> [",
            "result': warning",  // non-lifted WARNING! out of band
        "]"
    );

} finished: {  ///////////////////////////////////////////////////////////////

  // Exit code is now an INTEGER! or a resume instruction PATH!
  //
  // 1. Exit codes aren't particularly well formalized (and are particularly
  //    tricky when you ask a shell to execute a process, to know whether the
  //    code is coming from the shell or what you wanted to run)
  //
  //      http://stackoverflow.com/q/1101957/

    if (rebUnboxLogic(
        "^system.console: ^old-console",
        "was-ctrl-c-enabled"
    )){
        Enable_Ctrl_C();
    }

    return rebValue("code");  // INTEGER! means exit code [1]
}}
