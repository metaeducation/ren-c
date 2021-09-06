//
//  File: %call-windows.c
//  Summary: "Implemention of CALL native for Windows"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 Atronix Engineering
// Copyright 2012-2020 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except inbuf compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Windows has no clear standard on when piped processes return UTF-16 vs.
// ASCII, or UTF-8, or anything else.  It's just a pipe.  What programs do
// in general (including Rebol) is detect if they are hooked to a console
// with `GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_CHAR`.
// If so, they send UTF-16.
//
// https://docs.microsoft.com/en-us/windows/desktop/api/fileapi/nf-fileapi-getfiletype
//
// If you call CMD.EXE itself and as it to perform a shell function, such as
// say `ECHO`, it will default to giving back ASCII.  This can be overridden
// with `CMD.EXE /U` ("when piped or redirected, gives "UCS-2 little endian")
//
// Given Windows itself wishing to set the standard for pipes and redirects
// to use plain bytes, it seems good to go with it.  Rather than give the
// appearance of endorsement of UCS-2/UTF-16 by offering a switch for it,
// a process returning it may be handled by requesting BINARY! output and
// then doing the conversion themselves.
//

#define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
#include <windows.h>

#include <process.h>
#include <shlobj.h>

#ifdef IS_ERROR
    #undef IS_ERROR  // %winerror.h defines, Rebol has a different meaning
#endif

#include "sys-core.h"

#include "tmp-mod-process.h"

#include "reb-process.h"


//
//  Try_Init_Startupinfo_Sink: C
//
// Output and Error code is nearly identical and is factored into a
// subroutine.
//
// Note: If it returns `false` GetLastError() is tested to return a message,
// so the caller assumes the Windows error state is meaningful upon return.
//
static bool Try_Init_Startupinfo_Sink(
    HANDLE *hsink,  // will be set, is either &si.hStdOutput, &si.hStdError
    HANDLE *hwrite,  // set to match `hsink` unless hsink doesn't need closing
    HANDLE *hread,  // write may have "read" side if pipe captures variables
    DWORD std_handle_id,  // e.g. STD_OUTPUT_HANDLE, STD_ERROR_HANDLE
    const REBVAL *arg  // argument e.g. /OUTPUT or /ERROR for behavior
){
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;

    assert(*hread == 0 and *hwrite == 0);
    *hsink = INVALID_HANDLE_VALUE;  // this function must set unless error

    if (IS_NULLED(arg)) {  // write normally (usually to console)
        *hsink = GetStdHandle(std_handle_id);
    }
    else switch (VAL_TYPE(arg)) {
      case REB_LOGIC:
        if (VAL_LOGIC(arg)) {
            //
            // !!! This said true was "inherit", but hwrite was not being
            // set to anything...?  So is this supposed to be able to deal
            // with shell-based redirection of the parent in a way that is
            // distinct, saying if the r3 process was redirected to a file
            // then the default is *not* to also redirect the child?  There
            // was no comment on this.
            //
            if (not SetHandleInformation(
                *hwrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT
            )){
                return false;
            }
            *hsink = *hwrite;
        }
        else {
            // Not documented, but this is how to make a /dev/null on Windows
            // https://stackoverflow.com/a/25609668
            //
            *hwrite = CreateFile(
                L"NUL",
                GENERIC_WRITE,
                0,
                &sa,  // just says inherithandles = true
                OPEN_EXISTING,
                0,
                NULL
            );
            if (*hwrite == INVALID_HANDLE_VALUE)
                return false;
            *hsink = *hwrite;
        break; }

      case REB_TEXT:  // write to pre-existing TEXT!
      case REB_BINARY:  // write to pre-existing BINARY!
        if (not CreatePipe(hread, hwrite, NULL, 0))
            return false;

        // make child side handle inheritable
        //
        if (not SetHandleInformation(
            *hwrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT
        )){
            return false;
        }
        *hsink = *hwrite;
        break;

      case REB_FILE: {  // write to file
        WCHAR *local_wide = rebSpellWide("file-to-local @", arg);

        // !!! This was done in two steps, is this necessary?

        *hwrite = CreateFile(
            local_wide,
            GENERIC_WRITE,  // desired mode
            0,  // shared mode
            &sa,  // security attributes
            CREATE_NEW,  // creation disposition
            FILE_ATTRIBUTE_NORMAL,  // flag and attributes
            nullptr  // template
        );

        if (
            *hwrite == INVALID_HANDLE_VALUE
            and GetLastError() == ERROR_FILE_EXISTS
        ){
            *hwrite = CreateFile(
                local_wide,
                GENERIC_WRITE,  // desired mode
                0,  // shared mode
                &sa,  // security attributes
                OPEN_EXISTING,  // creation disposition
                FILE_ATTRIBUTE_NORMAL,  // flag and attributes
                nullptr  // template
            );
        }
        rebFree(local_wide);

        if (*hwrite == INVALID_HANDLE_VALUE)
            return false;

        *hsink = *hwrite;
        break; }

      default:
        panic (arg);  // CALL's type checking should have screened the types
    }

    assert(*hsink != INVALID_HANDLE_VALUE);
    assert(*hwrite == 0 or *hwrite == *hsink);

    return true;  // succeeded
}


