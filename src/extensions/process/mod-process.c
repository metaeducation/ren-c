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
#include "sys-ext.h"

#include "tmp-mod-process-first.h"


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
OSCHR *rebValSpellingAllocOS(const REBVAL *any_string)
{
  #ifdef OS_WIDE_CHAR
    return rebSpellAllocW(any_string, END);
  #else
    return rebSpellAlloc(any_string, END);
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
void Append_OS_Str(REBVAL *dest, const void *src, REBINT len)
{
  #ifdef TO_WINDOWS
    REBVAL *src_str = rebLengthedTextW(cast(const REBWCHAR*, src), len);
  #else
    REBVAL *src_str = rebSizedText(cast(const char*, src), len);
  #endif

    rebElide("append", dest, src_str, END);

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


#ifdef TO_WINDOWS
//
//  OS_Create_Process: C
//
// Return -1 on error.
//
int OS_Create_Process(
    REBFRM *frame_, // stopgap: allows access to CALL's ARG() and REF()
    const WCHAR *call,
    int argc,
    const WCHAR * argv[],
    REBOOL flag_wait,
    uint64_t *pid,
    int *exit_code,
    char *input,
    uint32_t input_len,
    char **output,
    uint32_t *output_len,
    char **err,
    uint32_t *err_len
) {
    PROCESS_INCLUDE_PARAMS_OF_CALL;

    UNUSED(ARG(command)); // turned into `call` and `argv/argc` by CALL
    UNUSED(REF(wait)); // covered by flag_wait

    UNUSED(REF(console)); // actually not paid attention to

    if (call == NULL)
        fail ("'argv[]'-style launching not implemented on Windows CALL");

  #ifdef GET_IS_NT_FLAG // !!! Why was this here?
    REBOOL is_NT;
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
    WCHAR *cmd = NULL;
    char *oem_input = NULL;

    UNUSED(REF(info));

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    STARTUPINFO si;
    si.cb = sizeof(si);
    si.lpReserved = NULL;
    si.lpDesktop = NULL;
    si.lpTitle = NULL;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.wShowWindow = SW_SHOWNORMAL;
    si.cbReserved2 = 0;
    si.lpReserved2 = NULL;

    UNUSED(REF(input)); // implicitly covered by void ARG(in)
    switch (VAL_TYPE(ARG(in))) {
    case REB_TEXT:
    case REB_BINARY:
        if (!CreatePipe(&hInputRead, &hInputWrite, NULL, 0)) {
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
        WCHAR *local_wide = rebSpellAllocW("lib/file-to-local", ARG(in), END);

        hInputRead = CreateFile(
            local_wide,
            GENERIC_READ, // desired mode
            0, // shared mode
            &sa, // security attributes
            OPEN_EXISTING, // creation disposition
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, // flags
            NULL // template
        );
        si.hStdInput = hInputRead;

        rebFree(local_wide);
        break; }

    case REB_BLANK:
        si.hStdInput = 0;
        break;

    case REB_MAX_VOID:
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        break;

    default:
        panic (ARG(in));
    }

    UNUSED(REF(output)); // implicitly covered by void ARG(out)
    switch (VAL_TYPE(ARG(out))) {
    case REB_TEXT:
    case REB_BINARY:
        if (!CreatePipe(&hOutputRead, &hOutputWrite, NULL, 0)) {
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
        WCHAR *local_wide = rebSpellAllocW("file-to-local", ARG(out), END);

        si.hStdOutput = CreateFile(
            local_wide,
            GENERIC_WRITE, // desired mode
            0, // shared mode
            &sa, // security attributes
            CREATE_NEW, // creation disposition
            FILE_ATTRIBUTE_NORMAL, // flag and attributes
            NULL // template
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
                NULL // template
            );
        }

        rebFree(local_wide);
        break; }

    case REB_BLANK:
        si.hStdOutput = 0;
        break;

    case REB_MAX_VOID:
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        break;

    default:
        panic (ARG(out));
    }

    UNUSED(REF(error)); // implicitly covered by void ARG(err)
    switch (VAL_TYPE(ARG(err))) {
    case REB_TEXT:
    case REB_BINARY:
        if (!CreatePipe(&hErrorRead, &hErrorWrite, NULL, 0)) {
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
        WCHAR *local_wide = rebSpellAllocW("file-to-local", ARG(out), END);

        si.hStdError = CreateFile(
            local_wide,
            GENERIC_WRITE, // desired mode
            0, // shared mode
            &sa, // security attributes
            CREATE_NEW, // creation disposition
            FILE_ATTRIBUTE_NORMAL, // flag and attributes
            NULL // template
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
                NULL // template
            );
        }

        rebFree(local_wide);
        break; }

    case REB_BLANK:
        si.hStdError = 0;
        break;

    case REB_MAX_VOID:
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        break;

    default:
        panic (ARG(err));
    }

    if (REF(shell)) {
        // command to cmd.exe needs to be surrounded by quotes to preserve the inner quotes
        const WCHAR *sh = L"cmd.exe /C \"";

        REBCNT len = wcslen(sh) + wcslen(call) + 3;

        cmd = rebAllocN(WCHAR, len);
        cmd[0] = L'\0';
        wcscat(cmd, sh);
        wcscat(cmd, call);
        wcscat(cmd, L"\"");
    }
    else {
        // CreateProcess might write to this memory
        // Duplicate it to be safe

        cmd = _wcsdup(call); // !!! guaranteed OS_FREE can release this?
    }

    PROCESS_INFORMATION pi;
    result = CreateProcess(
        NULL, // executable name
        cmd, // command to execute
        NULL, // process security attributes
        NULL, // thread security attributes
        TRUE, // inherit handles, must be TRUE for I/O redirection
        NORMAL_PRIORITY_CLASS | CREATE_DEFAULT_ERROR_MODE, // creation flags
        NULL, // environment
        NULL, // current directory
        &si, // startup information
        &pi // process information
    );

    rebFree(cmd);

    *pid = pi.dwProcessId;

    if (hInputRead != NULL)
        CloseHandle(hInputRead);

    if (hOutputWrite != NULL)
        CloseHandle(hOutputWrite);

    if (hErrorWrite != NULL)
        CloseHandle(hErrorWrite);

    // Wait for termination:
    if (result != 0 && flag_wait) {
        HANDLE handles[3];
        int count = 0;
        DWORD output_size = 0;
        DWORD err_size = 0;

        if (hInputWrite != NULL && input_len > 0) {
            if (IS_TEXT(ARG(in))) {
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
                    NULL,
                    NULL
                );
                if (dest_len > 0) {
                    oem_input = cast(char*, malloc(dest_len));
                    if (oem_input != NULL) {
                        WideCharToMultiByte(
                            CP_OEMCP,
                            0,
                            cast(WCHAR*, input),
                            input_len,
                            oem_input,
                            dest_len,
                            NULL,
                            NULL
                        );
                        input_len = dest_len;
                        input = oem_input;
                        handles[count ++] = hInputWrite;
                    }
                }
            } else {
                assert(IS_BINARY(ARG(in)));
                handles[count ++] = hInputWrite;
            }
        }
        if (hOutputRead != NULL) {
            output_size = BUF_SIZE_CHUNK;
            *output_len = 0;

            *output = cast(char*, malloc(output_size));
            handles[count ++] = hOutputRead;
        }
        if (hErrorRead != NULL) {
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
                        NULL
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
                            hInputWrite = NULL;
                            free(oem_input);
                            oem_input = NULL;
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
                        NULL
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
                            if (*output == NULL) goto kill;
                        }
                    }
                }
                else if (handles[i] == hErrorRead) {
                    if (!ReadFile(
                        hErrorRead,
                        *cast(char**, err) + *err_len,
                        err_size - *err_len,
                        &n,
                        NULL
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
                            if (*err == NULL) goto kill;
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

        if (IS_TEXT(ARG(out)) and *output and *output_len > 0) {
            /* convert to wide char string */
            int dest_len = 0;
            WCHAR *dest = NULL;
            dest_len = MultiByteToWideChar(
                CP_OEMCP, 0, *output, *output_len, dest, 0
            );
            if (dest_len <= 0) {
                free(*output);
                *output = NULL;
                *output_len = 0;
            }
            dest = cast(WCHAR*, malloc(*output_len * sizeof(WCHAR)));
            if (dest == NULL)
                goto cleanup;
            MultiByteToWideChar(
                CP_OEMCP, 0, *output, *output_len, dest, dest_len
            );
            free(*output);
            *output = cast(char*, dest);
            *output_len = dest_len;
        }

        if (IS_TEXT(ARG(err)) && *err != NULL && *err_len > 0) {
            /* convert to wide char string */
            int dest_len = 0;
            WCHAR *dest = NULL;
            dest_len = MultiByteToWideChar(
                CP_OEMCP, 0, *err, *err_len, dest, 0
            );
            if (dest_len <= 0) {
                free(*err);
                *err = NULL;
                *err_len = 0;
            }
            dest = cast(WCHAR*, malloc(*err_len * sizeof(WCHAR)));
            if (dest == NULL) goto cleanup;
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
    if (oem_input != NULL) {
        free(oem_input);
    }

    if (output and *output and *output_len == 0) {
        free(*output);
    }

    if (err and *err != NULL and *err_len == 0) {
        free(*err);
    }

    if (hInputWrite != NULL)
        CloseHandle(hInputWrite);

    if (hOutputRead != NULL)
        CloseHandle(hOutputRead);

    if (hErrorRead != NULL)
        CloseHandle(hErrorRead);

    if (IS_FILE(ARG(err))) {
        CloseHandle(si.hStdError);
    }

error_error:
    if (IS_FILE(ARG(out))) {
        CloseHandle(si.hStdOutput);
    }

output_error:
    if (IS_FILE(ARG(in))) {
        CloseHandle(si.hStdInput);
    }

input_error:
    return ret;  // meaning depends on flags
}

#else // !defined(TO_WINDOWS), so POSIX, LINUX, OS X, etc.

static inline REBOOL Open_Pipe_Fails(int pipefd[2]) {
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
        return TRUE;
#else
    if (pipe(pipefd) < 0)
        return TRUE;
    int direction; // READ=0, WRITE=1
    for (direction = 0; direction < 2; ++direction) {
        int oldflags = fcntl(pipefd[direction], F_GETFD);
        if (oldflags < 0)
            return TRUE;
        if (fcntl(pipefd[direction], F_SETFD, oldflags | FD_CLOEXEC) < 0)
            return TRUE;
    }
#endif
    return FALSE;
}

static inline REBOOL Set_Nonblocking_Fails(int fd) {
    int oldflags;
    oldflags = fcntl(fd, F_GETFL);
    if (oldflags < 0)
        return TRUE;
    if (fcntl(fd, F_SETFL, oldflags | O_NONBLOCK) < 0)
        return TRUE;

    return FALSE;
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
    REBFRM *frame_, // stopgap: allows access to CALL's ARG() and REF()
    const char *call,
    int argc,
    const char* argv[],
    REBOOL flag_wait, // distinct from REF(wait)
    uint64_t *pid,
    int *exit_code,
    char *input,
    uint32_t input_len,
    char **output,
    uint32_t *output_len,
    char **err,
    uint32_t *err_len
){
    PROCESS_INCLUDE_PARAMS_OF_CALL;

    UNUSED(ARG(command)); // translated into call and argc/argv
    UNUSED(REF(wait)); // flag_wait controls this
    UNUSED(REF(input));
    UNUSED(REF(output));
    UNUSED(REF(error));

    UNUSED(REF(console)); // actually not paid attention to

    UNUSED(call);

    int status = 0;
    int ret = 0;
    int non_errno_ret = 0; // "ret" above should be valid errno

    // An "info" pipe is used to send back an error code from the child
    // process back to the parent if there is a problem.  It only writes
    // an integer's worth of data in that case, but it may need a bigger
    // buffer if more interesting data needs to pass between them.
    //
    char *info = NULL;
    off_t info_size = 0;
    uint32_t info_len = 0;

    // suppress unused warnings but keep flags for future use
    UNUSED(REF(info));
    UNUSED(REF(console));

    const unsigned int R = 0;
    const unsigned int W = 1;
    int stdin_pipe[] = {-1, -1};
    int stdout_pipe[] = {-1, -1};
    int stderr_pipe[] = {-1, -1};
    int info_pipe[] = {-1, -1};

    if (IS_TEXT(ARG(in)) or IS_BINARY(ARG(in))) {
        if (Open_Pipe_Fails(stdin_pipe))
            goto stdin_pipe_err;
    }

    if (IS_TEXT(ARG(out)) or IS_BINARY(ARG(out))) {
        if (Open_Pipe_Fails(stdout_pipe))
            goto stdout_pipe_err;
    }

    if (IS_TEXT(ARG(err)) or IS_BINARY(ARG(err))) {
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

        if (IS_TEXT(ARG(in)) or IS_BINARY(ARG(in))) {
            close(stdin_pipe[W]);
            if (dup2(stdin_pipe[R], STDIN_FILENO) < 0)
                goto child_error;
            close(stdin_pipe[R]);
        }
        else if (IS_FILE(ARG(in))) {
            char *local_utf8 = rebSpellAlloc("file-to-local", ARG(in), END);

            int fd = open(local_utf8, O_RDONLY);

            rebFree(local_utf8);

            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDIN_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else if (IS_BLANK(ARG(in))) {
            int fd = open("/dev/null", O_RDONLY);
            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDIN_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else {
            assert(IS_VOID(ARG(in)));
            // inherit stdin from the parent
        }

        if (IS_TEXT(ARG(out)) or IS_BINARY(ARG(out))) {
            close(stdout_pipe[R]);
            if (dup2(stdout_pipe[W], STDOUT_FILENO) < 0)
                goto child_error;
            close(stdout_pipe[W]);
        }
        else if (IS_FILE(ARG(out))) {
            char *local_utf8 = rebSpellAlloc("file-to-local", ARG(out), END);

            int fd = open(local_utf8, O_CREAT | O_WRONLY, 0666);

            rebFree(local_utf8);

            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDOUT_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else if (IS_BLANK(ARG(out))) {
            int fd = open("/dev/null", O_WRONLY);
            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDOUT_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else {
            assert(IS_VOID(ARG(out)));
            // inherit stdout from the parent
        }

        if (IS_TEXT(ARG(err)) or IS_BINARY(ARG(err))) {
            close(stderr_pipe[R]);
            if (dup2(stderr_pipe[W], STDERR_FILENO) < 0)
                goto child_error;
            close(stderr_pipe[W]);
        }
        else if (IS_FILE(ARG(err))) {
            char *local_utf8 = rebSpellAlloc("file-to-local", ARG(err), END);

            int fd = open(local_utf8, O_CREAT | O_WRONLY, 0666);

            rebFree(local_utf8);

            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDERR_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else if (IS_BLANK(ARG(err))) {
            int fd = open("/dev/null", O_WRONLY);
            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDERR_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else {
            assert(IS_VOID(ARG(err)));
            // inherit stderr from the parent
        }

        close(info_pipe[R]);

        /* printf("flag_shell in child: %hhu\n", flag_shell); */

        // We want to be able to compile with most all warnings as errors, and
        // we'd like to use -Wcast-qual (in builds where it is possible--it
        // is not possible in plain C builds).  We must tunnel under the cast.
        //
        char * const *argv_hack;

        if (REF(shell)) {
            const char *sh = getenv("SHELL");

            if (sh == NULL) { // shell does not exist
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
            argv_new[argc + 2] = NULL;

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
            assert(FALSE);
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
                    char **buffer = NULL;
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
                            if (larger == NULL)
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
    waitpid(fpid, NULL, 0);

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
            *output = NULL;
        }

    if (err and *err)
        if (*err_len == 0) { // buffer allocated but never used
            free(*err);
            *err = NULL;
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
        assert(FALSE);
        if (info != NULL)
            free(info);
        fail (Error(RE_EXT_PROCESS_CHILD_STOPPED, END));
    }
    else {
        non_errno_ret = -2048; //randomly picked
    }

    if (info != NULL)
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
        DECLARE_LOCAL(i);
        Init_Integer(i, non_errno_ret);
        fail (Error(RE_EXT_PROCESS_CHILD_TERMINATED_BY_SIGNAL, i, END));
    }
    else if (non_errno_ret < 0) {
        fail ("Unknown error happened in CALL");
    }
    return ret;
}

#endif


//
//  call: native/export [
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
//          "Redirects stdin to in"
//      in [text! binary! file! blank!]
//      /output
//          "Redirects stdout to out"
//      out [text! binary! file! blank!]
//      /error
//          "Redirects stderr to err"
//      err [text! binary! file! blank!]
//  ]
//  new-errors: [
//      child-terminated-by-signal: ["Child process is terminated by signal:" :arg1]
//      child-stopped: ["Child process is stopped"]
//  ]
//
REBNATIVE(call)
//
// !!! Parameter usage may require WAIT mode even if not explicitly requested.
// /WAIT should be default, with /ASYNC (or otherwise) as exception!
{
    PROCESS_INCLUDE_PARAMS_OF_CALL;

    UNUSED(REF(shell)); // looked at via frame_ by OS_Create_Process
    UNUSED(REF(console)); // same

    // SECURE was never actually done for R3-Alpha
    //
    Check_Security(Canon(SYM_CALL), POL_EXEC, ARG(command));

    // Make sure that if the output or error series are STRING! or BINARY!,
    // they are not read-only, before we try appending to them.
    //
    if (IS_TEXT(ARG(out)) or IS_BINARY(ARG(out)))
        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(ARG(out)));
    if (IS_TEXT(ARG(err)) or IS_BINARY(ARG(err)))
        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(ARG(err)));

    char *os_input;
    REBCNT input_len;

    UNUSED(REF(input)); // implicit by void ARG(in)
    switch (VAL_TYPE(ARG(in))) {
    case REB_BLANK:
    case REB_MAX_VOID: // no /INPUT, so no argument provided
        os_input = NULL;
        input_len = 0;
        break;

    case REB_TEXT: {
        REBSIZ size;
        os_input = s_cast(rebBytesAlloc(
            &size,
            ARG(in),
            END
        ));
        input_len = size;
        break; }

    case REB_FILE: {
        REBSIZ size;
        os_input = s_cast(rebBytesAlloc( // !!! why fileNAME size passed in???
            &size,
            "file-to-local", ARG(in),
            END
        ));
        input_len = size;
        break; }

    case REB_BINARY: {
        os_input = s_cast(rebBytesAlloc(&input_len, ARG(in)));
        break; }

    default:
        panic(ARG(in));
    }

    UNUSED(REF(output));
    UNUSED(REF(error));

    REBOOL flag_wait;
    if (
        REF(wait)
        or (
            IS_TEXT(ARG(in)) or IS_BINARY(ARG(in))
            or IS_TEXT(ARG(out)) or IS_BINARY(ARG(out))
            or IS_TEXT(ARG(err)) or IS_BINARY(ARG(err))
        ) // I/O redirection implies /WAIT
    ){
        flag_wait = TRUE;
    }
    else
        flag_wait = FALSE;

    // We synthesize the argc and argv from the "command", and in the process
    // we do dynamic allocations of argc strings through the API.  These need
    // to be freed before we return.
    //
    OSCHR *cmd;
    int argc;
    const OSCHR **argv;

    if (IS_TEXT(ARG(command))) {
        // `call {foo bar}` => execute %"foo bar"

        // !!! Interpreting string case as an invocation of %foo with argument
        // "bar" has been requested and seems more suitable.  Question is
        // whether it should go through the shell parsing to do so.

        cmd = rebValSpellingAllocOS(ARG(command));

        argc = 1;
        argv = rebAllocN(const OSCHR*, (argc + 1));

        // !!! Make two copies because it frees cmd and all the argv.  Review.
        //
        argv[0] = rebValSpellingAllocOS(ARG(command));
        argv[1] = NULL;
    }
    else if (IS_BLOCK(ARG(command))) {
        // `call ["foo" "bar"]` => execute %foo with arg "bar"

        cmd = NULL;

        REBVAL *block = ARG(command);
        argc = VAL_LEN_AT(block);
        if (argc == 0)
            fail (Error_Too_Short_Raw());

        argv = rebAllocN(const OSCHR*, (argc + 1));

        int i;
        for (i = 0; i < argc; i ++) {
            RELVAL *param = VAL_ARRAY_AT_HEAD(block, i);
            if (IS_TEXT(param)) {
                argv[i] = rebValSpellingAllocOS(KNOWN(param));
            }
            else if (IS_FILE(param)) {
              #ifdef OS_WIDE_CHAR
                argv[i] = rebSpellAllocW(
                    "file-to-local", KNOWN(param), END
                );
              #else
                argv[i] = rebSpellAlloc(
                    "file-to-local", KNOWN(param), END
                );
              #endif
            }
            else
                fail (Error_Invalid_Core(param, VAL_SPECIFIER(block)));
        }
        argv[argc] = NULL;
    }
    else if (IS_FILE(ARG(command))) {
        // `call %"foo bar"` => execute %"foo bar"

        cmd = NULL;

        argc = 1;
        argv = rebAllocN(const OSCHR*, (argc + 1));

      #ifdef OS_WIDE_CHAR
        argv[0] = rebSpellAllocW("file-to-local", ARG(command), END);
      #else
        argv[0] = rebSpellAlloc("file-to-local", ARG(command), END);
      #endif

        argv[1] = NULL;
    }
    else
        fail (Error_Invalid(ARG(command)));

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
    char *os_output = NULL;
    REBCNT output_len = 0;
    char *os_err = NULL;
    REBCNT err_len = 0;

    REBINT r = OS_Create_Process(
        frame_,
        cast(const OSCHR*, cmd),
        argc,
        cast(const OSCHR**, argv),
        flag_wait,
        &pid,
        &exit_code,
        os_input,
        input_len,
        IS_TEXT(ARG(out)) or IS_BINARY(ARG(out)) ? &os_output : nullptr,
        IS_TEXT(ARG(out)) or IS_BINARY(ARG(out)) ? &output_len : nullptr,
        IS_TEXT(ARG(err)) or IS_BINARY(ARG(err)) ? &os_err : nullptr,
        IS_TEXT(ARG(err)) or IS_BINARY(ARG(err)) ? &err_len : nullptr
    );

    // Call may not succeed if r != 0, but we still have to run cleanup
    // before reporting any error...

    assert(argc > 0);

    int i;
    for (i = 0; i != argc; ++i)
        rebFree(m_cast(OSCHR*, argv[i]));

    if (cmd != NULL)
        rebFree(cmd);

    rebFree(m_cast(OSCHR**, argv));

    if (IS_TEXT(ARG(out))) {
        if (output_len > 0) {
            Append_OS_Str(ARG(out), os_output, output_len);
            free(os_output);
        }
    }
    else if (IS_BINARY(ARG(out))) {
        if (output_len > 0) {
            Append_Unencoded_Len(VAL_SERIES(ARG(out)), os_output, output_len);
            free(os_output);
        }
    }

    if (IS_TEXT(ARG(err))) {
        if (err_len > 0) {
            Append_OS_Str(ARG(err), os_err, err_len);
            free(os_err);
        }
    } else if (IS_BINARY(ARG(err))) {
        if (err_len > 0) {
            Append_Unencoded_Len(VAL_SERIES(ARG(err)), os_err, err_len);
            free(os_err);
        }
    }

    if (os_input != NULL)
        rebFree(os_input);

    if (REF(info)) {
        REBCTX *info = Alloc_Context(REB_OBJECT, 2);

        Init_Integer(Append_Context(info, NULL, Canon(SYM_ID)), pid);
        if (REF(wait))
            Init_Integer(
                Append_Context(info, NULL, Canon(SYM_EXIT_CODE)),
                exit_code
            );

        Init_Object(D_OUT, info);
        return R_OUT;
    }

    if (r != 0)
        rebFail_OS (r);

    // We may have waited even if they didn't ask us to explicitly, but
    // we only return a process ID if /WAIT was not explicitly used
    //
    if (REF(wait))
        Init_Integer(D_OUT, exit_code);
    else
        Init_Integer(D_OUT, pid);

    return R_OUT;
}


//
//  get-os-browsers: native/export [
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

    REBVAL *list = rebRun("copy []", END);

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

    REBCNT len = num_bytes / 2;

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

    rebElide("append", list, rebR(rebLengthedTextW(buffer, len)), END);

    rebFree(buffer);

  #elif defined(TO_LINUX)

    // Caller should try xdg-open first, then try x-www-browser otherwise
    //
    rebElide(
        "append", list, "[",
            rebT("xdg-open %1"),
            rebT("x-www-browser %1"),
        "]", END
    );

  #else // Just try /usr/bin/open on POSIX, OS X, Haiku, etc.

    rebElide("append", list, rebT("/usr/bin/open %1"), END);

  #endif

    Move_Value(D_OUT, list);
    rebRelease(list);

    return R_OUT;
}


//
//  sleep: native/export [
//
//  "Use system sleep to wait a certain amount of time (doesn't use PORT!s)."
//
//      return: [<opt>]
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

    REBCNT msec = Milliseconds_From_Value(ARG(duration));

#ifdef TO_WINDOWS
    Sleep(msec);
#else
    usleep(msec * 1000);
#endif

    return R_VOID;
}

#if defined(TO_LINUX) || defined(TO_ANDROID) || defined(TO_POSIX) || defined(TO_OSX)
static void kill_process(REBINT pid, REBINT signal);
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
//  new-errors: [
//      terminate-failed: ["terminate failed with error number:" :arg1]
//      permission-denied: ["The process does not have enough permission"]
//      no-process: ["The target process (group) does not exist:" :arg1]
//  ]
//
static REBNATIVE(terminate)
{
    PROCESS_INCLUDE_PARAMS_OF_TERMINATE;

#ifdef TO_WINDOWS
    if (GetCurrentProcessId() == cast(DWORD, VAL_INT32(ARG(pid))))
        fail ("Use QUIT or EXIT-REBOL to terminate current process, instead");

    REBINT err = 0;
    HANDLE ph = OpenProcess(PROCESS_TERMINATE, FALSE, VAL_INT32(ARG(pid)));
    if (ph == NULL) {
        err = GetLastError();
        switch (err) {
        case ERROR_ACCESS_DENIED:
            fail (Error(RE_EXT_PROCESS_PERMISSION_DENIED, END));
        case ERROR_INVALID_PARAMETER:
            fail (Error(RE_EXT_PROCESS_NO_PROCESS, ARG(pid), END));
        default: {
            DECLARE_LOCAL(val);
            Init_Integer(val, err);
            fail (Error(RE_EXT_PROCESS_TERMINATE_FAILED, val, END));
            }
        }
    }

    if (TerminateProcess(ph, 0)) {
        CloseHandle(ph);
        return R_VOID;
    }

    err = GetLastError();
    CloseHandle(ph);
    switch (err) {
        case ERROR_INVALID_HANDLE:
            fail (Error(RE_EXT_PROCESS_NO_PROCESS, ARG(pid), END));
        default: {
            DECLARE_LOCAL(val);
            Init_Integer(val, err);
            fail (Error(RE_EXT_PROCESS_TERMINATE_FAILED, val, END));
         }
    }
#elif defined(TO_LINUX) || defined(TO_ANDROID) || defined(TO_POSIX) || defined(TO_OSX)
    if (getpid() == VAL_INT32(ARG(pid))) {
        // signal is not as reliable for this purpose
        // it's caught in host-main.c as to stop the evaluation
        fail ("Use QUIT or EXIT-REBOL to terminate current process, instead");
    }
    kill_process(VAL_INT32(ARG(pid)), SIGTERM);
    return R_VOID;
#else
    UNUSED(frame_);
    fail ("terminate is not implemented for this platform");
#endif
}


//
//  get-env: native/export [
//
//  {Returns the value of an OS environment variable (for current process).}
//
//      return: [text! blank!]
//          {String the environment variable was set to, or blank if not set}
//      variable [text! word!]
//          {Name of variable to get (case-insensitive in Windows)}
//  ]
//
static REBNATIVE(get_env)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_ENV;

    REBVAL *variable = ARG(variable);

    Check_Security(Canon(SYM_ENVR), POL_READ, variable);

    REBCTX *error = NULL;

  #ifdef TO_WINDOWS
    // Note: The Windows variant of this API is NOT case-sensitive

    WCHAR *key = rebSpellAllocW(variable, END);

    DWORD val_len_plus_one = GetEnvironmentVariable(key, NULL, 0);
    if (val_len_plus_one == 0) { // some failure...
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND)
            Init_Blank(D_OUT);
        else
            error = Error_User("Unknown error when requesting variable size");
    }
    else {
        WCHAR *val = rebAllocN(WCHAR, val_len_plus_one);
        DWORD result = GetEnvironmentVariable(key, val, val_len_plus_one);
        if (result == 0)
            error = Error_User("Unknown error fetching variable to buffer");
        else {
            REBVAL *temp = rebLengthedTextW(val, val_len_plus_one - 1);
            Move_Value(D_OUT, temp);
            rebRelease(temp);
        }
        rebFree(val);
    }

    rebFree(key);
  #else
    // Note: The Posix variant of this API is case-sensitive

    char *key = rebSpellAlloc(variable, END);

    const char* val = getenv(key);
    if (val == NULL) // key not present in environment
        Init_Blank(D_OUT);
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

    return R_OUT;
}


//
//  set-env: native/export [
//
//  {Sets value of operating system environment variable for current process.}
//
//      return: [<opt>]
//      variable [text! word!]
//          "Variable to set (case-insensitive in Windows)"
//      value [text! blank!]
//          "Value to set the variable to, or a BLANK! to unset it"
//  ]
//
static REBNATIVE(set_env)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_ENV;

    REBVAL *variable = ARG(variable);
    REBVAL *value = ARG(value);

    Check_Security(Canon(SYM_ENVR), POL_WRITE, variable);

  #ifdef TO_WINDOWS
    WCHAR *key_wide = rebSpellAllocW(variable, END);
    WCHAR *val_wide = rebSpellAllocW(
        "opt ensure [text! blank!]", value, END
    ); // may be NULL if blank! input, which will unset the envionment var

    if (not SetEnvironmentVariable(key_wide, val_wide))
        fail ("environment variable couldn't be modified");

    rebFree(val_wide);
    rebFree(key_wide);
  #else
    char *key_utf8 = rebSpellAlloc(variable, END);

    if (IS_BLANK(value)) {
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
        char *val_utf8 = rebSpellAlloc(value, END);

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

        char *key_equals_val_utf8 = rebSpellAlloc(
            "unspaced [", variable, "{=}", value, "]", END
        );

        if (putenv(key_equals_val_utf8) == -1) // !!! why mutable?  :-/
            fail ("putenv() couldn't set environment variable");

        /* rebFree(key_equals_val_utf8); */ // !!! Can't!  Crashes getenv()
        rebUnmanage(key_equals_val_utf8); // oh well, have to leak it
      #endif
    }

    rebFree(key_utf8);
  #endif

    return R_VOID;
}


//
//  list-env: native/export [
//
//  {Returns a map of OS environment variables (for current process).}
//
//      ; No arguments
//  ]
//
static REBNATIVE(list_env)
{
  #ifdef TO_WINDOWS
    //
    // Windows environment strings are sequential null-terminated strings,
    // with a 0-length string signaling end ("keyA=valueA\0keyB=valueB\0\0")
    // We count the strings to know how big an array to make, and then
    // convert the array into a MAP!.
    //
    // !!! Adding to a map as we go along would probably be better.

    WCHAR *env = GetEnvironmentStrings();

    REBCNT num_pairs = 0;
    const WCHAR *key_equals_val = env;
    REBCNT len;
    while ((len = wcslen(key_equals_val)) != 0) {
        ++num_pairs;
        key_equals_val += len + 1; // next
    }

    REBARR *array = Make_Array(num_pairs * 2); // we split the keys and values

    key_equals_val = env;
    while ((len = wcslen(key_equals_val)) != 0) {
        const WCHAR *eq_pos = wcschr(key_equals_val, '=');

        REBVAL *key = rebLengthedTextW(
            key_equals_val,
            eq_pos - key_equals_val
        );
        REBVAL *val = rebLengthedTextW(
            eq_pos + 1,
            len - (eq_pos - key_equals_val) - 1
        );
        Append_Value(array, key);
        Append_Value(array, val);
        rebRelease(key);
        rebRelease(val);

        key_equals_val += len + 1; // next
    }

    FreeEnvironmentStrings(env);

    REBMAP *map = Mutate_Array_Into_Map(array);
    Init_Map(D_OUT, map);

    return R_OUT;
  #else
    // Note: 'environ' is an extern of a global found in <unistd.h>, and each
    // entry contains a `key=value` formatted string.
    //
    // https://stackoverflow.com/q/3473692/
    //
    REBCNT num_pairs = 0;
    REBCNT n;
    for (n = 0; environ[n] != NULL; ++n)
        ++num_pairs;

    REBARR *array = Make_Array(num_pairs * 2); // we split the keys and values

    for (n = 0; environ[n] != NULL; ++n) {
        //
        // Note: it's safe to search for just a `=` byte, since the high bit
        // isn't set...and even if the key contains UTF-8 characters, there
        // won't be any occurrences of such bytes in multi-byte-characters.
        //
        const char *key_equals_val = environ[n];
        const char *eq_pos = strchr(key_equals_val, '=');

        REBCNT size = strlen(key_equals_val);

        REBVAL *key = rebSizedText(
            key_equals_val,
            eq_pos - key_equals_val
        );
        REBVAL *val = rebSizedText(
            eq_pos + 1,
            size - (eq_pos - key_equals_val) - 1
        );
        Append_Value(array, key);
        Append_Value(array, val);
        rebRelease(key);
        rebRelease(val);
    }

    REBMAP *map = Mutate_Array_Into_Map(array);
    Init_Map(D_OUT, map);

    return R_OUT;
  #endif
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
static REBNATIVE(get_pid)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_PID;

    Init_Integer(D_OUT, getpid());

    return R_OUT;
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
static REBNATIVE(get_uid)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_UID;

    Init_Integer(D_OUT, getuid());

    return R_OUT;
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
static REBNATIVE(get_euid)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_EUID;

    Init_Integer(D_OUT, geteuid());

    return R_OUT;
}

//
//  get-gid: native [
//
//  "Get real group ID of the process"
//
//      return: [integer!]
//
//  ]
//  platforms: [linux android posix osx]
//
static REBNATIVE(get_gid)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_UID;

    Init_Integer(D_OUT, getgid());

    return R_OUT;
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
static REBNATIVE(get_egid)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_EUID;

    Init_Integer(D_OUT, getegid());

    return R_OUT;
}



//
//  set-uid: native [
//
//  "Set real user ID of the process"
//
//      return: [<opt>]
//      uid [integer!]
//          {The effective user ID}
//  ]
//  new-errors: [
//      invalid-uid: ["User id is invalid or not supported:" :arg1]
//      set-uid-failed: ["set-uid failed with error number:" :arg1]
//  ]
//  platforms: [linux android posix osx]
//
static REBNATIVE(set_uid)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_UID;

    if (setuid(VAL_INT32(ARG(uid))) < 0) {
        switch (errno) {
        case EINVAL:
            fail (Error(RE_EXT_PROCESS_INVALID_UID, ARG(uid), END));

        case EPERM:
            fail (Error(RE_EXT_PROCESS_PERMISSION_DENIED, END));

        default: {
            DECLARE_LOCAL(err);
            Init_Integer(err, errno);
            fail (Error(RE_EXT_PROCESS_SET_UID_FAILED, err, END)); }
        }
    }

    return R_VOID;
}



//
//  set-euid: native [
//
//  "Get effective user ID of the process"
//
//      return: [<opt>]
//      euid [integer!]
//          {The effective user ID}
//  ]
//  new-errors: [
//      invalid-euid: ["user id is invalid or not supported:" :arg1]
//      set-euid-failed: ["set-euid failed with error number:" :arg1]
//  ]
//  platforms: [linux android posix osx]
//
static REBNATIVE(set_euid)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_EUID;

    if (seteuid(VAL_INT32(ARG(euid))) < 0) {
        switch (errno) {
        case EINVAL:
            fail (Error(RE_EXT_PROCESS_INVALID_EUID, ARG(euid), END));

        case EPERM:
            fail (Error(RE_EXT_PROCESS_PERMISSION_DENIED, END));

        default: {
            DECLARE_LOCAL(err);
            Init_Integer(err, errno);
            fail (Error(RE_EXT_PROCESS_SET_EUID_FAILED, err, END)); }
        }
    }

    return R_VOID;
}



