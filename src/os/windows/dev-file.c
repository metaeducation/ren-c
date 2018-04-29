//
//  File: %dev-file.c
//  Summary: "Device: File access for Win32"
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
// File open, close, read, write, and other actions.
//

#include <stdio.h>
#include <windows.h>
#include <process.h>
#include <assert.h>

#include "reb-host.h"

// MSDN V6 missed this define:
#ifndef INVALID_SET_FILE_POINTER
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#endif


/***********************************************************************
**
**  Local Functions
**
***********************************************************************/

static REBOOL Seek_File_64(struct devreq_file *file)
{
    // Performs seek and updates index value.

    REBREQ *req = AS_REBREQ(file);
    HANDLE h = req->requestee.handle;
    DWORD result;
    LONG highint;

    if (file->index == -1) {
        // Append:
        highint = 0;
        result = SetFilePointer(h, 0, &highint, FILE_END);
    }
    else {
        // Line below updates index if it is affected:
        highint = cast(LONG, file->index >> 32);
        result = SetFilePointer(
            h, cast(LONG, file->index), &highint, FILE_BEGIN
        );
    }

    if (result == INVALID_SET_FILE_POINTER) {
        DWORD last_error = GetLastError();
        if (last_error != NO_ERROR)
            return FALSE; // GetLastError() should still hold the error
    }

    file->index = (cast(int64_t, highint) << 32) + result;

    return 1;
}


//
//  Read_Directory: C
//
// This function will read a file directory, one file entry
// at a time, then close when no more files are found.
//
// Procedure:
//
// This function is passed directory and file arguments.
// The dir arg provides information about the directory to read.
// The file arg is used to return specific file information.
//
// To begin, this function is called with a dir->requestee.handle that
// is set to zero and a dir->special.file.path string for the directory.
//
// The directory is opened and a handle is stored in the dir
// structure for use on subsequent calls. If an error occurred,
// dir->error is set to the error code and -1 is returned.
// The dir->size field can be set to the number of files in the
// dir, if it is known. The dir->special.file.index field can be used by this
// function to store information between calls.
//
// If the open succeeded, then information about the first file
// is stored in the file argument and the function returns 0.
// On an error, the dir->error is set, the dir is closed,
// dir->requestee.handle is nulled, and -1 is returned.
//
// The caller loops until all files have been obtained. This
// action should be uninterrupted. (The caller should not perform
// additional OS or IO operations between calls.)
//
// When no more files are found, the dir is closed, dir->requestee.handle
// is nulled, and 1 is returned. No file info is returned.
// (That is, this function is called one extra time. This helps
// for OSes that may deallocate file strings on dir close.)
//
// Note that the dir->special.file.path can contain wildcards * and ?. The
// processing of these can be done in the OS (if supported) or
// by a separate filter operation during the read.
//
// Store file date info in file->special.file.index or other fields?
// Store permissions? Ownership? Groups? Or, require that
// to be part of a separate request?
//
static int Read_Directory(struct devreq_file *dir, struct devreq_file *file)
{
    REBREQ *dir_req = AS_REBREQ(dir);
    REBREQ *file_req = AS_REBREQ(file);

    // !!! This old code from R3-Alpha triggered a warning on info not
    // necessarily being initialized.  Rather than try and fix it, this just
    // fails if an uninitialized case ever happens.
    //
    WIN32_FIND_DATA info;
    CLEARS(&info); // got_info protects usage if never initialized
    REBOOL got_info = FALSE;

    WCHAR *cp = NULL;

    HANDLE h = dir_req->requestee.handle;
    if (h == NULL) {
        // Read first file entry:

        WCHAR *dir_wide = rebFileToLocalAllocW(
            NULL,
            dir->path,
            REB_FILETOLOCAL_FULL | REB_FILETOLOCAL_WILD
        );
        h = FindFirstFile(dir_wide, &info);
        rebFree(dir_wide);

        if (h == INVALID_HANDLE_VALUE)
            rebFail_OS (GetLastError());

        got_info = TRUE;
        dir_req->requestee.handle = h;
        dir_req->flags &= ~RRF_DONE;
        cp = info.cFileName;
    }

    // Skip over the . and .. dir cases:
    while (
        cp == 0
        || (cp[0] == '.' && (cp[1] == 0 || (cp[1] == '.' && cp[2] == '\0')))
    ){
        // Read next file_req entry, or error:
        if (not FindNextFile(h, &info)) {
            DWORD last_error_cache = GetLastError();
            FindClose(h);
            dir_req->requestee.handle = NULL;

            if (last_error_cache != ERROR_NO_MORE_FILES)
                rebFail_OS (last_error_cache);

            dir_req->flags |= RRF_DONE; // no more file_reqs
            return DR_DONE;
        }
        got_info = TRUE;
        cp = info.cFileName;
    }

    if (not got_info) {
        assert(FALSE); // see above for why this R3-Alpha code had a "hole"
        rebFail ("{%dev-clipboard: NOT(got_info), please report}", rebEnd());
    }

    file_req->modes = 0;
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        file_req->modes |= RFM_DIR;

    const REBOOL is_dir = did (file_req->modes & RFM_DIR);
    file->path = rebLocalToFileW(info.cFileName, is_dir);
    file->size =
        (cast(int64_t, info.nFileSizeHigh) << 32) + info.nFileSizeLow;

    return DR_DONE;
}


