//
//  File: %mod-call.c
//  Summary: "Native Functions for spawning and controlling processes"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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

#include "tmp-mod-process.h"

#include "reb-process.h"


//
//  export call-internal*: native [
//
//  "Run another program by spawning a new process"
//
//      return: "If /WAIT, the forked process ID, else exit code"
//          [integer!]
//      command "OS-local command line, block with arguments, executable file"
//          [text! block! file!]
//      /wait "Wait for command to terminate before returning"
//      /console "Runs command with I/O redirected to console"
//      /shell "Forces command to be run from shell"
//      /info "Returns process information object"
//      /input "Redirects stdin (none = /dev/null)"
//          ['none 'inherit text! binary! file!]
//      /output "Redirects stdout (none = /dev/null)"
//          ['none 'inherit text! binary! file!]
//      /error "Redirects stderr (none = /dev/null)"
//          ['none 'inherit text! binary! file!]
//  ]
//
DECLARE_NATIVE(call_internal_p)
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
//      return: [block!]
//          {Block of strings, where %1 should be substituted with the string}
//  ]
//
DECLARE_NATIVE(get_os_browsers)
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
        fail ("Could not open registry key for http\\shell\\open\\command");
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
        fail ("Could not read registry key for http\\shell\\open\\command");
    }

    REBLEN len = num_bytes / 2;

    WCHAR *buffer = rebAllocN(WCHAR, len + 1); // include terminator

    flag = RegQueryValueEx(
        key, L"", 0, &type, cast(LPBYTE, buffer), &num_bytes
    );
    RegCloseKey(key);

    if (flag != ERROR_SUCCESS)
        fail ("Could not read registry key for http\\shell\\open\\command");

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
//      return: [~]
//      duration [integer! decimal! time!]
//          {Length to sleep (integer and decimal are measuring seconds)}
//  ]
//
DECLARE_NATIVE(sleep)
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

    REBLEN msec = Milliseconds_From_Value(ARG(duration));

  #if TO_WINDOWS
    Sleep(msec);
  #else
    usleep(msec * 1000);
  #endif

    return NOTHING;
}


#if TO_LINUX || TO_ANDROID || TO_POSIX || TO_OSX || TO_HAIKU
static void kill_process(pid_t pid, int signal)
{
    if (kill(pid, signal) >= 0)
        return; // success

    switch (errno) {
      case EINVAL:
        rebJumps("fail [{Invalid signal number:}", rebI(signal), "]");

      case EPERM:
        Fail_Permission_Denied();

      case ESRCH:
        Fail_No_Process(rebInteger(pid)); // failure releases integer handle

      default:
        rebFail_OS(errno);
    }
}
#endif


//
//  export terminate: native [
//
//  "Terminate a process (not current one)"
//
//      return: [~null~]
//      pid [integer!]
//          {The process ID}
//  ]
//
DECLARE_NATIVE(terminate)
{
    INCLUDE_PARAMS_OF_TERMINATE;

  #if TO_WINDOWS

    if (GetCurrentProcessId() == cast(DWORD, VAL_INT32(ARG(pid))))
        return RAISE(
          "QUIT or SYS.UTIL/EXIT terminate current process, not TERMINATE"
        );

    DWORD err = 0;
    HANDLE ph = OpenProcess(PROCESS_TERMINATE, FALSE, VAL_INT32(ARG(pid)));
    if (ph == NULL) {
        err = GetLastError();
        switch (err) {
          case ERROR_ACCESS_DENIED:
            Fail_Permission_Denied();

          case ERROR_INVALID_PARAMETER:
            Fail_No_Process(ARG(pid));

          default:
            Fail_Terminate_Failed(err);
        }
    }

    if (TerminateProcess(ph, 0)) {
        CloseHandle(ph);
        return nullptr;
    }

    err = GetLastError();
    CloseHandle(ph);
    switch (err) {
      case ERROR_INVALID_HANDLE:
        Fail_No_Process(ARG(pid));

      default:
        Fail_Terminate_Failed(err);
    }

  #elif TO_LINUX || TO_ANDROID || TO_POSIX || TO_OSX || TO_HAIKU

    if (getpid() == VAL_INT32(ARG(pid))) {
        // signal is not as reliable for this purpose
        // it's caught in main.c as to stop the evaluation
        fail ("QUIT or SYS.UTIL/EXIT to terminate current process, instead");
    }
    kill_process(VAL_INT32(ARG(pid)), SIGTERM);
    return nullptr;

  #else

    return RAISE("terminate is not implemented for this platform");

  #endif
}


