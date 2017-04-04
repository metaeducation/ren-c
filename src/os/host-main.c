//
//  File: %host-main.c
//  Summary: "Host environment main entry point"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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
// %host-main.c is the original entry point for the open-sourced R3-Alpha.
// Depending on whether it was POSIX or Windows, it would define either a
// `main()` or `WinMain()`, and implemented a very rudimentary console.
//
// On POSIX systems it uses <termios.h> to implement line editing:
//
// http://pubs.opengroup.org/onlinepubs/7908799/xbd/termios.html
//
// On Windows it uses the Console API:
//
// https://msdn.microsoft.com/en-us/library/ms682087.aspx
//
// !!! Originally %host-main.c was a client of the %reb-host.h (RL_Api).  It
// did not have access to things like the definition of a REBVAL or a REBSER.
// The sparse and convoluted nature of the RL_Api presented an awkward
// barrier, and the "sample console" stagnated as a result.
//
// In lieu of a suitable "abstracted" variant of the core services--be that
// an evolution of RL_Api or otherwise--the console now links directly
// against the Ren-C core.  This provides full access to the routines and
// hooks necessary to evolve the console if one were interested.  (The GUI
// inteface Ren Garden is the flagship console for Ren-C, so that is where
// most investment will be made.)
//

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32
    //
    // On Windows it is required to include <windows.h>, and defining the
    // _WIN32_WINNT constant to 0x0501 specifies the minimum targeted version
    // is Windows XP.  This is the earliest platform API still supported by
    // Visual Studio 2015:
    //
    //     https://msdn.microsoft.com/en-us/library/6sehtctf.aspx
    //
    // R3-Alpha used 0x0500, indicating a minimum target of Windows 2000.  No
    // Windows-XP-specific dependencies were added in Ren-C, but the version
    // was bumped to avoid compilation errors in the common case.
    //
    // !!! Note that %sys-core.h includes <windows.h> as well if building
    // for windows.  The redundant inclusion should not create a problem.
    // (So better to do the inclusion just to test that it doesn't.)
    //
    #undef _WIN32_WINNT
    #define _WIN32_WINNT 0x0501
    #include <windows.h>

    // Put any dependencies that include <windows.h> here
    //
    /* #include "..." */
    /* #include "..." */

    // Undefine the Windows version of IS_ERROR to avoid compiler warning
    // when Rebol redefines it.  (Rebol defines IS_XXX for all datatypes.)
    //
    #undef IS_ERROR
    #undef max
    #undef min
#else
    #include <signal.h> // needed for SIGINT, SIGTERM, SIGHUP
#endif


#include "sys-core.h"
#include "sys-ext.h"
#include "tmp-boot-extensions.h"

EXTERN_C void RL_Version(REBYTE vers[]);
EXTERN_C void RL_Shutdown(REBOOL clean);
EXTERN_C void RL_Escape();

EXTERN_C REBOL_HOST_LIB Host_Lib_Init;


// The initialization done by RL_Init() is intended to be as basic as possible
// in order to get the Rebol series/values/array functions ready to be run.
// Once that's ready, the rest of the initialization can take advantage of
// a working evaluator.  This includes PARSE to process the command line
// parameters, or PRINT to output boot banners.
//
// The %make-host-init.r file takes the %host-start.r script and turns it
// into a compressed binary C literal.  That literal can be LOADed and
// executed to return the HOST-START function, which takes the command line
// arguments as an array of STRING! and handles it from there.
//
#include "tmp-host-start.inc"


const REBYTE halt_str[] = "[escape]";
const REBYTE breakpoint_str[] =
    "** Breakpoint Hit (see BACKTRACE, DEBUG, and RESUME)\n";
const REBYTE interrupted_str[] =
    "** Execution Interrupted (see BACKTRACE, DEBUG, and RESUME)\n";

#ifndef REB_CORE
EXTERN_C void Init_Windows(void);
EXTERN_C void OS_Init_Graphics(void);
EXTERN_C void OS_Destroy_Graphics(void);
#endif


#ifdef TO_WINDOWS
    EXTERN_C HINSTANCE App_Instance;
    HINSTANCE App_Instance = 0;
#endif


// Host bare-bones stdio functs:
extern void Open_StdIO(void);
extern void Close_StdIO(void);
extern void Put_Str(const REBYTE *buf);


