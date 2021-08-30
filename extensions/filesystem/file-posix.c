//
//  File: %dev-file.c
//  Summary: "Device: File access for Posix"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2021 Ren-C Open Source Contributors
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
// These are helper functions used by the directory and file ports, to
// make filesystem calls to the operating system.  They are styled to speak
// in terms of Rebol values (e.g. a TEXT! or BINARY! to be written vs. raw
// C byte buffers), and do the extraction of the raw data themselves.
//
// Also, by convention they take the PORT! value itself.  This port may or
// may not be open...e.g. a function like Rename_File() actually expects the
// port to be closed so it can call the libuv function for doing a rename.
// This choice is being followed vs. only taking a PORT! in cases where an
// actual open file handle is required to be stylistically consistent (but
// maybe it's not the best idea?)
//
// Originally, these functions had parallel implementations for POSIX and
// Windows.  Hence which version of `Open_File()` (or whatever) would depend on
// some #ifdefs.  The right version would be picked in the build for the OS.
// However, this code is now standardized to use libuv...which provides an
// abstraction layer that looks a lot like the POSIX interface, but with the
// benefit of adding asynchronous (overlapped) IO.
//
// (At time of writing, this is passing in `nullptr` for the callback in all
// operations, which means they are running synchronously.  But asynchronous
// features are there to be taken advantage of when needed.)

#include <time.h>

#include "uv.h"  // includes windows.h
#ifdef TO_WINDOWS
    #undef IS_ERROR  // windows.h defines, contentious with IS_ERROR in Ren-C
#endif

#include "sys-core.h"

#include "file-req.h"


#ifndef PATH_MAX
    #define PATH_MAX 4096  // generally lacking in Posix
#endif


//
//  Get_File_Size_Cacheable: C
//
// If the file size hasn't been queried (because it wasn't needed) then do
// an fstat() to get the information.
//
REBVAL *Get_File_Size_Cacheable(uint64_t *size, const REBVAL *port)
{
    FILEREQ *file = File_Of_Port(port);

    if (file->size_cache != FILESIZE_UNKNOWN) {
        *size = file->size_cache;
        return nullptr;  // assume accurate (checked each entry to File_Actor)
    }

    uv_fs_t req;
    int result = uv_fs_fstat(uv_default_loop(), &req, file->id, nullptr);
    if (result != 0) {
        *size = FILESIZE_UNKNOWN;
        return rebError_UV(result);
    }

    *size = req.statbuf.st_size;
    return nullptr;
}


//
//  Try_Read_Directory_Entry: C
//
// This function will read a file directory, one file entry at a time, then
// close when no more files are found.  The value returned is an API handle
// of a FILE!, nullptr if there's no more left, or an ERROR!.
//
// !!! R3-Alpha comment said: "The dir->path can contain wildcards * and ?.
// The processing of these can be done in the OS (if supported) or by a
// separate filter operation during the read."  How does libuv handle this?
//
REBVAL *Try_Read_Directory_Entry(FILEREQ *dir)
{
    assert(dir->is_dir);

    // If no dir enumeration handle (e.g. this is the first Try_Read_Directory()
    // call in a batch that expects to keep calling until done) open the dir
    //
    if (dir->handle == nullptr) {
        char *dir_utf8 = rebSpell("file-to-local", dir->path);

        uv_fs_t req;
        int result = uv_fs_opendir(uv_default_loop(), &req, dir_utf8, nullptr);

        rebFree(dir_utf8);

        if (result < 0)
            return rebError_UV(result);

        dir->handle = cast(uv_dir_t*, req.ptr);
        uv_fs_req_cleanup(&req);  // Note: does not free the uv_dir_t handle
    }

    // Get dir entry (skip over the . and .. dir cases):
    //
    uv_dirent_t dirent;
    uv_fs_t req;
    do {
        // libuv supports reading multiple directories at a time (as well as
        // asynchronously) but for a first phase of compatibility do 1 sync.
        //
        dir->handle->dirents = &dirent;
        dir->handle->nentries = 1;

        ssize_t num_entries_read = uv_fs_readdir(
            uv_default_loop(), &req, dir->handle, nullptr
        );

        if (num_entries_read <= 0) {  // 0 means no more, negative means error
            int close_result = uv_fs_closedir(
                uv_default_loop(), &req, dir->handle, nullptr
            );

            dir->handle = 0;

            if (num_entries_read < 0)
                return rebError_UV(num_entries_read);  // error code

            if (close_result < 0)
                return rebError_UV(close_result);

            assert(num_entries_read == 0);
            return nullptr;  // no more files
        }
    } while (
        dirent.name[0] == '.' and (
            dirent.name[1] == '\0'
            or (dirent.name[1] == '.' and dirent.name[2] == '\0')
        )
    );

    // !!! R3-Alpha had a limited model and only recognized directory and file.
    // Libuv can detect symbolic links and block devices and other things.
    // Review the exposure of all that!
    //
    bool is_dir = (dirent.type == UV_DIRENT_DIR);

    REBVAL *path = rebValue(
        "applique :local-to-file [",
            "path:", rebT(dirent.name),
            "dir: if", rebL(is_dir), "'#",
        "]"
    );

    uv_fs_req_cleanup(&req);

    return path;
}