//
//  set-gid: native [
//
//  "Set real group ID of the process"
//
//      return: [<opt>]
//      gid [integer!]
//          {The effective group ID}
//  ]
//  new-errors: [
//      invalid-gid: ["group id is invalid or not supported:" :arg1]
//      set-gid-failed: ["set-gid failed with error number:" :arg1]
//  ]
//  platforms: [linux android posix osx]
//
static REBNATIVE(set_gid)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_GID;

    if (setgid(VAL_INT32(ARG(gid))) < 0) {
        switch (errno) {
        case EINVAL:
            fail (Error(RE_EXT_PROCESS_INVALID_GID, ARG(gid), END));

        case EPERM:
            fail (Error(RE_EXT_PROCESS_PERMISSION_DENIED, END));

        default: {
            DECLARE_LOCAL(err);
            Init_Integer(err, errno);
            fail (Error(RE_EXT_PROCESS_SET_GID_FAILED, err, END)); }
        }
    }

    return R_VOID;
}



//
//  set-egid: native [
//
//  "Get effective group ID of the process"
//
//      return: [<opt>]
//      egid [integer!]
//          {The effective group ID}
//  ]
//  new-errors: [
//      invalid-egid: ["group id is invalid or not supported:" :arg1]
//      set-egid-failed: ["set-egid failed with error number:" :arg1]
//  ]
//  platforms: [linux android posix osx]
//
static REBNATIVE(set_egid)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_EGID;

    if (setegid(VAL_INT32(ARG(egid))) < 0) {
        switch (errno) {
        case EINVAL:
            fail (Error(RE_EXT_PROCESS_INVALID_EGID, ARG(egid), END));

        case EPERM:
            fail (Error(RE_EXT_PROCESS_PERMISSION_DENIED, END));

        default: {
            DECLARE_LOCAL(err);
            Init_Integer(err, errno);
            fail (Error(RE_EXT_PROCESS_SET_EGID_FAILED, err, END)); }
        }
    }

    return R_VOID;
}



