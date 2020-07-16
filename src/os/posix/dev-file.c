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
//
static int Is_Dir(const char *path_utf8, const char *name_utf8)
{
    size_t size_path = strsize(path_utf8);
    size_t size_name = strsize(name_utf8);

    // !!! No clue why + 13 is needed, and not sure I want to know.
    // It was in the original code, not second-guessing ATM.  --@HF
    //
    char *full_utf8 = rebAllocN(char, size_path + 1 + size_name + 1 + 13);

    strncpy(full_utf8, path_utf8, size_path + 1); // include terminator

    // Avoid UNC-path "//name" on Cygwin.
    //
    if (size_path > 0 && full_utf8[size_path - 1] != '/')
        strncat(full_utf8, "/", 1);

    strncat(full_utf8, name_utf8, size_name);

    struct stat st;
    int stat_result = stat(full_utf8, &st);

    rebFree(full_utf8);

    if (stat_result != 0)
        return 0; // !!! What's the proper result?

    return S_ISDIR(st.st_mode);
}


static bool Seek_File_64(struct devreq_file *file)
{
    // Performs seek and updates index value. TRUE on success.

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

    if (result < 0)
        return false; // errno should still be good for caller to use

    file->index = result;

    return true;
}


static int Get_File_Info(struct devreq_file *file)
{
    // The original implementation here used /no-trailing-slash for the
    // FILE-TO-LOCAL, which meant that %/ would turn into an empty string.
    // It would appear that for directories, trailing slashes are acceptable
    // in `stat`...though for symlinks different answers are given based
    // on the presence of the slash:
    //
    // https://superuser.com/questions/240743/
    //
    char *path_utf8 = rebSpell("file-to-local/full", file->path, rebEND);

    struct stat info;
    int stat_result = stat(path_utf8, &info);

    rebFree(path_utf8);

    REBREQ *req = AS_REBREQ(file);

    if (stat_result != 0)
        rebFail_OS (errno);

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
    REBREQ *dir_req = AS_REBREQ(dir);
    REBREQ *file_req = AS_REBREQ(file);

    // Note: /WILD append of * is not necessary on POSIX
    //
    char *dir_utf8 = rebSpell("file-to-local", dir->path, rebEND);

    // If no dir handle, open the dir:
    //
    DIR *h;
    if ((h = cast(DIR*, dir_req->requestee.handle)) == NULL) {
        h = opendir(dir_utf8); // !!! does opendir() hold pointer?

        if (h == NULL) {
            rebFree(dir_utf8);
            rebFail_OS (errno);
        }

        dir_req->requestee.handle = h;
        dir_req->flags &= ~RRF_DONE;
    }

    // Get dir entry (skip over the . and .. dir cases):
    //
    char *file_utf8;
    struct dirent *d;
    do {
        // Read next file entry or error:
        errno = 0; // set errno to 0 to test if it changes
        if ((d = readdir(h)) == NULL) {
            int errno_cache = errno; // in case closedir() changes it

            rebFree(dir_utf8);

            closedir(h);
            dir_req->requestee.handle = 0;

            if (errno_cache != 0)
                rebFail_OS (errno_cache);

            dir_req->flags |= RRF_DONE; // no more files
            return DR_DONE;
        }
        file_utf8 = d->d_name;
    } while (
        file_utf8[0] == '.'
        && (
            file_utf8[1] == '\0'
            || (file_utf8[1] == '.' && file_utf8[2] == '\0')
        )
    );

    file_req->modes = 0;

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

    if (Is_Dir(dir_utf8, file_utf8))
        file_req->modes |= RFM_DIR;

    file->path = rebValue(
        "applique 'local-to-file [",
            "path:", rebT(file_utf8),
            "dir:", rebR(rebLogic(file_req->modes & RFM_DIR)),
        "]", rebEND
    );

    // !!! We currently unmanage this, because code using the API may
    // trigger a GC and there is nothing proxying the RebReq's data.
    // Long term, this file should have *been* the return result.
    //
    rebUnmanage(m_cast(REBVAL*, file->path));

    rebFree(dir_utf8);

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
    struct devreq_file *file = DEVREQ_FILE(req);

    // "Posix file names should be compatible with REBOL file paths"

    assert(file->path != NULL);

    int modes = O_BINARY | ((req->modes & RFM_READ) ? O_RDONLY : O_RDWR);

    if ((req->modes & (RFM_WRITE | RFM_APPEND)) != 0) {
        modes = O_BINARY | O_RDWR | O_CREAT;
        if (
            did (req->modes & RFM_NEW)
            or (req->modes & (RFM_READ | RFM_APPEND | RFM_SEEK)) == 0
        ){
            modes |= O_TRUNC;
        }
    }

    //modes |= (req->modes & RFM_SEEK) ? O_RANDOM : O_SEQUENTIAL;

    int access = 0;
    if (req->modes & RFM_READONLY)
        access = S_IREAD;
    else
        access = S_IREAD | S_IWRITE | S_IRGRP | S_IWGRP | S_IROTH;

    // Open the file:
    // printf("Open: %s %d %d\n", path, modes, access);

    char *path_utf8 = rebSpell(
        "applique 'file-to-local [",
            "path:", file->path,
            "wild:", rebR(rebLogic(req->modes & RFM_DIR)), // !!! necessary?
            "full: true"
        "]", rebEND
    );

    struct stat info;
    int h = open(path_utf8, modes, access);

    rebFree(path_utf8);

    if (h < 0)
        rebFail_OS (errno);

    // Confirm that a seek-mode file is actually seekable:
    if (req->modes & RFM_SEEK) {
        if (lseek(h, 0, SEEK_CUR) < 0) {
            int errno_cache = errno;
            close(h);
            rebFail_OS (errno_cache);
        }
    }

    // Fetch file size (if fails, then size is assumed zero):
    if (fstat(h, &info) == 0) {
        file->size = info.st_size;
        file->time.l = cast(long, info.st_mtime);
    }

    req->requestee.id = h;

    return DR_DONE;
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
    struct devreq_file *file = DEVREQ_FILE(req);

    if (req->modes & RFM_DIR) {
        return Read_Directory(
            file,
            cast(struct devreq_file*, req->common.data)
        );
    }

    assert(req->requestee.id != 0);

    if ((req->modes & (RFM_SEEK | RFM_RESEEK)) != 0) {
        req->modes &= ~RFM_RESEEK;
        if (not Seek_File_64(file))
            rebFail_OS (errno);
    }

    // printf("read %d len %d\n", req->requestee.id, req->length);

    ssize_t bytes = read(
        req->requestee.id, req->common.data, req->length
    );

    if (bytes < 0)
        rebFail_OS (errno);

    req->actual = bytes;
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

    assert(req->requestee.id != 0);

    if (req->modes & RFM_APPEND) {
        req->modes &= ~RFM_APPEND;
        lseek(req->requestee.id, 0, SEEK_END);
    }

    if ((req->modes & (RFM_SEEK | RFM_RESEEK | RFM_TRUNCATE)) != 0) {
        req->modes &= ~RFM_RESEEK;
        if (not Seek_File_64(file))
            rebFail_OS (errno);

        if (req->modes & RFM_TRUNCATE)
            if (ftruncate(req->requestee.id, file->index) != 0)
                rebFail_OS (errno);
    }

    if (req->length == 0)
        return DR_DONE;

    ssize_t bytes = write(req->requestee.id, req->common.data, req->length);
    if (bytes < 0)
        rebFail_OS (errno);

    req->actual = bytes;
    return DR_DONE;
}