//
//  Open_File: C
//
// Open the specified file with the given flags.  For the list of flags, see:
//
// http://docs.libuv.org/en/v1.x/fs.html#file-open-constants
//
// The file path is provided in POSIX format (standard for Rebol FILE!), and
// must be converted to local format before being used.
//
// !!! Does libuv gloss over the slash/backslash issues?
//
REBVAL *Open_File(const REBVAL *port, int flags)
{
    FILEREQ *file = File_Of_Port(port);

    if (file->id != FILEHANDLE_NONE)
        return rebValue("make error! {File is already open}");

    // "Posix file names should be compatible with REBOL file paths"

    assert(file->id == FILEHANDLE_NONE);
    assert(file->size_cache == FILESIZE_UNKNOWN);
    assert(file->offset == FILEOFFSET_UNKNOWN);

    // "mode must be specified when O_CREAT is in the flags, and is ignored
    // otherwise."  Although the parameter is named singularly, it is the
    // result of a bitmask of flags.
    //
    // !!! libuv does not seem to provide these despite providing UV_FS_O_XXX
    // constants.  Would anything bad happen if we left it at 0?
    //
    int mode = 0;
    if (flags & UV_FS_O_CREAT) {
        if (flags & UV_FS_O_RDONLY)
            mode = S_IREAD;
        else {
          #ifdef TO_WINDOWS
            mode = S_IREAD | S_IWRITE;
          #else
            mode = S_IREAD | S_IWRITE | S_IRGRP | S_IWGRP | S_IROTH;
          #endif
        }
    }

    char *path_utf8 = rebSpell("file-to-local/full", file->path);

    uv_fs_t req;
    int h;
    h = uv_fs_open(uv_default_loop(), &req, path_utf8, flags, mode, nullptr);

    rebFree(path_utf8);

    if (h < 0)
        return rebError_UV(h);

    // Note: this code used to do an lseek() to "confirm that a seek-mode file
    // is actually seekable".  libuv does not offer lseek, apparently because
    // it is contentious with asynchronous I/O.
    //
    // Note2: this code also used to fetch the file size with fstat.  It's not
    // clear why it would need to proactively do that.
    //
    file->id = h;
    file->offset = 0;
    file->flags = flags;
    assert(file->size_cache == FILESIZE_UNKNOWN);

    return nullptr;
}


//
//  Close_File: C
//
// Closes a previously opened file.
//
REBVAL *Close_File(const REBVAL *port)
{
    FILEREQ *file = File_Of_Port(port);

    assert(file->id != FILEHANDLE_NONE);

    uv_fs_t req;
    int result = uv_fs_close(uv_default_loop(), &req, file->id, nullptr);

    file->id = FILEHANDLE_NONE;
    file->offset = FILEOFFSET_UNKNOWN;
    file->size_cache = FILESIZE_UNKNOWN;

    if (result < 0)
        return rebError_UV(result);

    return nullptr;
}


//
//  Read_File: C
//
REBVAL *Read_File(const REBVAL *port, size_t length)
{
    FILEREQ *file = File_Of_Port(port);

    assert(not file->is_dir);  // should call Read_Directory!
    assert(file->id != FILEHANDLE_NONE);

    // Make buffer for read result that can be "repossessed" as a BINARY!
    //
    char *buffer = rebAllocN(char, length);

    const unsigned int num_bufs = 1;  // can read many buffers but we just do 1
    uv_buf_t buf;
    buf.base = buffer;
    buf.len = length;

    uv_fs_t req;
    ssize_t num_bytes_read = uv_fs_read(
        uv_default_loop(),
        &req,
        file->id,
        &buf,
        num_bufs,
        file->offset,
        nullptr  // no callback, synchronous
    );
    if (num_bytes_read < 0)
        return rebError_UV(num_bytes_read);

    file->offset += num_bytes_read;

    // !!! The read is probably frequently shorter than the buffer size that
    // was allocated, so the space should be reclaimed...though that should
    // probably be something the GC does when it notices oversized series
    // just as a general cleanup task.
    //
    return rebRepossess(buffer, num_bytes_read);
}


