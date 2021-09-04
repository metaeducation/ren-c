//
//  File: %mod-filesystem.c
//  Summary: "POSIX/Windows File and Directory Access"
//  Section: ports
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

// !!! When linking to libuv, the TCC TinyC compiler hits a problem with a
// missing __dso_handle symbol.  At one point there was a workaround added,
// and the workaround was to define it as a dummy...which only works if it
// wasn't actually being used.  Try it and see if it works with the older
// TCC that lacks this definition:
//
// https://www.mail-archive.com/tinycc-devel@nongnu.org/msg08429.html
//
#ifdef __TINYC__
    void * __dso_handle = &__dso_handle;
#endif

#include "sys-core.h"

#include "tmp-mod-filesystem.h"

extern REB_R File_Actor(REBFRM *frame_, REBVAL *port, const REBSYM *verb);
extern REB_R Dir_Actor(REBFRM *frame_, REBVAL *port, const REBSYM *verb);


#ifdef TO_WINDOWS
    #define OS_DIR_SEP '\\'  // file path separator (Thanks Bill.)
#else
    #define OS_DIR_SEP '/'  // rest of the world uses it
#endif


//
//  startup*: native [  ; Note: DO NOT EXPORT!
//
//  {Start up down the filesystem device}
//
//      return: <none>
//  ]
//
REBNATIVE(startup_p)
{
    FILESYSTEM_INCLUDE_PARAMS_OF_STARTUP_P;

    return rebNone();
}


//
//  export get-file-actor-handle: native [
//
//  {Retrieve handle to the native actor for files}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_file_actor_handle)
{
    Make_Port_Actor_Handle(D_OUT, &File_Actor);
    return D_OUT;
}


//
//  shutdown*: native [  ; Note: DO NOT EXPORT!
//
//  {Shut down the filesystem device}
//
//      return: <none>
//  ]
//
REBNATIVE(shutdown_p)
{
    FILESYSTEM_INCLUDE_PARAMS_OF_SHUTDOWN_P;

    return rebNone();
}


//
//  get-dir-actor-handle: native [
//
//  {Retrieve handle to the native actor for directories}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_dir_actor_handle)
{
    Make_Port_Actor_Handle(D_OUT, &Dir_Actor);
    return D_OUT;
}


// Options for To_REBOL_Path
enum {
    PATH_OPT_SRC_IS_DIR = 1 << 0
};


//
//  To_REBOL_Path: C
//
// Convert local-format filename to a Rebol-format filename.  This basically
// means that on Windows, "C:\" is translated to "/C/", backslashes are
// turned into forward slashes, multiple slashes get turned into one slash.
// If something is supposed to be a directory, then it is ensured that the
// Rebol-format filename ends in a slash.
//
// To try and keep it straight whether a path has been converted already or
// not, STRING!s are used to hold local-format filenames, while FILE! is
// assumed to denote a Rebol-format filename.
//
// Allocates and returns a new series with the converted path.
//
// Note: This routine apparently once appended the current directory to the
// volume when no root slash was provided.  It was an odd case to support
// the MSDOS convention of `c:file`.  That is not done here.
//
REBSTR *To_REBOL_Path(const RELVAL *string, REBFLGS flags)
{
    assert(IS_TEXT(string));

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    bool lead_slash = false; // did we restart to insert a leading slash?
    bool saw_colon = false; // have we hit a ':' yet?
    bool saw_slash = false; // have we hit a '/' yet?
    bool last_was_slash = false; // was last character appended a slash?

restart:;
    REBLEN len;
    REBCHR(const*) up = VAL_UTF8_LEN_SIZE_AT(&len, nullptr, string);

    REBUNI c = '\0'; // for test after loop (in case loop does not run)

    REBLEN i;
    for (i = 0; i < len;) {
        up = NEXT_CHR(&c, up);
        ++i;

        if (c == ':') {
            //
            // Handle the vol:dir/file format:
            //
            if (saw_colon || saw_slash)
                fail ("no prior : or / allowed for vol:dir/file format");

            if (not lead_slash) {
                //
                // Drop mold so far, and change C:/ to /C/ (and C:X to /C/X)
                //
                TERM_STR_LEN_SIZE(mo->series, mo->index, mo->offset);
                Append_Codepoint(mo->series, '/');
                lead_slash = true; // don't do this the second time around
                goto restart;
            }

            saw_colon = true;

            Append_Codepoint(mo->series, '/'); // replace : with a /

            if (i < len) {
                up = NEXT_CHR(&c, up);
                ++i;

                if (c == '\\' || c == '/') {
                    //
                    // skip / in foo:/file
                    //
                    if (i >= len)
                        break;
                    up = NEXT_CHR(&c, up);
                    ++i;
                }
            }
        }
        else if (c == '\\' || c== '/') { // !!! Should this use OS_DIR_SEP
            if (last_was_slash)
                continue; // Collapse multiple / or \ to a single slash

            c = '/';
            last_was_slash = true;
            saw_slash = true;
        }
        else
            last_was_slash = false;

        Append_Codepoint(mo->series, c);
    }

    // If this is supposed to be a directory and the last character is not a
    // slash, make it one (this is Rebol's rule for FILE!s that are dirs)
    //
    if ((flags & PATH_OPT_SRC_IS_DIR) and c != '/') // watch for %/c/ case
        Append_Codepoint(mo->series, '/');

    return Pop_Molded_String(mo);
}


