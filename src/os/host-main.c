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


/* coverity[+kill] */
void Host_Crash(const char *reason) {
    OS_CRASH(cb_cast("REBOL Host Failure"), cb_cast(reason));
}


int Host_Repl(const REBVAL *repl_fun) {
    //
    // Currently, no code is run that doesn't implicitly lock the result.
    // But a guard would be needed if it ever did, so go ahead and do that.
    //
    DECLARE_LOCAL (result);
    Init_Void(result);
    PUSH_GUARD_VALUE(result);

    // Note that to avoid spurious longjmp clobbering warnings, last_failed
    // cannot be stack allocated to act cumulatively across fail()s.  (Dumb
    // compilers can't tell that all longjmps set error to non-NULL and will
    // subsequently assign last_failed to TRUE_VALUE.)
    //
    const REBVAL **last_failed = cast(const REBVAL**, malloc(sizeof(REBVAL*)));
    *last_failed = BLANK_VALUE; // indicate first call to REPL

    while (TRUE) {
    loop:;
        // !!! We do not want the trace level to apply to the REPL execution
        // itself.  Review how a usermode trace hook would recognize the
        // REPL dispatch and suspend tracing until the REPL ends.
        //
        REBINT Save_Trace_Level = Trace_Level;
        REBINT Save_Trace_Depth = Trace_Depth;
        Trace_Level = 0;
        Trace_Depth = 0;

        struct Reb_State state;
        REBCTX *error;

        PUSH_UNHALTABLE_TRAP(&error, &state); // must catch HALTs

    // The first time through the following code 'error' will be NULL, but...
    // `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

        if (error != NULL) {
            //
            // We don't really want the REPL code itself invoking HALT.  But
            // so long as we have a handler for Ctrl-C registered, it is
            // possible that the interrupt will happen while the REPL is
            // doing something (LOADing text, PRINTing errors, etc.)  If
            // so, just loop it.
            //
            // Note that currently, a Ctrl-C pressed during the INPUT command
            // will not be processed until after return is pressed.
            //
            if (ERR_NUM(error) == RE_HALT) {
                Init_Void(result);
                *last_failed = BAR_VALUE;
                goto loop;
            }

            panic (error); // !!! Handle if REPL has a bug/error in it?
        }

        const REBOOL fully = TRUE; // error if not all arguments consumed

        DECLARE_LOCAL (code);
        if (Apply_Only_Throws(
            code, // where return value of HOST-CONSOLE is saved
            fully,
            repl_fun, // HOST-CONSOLE function to run
            result, // last-result (always blank first run through loop)
            *last_failed, // TRUE, FALSE, BLANK! on first run, BAR! if HALT
            BLANK_VALUE, // focus-level, supplied by debugger REPL, not here
            BLANK_VALUE, // focus-frame, ...same
            END
        )){
            panic (code); // !!! Handle if REPL itself THROWs?
        }

        DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

        Trace_Level = Save_Trace_Level;
        Trace_Depth = Save_Trace_Depth;

        if (NOT(IS_BLOCK(code)))
            panic (code); // !!! Handle if REPL doesn't return a block?

        PUSH_UNHALTABLE_TRAP(&error, &state); // must catch HALTs

    // The first time through the following code 'error' will be NULL, but...
    // `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

        if (error != NULL) {
            if (ERR_NUM(error) == RE_HALT) { // not really an "ERROR!"
                Init_Void(result);
                *last_failed = BAR_VALUE; // informs REPL it was a HALT/Ctrl-C
                goto loop;
            }

            Init_Error(result, error);
            *last_failed = TRUE_VALUE;
            goto loop;
        }

        if (Do_Any_Array_At_Throws(result, code)) {
            if (
                IS_FUNCTION(result)
                && VAL_FUNC_DISPATCHER(result) == &N_quit
            ){
                // Command issued a purposeful QUIT or EXIT.  Convert the
                // QUIT/WITH value (if any) into an exit status and end loop.
                //
                DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);
                CATCH_THROWN(result, result);
                DROP_GUARD_VALUE(result);

                free(m_cast(REBVAL**, last_failed)); // C++ free() mutable ptr
                return Exit_Status_From_Value(result);
            }

            fail (Error_No_Catch_For_Throw(result));
        }

        DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

        // NOTE: Although the operation has finished at this point, it may
        // be that a Ctrl-C set up a pending FAIL, which will be triggered
        // during output below.  See the PUSH_UNHALTABLE_TRAP in the caller.

        *last_failed = FALSE_VALUE; // success, so REPL should output result
    }

    DEAD_END;
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
        rebEscape();
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

BOOL WINAPI Handle_Nothing(DWORD dwCtrlType)
{
    UNUSED(dwCtrlType);
    return TRUE;
}

#else

//
// Hook registered via `signal()`.
//
static void Handle_Signal(int sig)
{
    UNUSED(sig);
    rebEscape();
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
    rebInit(Host_Lib);

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

    // !!! Note that the first element of the argv_value block is used to
    // initialize system/options/boot by the startup code.  The real way to
    // get the path to the executable varies by OS, and should either be
    // passed in independently (with no argv[0]) or substituted in the first
    // element of the array:
    //
    // http://stackoverflow.com/a/933996/211160
    //
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

    struct Reb_State state;
    REBCTX *error;

    PUSH_UNHALTABLE_TRAP(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

    int exit_status;

    if (error != NULL) {
        //
        // We want to avoid doing I/O directly from the C code of the host,
        // and let that go through WRITE-STDOUT.  Hence any part of the
        // startup that can error should be TRAP'd by the startup code itself
        // and handled or PRINT'd in some way.
        //
        if (ERR_NUM(error) != RE_HALT)
            panic (error);

        exit_status = 128; // http://stackoverflow.com/questions/1101957/
    }
    else {
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

        DECLARE_LOCAL (host_start);
        if (Do_At_Throws(
            host_start, // returned value must be a FUNCTION!
            array,
            0,
            SPECIFIED
        )){
            panic (startup); // just loads functions, shouldn't QUIT or error
        }

        if (!IS_FUNCTION(host_start))
            panic (host_start); // should not be able to error

        Free_Series(startup);

        DECLARE_LOCAL (ext_value);
        Init_Blank(ext_value);
        LOAD_BOOT_EXTENSIONS(ext_value);

        const REBOOL fully = TRUE; // error if not all arguments are consumed

        DECLARE_LOCAL(exec_path);
        REBCHR *path;
        REBINT path_len = OS_GET_CURRENT_EXEC(&path);
        if (path_len < 0){
            Init_Blank(exec_path);
        } else {
            Init_File(exec_path,
                To_REBOL_Path(path, path_len, (OS_WIDE ? PATH_OPT_UNI_SRC : 0))
                );
            OS_FREE(path);
        }

        DECLARE_LOCAL (result);
        if (Apply_Only_Throws(
            result,
            fully,
            host_start, // startup function, implicit GC guard
            exec_path,  // path to executable file, implicit GC guard
            argv_value, // argv parameter, implicit GC guard
            ext_value,
            END
        )){
            if (
                IS_FUNCTION(result)
                && VAL_FUNC_DISPATCHER(result) == &N_quit
            ){
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

        // HOST-START returns either a FUNCTION! to act as the REPL, or an
        // integer exit code if no REPL should be spawned.
        //
        if (IS_FUNCTION(result)) {
            PUSH_GUARD_VALUE(result);
            exit_status = Host_Repl(result);
            DROP_GUARD_VALUE(result);
        }
        else if (IS_INTEGER(result))
            exit_status = VAL_INT32(result);
        else
            panic (result); // no other legal return values for now
    }

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

    return exit_status;
}