//
//  Write_File: C
//
REBVAL *Write_File(const REBVAL *port, const REBVAL *value, REBLEN limit)
{
    FILEREQ *file = File_Of_Port(port);

    assert(file->id != FILEHANDLE_NONE);

    if (limit == 0) {
        //
        // !!! While it may seem like writing a length of 0 could be shortcut
        // here, it is actually the case that 0 byte writes can have meaning
        // to some receivers of pipes.  Use cases should be studied before
        // doing a shortcut here.
    }

    const REBYTE *data;
    size_t size;

    if (IS_TEXT(value) or IS_ISSUE(value)) {
        REBCHR(const*) utf8 = VAL_UTF8_LEN_SIZE_AT_LIMIT(
            nullptr,
            &size,
            value,
            limit
        );

        // !!! In the quest to purify the universe, we've been checking to
        // make sure that strings containing CR are not written out if you
        // are writing "text".  You have to send BINARY! (which can be done
        // cheaply with an alias, AS TEXT!, uses the same memory)
        //
        const REBYTE *tail = utf8 + size;
        const REBYTE *pos = utf8;
        for (; pos != tail; ++pos)
            if (*pos == CR)
                fail (Error_Illegal_Cr(pos, utf8));

        data = utf8;
    }
    else {
        if (not IS_BINARY(value))
            return rebValue("make error! {ISSUE!, TEXT!, BINARY! for WRITE}");

        data = VAL_BINARY_AT(value);
        size = limit;
    }

    assert(file->offset != FILEOFFSET_UNKNOWN);

    const int num_bufs = 1;
    uv_buf_t buf;
    buf.base = m_cast(char*, cs_cast(data));  // doesn't mutate
    buf.len = size;

    uv_fs_t req;
    ssize_t num_bytes_written = uv_fs_write(
        uv_default_loop(), &req, file->id, &buf, num_bufs, file->offset, nullptr
    );

    if (num_bytes_written < 0) {
        file->size_cache = FILESIZE_UNKNOWN;  // don't know what fail did
        return rebError_UV(num_bytes_written);
    }

    assert(num_bytes_written == cast(ssize_t, size));

    file->offset += num_bytes_written;

    // !!! The concept of R3-Alpha was that it would keep the file size up to
    // date...theoretically.  But it actually didn't do that here.  Adding it,
    // but also adding a check in File_Actor() to make sure the cache is right.
    //
    if (file->size_cache != FILESIZE_UNKNOWN) {
        if (file->offset + num_bytes_written > file->size_cache) {
            file->size_cache += (
                num_bytes_written - (file->size_cache - file->offset)
            );
        }
   }

    return nullptr;
}


//
//  Truncate_File: C
//
REBVAL *Truncate_File(const REBVAL *port)
{
    FILEREQ *file = File_Of_Port(port);
    assert(file->id != FILEHANDLE_NONE);

    uv_fs_t req;
    int result = uv_fs_ftruncate(
        uv_default_loop(), &req, file->id, file->offset, nullptr
    );
    if (result != 0)
        return rebError_UV(result);

    return nullptr;
}


//
//  Create_Directory: C
//
REBVAL *Create_Directory(const REBVAL *port)
{
    FILEREQ *dir = File_Of_Port(port);
    assert(dir->is_dir);

    // !!! We use /NO-TAIL-SLASH here because there was some historical issue
    // about leaving the tail slash on calling mkdir() on some implementation.
    //
    char *path_utf8 = rebSpell(
        "file-to-local/full/no-tail-slash", dir->path
    );

    uv_fs_t req;
    int result = uv_fs_mkdir(uv_default_loop(), &req, path_utf8, 0777, nullptr);

    rebFree(path_utf8);

    if (result != 0)
        return rebError_UV(result);

    return nullptr;
}