extern bool Set_Current_Dir_Value(const REBVAL *path);
extern REBVAL *Get_Current_Dir_Value(void);


enum {
    REB_FILETOLOCAL_0 = 0, // make it clearer when using no options
    REB_FILETOLOCAL_FULL = 1 << 0, // expand path relative to current dir

    // !!! A comment in the R3-Alpha %p-dir.c said "Special policy: Win32 does
    // not want tail slash for dir info".
    //
    REB_FILETOLOCAL_NO_TAIL_SLASH = 1 << 2 // don't include the terminal slash
};


//
//  Mold_File_To_Local: C
//
// Implementation routine of To_Local_Path which leaves the path in the mold
// buffer (e.g. for further appending or just counting the number of bytes)
//
void Mold_File_To_Local(REB_MOLD *mo, const RELVAL *file, REBFLGS flags) {
    assert(IS_FILE(file));

    REBLEN len;
    REBCHR(const*) up = VAL_UTF8_LEN_SIZE_AT(&len, nullptr, file);

    REBLEN i = 0;

    REBUNI c;
    if (len == 0)
        c = '\0';
    else
        up = NEXT_CHR(&c, up);

    // Prescan for: /c/dir = c:/dir, /vol/dir = //vol/dir, //dir = ??
    //
    if (c == '/') { // %/
        if (i < len) {
            up = NEXT_CHR(&c, up);
            ++i;
        }
        else
            c = '\0';

      #ifdef TO_WINDOWS
        if (c != '\0' and c != '/') { // %/c or %/c/ but not %/ %// %//c
            //
            // peek ahead for a '/'
            //
            REBUNI d = '/';
            REBCHR(const*) dp;
            if (i < len)
                dp = NEXT_CHR(&d, up);
            else
                dp = up;
            if (d == '/') { // %/c/ => "c:/"
                ++i;
                Append_Codepoint(mo->series, c);
                Append_Codepoint(mo->series, ':');
                up = NEXT_CHR(&c, dp);
                ++i;
            }
            else {
                // %/cc %//cc => "//cc"
                //
                Append_Codepoint(mo->series, OS_DIR_SEP);
            }
        }
      #endif

        Append_Codepoint(mo->series, OS_DIR_SEP);
    }
    else if (flags & REB_FILETOLOCAL_FULL) {
        //
        // When full path is requested and the source path was relative (e.g.
        // did not start with `/`) then prepend the current directory.
        //
        // Get_Current_Dir_Value() comes back in Rebol-format FILE! form, and
        // it has to be converted to the local-format before being prepended
        // to the local-format file path we're generating.  So recurse.  Don't
        // use REB_FILETOLOCAL_FULL as that would recurse (we assume a fully
        // qualified path was returned by Get_Current_Dir_Value())
        //
        REBVAL *lpath = Get_Current_Dir_Value();
        Mold_File_To_Local(mo, lpath, REB_FILETOLOCAL_0);
        rebRelease(lpath);
    }

    // Prescan each file segment for: . .. directory names.  (Note the top of
    // this loop always follows / or start).  Each iteration takes care of one
    // segment of the path, i.e. stops after OS_DIR_SEP
    //
    for (; i < len; up = NEXT_CHR(&c, up), ++i) {
        if (flags & REB_FILETOLOCAL_FULL) {
            //
            // While file and directory names like %.foo or %..foo/ are legal,
            // lone %. and %.. have special meaning.  If a file path component
            // starts with `.` then look ahead for special consideration.
            //
            if (c == '.') {
                up = NEXT_CHR(&c, up);
                ++i;

                if (c == '\0') {
                    assert(i == len);
                    break;  // %xxx/. means stay in the same directory
                }

                if (c == '/')
                    continue; // %xxx/./yyy has ./ mean stay in same directory

                if (c != '.') {
                    //
                    // It's a filename like %.xxx, which is legal.  Output the
                    // . character we'd found before the peek ahead and break
                    // to the next loop that copies without further `.` search
                    //
                    Append_Codepoint(mo->series, '.');
                    goto segment_loop;
                }

                // We've seen two sequential dots, so .. or ../ or ..xxx

                up = NEXT_CHR(&c, up);
                ++i;
                assert(c != '\0' || i == len);

                if (c == '\0' || c == '/') { // .. or ../ means back up a dir
                    //
                    // Seek back to the previous slash in the mold buffer and
                    // truncate it there, to trim off one path segment.
                    //
                    REBLEN n = STR_LEN(mo->series);
                    REBUNI c2;  // character in mold buffer
                    if (n > mo->index) {
                        REBCHR(*) tp = STR_TAIL(mo->series);

                        --n;
                        tp = BACK_CHR(&c2, tp);
                        assert(c2 == OS_DIR_SEP);

                        if (n > mo->index) {
                            --n; // don't want the *ending* slash
                            tp = BACK_CHR(&c2, tp);
                        }

                        while (n > mo->index and c2 != OS_DIR_SEP) {
                            --n;
                            tp = BACK_CHR(&c2, tp);
                        }

                        tp = BACK_CHR(&c2, tp);

                        // Terminate, loses '/' (or '\'), but added back below
                        //
                        TERM_STR_LEN_SIZE(
                            mo->series,
                            n,
                            tp - STR_HEAD(mo->series) + 1
                        );
                    }

                    // Add separator and keep looking (%../../ can happen)
                    //
                    Append_Codepoint(mo->series, OS_DIR_SEP);

                    if (i == len) {
                        assert(c == '\0');  // don't run NEXT_CHR() again!
                        break;
                    }
                    continue;
                }

                // Files named `..foo` are ordinary files.  Account for the
                // pending `..` and fall through to the loop that doesn't look
                // further at .
                //
                Append_Codepoint(mo->series, '.');
                Append_Codepoint(mo->series, '.');
            }
        }

    segment_loop:;
        for (; i < len; up = NEXT_CHR(&c, up), ++i) {
            //
            // Keep copying characters out of the path segment until we find
            // a slash or hit the end of the input path string.
            //
            if (c != '/') {
                Append_Codepoint(mo->series, c);
                continue;
            }

            REBLEN n = STR_SIZE(mo->series);
            if (
                n > mo->offset
                and *BIN_AT(mo->series, n - 1) == OS_DIR_SEP
            ){
                // Collapse multiple sequential slashes into just one, by
                // skipping to the next character without adding to mold.
                //
                // !!! While this might (?) make sense when converting a local
                // path into a FILE! to "clean it up", it seems perhaps that
                // here going the opposite way it would be best left to the OS
                // if someone has an actual FILE! with sequential slashes.
                //
                // https://unix.stackexchange.com/a/1919/118919
                //
                continue;
            }

            // Accept the slash, but translate to backslash on Windows.
            //
            Append_Codepoint(mo->series, OS_DIR_SEP);
            break;
        }

        // If we're past the end of the content, we don't want to run the
        // outer loop test and NEXT_CHR() again...that's past the terminator.
        //
        assert(i <= len);
        if (i == len) {
            assert(c == '\0');
            break;
        }
    }

    // Some operations on directories in various OSes will fail if the slash
    // is included in the filename (move, delete), so it might not be wanted.
    //
    if (flags & REB_FILETOLOCAL_NO_TAIL_SLASH) {
        REBSIZ n = STR_SIZE(mo->series);
        if (n > mo->offset and *BIN_AT(mo->series, n - 1) == OS_DIR_SEP)
            TERM_STR_LEN_SIZE(mo->series, STR_LEN(mo->series) - 1, n - 1);
    }
}


