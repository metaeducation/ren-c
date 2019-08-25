//
//  File: %main.c
//  Summary: "Console application main entry point"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Rebol Open Source Contributors
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
//=////////////////////////////////////////////////////////////////////////=//
//
// This contains the main() routine, which uses the libRebol API to start up
// an interactive console system for environments that can compile C.
//
// On POSIX systems it uses <termios.h> to implement line editing:
//
// http://pubs.opengroup.org/onlinepubs/7908799/xbd/termios.html
//
// On Windows it uses the Console API:
//
// https://msdn.microsoft.com/en-us/library/ms682087.aspx
//
// Very little work is done in C.  For instance, the command line arguments
// are processed using PARSE by Rebol code that is embedded into the
// executable as compressed bytes.  And the majority of the console behavior
// is defined by Rebol code in %extensions/console (though it has some of
// its own C to handle things like SIGINT for Ctrl-C handling).
//

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef TO_WINDOWS
    #undef _WIN32_WINNT  // https://forum.rebol.info/t/326/4
    #define _WIN32_WINNT 0x0501  // Minimum API target: WinXP
    #define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
    #include <windows.h>

    #include <shellapi.h>  // for CommandLineToArgvW()
#endif

#include "pstdint.h"  // stdint.h for builds w/pre-C99 compilers--see notes
#include "pstdbool.h"  // stdbool.h for builds w/pre-C99 compilers--see notes

// This file should only use the external API.  However, it can be helpful in
// debug situations to have access to PROBE() and other internal features.
//
#if !defined(DEBUG_MAIN_USING_SYS_CORE)
    #define REBOL_EXPLICIT_END  // for building w/pre-C99 compilers--see notes
    #include "rebol.h"  // note: includes pstdint.h and pstdbool.h by default
#else
    #undef IS_ERROR  // windows.h has its own definition of this macro
    #include "sys-core.h"
#endif

#include "assert-fixes.h"  // see file for notes

#include "reb-c.h"  // provides cast(), UNUSED(), etc.


// Initialization done by rebStartup() is intended to be as basic as possible
// in order to get the Rebol series/values/array functions ready to be run.
// Once that's ready, the rest of the initialization can take advantage of
// a working evaluator.  This includes PARSE to process the command line
// parameters, or PRINT to output boot banners.
//
// The %prep-main.reb script bundles the %main-startup.reb with some other
// files, and turns the bundle into a compressed binary C literal.  That
// literal can be LOADed and executed to return the MAIN-STARTUP function,
// which takes the command line arguments as an array of STRING! and handles
// it from there.
//
#include "tmp-main-startup.inc"


#ifdef TO_WINDOWS
    //
    // Most Windows-specific code is expected to be run in extensions (or
    // in the interim, in "devices").  However, it's expected that all Windows
    // code be able to know its `HINSTANCE`.  This is usually passed in a
    // WinMain(), but since we don't use WinMain() in order to be able to
    // act as a console app -or- a GUI app some tricks are needed to capture
    // it, and then export it for other code to use.
    //
    // !!! This is not currently exported via EXTERN_C, because the core was
    // building in a dependency on the host.  This created problems for the
    // libRebol, which needs to be independent of %host-main.c, and may be
    // used with clients that do not have the HINSTANCE easily available.
    // The best idea for exporting it is probably to have those clients who
    // provide it to inject it into the system object as a HANDLE!, so that
    // those extensions which need it have access to it, while not creating
    // problems for those that do not.
    //
    HINSTANCE App_Instance = 0;

    // For why this is done this way with a potential respawning, see the
    // StackOverflow question:
    //
    // "Can one executable be both a console and a GUI application":
    //
    //     http://stackoverflow.com/q/493536/
    //
    void Determine_Hinstance_May_Respawn(WCHAR *this_exe_path) {
        if (GetStdHandle(STD_OUTPUT_HANDLE) == 0) {
            //
            // No console to attach to, we must be the DETACHED_PROCESS which
            // was spawned in the below branch.
            //
            App_Instance = GetModuleHandle(nullptr);
        }
        else {
          #ifdef REB_CORE
            //
            // In "Core" mode, use a console but do not initialize graphics.
            // (stdio redirection works, blinking console window during start)
            //
            App_Instance = cast(HINSTANCE,
                GetWindowLongPtr(GetConsoleWindow(), GWLP_HINSTANCE)
            );
            UNUSED(this_exe_path);
          #else
            //
            // In the "GUI app" mode, stdio redirection doesn't work properly,
            // but no blinking console window during start.
            //
            if (not this_exe_path) { // argc was > 1
                App_Instance = cast(HINSTANCE,
                    GetWindowLongPtr(GetConsoleWindow(), GWLP_HINSTANCE)
                );
            }
            else {
                // Launch child as a DETACHED_PROCESS so that GUI can be
                // initialized, and exit.
                //
                STARTUPINFO startinfo;
                ZeroMemory(&startinfo, sizeof(startinfo));
                startinfo.cb = sizeof(startinfo);

                PROCESS_INFORMATION procinfo;
                if (not CreateProcess(
                    nullptr, // lpApplicationName
                    this_exe_path, // lpCommandLine
                    nullptr, // lpProcessAttributes
                    nullptr, // lpThreadAttributes
                    FALSE, // bInheritHandles
                    CREATE_DEFAULT_ERROR_MODE | DETACHED_PROCESS,
                    nullptr, // lpEnvironment
                    nullptr, // lpCurrentDirectory
                    &startinfo,
                    &procinfo
                )){
                    MessageBox(
                        nullptr, // owner window
                        L"CreateProcess() failed in %host-main.c",
                        this_exe_path, // title
                        MB_ICONEXCLAMATION | MB_OK
                    );
                }

                exit(0);
            }
          #endif
        }
    }
