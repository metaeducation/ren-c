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

static BOOL Seek_File_64(struct devreq_file *file)
{
    // Performs seek and updates index value. TRUE on scuccess.
    // On error, returns FALSE and sets file->error field.
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

    if (result == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) {
        req->error = -RFE_NO_SEEK;
        return 0;
    }

    file->index = (cast(i64, highint) << 32) + result;

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
    WIN32_FIND_DATA info;
    REBREQ *dir_req = AS_REBREQ(dir);
    REBREQ *file_req = AS_REBREQ(file);
    HANDLE h= dir_req->requestee.handle;
    wchar_t *cp = 0;

    if (!h) {

        // Read first file entry:
        h = FindFirstFile(dir->path, &info);
        if (h == INVALID_HANDLE_VALUE) {
            dir_req->error = -RFE_OPEN_FAIL;
            return DR_ERROR;
        }
        dir_req->requestee.handle = h;
        CLR_FLAG(dir_req->flags, RRF_DONE);
        cp = info.cFileName;

    }

    // Skip over the . and .. dir cases:
    while (cp == 0 || (cp[0] == '.' && (cp[1] == 0 || (cp[1] == '.' && cp[2] == 0)))) {

        // Read next file_req entry, or error:
        if (!FindNextFile(h, &info)) {
            dir_req->error = GetLastError();
            FindClose(h);
            dir_req->requestee.handle = 0;
            if (dir_req->error != ERROR_NO_MORE_FILES) return DR_ERROR;
            dir_req->error = 0;
            SET_FLAG(dir_req->flags, RRF_DONE); // no more file_reqs
            return DR_DONE;
        }
        cp = info.cFileName;

    }

    file_req->modes = 0;
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) SET_FLAG(file_req->modes, RFM_DIR);
    wcsncpy(file->path, info.cFileName, MAX_FILE_NAME);
    file->size =
        (cast(i64, info.nFileSizeHigh) << 32) + info.nFileSizeLow;

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
    HANDLE h;
    BY_HANDLE_FILE_INFORMATION info;
    struct devreq_file *file = DEVREQ_FILE(req);

    // Set the access, creation, and attribute for file creation:
    if (GET_FLAG(req->modes, RFM_READ)) {
        access |= GENERIC_READ;
        create = OPEN_EXISTING;
    }

    if (GET_FLAGS(req->modes, RFM_WRITE, RFM_APPEND)) {
        access |= GENERIC_WRITE;
        if (
            GET_FLAG(req->modes, RFM_NEW) ||
            !(
                GET_FLAG(req->modes, RFM_READ) ||
                GET_FLAG(req->modes, RFM_APPEND) ||
                GET_FLAG(req->modes, RFM_SEEK)
            )
        ) create = CREATE_ALWAYS;
        else create = OPEN_ALWAYS;
    }

    attrib |= GET_FLAG(req->modes, RFM_SEEK) ? FILE_FLAG_RANDOM_ACCESS : FILE_FLAG_SEQUENTIAL_SCAN;

    if (GET_FLAG(req->modes, RFM_READONLY))
        attrib |= FILE_ATTRIBUTE_READONLY;

    if (!access) {
        req->error = -RFE_NO_MODES;
        goto fail;
    }

    // Open the req (yes, this is how windows does it, the nutty kids):
    h = CreateFile(file->path, access, FILE_SHARE_READ|FILE_SHARE_WRITE, 0, create, attrib, 0);
    if (h == INVALID_HANDLE_VALUE) {
        req->error = -RFE_OPEN_FAIL;
        goto fail;
    }

    // Confirm that a seek-mode req is actually seekable:
    if (GET_FLAG(req->modes, RFM_SEEK)) {
        // Below should work because we are seeking to 0:
        if (SetFilePointer(h, 0, 0, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
            CloseHandle(h);
            req->error = -RFE_BAD_SEEK;
            goto fail;
        }
    }

    // Fetch req size (if fails, then size is assumed zero):
    if (GetFileInformationByHandle(h, &info)) {
        file->size =
            (cast(i64, info.nFileSizeHigh) << 32) + info.nFileSizeLow;
        file->time.l = info.ftLastWriteTime.dwLowDateTime;
        file->time.h = info.ftLastWriteTime.dwHighDateTime;
    }

    req->requestee.handle = h;

    return DR_DONE;

fail:
    return DR_ERROR;
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
    if (GET_FLAG(req->modes, RFM_DIR)) {
        return Read_Directory(file, cast(struct devreq_file*, req->common.data));
    }

    if (!req->requestee.handle) {
        req->error = -RFE_NO_HANDLE;
        return DR_ERROR;
    }

    if (req->modes & ((1 << RFM_SEEK) | (1 << RFM_RESEEK))) {
        CLR_FLAG(req->modes, RFM_RESEEK);
        if (!Seek_File_64(file)) return DR_ERROR;
    }

    assert(sizeof(DWORD) == sizeof(req->actual));

    if (!ReadFile(
        req->requestee.handle,
        req->common.data,
        req->length,
        cast(DWORD*, &req->actual),
        0
    )) {
        req->error = -RFE_BAD_READ;
        return DR_ERROR;
    } else {
        file->index += req->actual;
    }

    return DR_DONE;
}