//
//  To_Local_Path: C
//
// Convert Rebol-format filename to a local-format filename.  This is the
// opposite operation of To_REBOL_Path.
//
REBSTR *To_Local_Path(const RELVAL *file, REBFLGS flags) {
    DECLARE_MOLD (mo);
    Push_Mold(mo);

    Mold_File_To_Local(mo, file, flags);
    return Pop_Molded_String(mo);
}


//
//  export local-to-file: native [
//
//  {Converts a local system file path TEXT! to a Rebol FILE! path.}
//
//      return: [<opt> file!]
//          {The returned value should be a valid natural FILE! literal}
//      path [<blank> text! file!]
//          {Path to convert (by default, only TEXT! for type safety)}
//      /pass
//          {Convert TEXT!, but pass thru FILE!, assuming it's canonized}
//      /dir
//          {Ensure input path is treated as a directory}
//  ]
//
REBNATIVE(local_to_file)
{
    FILESYSTEM_INCLUDE_PARAMS_OF_LOCAL_TO_FILE;

    REBVAL *path = ARG(path);
    if (IS_FILE(path)) {
        if (not REF(pass))
            fail ("LOCAL-TO-FILE only passes through FILE! if /PASS used");

        return Init_File(D_OUT, Copy_String_At(path));  // many callers modify
    }

    return Init_File(
        D_OUT,
        To_REBOL_Path(path, REF(dir) ? PATH_OPT_SRC_IS_DIR : 0)
    );
}