//
//  export get-env: native [
//
//  "Returns the value of an OS environment variable (for current process)"
//
//      return: "String the variable was set to, or null if not set"
//          [~null~ text!]
//      variable "Name of variable to get (case-insensitive in Windows)"
//          [<maybe> text! word!]
//  ]
//
DECLARE_NATIVE(get_env)
//
// !!! Prescriptively speaking, it is typically considered a bad idea to treat
// an empty string environment variable as different from an unset one:
//
// https://unix.stackexchange.com/q/27708/
//
// It might be worth it to require a refinement to treat empty strings in a
// different way, or to return them as BLANK! instead of plain TEXT! so they
// were falsey like nulls but might trigger awareness of their problematic
// nature in some string routines.  Review.
{
    INCLUDE_PARAMS_OF_GET_ENV;

    Value* variable = ARG(variable);

    Value* error = nullptr;

  #if TO_WINDOWS
    // Note: The Windows variant of this API is NOT case-sensitive

    WCHAR *key = rebSpellWide("@", variable);

    DWORD val_len_plus_one = GetEnvironmentVariable(key, nullptr, 0);
    if (val_len_plus_one == 0) {  // some failure...
        DWORD dwerr = GetLastError();
        if (dwerr == ERROR_ENVVAR_NOT_FOUND)
            Init_Nulled(OUT);
        else
            error = rebError_OS(dwerr);  // don't call GetLastError() twice!
    }
    else {
        WCHAR *val = rebAllocN(WCHAR, val_len_plus_one);
        DWORD val_len = GetEnvironmentVariable(key, val, val_len_plus_one);

        // This is tricky, because although GetEnvironmentVariable() says that
        // a 0 return means an error, it also says it is the length of the
        // variable minus the terminator (when the passed in buffer is of a
        // sufficient size).
        //
        // https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getenvironmentvariable
        //
        // So if a variable is set-but-empty, then it could return 0 in this
        // second step.  (Who would design such an API?!  Why wouldn't it just
        // consistently return length including the terminator regardless of
        // whether the buffer is big enough or not, so 0 is always an error?!)
        //
        // Such variables can't be assigned with SET, since `set var=` will
        // clear it.  But other mechanisms can...including GitHub Actions when
        // it sets up `env:` variables.
        //
        if (val_len + 1 != val_len_plus_one) {
            DWORD dwerr = GetLastError();
            if (dwerr == 0) {  // in case this ever happens, give more info
                error = rebValue("make error! spaced [",
                    "{Mystery bug getting environment var} @", ARG(variable),
                    "{with length reported as}", rebI(val_len_plus_one - 1),
                    "{but returned length from fetching is}", rebI(val_len),
                "]");
            }
            else
                error = rebError_OS(dwerr);
        }
        else {
            Value* temp = rebLengthedTextWide(val, val_len_plus_one - 1);
            Copy_Cell(OUT, temp);
            rebRelease(temp);
        }
        rebFree(val);
    }

    rebFree(key);
  #else
    // Note: The Posix variant of this API is case-sensitive

    char *key = rebSpell("@", variable);

    const char* val = getenv(key);
    if (val == nullptr)  // key not present in environment
        Init_Nulled(OUT);
    else {
        size_t size = strsize(val);

        /* assert(size != 0); */  // True?  Should it return BLANK!?

        Init_Text(OUT, Make_Sized_String_UTF8(val, size));
    }

    rebFree(key);
  #endif

    // Error is broken out like this so that the proper freeing can be done
    // without leaking temporary buffers.
    //
    if (error != nullptr)
        rebJumps ("fail", rebR(error));

    return OUT;
}