//
//  Write_File: C
//
// Bug?: update file->size value after write !?
//
DEVICE_CMD Write_File(REBREQ *req)
{
    DWORD result;
    DWORD size_high, size_low;
    struct devreq_file *file = DEVREQ_FILE(req);

    if (!req->requestee.handle) {
        req->error = -RFE_NO_HANDLE;
        return DR_ERROR;
    }

    if (GET_FLAG(req->modes, RFM_APPEND)) {
        CLR_FLAG(req->modes, RFM_APPEND);
        SetFilePointer(req->requestee.handle, 0, 0, FILE_END);
    }

    if (req->modes & ((1 << RFM_SEEK) | (1 << RFM_RESEEK) | (1 << RFM_TRUNCATE))) {
        CLR_FLAG(req->modes, RFM_RESEEK);
        if (!Seek_File_64(file)) return DR_ERROR;
        if (GET_FLAG(req->modes, RFM_TRUNCATE))
            SetEndOfFile(req->requestee.handle);
    }

    if (req->length != 0) {
        if (!WriteFile(req->requestee.handle, req->common.data, req->length, (LPDWORD)&req->actual, 0)) {
            result = GetLastError();
            if (result == ERROR_HANDLE_DISK_FULL) req->error = -RFE_DISK_FULL;
            else req->error = -RFE_BAD_WRITE;
            return DR_ERROR;
        }
    }

    size_low = GetFileSize(req->requestee.handle, &size_high);
    if (size_low == 0xffffffff) {
        result = GetLastError();
        req->error = -RFE_BAD_WRITE;
        return DR_ERROR;
    }

    file->size =
        (cast(i64, size_high) << 32) + cast(i64, size_low);

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

    if (!GetFileAttributesEx(file->path, GetFileExInfoStandard, &info)) {
        req->error = GetLastError();
        return DR_ERROR;
    }

    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) SET_FLAG(req->modes, RFM_DIR);
    else CLR_FLAG(req->modes, RFM_DIR);
    file->size =
        (cast(i64, info.nFileSizeHigh) << 32) + cast(i64, info.nFileSizeLow);
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
    if (GET_FLAG(req->modes, RFM_DIR)) {
        if (CreateDirectory(file->path, 0)) return DR_DONE;
        req->error = GetLastError();
        return DR_ERROR;
    } else
        return Open_File(req);
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
    if (GET_FLAG(req->modes, RFM_DIR)) {
        if (RemoveDirectory(file->path)) return DR_DONE;
    } else
        if (DeleteFile(file->path)) return DR_DONE;

    req->error = GetLastError();
    return DR_ERROR;
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
    if (MoveFile(cast(wchar_t*, file->path), cast(wchar_t*, req->common.data)))
        return DR_DONE;
    req->error = GetLastError();
    return DR_ERROR;
}


//
//  Poll_File: C
//
DEVICE_CMD Poll_File(REBREQ *file)
{
    UNUSED(file);
    return DR_DONE;     // files are synchronous (currently)
}

//
//  Request_Size_File: C
//
static i32 Request_Size_File(REBREQ *req)
{
    UNUSED(req);
    return sizeof(struct devreq_file);
}


/***********************************************************************
**
**  Command Dispatch Table (RDC_ enum order)
**
***********************************************************************/

static DEVICE_CMD_FUNC Dev_Cmds[RDC_MAX] = {
    Request_Size_File,
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

DEFINE_DEV(Dev_File, "File IO", 1, Dev_Cmds, RDC_MAX);