//
//  Query_File: C
//
// Obtain information about a file. Return TRUE on success.
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
    if (not (req->modes & RFM_DIR))
        return Open_File(req);

    char *path_utf8 = rebSpell(
        "file-to-local/full/no-tail-slash", file->path,
        rebEND
    );

    int mkdir_result = mkdir(path_utf8, 0777);

    rebFree(path_utf8);

    if (mkdir_result != 0)
        rebFail_OS (errno);

    return DR_DONE;
}


//
//  Delete_File: C
//
// Delete a file or directory. Return TRUE if it was done.
// The file->path provides the directory path and name.
//
// Note: Dirs must be empty to succeed
//
DEVICE_CMD Delete_File(REBREQ *req)
{
    struct devreq_file *file = DEVREQ_FILE(req);

    char *path_utf8 = rebSpell(
        "file-to-local/full", file->path,
        rebEND // leave tail slash on for directory removal
    );

    int removal_result;
    if (req->modes & RFM_DIR)
        removal_result = rmdir(path_utf8);
    else
        removal_result = remove(path_utf8);

    rebFree(path_utf8);

    if (removal_result != 0)
        rebFail_OS (errno);

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

    char *from_utf8 = rebSpell(
        "file-to-local/full/no-tail-slash", file->path,
        rebEND
    );
    char *to_utf8 = rebSpell(
        "file-to-local/full/no-tail-slash", to,
        rebEND
    );

    int rename_result = rename(from_utf8, to_utf8);

    rebFree(to_utf8);
    rebFree(from_utf8);

    if (rename_result != 0)
        rebFail_OS (errno);

    return DR_DONE;
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
