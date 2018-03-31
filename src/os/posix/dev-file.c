//
//  File: %dev-file.c
//  Summary: "Device: File access for Posix"
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
// -D_FILE_OFFSET_BITS=64 to support large files
//

// ftruncate is not a standard C function, but as we are using it then
// we have to use a special define if we want standards enforcement.
// By defining it as the first header file we include, we ensure another
// inclusion of <unistd.h> won't be made without the definition first.
//
//     http://stackoverflow.com/a/26806921/211160
//
#define _XOPEN_SOURCE 500

// !!! See notes on why this is needed on #define HAS_POSIX_SIGNAL in
// reb-config.h (similar reasons, and means this file cannot be
// compiled as --std=c99 but rather --std=gnu99)
//
#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <assert.h>

#include "reb-host.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif


// The BSD legacy names S_IREAD/S_IWRITE are not defined several places.
// That includes building on Android, or if you compile as C99.

#ifndef S_IREAD
    #define S_IREAD S_IRUSR
#endif

#ifndef S_IWRITE
    #define S_IWRITE S_IWUSR
#endif

// NOTE: the code below assumes a file id will never be zero.  In POSIX,
// 0 represents standard input...which is handled by dev-stdio.c.
// Though 0 for stdin is a POSIX standard, many C compilers define
// STDIN_FILENO, STDOUT_FILENO, STDOUT_FILENO.  These may be set to
// different values in unusual circumstances, such as emscripten builds.


/***********************************************************************
**
**  Local Functions
**
***********************************************************************/

// dirent.d_type is a BSD extension, actually not part of POSIX
// reformatted from: http://ports.haiku-files.org/wiki/CommonProblems
// this comes from: http://ports.haiku-files.org/wiki/CommonProblems
// modified for reformatting and to not use a variable-length-array
static int Is_Dir(const char *path, const char *name)
{
    int len_path = strlen(path);
    int len_name = strlen(name);
    struct stat st;

    // !!! No clue why + 13 is needed, and not sure I want to know.
    // It was in the original code, not second-guessing ATM.  --@HF
    char *pathname = OS_ALLOC_N(char, len_path + 1 + len_name + 1 + 13);

    strcpy(pathname, path);

    /* Avoid UNC-path "//name" on Cygwin.  */
    if (len_path > 0 && pathname[len_path - 1] != '/')
        strcat(pathname, "/");

    strcat(pathname, name);

    if (stat(pathname, &st)) {
        OS_FREE(pathname);
        return 0;
    }

    OS_FREE(pathname);
    return S_ISDIR(st.st_mode);
}


static REBOOL Seek_File_64(struct devreq_file *file)
{
    // Performs seek and updates index value. TRUE on success.
    // On error, returns FALSE and sets req->error field.
    REBREQ *req = AS_REBREQ(file);

    int h = req->requestee.id;
    int64_t result;

    if (file->index == -1) {
        // Append:
        result = lseek(h, 0, SEEK_END);
    }
    else {
        result = lseek(h, file->index, SEEK_SET);
    }

    if (result < 0) {
        req->error = -RFE_NO_SEEK;
        return FALSE;
    }

    file->index = result;

    return TRUE;
}