//
//  export set-env: native [
//
//  "Sets value of operating system environment variable for current process"
//
//      return: "Returns same value passed in"
//          [~null~ text!]
//      variable [<maybe> text! word!]
//          "Variable to set (case-insensitive in Windows)"
//      value [~null~ text!]
//          "Value to set the variable to, or NULL to unset it"
//  ]
//
DECLARE_NATIVE(set_env)
{
    INCLUDE_PARAMS_OF_SET_ENV;

    Value* variable = ARG(variable);
    Value* value = ARG(value);

  #if TO_WINDOWS
    WCHAR* key_wide = rebSpellWide(variable);
    Option(WCHAR*) val_wide = rebSpellWideMaybe("ensure [~null~ text!]", value);

    if (not SetEnvironmentVariable(
        key_wide,
        maybe val_wide  // null means unset the environment variable
    )){
        Value* error = rebError_OS(GetLastError());
        rebJumps ("fail", rebR(error));
    }

    rebFree(maybe val_wide);  // nulls no-op for rebFree()
    rebFree(key_wide);
  #else
    char *key_utf8 = rebSpell(variable);

    if (Is_Nulled(value)) {  // distinct setenv() and unsetenv() calls
      #ifdef unsetenv
        if (unsetenv(key_utf8) == -1)
            rebJumps ("fail {unsetenv() couldn't unset environment variable}");
      #else
        // WARNING: SPECIFIC PORTABILITY ISSUE
        //
        // Simply saying putenv("FOO") will delete FOO from the environment,
        // but it's not consistent...does nothing on NetBSD for instance.  But
        // not all other systems have unsetenv...
        //
        // http://julipedia.meroh.net/2004/10/portability-unsetenvfoo-vs-putenvfoo.html
        //
        // going to hope this case doesn't hold onto the string...
        //
        if (putenv(key_utf8) == -1) // !!! Why mutable?
            rebJumps ("fail {putenv() couldn't unset environment variable}");
      #endif
    }
    else {
      #ifdef setenv
        char *val_utf8 = rebSpell(value);

        if (setenv(key_utf8, val_utf8, 1) == -1) // the 1 means "overwrite"
            rebJumps ("fail {setenv() couldn't set environment variable}");

        rebFree(val_utf8);
      #else
        // WARNING: SPECIFIC MEMORY LEAK!
        //
        // putenv takes its argument as a single "key=val" string.  It is
        // *fatally flawed*, and obsoleted by setenv and unsetenv in System V:
        //
        // http://stackoverflow.com/a/5876818/211160
        //
        // Once you have passed a string to it you never know when that string
        // will no longer be needed.  Thus it may either not be dynamic or you
        // must leak it, or track a local copy of the environment yourself.
        //
        // If you're stuck without setenv on some old platform, but really
        // need to set an environment variable, here's a way that just leaks a
        // string each time you call.  The code would have to keep track of
        // each string added in some sort of a map...which is currently deemed
        // not worth the work.

        char *key_equals_val_utf8 = rebSpell(
            "unspaced [", variable, "{=}", value, "]"
        );

        char *duplicate = strdup(key_equals_val_utf8);

        if (putenv(duplicate) == -1)  // leak!  (why mutable?  :-/)
            rebJumps ("fail {putenv() couldn't set environment variable}");

        rebFree(key_equals_val_utf8);
      #endif
    }

    rebFree(key_utf8);
  #endif

    return COPY(ARG(value));
}


//
//  export list-env: native [
//
//  "Returns a map of OS environment variables (for current process)"
//
//      return: [map!]
//  ]
//
DECLARE_NATIVE(list_env)
{
    INCLUDE_PARAMS_OF_LIST_ENV;

    Value* map = rebValue("make map! []");

  #if TO_WINDOWS
    //
    // Windows environment strings are sequential null-terminated strings,
    // with a 0-length string signaling end ("keyA=valueA\0keyB=valueB\0\0")
    // We count the strings to know how big an array to make, and then
    // convert the array into a MAP!.
    //
    // !!! Adding to a map as we go along would probably be better.

    WCHAR *env = GetEnvironmentStrings();

    REBLEN len;
    const WCHAR *key_equals_val = env;
    while ((len = wcslen(key_equals_val)) != 0) {
        const WCHAR *eq_pos = wcschr(key_equals_val, '=');

        // "What are these strange =C: environment variables?"
        // https://blogs.msdn.microsoft.com/oldnewthing/20100506-00/?p=14133
        //
        if (eq_pos == key_equals_val) {
            key_equals_val += len + 1; // next
            continue;
        }

        int key_len = eq_pos - key_equals_val;
        Value* key = rebLengthedTextWide(key_equals_val, key_len);

        int val_len = len - (eq_pos - key_equals_val) - 1;
        Value* val = rebLengthedTextWide(eq_pos + 1, val_len);

        rebElide(
            "append", map, "spread [", rebR(key), rebR(val), "]"
        );

        key_equals_val += len + 1; // next
    }

    FreeEnvironmentStrings(env);
  #else
    // Note: 'environ' is an extern of a global found in <unistd.h>, and each
    // entry contains a `key=value` formatted string.
    //
    // https://stackoverflow.com/q/3473692/
    //
    int n;
    for (n = 0; environ[n] != NULL; ++n) {
        //
        // Note: it's safe to search for just a `=` byte, since the high bit
        // isn't set...and even if the key contains UTF-8 characters, there
        // won't be any occurrences of such bytes in multi-byte-characters.
        //
        const char *key_equals_val = environ[n];
        const char *eq_pos = strchr(key_equals_val, '=');

        REBLEN size = strlen(key_equals_val);

        int key_size = eq_pos - key_equals_val;
        Value* key = rebSizedText(key_equals_val, key_size);

        int val_size = size - (eq_pos - key_equals_val) - 1;
        Value* val = rebSizedText(eq_pos + 1, val_size);

        rebElide("append", map, "spread [", rebR(key), rebR(val), "]");
    }
  #endif

    return map;
}
