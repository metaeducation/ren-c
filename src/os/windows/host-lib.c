//
//  File: %host-lib.c
//  Summary: {OS API function library called by REBOL interpreter}
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
// This module is parsed for function declarations used to
// build prototypes, tables, and other definitions. To change
// function arguments requires a rebuild of the REBOL library.
//
// This module provides the functions that REBOL calls
// to interface to the native (host) operating system.
// REBOL accesses these functions through the structure
// defined in host-lib.h (auto-generated, do not modify).
//
// compile with -DUNICODE for Win32 wide char API
//
//=////////////////////////////////////////////////////////////////////////=//
//
// WARNING: The function declarations here cannot be modified without also
// modifying those found in the other OS host-lib files!  Do not even modify
// the argument names.
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <process.h>
#include <shlobj.h>
#include <assert.h>

#include "reb-host.h"

#ifndef REB_CORE
REBSER* Gob_To_Image(REBGOB *gob);
#endif

//used to detect non-modal OS dialogs
BOOL osDialogOpen = FALSE;


//
//  Convert_Date: C
//
// Convert local format of system time into standard date
// and time structure.
//
void Convert_Date(REBVAL *out, long zone, const SYSTEMTIME *stime)
{
    RL_Init_Date(
        out,
        stime->wYear, // year
        stime->wMonth, // month
        stime->wDay, // day
        stime->wHour * 3600 + stime->wMinute * 60 + stime->wSecond, // "time"
        1000000 * stime->wMilliseconds, // nano
        zone
    );
}

//
//  Insert_Command_Arg: C
//
// Insert an argument into a command line at the %1 position,
// or at the end if there is no %1. (An INSERT action.)
// Do not exceed the specified limit length.
//
// Too bad std Clib does not provide INSERT or REPLACE functions.
//
static void Insert_Command_Arg(REBCHR *cmd, const REBCHR *arg, REBCNT limit)
{
    #define HOLD_SIZE 2000
    wchar_t *spot;
    wchar_t hold[HOLD_SIZE + 4];

    if (wcslen(cmd) >= limit) return; // invalid case, ignore it.

    // Find %1:
    spot = wcsstr(cmd, L"%1");

    if (spot) {
        // Save rest of cmd line (such as end quote, -flags, etc.)
        wcsncpy(hold, spot+2, HOLD_SIZE);

        // Terminate at the arg location:
        spot[0] = 0;

        // Insert the arg:
        wcsncat(spot, arg, limit - wcslen(cmd) - 1);

        // Add back the rest of cmd:
        wcsncat(spot, hold, limit - wcslen(cmd) - 1);
    }
    else {
        wcsncat(cmd, L" ", 1);
        wcsncat(cmd, arg, limit - wcslen(cmd) - 1);
    }
}


/***********************************************************************
**
**  OS Library Functions
**
***********************************************************************/

/* Keep in sync with n-io.c */
#define OS_ENA   -1
#define OS_EINVAL -2
#define OS_EPERM -3
#define OS_ESRCH -4

//
//  OS_Get_PID: C
//
// Return the current process ID
//
REBINT OS_Get_PID()
{
    return GetCurrentProcessId();
}

//
//  OS_Get_UID: C
//
// Return the real user ID
//
REBINT OS_Get_UID()
{
    return OS_ENA;
}

//
//  OS_Set_UID: C
//
// Set the user ID, see setuid manual for its semantics
//
REBINT OS_Set_UID(REBINT uid)
{
    UNUSED(uid);
    return OS_ENA;
}

//
//  OS_Get_GID: C
//
// Return the real group ID
//
REBINT OS_Get_GID()
{
    return OS_ENA;
}

//
//  OS_Set_GID: C
//
// Set the group ID, see setgid manual for its semantics
//
REBINT OS_Set_GID(REBINT gid)
{
    UNUSED(gid);
    return OS_ENA;
}

//
//  OS_Get_EUID: C
//
// Return the effective user ID
//
REBINT OS_Get_EUID()
{
    return OS_ENA;
}

//
//  OS_Set_EUID: C
//
// Set the effective user ID
//
REBINT OS_Set_EUID(REBINT uid)
{
    UNUSED(uid);
    return OS_ENA;
}

//
//  OS_Get_EGID: C
//
// Return the effective group ID
//
REBINT OS_Get_EGID()
{
    return OS_ENA;
}

//
//  OS_Set_EGID: C
//
// Set the effective group ID
//
REBINT OS_Set_EGID(REBINT gid)
{
    UNUSED(gid);
    return OS_ENA;
}

//
//  OS_Send_Signal: C
//
// Send signal to a process
//
REBINT OS_Send_Signal(REBINT pid, REBINT signal)
{
    UNUSED(pid);
    UNUSED(signal);
    return OS_ENA;
}

