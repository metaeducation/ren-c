//
//  File: %dev-file.c
//  Summary: "Device: File access for Win32"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
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
// File open, close, read, write, and other actions.
//

#define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
#include <windows.h>
#undef IS_ERROR

#include <process.h>
#include <assert.h>

#include "sys-core.h"

#include "file-req.h"

// MSDN V6 missed this define:
#ifndef INVALID_SET_FILE_POINTER
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#endif


/***********************************************************************
**
**  Local Functions
**
***********************************************************************/

static bool Seek_File_64(FILEREQ *file)
{
    // Performs seek and updates index value.

    HANDLE h = file->handle;
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
            return false; // GetLastError() should still hold the error
    }

    file->index = (cast(int64_t, highint) << 32) + result;

    return true;
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
REBVAL *Read_Directory(bool *done, FILEREQ *dir, FILEREQ *file)
{
    memset(file, 0, sizeof(FILEREQ));

    WIN32_FIND_DATA info;
    memset(&info, 0, sizeof(info));  // got_info avoids use if uninitialized

    bool got_info = false;

    WCHAR *cp = NULL;

    HANDLE h = dir->handle;
    if (h == NULL) {
        // Read first file entry:

        WCHAR *dir_wide = rebSpellWide(
            "file-to-local/full/wild @", dir->path
        );
        h = FindFirstFile(dir_wide, &info);
        rebFree(dir_wide);

        if (h == INVALID_HANDLE_VALUE)
            return rebError_OS(GetLastError());

        got_info = true;
        dir->handle = h;
        *done = false;

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
            dir->handle = NULL;

            if (last_error_cache != ERROR_NO_MORE_FILES)
                return rebError_OS(last_error_cache);

            *done = true;  // no more files
            return nullptr;
        }

        got_info = true;
        cp = info.cFileName;
    }

    if (not got_info) {
        assert(false); // see above for why this R3-Alpha code had a "hole"
        rebJumps(
            "FAIL {%dev-clipboard: NOT(got_info), please report}"
        );
    }

    file->modes = 0;
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        file->modes |= RFM_DIR;

    file->path = rebValue(
        "applique :local-to-file [",
            "path:", rebR(rebTextWide(info.cFileName)),
            "dir: if", rebL(file->modes & RFM_DIR), "'#",
        "]"
    );

    rebUnmanage(m_cast(REBVAL*, file->path));

    file->size = (cast(int64_t, info.nFileSizeHigh) << 32) + info.nFileSizeLow;

    return nullptr;
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
REBVAL *Open_File(FILEREQ *file)
{
    DWORD attrib = FILE_ATTRIBUTE_NORMAL;
    DWORD access = 0;
    DWORD create = 0;
    BY_HANDLE_FILE_INFORMATION info;

    // Set the access, creation, and attribute for file creation:
    if (file->modes & RFM_READ) {
        access |= GENERIC_READ;
        create = OPEN_EXISTING;
    }

    if ((file->modes & (RFM_WRITE | RFM_APPEND)) != 0) {
        access |= GENERIC_WRITE;
        if (
            (file->modes & RFM_NEW)
            or (file->modes & (RFM_READ | RFM_APPEND | RFM_SEEK)) == 0
        ){
            create = CREATE_ALWAYS;
        }
        else
            create = OPEN_ALWAYS;
    }

    attrib |= (file->modes & RFM_SEEK)
        ? FILE_FLAG_RANDOM_ACCESS
        : FILE_FLAG_SEQUENTIAL_SCAN;

    if (file->modes & RFM_READONLY)
        attrib |= FILE_ATTRIBUTE_READONLY;

    if (access == 0)
        rebJumps("fail {No access modes provided to Open_File()}");

    WCHAR *path_wide = rebSpellWide(
        "applique :file-to-local [",
            "path: @", file->path,
            "wild: if", rebL(file->modes & RFM_DIR), "'#",
            "full: #"
        "]"
    );

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
        return rebError_OS(GetLastError());

    if (file->modes & RFM_SEEK) {
        //
        // Confirm that a seek-mode req is actually seekable, by seeking the
        // file to 0 (which should always work if it is)
        //
        if (SetFilePointer(h, 0, 0, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
            DWORD last_error_cache = GetLastError();
            CloseHandle(h);
            return rebError_OS(last_error_cache);
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

    file->handle = h;

    return nullptr;
}


//
//  Close_File: C
//
// Closes a previously opened file.
//
REBVAL *Close_File(FILEREQ *file)
{
    if (file->handle) {
        CloseHandle(file->handle);
        file->handle = 0;
    }
    return nullptr;
}


//
//  Read_File: C
//
REBVAL *Read_File(size_t *actual, FILEREQ *file, REBYTE *data, size_t length)
{
    assert(file->handle != 0);

    assert(not (file->modes & RFM_DIR));  // should call Read_Directory!

    if ((file->modes & (RFM_SEEK | RFM_RESEEK)) != 0) {
        file->modes &= ~RFM_RESEEK;
        if (not Seek_File_64(file))
            return rebError_OS(GetLastError());
    }

    // !!! There used to be an assert here about the read size.  If bigger
    // reads are wanted, this would have to change to multiple reads or
    // another API.
    //
    if (length > UINT32_MAX)
        fail ("ReadFile() amount exceeds size of DWORD");

    DWORD dword_actual;
    if (not ReadFile(file->handle, data, length, &dword_actual, 0))
        return rebError_OS(GetLastError());

    *actual = dword_actual;
    file->index += dword_actual;

    return nullptr;
}


//
//  Write_File: C
//
// Bug?: update file->size value after write !?
//
REBVAL *Write_File(FILEREQ *file, const REBYTE *data, size_t length)
{
    assert(file->handle != NULL);

    if (file->modes & RFM_APPEND) {
        file->modes &= ~RFM_APPEND;
        SetFilePointer(file->handle, 0, 0, FILE_END);
    }

    if ((file->modes & (RFM_SEEK | RFM_RESEEK | RFM_TRUNCATE)) != 0) {
        file->modes &= ~RFM_RESEEK;
        if (not Seek_File_64(file))
            return rebError_OS(GetLastError());
        if (file->modes & RFM_TRUNCATE)
            SetEndOfFile(file->handle);
    }

    // !!! We now are operating on the belief that CR LF does not count in the
    // nominal idea of what a "text" file format is, so any CRs in the file
    // trigger the need to use special codec settings or to write the file as
    // binary (where the CR LF is handled by the person building and working
    // with the strings, e.g. WRITE ENLINE STR).
    //
    // We save the translation code in case this winds up being used in a
    // codec, but default to just making CRs completely illegal.
    //
    const enum Reb_Strmode strmode = STRMODE_NO_CR;

    DWORD actual = 0; // count actual bytes written as we go along

    if (not (file->modes & RFM_TEXT) or strmode == STRMODE_ALL_CODEPOINTS) {
        //
        // no LF => CR LF translation or error checking needed
        //
        if (length != 0) {
            DWORD dword_actual;
            BOOL ok = WriteFile(
                file->handle,
                data,
                length,
                &dword_actual,
                0
            );

            if (not ok)
                return rebError_OS(GetLastError());

            actual = dword_actual;
        }
    }
    else {
        // !!! This repeats code used in %dev-stdio.c, which is needed when
        // console output is redirected to a file.  It should be shareable.

        unsigned int start = 0;
        unsigned int end = 0;

        while (true) {
            for (; end < length; ++end) {
                switch (strmode) {
                  case STRMODE_NO_CR:
                    if (data[end] == CR)
                        fail (Error_Illegal_Cr(&data[end], data));
                    break;

                  case STRMODE_LF_TO_CRLF:
                    if (data[end] == CR)  // be strict, for sanity
                        fail (Error_Illegal_Cr(&data[end], data));
                    if (data[end] == LF)
                        goto exit_loop;
                    break;

                  default:
                    assert(!"Branch supports LF_TO_CRLF or NO_CR strmodes");
                    break;
                }
            }

          exit_loop:;
            DWORD total_bytes;

            if (start != end) {
                BOOL ok = WriteFile(
                    file->handle,
                    data + start,
                    end - start,
                    &total_bytes,
                    0
                );
                if (not ok)
                    return rebError_OS(GetLastError());
                actual += total_bytes;
            }

            if (data[end] == '\0')
                break;

            assert(strmode == STRMODE_LF_TO_CRLF);
            assert(data[end] == LF);

            BOOL ok = WriteFile(
                file->handle,
                "\r\n",
                2,
                &total_bytes,
                0
            );
            if (not ok)
                return rebError_OS(GetLastError());
            actual += total_bytes;

            ++end;
            start = end;
        }
    }

    DWORD size_high;
    DWORD size_low = GetFileSize(file->handle, &size_high);
    if (size_low == 0xffffffff) {
        DWORD last_error = GetLastError();
        if (last_error != NO_ERROR)
            return rebError_OS(last_error);

        // ...else the file size really is 0xffffffff
    }

    file->size = (cast(int64_t, size_high) << 32) + cast(int64_t, size_low);

    return nullptr;
}


//
//  Query_File: C
//
// Obtain information about a file. Return TRUE on success.
// On error, return FALSE and set file->error code.
//
// Note: time is in local format and must be converted
//
REBVAL *Query_File(FILEREQ *file)
{
    WIN32_FILE_ATTRIBUTE_DATA info;

    // Windows seems to tolerate a trailing slash for directories, hence
    // `/no-tail-slash` is not necessary here for FILE-TO-LOCAL.  If that were
    // used, it would mean `%/` would turn into an empty string, that would
    // cause GetFileAttributesEx() to error, vs. backslash (which works)
    //
    WCHAR *path_wide = rebSpellWide(
        "file-to-local/full @", file->path
    );

    BOOL success = GetFileAttributesEx(
        path_wide, GetFileExInfoStandard, &info
    );

    rebFree(path_wide);

    if (not success)
        return rebError_OS(GetLastError());

    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        file->modes |= RFM_DIR;
    else
        file->modes &= ~RFM_DIR;

    file->size = (cast(int64_t, info.nFileSizeHigh) << 32)
            + cast(int64_t, info.nFileSizeLow);
    file->time.l = info.ftLastWriteTime.dwLowDateTime;
    file->time.h = info.ftLastWriteTime.dwHighDateTime;

    return nullptr;
}


//
//  Create_File: C
//
REBVAL *Create_File(FILEREQ *file)
{
    if (not (file->modes & RFM_DIR))
        return Open_File(file);

    WCHAR *path_wide = rebSpellWide(
        "file-to-local/full/no-tail-slash @", file->path
    );

    LPSECURITY_ATTRIBUTES lpSecurityAttributes = NULL;
    BOOL success = CreateDirectory(path_wide, lpSecurityAttributes);

    rebFree(path_wide);

    if (not success)
        return rebError_OS(GetLastError());

    return nullptr;
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
REBVAL *Delete_File(FILEREQ *file)
{
    WCHAR *path_wide = rebSpellWide(
        "file-to-local/full @", file->path
    );  // leave tail slash on for directory removal

    BOOL success;
    if (file->modes & RFM_DIR)
        success = RemoveDirectory(path_wide);
    else
        success = DeleteFile(path_wide);

    if (not success)
        return rebError_OS(GetLastError());

    rebFree(path_wide);

    return nullptr;
}


//
//  Rename_File: C
//
// Rename a file or directory.
// Note: cannot rename across file volumes.
//
REBVAL *Rename_File(FILEREQ *file, const REBVAL *to)
{
    WCHAR *from_wide = rebSpellWide(
        "file-to-local/full/no-tail-slash @", file->path
    );
    WCHAR *to_wide = rebSpellWide(
        "file-to-local/full/no-tail-slash @", to
    );

    BOOL success = MoveFile(from_wide, to_wide);

    rebFree(to_wide);
    rebFree(from_wide);

    if (not success)
        return rebError_OS(GetLastError());

    return nullptr;
}


//
//  File_Time_To_Rebol: C
//
// Convert file.time to REBOL date/time format.
// Time zone is UTC.
//
REBVAL *File_Time_To_Rebol(FILEREQ *file)
{
    SYSTEMTIME stime;
    TIME_ZONE_INFORMATION tzone;

    if (TIME_ZONE_ID_DAYLIGHT == GetTimeZoneInformation(&tzone))
        tzone.Bias += tzone.DaylightBias;

    FileTimeToSystemTime(cast(FILETIME *, &file->time), &stime);

    return rebValue("ensure date! (make-date-ymdsnz",
        rebI(stime.wYear),  // year
        rebI(stime.wMonth),  // month
        rebI(stime.wDay),  // day
        rebI(
            stime.wHour * 3600 + stime.wMinute * 60 + stime.wSecond
        ),  // "secs"
        rebI(1000000 * stime.wMilliseconds), // nano
        rebI(-tzone.Bias),  // zone
    ")");
}


//
//  Get_Current_Dir_Value: C
//
// Return the current directory path as a FILE!.  Result should be freed
// with rebRelease()
//
REBVAL *Get_Current_Dir_Value(void)
{
    DWORD len = GetCurrentDirectory(0, NULL); // length, incl terminator.
    WCHAR *path = rebAllocN(WCHAR, len);
    GetCurrentDirectory(len, path);

    REBVAL *result = rebValue("local-to-file/dir", rebR(rebTextWide(path)));
    rebFree(path);
    return result;
}


//
//  Set_Current_Dir_Value: C
//
// Set the current directory to local path.  Return false on failure.
//
bool Set_Current_Dir_Value(const REBVAL *path)
{
    WCHAR *path_wide = rebSpellWide("file-to-local/full", path);

    BOOL success = SetCurrentDirectory(path_wide);

    rebFree(path_wide);

    return success == TRUE;
}


//
//  Get_Current_Exec: C
//
// Return the current executable path as a FILE!.  The result should be freed
// with rebRelease()
//
REBVAL *Get_Current_Exec(void)
{
    WCHAR *path = rebAllocN(WCHAR, MAX_PATH);

    DWORD r = GetModuleFileName(NULL, path, MAX_PATH);
    if (r == 0) {
        rebFree(path);
        return nullptr;
    }
    path[r] = '\0';  // May not be NULL-terminated if buffer is not big enough

    REBVAL *result = rebValue(
        "local-to-file", rebR(rebTextWide(path))
    );
    rebFree(path);

    return result;
}
