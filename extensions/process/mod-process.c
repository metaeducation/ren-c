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

#ifdef TO_WINDOWS
    #define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
    #include <windows.h>

    #include <process.h>
    #include <shlobj.h>

    #ifdef IS_ERROR
        #undef IS_ERROR //winerror.h defines, Rebol has a different meaning
    #endif
#else
    #if !defined(__cplusplus) && defined(TO_LINUX)
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
    #if defined(TO_OSX) || defined(TO_OPENBSD_X64)
        extern char **environ;
    #endif

    #include <errno.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <signal.h>
    #include <sys/stat.h>
    #include <sys/wait.h>
    #if !defined(WIFCONTINUED) && defined(TO_ANDROID)
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
//  {Run another program by spawning a new process}
//
//      command "OS-local command line, block with arguments, executable file"
//          [text! block! file!]
//      /wait "Wait for command to terminate before returning"
//      /console "Runs command with I/O redirected to console"
//      /shell "Forces command to be run from shell"
//      /info "Returns process information object"
//      /input "Redirects stdin (false=/dev/null, true=inherit)"
//          [text! binary! file! logic!]
//      /output "Redirects stdout (false=/dev/null, true=inherit)"
//          [text! binary! file! logic!]
//      /error "Redirects stderr (false=/dev/null, true=inherit)"
//          [text! binary! file! logic!]
//  ]
//
REBNATIVE(call_internal_p)
//
// !!! Parameter usage may require WAIT mode even if not explicitly requested.
// /WAIT should be default, with /ASYNC (or otherwise) as exception!
{
    return Call_Core(frame_);
}


//
//  export get-os-browsers: native [
//
//  "Ask the OS or registry what command(s) to use for starting a browser."
//
//      return: [block!]
//          {Block of strings, where %1 should be substituted with the string}
//  ]
//
REBNATIVE(get_os_browsers)
//
// !!! Using the %1 convention is not necessarily ideal vs. having some kind
// of more "structural" result, it was just easy because it's how the string
// comes back from the Windows registry.  Review.
{
    PROCESS_INCLUDE_PARAMS_OF_GET_OS_BROWSERS;

    REBVAL *list = rebValue("copy []", rebEND);

  #if defined(TO_WINDOWS)

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

    rebElide("append", list, rebR(rebLengthedTextWide(buffer, len)), rebEND);

    rebFree(buffer);

  #elif defined(TO_LINUX)

    // Caller should try xdg-open first, then try x-www-browser otherwise
    //
    rebElide(
        "append", list, "[",
            rebT("xdg-open %1"),
            rebT("x-www-browser %1"),
        "]", rebEND
    );

  #else // Just try /usr/bin/open on POSIX, OS X, Haiku, etc.

    rebElide("append", list, rebT("/usr/bin/open %1"), rebEND);

  #endif

    return list;
}


//
//  export sleep: native [
//
//  "Use system sleep to wait a certain amount of time (doesn't use PORT!s)."
//
//      return: [void!]
//      duration [integer! decimal! time!]
//          {Length to sleep (integer and decimal are measuring seconds)}
//
//  ]
//
REBNATIVE(sleep)
//
// !!! This is a temporary workaround for the fact that it is not currently
// possible to do a WAIT on a time from within an AWAKE handler.  A proper
// solution would presumably solve that problem, so two different functions
// would not be needed.
//
// This function was needed by @GrahamChiu, and putting it in the CALL module
// isn't necessarily ideal, but it's better than making the core dependent
// on Sleep() vs. usleep()...and all the relevant includes have been
// established here.
{
    PROCESS_INCLUDE_PARAMS_OF_SLEEP;

    REBLEN msec = Milliseconds_From_Value(ARG(duration));

  #ifdef TO_WINDOWS
    Sleep(msec);
  #else
    usleep(msec * 1000);
  #endif

    return Init_Void(D_OUT);
}


#if defined(TO_LINUX) || defined(TO_ANDROID) || defined(TO_POSIX) || defined(TO_OSX)
static void kill_process(pid_t pid, int signal);
#endif

//
//  terminate: native [
//
//  "Terminate a process (not current one)"
//
//      return: [<opt>]
//      pid [integer!]
//          {The process ID}
//  ]
//
REBNATIVE(terminate)
{
    PROCESS_INCLUDE_PARAMS_OF_TERMINATE;

  #ifdef TO_WINDOWS

    if (GetCurrentProcessId() == cast(DWORD, VAL_INT32(ARG(pid))))
        fail ("Use QUIT or EXIT-REBOL to terminate current process, instead");

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

  #elif defined(TO_LINUX) || defined(TO_ANDROID) || defined(TO_POSIX) || defined(TO_OSX)

    if (getpid() == VAL_INT32(ARG(pid))) {
        // signal is not as reliable for this purpose
        // it's caught in main.c as to stop the evaluation
        fail ("Use QUIT or EXIT-REBOL to terminate current process, instead");
    }
    kill_process(VAL_INT32(ARG(pid)), SIGTERM);
    return nullptr;

  #else

    UNUSED(frame_);
    fail ("terminate is not implemented for this platform");

  #endif
}