static void kill_process(REBINT pid, REBINT signal)
{
    if (kill(pid, signal) < 0) {
        DECLARE_LOCAL(arg1);

        switch (errno) {
        case EINVAL:
            Init_Integer(arg1, signal);
            fail (Error(RE_EXT_PROCESS_INVALID_SIGNAL, arg1, END));

        case EPERM:
            fail (Error(RE_EXT_PROCESS_PERMISSION_DENIED, END));

        case ESRCH:
            Init_Integer(arg1, pid);
            fail (Error(RE_EXT_PROCESS_NO_PROCESS, arg1, END));

        default:
            Init_Integer(arg1, errno);
            fail (Error(RE_EXT_PROCESS_SEND_SIGNAL_FAILED, arg1, END));
        }
    }
}


//
//  send-signal: native [
//
//  "Send signal to a process"
//
//      return: [<opt>]
//      pid [integer!]
//          {The process ID}
//      signal [integer!]
//          {The signal number}
//  ]
//  new-errors: [
//      invalid-signal: ["An invalid signal is specified:" :arg1]
//      send-signal-failed: ["send-signal failed with error number:" :arg1]
//  ]
//  platforms: [linux android posix osx]
//
static REBNATIVE(send_signal)
{
    PROCESS_INCLUDE_PARAMS_OF_SEND_SIGNAL;

    kill_process(VAL_INT32(ARG(pid)), VAL_INT32(ARG(signal)));

    return R_VOID;
}
#endif // defined(TO_LINUX) || defined(TO_ANDROID) || defined(TO_POSIX) || defined(TO_OSX)



#include "tmp-mod-process-last.h"
