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


// Assume that Ctrl-C is enabled in a console application by default.
// (Technically it may be set to be ignored by a parent process or context,
// in which case conventional wisdom is that we should not be enabling it
// ourselves.)
//
REBOOL ctrl_c_enabled = TRUE;


#ifdef TO_WINDOWS

//
// This is the callback passed to `SetConsoleCtrlHandler()`.
//
BOOL WINAPI Handle_Break(DWORD dwCtrlType)
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
        return TRUE; // TRUE = "we handled it"

    default:
        return FALSE; // FALSE = "we didn't handle it"
    }
}

BOOL WINAPI Handle_Nothing(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_C_EVENT)
        return TRUE;

    return FALSE;
}

void Disable_Ctrl_C(void)
{
    assert(ctrl_c_enabled);

    SetConsoleCtrlHandler(Handle_Break, FALSE);
    SetConsoleCtrlHandler(Handle_Nothing, TRUE);

    ctrl_c_enabled = FALSE;
}

void Enable_Ctrl_C(void)
{
    assert(NOT(ctrl_c_enabled));

    SetConsoleCtrlHandler(Handle_Break, TRUE);
    SetConsoleCtrlHandler(Handle_Nothing, FALSE);

    ctrl_c_enabled = TRUE;
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

    sigaction(SIGINT, NULL, &old_action); // fetch current handler
    if (old_action.sa_handler != SIG_IGN) {
        struct sigaction new_action;
        new_action.sa_handler = SIG_IGN;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction(SIGINT, &new_action, NULL);
    }

    ctrl_c_enabled = FALSE;
}

void Enable_Ctrl_C(void)
{
    assert(NOT(ctrl_c_enabled));

    if (old_action.sa_handler != SIG_IGN) {
        struct sigaction new_action;
        new_action.sa_handler = &Handle_Signal;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction(SIGINT, &new_action, NULL);
    }

    ctrl_c_enabled = TRUE;
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
    rebStartup(Host_Lib);

    // We only enable Ctrl-C when user code is running...not when the
    // HOST-CONSOLE function itself is.
    //
    Disable_Ctrl_C();

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

        REBVAL *str = rebStringW(argv_utf16[i]);
        Append_Value(argv, str);
        rebFree(str);
    }
#else
    // Assume no wide character support, and just take the ANSI C args, which
    // should ideally be in UTF8
    //
    int i = 0;
    for (; i < argc; ++i) {
        if (argv_ansi[i] == NULL)
            continue; // shell bug

        REBVAL *str = rebString(argv_ansi[i]);
        Append_Value(argv, str);
        rebFree(str);
    }