/* coverity[+kill] */
void Host_Crash(const char *reason) {
    OS_CRASH(cb_cast("REBOL Host Failure"), cb_cast(reason));
}


// Current stack level displayed in the REPL, where bindings are assumed to
// be made for evaluations.  So if the prompt reads `[3]>>`, and a string
// of text is typed in to be loaded as code, that code will be bound to
// the user context, then the lib context, then to the variables of whatever
// function is located at stack level 3.
//
extern REBCNT HG_Stack_Level;
REBCNT HG_Stack_Level = 1;

REBVAL HG_Host_Repl;


// The DEBUG command is a host-specific "native", which modifies state that
// is specific to controlling variables and behaviors in the REPL.  Since
// the core itself seeks to avoid having any UI and only provide evaluation
// services, C code for DEBUG must either be within the host, or the DEBUG
// native would need to implement an abstract protocol that could make
// callbacks into the host.
//
// A standard or library might evolve so that every host does not reimplement
// the debug logic.  However, much of the debugging behavior depends on the
// nature of the host (textual vs. GUI), as well as being able to modify
// state known to the host and not the core.  So for the moment, DEBUG is
// implemented entirely in the host...while commands like BREAKPOINT have
// their implementation in the core with a callback to the host to implement
// the host-specific portion.
//
// !!! Can the REBNATIVE with source-in-comment declaration style be
// something that non-core code can use, vs. this handmade variant?
//
const REBYTE N_debug_spec[] =
    " {Dialect for interactive debugging, see documentation for details}"
    " 'value [_ integer! frame! function! block!]"
        " {Stack level to inspect or dialect block, or enter debug mode}"
    "";
REB_R N_debug(REBFRM *frame_) {
    PARAM(1, value); // no automatic INCLUDE_PARAMS_OF_XXX for manual native

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

    if (IS_INTEGER(value) || IS_FRAME(value) || IS_FUNCTION(value)) {
        REBFRM *frame;

        // We pass TRUE here to account for an extra stack level... the one
        // added by DEBUG itself, which presumably should not count.
        //
        if (!(frame = Frame_For_Stack_Level(&HG_Stack_Level, value, TRUE)))
            fail (value);

        Init_Block(D_OUT, Make_Where_For_Frame(frame));
        return R_OUT;
    }

    assert(IS_BLOCK(value));

    Debug_Fmt(
        "Sorry, but the `debug [...]` dialect is not defined yet.\n"
        "Change the stack level (integer!, frame!, function!)\n"
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


//
//  Do_Code()
//
// This is a version of a routine that was offered by the RL_Api, which has
// been expanded here in order to permit the necessary customizations for
// interesting REPL behavior w.r.t. binding, error handling, and response
// to throws.
//
// !!! Now that this code has been moved into the host, the convoluted
// integer-return-scheme can be eliminated and the code integrated more
// clearly into the surrounding calls.
//
int Do_Code(
    int *exit_status,
    REBVAL *out,
    const REBVAL *code,
    REBOOL at_breakpoint
) {
    assert(IS_BLOCK(code));

    struct Reb_State state;
    REBCTX *error;

    // Breakpoint REPLs are nested, and we may wish to jump out of them to
    // the topmost level via a HALT.  However, all other errors need to be
    // confined, so that if one is doing evaluations during the pause of
    // a breakpoint an error doesn't "accidentally resume" by virtue of
    // jumping the stack out of the REPL.
    //
    // The topmost layer REPL, however, needs to catch halts in order to
    // keep control and not crash out.
    //
    if (at_breakpoint)
        PUSH_TRAP(&error, &state);
    else
        PUSH_UNHALTABLE_TRAP(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

    if (error) {
        if (ERR_NUM(error) == RE_HALT) {
            assert(!at_breakpoint);
            return -1; // !!! Revisit hardcoded #
        }

        Init_Error(out, error);
        return -cast(REBINT, ERR_NUM(error));
    }

    if (Do_At_Throws(out, VAL_ARRAY(code), VAL_INDEX(code), SPECIFIED)) {
        if (at_breakpoint) {
            if (
                IS_FUNCTION(out)
                && VAL_FUNC_DISPATCHER(out) == &N_resume
            ) {
                //
                // This means we're done with the embedded REPL.  We want to
                // resume and may be returning a piece of code that will be
                // run by the finishing BREAKPOINT command in the target
                // environment.
                //
                // We'll never return a halt, so we reuse -1 (in this very
                // temporary scheme built on the very clunky historical REPL,
                // which will not last much longer...fingers crossed.)
                //
                DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);
                CATCH_THROWN(out, out);
                *exit_status = -1;
                return -1;
            }

            if (
                IS_FUNCTION(out)
                && VAL_FUNC_DISPATCHER(out) == &N_quit
            ) {
                //
                // It would be frustrating if the system did not respond to
                // a QUIT and forced you to do `resume/with [quit]`.  So
                // this is *not* caught, rather passed back up with the
                // special -2 status code.
                //
                DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);
                CATCH_THROWN(out, out);
                *exit_status = -2;
                return -2;
            }
        }
        else {
            // We are at the top level REPL, where we catch QUIT
            //
            if (
                IS_FUNCTION(out)
                && VAL_FUNC_DISPATCHER(out) == &N_quit
            ) {
                DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);
                CATCH_THROWN(out, out);
                *exit_status = Exit_Status_From_Value(out);
                return -2; // Revisit hardcoded #
            }
        }

        fail (Error_No_Catch_For_Throw(out));
    }

    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

    return 0;
}