#endif


//=//// MAIN ENTRY POINT //////////////////////////////////////////////////=//
//
// Using a main() entry point for a console program (as opposed to WinMain())
// so we can connect to the console.  See Determine_Hinstance_May_Respawn().
//
int main(int argc, char *argv_ansi[])
{
    // Note: By default, Ctrl-C is not hooked or handled.  This is done by
    // the console extension (%extensions/console).  Halting should not be
    // possible while the mezzanine is loading.

    rebStartup();

    // With interpreter startup done, we want to turn the platform-dependent
    // argument strings into a block of Rebol strings as soon as possible.
    // That way the command line argument processing can be taken care of by
    // PARSE in the MAIN-STARTUP user function, instead of C code!
    //
    REBVAL *argv_block = rebValue("copy []", rebEND);

  #ifdef TO_WINDOWS
    //
    // Were we using WinMain we'd be getting our arguments in Unicode, but
    // since we're using an ordinary main() we do not.  However, this call
    // lets us slip out and pick up the arguments in Unicode form (UTF-16).
    //
    WCHAR **argv_ucs2 = CommandLineToArgvW(GetCommandLineW(), &argc);
    UNUSED(argv_ansi);

    Determine_Hinstance_May_Respawn(argc > 1 ? nullptr : argv_ucs2[0]);

    int i;
    for (i = 0; i != argc; ++i) {
        if (argv_ucs2[i] == nullptr)
            continue;  // !!! R3-Alpha commented here saying "shell bug" (?)

        // Note: rebTextWide() currently only supports UCS-2, so codepoints
        // needing more than two bytes to be represented will cause a failure.
        //
        rebElide(
            "append/only", argv_block, rebR(rebTextWide(argv_ucs2[i])),
        rebEND);
    }
  #else
    // Just take the ANSI C "char*" args...which should ideally be in UTF-8.
    //
    int i = 0;
    for (; i != argc; ++i) {
        if (argv_ansi[i] == nullptr)
            continue;  // !!! R3-Alpha commented here saying "shell bug" (?)

        rebElide("append/only", argv_block, rebT(argv_ansi[i]), rebEND);
    }
  #endif

    size_t startup_utf8_size;
    const int max = -1;  // decompressed size is stored in gzip
    void *startup_utf8_bytes = rebGunzipAlloc(
        &startup_utf8_size,
        &Main_Startup_Code[0],
        MAIN_STARTUP_SIZE,
        max
    );

    // The inflated data was allocated with rebMalloc, and hence can be
    // repossessed as a BINARY!
    //
    REBVAL *startup_bin = rebRepossess(startup_utf8_bytes, startup_utf8_size);

    // !!! The startup code isn't really set up to run as a Module, though it
    // probably should be.  This is a carry-over of what some %sys-core.h
    // code was doing...adding top-level set-words to the lib context, while
    // binding everything to lib.  What you avoid by running this in the
    // user context is getting an importation of every word mentioned in the
    // host startup file...that's a lot of words, like CONSOLE.  And once the
    // word is imported from lib as NULL, if it gets added later (e.g. by
    // loading extensions) it won't update:
    //
    // https://forum.rebol.info/t/764
    //
    // R3-Alpha didn't resolve these issues, so for now the code is mostly
    // just following what it did before...yet pushing more and more of it
    // out through a user-friendly API.  But fundamental work is needed.
    //
    REBVAL *main_startup = rebValue(
        "use [code] [",
            "transcode 'code", rebR(startup_bin),
            "bind/only/set code lib",  // only ADD top level set-word!s to lib
            "bind code lib",  // but BIND to anything else that exists in lib
            "do code",
        "]",
    rebEND);

    if (rebNot("action?", rebQ1(main_startup), rebEND))
        rebJumps("PANIC-VALUE", rebQ1(main_startup), rebEND);  // terminates

    // This runs the MAIN-STARTUP, which returns *requests* to execute
    // arbitrary code by way of its return results.  The ENTRAP is thus here
    // to intercept bugs *in MAIN-STARTUP itself*.
    //
    REBVAL *trapped = rebValue(
        "entrap [",  // MAIN-STARTUP action! takes one argument (argv[])
            main_startup, rebR(argv_block),
        "]",
    rebEND);
    rebRelease(main_startup);

    if (rebDid("error?", trapped, rebEND))  // error in MAIN-STARTUP itself
        rebJumps("PANIC", trapped, rebEND);  // terminates

    REBVAL *code = rebValue("first", trapped, rebEND); // entrap's output
    rebRelease(trapped);  // don't need the outer block any more

    // !!! For the moment, the CONSOLE extension does all the work of running
    // usermode code or interpreting exit codes.  This requires significant
    // logic which is reused by the debugger, which ranges from the managing
    // of Ctrl-C enablement and disablement (and how that affects the ability
    // to set unix flags for unblocking file-I/O) to protecting against other
    // kinds of errors.  Hence there is a /PROVOKE refinement to CONSOLE
    // which feeds it an instruction, as if the console gave it to itself.

    REBVAL *result = rebValue("console/provoke", rebR(code), rebEND);

    int exit_status = rebUnboxInteger(rebR(result), rebEND);

    const bool clean = false;  // process exiting, not necessary
    rebShutdown(clean);  // Note: debug build runs a clean shutdown anyway

    return exit_status;  // http://stackoverflow.com/q/1101957/
}