static int Get_File_Info(struct devreq_file *file)
{
    struct stat info;

    REBREQ *req = AS_REBREQ(file);

    if (stat(file->path, &info)) {
        req->error = errno;
        return DR_ERROR;
    }

    if (S_ISDIR(info.st_mode)) {
        req->modes |= RFM_DIR;
        file->size = 0; // in order to be consistent on all systems
    }
    else {
        req->modes &= ~RFM_DIR;
        file->size = info.st_size;
    }
    file->time.l = cast(long, info.st_mtime);

    return DR_DONE;
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
// is set to zero and a dir->path string for the directory.
//
// The directory is opened and a handle is stored in the dir
// structure for use on subsequent calls. If an error occurred,
// dir->error is set to the error code and -1 is returned.
// The dir->size field can be set to the number of files in the
// dir, if it is known. The dir->index field can be used by this
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
// Note that the dir->path can contain wildcards * and ?. The
// processing of these can be done in the OS (if supported) or
// by a separate filter operation during the read.
//
// Store file date info in file->index or other fields?
// Store permissions? Ownership? Groups? Or, require that
// to be part of a separate request?
//
static int Read_Directory(struct devreq_file *dir, struct devreq_file *file)
{
    struct dirent *d;
    char *cp;
    DIR *h;
    int n;

    REBREQ *dir_req = AS_REBREQ(dir);
    REBREQ *file_req = AS_REBREQ(file);

    // Remove * from tail, if present. (Allowed because the
    // path was copied into to-local-path first).
    n = strlen(cp = dir->path);
    if (n > 0 && cp[n-1] == '*') cp[n-1] = 0;

    // If no dir handle, open the dir:
    if (!(h = cast(DIR*, dir_req->requestee.handle))) {
        h = opendir(dir->path);
        if (!h) {
            dir_req->error = errno;
            return DR_ERROR;
        }
        dir_req->requestee.handle = h;
        dir_req->flags &= ~RRF_DONE;
    }

    // Get dir entry (skip over the . and .. dir cases):
    do {
        // Read next file entry or error:
        if (!(d = readdir(h))) {
            //dir->error = errno;
            closedir(h);
            dir_req->requestee.handle = 0;
            //if (dir->error) return DR_ERROR;
            dir_req->flags |= RRF_DONE; // no more files
            return DR_DONE;
        }
        cp = d->d_name;
    } while (cp[0] == '.' && (cp[1] == 0 || (cp[1] == '.' && cp[2] == 0)));

    file_req->modes = 0;
    strncpy(file->path, cp, MAX_FILE_NAME);

#if 0
    // NOTE: we do not use d_type even if DT_DIR is #define-d.  First of all,
    // it's not a POSIX requirement and not all operating systems support it.
    // (Linux/BSD have it defined in their structs, but Haiku doesn't--for
    // instance).  But secondly, even if your OS supports it...a filesystem
    // doesn't have to.  (Examples: VirtualBox shared folders, XFS.)

    if (d->d_type == DT_DIR)
        file_req->modes |= RFM_DIR;
#endif

    // More widely supported mechanism of determining if something is a
    // directory, although less efficient than DT_DIR (because it requires
    // making an additional filesystem call)

    if (Is_Dir(dir->path, file->path))
        file_req->modes |= RFM_DIR;

    // Line below DOES NOT WORK -- because we need full path.
    //Get_File_Info(file); // updates modes, size, time

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
DEVICE_CMD Open_File(REBREQ *req)
{
    int h;
    struct stat info;

    struct devreq_file *file = DEVREQ_FILE(req);

    // Posix file names should be compatible with REBOL file paths:
    char *path;
    if (!(path = file->path)) {
        req->error = -RFE_BAD_PATH;
        return DR_ERROR;
    }

    int modes = O_BINARY | ((req->modes & RFM_READ) ? O_RDONLY : O_RDWR);

    if ((req->modes & (RFM_WRITE | RFM_APPEND)) != 0) {
        modes = O_BINARY | O_RDWR | O_CREAT;
        if (
            LOGICAL(req->modes & RFM_NEW) ||
            (req->modes & (RFM_READ | RFM_APPEND | RFM_SEEK)) == 0
        ){
            modes |= O_TRUNC;
        }
    }

    //modes |= LOGICAL(req->modes & RFM_SEEK) ? O_RANDOM : O_SEQUENTIAL;

    int access = 0;
    if (req->modes & RFM_READONLY)
        access = S_IREAD;
    else
        access = S_IREAD | S_IWRITE | S_IRGRP | S_IWGRP | S_IROTH;

    // Open the file:
    // printf("Open: %s %d %d\n", path, modes, access);
    h = open(path, modes, access);
    if (h < 0) {
        req->error = -RFE_OPEN_FAIL;
        goto fail;
    }

    // Confirm that a seek-mode file is actually seekable:
    if (req->modes & RFM_SEEK) {
        if (lseek(h, 0, SEEK_CUR) < 0) {
            close(h);
            req->error = -RFE_BAD_SEEK;
            goto fail;
        }
    }

    // Fetch file size (if fails, then size is assumed zero):
    if (fstat(h, &info) == 0) {
        file->size = info.st_size;
        file->time.l = cast(long, info.st_mtime);
    }

    req->requestee.id = h;

    return DR_DONE;

fail:
    return DR_ERROR;
}


//
//  Close_File: C
//
// Closes a previously opened file.
//
DEVICE_CMD Close_File(REBREQ *req)
{
    if (req->requestee.id) {
        close(req->requestee.id);
        req->requestee.id = 0;
    }
    return DR_DONE;
}


//
//  Read_File: C
//
DEVICE_CMD Read_File(REBREQ *req)
{
    ssize_t bytes = 0;

    struct devreq_file *file = DEVREQ_FILE(req);

    if (req->modes & RFM_DIR) {
        return Read_Directory(file, cast(struct devreq_file*, req->common.data));
    }

    if (!req->requestee.id) {
        req->error = -RFE_NO_HANDLE;
        return DR_ERROR;
    }

    if ((req->modes & (RFM_SEEK | RFM_RESEEK)) != 0) {
        req->modes &= ~RFM_RESEEK;
        if (!Seek_File_64(file))
            return DR_ERROR;
    }

    // printf("read %d len %d\n", req->requestee.id, req->length);

    bytes = read(req->requestee.id, req->common.data, req->length);
    if (bytes < 0) {
        req->error = -RFE_BAD_READ;
        return DR_ERROR;
    } else {
        req->actual = bytes;
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
    ssize_t bytes = 0;

    struct devreq_file *file = DEVREQ_FILE(req);

    if (!req->requestee.id) {
        req->error = -RFE_NO_HANDLE;
        return DR_ERROR;
    }

    if (req->modes & RFM_APPEND) {
        req->modes &= ~RFM_APPEND;
        lseek(req->requestee.id, 0, SEEK_END);
    }

    if ((req->modes & (RFM_SEEK | RFM_RESEEK | RFM_TRUNCATE)) != 0) {
        req->modes &= ~RFM_RESEEK;
        if (!Seek_File_64(file))
            return DR_ERROR;
        if (req->modes & RFM_TRUNCATE)
            if (ftruncate(req->requestee.id, file->index))
                return DR_ERROR;
    }

    if (req->length == 0) return DR_DONE;

    req->actual = bytes = write(req->requestee.id, req->common.data, req->length);
    if (bytes < 0) {
        if (errno == ENOSPC) req->error = -RFE_DISK_FULL;
        else req->error = -RFE_BAD_WRITE;
        return DR_ERROR;
    }

    return DR_DONE;
}


//
//  Query_File: C
//
// Obtain information about a file. Return TRUE on success.
// On error, return FALSE and set req->error code.
//
// Note: time is in local format and must be converted
//
DEVICE_CMD Query_File(REBREQ *req)
{
    return Get_File_Info(DEVREQ_FILE(req));
}


//
//  Create_File: C
//
DEVICE_CMD Create_File(REBREQ *req)
{
    struct devreq_file *file = DEVREQ_FILE(req);
    if (req->modes & RFM_DIR) {
        if (!mkdir(file->path, 0777)) return DR_DONE;
        req->error = errno;
        return DR_ERROR;
    } else
        return Open_File(req);
}


//
//  Delete_File: C
//
// Delete a file or directory. Return TRUE if it was done.
// The file->path provides the directory path and name.
// For errors, return FALSE and set req->error to error code.
//
// Note: Dirs must be empty to succeed
//
DEVICE_CMD Delete_File(REBREQ *req)
{
    struct devreq_file *file = DEVREQ_FILE(req);

    if (req->modes & RFM_DIR) {
        if (!rmdir(file->path))
            return DR_DONE;
    }
    else {
        if (!remove(file->path))
            return DR_DONE;
    }

    req->error = errno;
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

    if (!rename(file->path, s_cast(req->common.data)))
        return DR_DONE;
    req->error = errno;
    return DR_ERROR;
}


//
//  Poll_File: C
//
DEVICE_CMD Poll_File(REBREQ *req)
{
    UNUSED(req);
    return DR_DONE;     // files are synchronous (currently)
}


/***********************************************************************
**
**  Command Dispatch Table (RDC_ enum order)
**
***********************************************************************/

static DEVICE_CMD_FUNC Dev_Cmds[RDC_MAX] = {
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