void Host_Repl(
    int *exit_status,
    REBVAL *out,
    REBOOL at_breakpoint
) {
    REBOOL last_failed = FALSE;
    Init_Void(out);

    DECLARE_LOCAL (level);
    DECLARE_LOCAL (frame);
    Init_Blank(level);
    Init_Blank(frame);

    PUSH_GUARD_VALUE(frame);

    while (TRUE) {
        int do_result;

        if (at_breakpoint) {
            //
            // If we're stopped at a breakpoint, then the REPL has a
            // modality to it of "which stack level you are examining".
            // The DEBUG command can change this, so at the moment it
            // has to be refreshed each time an evaluation is performed.

            Init_Integer(level, HG_Stack_Level);

            REBFRM *f = Frame_For_Stack_Level(NULL, level, FALSE);
            assert(f);

            Init_Any_Context(
                frame,
                REB_FRAME,
                Context_For_Frame_May_Reify_Managed(f)
            );
        }
        const REBOOL fully = TRUE; // error if not all arguments consumed

        // Generally speaking, we do not want the trace level to apply to the
        // REPL execution itself.
        //
        REBINT Save_Trace_Level = Trace_Level;
        REBINT Save_Trace_Depth = Trace_Depth;
        Trace_Level = 0;
        Trace_Depth = 0;

        DECLARE_LOCAL (code_or_error);
        if (Apply_Only_Throws(
            code_or_error, // where return value of HOST-REPL is saved
            fully,
            &HG_Host_Repl, // HOST-REPL function to run
            out, // last-result (always void first run through loop)
            last_failed ? TRUE_VALUE : FALSE_VALUE, // last-failed
            level, // focus-level
            frame, // focus-frame
            END
        )) {
            // The REPL should not execute anything that should throw.
            // Determine graceful way of handling if it does.
            //
            panic (code_or_error);
        }

        Trace_Level = Save_Trace_Level;
        Trace_Depth = Save_Trace_Depth;

        if (IS_ERROR(code_or_error)) {
            do_result = -cast(int, ERR_NUM(VAL_CONTEXT(code_or_error)));
            Move_Value(out, code_or_error);
        }
        else if (IS_BLOCK(code_or_error))
            do_result = Do_Code(
                exit_status, out, code_or_error, at_breakpoint
            );
        else
            panic (code_or_error);

        // NOTE: Although the operation has finished at this point, it may
        // be that a Ctrl-C set up a pending FAIL, which will be triggered
        // during output below.  See the PUSH_UNHALTABLE_TRAP in the caller.

        if (do_result == -1) {
            //
            // If we're inside a breakpoint, this actually means "resume",
            // because Do_Code doesn't do any error trapping if we pass
            // in `at_breakpoint = TRUE`.  Hence any HALT longjmp would
            // have bypassed this, so the -1 signal is reused (for now).
            //
            if (at_breakpoint)
                goto cleanup_and_return;

            // !!! The "Halt" status is communicated via -1, but
            // is not an actual valid "error value".  It cannot be
            // created by user code, and the fact that it is done
            // via the error mechanism is an "implementation detail".
            //
            Put_Str(halt_str);
            last_failed = FALSE;

            // The output value will be an END marker on halt, to signal the
            // unusability of the interrupted result.
            //
            Init_Void(out);
        }
        else if (do_result == -2) {
            //
            // Command issued a purposeful QUIT or EXIT, exit_status
            // contains status.  Assume nothing was pushed on stack
            //
            goto cleanup_and_return;
        }
        else if (do_result < -2) {
            last_failed = TRUE;
            assert(IS_ERROR(out));
        }
        else {
            // Result will be printed by next loop
            //
            assert(do_result == 0);
            last_failed = FALSE;
        }
    }

cleanup_and_return:
    DROP_GUARD_VALUE(frame);
    return;
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
) {
    // Notify the user that the breakpoint or interruption was hit.
    //
    if (interrupted)
        Put_Str(interrupted_str);
    else
        Put_Str(breakpoint_str);

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

    // Spawn nested REPL.
    //
    int exit_status;
    Host_Repl(&exit_status, instruction_out, TRUE);

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

    // We get -1 for RESUME and -2 for QUIT, under the current convoluted
    // scheme of return codes.
    //
    // !!! Eliminate return codes now that RL_Api dependence is gone and
    // speak in terms of the REBVALs themselves.
    //
    assert(exit_status == -1 || exit_status == -2);
    return LOGICAL(exit_status == -2);
}


