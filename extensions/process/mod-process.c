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

#ifdef TO_WINDOWS
    #include <windows.h>
    #include <process.h>
    #include <shlobj.h>

    #undef OUT  // %minwindef.h defines this, we have a better use for it
    #undef VOID  // %winnt.h defines this, we have a better use for it
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
    #if defined(TO_OSX)
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


// !!! %mod-process.c is now the last file that uses this cross platform OS
// character definition.  Excise as soon as possible.
//
#ifdef TO_WINDOWS
    #define OSCHR WCHAR
#else
    #define OSCHR char
#endif


//
//  rebValSpellingAllocOS: C
//
// This is used to pass a REBOL value string to an OS API.
// On Windows, the result is a wide-char pointer, but on Linux, its UTF-8.
// The returned pointer must be freed with OS_FREE.
//
OSCHR *rebValSpellingAllocOS(const Value* any_string)
{
  #ifdef OS_WIDE_CHAR
    return rebSpellW(any_string);
  #else
    return rebSpell(any_string);
  #endif
}


//
//  Append_OS_Str: C
//
// The data which came back from the piping interface may be UTF-8 on Linux,
// or WCHAR on windows.  Yet we want to append that data to an existing
// Rebol string, whose size may vary.
//
// !!! Note: With UTF-8 Everywhere as the native Rebol string format, it
// *might* be more efficient to try using that string's buffer...however
// there can be issues of permanent wasted space if the buffer is made too
// large and not shrunk.
//
void Append_OS_Str(Value* dest, const void *src, REBINT len)
{
  #ifdef TO_WINDOWS
    Value* src_str = rebLengthedTextWide(cast(const REBWCHAR*, src), len);
  #else
    Value* src_str = rebSizedText(cast(const char*, src), len);
  #endif

    rebElide("append", dest, src_str);

    rebRelease(src_str);
}


// !!! The original implementation of CALL from Atronix had to communicate
// between the CALL native (defined in the core) and the host routine
// OS_Create_Process, which was not designed to operate on Rebol types.
// Hence if the user was passing in a BINARY! to which the data for the
// standard out or standard error was to be saved, it was produced in full
// in a buffer and returned, then appended.  This wastes space when compared
// to just appending to the string or binary itself.  With CALL rethought
// as an extension with access to the internal API, this could be changed...
// though for the moment, a malloc()'d buffer is expanded independently by
// BUF_SIZE_CHUNK and returned to CALL.
//
#define BUF_SIZE_CHUNK 4096


ATTRIBUTE_NO_RETURN
INLINE void Fail_Permission_Denied(void) {
    rebJumps("fail {The process does not have enough permission}");
}

ATTRIBUTE_NO_RETURN
INLINE void Fail_No_Process(const Value* arg) {
    rebJumps("fail [{The target process (group) does not exist:}",
        arg, "]");
}


#ifdef TO_WINDOWS

ATTRIBUTE_NO_RETURN
INLINE void Fail_Terminate_Failed(DWORD err) { // from GetLastError()
    rebJumps(
        "fail [{Terminate failed with error number:}", rebI(err), "]"
    );
}