//
//  export file-to-local: native [
//
//  {Converts a Rebol FILE! path to TEXT! of the local system file path}
//
//      return: [<opt> text!]
//          {A TEXT! like "\foo\bar" is not a "natural" FILE! %\foo\bar}
//      path [<blank> file! text!]
//          {Path to convert (by default, only FILE! for type safety)}
//      /pass
//          {Convert FILE!s, but pass thru TEXT!, assuming it's local}
//      /full
//          {For relative paths, prepends current dir for full path}
//      /no-tail-slash
//          {For directories, do not add a slash or backslash to the tail}
//  ]
//
REBNATIVE(file_to_local)
{
    FILESYSTEM_INCLUDE_PARAMS_OF_FILE_TO_LOCAL;

    REBVAL *path = ARG(path);
    if (IS_TEXT(path)) {
        if (not REF(pass))
            fail ("FILE-TO-LOCAL only passes through STRING! if /PASS used");

        return Init_Text(D_OUT, Copy_String_At(path));  // callers modify
    }

    return Init_Text(
        D_OUT,
        To_Local_Path(
            path,
            REB_FILETOLOCAL_0
                | (REF(full) ? REB_FILETOLOCAL_FULL : 0)
                | (REF(no_tail_slash) ? REB_FILETOLOCAL_NO_TAIL_SLASH : 0)
        )
    );
}


//
//  export what-dir: native [
//
//  {Returns the current directory path}
//
//      return: [<opt> file! url!]
//  ]
//
REBNATIVE(what_dir)
{
    FILESYSTEM_INCLUDE_PARAMS_OF_WHAT_DIR;

    REBVAL *current_path = Get_System(SYS_OPTIONS, OPTIONS_CURRENT_PATH);

    if (IS_FILE(current_path) || IS_NULLED(current_path)) {
        //
        // !!! Because of the need to track a notion of "current path" which
        // could be a URL! as well as a FILE!, the state is stored in the
        // system options.  For now--however--it is "duplicate" in the case
        // of a FILE!, because the OS has its own tracked state.  We let the
        // OS state win for files if they have diverged somehow--because the
        // code was already here and it would be more compatible.  But
        // reconsider the duplication.

        REBVAL *refresh = Get_Current_Dir_Value();
        Copy_Cell(current_path, refresh);
        rebRelease(refresh);
    }
    else if (not IS_URL(current_path)) {
        //
        // Lousy error, but ATM the user can directly edit system/options.
        // They shouldn't be able to (or if they can, it should be validated)
        //
        fail (current_path);
    }

    return rebValue(Lib(COPY), Lib(TRY), current_path);  // caller mutates, copy
}


//
//  export change-dir: native [
//
//  {Changes the current path (where scripts with relative paths will be run).}
//
//      return: [file! url!]
//      path [file! url!]
//  ]
//
REBNATIVE(change_dir)
{
    FILESYSTEM_INCLUDE_PARAMS_OF_CHANGE_DIR;

    REBVAL *arg = ARG(path);
    REBVAL *current_path = Get_System(SYS_OPTIONS, OPTIONS_CURRENT_PATH);

    if (IS_URL(arg)) {
        // There is no directory listing protocol for HTTP (although this
        // needs to be methodized to work for SFTP etc.)  So this takes
        // your word for it for the moment that it's a valid "directory".
        //
        // !!! Should it at least check for a trailing `/`?
    }
    else {
        assert(IS_FILE(arg));

        bool success = Set_Current_Dir_Value(arg);

        if (not success)
            fail (PAR(path));
    }

    Copy_Cell(current_path, arg);

    RETURN (ARG(path));
}


extern REBVAL *Get_Current_Exec();

//
//  export get-current-exec: native [
//
//  {Get the current path to the running executable}
//
//      return: [<opt> file!]
//  ]
//
REBNATIVE(get_current_exec)
{
    FILESYSTEM_INCLUDE_PARAMS_OF_GET_CURRENT_EXEC;

    return Get_Current_Exec();
}
