//
//  file: %mod-call.c
//  summary: "Native Functions for spawning and controlling processes"
//  section: extension
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 Atronix Engineering
// Copyright 2012-2019 Ren-C Open Source Contributors
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
    #define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
    #include <windows.h>

    // As is typical, Microsoft's own header files don't work through with
    // the static analyzer, disable checking of their _In_out_ annotations
    // (which we don't use, anyway):
    //
    //   https://developercommunity.visualstudio.com/t/warning-C6553:-The-annotation-for-functi/1676659

  #if defined(_MSC_VER) && defined(_PREFAST_)  // _PREFAST_ if MSVC /analyze
    #pragma warning(disable : 6282)  // suppress "incorrect operator" [1]
  #endif

    #include <process.h>
    #include <shlobj.h>

    #undef OUT  // %minwindef.h defines this, we have a better use for it
    #undef VOID  // %winnt.h defines this, we have a better use for it
#else
    #if !defined(__cplusplus) && TO_LINUX
        //
        // See feature_test_macros(7), this definition is redundant under C++
        //
        #define _GNU_SOURCE // Needed for pipe2 when #including <unistd.h>
    #endif
    #include <unistd.h>
    #include <stdlib.h>

    // The location of "environ" (environment variables inventory that you
    // can walk on POSIX) can vary.  Some put it in stdlib, some put it
    // in <unistd.h>.  And OS X doesn't define it in a header at all, you
    // just have to declare it yourself.  :-/
    //
    // https://stackoverflow.com/a/31347357/211160
    //
    #if TO_OSX || TO_OPENBSD_X64
        extern char **environ;
    #endif

    #include <errno.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <signal.h>
    #include <sys/stat.h>
    #include <sys/wait.h>
    #if !defined(WIFCONTINUED) && TO_ANDROID
    // old version of bionic doesn't define WIFCONTINUED
    // https://android.googlesource.com/platform/bionic/+/c6043f6b27dc8961890fed12ddb5d99622204d6d%5E%21/#F0
        # define WIFCONTINUED(x) (WIFSTOPPED(x) && WSTOPSIG(x) == 0xffff)
    #endif
#endif

#include "sys-core.h"
#include "tmp-mod-process.h"

#include "reb-process.h"


//
//  export call-internal*: native [
//
//  "Run another program by spawning a new process"
//
//      return: "If :WAIT, the forked process ID, else exit code"
//          [integer!]
//      command "OS-local command line, block with arguments, executable file"
//          [text! block! file!]
//      :wait "Wait for command to terminate before returning"
//      :console "Runs command with I/O redirected to console"
//      :shell "Forces command to be run from shell"
//      :info "Returns process information object"
//      :input "Redirects stdin (none = /dev/null)"
//          [~(none inherit)~ text! blob! file!]
//      :output "Redirects stdout (none = /dev/null)"
//          [~(none inherit)~ text! blob! file!]
//      :error "Redirects stderr (none = /dev/null)"
//          [~(none inherit)~ text! blob! file!]
//  ]
//
DECLARE_NATIVE(CALL_INTERNAL_P)
//
// !!! Parameter usage may require WAIT mode even if not explicitly requested.
// /WAIT should be default, with /ASYNC (or otherwise) as exception!
{
    return Call_Core(level_);
}


//
//  export get-os-browsers: native [
//
//  "Ask the OS or registry what command(s) to use for starting a browser"
//
//      return: "Block of strings, %1 should be substituted with the string"
//          [block!]
//  ]
//
DECLARE_NATIVE(GET_OS_BROWSERS)
//
// !!! Using the %1 convention is not necessarily ideal vs. having some kind
// of more "structural" result, it was just easy because it's how the string
// comes back from the Windows registry.  Review.
{
    INCLUDE_PARAMS_OF_GET_OS_BROWSERS;

    Value* list = rebValue("copy []");

  #if TO_WINDOWS

    HKEY key;
    if (
        RegOpenKeyEx(
            HKEY_CLASSES_ROOT,
            L"http\\shell\\open\\command",
            0,
            KEY_READ,
            &key
        ) != ERROR_SUCCESS
    ){
        panic ("Could not open registry key for http\\shell\\open\\command");
    }

    DWORD num_bytes = 0; // pass NULL and use 0 for initial length, to query

    DWORD type;
    DWORD flag = RegQueryValueExW(key, L"", 0, &type, NULL, &num_bytes);

    if (
        (flag != ERROR_MORE_DATA and flag != ERROR_SUCCESS)
        or num_bytes == 0
        or type != REG_SZ // RegQueryValueExW returns unicode
        or num_bytes % 2 != 0 // byte count should be even for unicode
    ){
        RegCloseKey(key);
        return PANIC(
            "Could not read registry key for http\\shell\\open\\command"
        );
    }

    REBLEN len = num_bytes / 2;

    WCHAR *buffer = rebAllocN(WCHAR, len + 1); // include terminator

    flag = RegQueryValueEx(
        key, L"", 0, &type, cast(LPBYTE, buffer), &num_bytes
    );
    RegCloseKey(key);

    if (flag != ERROR_SUCCESS)
        return PANIC(
            "Could not read registry key for http\\shell\\open\\command"
        );

    while (buffer[len - 1] == '\0') {
        //
        // Don't count terminators; seems the guarantees are a bit fuzzy
        // about whether the string in the registry has one included in the
        // byte count or not.
        //
        --len;
    }

    rebElide("append", list, rebR(rebLengthedTextWide(buffer, len)));

    rebFree(buffer);

  #elif TO_LINUX

    // Caller should try xdg-open first, then try x-www-browser otherwise
    //
    rebElide(
        "append", list, "spread [",
            rebT("xdg-open %1"),
            rebT("x-www-browser %1"),
        "]"
    );

  #elif TO_HAIKU

    rebElide("append", list, rebT("open %1"));

  #else // Just try /usr/bin/open on POSIX, OS X, etc.

    rebElide("append", list, rebT("/usr/bin/open %1"));

  #endif

    return list;
}


