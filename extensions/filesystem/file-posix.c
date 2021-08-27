//
//  File: %dev-file.c
//  Summary: "Device: File access for Posix"
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

#ifndef __cplusplus
    // See feature_test_macros(7)
    // This definition is redundant under C++
    #define _GNU_SOURCE
#endif

// Typically, including stdio.h is not done in release builds.
// (That is to avoid linking in functions like printf(), etc.)
//
// But this file needs it for remove(), rmdir(), rename()
// (unlink() in unistd.h works for remove() and rmdir(), but not rename())
//
#include <stdio.h>

#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>  // includes `O_XXX` constant definitions
#include <dirent.h>
#include <errno.h>
#include <assert.h>

#include <time.h>
#ifndef timeval
    #include <sys/time.h>  // for older systems
#endif

#include "sys-core.h"

#include "file-req.h"

#ifndef O_BINARY
    #define O_BINARY 0
#endif

#ifndef PATH_MAX
    #define PATH_MAX 4096  // generally lacking in Posix
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
//
// modifications:
// * use Rebol allocator instead of a variable-length-array (VLA)
// * avoidance of warning on strncat: [-Werror=stringop-truncation]
//
static bool Is_Dir(const char *path_utf8, const char *name_utf8)
{
    size_t size_path = strsize(path_utf8);
    size_t size_name = strsize(name_utf8);
    size_t size_full = size_path + size_name;

    // Avoid UNC-path "//name" on Cygwin.
    //
    if (size_path > 0 and path_utf8[size_path - 1] != '/')
        size_full += 1;

    // !!! No clue why + 13 is needed, and not sure I want to know.
    // It was in the original FAQ code, not second-guessing ATM.  --@HF
    //
    char *full_utf8 = rebAllocN(char, size_full + 1 + 13);

    strncpy(full_utf8, path_utf8, size_path + 1);  // include terminator

    // Avoid UNC-path "//name" on Cygwin.
    //
    if (size_path > 0 and path_utf8[size_path - 1] != '/')
        strcat(full_utf8, "/");

    strncat(full_utf8, name_utf8, size_full - 1);

    struct stat st;
    int stat_result = stat(full_utf8, &st);

    rebFree(full_utf8);

    return (stat_result != 0) ? false : did S_ISDIR(st.st_mode);
}


