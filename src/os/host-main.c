//
//  File: %host-main.c
//  Summary: "Host environment main entry point"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
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

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef TO_WINDOWS
    #undef _WIN32_WINNT // https://forum.rebol.info/t/326/4
    #define _WIN32_WINNT 0x0501 // Minimum API target: WinXP
    #include <windows.h>

    #undef IS_ERROR // %windows.h defines this, but so does %sys-core.h
#endif


#include "sys-core.h"


// Initialization done by rebStartup() is intended to be as basic as possible
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
    // the console extension.  Halting should not be possible while the
    // mezzanine is loading.

    rebStartup();

    // With interpreter startup done, we want to turn the platform-dependent
    // argument strings into a block of Rebol strings as soon as possible.
    // That way the command line argument processing can be taken care of by
    // PARSE in the HOST-STARTUP user function, instead of C code!
    //
    REBVAL *argv_block = rebValue("lib/copy []", rebEND);

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
            continue; // !!! Comment here said "shell bug" (?)

        // Note: rebTextW() currently only supports UCS-2, so codepoints that
        // need more than two bytes to be represented will cause a failure.
        //
        rebElide("append", argv_block, rebR(rebTextW(argv_ucs2[i])), rebEND);
    }
  #else
    // Just take the ANSI C "char*" args...which should ideally be in UTF8.
    //
    int i = 0;
    for (; i != argc; ++i) {
        if (argv_ansi[i] == nullptr)
            continue; // !!! Comment here said "shell bug" (?)

        rebElide("append", argv_block, rebT(argv_ansi[i]), rebEND);
    }
  #endif

    size_t host_utf8_size;
    const int max = -1; // decompressed size is stored in gzip
    void *host_utf8_bytes = rebGunzipAlloc(
        &host_utf8_size,
        &Reb_Init_Code[0],
        REB_INIT_SIZE,
        max
    );

    // The inflated data was allocated with rebMalloc, and hence can be
    // repossessed as a BINARY!
    //
    REBVAL *host_bin = rebRepossess(host_utf8_bytes, host_utf8_size);

    // Use TRANSCODE to get a BLOCK! from the BINARY!, then release the binary
    //
    REBVAL *host_code = rebValue(
        "lib/transcode/file", host_bin, "%tmp-host-start.inc", rebEND
    );
    rebElide(
        "lib/ensure :lib/empty? lib/take/last", host_code, // empty bin @ tail
        rebEND
    );
    rebRelease(host_bin);

    // Create a new context specifically for startup.  This way, changes
    // to the user context should hopefully not affect it...e.g. if the user
    // redefines PRINT in their script, the console should keep working.
    //
    // !!! In the API source here calling methods textually, the current way
    // of insulating by using lib, e.g. `rebValue("lib/error?", ...)`, is still
    // using *the user context's notion of `lib`*.  So if they said `lib: 10`
    // then the console would die.  General API point to consider, as the
    // design emerges.
    //
    REBCTX *startup_ctx = Alloc_Context_Core(
        REB_OBJECT,
        80,
        NODE_FLAG_MANAGED // no PUSH_GC_GUARD needed, gets referenced
    );

    // Bind words that can be found in lib context (don't add any new words)
    //
    // !!! Directly binding to lib means that the startup *could* screw up and
    // overwrite lib declarations.  It should probably import its own copy,
    // just in case.  (Lib should also be protected by default)
    //
    Bind_Values_Deep(VAL_ARRAY_HEAD(host_code), Lib_Context);

    // Do two passes on the startup context.  One to find SET-WORD!s at the
    // top level and add them to the context, and another pass to deeply bind
    // to those declarations.
    //
    Bind_Values_Set_Midstream_Shallow(VAL_ARRAY_HEAD(host_code), startup_ctx);
    Bind_Values_Deep(VAL_ARRAY_HEAD(host_code), startup_ctx);

    // The new policy for source code in Ren-C is that it loads read only.
    // This didn't go through the LOAD Rebol action or anything like it, so
    // go ahead and lock it manually.
    //
    // !!! This file is supposed to be based on libRebol APIs, and the method
    // of creating a new context here is low level using the internal API.
    // However the console context is created should ideally be done in a
    // way that would work well for users, by leveraging modules or some other
    // level of abstraction, where issues like this would be taken care of.
    //
    rebElide("lib/lock", host_code, rebEND);

    REBVAL *host_start = rebValueInline(host_code); // HOST-START is an ACTION!
    rebRelease(host_code);

    if (rebNot("lib/action?", host_start, rebEND))
        rebJumps("lib/PANIC-VALUE", host_start, rebEND);

    // While some people may think that argv[0] in C contains the path to
    // the running executable, this is not necessarily the case.  The actual
    // method for getting the current executable path is OS-specific:
    //
    // https://stackoverflow.com/q/1023306/
    // http://stackoverflow.com/a/933996/211160
    //
    // It's not foolproof, so it might come back blank.  The console code can
    // then decide if it wants to fall back on argv[0]
    //
    REBVAL *exec_path = OS_GET_CURRENT_EXEC();
    rebElide(
        "system/options/boot: lib/ensure [blank! file!]", rebR(exec_path),
        rebEND
    );

    // This runs the HOST-START, which returns *requests* to execute
    // arbitrary code by way of its return results.  The TRAP and CATCH
    // are thus here to intercept bugs *in HOST-START itself*.
    //
    REBVAL *trapped = rebValue(
        "lib/entrap [",
            rebR(host_start), // action! that takes 2 args
            rebR(argv_block),
        "]", rebEND
    );

    if (rebDid("lib/error?", trapped, rebEND)) // error in HOST-START itself
        rebJumps("lib/PANIC", trapped, rebEND);

    REBVAL *code = rebValue("lib/first", trapped, rebEND); // entrap []'s output
    rebRelease(trapped); // don't need the outer block any more

    // !!! For the moment, the CONSOLE extension does all the work of running
    // usermode code or interpreting exit codes.  This requires significant
    // logic which is reused by the debugger, which ranges from the managing
    // of Ctrl-C enablement and disablement (and how that affects the ability
    // to set unix flags for unblocking file-I/O) to protecting against other
    // kinds of errors.  Hence there is a /PROVOKE refinement to CONSOLE
    // which feeds it an instruction, as if the console gave it to itself.

    REBVAL *result = rebValue("console/provoke", rebR(code), rebEND);

    int exit_status = rebUnboxInteger(rebR(result), rebEND);

    OS_QUIT_DEVICES(0);

    const bool clean = false; // process exiting, not necessary
    rebShutdown(clean); // Note: debug build runs a clean shutdown anyway

    return exit_status; // http://stackoverflow.com/q/1101957/
}