//
//  export sleep: native [
//
//  "Use system sleep to wait a certain amount of time (doesn't use PORT!s)"
//
//      return: []
//      duration "Length to sleep (integer and decimal measure seconds)"
//          [integer! decimal! time!]
//  ]
//
DECLARE_NATIVE(SLEEP)
//
// !!! This was a temporary workaround for the fact that it is not currently
// possible to do a WAIT on a time from within an AWAKE handler.  A proper
// solution would presumably solve that problem, so two different functions
// would not be needed.
//
// This function was needed by @GrahamChiu, and putting it in the CALL module
// isn't necessarily ideal, but it's better than making the core dependent
// on Sleep() vs. usleep()...and all the relevant includes have been
// established here.
{
    INCLUDE_PARAMS_OF_SLEEP;

    REBLEN msec = Milliseconds_From_Value(ARG(DURATION));

  #if TO_WINDOWS
    Sleep(msec);
  #else
    usleep(msec * 1000);
  #endif

    return TRIPWIRE;
}


#if TO_LINUX || TO_ANDROID || TO_POSIX || TO_OSX || TO_HAIKU
static Bounce Delegate_Kill_Process(pid_t pid, int signal)
{
    if (kill(pid, signal) >= 0)
        return "~";  // success

    switch (errno) {
      case EINVAL:
        return rebDelegate(
            "panic [-[Invalid signal number:]-", rebI(signal), "]"
        );

      case EPERM:
        return Delegate_Panic_Permission_Denied();

      case ESRCH:
        return Delegate_Panic_No_Process(rebInteger(pid));  // releases integer

      default:
        return rebDelegate("panic", rebError_OS(errno));
    }
}
#endif


//
//  export terminate: native [
//
//  "Terminate a process (not current one)"
//
//      return: []
//      pid "The process ID"
//          [integer!]
//  ]
//
DECLARE_NATIVE(TERMINATE)
{
    INCLUDE_PARAMS_OF_TERMINATE;

  #if TO_WINDOWS

    if (GetCurrentProcessId() == cast(DWORD, VAL_INT32(ARG(PID))))
        return FAIL(
          "QUIT or SYS.UTIL/EXIT terminate current process, not TERMINATE"
        );

    DWORD err = 0;
    HANDLE ph = OpenProcess(PROCESS_TERMINATE, FALSE, VAL_INT32(ARG(PID)));
    if (ph == NULL) {
        err = GetLastError();
        switch (err) {
          case ERROR_ACCESS_DENIED:
            return Delegate_Panic_Permission_Denied();

          case ERROR_INVALID_PARAMETER:
            return Delegate_Panic_No_Process(ARG(PID));

          default:
            return Delegate_Panic_Terminate_Failed(err);
        }
    }

    if (TerminateProcess(ph, 0)) {
        CloseHandle(ph);
        return TRIPWIRE;
    }

    err = GetLastError();
    CloseHandle(ph);
    switch (err) {
      case ERROR_INVALID_HANDLE:
        return Delegate_Panic_No_Process(ARG(PID));

      default:
        return Delegate_Panic_Terminate_Failed(err);
    }

  #elif TO_LINUX || TO_ANDROID || TO_POSIX || TO_OSX || TO_HAIKU

    if (getpid() == VAL_INT32(ARG(PID))) {  // signal not reliable for this
        return PANIC(
            "QUIT or SYS.UTIL/EXIT to terminate current process, instead"
        );
    }

    return Delegate_Kill_Process(VAL_INT32(ARG(PID)), SIGTERM);

  #else

    return FAIL("terminate is not implemented for this platform");

  #endif
}