//
//  Delete_File_Or_Directory: C
//
// Note: Directories must be empty to succeed
//
REBVAL *Delete_File_Or_Directory(const REBVAL *port)
{
    FILEREQ *file = File_Of_Port(port);

    // !!! There is a /NO-TAIL-SLASH refinement, but the tail slash was left on
    // for directory removal, because it seemed to be supported.  Review if
    // there is any reason to remove it.
    //
    char *path_utf8 = rebSpell("file-to-local/full", file->path);

    uv_fs_t req;
    int result;
    if (file->is_dir)
        result = uv_fs_rmdir(uv_default_loop(), &req, path_utf8, nullptr);
    else
        result = uv_fs_unlink(uv_default_loop(), &req, path_utf8, nullptr);

    rebFree(path_utf8);

    if (result != 0)
        return rebError_UV(result);

    return nullptr;
}


//
//  Rename_File_Or_Directory: C
//
REBVAL *Rename_File_Or_Directory(const REBVAL *port, const REBVAL *to)
{
    FILEREQ *file = File_Of_Port(port);

    char *from_utf8 = rebSpell(
        "file-to-local/full/no-tail-slash", file->path
    );
    char *to_utf8 = rebSpell("file-to-local/full/no-tail-slash", to);

    uv_fs_t req;
    int result = uv_fs_rename(
        uv_default_loop(), &req, from_utf8, to_utf8, nullptr
    );

    rebFree(to_utf8);
    rebFree(from_utf8);

    if (result != 0)
        return rebError_UV(result);

    return nullptr;
}