//
//  OS_Kill: C
//
// Try to kill the process
//
REBINT OS_Kill(REBINT pid)
{
    REBINT err = 0;
    HANDLE ph = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (ph == NULL) {
        err = GetLastError();
        switch (err) {
            case ERROR_ACCESS_DENIED:
                return OS_EPERM;
            case ERROR_INVALID_PARAMETER:
                return OS_ESRCH;
            default:
                return OS_ESRCH;
        }
    }
    if (TerminateProcess(ph, 0)) {
        CloseHandle(ph);
        return 0;
    }
    err = GetLastError();
    CloseHandle(ph);
    switch (err) {
        case ERROR_INVALID_HANDLE:
            return OS_EINVAL;
        default:
            return -err;
    }
}

//
//  OS_Config: C
//
// Return a specific runtime configuration parameter.
//
REBINT OS_Config(int id, REBYTE *result)
{
    UNUSED(result);

#define OCID_STACK_SIZE 1  // needs to move to .h file

    switch (id) {
    case OCID_STACK_SIZE:
        return 0;  // (size in bytes should be returned here)
    }

    return 0;
}


//
//  OS_Exit: C
//
// Called in cases where REBOL needs to quit immediately
// without returning from the main() function.
//
void OS_Exit(int code)
{
    //OS_Call_Device(RDI_STDIO, RDC_CLOSE); // close echo
    OS_Quit_Devices(0);
#ifndef REB_CORE
    OS_Destroy_Graphics();
#endif
    exit(code);
}


//
//  OS_Crash: C
//
// Tell user that REBOL has crashed. This function must use
// the most obvious and reliable method of displaying the
// crash message.
//
// If the title is NULL, then REBOL is running in a server mode.
// In that case, we do not want the crash message to appear on
// the screen, because the system may be unattended.
//
// On some systems, the error may be recorded in the system log.
//
void OS_Crash(const REBYTE *title, const REBYTE *content)
{
    // Echo crash message if echo file is open:
    ///PUTE(content);
    OS_Call_Device(RDI_STDIO, RDC_CLOSE); // close echo

    // A title tells us we should alert the user:
    if (title) {
    //  OS_Put_Str(title);
    //  OS_Put_Str(":\n");
        // Use ASCII only
        MessageBoxA(NULL, cs_cast(content), cs_cast(title), MB_ICONHAND);
    }
    //  OS_Put_Str(content);
    exit(100);
}


//
//  OS_Form_Error: C
//
// Translate OS error into a string. The str is the string
// buffer and the len is the length of the buffer.
//
REBCHR *OS_Form_Error(int errnum, REBCHR *str, int len)
{
    wchar_t *lpMsgBuf;
    int ok;

    if (!errnum) errnum = GetLastError();

    // !!! Why does this allocate a buffer when FormatMessage takes a
    // buffer and a size...exactly the interface we're implementing?
    ok = FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            errnum,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
            cast(wchar_t*, &lpMsgBuf), // see FORMAT_MESSAGE_ALLOCATE_BUFFER
            0,
            NULL);

    len--; // termination

    if (!ok) wcsncpy(str, L"unknown error", len);
    else {
        wcsncpy(str, lpMsgBuf, len);
        LocalFree(lpMsgBuf);
    }
    return str;
}


//
//  OS_Get_Boot_Path: C
//
// Used to determine the program file path for REBOL.
// This is the path stored in system->options->boot and
// it is used for finding default boot files.
//
REBOOL OS_Get_Boot_Path(REBCHR *name)
{
    return (GetModuleFileName(0, name, MAX_FILE_NAME) > 0);
}


//
//  OS_Get_Locale: C
//
// Used to obtain locale information from the system.
// The returned value must be freed with OS_FREE_MEM.
//
REBCHR *OS_Get_Locale(int what)
{
    LCTYPE type;
    int len;
    wchar_t *data;
    LCTYPE types[] = {
        LOCALE_SENGLANGUAGE,
        LOCALE_SNATIVELANGNAME,
        LOCALE_SENGCOUNTRY,
        LOCALE_SCOUNTRY,
    };

    type = types[what];

    len = GetLocaleInfo(0, type, 0, 0);
    data = OS_ALLOC_N(wchar_t, len);
    len = GetLocaleInfo(0, type, data, len);

    return data;
}