static bool Seek_File_64(FILEREQ *file)
{
    // Performs seek and updates index value. TRUE on success.

    int h = file->id;
    int64_t result;

    if (file->index == -1)  // append
        result = lseek(h, 0, SEEK_END);
    else
        result = lseek(h, file->index, SEEK_SET);

    if (result < 0)
        return false;  // errno should still be good for caller to use

    file->index = result;

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
REBVAL *Read_Directory(bool *done, FILEREQ *dir, FILEREQ *file)
{
    memset(file, 0, sizeof(FILEREQ));

    // Note: /WILD append of * is not necessary on POSIX
    //
    char *dir_utf8 = rebSpell("file-to-local", dir->path);

    // If no dir handle, open the dir:
    //
    DIR *h;
    if ((h = cast(DIR*, dir->handle)) == NULL) {
        h = opendir(dir_utf8); // !!! does opendir() hold pointer?

        if (h == NULL) {
            rebFree(dir_utf8);
            return rebError_OS(errno);
        }

        dir->handle = h;
        *done = false;
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
            dir->handle = 0;

            if (errno_cache != 0)
                return rebError_OS(errno_cache);

            *done = true; // no more files
            return nullptr;
        }
        file_utf8 = d->d_name;
    } while (
        file_utf8[0] == '.'
        && (
            file_utf8[1] == '\0'
            || (file_utf8[1] == '.' && file_utf8[2] == '\0')
        )
    );

    file->modes = 0;

  #if 0
    // NOTE: we do not use d_type even if DT_DIR is #define-d.  First of all,
    // it's not a POSIX requirement and not all operating systems support it.
    // (Linux/BSD have it defined in their structs, but Haiku doesn't--for
    // instance).  But secondly, even if your OS supports it...a filesystem
    // doesn't have to.  (Examples: VirtualBox shared folders, XFS.)

    if (d->d_type == DT_DIR)
        file->modes |= RFM_DIR;
  #endif

    // More widely supported mechanism of determining if something is a
    // directory, although less efficient than DT_DIR (because it requires
    // making an additional filesystem call)

    if (Is_Dir(dir_utf8, file_utf8))
        file->modes |= RFM_DIR;

    file->path = rebValue(
        "applique :local-to-file [",
            "path:", rebT(file_utf8),
            "dir: if", rebL(file->modes & RFM_DIR), "'#",
        "]"
    );

    // !!! We currently unmanage this, because code using the API may
    // trigger a GC and there is nothing proxying the RebReq's data.
    // Long term, this file should have *been* the return result.
    //
    rebUnmanage(m_cast(REBVAL*, file->path));

    rebFree(dir_utf8);
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
REBVAL *Open_File(FILEREQ *file)
{
    // "Posix file names should be compatible with REBOL file paths"

    assert(file->path != NULL);

    int modes = O_BINARY | ((file->modes & RFM_READ) ? O_RDONLY : O_RDWR);

    if ((file->modes & (RFM_WRITE | RFM_APPEND)) != 0) {
        modes = O_BINARY | O_RDWR | O_CREAT;
        if (
            did (file->modes & RFM_NEW)
            or (file->modes & (RFM_READ | RFM_APPEND | RFM_SEEK)) == 0
        ){
            modes |= O_TRUNC;
        }
    }

    // Code here was commented out:
    //
    //   modes |= (file->modes & RFM_SEEK) ? O_RANDOM : O_SEQUENTIAL;

    int access = 0;
    if (file->modes & RFM_READONLY)
        access = S_IREAD;
    else
        access = S_IREAD | S_IWRITE | S_IRGRP | S_IWGRP | S_IROTH;

    // Open the file:
    // printf("Open: %s %d %d\n", path, modes, access);

    char *path_utf8 = rebSpell(
        "applique :file-to-local [",
            "path:", file->path,
            "wild: if", rebL(file->modes & RFM_DIR), "'#",  // !!! necessary?
            "full: #",
        "]"
    );

    struct stat info;
    int h = open(path_utf8, modes, access);

    rebFree(path_utf8);

    if (h < 0)
        return rebError_OS(errno);

    // Confirm that a seek-mode file is actually seekable:
    if (file->modes & RFM_SEEK) {
        if (lseek(h, 0, SEEK_CUR) < 0) {
            int errno_cache = errno;
            close(h);
            return rebError_OS(errno_cache);
        }
    }

    // Fetch file size (if fails, then size is assumed zero):
    if (fstat(h, &info) == 0) {
        file->size = info.st_size;
        file->time.l = cast(long, info.st_mtime);
    }

    file->id = h;
    return nullptr;
}


//
//  Close_File: C
//
// Closes a previously opened file.
//
REBVAL *Close_File(FILEREQ *file)
{
    if (file->id) {
        close(file->id);
        file->id = 0;
    }
    return nullptr;
}


//
//  Read_File: C
//
REBVAL *Read_File(size_t *actual, FILEREQ *file, REBYTE *data, size_t length)
{
    assert(not (file->modes & RFM_DIR));  // should call Read_Directory!

    assert(file->id != 0);

    if ((file->modes & (RFM_SEEK | RFM_RESEEK)) != 0) {
        file->modes &= ~RFM_RESEEK;
        if (not Seek_File_64(file))
            return rebError_OS(errno);
    }

    ssize_t bytes = read(file->id, data, length);
    if (bytes < 0)
        return rebError_OS(errno);

    *actual = bytes;
    file->index += bytes;

    return nullptr;
}


//
//  Write_File: C
//
// Bug?: update file->size value after write !?
//
REBVAL *Write_File(FILEREQ *file, const REBYTE *data, size_t length)
{
    assert(file->id != 0);

    if (file->modes & RFM_APPEND) {
        file->modes &= ~RFM_APPEND;
        lseek(file->id, 0, SEEK_END);
    }

    if ((file->modes & (RFM_SEEK | RFM_RESEEK | RFM_TRUNCATE)) != 0) {
        file->modes &= ~RFM_RESEEK;
        if (not Seek_File_64(file))
            return rebError_OS(errno);

        if (file->modes & RFM_TRUNCATE)
            if (ftruncate(file->id, file->index) != 0)
                return rebError_OS(errno);
    }

    size_t actual = 0;  // count actual bytes written as we go along

    if (length == 0)
        return nullptr;

    // !!! This repeats code in %file-windows.c for doing CR LF handling.
    // See the notes there.  This needs to be captured in some kind of
    // streaming codec built on a byte-level service.
    //
    const enum Reb_Strmode strmode = STRMODE_NO_CR;  // we assume this for now

    if (not (file->modes & RFM_TEXT) or strmode == STRMODE_ALL_CODEPOINTS) {
        //
        // no LF => CR LF translation or error checking needed
        //
        assert(length != 0);  // checked above
        ssize_t bytes = write(file->id, data, length);
        if (bytes < 0)
            return rebError_OS(errno);

        actual = bytes;
    }
    else {
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
            if (start != end) {
                ssize_t bytes = write(file->id, data, length);
                if (bytes < 0)
                    return rebError_OS(errno);
                actual += bytes;
            }

            if (data[end] == '\0')
                break;

            assert(strmode == STRMODE_LF_TO_CRLF);
            assert(data[end] == LF);

            blockscope {
                int bytes = write(file->id, data, length);
                if (bytes < 0)
                    return rebError_OS(errno);
                actual += bytes;
            }

            ++end;
            start = end;
        }
    }

    assert(actual == length);

    return nullptr;
}