// Register host-specific DEBUG native in user and lib contexts.  (See
// notes on N_debug regarding why the C code implementing DEBUG is in
// the host and not part of Rebol Core.)
//
void Init_Debug_Extension(void) {
    const REBYTE debug_utf8[] = "debug";
    REBSTR *debug_name = Intern_UTF8_Managed(debug_utf8, LEN_BYTES(debug_utf8));

    REBCTX *user_context = VAL_CONTEXT(Get_System(SYS_CONTEXTS, CTX_USER));
    if (
        0 == Find_Canon_In_Context(Lib_Context, STR_CANON(debug_name), TRUE) &&
        0 == Find_Canon_In_Context(user_context, STR_CANON(debug_name), TRUE)
    ) {
        REBSTR *filename = Canon(SYM___ANONYMOUS__);
        REBARR *spec_array = Scan_UTF8_Managed(
            N_debug_spec, LEN_BYTES(N_debug_spec), filename
        );
        DECLARE_LOCAL (spec);
        Init_Block(spec, spec_array);
        Bind_Values_Deep(ARR_HEAD(spec_array), Lib_Context);

        REBFUN *debug_native = Make_Function(
            Make_Paramlist_Managed_May_Fail(spec, MKF_KEYWORDS),
            &N_debug,
            NULL, // no underlying function, this is fundamental
            NULL // not providing a specialization
        );

        Move_Value(
            Append_Context(Lib_Context, 0, debug_name),
            FUNC_VALUE(debug_native)
        );
        Move_Value(
            Append_Context(user_context, 0, debug_name),
            FUNC_VALUE(debug_native)
        );
    }
    else {
        // It's already there--e.g. someone added REBNATIVE(debug).  Assert
        // about it in the debug build, otherwise don't add the host version.
        //
        assert(FALSE);
    }
}


#ifdef TO_WINDOWS

//
// This is the callback passed to `SetConsoleCtrlHandler()`.
//
BOOL WINAPI Handle_Break(DWORD dwCtrlType)
{
    switch(dwCtrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
        RL_Escape();
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
        OS_EXIT(100);
        return TRUE; // TRUE = "we handled it"

    default:
        return FALSE; // FALSE = "we didn't handle it"
    }
}

#else

//
// Hook registered via `signal()`.
//
static void Handle_Signal(int sig)
{
    UNUSED(sig);
    RL_Escape();
}

#endif



/***********************************************************************
**
**  MAIN ENTRY POINT
**
**  Win32 args:
**      inst:  current instance of the application (app handle)
**      prior: always NULL (use a mutex for single inst of app)
**      cmd:   command line string (or use GetCommandLine)
**      show:  how app window is to be shown (e.g. maximize, minimize, etc.)
**
**  Win32 return:
**      If the function succeeds, terminating when it receives a WM_QUIT
**      message, it should return the exit value contained in that
**      message's wParam parameter. If the function terminates before
**      entering the message loop, it should return zero.
**
**  Posix args: as you would expect in C.
**  Posix return: ditto.
**
*/
/***********************************************************************/