//
//  OS_Get_Env: C
//
// Get a value from the environment.
// Returns size of retrieved value for success or zero if missing.
// If return size is greater than valsize then value contents
// are undefined, and size includes null terminator of needed buf
//
REBINT OS_Get_Env(REBCHR *envname, REBCHR* envval, REBINT valsize)
{
    // Note: The Windows variant of this API is NOT case-sensitive

    REBINT result = GetEnvironmentVariable(envname, envval, valsize);
    if (result == 0) { // some failure...
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
            return 0; // not found
        }
        return -1; // other error
    }
    return result;
}


//
//  OS_Set_Env: C
//
// Set a value from the environment.
// Returns >0 for success and 0 for errors.
//
REBOOL OS_Set_Env(REBCHR *envname, REBCHR *envval)
{
    return SetEnvironmentVariable(envname, envval);
}


//
//  OS_List_Env: C
//
REBCHR *OS_List_Env(void)
{
    wchar_t *env = GetEnvironmentStrings();
    REBCNT n, len = 0;
    wchar_t *str;

    str = env;
    while ((n = wcslen(str))) {
        len += n + 1;
        str = env + len; // next
    }
    len++;

    str = OS_ALLOC_N(wchar_t, len);
    memmove(str, env, len * sizeof(wchar_t));

    FreeEnvironmentStrings(env);

    return str;
}


//
//  OS_Get_Time: C
//
// Get the current system date/time in UTC plus zone offset (mins).
//
void OS_Get_Time(REBVAL *out)
{
    SYSTEMTIME stime;
    TIME_ZONE_INFORMATION tzone;

    GetSystemTime(&stime);

    if (TIME_ZONE_ID_DAYLIGHT == GetTimeZoneInformation(&tzone))
        tzone.Bias += tzone.DaylightBias;

    Convert_Date(out, -tzone.Bias, &stime);
}


//
//  OS_Delta_Time: C
//
// Return time difference in microseconds. If base = 0, then
// return the counter. If base != 0, compute the time difference.
//
// Note: Requires high performance timer.
//      Q: If not found, use timeGetTime() instead ?!
//
i64 OS_Delta_Time(i64 base, int flags)
{
    UNUSED(flags);

    LARGE_INTEGER freq;
    LARGE_INTEGER time;

    if (!QueryPerformanceCounter(&time))
        OS_Crash(cb_cast("Missing resource"), cb_cast("High performance timer"));

    if (base == 0) return time.QuadPart; // counter (may not be time)

    QueryPerformanceFrequency(&freq);

    return ((time.QuadPart - base) * 1000) / (freq.QuadPart / 1000);
}


//
//  OS_Get_Current_Dir: C
//
// Return the current directory path as a string and
// its length in chars (not bytes).
//
// The result should be freed after copy/conversion.
//
int OS_Get_Current_Dir(REBCHR **path)
{
    int len;

    len = GetCurrentDirectory(0, NULL); // length, incl terminator.
    *path = OS_ALLOC_N(wchar_t, len);
    GetCurrentDirectory(len, *path);
    len--; // less terminator

    return len;
}


//
//  OS_Set_Current_Dir: C
//
// Set the current directory to local path. Return FALSE
// on failure.
//
REBOOL OS_Set_Current_Dir(REBCHR *path)
{
    return SetCurrentDirectory(path);
}


//
//  OS_File_Time: C
//
// Convert file.time to REBOL date/time format.
// Time zone is UTC.
//
void OS_File_Time(REBVAL *out, struct devreq_file *file)
{
    SYSTEMTIME stime;
    TIME_ZONE_INFORMATION tzone;

    if (TIME_ZONE_ID_DAYLIGHT == GetTimeZoneInformation(&tzone))
        tzone.Bias += tzone.DaylightBias;

    FileTimeToSystemTime(cast(FILETIME *, &file->time), &stime);
    Convert_Date(out, -tzone.Bias, &stime);
}


//
//  OS_Open_Library: C
//
// Load a DLL library and return the handle to it.
// If zero is returned, error indicates the reason.
//
void *OS_Open_Library(const REBCHR *path, REBCNT *error)
{
    void *dll = LoadLibraryW(path);
    *error = GetLastError();

    return dll;
}


//
//  OS_Close_Library: C
//
// Free a DLL library opened earlier.
//
void OS_Close_Library(void *dll)
{
    FreeLibrary((HINSTANCE)dll);
}


//
//  OS_Find_Function: C
//
// Get a DLL function address from its string name.
//
CFUNC *OS_Find_Function(void *dll, const char *funcname)
{
    // !!! See notes about data pointers vs. function pointers in the
    // definition of CFUNC.  This is trying to stay on the right side
    // of the specification, but OS APIs often are not standard C.  So
    // this implementation is not guaranteed to work, just to suppress
    // compiler warnings.  See:
    //
    //      http://stackoverflow.com/a/1096349/211160

    FARPROC fp = GetProcAddress((HMODULE)dll, funcname);

    //DWORD err = GetLastError();

    return cast(CFUNC*, fp);
}