//
//  Query_File: C
//
// Obtain information about a file. Return TRUE on success.
//
// Note: time is in local format and must be converted
//
REBVAL *Query_File(FILEREQ *file)
{
    // The original implementation here used /no-trailing-slash for the
    // FILE-TO-LOCAL, which meant that %/ would turn into an empty string.
    // It would appear that for directories, trailing slashes are acceptable
    // in `stat`...though for symlinks different answers are given based
    // on the presence of the slash:
    //
    // https://superuser.com/questions/240743/
    //
    char *path_utf8 = rebSpell("file-to-local/full", file->path);

    struct stat info;
    int stat_result = stat(path_utf8, &info);

    rebFree(path_utf8);

    if (stat_result != 0)
        return rebError_OS (errno);

    if (S_ISDIR(info.st_mode)) {
        file->modes |= RFM_DIR;
        file->size = 0;  // "to be consistent on all systems" ?
    }
    else {
        file->modes &= ~RFM_DIR;
        file->size = info.st_size;
    }
    file->time.l = cast(long, info.st_mtime);

    return nullptr;
}


//
//  Create_File: C
//
REBVAL *Create_File(FILEREQ *file)
{
    if (not (file->modes & RFM_DIR))
        return Open_File(file);

    char *path_utf8 = rebSpell(
        "file-to-local/full/no-tail-slash", file->path
    );

    int mkdir_result = mkdir(path_utf8, 0777);

    rebFree(path_utf8);

    if (mkdir_result != 0)
        return rebError_OS(errno);

    return nullptr;
}