//
//  Call_Core: C
//
REB_R Call_Core(REBFRM *frame_) {
    PROCESS_INCLUDE_PARAMS_OF_CALL_INTERNAL_P;

    UNUSED(REF(console));  // !!! This is not paid attention to (?)

    // Make sure that if the output or error series are STRING! or BINARY!,
    // they are not read-only, before we try appending to them.
    //
    if (IS_TEXT(ARG(output)) or IS_BINARY(ARG(output)))
        ENSURE_MUTABLE(ARG(output));
    if (IS_TEXT(ARG(error)) or IS_BINARY(ARG(error)))
        ENSURE_MUTABLE(ARG(error));

    bool flag_wait;
    if (
        REF(wait)
        or (
            IS_TEXT(ARG(input)) or IS_BINARY(ARG(input))
            or IS_TEXT(ARG(output)) or IS_BINARY(ARG(output))
            or IS_TEXT(ARG(error)) or IS_BINARY(ARG(error))
        )  // I/O redirection implies /WAIT
    ){
        flag_wait = true;
    }
    else
        flag_wait = false;

    // We synthesize the argc and argv from the "command".  This does dynamic
    // allocations of argc strings through the API, which need to be freed
    // before we return.
    //
    REBWCHAR *cmd;
    int argc;
    const REBWCHAR **argv;

    if (IS_TEXT(ARG(command))) {  // Windows takes command-lines by default

      text_command:

        if (REF(shell)) {
            //
            // Do not pass /U for UCS-2, see notes at top of file.
            //
            // !!! This seems it would be better to be detected and done in
            // usermode, perhaps as an AUGMENT on platforms that wish to offer
            // the facility.
            //
            cmd = rebSpellWide(
                "unspaced [{cmd.exe /C \"}", ARG(command), "{\"}]"
            );
        }
        else {
            cmd = rebSpellWide(ARG(command));
        }

        argc = 1;
        argv = rebAllocN(const REBWCHAR*, (argc + 1));

        // !!! Make two copies because it frees cmd and all the argv.  Review.
        //
        argv[0] = rebSpellWide(ARG(command));
        argv[1] = nullptr;
    }
    else if (IS_BLOCK(ARG(command))) {
        //
        // In order for argv-call to work with Windows reliably, it has to do
        // proper escaping of its arguments when forming a string.  We
        // do this with a usermode helper.
        //
        // https://github.com/rebol/rebol-issues/issues/2225

        REBVAL *text = rebValue("argv-block-to-command*", ARG(command));
        Copy_Cell(RESET(ARG(command)), text);
        rebRelease(text);
        goto text_command;
    }
    else
        fail (PAR(command));

    REBU64 pid = 1020;  // avoid uninitialized warning, garbage value
    DWORD exit_code = 304;  // ...same...

    REBINT result = -1;
    REBINT ret = 0;

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

    // We don't want to close standard handles.  So we only close handles that
    // *we open* with CreateFile.  So we track this separate list of handles
    // rather than using the ones in the STARTUPINFO to know to close them.
    // https://devblogs.microsoft.com/oldnewthing/20130307-00/?p=5033
    //
    HANDLE hOutputRead = 0;
    HANDLE hOutputWrite = 0;
    HANDLE hInputWrite = 0;
    HANDLE hInputRead = 0;
    HANDLE hErrorWrite = 0;
    HANDLE hErrorRead = 0;

    UNUSED(REF(info));

    char *inbuf = nullptr;
    size_t inbuf_size = 0;
    char *outbuf = nullptr;
    size_t outbuf_used = 0;
    char *errbuf = nullptr;
    size_t errbuf_used = 0;

    //=//// INPUT SOURCE SETUP ////////////////////////////////////////////=//

    if (not REF(input)) {  // get stdin normally (usually from user console)
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    else switch (VAL_TYPE(ARG(input))) {
      case REB_LOGIC:
        if (VAL_LOGIC(ARG(input))) {  // !!! make inheritable (correct?)
            if (not SetHandleInformation(
                hInputRead,
                HANDLE_FLAG_INHERIT,
                HANDLE_FLAG_INHERIT
            )){
                goto stdin_error;
            }
            si.hStdInput = hInputRead;
        }
        else {
            // Not documented, but this is how to make a /dev/null on Windows
            // https://stackoverflow.com/a/25609668
            //
            si.hStdInput = hInputRead = CreateFile(
                L"NUL",
                GENERIC_READ,
                0,
                &sa,  // just says inherithandles = true
                OPEN_EXISTING,
                0,
                NULL
             );  // don't offer any stdin
        break; }
      case REB_TEXT: {  // feed standard input from TEXT!
        //
        // See notes at top of file about why UTF-16/UCS-2 are not used here.
        // Pipes and file redirects are generally understood in Windows to
        // *not* use those encodings, and transmit raw bytes.
        //
        inbuf_size = rebSpellInto(nullptr, 0, ARG(input));
        inbuf = rebAllocN(char, inbuf_size + 1);
        size_t check = rebSpellInto(inbuf, inbuf_size, ARG(input));
        assert(check == inbuf_size);
        UNUSED(check);
        goto input_via_buffer; }

      case REB_BINARY:  // feed standard input from BINARY! (full-band)
        inbuf = s_cast(rebBytes(&inbuf_size, ARG(input)));

      input_via_buffer:

        if (not CreatePipe(&hInputRead, &hInputWrite, nullptr, 0))
            goto stdin_error;

        if (not SetHandleInformation(
            hInputRead,  // make child side handle inheritable
            HANDLE_FLAG_INHERIT,
            HANDLE_FLAG_INHERIT
        )){
            goto stdin_error;
        }
        si.hStdInput = hInputRead;
        break;

      case REB_FILE: {  // feed standard input from file contents
        WCHAR *local_wide = rebSpellWide("file-to-local", ARG(input));

        hInputRead = CreateFile(
            local_wide,
            GENERIC_READ,  // desired mode
            0,  // shared mode
            &sa,  // security attributes
            OPEN_EXISTING,  // creation disposition
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,  // flags
            nullptr  // template
        );
        si.hStdInput = hInputRead;

        rebFree(local_wide);

        inbuf = nullptr;
        inbuf_size = 0;
        break; }

      default:
        panic (ARG(input));
    }

    //=//// OUTPUT SINK SETUP /////////////////////////////////////////////=//

    if (not Try_Init_Startupinfo_Sink(
        &si.hStdOutput,
        &hOutputWrite,
        &hOutputRead,
        STD_OUTPUT_HANDLE,
        ARG(output)
    )){
        goto stdout_error;
    }

    //=//// ERROR SINK SETUP ////./////////////////////////////////////////=//

    if (not Try_Init_Startupinfo_Sink(
        &si.hStdError,
        &hErrorWrite,
        &hErrorRead,
        STD_ERROR_HANDLE,
        ARG(error)
    )){
        goto stderr_error;
    }

    //=//// COMMAND AND ARGUMENTS SETUP ///////////////////////////////////=//

    PROCESS_INFORMATION pi;
    result = CreateProcess(
        nullptr,  // executable name
        cmd,  // command to execute
        nullptr,  // process security attributes
        nullptr,  // thread security attributes
        TRUE,  // inherit handles, must be TRUE for I/O redirection
        NORMAL_PRIORITY_CLASS | CREATE_DEFAULT_ERROR_MODE,  // creation flags
        nullptr,  // environment
        nullptr,  // current directory
        &si,  // startup information
        &pi  // process information
    );

    rebFree(cmd);

    pid = pi.dwProcessId;

    if (hInputRead != nullptr)
        CloseHandle(hInputRead);

    if (hOutputWrite != nullptr)
        CloseHandle(hOutputWrite);

    if (hErrorWrite != nullptr)
        CloseHandle(hErrorWrite);

    if (result != 0 and flag_wait) {  // Wait for termination
        HANDLE handles[4];
        int count = 0;
        DWORD outbuf_capacity = 0;
        DWORD errbuf_capacity = 0;

        // The original Atronix code only waited on the file handles, and then
        // waited on the process after the handle wait loop.  But it seems to
        // be cleaner if we add the process handle to the waited-on set, so
        // that process exit will break the wait loop.
        //
        handles[count++] = pi.hProcess;

        if (hInputWrite and inbuf_size > 0) {
            handles[count++] = hInputWrite;
        }
        if (hOutputRead != NULL) {
            outbuf_capacity = BUF_SIZE_CHUNK;
            outbuf_used = 0;

            outbuf = rebAllocN(char, outbuf_capacity);
            handles[count++] = hOutputRead;
        }
        if (hErrorRead != NULL) {
            errbuf_capacity = BUF_SIZE_CHUNK;
            errbuf_used = 0;

            errbuf = rebAllocN(char, errbuf_capacity);
            handles[count++] = hErrorRead;
        }

        DWORD inbuf_pos = 0;

        while (count > 0) {
            DWORD wait_result = WaitForMultipleObjects(
                count, handles, FALSE, INFINITE
            );

            // If we test wait_result >= WAIT_OBJECT_0 it will tell us "always
            // true" with -Wtype-limits, since WAIT_OBJECT_0 is 0.  Take that
            // comparison out, but add assert in case you're on an abstracted
            // Windows and it isn't 0 for that implementation.
            //
            assert(WAIT_OBJECT_0 == 0);
            if (wait_result < WAIT_OBJECT_0 + count) {
                int i = wait_result - WAIT_OBJECT_0;
                DWORD n = 0;

                if (handles[i] == pi.hProcess) {
                    //
                    // Process terminated.
                    //
                    GetExitCodeProcess(pi.hProcess, &exit_code);
                    CloseHandle(pi.hThread);
                    CloseHandle(pi.hProcess);
                    goto cleanup;
                }
                else if (handles[i] == hInputWrite) {
                    if (not WriteFile(
                        hInputWrite,
                        inbuf + inbuf_pos,
                        inbuf_size - inbuf_pos,
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
                        inbuf_pos += n;
                        if (inbuf_pos >= inbuf_size) {  // done with input
                            //
                            // If we close the handle of the pipe we are using
                            // to provide input to the child before it is done
                            // reading the data we wrote, it will get an
                            // ERROR_BROKEN_PIPE.  So hopefully the client has
                            // looked at the data it has already read and
                            // decided it does not need more...because if it
                            // reads past the protocol that will happen.
                            //
                            CloseHandle(hInputWrite);
                            hInputWrite = NULL;

                            rebFree(inbuf);
                            inbuf = nullptr;
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
                    if (not ReadFile(
                        hOutputRead,
                        outbuf + outbuf_used,
                        outbuf_capacity - outbuf_used,
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
                        outbuf_used += n;
                        if (outbuf_used >= outbuf_capacity) {
                            outbuf_capacity += BUF_SIZE_CHUNK;
                            outbuf = cast(char*,
                                rebRealloc(outbuf, outbuf_capacity)
                            );
                            if (outbuf == NULL)  // !!! never with rebRealloc
                                goto kill;
                        }
                    }
                }
                else if (handles[i] == hErrorRead) {
                    if (not ReadFile(
                        hErrorRead,
                        errbuf + errbuf_used,
                        errbuf_capacity - errbuf_used,
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
                        errbuf_used += n;
                        if (errbuf_used >= errbuf_capacity) {
                            errbuf_capacity += BUF_SIZE_CHUNK;
                            errbuf = cast(char*,
                                rebRealloc(errbuf, errbuf_capacity)
                            );
                            if (errbuf == NULL)  // !!! never with rebRealloc
                                goto kill;
                        }
                    }
                }
                else {
                    //printf("Error READ");
                    if (ret == 0)
                        ret = GetLastError();
                    goto kill;
                }
            }
            else if (wait_result == WAIT_FAILED) {
                //printf("Wait Failed\n");
                if (ret == 0)
                    ret = GetLastError();
                goto kill;
            }
            else {
                //printf("Wait returns unexpected result: %d\n", wait_result);
                if (ret == 0)
                    ret = GetLastError();
                goto kill;
            }
        }

        assert(!"WaitForMultipleObjects() loop ended but process didn't end");
    }
    else if (result) {
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

        GetExitCodeProcess(pi.hProcess, &exit_code);
    }
    else if (ret == 0) {
        ret = GetLastError();
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

  cleanup:

    if (hInputWrite != nullptr)
        CloseHandle(hInputWrite);

    if (hOutputRead != nullptr)
        CloseHandle(hOutputRead);

    if (hErrorRead != nullptr)
        CloseHandle(hErrorRead);

    if (IS_FILE(ARG(error)))
        CloseHandle(si.hStdError);

  stderr_error:

    if (IS_FILE(ARG(output)))
        CloseHandle(si.hStdOutput);

  stdout_error:

    if (IS_FILE(ARG(input)))
        CloseHandle(si.hStdInput);

  stdin_error:

    // Call may not succeed if r != 0, but we still have to run cleanup
    // before reporting any error...

    assert(argc > 0);

    int i;
    for (i = 0; i != argc; ++i)
        rebFree(m_cast(REBWCHAR*, argv[i]));

    rebFree(m_cast(REBWCHAR**, argv));

    // We can actually recover the rebAlloc'd buffers as BINARY!.  If the
    // target is TEXT!, we DELINE it first to eliminate any CRs.  Note the
    // remarks at the top of file about how piped data is not generally
    // assumed to be UCS-2.
    //
    if (IS_TEXT(ARG(output))) {
        REBVAL *output_val = rebRepossess(outbuf, outbuf_used);
        rebElide("insert", ARG(output), "deline", output_val);
        rebRelease(output_val);
    }
    else if (IS_BINARY(ARG(output))) {
        REBVAL *output_val = rebRepossess(outbuf, outbuf_used);
        rebElide("insert", ARG(output), output_val);
        rebRelease(output_val);
    }
    else
        assert(outbuf == nullptr);

    if (IS_TEXT(ARG(error))) {
        REBVAL *error_val = rebRepossess(errbuf, errbuf_used);
        rebElide("insert", ARG(error), "deline", error_val);
        rebRelease(error_val);
    }
    else if (IS_BINARY(ARG(error))) {
        REBVAL *error_val = rebRepossess(errbuf, errbuf_used);
        rebElide("append", ARG(error), error_val);
        rebRelease(error_val);
    }
    else
        assert(errbuf == nullptr);

    if (inbuf != nullptr)
        rebFree(inbuf);

    if (ret != 0)
        rebFail_OS (ret);

    if (REF(info)) {
        REBCTX *info = Alloc_Context(REB_OBJECT, 2);

        Init_Integer(Append_Context(info, nullptr, Canon(ID)), pid);
        if (REF(wait))
            Init_Integer(
                Append_Context(info, nullptr, Canon(EXIT_CODE)),
                exit_code
            );

        return Init_Object(D_OUT, info);
    }

    // We may have waited even if they didn't ask us to explicitly, but
    // we only return a process ID if /WAIT was not explicitly used
    //
    if (REF(wait))
        return Init_Integer(D_OUT, exit_code);

    return Init_Integer(D_OUT, pid);
}