//
//  OS_Create_Process: C
//
// Return -1 on error.
// For right now, set flags to 1 for /wait.
//
int OS_Create_Process(
    const REBCHR *call,
    int argc,
    const REBCHR* argv[],
    u32 flags, u64 *pid,
    int *exit_code,
    u32 input_type,
    char *input,
    u32 input_len,
    u32 output_type,
    char **output,
    u32 *output_len,
    u32 err_type,
    char **err,
    u32 *err_len
) {
    UNUSED(argc);
    UNUSED(argv);

#define INHERIT_TYPE 0
#define NONE_TYPE 1
#define STRING_TYPE 2
#define FILE_TYPE 3
#define BINARY_TYPE 4

#define FLAG_WAIT 1
#define FLAG_CONSOLE 2
#define FLAG_SHELL 4
#define FLAG_INFO 8

    STARTUPINFO         si;
    PROCESS_INFORMATION pi;
//  REBOOL              is_NT;
//  OSVERSIONINFO       info;
    REBINT              result = -1;
    REBINT              ret = 0;
    HANDLE hOutputRead = 0, hOutputWrite = 0;
    HANDLE hInputWrite = 0, hInputRead = 0;
    HANDLE hErrorWrite = 0, hErrorRead = 0;
    wchar_t *cmd = NULL;
    char *oem_input = NULL;

    SECURITY_ATTRIBUTES sa;

    unsigned char flag_wait = FALSE;
    unsigned char flag_console = FALSE;
    unsigned char flag_shell = FALSE;
    unsigned char flag_info = FALSE;
    (void)flag_info; // Suppress -Wunused-but-set-variable
    (void)flag_console; // Suppress -Wunused-but-set-variable

    if (flags & FLAG_WAIT) flag_wait = TRUE;
    if (flags & FLAG_CONSOLE) flag_console = TRUE;
    if (flags & FLAG_SHELL) flag_shell = TRUE;
    if (flags & FLAG_INFO) flag_info = TRUE;

    // Set up the security attributes struct.
    sa.nLength= sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    si.cb = sizeof(si);
    si.lpReserved = NULL;
    si.lpDesktop = NULL;
    si.lpTitle = NULL;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.wShowWindow = SW_SHOWNORMAL;
    si.cbReserved2 = 0;
    si.lpReserved2 = NULL;

//  GetVersionEx(&info);
//  is_NT = info.dwPlatformId >= VER_PLATFORM_WIN32_NT;

    /* initialize output/error */
    if (output_type != NONE_TYPE
        && output_type != INHERIT_TYPE
        && (output == NULL
            || output_len == NULL)) {
        return -1;
    }
    if (output != NULL) *output = NULL;
    if (output_len != NULL) *output_len = 0;

    if (err_type != NONE_TYPE
        && err_type != INHERIT_TYPE
        && (err == NULL
            || err_len == NULL)) {
        return -1;
    }
    if (err != NULL) *err = NULL;
    if (err_len != NULL) *err_len = 0;

    switch (input_type) {
        case STRING_TYPE:
        case BINARY_TYPE:
            if (!CreatePipe(&hInputRead, &hInputWrite, NULL, 0)) {
                goto input_error;
            }
            /* make child side handle inheritable */
            if (!SetHandleInformation(hInputRead, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
                goto input_error;
            }
            si.hStdInput = hInputRead;
            break;
        case FILE_TYPE:
            hInputRead = CreateFile(cast(wchar_t*, input), // REVIEW! (and all wchar_t*/char*)
                GENERIC_READ, /* desired mode*/
                0, /* shared mode*/
                &sa, /* security attributes */
                OPEN_EXISTING, /* Creation disposition */
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, /* flag and attributes */
                NULL /* template */);
            si.hStdInput = hInputRead;
            break;
        case NONE_TYPE:
            si.hStdInput = 0;
            break;
        default:
            si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            break;
    }

    switch (output_type) {
        case STRING_TYPE:
        case BINARY_TYPE:
            if (!CreatePipe(&hOutputRead, &hOutputWrite, NULL, 0)) {
                goto output_error;
            }

            /* make child side handle inheritable */
            if (!SetHandleInformation(hOutputWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
                goto output_error;
            }
            si.hStdOutput = hOutputWrite;
            break;
        case FILE_TYPE:
            si.hStdOutput = CreateFile(*(LPCTSTR*)output,
                GENERIC_WRITE, /* desired mode*/
                0, /* shared mode*/
                &sa, /* security attributes */
                CREATE_NEW, /* Creation disposition */
                FILE_ATTRIBUTE_NORMAL, /* flag and attributes */
                NULL /* template */);

            if (si.hStdOutput == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_EXISTS) {
                si.hStdOutput = CreateFile(*(LPCTSTR*)output,
                    GENERIC_WRITE, /* desired mode*/
                    0, /* shared mode*/
                    &sa, /* security attributes */
                    OPEN_EXISTING, /* Creation disposition */
                    FILE_ATTRIBUTE_NORMAL, /* flag and attributes */
                    NULL /* template */);
            }
            break;
        case NONE_TYPE:
            si.hStdOutput = 0;
            break;
        default:
            si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
            break;
    }

    switch (err_type) {
        case STRING_TYPE:
        case BINARY_TYPE:
            if (!CreatePipe(&hErrorRead, &hErrorWrite, NULL, 0)) {
                goto error_error;
            }
            /* make child side handle inheritable */
            if (!SetHandleInformation(hErrorWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
                goto error_error;
            }
            si.hStdError = hErrorWrite;
            break;
        case FILE_TYPE:
            si.hStdError = CreateFile(*(LPCTSTR*)err,
                GENERIC_WRITE, /* desired mode*/
                0, /* shared mode*/
                &sa, /* security attributes */
                CREATE_NEW, /* Creation disposition */
                FILE_ATTRIBUTE_NORMAL, /* flag and attributes */
                NULL /* template */);

            if (si.hStdError == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_EXISTS) {
                si.hStdError = CreateFile(*(LPCTSTR*)err,
                    GENERIC_WRITE, /* desired mode*/
                    0, /* shared mode*/
                    &sa, /* security attributes */
                    OPEN_EXISTING, /* Creation disposition */
                    FILE_ATTRIBUTE_NORMAL, /* flag and attributes */
                    NULL /* template */);
            }
            break;
        case NONE_TYPE:
            si.hStdError = 0;
            break;
        default:
            si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
            break;
    }

    if (call == NULL) {
        /* command in argv */
        goto cleanup; /* NOT IMPLEMENTED*/
    } else {
        if (flag_shell) {
            const wchar_t *sh = L"cmd.exe /C ";
            size_t len = wcslen(sh) + wcslen(call) + 1;

            // other branch uses _wcsdup and free(), so we can't use
            // OS_ALLOC_N here (doesn't matter, not returning it to Rebol)
            cmd = cast(wchar_t*, malloc(len * sizeof(wchar_t)));
            cmd[0] = L'\0';
            wcscat(cmd, sh);
            wcscat(cmd, call);
        } else {
            // CreateProcess might write to this memory
            // Duplicate it to be safe
            cmd = _wcsdup(call);
        }
    }

    result = CreateProcess(
        NULL,                       // Executable name
        cmd,                        // Command to execute
        NULL,                       // Process security attributes
        NULL,                       // Thread security attributes
        TRUE,                       // Inherit handles, must be TRUE for I/O redirection
        NORMAL_PRIORITY_CLASS       // Creation flags
        | CREATE_DEFAULT_ERROR_MODE,
        NULL,                       // Environment
        NULL,                       // Current directory
        &si,                        // Startup information
        &pi                         // Process information
    );

    free(cmd);

    if (pid != NULL) *pid = pi.dwProcessId;

    if (hInputRead != NULL)
        CloseHandle(hInputRead);

    if (hOutputWrite != NULL)
        CloseHandle(hOutputWrite);

    if (hErrorWrite != NULL)
        CloseHandle(hErrorWrite);

    // Wait for termination:
    if (result && flag_wait) {
        HANDLE handles[3];
        int count = 0;
        DWORD wait_result = 0;
        DWORD output_size = 0;
        DWORD err_size = 0;

#define BUF_SIZE_CHUNK 4096

        if (hInputWrite != NULL && input_len > 0) {
            if (input_type == STRING_TYPE) {
                DWORD dest_len = 0;
                /* convert input encoding from UNICODE to OEM */
                // !!! Is cast to wchar_t here legal?
                dest_len = WideCharToMultiByte(CP_OEMCP, 0, cast(wchar_t*, input), input_len, oem_input, dest_len, NULL, NULL);
                if (dest_len > 0) {
                    // Not returning memory to Rebol, but we don't realloc or
                    // free, so it's all right to use OS_ALLOC_N anyway
                    oem_input = OS_ALLOC_N(char, dest_len);
                    if (oem_input != NULL) {
                        WideCharToMultiByte(CP_OEMCP, 0, cast(wchar_t*, input), input_len, oem_input, dest_len, NULL, NULL);
                        input_len = dest_len;
                        input = oem_input;
                        handles[count ++] = hInputWrite;
                    }
                }
            } else { /* BINARY_TYPE */
                handles[count ++] = hInputWrite;
            }
        }
        if (hOutputRead != NULL) {
            output_size = BUF_SIZE_CHUNK;
            *output_len = 0;

            // Might realloc(), can't use OS_ALLOC_N.  (This memory is not
            // passed back to Rebol, so it doesn't matter.)
            *output = cast(char*, malloc(output_size));
            handles[count ++] = hOutputRead;
        }
        if (hErrorRead != NULL) {
            err_size = BUF_SIZE_CHUNK;
            *err_len = 0;

            // Might realloc(), can't use OS_ALLOC_N.  (This memory is not
            // passed back to Rebol, so it doesn't matter.)
            *err = cast(char*, malloc(err_size));
            handles[count++] = hErrorRead;
        }

        while (count > 0) {
            wait_result = WaitForMultipleObjects(count, handles, FALSE, INFINITE);
            // If we test wait_result >= WAIT_OBJECT_0 it will tell us "always
            // true" with -Wtype-limits, since WAIT_OBJECT_0 is 0.  Take that
            // comparison out but add an assert in case you're on some abstracted
            // Windows and need to know that it isn't 0 for that implementation.
            assert(WAIT_OBJECT_0 == 0);
            if (wait_result < WAIT_OBJECT_0 + count) {
                int i = wait_result - WAIT_OBJECT_0;
                DWORD input_pos = 0;
                DWORD n = 0;

                if (handles[i] == hInputWrite) {
                    if (!WriteFile(hInputWrite, (char*)input + input_pos, input_len - input_pos, &n, NULL)) {
                        if (i < count - 1) {
                            memmove(&handles[i], &handles[i + 1], (count - i - 1) * sizeof(HANDLE));
                        }
                        count--;
                    } else {
                        input_pos += n;
                        if (input_pos >= input_len) {
                            /* done with input */
                            CloseHandle(hInputWrite);
                            hInputWrite = NULL;
                            OS_FREE(oem_input);
                            oem_input = NULL;
                            if (i < count - 1) {
                                memmove(&handles[i], &handles[i + 1], (count - i - 1) * sizeof(HANDLE));
                            }
                            count--;
                        }
                    }
                } else if (handles[i] == hOutputRead) {
                    if (!ReadFile(hOutputRead, *(char**)output + *output_len, output_size - *output_len, &n, NULL)) {
                        if (i < count - 1) {
                            memmove(&handles[i], &handles[i + 1], (count - i - 1) * sizeof(HANDLE));
                        }
                        count--;
                    } else {
                        *output_len += n;
                        if (*output_len >= output_size) {
                            output_size += BUF_SIZE_CHUNK;
                            *output = cast(char*, realloc(*output, output_size));
                            if (*output == NULL) goto kill;
                        }
                    }
                } else if (handles[i] == hErrorRead) {
                    if (!ReadFile(hErrorRead, *(char**)err + *err_len, err_size - *err_len, &n, NULL)) {
                        if (i < count - 1) {
                            memmove(&handles[i], &handles[i + 1], (count - i - 1) * sizeof(HANDLE));
                        }
                        count--;
                    } else {
                        *err_len += n;
                        if (*err_len >= err_size) {
                            err_size += BUF_SIZE_CHUNK;
                            *err = cast(char*, realloc(*err, err_size));
                            if (*err == NULL) goto kill;
                        }
                    }
                } else {
                    //printf("Error READ");
                    if (!ret) ret = GetLastError();
                    goto kill;
                }
            } else if (wait_result == WAIT_FAILED) { /* */
                //printf("Wait Failed\n");
                if (!ret) ret = GetLastError();
                goto kill;
            } else {
                //printf("Wait returns unexpected result: %d\n", wait_result);
                if (!ret) ret = GetLastError();
                goto kill;
            }
        }

        WaitForSingleObject(pi.hProcess, INFINITE); // check result??
        if (exit_code != NULL) {
            DWORD temp;
            GetExitCodeProcess(pi.hProcess, &temp);
            *exit_code = temp;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);

        if (output_type == STRING_TYPE && *output != NULL && *output_len > 0) {
            /* convert to wide char string */
            int dest_len = 0;
            wchar_t *dest = NULL;
            dest_len = MultiByteToWideChar(CP_OEMCP, 0, *output, *output_len, dest, 0);
            if (dest_len <= 0) {
                OS_FREE(*output);
                *output = NULL;
                *output_len = 0;
            }
            // We've already established that output is a malloc()'d pointer,
            // not one we got back from OS_ALLOC_N()
            dest = cast(wchar_t*, malloc(*output_len * sizeof(wchar_t)));
            if (dest == NULL) goto cleanup;
            MultiByteToWideChar(CP_OEMCP, 0, *output, *output_len, dest, dest_len);
            free(*output);
            *output = cast(char*, dest);
            *output_len = dest_len;
        }

        if (err_type == STRING_TYPE && *err != NULL && *err_len > 0) {
            /* convert to wide char string */
            int dest_len = 0;
            wchar_t *dest = NULL;
            dest_len = MultiByteToWideChar(CP_OEMCP, 0, *err, *err_len, dest, 0);
            if (dest_len <= 0) {
                OS_FREE(*err);
                *err = NULL;
                *err_len = 0;
            }
            // We've already established that output is a malloc()'d pointer,
            // not one we got back from OS_ALLOC_N()
            dest = cast(wchar_t*, malloc(*err_len * sizeof(wchar_t)));
            if (dest == NULL) goto cleanup;
            MultiByteToWideChar(CP_OEMCP, 0, *err, *err_len, dest, dest_len);
            free(*err);
            *err = cast(char*, dest);
            *err_len = dest_len;
        }
    } else if (result) {
        /* no wait */
        /* Close handles to avoid leaks */
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        /* CreateProcess failed */
        ret = GetLastError();
    }

    goto cleanup;

kill:
    if (TerminateProcess(pi.hProcess, 0)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        if (exit_code != NULL) {
            DWORD temp;
            GetExitCodeProcess(pi.hProcess, &temp);
            *exit_code = temp;
        }
    } else if (ret == 0) {
        ret = GetLastError();
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

cleanup:
    if (oem_input != NULL) {
        // Since we didn't need realloc() for oem_input, we used the
        // OS_ALLOC_N allocator.
        OS_FREE(oem_input);
    }

    if (output != NULL && *output != NULL && *output_len == 0) {
        free(*output);
    }

    if (err != NULL && *err != NULL && *err_len == 0) {
        free(*err);
    }

    if (hInputWrite != NULL)
        CloseHandle(hInputWrite);

    if (hOutputRead != NULL)
        CloseHandle(hOutputRead);

    if (hErrorRead != NULL)
        CloseHandle(hErrorRead);

    if (err_type == FILE_TYPE) {
        CloseHandle(si.hStdError);
    }

error_error:
    if (output_type == FILE_TYPE) {
        CloseHandle(si.hStdOutput);
    }

output_error:
    if (input_type == FILE_TYPE) {
        CloseHandle(si.hStdInput);
    }

input_error:
    return ret;  // meaning depends on flags
}

//
//  OS_Reap_Process: C
//
// pid:
//      > 0, a single process
//      -1, any child process
// flags:
//      0: return immediately
//
//      Return -1 on error
//
int OS_Reap_Process(int pid, int *status, int flags)
{
    UNUSED(pid);
    UNUSED(status);
    UNUSED(flags);

    // !!! It seems that process doesn't need to be reaped on Windows
    return 0;
}

//
//  OS_Browse: C
//
int OS_Browse(const REBCHR *url, int reserved)
{
    UNUSED(reserved);

    #define MAX_BRW_PATH 2044
    DWORD flag;
    DWORD len;
    DWORD type;
    HKEY key;
    wchar_t *path;
    int exit_code = 0;

    if (RegOpenKeyEx(HKEY_CLASSES_ROOT, L"http\\shell\\open\\command", 0, KEY_READ, &key) != ERROR_SUCCESS)
        return 0;

    if (!url) url = L"";

    path = OS_ALLOC_N(wchar_t, MAX_BRW_PATH+4);
    len = MAX_BRW_PATH;

    flag = RegQueryValueEx(key, L"", 0, &type, cast(LPBYTE, path), &len);
    RegCloseKey(key);
    if (flag != ERROR_SUCCESS) {
        OS_FREE(path);
        return 0;
    }
    //if (ExpandEnvironmentStrings(&str[0], result, len))

    Insert_Command_Arg(path, url, MAX_BRW_PATH);

    //len = OS_Create_Process(path, 0);

    REBCHR * const argv[] = {path, NULL};
    len = OS_Create_Process(path, 1, c_cast(const REBCHR**, argv), 0,
                            NULL, /* pid */
                            &exit_code,
                            INHERIT_TYPE, NULL, 0, /* input_type, void *input, u32 input_len, */
                            INHERIT_TYPE, NULL, NULL, /* output_type, void **output, u32 *output_len, */
                            INHERIT_TYPE, NULL, NULL); /* u32 err_type, void **err, u32 *err_len */

    OS_FREE(path);
    return len;
}


//
//  OS_Request_File: C
//
REBOOL OS_Request_File(REBRFR *fr)
{
    OPENFILENAME ofn;
    BOOL ret;
    //int err;
    const wchar_t *filters = L"All files\0*.*\0REBOL scripts\0*.r\0Text files\0*.txt\0";

    memset(&ofn, '\0', sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);

    // ofn.hwndOwner = WIN_WIN(win); // Must find a way to set this

    ofn.lpstrTitle = fr->title;
    ofn.lpstrInitialDir = fr->dir;
    ofn.lpstrFile = fr->files;
    ofn.lpstrFilter = fr->filter ? fr->filter : filters;
    ofn.nMaxFile = fr->len;
    ofn.lpstrFileTitle = 0;
    ofn.nMaxFileTitle = 0;

    ofn.Flags = OFN_HIDEREADONLY | OFN_EXPLORER | OFN_NOCHANGEDIR; //|OFN_NONETWORKBUTTON; //;

    if (GET_FLAG(fr->flags, FRF_MULTI)) ofn.Flags |= OFN_ALLOWMULTISELECT;

    osDialogOpen = TRUE;

    if (GET_FLAG(fr->flags, FRF_SAVE))
        ret = GetSaveFileName(&ofn);
    else
        ret = GetOpenFileName(&ofn);

    osDialogOpen = FALSE;

    //if (!ret)
    //  err = CommDlgExtendedError(); // CDERR_FINDRESFAILURE

    return ret;
}

int CALLBACK ReqDirCallbackProc( HWND hWnd, UINT uMsg, LPARAM lParam, LPARAM lpData )
{
    UNUSED(lParam);

    static REBOOL inited = FALSE;
    switch (uMsg) {
        case BFFM_INITIALIZED:
            if (lpData) SendMessage(hWnd,BFFM_SETSELECTION,TRUE,lpData);
            SetForegroundWindow(hWnd);
            inited = TRUE;
            break;
        case BFFM_SELCHANGED:
            if (inited && lpData) {
                SendMessage(hWnd,BFFM_SETSELECTION,TRUE,lpData);
                inited = FALSE;
            }
            break;
    }
    return 0;
}


//
//  OS_Request_Dir: C
//
// WARNING: TEMPORARY implementation! Used only by host-core.c
// Will be most probably changed in future.
//
REBOOL OS_Request_Dir(REBCHR* title, REBCHR** folder, REBCHR* path)
{
    BROWSEINFO bi;
    wchar_t buffer[MAX_PATH];
    LPCITEMIDLIST pFolder;
    ZeroMemory(buffer, MAX_PATH);
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = NULL;
    bi.pszDisplayName = buffer;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_EDITBOX | BIF_NEWDIALOGSTYLE | BIF_RETURNONLYFSDIRS | BIF_SHAREABLE;
    bi.lpfn = ReqDirCallbackProc;
    bi.lParam = (LPARAM)path;

    osDialogOpen = TRUE;
    pFolder = SHBrowseForFolder(&bi);
    osDialogOpen = FALSE;
    if (pFolder == NULL) return FALSE;
    if (!SHGetPathFromIDList(pFolder, buffer) ) return FALSE;
    wcscpy(*folder, buffer);
    return TRUE;
}

//
//  OS_GOB_To_Image: C
//
// Render a GOB into an image. Returns an image or zero if
// it cannot be done.
//
REBVAL *OS_GOB_To_Image(REBGOB *gob)
{
#if (defined REB_CORE)
    UNUSED(gob);
    return 0;
#else
    return Gob_To_Image(gob);
#endif
}


//
//  OS_Read_Embedded: C
//
// Read embedded rebol script from the executable
//
REBYTE *OS_Read_Embedded(REBI64 *script_size)
{
#define PAYLOAD_NAME L"EMBEDDEDREBOL"

    REBYTE *embedded_script = NULL;
    HMODULE h_mod= 0;
    HRSRC h_res = 0;
    HGLOBAL h_res_mem = NULL;
    void *res_ptr = NULL;

    h_mod = GetModuleHandle(NULL);
    if (h_mod == 0) {
        return NULL;
    }

    h_res = FindResource(h_mod, PAYLOAD_NAME, RT_RCDATA);
    if (h_res == 0) {
        return NULL;
    }

    h_res_mem = LoadResource(h_mod, h_res);
    if (h_res_mem == 0) {
        return NULL;
    }

    res_ptr = LockResource(h_res_mem);
    if (res_ptr == NULL) {
        return NULL;
    }

    *script_size = SizeofResource(h_mod, h_res);

    if (*script_size <= 0) {
        return NULL;
    }

    embedded_script = OS_ALLOC_N(REBYTE, *script_size);

    if (embedded_script == NULL) {
        return NULL;
    }

    memcpy(embedded_script, res_ptr, *script_size);

    return embedded_script;
}