//
//  export get-env: native [
//
//  {Returns the value of an OS environment variable (for current process).}
//
//      return: "String the variable was set to, or null if not set"
//          [<opt> text!]
//      variable "Name of variable to get (case-insensitive in Windows)"
//          [<blank> text! word!]
//  ]
//
REBNATIVE(get_env)
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
    PROCESS_INCLUDE_PARAMS_OF_GET_ENV;

    REBVAL *variable = ARG(variable);

    Check_Security_Placeholder(Canon(SYM_ENVR), SYM_READ, variable);

    REBCTX *error = NULL;

  #ifdef TO_WINDOWS
    // Note: The Windows variant of this API is NOT case-sensitive

    WCHAR *key = rebSpellWideQ(variable, rebEND);

    DWORD val_len_plus_one = GetEnvironmentVariable(key, NULL, 0);
    if (val_len_plus_one == 0) { // some failure...
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND)
            Init_Nulled(D_OUT);
        else
            error = Error_User("Unknown error when requesting variable size");
    }
    else {
        WCHAR *val = rebAllocN(WCHAR, val_len_plus_one);
        DWORD result = GetEnvironmentVariable(key, val, val_len_plus_one);
        if (result == 0)
            error = Error_User("Unknown error fetching variable to buffer");
        else {
            REBVAL *temp = rebLengthedTextWide(val, val_len_plus_one - 1);
            Move_Value(D_OUT, temp);
            rebRelease(temp);
        }
        rebFree(val);
    }

    rebFree(key);
  #else
    // Note: The Posix variant of this API is case-sensitive

    char *key = rebSpellQ(variable, rebEND);

    const char* val = getenv(key);
    if (val == NULL) // key not present in environment
        Init_Nulled(D_OUT);
    else {
        size_t size = strsize(val);

        /* assert(size != 0); */ // True?  Should it return BLANK!?

        Init_Text(D_OUT, Make_Sized_String_UTF8(val, size));
    }

    rebFree(key);
  #endif

    // Error is broken out like this so that the proper freeing can be done
    // without leaking temporary buffers.
    //
    if (error != NULL)
        fail (error);

    return D_OUT;
}


//
//  export set-env: native [
//
//  {Sets value of operating system environment variable for current process.}
//
//      return: "Returns same value passed in"
//          [<opt> text!]
//      variable [<blank> text! word!]
//          "Variable to set (case-insensitive in Windows)"
//      value [<opt> text!]
//          "Value to set the variable to, or NULL to unset it"
//  ]
//
REBNATIVE(set_env)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_ENV;

    REBVAL *variable = ARG(variable);
    REBVAL *value = ARG(value);

    Check_Security_Placeholder(Canon(SYM_ENVR), SYM_WRITE, variable);

  #ifdef TO_WINDOWS
    WCHAR *key_wide = rebSpellWide(variable, rebEND);
    WCHAR *opt_val_wide = rebSpellWide("ensure [<opt> text!]", value, rebEND);

    if (not SetEnvironmentVariable(key_wide, opt_val_wide)) // null unsets
        fail ("environment variable couldn't be modified");

    rebFree(opt_val_wide);
    rebFree(key_wide);
  #else
    char *key_utf8 = rebSpell(variable, rebEND);

    if (IS_NULLED(value)) {
      #ifdef unsetenv
        if (unsetenv(key_utf8) == -1)
            fail ("unsetenv() couldn't unset environment variable");
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
            fail ("putenv() couldn't unset environment variable");
      #endif
    }
    else {
      #ifdef setenv
        char *val_utf8 = rebSpell(value, rebEND);

        if (setenv(key_utf8, val_utf8, 1) == -1) // the 1 means "overwrite"
            fail ("setenv() coudln't set environment variable");

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
            "unspaced [", variable, "{=}", value, "]", rebEND
        );

        char *duplicate = strdup(key_equals_val_utf8);

        if (putenv(duplicate) == -1)  // leak!  (why mutable?  :-/)
            fail ("putenv() couldn't set environment variable");

        rebFree(key_equals_val_utf8);
      #endif
    }

    rebFree(key_utf8);
  #endif

    RETURN (ARG(value));
}


//
//  export list-env: native [
//
//  {Returns a map of OS environment variables (for current process).}
//
//      ; No arguments
//  ]
//
REBNATIVE(list_env)
{
    PROCESS_INCLUDE_PARAMS_OF_LIST_ENV;

    REBVAL *map = rebValue("make map! []", rebEND);

  #ifdef TO_WINDOWS
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
        REBVAL *key = rebLengthedTextWide(key_equals_val, key_len);

        int val_len = len - (eq_pos - key_equals_val) - 1;
        REBVAL *val = rebLengthedTextWide(eq_pos + 1, val_len);

        rebElide(
            "append", map, "[", rebR(key), rebR(val), "]",
        rebEND);

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
        REBVAL *key = rebSizedText(key_equals_val, key_size);

        int val_size = size - (eq_pos - key_equals_val) - 1;
        REBVAL *val = rebSizedText(eq_pos + 1, val_size);

        rebElide(
            "append", map, "[", rebR(key), rebR(val), "]",
        rebEND);
    }
  #endif

    return map;
}