//
//  OS_Create_Process: C
//
// Return -1 on error.
//
int OS_Create_Process(
    Level* level_, // stopgap: allows access to CALL's ARG() and Bool_ARG()
    const WCHAR *call,
    int argc,
    const WCHAR * argv[],
    bool flag_wait,
    uint64_t *pid,
    int *exit_code,
    char *input,
    uint32_t input_len,
    char **output,
    uint32_t *output_len,
    char **err,
    uint32_t *err_len
) {
    PROCESS_INCLUDE_PARAMS_OF_CALL_INTERNAL_P;

    UNUSED(ARG(COMMAND)); // turned into `call` and `argv/argc` by CALL
    UNUSED(Bool_ARG(WAIT)); // covered by flag_wait

    UNUSED(Bool_ARG(CONSOLE)); // actually not paid attention to

    if (call == nullptr)
        fail ("'argv[]'-style launching not implemented on Windows CALL");

  #ifdef GET_IS_NT_FLAG // !!! Why was this here?
    bool is_NT;
    OSVERSIONINFO info;
    GetVersionEx(&info);
    is_NT = info.dwPlatformId >= VER_PLATFORM_WIN32_NT;
  #endif

    UNUSED(argc);
    UNUSED(argv);

    REBINT result = -1;
    REBINT ret = 0;
    HANDLE hOutputRead = 0, hOutputWrite = 0;
    HANDLE hInputWrite = 0, hInputRead = 0;
    HANDLE hErrorWrite = 0, hErrorRead = 0;
    WCHAR *cmd = nullptr;
    char *oem_input = nullptr;

    UNUSED(Bool_ARG(INFO));

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;

    STARTUPINFO si;
    si.cb = sizeof(si);
    si.lpReserved = nullptr;
    si.lpDesktop = nullptr;
    si.lpTitle = nullptr;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.wShowWindow = SW_SHOWNORMAL;
    si.cbReserved2 = 0;
    si.lpReserved2 = nullptr;

    UNUSED(Bool_ARG(INPUT)); // implicitly covered by void ARG(IN)
    switch (VAL_TYPE(ARG(IN))) {
    case REB_TEXT:
    case REB_BINARY:
        if (!CreatePipe(&hInputRead, &hInputWrite, nullptr, 0)) {
            goto input_error;
        }

        // make child side handle inheritable
        if (!SetHandleInformation(
            hInputRead, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT
        )){
            goto input_error;
        }
        si.hStdInput = hInputRead;
        break;

    case REB_FILE: {
        WCHAR *local_wide = rebSpellW("file-to-local", ARG(IN));

        hInputRead = CreateFile(
            local_wide,
            GENERIC_READ, // desired mode
            0, // shared mode
            &sa, // security attributes
            OPEN_EXISTING, // creation disposition
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, // flags
            nullptr  // template
        );
        si.hStdInput = hInputRead;

        rebFree(local_wide);
        break; }

    case REB_BLANK:
        si.hStdInput = 0;
        break;

    case REB_MAX_NULLED:
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        break;

    default:
        panic (ARG(IN));
    }

    UNUSED(Bool_ARG(OUTPUT)); // implicitly covered by void ARG(OUT)
    switch (VAL_TYPE(ARG(OUT))) {
    case REB_TEXT:
    case REB_BINARY:
        if (!CreatePipe(&hOutputRead, &hOutputWrite, nullptr, 0)) {
            goto output_error;
        }

        // make child side handle inheritable
        //
        if (!SetHandleInformation(
            hOutputWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT
        )){
            goto output_error;
        }
        si.hStdOutput = hOutputWrite;
        break;

    case REB_FILE: {
        WCHAR *local_wide = rebSpellW("file-to-local", ARG(OUT));

        si.hStdOutput = CreateFile(
            local_wide,
            GENERIC_WRITE, // desired mode
            0, // shared mode
            &sa, // security attributes
            CREATE_NEW, // creation disposition
            FILE_ATTRIBUTE_NORMAL, // flag and attributes
            nullptr  // template
        );

        if (
            si.hStdOutput == INVALID_HANDLE_VALUE
            && GetLastError() == ERROR_FILE_EXISTS
        ){
            si.hStdOutput = CreateFile(
                local_wide,
                GENERIC_WRITE, // desired mode
                0, // shared mode
                &sa, // security attributes
                OPEN_EXISTING, // creation disposition
                FILE_ATTRIBUTE_NORMAL, // flag and attributes
                nullptr  // template
            );
        }

        rebFree(local_wide);
        break; }

    case REB_BLANK:
        si.hStdOutput = 0;
        break;

    case REB_MAX_NULLED:
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        break;

    default:
        panic (ARG(OUT));
    }

    UNUSED(Bool_ARG(ERROR)); // implicitly covered by void ARG(ERR)
    switch (VAL_TYPE(ARG(ERR))) {
    case REB_TEXT:
    case REB_BINARY:
        if (!CreatePipe(&hErrorRead, &hErrorWrite, nullptr, 0)) {
            goto error_error;
        }

        // make child side handle inheritable
        //
        if (!SetHandleInformation(
            hErrorWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT
        )){
            goto error_error;
        }
        si.hStdError = hErrorWrite;
        break;

    case REB_FILE: {
        WCHAR *local_wide = rebSpellW("file-to-local", ARG(OUT));

        si.hStdError = CreateFile(
            local_wide,
            GENERIC_WRITE, // desired mode
            0, // shared mode
            &sa, // security attributes
            CREATE_NEW, // creation disposition
            FILE_ATTRIBUTE_NORMAL, // flag and attributes
            nullptr  // template
        );

        if (
            si.hStdError == INVALID_HANDLE_VALUE
            && GetLastError() == ERROR_FILE_EXISTS
        ){
            si.hStdError = CreateFile(
                local_wide,
                GENERIC_WRITE, // desired mode
                0, // shared mode
                &sa, // security attributes
                OPEN_EXISTING, // creation disposition
                FILE_ATTRIBUTE_NORMAL, // flag and attributes
                nullptr  // template
            );
        }

        rebFree(local_wide);
        break; }

    case REB_BLANK:
        si.hStdError = 0;
        break;

    case REB_MAX_NULLED:
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        break;

    default:
        panic (ARG(ERR));
    }

    if (Bool_ARG(SHELL)) {
        // command to cmd.exe needs to be surrounded by quotes to preserve the inner quotes
        const WCHAR *sh = L"cmd.exe /C \"";

        REBLEN len = wcslen(sh) + wcslen(call)
            + 1 // terminal quote mark
            + 1; // NUL terminator

        cmd = cast(WCHAR*, malloc(sizeof(WCHAR) * len));
        cmd[0] = L'\0';
        wcscat(cmd, sh);
        wcscat(cmd, call);
        wcscat(cmd, L"\"");
    }
    else {
        // CreateProcess might write to this memory
        // Duplicate it to be safe

        cmd = _wcsdup(call); // uses malloc()
    }

    PROCESS_INFORMATION pi;
    result = CreateProcess(
        nullptr,  // executable name
        cmd, // command to execute
        nullptr, // process security attributes
        nullptr, // thread security attributes
        TRUE, // inherit handles, must be TRUE for I/O redirection
        NORMAL_PRIORITY_CLASS | CREATE_DEFAULT_ERROR_MODE, // creation flags
        nullptr, // environment
        nullptr, // current directory
        &si, // startup information
        &pi // process information
    );

    free(cmd);

    *pid = pi.dwProcessId;

    if (hInputRead != nullptr)
        CloseHandle(hInputRead);

    if (hOutputWrite != nullptr)
        CloseHandle(hOutputWrite);

    if (hErrorWrite != nullptr)
        CloseHandle(hErrorWrite);

    // Wait for termination:
    if (result != 0 && flag_wait) {
        HANDLE handles[3];
        int count = 0;
        DWORD output_size = 0;
        DWORD err_size = 0;

        if (hInputWrite != nullptr && input_len > 0) {
            if (Is_Text(ARG(IN))) {
                DWORD dest_len = 0;
                /* convert input encoding from UNICODE to OEM */
                // !!! Is cast to WCHAR here legal?
                dest_len = WideCharToMultiByte(
                    CP_OEMCP,
                    0,
                    cast(WCHAR*, input),
                    input_len,
                    oem_input,
                    dest_len,
                    nullptr,
                    nullptr
                );
                if (dest_len > 0) {
                    oem_input = cast(char*, malloc(dest_len));
                    if (oem_input != nullptr) {
                        WideCharToMultiByte(
                            CP_OEMCP,
                            0,
                            cast(WCHAR*, input),
                            input_len,
                            oem_input,
                            dest_len,
                            nullptr,
                            nullptr
                        );
                        input_len = dest_len;
                        input = oem_input;
                        handles[count ++] = hInputWrite;
                    }
                }
            } else {
                assert(Is_Binary(ARG(IN)));
                handles[count ++] = hInputWrite;
            }
        }
        if (hOutputRead != nullptr) {
            output_size = BUF_SIZE_CHUNK;
            *output_len = 0;

            *output = cast(char*, malloc(output_size));
            handles[count ++] = hOutputRead;
        }
        if (hErrorRead != nullptr) {
            err_size = BUF_SIZE_CHUNK;
            *err_len = 0;

            *err = cast(char*, malloc(err_size));
            handles[count++] = hErrorRead;
        }

        while (count > 0) {
            DWORD wait_result = WaitForMultipleObjects(
                count, handles, FALSE, INFINITE
            );

            // If we test wait_result >= WAIT_OBJECT_0 it will tell us "always
            // true" with -Wtype-limits, since WAIT_OBJECT_0 is 0.  Take that
            // comparison out but add assert in case you're on some abstracted
            // Windows and it isn't 0 for that implementation.
            //
            assert(WAIT_OBJECT_0 == 0);
            if (wait_result < WAIT_OBJECT_0 + count) {
                int i = wait_result - WAIT_OBJECT_0;
                DWORD input_pos = 0;
                DWORD n = 0;

                if (handles[i] == hInputWrite) {
                    if (!WriteFile(
                        hInputWrite,
                        cast(char*, input) + input_pos,
                        input_len - input_pos,
                        &n,
                        nullptr
                    )){
                        if (i < count - 1) {
                            memmove(
                                &handles[i],
                                &handles[i + 1],
                                (count - i - 1) * sizeof(HANDLE)
                            );
                        }
                        count--;
                    }
                    else {
                        input_pos += n;
                        if (input_pos >= input_len) {
                            /* done with input */
                            CloseHandle(hInputWrite);
                            hInputWrite = nullptr;
                            free(oem_input);
                            oem_input = nullptr;
                            if (i < count - 1) {
                                memmove(
                                    &handles[i],
                                    &handles[i + 1],
                                    (count - i - 1) * sizeof(HANDLE)
                                );
                            }
                            count--;
                        }
                    }
                }
                else if (handles[i] == hOutputRead) {
                    if (!ReadFile(
                        hOutputRead,
                        *cast(char**, output) + *output_len,
                        output_size - *output_len,
                        &n,
                        nullptr
                    )){
                        if (i < count - 1) {
                            memmove(
                                &handles[i],
                                &handles[i + 1],
                                (count - i - 1) * sizeof(HANDLE)
                            );
                        }
                        count--;
                    }
                    else {
                        *output_len += n;
                        if (*output_len >= output_size) {
                            output_size += BUF_SIZE_CHUNK;
                            *output = cast(char*, realloc(*output, output_size));
                            if (*output == nullptr) goto kill;
                        }
                    }
                }
                else if (handles[i] == hErrorRead) {
                    if (!ReadFile(
                        hErrorRead,
                        *cast(char**, err) + *err_len,
                        err_size - *err_len,
                        &n,
                        nullptr
                    )){
                        if (i < count - 1) {
                            memmove(
                                &handles[i],
                                &handles[i + 1],
                                (count - i - 1) * sizeof(HANDLE)
                            );
                        }
                        count--;
                    }
                    else {
                        *err_len += n;
                        if (*err_len >= err_size) {
                            err_size += BUF_SIZE_CHUNK;
                            *err = cast(char*, realloc(*err, err_size));
                            if (*err == nullptr) goto kill;
                        }
                    }
                }
                else {
                    //printf("Error READ");
                    if (!ret) ret = GetLastError();
                    goto kill;
                }
            }
            else if (wait_result == WAIT_FAILED) { /* */
                //printf("Wait Failed\n");
                if (!ret) ret = GetLastError();
                goto kill;
            }
            else {
                //printf("Wait returns unexpected result: %d\n", wait_result);
                if (!ret) ret = GetLastError();
                goto kill;
            }
        }

        WaitForSingleObject(pi.hProcess, INFINITE); // check result??

        DWORD temp;
        GetExitCodeProcess(pi.hProcess, &temp);
        *exit_code = temp;

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);

        if (Is_Text(ARG(OUT)) and *output and *output_len > 0) {
            /* convert to wide char string */
            int dest_len = 0;
            WCHAR *dest = nullptr;
            dest_len = MultiByteToWideChar(
                CP_OEMCP, 0, *output, *output_len, dest, 0
            );
            if (dest_len <= 0) {
                free(*output);
                *output = nullptr;
                *output_len = 0;
            }
            dest = cast(WCHAR*, malloc(*output_len * sizeof(WCHAR)));
            if (dest == nullptr)
                goto cleanup;
            MultiByteToWideChar(
                CP_OEMCP, 0, *output, *output_len, dest, dest_len
            );
            free(*output);
            *output = cast(char*, dest);
            *output_len = dest_len;
        }

        if (Is_Text(ARG(ERR)) && *err != nullptr && *err_len > 0) {
            /* convert to wide char string */
            int dest_len = 0;
            WCHAR *dest = nullptr;
            dest_len = MultiByteToWideChar(
                CP_OEMCP, 0, *err, *err_len, dest, 0
            );
            if (dest_len <= 0) {
                free(*err);
                *err = nullptr;
                *err_len = 0;
            }
            dest = cast(WCHAR*, malloc(*err_len * sizeof(WCHAR)));
            if (dest == nullptr) goto cleanup;
            MultiByteToWideChar(CP_OEMCP, 0, *err, *err_len, dest, dest_len);
            free(*err);
            *err = cast(char*, dest);
            *err_len = dest_len;
        }
    } else if (result) {
        //
        // No wait, close handles to avoid leaks
        //
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    else {
        // CreateProcess failed
        ret = GetLastError();
    }

    goto cleanup;

kill:
    if (TerminateProcess(pi.hProcess, 0)) {
        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD temp;
        GetExitCodeProcess(pi.hProcess, &temp);
        *exit_code = temp;
    }
    else if (ret == 0) {
        ret = GetLastError();
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

cleanup:
    if (oem_input != nullptr) {
        free(oem_input);
    }

    if (output and *output and *output_len == 0) {
        free(*output);
    }

    if (err and *err != nullptr and *err_len == 0) {
        free(*err);
    }

    if (hInputWrite != nullptr)
        CloseHandle(hInputWrite);

    if (hOutputRead != nullptr)
        CloseHandle(hOutputRead);

    if (hErrorRead != nullptr)
        CloseHandle(hErrorRead);

    if (Is_File(ARG(ERR))) {
        CloseHandle(si.hStdError);
    }

error_error:
    if (Is_File(ARG(OUT))) {
        CloseHandle(si.hStdOutput);
    }

output_error:
    if (Is_File(ARG(IN))) {
        CloseHandle(si.hStdInput);
    }

input_error:
    return ret;  // meaning depends on flags
}

#else // !defined(TO_WINDOWS), so POSIX, LINUX, OS X, etc.

INLINE bool Open_Pipe_Fails(int pipefd[2]) {
  #ifdef USE_PIPE2_NOT_PIPE
    //
    // NOTE: pipe() is POSIX, but pipe2() is Linux-specific.  With pipe() it
    // takes an additional call to fcntl() to request non-blocking behavior,
    // so it's a small amount more work.  However, there are other flags which
    // if aren't passed atomically at the moment of opening allow for a race
    // condition in threading if split, e.g. FD_CLOEXEC.
    //
    // (If you don't have FD_CLOEXEC set on the file descriptor, then all
    // instances of CALL will act as a /WAIT.)
    //
    // At time of writing, this is mostly academic...but the code needed to be
    // patched to work with pipe() since some older libcs do not have pipe2().
    // So the ability to target both are kept around, saving the pipe2() call
    // for later Linuxes known to have it (and O_CLOEXEC).
    //
    if (pipe2(pipefd, O_CLOEXEC))
        return true;
  #else
    if (pipe(pipefd) < 0)
        return true;
    int direction; // READ=0, WRITE=1
    for (direction = 0; direction < 2; ++direction) {
        int oldflags = fcntl(pipefd[direction], F_GETFD);
        if (oldflags < 0)
            return true;
        if (fcntl(pipefd[direction], F_SETFD, oldflags | FD_CLOEXEC) < 0)
            return true;
    }
  #endif
    return false;
}

INLINE bool Set_Nonblocking_Fails(int fd) {
    int oldflags;
    oldflags = fcntl(fd, F_GETFL);
    if (oldflags < 0)
        return true;
    if (fcntl(fd, F_SETFL, oldflags | O_NONBLOCK) < 0)
        return true;

    return false;
}


//
//  OS_Create_Process: C
//
// flags:
//     1: wait, is implied when I/O redirection is enabled
//     2: console
//     4: shell
//     8: info
//     16: show
//
// Return -1 on error, otherwise the process return code.
//
// POSIX previous simple version was just 'return system(call);'
// This uses 'execvp' which is "POSIX.1 conforming, UNIX compatible"
//
int OS_Create_Process(
    Level* level_, // stopgap: allows access to CALL's ARG() and Bool_ARG()
    const char *call,
    int argc,
    const char* argv[],
    bool flag_wait, // distinct from Bool_ARG(WAIT)
    uint64_t *pid,
    int *exit_code,
    char *input,
    uint32_t input_len,
    char **output,
    uint32_t *output_len,
    char **err,
    uint32_t *err_len
){
    PROCESS_INCLUDE_PARAMS_OF_CALL_INTERNAL_P;
    UNUSED(Bool_ARG(RELAX));  // handled by CALL_INTERNAL*

    UNUSED(ARG(COMMAND)); // translated into call and argc/argv
    UNUSED(Bool_ARG(WAIT)); // flag_wait controls this
    UNUSED(Bool_ARG(INPUT));
    UNUSED(Bool_ARG(OUTPUT));
    UNUSED(Bool_ARG(ERROR));

    UNUSED(Bool_ARG(CONSOLE)); // actually not paid attention to

    UNUSED(call);

    int status = 0;
    int ret = 0;
    int non_errno_ret = 0; // "ret" above should be valid errno

    // An "info" pipe is used to send back an error code from the child
    // process back to the parent if there is a problem.  It only writes
    // an integer's worth of data in that case, but it may need a bigger
    // buffer if more interesting data needs to pass between them.
    //
    char *info = nullptr;
    off_t info_size = 0;
    uint32_t info_len = 0;

    // suppress unused warnings but keep flags for future use
    UNUSED(Bool_ARG(INFO));
    UNUSED(Bool_ARG(CONSOLE));

    const unsigned int R = 0;
    const unsigned int W = 1;
    int stdin_pipe[] = {-1, -1};
    int stdout_pipe[] = {-1, -1};
    int stderr_pipe[] = {-1, -1};
    int info_pipe[] = {-1, -1};

    if (Is_Text(ARG(IN)) or Is_Binary(ARG(IN))) {
        if (Open_Pipe_Fails(stdin_pipe))
            goto stdin_pipe_err;
    }

    if (Is_Text(ARG(OUT)) or Is_Binary(ARG(OUT))) {
        if (Open_Pipe_Fails(stdout_pipe))
            goto stdout_pipe_err;
    }

    if (Is_Text(ARG(ERR)) or Is_Binary(ARG(ERR))) {
        if (Open_Pipe_Fails(stderr_pipe))
            goto stdout_pipe_err;
    }

    if (Open_Pipe_Fails(info_pipe))
        goto info_pipe_err;

    pid_t fpid; // gotos would cross initialization
    fpid = fork();
    if (fpid == 0) {
        //
        // This is the child branch of the fork.  In GDB if you want to debug
        // the child you need to use `set follow-fork-mode child`:
        //
        // http://stackoverflow.com/questions/15126925/

        if (Is_Text(ARG(IN)) or Is_Binary(ARG(IN))) {
            close(stdin_pipe[W]);
            if (dup2(stdin_pipe[R], STDIN_FILENO) < 0)
                goto child_error;
            close(stdin_pipe[R]);
        }
        else if (Is_File(ARG(IN))) {
            char *local_utf8 = rebSpell("file-to-local", ARG(IN));

            int fd = open(local_utf8, O_RDONLY);

            rebFree(local_utf8);

            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDIN_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else if (Is_Blank(ARG(IN))) {
            int fd = open("/dev/null", O_RDONLY);
            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDIN_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else {
            assert(Is_Nulled(ARG(IN)));
            // inherit stdin from the parent
        }

        if (Is_Text(ARG(OUT)) or Is_Binary(ARG(OUT))) {
            close(stdout_pipe[R]);
            if (dup2(stdout_pipe[W], STDOUT_FILENO) < 0)
                goto child_error;
            close(stdout_pipe[W]);
        }
        else if (Is_File(ARG(OUT))) {
            char *local_utf8 = rebSpell("file-to-local", ARG(OUT));

            int fd = open(local_utf8, O_CREAT | O_WRONLY, 0666);

            rebFree(local_utf8);

            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDOUT_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else if (Is_Blank(ARG(OUT))) {
            int fd = open("/dev/null", O_WRONLY);
            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDOUT_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else {
            assert(Is_Nulled(ARG(OUT)));
            // inherit stdout from the parent
        }

        if (Is_Text(ARG(ERR)) or Is_Binary(ARG(ERR))) {
            close(stderr_pipe[R]);
            if (dup2(stderr_pipe[W], STDERR_FILENO) < 0)
                goto child_error;
            close(stderr_pipe[W]);
        }
        else if (Is_File(ARG(ERR))) {
            char *local_utf8 = rebSpell("file-to-local", ARG(ERR));

            int fd = open(local_utf8, O_CREAT | O_WRONLY, 0666);

            rebFree(local_utf8);

            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDERR_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else if (Is_Blank(ARG(ERR))) {
            int fd = open("/dev/null", O_WRONLY);
            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDERR_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else {
            assert(Is_Nulled(ARG(ERR)));
            // inherit stderr from the parent
        }

        close(info_pipe[R]);

        /* printf("flag_shell in child: %hhu\n", flag_shell); */

        // We want to be able to compile with most all warnings as errors, and
        // we'd like to use -Wcast-qual (in builds where it is possible--it
        // is not possible in plain C builds).  We must tunnel under the cast.
        //
        char * const *argv_hack;

        if (Bool_ARG(SHELL)) {
            const char *sh = getenv("SHELL");

            if (sh == nullptr) { // shell does not exist
                int err = 2;
                if (write(info_pipe[W], &err, sizeof(err)) == -1) {
                    //
                    // Nothing we can do, but need to stop compiler warning
                    // (cast to void is insufficient for warn_unused_result)
                }
                exit(EXIT_FAILURE);
            }

            const char ** argv_new = cast(
                const char**,
                malloc((argc + 3) * sizeof(argv[0])
            ));
            argv_new[0] = sh;
            argv_new[1] = "-c";
            memcpy(&argv_new[2], argv, argc * sizeof(argv[0]));
            argv_new[argc + 2] = nullptr;

            memcpy(&argv_hack, &argv_new, sizeof(argv_hack));
            execvp(sh, argv_hack);
        }
        else {
            memcpy(&argv_hack, &argv, sizeof(argv_hack));
            execvp(argv[0], argv_hack);
        }

        // Note: execvp() will take over the process and not return, unless
        // there was a problem in the execution.  So you shouldn't be able
        // to get here *unless* there was an error, which will be in errno.

child_error: ;
        //
        // The original implementation of this code would write errno to the
        // info pipe.  However, errno may be volatile (and it is on Android).
        // write() does not accept volatile pointers, so copy it to a
        // temporary value first.
        //
        int nonvolatile_errno = errno;

        if (write(info_pipe[W], &nonvolatile_errno, sizeof(int)) == -1) {
            //
            // Nothing we can do, but need to stop compiler warning
            // (cast to void is insufficient for warn_unused_result)
            //
            assert(false);
        }
        exit(EXIT_FAILURE); /* get here only when exec fails */
    }
    else if (fpid > 0) {
        //
        // This is the parent branch, so it may (or may not) wait on the
        // child fork branch, based on /WAIT.  Even if you are not using
        // /WAIT, it will use the info pipe to make sure the process did
        // actually start.
        //
        nfds_t nfds = 0;
        struct pollfd pfds[4];
        unsigned int i;
        ssize_t nbytes;
        off_t input_size = 0;
        off_t output_size = 0;
        off_t err_size = 0;
        int valid_nfds;

        // Only put the input pipe in the consideration if we can write to
        // it and we have data to send to it.

        if ((stdin_pipe[W] > 0) && (input_size = strlen(input)) > 0) {
            /* printf("stdin_pipe[W]: %d\n", stdin_pipe[W]); */
            if (Set_Nonblocking_Fails(stdin_pipe[W]))
                goto kill;

            // the passed in input_len is in characters, not in bytes
            //
            input_len = 0;

            pfds[nfds].fd = stdin_pipe[W];
            pfds[nfds].events = POLLOUT;
            nfds++;

            close(stdin_pipe[R]);
            stdin_pipe[R] = -1;
        }
        if (stdout_pipe[R] > 0) {
            /* printf("stdout_pipe[R]: %d\n", stdout_pipe[R]); */
            if (Set_Nonblocking_Fails(stdout_pipe[R]))
                goto kill;

            output_size = BUF_SIZE_CHUNK;

            *output = cast(char*, malloc(output_size));
            *output_len = 0;

            pfds[nfds].fd = stdout_pipe[R];
            pfds[nfds].events = POLLIN;
            nfds++;

            close(stdout_pipe[W]);
            stdout_pipe[W] = -1;
        }
        if (stderr_pipe[R] > 0) {
            /* printf("stderr_pipe[R]: %d\n", stderr_pipe[R]); */
            if (Set_Nonblocking_Fails(stderr_pipe[R]))
                goto kill;

            err_size = BUF_SIZE_CHUNK;

            *err = cast(char*, malloc(err_size));
            *err_len = 0;

            pfds[nfds].fd = stderr_pipe[R];
            pfds[nfds].events = POLLIN;
            nfds++;

            close(stderr_pipe[W]);
            stderr_pipe[W] = -1;
        }

        if (info_pipe[R] > 0) {
            if (Set_Nonblocking_Fails(info_pipe[R]))
                goto kill;

            pfds[nfds].fd = info_pipe[R];
            pfds[nfds].events = POLLIN;
            nfds++;

            info_size = 4;

            info = cast(char*, malloc(info_size));

            close(info_pipe[W]);
            info_pipe[W] = -1;
        }

        valid_nfds = nfds;
        while (valid_nfds > 0) {
            pid_t xpid = waitpid(fpid, &status, WNOHANG);
            if (xpid == -1) {
                ret = errno;
                goto error;
            }

            if (xpid == fpid) {
                //
                // try one more time to read any remainding output/err
                //
                if (stdout_pipe[R] > 0) {
                    nbytes = read(
                        stdout_pipe[R],
                        *output + *output_len,
                        output_size - *output_len
                    );

                    if (nbytes > 0) {
                        *output_len += nbytes;
                    }
                }

                if (stderr_pipe[R] > 0) {
                    nbytes = read(
                        stderr_pipe[R],
                        *err + *err_len,
                        err_size - *err_len
                    );
                    if (nbytes > 0) {
                        *err_len += nbytes;
                    }
                }

                if (info_pipe[R] > 0) {
                    nbytes = read(
                        info_pipe[R],
                        info + info_len,
                        info_size - info_len
                    );
                    if (nbytes > 0) {
                        info_len += nbytes;
                    }
                }

                if (WIFSTOPPED(status)) {
                    // TODO: Review, What's the expected behavior if the child process is stopped?
                    continue;
                } else if  (WIFCONTINUED(status)) {
                    // pass
                } else {
                    // exited normally or due to signals
                    break;
                }
            }

            /*
            for (i = 0; i < nfds; ++i) {
                printf(" %d", pfds[i].fd);
            }
            printf(" / %d\n", nfds);
            */
            if (poll(pfds, nfds, -1) < 0) {
                ret = errno;
                goto kill;
            }

            for (i = 0; i < nfds && valid_nfds > 0; ++i) {
                /* printf("check: %d [%d/%d]\n", pfds[i].fd, i, nfds); */

                if (pfds[i].revents & POLLERR) {
                    /* printf("POLLERR: %d [%d/%d]\n", pfds[i].fd, i, nfds); */

                    close(pfds[i].fd);
                    pfds[i].fd = -1;
                    valid_nfds --;
                }
                else if (pfds[i].revents & POLLOUT) {
                    /* printf("POLLOUT: %d [%d/%d]\n", pfds[i].fd, i, nfds); */

                    nbytes = write(pfds[i].fd, input, input_size - input_len);
                    if (nbytes <= 0) {
                        ret = errno;
                        goto kill;
                    }
                    /* printf("POLLOUT: %d bytes\n", nbytes); */
                    input_len += nbytes;
                    if (cast(off_t, input_len) >= input_size) {
                        close(pfds[i].fd);
                        pfds[i].fd = -1;
                        valid_nfds --;
                    }
                }
                else if (pfds[i].revents & POLLIN) {
                    /* printf("POLLIN: %d [%d/%d]\n", pfds[i].fd, i, nfds); */
                    char **buffer = nullptr;
                    uint32_t *offset;
                    ssize_t to_read = 0;
                    off_t *size;
                    if (pfds[i].fd == stdout_pipe[R]) {
                        buffer = output;
                        offset = output_len;
                        size = &output_size;
                    }
                    else if (pfds[i].fd == stderr_pipe[R]) {
                        buffer = err;
                        offset = err_len;
                        size = &err_size;
                    }
                    else {
                        assert(pfds[i].fd == info_pipe[R]);
                        buffer = &info;
                        offset = &info_len;
                        size = &info_size;
                    }

                    do {
                        to_read = *size - *offset;
                        assert (to_read > 0);
                        /* printf("to read %d bytes\n", to_read); */
                        nbytes = read(pfds[i].fd, *buffer + *offset, to_read);

                        // The man page of poll says about POLLIN:
                        //
                        // POLLIN      Data other than high-priority data may be read without blocking.

                        //    For STREAMS, this flag is set in revents even if the message is of _zero_ length. This flag shall be equivalent to POLLRDNORM | POLLRDBAND.
                        // POLLHUP     A  device  has been disconnected, or a pipe or FIFO has been closed by the last process that had it open for writing. Once set, the hangup state of a FIFO shall persist until some process opens the FIFO for writing or until all read-only file descriptors for the FIFO  are  closed.  This  event  and POLLOUT  are  mutually-exclusive; a stream can never be writable if a hangup has occurred. However, this event and POLLIN, POLLRDNORM, POLLRDBAND, or POLLPRI are not mutually-exclusive. This flag is only valid in the revents bitmask; it shall be ignored in the events member.
                        // So "nbytes = 0" could be a valid return with POLLIN, and not indicating the other end closed the pipe, which is indicated by POLLHUP
                        if (nbytes <= 0)
                            break;

                        /* printf("POLLIN: %d bytes\n", nbytes); */

                        *offset += nbytes;
                        assert(cast(off_t, *offset) <= *size);

                        if (cast(off_t, *offset) == *size) {
                            char *larger = cast(
                                char*,
                                malloc(*size + BUF_SIZE_CHUNK)
                            );
                            if (larger == nullptr)
                                goto kill;
                            memcpy(larger, *buffer, *size);
                            free(*buffer);
                            *buffer = larger;
                            *size += BUF_SIZE_CHUNK;
                        }
                        assert(cast(off_t, *offset) < *size);
                    } while (nbytes == to_read);
                }
                else if (pfds[i].revents & POLLHUP) {
                    /* printf("POLLHUP: %d [%d/%d]\n", pfds[i].fd, i, nfds); */
                    close(pfds[i].fd);
                    pfds[i].fd = -1;
                    valid_nfds --;
                }
                else if (pfds[i].revents & POLLNVAL) {
                    /* printf("POLLNVAL: %d [%d/%d]\n", pfds[i].fd, i, nfds); */
                    ret = errno;
                    goto kill;
                }
            }
        }

        if (valid_nfds == 0 && flag_wait) {
            if (waitpid(fpid, &status, 0) < 0) {
                ret = errno;
                goto error;
            }
        }

    }
    else { // error
        ret = errno;
        goto error;
    }

    goto cleanup;

kill:
    kill(fpid, SIGKILL);
    waitpid(fpid, nullptr, 0);

error:
    if (ret == 0) {
        non_errno_ret = -1024; //randomly picked
    }

cleanup:
    // CALL only expects to have to free the output or error buffer if there
    // was a non-zero number of bytes returned.  If there was no data, take
    // care of it here.
    //
    // !!! This won't be done this way when this routine actually appends to
    // the BINARY! or STRING! itself.
    //
    if (output and *output)
        if (*output_len == 0) { // buffer allocated but never used
            free(*output);
            *output = nullptr;
        }

    if (err and *err)
        if (*err_len == 0) { // buffer allocated but never used
            free(*err);
            *err = nullptr;
        }

    if (info_pipe[R] > 0)
        close(info_pipe[R]);

    if (info_pipe[W] > 0)
        close(info_pipe[W]);

    if (info_len == sizeof(int)) {
        //
        // exec in child process failed, set to errno for reporting.
        //
        ret = *cast(int*, info);
    }
    else if (WIFEXITED(status)) {
        assert(info_len == 0);

       *exit_code = WEXITSTATUS(status);
       *pid = fpid;
    }
    else if (WIFSIGNALED(status)) {
        non_errno_ret = WTERMSIG(status);
    }
    else if (WIFSTOPPED(status)) {
        //
        // Shouldn't be here, as the current behavior is keeping waiting when
        // child is stopped
        //
        assert(false);
        if (info)
            free(info);
        rebJumps("fail {Child process is stopped}");
    }
    else {
        non_errno_ret = -2048; //randomly picked
    }

    if (info != nullptr)
        free(info);

info_pipe_err:
    if (stderr_pipe[R] > 0)
        close(stderr_pipe[R]);

    if (stderr_pipe[W] > 0)
        close(stderr_pipe[W]);

    goto stderr_pipe_err; // no jumps here yet, avoid warning

stderr_pipe_err:
    if (stdout_pipe[R] > 0)
        close(stdout_pipe[R]);

    if (stdout_pipe[W] > 0)
        close(stdout_pipe[W]);

stdout_pipe_err:
    if (stdin_pipe[R] > 0)
        close(stdin_pipe[R]);

    if (stdin_pipe[W] > 0)
        close(stdin_pipe[W]);

stdin_pipe_err:

    //
    // We will get to this point on success, as well as error (so ret may
    // be 0.  This is the return value of the host kit function to Rebol, not
    // the process exit code (that's written into the pointer arg 'exit_code')
    //

    if (non_errno_ret > 0) {
        rebJumps(
            "fail [{Child process is terminated by signal:}",
                rebI(non_errno_ret),
            "]"
        );
    }
    else if (non_errno_ret < 0)
        rebJumps("fail {Unknown error happened in CALL}");

    return ret;
}

#endif


//
//  export call-internal*: native [
//
//  "Run another program; return immediately (unless /WAIT)."
//
//      command [text! block! file!]
//          {An OS-local command line (quoted as necessary), a block with
//          arguments, or an executable file}
//      /wait
//          "Wait for command to terminate before returning"
//      /console
//          "Runs command with I/O redirected to console"
//      /shell
//          "Forces command to be run from shell"
//      /info
//          "Returns process information object"
//      /input
//          "Redirects stdin to in (if blank, /dev/null)"
//      in [text! binary! file! blank!]
//      /output
//          "Redirects stdout to out (if blank, /dev/null)"
//      out [text! binary! file! blank!]
//      /error
//          "Redirects stderr to err (if blank, /dev/null)"
//      err [text! binary! file! blank!]
//      /relax "If exit code is non-zero, return the integer vs. raising error"
//  ]
//
DECLARE_NATIVE(CALL_INTERNAL_P)
//
// !!! Parameter usage may require WAIT mode even if not explicitly requested.
// /WAIT is CALL wrapper default, see CALL* and CALL!
{
    PROCESS_INCLUDE_PARAMS_OF_CALL_INTERNAL_P;
    UNUSED(Bool_ARG(RELAX));  // handled by CALL_INTERNAL_P

    UNUSED(Bool_ARG(SHELL)); // looked at via level_ by OS_Create_Process
    UNUSED(Bool_ARG(CONSOLE)); // same

    // Make sure that if the output or error series are STRING! or BINARY!,
    // they are not read-only, before we try appending to them.
    //
    if (Is_Text(ARG(OUT)) or Is_Binary(ARG(OUT)))
        Fail_If_Read_Only_Flex(Cell_Flex(ARG(OUT)));
    if (Is_Text(ARG(ERR)) or Is_Binary(ARG(ERR)))
        Fail_If_Read_Only_Flex(Cell_Flex(ARG(ERR)));

    char *os_input;
    REBLEN input_len;

    UNUSED(Bool_ARG(INPUT)); // implicit by void ARG(IN)
    switch (VAL_TYPE(ARG(IN))) {
    case REB_BLANK:
    case REB_MAX_NULLED: // no /INPUT, so no argument provided
        os_input = nullptr;
        input_len = 0;
        break;

    case REB_TEXT: {
        size_t size;
        os_input = s_cast(rebBytes(&size, ARG(IN)));
        input_len = size;
        break; }

    case REB_FILE: {
        size_t size;  // !!! why fileNAME size passed in???
        os_input = s_cast(rebBytes(&size, "file-to-local", ARG(IN)));
        input_len = size;
        break; }

    case REB_BINARY: {
        size_t size;
        os_input = s_cast(rebBytes(&size, ARG(IN)));
        input_len = size;
        break; }

    default:
        panic(ARG(IN));
    }

    UNUSED(Bool_ARG(OUTPUT));
    UNUSED(Bool_ARG(ERROR));

    bool flag_wait;
    if (
        Bool_ARG(WAIT)
        or (
            Is_Text(ARG(IN)) or Is_Binary(ARG(IN))
            or Is_Text(ARG(OUT)) or Is_Binary(ARG(OUT))
            or Is_Text(ARG(ERR)) or Is_Binary(ARG(ERR))
        ) // I/O redirection implies /WAIT
    ){
        flag_wait = true;
    }
    else
        flag_wait = false;

    // We synthesize the argc and argv from the "command", and in the process
    // we do dynamic allocations of argc strings through the API.  These need
    // to be freed before we return.
    //
    OSCHR *cmd;
    int argc;
    const OSCHR **argv;

    if (Is_Text(ARG(COMMAND))) {
        // `call {foo bar}` => execute %"foo bar"

        // !!! Interpreting string case as an invocation of %foo with argument
        // "bar" has been requested and seems more suitable.  Question is
        // whether it should go through the shell parsing to do so.

        cmd = rebValSpellingAllocOS(ARG(COMMAND));

        argc = 1;
        argv = rebAllocN(const OSCHR*, (argc + 1));

        // !!! Make two copies because it frees cmd and all the argv.  Review.
        //
        argv[0] = rebValSpellingAllocOS(ARG(COMMAND));
        argv[1] = nullptr;
    }
    else if (Is_Block(ARG(COMMAND))) {
        // `call ["foo" "bar"]` => execute %foo with arg "bar"

        cmd = nullptr;

        Value* block = ARG(COMMAND);
        argc = Cell_Series_Len_At(block);
        if (argc == 0)
            fail (Error_Too_Short_Raw());

        argv = rebAllocN(const OSCHR*, (argc + 1));

        int i;
        for (i = 0; i < argc; i ++) {
            Cell* param = Cell_List_At_Head(block, i);
            if (Is_Text(param)) {
                argv[i] = rebValSpellingAllocOS(KNOWN(param));
            }
            else if (Is_File(param)) {
              #ifdef OS_WIDE_CHAR
                argv[i] = rebSpellW("file-to-local", KNOWN(param));
              #else
                argv[i] = rebSpell("file-to-local", KNOWN(param));
              #endif
            }
            else
                fail (Error_Invalid_Core(param, VAL_SPECIFIER(block)));
        }
        argv[argc] = nullptr;
    }
    else if (Is_File(ARG(COMMAND))) {
        // `call %"foo bar"` => execute %"foo bar"

        cmd = nullptr;

        argc = 1;
        argv = rebAllocN(const OSCHR*, (argc + 1));

      #ifdef OS_WIDE_CHAR
        argv[0] = rebSpellW("file-to-local", ARG(COMMAND));
      #else
        argv[0] = rebSpell("file-to-local", ARG(COMMAND));
      #endif

        argv[1] = nullptr;
    }
    else
        fail (Error_Invalid(ARG(COMMAND)));

    REBU64 pid;
    int exit_code;

    // If a STRING! or BINARY! is used for the output or error, then that
    // is treated as a request to append the results of the pipe to them.
    //
    // !!! At the moment this is done by having the OS-specific routine
    // pass back a buffer it malloc()s and reallocates to be the size of the
    // full data, which is then appended after the operation is finished.
    // With CALL now an extension where all parts have access to the internal
    // API, it could be added directly to the binary or string as it goes.

    // These are initialized to avoid a "possibly uninitialized" warning.
    //
    char *os_output = nullptr;
    uint32_t output_len = 0;
    char *os_err = nullptr;
    uint32_t err_len = 0;

    REBINT r = OS_Create_Process(
        level_,
        cast(const OSCHR*, cmd),
        argc,
        cast(const OSCHR**, argv),
        flag_wait,
        &pid,
        &exit_code,
        os_input,
        input_len,
        Is_Text(ARG(OUT)) or Is_Binary(ARG(OUT)) ? &os_output : nullptr,
        Is_Text(ARG(OUT)) or Is_Binary(ARG(OUT)) ? &output_len : nullptr,
        Is_Text(ARG(ERR)) or Is_Binary(ARG(ERR)) ? &os_err : nullptr,
        Is_Text(ARG(ERR)) or Is_Binary(ARG(ERR)) ? &err_len : nullptr
    );

    // Call may not succeed if r != 0, but we still have to run cleanup
    // before reporting any error...

    assert(argc > 0);

    int i;
    for (i = 0; i != argc; ++i)
        rebFree(m_cast(OSCHR*, argv[i]));

    if (cmd != nullptr)
        rebFree(cmd);

    rebFree(m_cast(OSCHR**, argv));

    if (Is_Text(ARG(OUT))) {
        if (output_len > 0) {
            Append_OS_Str(ARG(OUT), os_output, output_len);
            free(os_output);
        }
    }
    else if (Is_Binary(ARG(OUT))) {
        if (output_len > 0) {
            Append_Unencoded_Len(Cell_Binary(ARG(OUT)), os_output, output_len);
            free(os_output);
        }
    }

    if (Is_Text(ARG(ERR))) {
        if (err_len > 0) {
            Append_OS_Str(ARG(ERR), os_err, err_len);
            free(os_err);
        }
    } else if (Is_Binary(ARG(ERR))) {
        if (err_len > 0) {
            Append_Unencoded_Len(Cell_Binary(ARG(ERR)), os_err, err_len);
            free(os_err);
        }
    }

    if (os_input != nullptr)
        rebFree(os_input);

    if (Bool_ARG(INFO)) {
        VarList* info = Alloc_Context(REB_OBJECT, 2);

        Init_Integer(Append_Context(info, nullptr, Canon(SYM_ID)), pid);
        if (Bool_ARG(WAIT))
            Init_Integer(
                Append_Context(info, nullptr, Canon(SYM_EXIT_CODE)),
                exit_code
            );

        return Init_Object(OUT, info);
    }

    if (r != 0)
        rebFail_OS (r);

    // We may have waited even if they didn't ask us to explicitly, but
    // we only return a process ID if /WAIT was not explicitly used
    //
    if (Bool_ARG(WAIT)) {
        //
        // !!! should Bool_ARG(RELAX) and exit_code == 0 return trash instead of 0?
        // it would be less visually noisy in the console.
        //
        if (Bool_ARG(RELAX) or exit_code == 0)
            return Init_Integer(OUT, exit_code);

        rebJumps (
            "fail ["
                "{CALL without /RELAX got nonzero exit code:}",
                rebI(exit_code),
            "]"
        );
    }

    return Init_Integer(OUT, pid);
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
DECLARE_NATIVE(GET_OS_BROWSERS)
//
// !!! Using the %1 convention is not necessarily ideal vs. having some kind
// of more "structural" result, it was just easy because it's how the string
// comes back from the Windows registry.  Review.
{
    PROCESS_INCLUDE_PARAMS_OF_GET_OS_BROWSERS;

    Value* list = rebValue("copy []");

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

    DWORD num_bytes = 0; // pass nullptr and use 0 for initial length, to query

    DWORD type;
    DWORD flag = RegQueryValueExW(key, L"", 0, &type, nullptr, &num_bytes);

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

  #elif defined(TO_LINUX)

    // Caller should try xdg-open first, then try x-www-browser otherwise
    //
    rebElide(
        "append", list, "[",
            rebT("xdg-open %1"),
            rebT("x-www-browser %1"),
        "]"
    );

  #else // Just try /usr/bin/open on POSIX, OS X, Haiku, etc.

    rebElide("append", list, rebT("/usr/bin/open %1"));

  #endif

    return list;
}


//
//  export sleep: native [
//
//  "Use system sleep to wait a certain amount of time (doesn't use PORT!s)."
//
//      return: [nothing!]
//      duration [integer! decimal! time!]
//          {Length to sleep (integer and decimal are measuring seconds)}
//
//  ]
//
DECLARE_NATIVE(SLEEP)
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

    REBLEN msec = Milliseconds_From_Value(ARG(DURATION));

  #ifdef TO_WINDOWS
    Sleep(msec);
  #else
    usleep(msec * 1000);
  #endif

    return Init_Nothing(OUT);
}

#if defined(TO_LINUX) || defined(TO_ANDROID) || defined(TO_POSIX) || defined(TO_OSX)
static void kill_process(pid_t pid, int signal);
#endif

//
//  terminate: native [
//
//  "Terminate a process (not current one)"
//
//      return: [~null~]
//      pid [integer!]
//          {The process ID}
//  ]
//
DECLARE_NATIVE(TERMINATE)
{
    PROCESS_INCLUDE_PARAMS_OF_TERMINATE;

  #ifdef TO_WINDOWS

    if (GetCurrentProcessId() == cast(DWORD, VAL_INT32(ARG(PID))))
        fail ("Use QUIT or EXIT-REBOL to terminate current process, instead");

    DWORD err = 0;
    HANDLE ph = OpenProcess(PROCESS_TERMINATE, FALSE, VAL_INT32(ARG(PID)));
    if (ph == nullptr) {
        err = GetLastError();
        switch (err) {
          case ERROR_ACCESS_DENIED:
            Fail_Permission_Denied();

          case ERROR_INVALID_PARAMETER:
            Fail_No_Process(ARG(PID));

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
        Fail_No_Process(ARG(PID));

      default:
        Fail_Terminate_Failed(err);
    }

  #elif defined(TO_LINUX) || defined(TO_ANDROID) || defined(TO_POSIX) || defined(TO_OSX)

    if (getpid() == VAL_INT32(ARG(PID))) {
        // signal is not as reliable for this purpose
        // it's caught in host-main.c as to stop the evaluation
        fail ("Use QUIT or EXIT-REBOL to terminate current process, instead");
    }
    kill_process(VAL_INT32(ARG(PID)), SIGTERM);
    return nullptr;

  #else

    UNUSED(level_);
    fail ("terminate is not implemented for this platform");

  #endif
}


//
//  export get-env: native [
//
//  {Returns the value of an OS environment variable (for current process).}
//
//      return: "String the variable was set to, or null if not set"
//          [~null~ text!]
//      variable "Name of variable to get (case-insensitive in Windows)"
//          [text! word!]
//  ]
//
DECLARE_NATIVE(GET_ENV)
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

    Value* variable = ARG(VARIABLE);

    Error* error = nullptr;

  #ifdef TO_WINDOWS
    // Note: The Windows variant of this API is NOT case-sensitive

    WCHAR *key = rebSpellW(variable);

    DWORD val_len_plus_one = GetEnvironmentVariable(key, nullptr, 0);
    if (val_len_plus_one == 0) { // some failure...
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND)
            Init_Nulled(OUT);
        else
            error = Error_User("Unknown error when requesting variable size");
    }
    else {
        WCHAR *val = rebAllocN(WCHAR, val_len_plus_one);
        DWORD result = GetEnvironmentVariable(key, val, val_len_plus_one);
        if (result == 0)
            error = Error_User("Unknown error fetching variable to buffer");
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

    char *key = rebSpell(variable);

    const char* val = getenv(key);
    if (val == nullptr) // key not present in environment
        Init_Nulled(OUT);
    else {
        size_t size = strsize(val);

        /* assert(size != 0); */ // True?  Should it return BLANK!?

        Init_Text(OUT, Make_Sized_String_UTF8(val, size));
    }

    rebFree(key);
  #endif

    // Error is broken out like this so that the proper freeing can be done
    // without leaking temporary buffers.
    //
    if (error != nullptr)
        fail (error);

    return OUT;
}


//
//  export set-env: native [
//
//  {Sets value of operating system environment variable for current process.}
//
//      return: "Returns same value passed in"
//          [~null~ text!]
//      variable [<maybe> text! word!]
//          "Variable to set (case-insensitive in Windows)"
//      value [~null~ text!]
//          "Value to set the variable to, or NULL to unset it"
//  ]
//
DECLARE_NATIVE(SET_ENV)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_ENV;

    Value* variable = ARG(VARIABLE);
    Value* value = ARG(VALUE);

  #ifdef TO_WINDOWS
    WCHAR *key_wide = rebSpellW(variable);
    WCHAR *opt_val_wide = rebSpellW("ensure [~null~ text!]", value);

    if (not SetEnvironmentVariable(key_wide, opt_val_wide)) // null unsets
        fail ("environment variable couldn't be modified");

    rebFree(opt_val_wide);
    rebFree(key_wide);
  #else
    char *key_utf8 = rebSpell(variable);

    if (Is_Nulled(value)) {
      #ifdef unsetenv
        if (unsetenv(key_utf8) == -1)
            fail ("unsetenv() couldn't unset environment variable");
      #else
        // WARNING: KNOWN PORTABILITY ISSUE
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
        char *val_utf8 = rebSpell(value);

        if (setenv(key_utf8, val_utf8, 1) == -1) // the 1 means "overwrite"
            fail ("setenv() coudln't set environment variable");

        rebFree(val_utf8);
      #else
        // WARNING: KNOWN MEMORY LEAK!
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

        if (putenv(key_equals_val_utf8) == -1) // !!! why mutable?  :-/
            fail ("putenv() couldn't set environment variable");

        /* rebFree(key_equals_val_utf8); */ // !!! Can't!  Crashes getenv()
        rebUnmanage(key_equals_val_utf8); // oh well, have to leak it
      #endif
    }

    rebFree(key_utf8);
  #endif

    RETURN (ARG(VALUE));
}


//
//  export list-env: native [
//
//  {Returns a map of OS environment variables (for current process).}
//
//      ; No arguments
//  ]
//
DECLARE_NATIVE(LIST_ENV)
{
    PROCESS_INCLUDE_PARAMS_OF_LIST_ENV;

    Value* map = rebValue("make map! []");

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
        Value* key = rebLengthedTextWide(key_equals_val, key_len);

        int val_len = len - (eq_pos - key_equals_val) - 1;
        Value* val = rebLengthedTextWide(eq_pos + 1, val_len);

        rebElide("append", map, "[", rebR(key), rebR(val), "]");

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
    for (n = 0; environ[n] != nullptr; ++n) {
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

        rebElide("append", map, "[", rebR(key), rebR(val), "]");
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
DECLARE_NATIVE(GET_PID)
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
DECLARE_NATIVE(GET_UID)
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
DECLARE_NATIVE(GET_EUID)
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
DECLARE_NATIVE(GET_GID)
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
DECLARE_NATIVE(GET_EGID)
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
DECLARE_NATIVE(SET_UID)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_UID;

    if (setuid(VAL_INT32(ARG(UID))) >= 0)
        RETURN (ARG(UID));

    switch (errno) {
      case EINVAL:
        fail (Error_Invalid(ARG(UID)));

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
//          [~null~]
//      euid "The effective user ID"
//          [integer!]
//  ]
//  platforms: [linux android posix osx]
//
DECLARE_NATIVE(SET_EUID)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_EUID;

    if (seteuid(VAL_INT32(ARG(EUID))) >= 0)
        RETURN (ARG(EUID));

    switch (errno) {
      case EINVAL:
        fail (Error_Invalid(ARG(EUID)));

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
//          [~null~]
//      gid "The effective group ID"
//          [integer!]
//  ]
//  platforms: [linux android posix osx]
//
DECLARE_NATIVE(SET_GID)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_GID;

    if (setgid(VAL_INT32(ARG(GID))) >= 0)
        RETURN (ARG(GID));

    switch (errno) {
      case EINVAL:
        fail (Error_Invalid(ARG(GID)));

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
DECLARE_NATIVE(SET_EGID)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_EGID;

    if (setegid(VAL_INT32(ARG(EGID))) >= 0)
        RETURN (ARG(EGID));

    switch (errno) {
      case EINVAL:
        fail (Error_Invalid(ARG(EGID)));

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
            "fail [{Invalid signal number:}", rebI(signal), "]"
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
//      return: [nothing!]  ;-- !!! might this return pid or signal (?)
//      pid [integer!]
//          {The process ID}
//      signal [integer!]
//          {The signal number}
//  ]
//  platforms: [linux android posix osx]
//
DECLARE_NATIVE(SEND_SIGNAL)
{
    PROCESS_INCLUDE_PARAMS_OF_SEND_SIGNAL;

    // !!! Is called `send-signal` but only seems to call kill (?)
    //
    kill_process(rebUnboxInteger(ARG(PID)), rebUnboxInteger(ARG(SIGNAL)));

    return Init_Nothing(OUT);
}

#endif // defined(TO_LINUX) || defined(TO_ANDROID) || defined(TO_POSIX) || defined(TO_OSX)