//
//  Open_File: C
//
// Open the specified file with the given modes.
//
// Notes:
// 1.    The file path is provided in REBOL format, and must be
//     converted to local format before it is used.
// 2.    REBOL performs the required access security check before
//     calling this function.
// 3.    REBOL clears necessary fields of file structure before
//     calling (e.g. error and size fields).
//
// !! Confirm that /seek /append works properly.
//
DEVICE_CMD Open_File(REBREQ *req)
{
    DWORD attrib = FILE_ATTRIBUTE_NORMAL;
    DWORD access = 0;
    DWORD create = 0;
    BY_HANDLE_FILE_INFORMATION info;
    struct devreq_file *file = DEVREQ_FILE(req);

    // Set the access, creation, and attribute for file creation:
    if (req->modes & RFM_READ) {
        access |= GENERIC_READ;
        create = OPEN_EXISTING;
    }

    if ((req->modes & (RFM_WRITE | RFM_APPEND)) != 0) {
        access |= GENERIC_WRITE;
        if (
            (req->modes & RFM_NEW)
            or (req->modes & (RFM_READ | RFM_APPEND | RFM_SEEK)) == 0
        ){
            create = CREATE_ALWAYS;
        }
        else
            create = OPEN_ALWAYS;
    }

    attrib |= (req->modes & RFM_SEEK)
        ? FILE_FLAG_RANDOM_ACCESS
        : FILE_FLAG_SEQUENTIAL_SCAN;

    if (req->modes & RFM_READONLY)
        attrib |= FILE_ATTRIBUTE_READONLY;

    if (access == 0)
        rebFail ("{No access modes provided to Open_File()}", rebEnd());

    REBFLGS flags = REB_FILETOLOCAL_FULL;
    if (req->modes & RFM_DIR)
        flags |= REB_FILETOLOCAL_WILD;

    WCHAR *path_wide = rebFileToLocalAllocW(NULL, file->path, flags);

    HANDLE h = CreateFile(
        path_wide,
        access,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        0,
        create,
        attrib,
        0
    );

    rebFree(path_wide);

    if (h == INVALID_HANDLE_VALUE)
        rebFail_OS (GetLastError());

    if (req->modes & RFM_SEEK) {
        //
        // Confirm that a seek-mode req is actually seekable, by seeking the
        // file to 0 (which should always work if it is)
        //
        if (SetFilePointer(h, 0, 0, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
            DWORD last_error_cache = GetLastError();
            CloseHandle(h);
            rebFail_OS (last_error_cache);
        }
    }

    // Fetch req size (if fails, then size is assumed zero)
    //
    if (GetFileInformationByHandle(h, &info)) {
        file->size =
            (cast(int64_t, info.nFileSizeHigh) << 32) + info.nFileSizeLow;
        file->time.l = info.ftLastWriteTime.dwLowDateTime;
        file->time.h = info.ftLastWriteTime.dwHighDateTime;
    }

    req->requestee.handle = h;

    return DR_DONE;
}


//
//  Close_File: C
//
// Closes a previously opened file.
//
DEVICE_CMD Close_File(REBREQ *file)
{
    if (file->requestee.handle) {
        CloseHandle(file->requestee.handle);
        file->requestee.handle = 0;
    }
    return DR_DONE;
}


//
//  Read_File: C
//
DEVICE_CMD Read_File(REBREQ *req)
{
    struct devreq_file *file = DEVREQ_FILE(req);
    if (req->modes & RFM_DIR)
        return Read_Directory(
            file,
            cast(struct devreq_file*, req->common.data)
        );

    assert(req->requestee.handle != 0);

    if ((req->modes & (RFM_SEEK | RFM_RESEEK)) != 0) {
        req->modes &= ~RFM_RESEEK;
        if (not Seek_File_64(file))
            rebFail_OS (GetLastError());
    }

    assert(sizeof(DWORD) == sizeof(req->actual));

    if (not ReadFile(
        req->requestee.handle,
        req->common.data,
        req->length,
        cast(DWORD*, &req->actual),
        0
    )){
        rebFail_OS (GetLastError());
    }

    file->index += req->actual;
    return DR_DONE;
}


//
//  Write_File: C
//
// Bug?: update file->size value after write !?
//
DEVICE_CMD Write_File(REBREQ *req)
{
    struct devreq_file *file = DEVREQ_FILE(req);

    assert(req->requestee.handle != NULL);

    if (req->modes & RFM_APPEND) {
        req->modes &= ~RFM_APPEND;
        SetFilePointer(req->requestee.handle, 0, 0, FILE_END);
    }

    if ((req->modes & (RFM_SEEK | RFM_RESEEK | RFM_TRUNCATE)) != 0) {
        req->modes &= ~RFM_RESEEK;
        if (not Seek_File_64(file))
            rebFail_OS (GetLastError());
        if (req->modes & RFM_TRUNCATE)
            SetEndOfFile(req->requestee.handle);
    }

    if (not (req->modes & RFM_TEXT)) { // e.g. no LF => CRLF translation needed
        if (req->length != 0) {
            BOOL ok = WriteFile(
                req->requestee.handle,
                req->common.data,
                req->length,
                cast(LPDWORD, &req->actual),
                0
            );

            if (not ok)
                rebFail_OS (GetLastError());
        }
    }
    else {
        // !!! This repeats code used in %dev-stdio.c, which is needed when
        // console output is redirected to a file.  It should be shareable.

        REBCNT start = 0;
        REBCNT end = 0;

        req->actual = 0; // count actual bytes written as we go along

        while (TRUE) {
            while (end < req->length && req->common.data[end] != LF)
                ++end;
            DWORD total_bytes;

            if (start != end) {
                BOOL ok = WriteFile(
                    req->requestee.handle,
                    req->common.data + start,
                    end - start,
                    &total_bytes,
                    0
                );
                if (not ok)
                    rebFail_OS (GetLastError());
                req->actual += total_bytes;
            }

            if (req->common.data[end] == '\0')
                break;

            assert(req->common.data[end] == LF);
            BOOL ok = WriteFile(
                req->requestee.handle,
                "\r\n",
                2,
                &total_bytes,
                0
            );
            if (not ok)
                rebFail_OS (GetLastError());
            req->actual += total_bytes;

            ++end;
            start = end;
        }
    }

    DWORD size_high;
    DWORD size_low = GetFileSize(req->requestee.handle, &size_high);
    if (size_low == 0xffffffff) {
        DWORD last_error = GetLastError();
        if (last_error != NO_ERROR)
            rebFail_OS (last_error);

        // ...else the file size really is 0xffffffff
    }

    file->size =
        (cast(int64_t, size_high) << 32) + cast(int64_t, size_low);

    return DR_DONE;
}


//
//  Query_File: C
//
// Obtain information about a file. Return TRUE on success.
// On error, return FALSE and set file->error code.
//
// Note: time is in local format and must be converted
//
DEVICE_CMD Query_File(REBREQ *req)
{
    WIN32_FILE_ATTRIBUTE_DATA info;
    struct devreq_file *file = DEVREQ_FILE(req);

    WCHAR *path_wide = rebFileToLocalAllocW(
        NULL,
        file->path,
        REB_FILETOLOCAL_FULL | REB_FILETOLOCAL_NO_TAIL_SLASH
    );

    REBOOL success = GetFileAttributesEx(
        path_wide, GetFileExInfoStandard, &info
    );

    rebFree(path_wide);

    if (not success)
        rebFail_OS (GetLastError());

    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        req->modes |= RFM_DIR;
    else
        req->modes &= ~RFM_DIR;

    file->size =
        (cast(int64_t, info.nFileSizeHigh) << 32)
            + cast(int64_t, info.nFileSizeLow);
    file->time.l = info.ftLastWriteTime.dwLowDateTime;
    file->time.h = info.ftLastWriteTime.dwHighDateTime;
    return DR_DONE;
}


//
//  Create_File: C
//
DEVICE_CMD Create_File(REBREQ *req)
{
    struct devreq_file *file = DEVREQ_FILE(req);

    if (not (req->modes & RFM_DIR))
        return Open_File(req);

    WCHAR *path_wide = rebFileToLocalAllocW(
        NULL,
        file->path,
        REB_FILETOLOCAL_FULL | REB_FILETOLOCAL_NO_TAIL_SLASH
    );

    LPSECURITY_ATTRIBUTES lpSecurityAttributes = NULL;
    REBOOL success = CreateDirectory(path_wide, lpSecurityAttributes);

    rebFree(path_wide);

    if (not success)
        rebFail_OS (GetLastError());

    return DR_DONE;
}


//
//  Delete_File: C
//
// Delete a file or directory. Return TRUE if it was done.
// The file->special.file.path provides the directory path and name.
// For errors, return FALSE and set file->error to error code.
//
// Note: Dirs must be empty to succeed
//
DEVICE_CMD Delete_File(REBREQ *req)
{
    struct devreq_file *file = DEVREQ_FILE(req);

    WCHAR *path_wide = rebFileToLocalAllocW(
        NULL,
        file->path,
        REB_FILETOLOCAL_FULL // leave tail slash on for directory removal
    );

    REBOOL success;
    if (req->modes & RFM_DIR)
        success = RemoveDirectory(path_wide);
    else
        success = DeleteFile(path_wide);

    if (not success)
        rebFail_OS (GetLastError());

    return DR_DONE;
}


//
//  Rename_File: C
//
// Rename a file or directory.
// Note: cannot rename across file volumes.
//
DEVICE_CMD Rename_File(REBREQ *req)
{
    struct devreq_file *file = DEVREQ_FILE(req);

    REBVAL *to = cast(REBVAL*, req->common.data); // !!! hack!

    WCHAR *from_wide = rebFileToLocalAllocW(
        NULL,
        file->path,
        REB_FILETOLOCAL_FULL | REB_FILETOLOCAL_NO_TAIL_SLASH
    );
    WCHAR *to_wide = rebFileToLocalAllocW(
        NULL,
        to,
        REB_FILETOLOCAL_FULL | REB_FILETOLOCAL_NO_TAIL_SLASH
    );

    REBOOL success = MoveFile(from_wide, to_wide);

    rebFree(to_wide);
    rebFree(from_wide);

    if (not success)
        rebFail_OS (GetLastError());

    return DR_DONE;
}


//
//  Poll_File: C
//
DEVICE_CMD Poll_File(REBREQ *file)
{
    UNUSED(file);
    return DR_DONE;     // files are synchronous (currently)
}


/***********************************************************************
**
**  Command Dispatch Table (RDC_ enum order)
**
***********************************************************************/

static DEVICE_CMD_CFUNC Dev_Cmds[RDC_MAX] = {
    0,
    0,
    Open_File,
    Close_File,
    Read_File,
    Write_File,
    Poll_File,
    0,  // connect
    Query_File,
    0,  // modify
    Create_File,
    Delete_File,
    Rename_File,
};

DEFINE_DEV(
    Dev_File,
    "File IO", 1, Dev_Cmds, RDC_MAX, sizeof(struct devreq_file)
);