// Using a main entry point for a console program (as opposed to WinMain)
// so that we can connect to the console.  See the StackOverflow question
// "Can one executable be both a console and a GUI application":
//
//     http://stackoverflow.com/questions/493536/
//
// int WINAPI WinMain(HINSTANCE inst, HINSTANCE prior, LPSTR cmd, int show)

int main(int argc, char **argv_ansi)
{
    // Must be done before an console I/O can occur. Does not use reb-lib,
    // so this device should open even if there are other problems.
    //
    Open_StdIO();

    Host_Lib = &Host_Lib_Init;
    RL_Init(Host_Lib);

    // While running the Rebol initialization code, we don't want any special
    // Ctrl-C handling... leave it to the OS (which would likely terminate
    // the process).  But once it's done, set up the interrupt handler.
    //
    // Note: Once this was done in Open_StdIO, but it's less opaque to do it
    // here (since there are already platform-dependent #ifdefs to handle the
    // command line arguments)
    //
#ifdef TO_WINDOWS
    SetConsoleCtrlHandler(Handle_Break, TRUE);
#else
    // SIGINT is the interrupt, usually tied to "Ctrl-C"
    //
    signal(SIGINT, Handle_Signal);

    // SIGTERM is sent on "polite request to end", e.g. default unix `kill`
    //
    signal(SIGTERM, Handle_Signal);

    // SIGHUP is sent on a hangup, e.g. user's terminal disconnected
    //
    signal(SIGHUP, Handle_Signal);

    // SIGQUIT is used to terminate a program in a way that is designed to
    // debug it, e.g. a core dump.  Receiving SIGQUIT is a case where
    // program exit functions like deletion of temporary files may be
    // skipped to provide more state to analyze in a debugging scenario.
    //
    // -- no handler

    // SIGKILL is the impolite signal for shutdown; cannot be hooked/blocked
#endif

    // With basic initialization done, we want to turn the platform-dependent
    // argument strings into a block of Rebol strings as soon as possible.
    // That way the command line argument processing can be taken care of by
    // PARSE instead of C code!
    //
    REBARR *argv = Make_Array(argc);

#ifdef TO_WINDOWS
    UNUSED(argv_ansi);

    //
    // Were we using WinMain we'd be getting our arguments in Unicode, but
    // since we're using an ordinary main() we do not.  However, this call
    // lets us slip out and pick up the arguments in Unicode form.
    //
    wchar_t **argv_utf16 = cast(
        wchar_t**, CommandLineToArgvW(GetCommandLineW(), &argc)
    );
    int i = 0;
    for (; i < argc; ++i) {
        if (argv_utf16[i] == NULL)
            continue; // shell bug

        static_assert_c(sizeof(REBUNI) == sizeof(wchar_t));

        Init_String(
            Alloc_Tail_Array(argv),
            Make_UTF16_May_Fail(cast(REBUNI*, argv_utf16[i]))
        );
    }
#else
    // Assume no wide character support, and just take the ANSI C args, which
    // should ideally be in UTF8
    //
    int i = 0;
    for (; i < argc; ++i) {
        if (argv_ansi[i] == NULL)
            continue; // shell bug

        Init_String(
            Alloc_Tail_Array(argv), Make_UTF8_May_Fail(argv_ansi[i])
        );
    }
#endif

    // !!! Register EXPERIMENTAL breakpoint hook.  Note that %host-main.c is
    // not really expected to stick around as the main REPL...
    //
    PG_Breakpoint_Quitting_Hook = &Host_Breakpoint_Quitting_Hook;

    DECLARE_LOCAL (argv_value);
    Init_Block(argv_value, argv);
    PUSH_GUARD_VALUE(argv_value);

#ifdef TEST_EXTENSIONS
    Init_Ext_Test();
#endif

#ifdef TO_WINDOWS
    // no console, we must be the child process
    if (GetStdHandle(STD_OUTPUT_HANDLE) == 0)
    {
        App_Instance = GetModuleHandle(NULL);
    }
#ifdef REB_CORE
    else //use always the console for R3/core
    {
        // GetWindowsLongPtr support 32 & 64 bit windows
        App_Instance = (HINSTANCE)GetWindowLongPtr(GetConsoleWindow(), GWLP_HINSTANCE);
    }
#else
    //followinng R3/view code behaviors when compiled as:
    //-"console app" mode: stdio redirection works but blinking console window during start
    //-"GUI app" mode stdio redirection doesn't work properly, no blinking console window during start
    else if (argc > 1) // we have command line args
    {
        // GetWindowsLongPtr support 32 & 64 bit windows
        App_Instance = (HINSTANCE)GetWindowLongPtr(GetConsoleWindow(), GWLP_HINSTANCE);
    }
    else // no command line args but a console - launch child process so GUI is initialized and exit
    {
        DWORD dwCreationFlags = CREATE_DEFAULT_ERROR_MODE | DETACHED_PROCESS;
        STARTUPINFO startinfo;
        PROCESS_INFORMATION procinfo;
        ZeroMemory(&startinfo, sizeof(startinfo));
        startinfo.cb = sizeof(startinfo);
        if (!CreateProcess(NULL, argv[0], NULL, NULL, FALSE, dwCreationFlags, NULL, NULL, &startinfo, &procinfo))
            MessageBox(0, L"CreateProcess() failed :(", L"", 0);
        exit(0);
    }
#endif //REB_CORE
#endif //TO_WINDOWS

    // Common code for console & GUI version
#ifndef REB_CORE
    Init_Windows();
    OS_Init_Graphics();
#endif // REB_CORE

    Init_Debug_Extension();

    struct Reb_State state;
    REBCTX *error;

    PUSH_UNHALTABLE_TRAP(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

    int exit_status;

    volatile REBOOL finished; // without volatile, gets "clobbered" warning

    Prep_Global_Cell(&HG_Host_Repl);
    Init_Blank(&HG_Host_Repl);

    if (error != NULL) {
        //
        // We want to avoid doing I/O directly from the C code of the host,
        // and let that go through WRITE-STDOUT.  Hence any part of the
        // startup that can error should be TRAP'd by the startup code itself
        // and handled or PRINT'd in some way.
        // 
        // The exception is a halt with Ctrl-C, which can currently only be
        // handled by C code that ran PUSH_UNHALTABLE_TRAP().
        //
        if (ERR_NUM(error) != RE_HALT)
            panic (error);

        exit_status = 128; // http://stackoverflow.com/questions/1101957/
        finished = TRUE;
    }
    else {
        REBSER *startup = Decompress(
            &Reb_Init_Code[0],
            REB_INIT_SIZE,
            -1,
            FALSE,
            FALSE
        );
        if (startup == NULL)
            panic ("Can't decompress %host-start.r linked into executable");

        const char *host_start_utf8 = "host-start.r";
        REBSTR *host_start_filename = Intern_UTF8_Managed(
            cb_cast(host_start_utf8), strlen(host_start_utf8)
        );
        REBARR *array = Scan_UTF8_Managed(
            BIN_HEAD(startup), BIN_LEN(startup), host_start_filename
        );

        // Bind the REPL and startup code into the lib context.
        //
        // !!! It's important not to load the REPL into user, because since it
        // uses routines like PRINT to do it's I/O you (probably) don't want
        // the REPL to get messed up if PRINT is redefined--for instance.  It
        // should probably have its own context, which would entail a copy of
        // every word in lib that it uses, but that mechanic hasn't been
        // fully generalized--and might not be the right answer anyway.
        //
        // Only add top-level words to the `lib' context
        Bind_Values_Set_Midstream_Shallow(ARR_HEAD(array), Lib_Context);

        // Bind all words to the `lib' context, but not adding any new words
        Bind_Values_Deep(ARR_HEAD(array), Lib_Context);

        // The new policy for source code in Ren-C is that it loads read only.
        // This didn't go through the LOAD Rebol function (should it?  it
        // never did before.)  For now, use simple binding but lock it.
        //
        Deep_Freeze_Array(array);

        DECLARE_LOCAL (code);
        Init_Block(code, array);

        DECLARE_LOCAL (host_start);
        if (
            Do_Code(&exit_status, host_start, code, FALSE)
            != 0
        ){
            panic (startup); // just loads functions, shouldn't QUIT or error
        }

        Free_Series(startup);

        REBSER *embedded = NULL;
        REBI64 embedded_size = 0;
        REBYTE *embedded_utf8 = OS_READ_EMBEDDED(&embedded_size);
        if (embedded_utf8 != NULL) {
            if (embedded_size <= 4)
                panic ("No 4-byte long payload at start of embedded script");

            i32 ptype = 0;
            REBYTE *data = embedded_utf8 + sizeof(ptype);
            embedded_size -= sizeof(ptype);

            memcpy(&ptype, embedded_utf8, sizeof(ptype));

            if (ptype == 1) { // COMPRESSed data
                embedded = Decompress(data, embedded_size, -1, FALSE, FALSE);
            }
            else {
                embedded = Make_Binary(embedded_size);
                memcpy(BIN_HEAD(embedded), data, embedded_size);
            }

            OS_FREE(embedded_utf8);
        }

        DECLARE_LOCAL (embedded_value);
        if (embedded == NULL)
            Init_Blank(embedded_value);
        else
            Init_Block(embedded_value, embedded);

        DECLARE_LOCAL (ext_value);
        Init_Blank(ext_value);
        LOAD_BOOT_EXTENSIONS(ext_value);

        if (!IS_FUNCTION(host_start))
            panic (host_start); // should not be able to error

        const REBOOL fully = TRUE; // error if not all arguments are consumed

        DECLARE_LOCAL (result);
        if (Apply_Only_Throws(
            result,
            fully,
            host_start, // startup function, implicit GC guard
            argv_value, // argv parameter, implicit GC guard
            embedded_value, // embedded-script parameter, implicit GC guard
            ext_value,
            END
        )) {
            if (
                IS_FUNCTION(result)
                && VAL_FUNC_DISPATCHER(result) == &N_quit
            ) {
                CATCH_THROWN(result, result);
                exit_status = Exit_Status_From_Value(result);

                DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

                SHUTDOWN_BOOT_EXTENSIONS();
                Shutdown_Core();
                OS_EXIT(exit_status);
                DEAD_END;
            }

            fail (Error_No_Catch_For_Throw(result));
        }

        DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

        // HOST-START returns either an integer exit code or a blank if the
        // behavior should be to fall back to the REPL.
        //
        if (IS_FUNCTION(result)) {
            finished = FALSE;
            Move_Value(&HG_Host_Repl, result);
        }
        else if (IS_INTEGER(result)) {
            finished = TRUE;
            exit_status = VAL_INT32(result);
        }
        else
            panic (result); // no other legal return values for now
    }

    DROP_GUARD_VALUE(argv_value);

    PUSH_GUARD_VALUE(&HG_Host_Repl); // might be blank

    // Although the REPL routine does a PUSH_UNHALTABLE_TRAP in order to
    // catch any errors or halts, it then has to report those errors when
    // that trap is engaged.  So imagine it's in the process of trapping an
    // error and prints out a very long one, and the user wants to interrupt
    // the error report with a Ctrl-C...but there's not one in effect.
    //
    // This loop institutes a top-level trap whose only job is to catch the
    // interrupts that occur during overlong error reports inside the REPL.
    //
    DECLARE_LOCAL (value);

    while (NOT(finished)) {
        Init_End(value);
        PUSH_GUARD_VALUE(value); // !!! Out_Value expects value to be GC safe

        struct Reb_State state;
        REBCTX *error;

        PUSH_UNHALTABLE_TRAP(&error, &state);

    // The first time through the following code 'error' will be NULL, but...
    // `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

        if (error) {
            //
            // If a HALT happens and manages to get here, just go set up the
            // trap again and call into the REPL again.  (It wasn't an
            // evaluation error because those have their own traps, it was a
            // halt that happened during output.)
            //
            if (ERR_NUM(error) != RE_HALT) {
            #ifdef NDEBUG
                // do something sensible in release builds here that does not
                // crash.
            #else
                panic(error);
            #endif
            }
        }
        else {
            Host_Repl(&exit_status, value, FALSE);

            finished = TRUE;

            DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);
        }

        DROP_GUARD_VALUE(value);
    }

    DROP_GUARD_VALUE(&HG_Host_Repl);

    SHUTDOWN_BOOT_EXTENSIONS();

    OS_QUIT_DEVICES(0);

#ifndef REB_CORE
    OS_Destroy_Graphics();
#endif

    Close_StdIO();

    // No need to do a "clean" shutdown, as we are about to exit the process
    // (Note: The debug build runs through the clean shutdown anyway!)
    //
    RL_Shutdown(FALSE);

    return exit_status;
}