#endif

    DECLARE_LOCAL (argv_value);
    Init_Block(argv_value, argv);
    PUSH_GUARD_VALUE(argv_value);

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
        if (!CreateProcess(NULL, argv_utf16[0], NULL, NULL, FALSE, dwCreationFlags, NULL, NULL, &startinfo, &procinfo))
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

    const REBOOL gzip = FALSE;
    const REBOOL raw = FALSE;
    const REBOOL only = FALSE;
    REBSER *startup = Inflate_To_Series(
        &Reb_Init_Code[0],
        REB_INIT_SIZE,
        -1,
        gzip,
        raw,
        only
    );
    if (startup == NULL)
        panic ("Can't decompress %host-start.r linked into executable");

    REBARR *array = Scan_UTF8_Managed(
        STR("host-start.r"), BIN_HEAD(startup), BIN_LEN(startup)
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

    DECLARE_LOCAL (host_console);
    if (Do_At_Throws(
        host_console, // returned value must be a FUNCTION!
        array,
        0,
        SPECIFIED
    )){
        panic (startup); // just loads functions, shouldn't QUIT or error
    }

    if (!IS_FUNCTION(host_console))
        rebPanic (host_console);

    Free_Series(startup);

    DECLARE_LOCAL (ext_value);
    Init_Blank(ext_value);
    LOAD_BOOT_EXTENSIONS(ext_value);

    // While some people may think that argv[0] in C contains the path to
    // the running executable, this is not necessarily the case.  The actual
    // method for getting the current executable path is OS-specific:
    //
    // https://stackoverflow.com/q/1023306/
    // http://stackoverflow.com/a/933996/211160
    //
    // It's not foolproof, so BLANK! is passed in if nothing could be found.
    // The console code can then decide if it wants to fall back on argv[0].
    //
    REBVAL *exec_path = OS_GET_CURRENT_EXEC();
    assert(IS_FILE(exec_path) || IS_BLANK(exec_path));

    // !!! Previously the C code would call a separate startup function
    // explicitly.  This created another difficult case to bulletproof
    // various forms of failures during service routines that were already
    // being handled by the framework surrounding HOST-CONSOLE.  The new
    // approach is to let HOST-CONSOLE be the sole entry point, and that
    // LAST-STATUS being void is an indication that it is running for the
    // first time.  Thus it can use that opportunity to run any startup
    // code or print any banners it wishes.
    //
    // However, the previous call to the startup function gave it three
    // explicit parameters.  The parameters might best be passed by
    // sticking them in the environment somewhere and letting HOST-CONSOLE
    // find them...but for the moment we pass them as a BLOCK! in the
    // LAST-RESULT argument when the LAST-STATUS is void, and let it
    // unpack them.
    //
    // Note that `result`, `code`, and status have to be freed each loop ATM.
    //
    REBVAL *code = rebVoid();
    REBVAL *status = rebVoid();
    REBVAL *result = rebBlock(exec_path, argv_value, ext_value, END);
    rebFree(exec_path); // ...value in BLOCK! keeps series alive now...

    // The DO and APPLY hooks are used to implement things like tracing
    // or debugging.  If they were allowed to run during the host
    // console, they would create a fair amount of havoc (the console
    // is supposed to be "invisible" and not show up on the stack...as if
    // it were part of the C codebase, even though it isn't written in C)
    //
    REBDOF saved_do_hook = PG_Do;
    REBAPF saved_apply_hook = PG_Apply;

    // !!! While the new mode of TRACE (and other code hooking function
    // execution) is covered by `saved_do_hook` and `saved_apply_hook`, there
    // is independent tracing code in PARSE which is also enabled by TRACE ON
    // and has to be silenced during console-related code.  Review how hooks
    // into PARSE and other services can be avoided by the console itself
    //
    REBINT Save_Trace_Level = Trace_Level;
    REBINT Save_Trace_Depth = Trace_Depth;

    do {
        assert(NOT(ctrl_c_enabled)); // can't cancel during HOST-CONSOLE

        // !!! In this early phase of trying to establish the API, we assume
        // this code is responsible for freeing the result `code` (if it
        // does not come back NULL indicating a failure).
        //
        REBVAL *new_code = rebDo(
            BLANK_VALUE, // hack around rebEval() not allowed yet in first slot
            rebEval(host_console), // HOST-CONSOLE function (run it)
            code, // GROUP! or BLOCK! that was executed prior below (or void)
            result, // result of evaluating previous code (void if error)
            status, // BLANK! if no error, BAR! if halt, or the ERROR!
            END
        );
        rebFree(code);
        rebFree(result);
        rebFree(status);

        if ((code = new_code) == NULL) {
            //
            // We don't allow cancellation while the HOST-CONSOLE function is
            // running, and it should not FAIL or otherwise raise an error.
            // This is why it needs to be written in such a way that any
            // arbitrary user code--or operations that might just legitimately
            // take a long time--are returned in `code` to be sandboxed.
            //
            REBVAL *e = rebLastError();
            assert(NOT(IS_BAR(e))); // at moment, the signal for HALT/Ctrl-C
            assert(NOT(IS_INTEGER(e))); // at moment, signals an exit code
            rebPanic (e); // should dump some info about the `e` ERROR!
        }

        if (NOT(IS_BLOCK(code)) && NOT(IS_GROUP(code))) {
            status = rebError("HOST-CONSOLE must return GROUP! or BLOCK!");
            result = rebVoid();
            continue;
        }

        // Restore custom DO and APPLY hooks, but only if running a GROUP!.
        // (We do not want to trace/debug/instrument Rebol code that the
        // console is using to implement *itself*, which it does with BLOCK!)
        // Same for Trace_Level seen by PARSE.
        //
        if (IS_GROUP(code)) {
            PG_Do = saved_do_hook;
            PG_Apply = saved_apply_hook;
            Trace_Level = Save_Trace_Level;
            Trace_Depth = Save_Trace_Depth;
        }

        // Both GROUP! and BLOCK! code is cancellable with Ctrl-C (though it's
        // up to HOST-CONSOLE on the next iteration to decide whether to
        // accept the cancellation or consider it an error condition or a
        // reason to fall back to the default skin).
        //
        Enable_Ctrl_C();
        result = rebDoValue(code);
        Disable_Ctrl_C();

        // If the custom DO and APPLY hooks were changed by the user code,
        // then save them...but restore the unhooked versions for the next
        // iteration of HOST-CONSOLE.  Same for Trace_Level seen by PARSE.
        //
        if (IS_GROUP(code)) {
            saved_do_hook = PG_Do;
            saved_apply_hook = PG_Apply;
            PG_Do = &Do_Core;
            PG_Apply = &Apply_Core;
            Save_Trace_Level = Trace_Level;
            Save_Trace_Depth = Trace_Depth;
            Trace_Level = 0;
            Trace_Depth = 0;
        }

        if (result != NULL) {
            status = rebBlank();
            continue;
        }

        // Otherwise it was a failure of some kind...get the last error and
        // signal it to the next iteration of HOST-CONSOLE.

        status = rebLastError();
        assert(status != NULL);
        result = rebVoid();

        if (IS_BAR(status)) // currently means halted, not really an ERROR!
            continue;

        if (IS_ERROR(status))
            continue;

        assert(IS_INTEGER(status));

    } while (NOT(IS_INTEGER(status)));

    int exit_status = VAL_INT32(status);

    rebFree(status);
    rebFree(code);
    rebFree(result);

    DROP_GUARD_VALUE(argv_value);

    SHUTDOWN_BOOT_EXTENSIONS();

    OS_QUIT_DEVICES(0);

#ifndef REB_CORE
    OS_Destroy_Graphics();
#endif

    Close_StdIO();

    // No need to do a "clean" shutdown, as we are about to exit the process
    // (Note: The debug build runs through the clean shutdown anyway!)
    //
    REBOOL clean = FALSE;
    rebShutdown(clean);

    return exit_status; // http://stackoverflow.com/questions/1101957/
}