//
//  Delete_File: C
//
// Delete a file or directory. Return TRUE if it was done.
// The file->path provides the directory path and name.
//
// Note: Dirs must be empty to succeed
//
REBVAL *Delete_File(FILEREQ *file)
{
    char *path_utf8 = rebSpell(
        "file-to-local/full", file->path
        // leave tail slash on for directory removal
    );

    int removal_result;
    if (file->modes & RFM_DIR)
        removal_result = rmdir(path_utf8);
    else
        removal_result = remove(path_utf8);

    rebFree(path_utf8);

    if (removal_result != 0)
        return rebError_OS (errno);

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
    char *from_utf8 = rebSpell(
        "file-to-local/full/no-tail-slash", file->path
    );
    char *to_utf8 = rebSpell("file-to-local/full/no-tail-slash", to);

    int rename_result = rename(from_utf8, to_utf8);

    rebFree(to_utf8);
    rebFree(from_utf8);

    if (rename_result != 0)
        return rebError_OS(errno);

    return nullptr;
}


//
//  Get_Timezone: C
//
// Get the time zone in minutes from GMT.
// NOT consistently supported in Posix OSes!
// We have to use a few different methods.
//
// !!! "local_tm->tm_gmtoff / 60 would make the most sense,
// but is no longer used" (said a comment)
//
// !!! This code is currently repeated in the time extension, until a better
// way of sharing it is accomplished.
//
static int Get_Timezone(struct tm *utc_tm_unused)
{
    time_t now_secs;
    time(&now_secs); // UNIX seconds (since "epoch")
    struct tm local_tm = *localtime(&now_secs);

  #if !defined(HAS_SMART_TIMEZONE)
    //
    // !!! The R3-Alpha host code would always give back times in UTC plus a
    // timezone.  Then, functions like NOW would have ways of adjusting for
    // the timezone (unless you asked to do something like NOW/UTC), but
    // without taking daylight savings time into account.
    //
    // We don't want to return a fake UTC time to the caller for the sake of
    // keeping the time zone constant.  So this should return e.g. GMT-7
    // during pacific daylight time, and GMT-8 during pacific standard time.
    // Get that effect by erasing the is_dst flag out of the local time.
    //
    local_tm.tm_isdst = 0;
  #endif

    // mktime() function inverts localtime()... there is no equivalent for
    // gmtime().  However, we feed it a gmtime() as if it were the localtime.
    // Then the time zone can be calculated by diffing it from a mktime()
    // inversion of a suitable local time.
    //
    // !!! For some reason, R3-Alpha expected the caller to pass in a utc tm
    // structure pointer but then didn't use it, choosing to make another call
    // to gmtime().  Review.
    //
    UNUSED(utc_tm_unused);
    time_t now_secs_gm = mktime(gmtime(&now_secs));

    double diff = difftime(mktime(&local_tm), now_secs_gm);
    return cast(int, diff / 60);
}


//
//  File_Time_To_Rebol: C
//
// Convert file.time to REBOL date/time format.
// Time zone is UTC.
//
REBVAL *File_Time_To_Rebol(FILEREQ *file)
{
    time_t stime;

    if (sizeof(time_t) > sizeof(file->time.l)) {
        int64_t t = file->time.l;
        t |= cast(int64_t, file->time.h) << 32;
        stime = t;
    }
    else
        stime = file->time.l;

    // gmtime() is badly named.  It's utc time.  Note we have to be careful as
    // it returns a system static buffer, so we have to copy the result
    // via dereference to avoid calls to localtime() inside Get_Timezone
    // from corrupting the buffer before it gets used.
    //
    // !!! Consider usage of the thread-safe variants, though they are not
    // available on all older systems.
    //
    struct tm utc_tm = *gmtime(&stime);

    int zone = Get_Timezone(&utc_tm);

    return rebValue("ensure date! (make-date-ymdsnz",
        rebI(utc_tm.tm_year + 1900),  // year
        rebI(utc_tm.tm_mon + 1),  // month
        rebI(utc_tm.tm_mday),  // day
        rebI(
            utc_tm.tm_hour * 3600
            + utc_tm.tm_min * 60
            + utc_tm.tm_sec
        ),  // secs
        rebI(0),  // nanoseconds (file times don't have this)
        rebI(zone),  // zone
    ")");
}