#if defined(TO_LINUX) || defined(TO_ANDROID) || defined(TO_POSIX) || defined(TO_OSX)

//
//  get-pid: native [
//
//  "Get ID of the process"
//
//      return: [integer!]
//
//  ]
//  platforms: [linux android posix osx]
//
REBNATIVE(get_pid)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_PID;

    return rebInteger(getpid());
}



//
//  get-uid: native [
//
//  "Get real user ID of the process"
//
//      return: [integer!]
//
//  ]
//  platforms: [linux android posix osx]
//
REBNATIVE(get_uid)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_UID;

    return rebInteger(getuid());
}


//
//  get-euid: native [
//
//  "Get effective user ID of the process"
//
//      return: [integer!]
//
//  ]
//  platforms: [linux android posix osx]
//
REBNATIVE(get_euid)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_EUID;

    return rebInteger(geteuid());
}


//
//  get-gid: native [
//
//  "Get real group ID of the process"
//
//      return: [integer!]
//  ]
//  platforms: [linux android posix osx]
//
REBNATIVE(get_gid)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_UID;

    return rebInteger(getgid());
}


//
//  get-egid: native [
//
//  "Get effective group ID of the process"
//
//      return: [integer!]
//
//  ]
//  platforms: [linux android posix osx]
//
REBNATIVE(get_egid)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_EUID;

    return rebInteger(getegid());
}


//
//  set-uid: native [
//
//  {Set real user ID of the process}
//
//      return: "Same ID as input"
//          [integer!]
//      uid {The effective user ID}
//          [integer!]
//  ]
//  platforms: [linux android posix osx]
//
REBNATIVE(set_uid)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_UID;

    if (setuid(VAL_INT32(ARG(uid))) >= 0)
        RETURN (ARG(uid));

    switch (errno) {
      case EINVAL:
        fail (PAR(uid));

      case EPERM:
        Fail_Permission_Denied();

      default:
        rebFail_OS(errno);
    }
}


//
//  set-euid: native [
//
//  {Get effective user ID of the process}
//
//      return: "Same ID as input"
//          [<opt>]
//      euid "The effective user ID"
//          [integer!]
//  ]
//  platforms: [linux android posix osx]
//
REBNATIVE(set_euid)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_EUID;

    if (seteuid(VAL_INT32(ARG(euid))) >= 0)
        RETURN (ARG(euid));

    switch (errno) {
      case EINVAL:
        fail (PAR(euid));

      case EPERM:
        Fail_Permission_Denied();

      default:
        rebFail_OS(errno);
    }
}


//
//  set-gid: native [
//
//  {Set real group ID of the process}
//
//      return: "Same ID as input"
//          [<opt>]
//      gid "The effective group ID"
//          [integer!]
//  ]
//  platforms: [linux android posix osx]
//
REBNATIVE(set_gid)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_GID;

    if (setgid(VAL_INT32(ARG(gid))) >= 0)
        RETURN (ARG(gid));

    switch (errno) {
      case EINVAL:
        fail (PAR(gid));

      case EPERM:
        Fail_Permission_Denied();

      default:
        rebFail_OS(errno);
    }
}


//
//  set-egid: native [
//
//  "Get effective group ID of the process"
//
//      return: "Same ID as input"
//          [integer!]
//      egid "The effective group ID"
//          [integer!]
//  ]
//  platforms: [linux android posix osx]
//
REBNATIVE(set_egid)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_EGID;

    if (setegid(VAL_INT32(ARG(egid))) >= 0)
        RETURN (ARG(egid));

    switch (errno) {
      case EINVAL:
        fail (PAR(egid));

      case EPERM:
        Fail_Permission_Denied();

      default:
        rebFail_OS(errno);
    }
}


static void kill_process(pid_t pid, int signal)
{
    if (kill(pid, signal) >= 0)
        return; // success

    switch (errno) {
      case EINVAL:
        rebJumps(
            "fail [{Invalid signal number:}", rebI(signal), "]", rebEND
        );

      case EPERM:
        Fail_Permission_Denied();

      case ESRCH:
        Fail_No_Process(rebInteger(pid)); // failure releases integer handle

      default:
        rebFail_OS(errno);
    }
}


//
//  send-signal: native [
//
//  "Send signal to a process"
//
//      return: [void!]  ; !!! might this return pid or signal (?)
//      pid [integer!]
//          {The process ID}
//      signal [integer!]
//          {The signal number}
//  ]
//  platforms: [linux android posix osx]
//
REBNATIVE(send_signal)
{
    PROCESS_INCLUDE_PARAMS_OF_SEND_SIGNAL;

    pid_t pid = rebUnboxInteger(ARG(pid), rebEND);
    int signal = rebUnboxInteger(ARG(signal), rebEND);

    // !!! Is called `send-signal` but only seems to call kill (?)
    //
    kill_process(pid, signal);

    return Init_Void(D_OUT);
}

#endif // defined(TO_LINUX) || defined(TO_ANDROID) || defined(TO_POSIX) || defined(TO_OSX)