#ifdef TO_WINDOWS
    //
    //  File_Time_To_Rebol: C
    //
    // Convert file.time to REBOL date/time format.  Time zone is UTC.
    //
    REBVAL *File_Time_To_Rebol(uv_timespec_t uvtime)
    {
        SYSTEMTIME stime;
        TIME_ZONE_INFORMATION tzone;

        if (TIME_ZONE_ID_DAYLIGHT == GetTimeZoneInformation(&tzone))
            tzone.Bias += tzone.DaylightBias;

        FILETIME filetime;
        filetime.dwLowDateTime = uvtime.tv_sec;
        filetime.dwHighDateTime = uvtime.tv_nsec;
        FileTimeToSystemTime(cast(FILETIME *, &uvtime), &stime);

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
#else
    #ifndef timeval
        #include <sys/time.h>  // for older systems
    #endif

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
        // !!! The R3-Alpha host code would always give back times in UTC plus
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
        // gmtime().  However, we feed it gmtime() as if it were the localtime.
        // Then the time zone can be calculated by diffing it from a mktime()
        // inversion of a suitable local time.
        //
        // !!! For some reason, R3-Alpha expected the caller to pass in a utc
        // tm structure pointer but then didn't use it, choosing to make
        // another call to gmtime().  Review.
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
    REBVAL *File_Time_To_Rebol(uv_timespec_t uvtime)
    {
        time_t stime;

        if (sizeof(time_t) > sizeof(uvtime.tv_sec)) {
            int64_t t = uvtime.tv_sec;
            t |= cast(int64_t, uvtime.tv_nsec) << 32;
            stime = t;
        }
        else
            stime = uvtime.tv_sec;

        // gmtime() is badly named.  It's utc time.  Note we have to be careful
        // as it returns a system static buffer, so we have to copy the result
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
#endif


//
//  Query_File_Or_Directory: C
//
// Obtain information about a file.  Produces a STD_FILE_INFO object.
//
REBVAL *Query_File_Or_Directory(const REBVAL *port)
{
    FILEREQ *file = File_Of_Port(port);

    // The original implementation here used /no-trailing-slash for the
    // FILE-TO-LOCAL, which meant that %/ would turn into an empty string.
    // It would appear that for directories, trailing slashes are acceptable
    // in `stat`...though for symlinks different answers are given based
    // on the presence of the slash:
    //
    // https://superuser.com/questions/240743/
    //
    char *path_utf8 = rebSpell("file-to-local/full", file->path);

    uv_fs_t req;
    int result = uv_fs_stat(uv_default_loop(), &req, path_utf8, nullptr);

    rebFree(path_utf8);

    if (result != 0)
        return rebError_UV(result);

    bool is_dir = S_ISDIR(req.statbuf.st_mode);
    if (is_dir != file->is_dir)
        return rebValue("make error! {Directory/File flag mismatch}");

    // !!! R3-Alpha would do this "to be consistent on all systems".  But it
    // seems better to just make the size null, unless there is some info
    // to be gleaned from a directory's size?
    //
    //     if (is_dir)
    //         req.statbuf.st_size = 0;

    // Note: time is in local format and must be converted
    //
    REBVAL *timestamp = File_Time_To_Rebol(req.statbuf.st_mtim);

    return rebValue(
        "make ensure object! (", port , ").scheme.info [",
            "name:", file->path,
            "size:", is_dir ? rebQ(nullptr) : rebI(req.statbuf.st_size),
            "type:", is_dir ? "'dir" : "'file",
            "date:", rebR(timestamp),
        "]"
    );
}


//
//  Get_Current_Dir_Value: C
//
// Result is a FILE! API Handle, must be freed with rebRelease()
//
REBVAL *Get_Current_Dir_Value(void)
{
    char *path_utf8 = rebAllocN(char, PATH_MAX);

    size_t size = PATH_MAX - 1;
    if (uv_cwd(path_utf8, &size) == UV_ENOBUFS) {
        path_utf8 = cast(char*, rebRealloc(path_utf8, size));  // includes \0
        size_t check = size;
        uv_cwd(path_utf8, &check);
        assert(check == size);
    }
    assert(size == strsize(path_utf8));  // does it give correct size?

    // "On Unix the path no longer ends in a slash"...the /DIR option should
    // make it end in a slash for the result.

    REBVAL *result = rebValue("local-to-file/dir", rebT(path_utf8));

    rebFree(path_utf8);
    return result;
}


//
//  Set_Current_Dir_Value: C
//
// Set the current directory to local path. Return FALSE on failure.
//
bool Set_Current_Dir_Value(const REBVAL *path)
{
    char *path_utf8 = rebSpell("file-to-local/full", path);

    int result = uv_chdir(path_utf8);

    rebFree(path_utf8);

    return result == 0;  // !!! return ERROR! value instead?
}


#if 0
// !!! Using this libuv-provided function is a nice thought, but it requires
// calling uv_setup_args() which expects to get argc and argv.  If libuv is
// an extension, then it would load much later than main() and be optional...
// so we may not want to couple it that tightly.  But what was there before
// is kind of a mess...though it was a much smaller dependency than libuv.
// Review as things evolve.

//
//  Get_Current_Exec: C
//
// Return this running interpreter's executable path as TEXT!.
// Result is a FILE! API Handle, must be freed with rebRelease()
//
// Note: You must call uv_setup_args() before calling this function!
//
REBVAL *Get_Current_Exec(void)
{
    char *path_utf8 = rebAllocN(char, PATH_MAX);

    size_t size = PATH_MAX - 1;
    if (uv_exepath(path_utf8, &size) == UV_ENOBUFS) {
        path_utf8 = cast(char*, rebRealloc(path_utf8, size));  // includes \0
        size_t check = size;
        uv_exepath(path_utf8, &check);
        assert(check == size);
    }
    assert(size == strsize(path_utf8));  // does it give correct size?

    REBVAL *result = rebValue(
        "local-to-file", rebT(path_utf8)  // just return unresolved path
    );
    rebFree(path_utf8);
    return result;
}
#endif


#ifdef TO_OSX

    // !!! Note: intentionally not using libuv here, in case this is to be
    // extracted for a lighter build!

    // Should include <mach-o/dyld.h> ?
    #ifdef __cplusplus
    extern "C"
    #endif
    int _NSGetExecutablePath(char* buf, uint32_t* bufsize);

    //
    //  Get_Current_Exec: C
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

#elif defined(TO_WINDOWS)

    // !!! Note: intentionally not using libuv here, in case this is to be
    // extracted for a lighter build!

    //
    //  Get_Current_Exec: C
    //
    REBVAL *Get_Current_Exec(void)
    {
        WCHAR *path = rebAllocN(WCHAR, MAX_PATH);

        DWORD r = GetModuleFileName(NULL, path, MAX_PATH);
        if (r == 0) {
            rebFree(path);
            return nullptr;
        }
        path[r] = '\0';  // May not be NULL-terminated if buffer not big enough

        REBVAL *result = rebValue(
            "local-to-file", rebR(rebTextWide(path))
        );
        rebFree(path);

        return result;
    }

#else  // not TO_OSX and not TO_WINDOWS

    // !!! Note: intentionally not using libuv here, in case this is to be
    // extracted for a lighter build!

    #if defined(HAVE_PROC_PATHNAME)
        #include <sys/sysctl.h>
    #endif
    #include <unistd.h>  // for readlink()

    //
    //  Get_Current_Exec: C
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