//
//  Get_Current_Dir_Value: C
//
// Return the current directory path as a FILE!.  The result should be freed
// with rebRelease()
//
REBVAL *Get_Current_Dir_Value(void)
{
    char *path = rebAllocN(char, PATH_MAX);

    if (getcwd(path, PATH_MAX - 1) == 0) {
        rebFree(path);
        return rebBlank();
    }

    REBVAL *result = rebValue("local-to-file/dir", rebT(path));

    rebFree(path);
    return result;
}


//
//  Set_Current_Dir_Value: C
//
// Set the current directory to local path. Return FALSE
// on failure.
//
bool Set_Current_Dir_Value(const REBVAL *path)
{
    char *path_utf8 = rebSpell("file-to-local/full", path);

    int chdir_result = chdir(path_utf8);

    rebFree(path_utf8);

    return chdir_result == 0;
}


#ifdef TO_OSX
    // Should include <mach-o/dyld.h> ?
    #ifdef __cplusplus
    extern "C"
    #endif
    int _NSGetExecutablePath(char* buf, uint32_t* bufsize);


    //
    //  Get_Current_Exec: C
    //
    // Return the current executable path as a STRING!.  The result should be
    // freed with rebRelease()
    //
    REBVAL *Get_Current_Exec(void)
    {
        uint32_t path_size = 1024;

        char *path_utf8 = rebAllocN(char, path_size);

        int r = _NSGetExecutablePath(path_utf8, &path_size);
        if (r == -1) {  // buffer is too small
            assert(path_size > 1024);  // path_size should now hold needed size

            rebFree(path_utf8);
            path_utf8 = rebAllocN(char, path_size);

            int r = _NSGetExecutablePath(path_utf8, &path_size);
            if (r != 0) {
                rebFree(path_utf8);
                return nullptr;
            }
        }

        // Note: _NSGetExecutablePath returns "a path" not a "real path",
        // and it could be a symbolic link.

        char *resolved_path_utf8 = realpath(path_utf8, NULL);
        if (resolved_path_utf8) {
            REBVAL *result = rebValue(
                "local-to-file", rebT(resolved_path_utf8)
            );
            rebFree(path_utf8);
            free(resolved_path_utf8);  // NOTE: realpath() uses malloc()
            return result;
        }

        REBVAL *result = rebValue(
            "local-to-file", rebT(path_utf8)  // just return unresolved path
        );
        rebFree(path_utf8);
        return result;
    }

#else  // not TO_OSX

    #if defined(HAVE_PROC_PATHNAME)
        #include <sys/sysctl.h>
    #endif

    //
    //  Get_Current_Exec: C
    //
    // Return the current executable path as a FILE!
    //
    // https://stackoverflow.com/questions/1023306/
    //
    REBVAL *Get_Current_Exec(void)
    {
      #if !defined(PROC_EXEC_PATH) && !defined(HAVE_PROC_PATHNAME)
        return nullptr;
      #else
        char *buffer;
        const char *self;
          #if defined(PROC_EXEC_PATH)
            buffer = NULL;
            self = PROC_EXEC_PATH;
          #else  //HAVE_PROC_PATHNAME
            int mib[4] = {
                CTL_KERN,
                KERN_PROC,
                KERN_PROC_PATHNAME,
                -1  //current process
            };
            buffer = rebAllocN(char, PATH_MAX + 1);
            size_t len = PATH_MAX + 1;
            if (sysctl(mib, sizeof(mib), buffer, &len, NULL, 0) != 0) {
                rebFree(buffer);
                return nullptr;
            }
            self = buffer;
        #endif

        char *path_utf8 = rebAllocN(char, PATH_MAX);
        int r = readlink(self, path_utf8, PATH_MAX);

        if (buffer)
            rebFree(buffer);

        if (r < 0) {
            rebFree(path_utf8);
            return nullptr;
        }

        path_utf8[r] = '\0';

        REBVAL *result = rebValue("local-to-file", rebT(path_utf8));
        rebFree(path_utf8);
        return result;
      #endif
    }

#endif
