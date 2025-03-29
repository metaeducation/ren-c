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

#include "tmp-mod-filesystem.h"

extern Bounce File_Actor_Dispatcher(Level* level_);
extern Bounce Dir_Actor_Dispatcher(Level* level_);


#if TO_WINDOWS
    #define OS_DIR_SEP '\\'  // file path separator (Thanks Bill.)
#else
    #define OS_DIR_SEP '/'  // rest of the world uses it
#endif


//
//  startup*: native [  ; Note: DO NOT EXPORT!
//
//  "Startup the filesystem device"
//
//      return: [~]
//  ]
//
DECLARE_NATIVE(STARTUP_P)
{
    INCLUDE_PARAMS_OF_STARTUP_P;

    return rebNothing();
}


//
//  export file-actor: native [
//
//  "Handler for OLDGENERIC dispatch on File PORT!s"
//
//      return: [any-value?]
//  ]
//
DECLARE_NATIVE(FILE_ACTOR)
{
    return File_Actor_Dispatcher(LEVEL);
}


//
//  export dir-actor: native [
//
//  "Handler for OLDGENERIC dispatch on Directory PORT!s"
//
//      return: [any-value?]
//  ]
//
DECLARE_NATIVE(DIR_ACTOR)
{
    return Dir_Actor_Dispatcher(LEVEL);
}


//
//  shutdown*: native [  ; Note: DO NOT EXPORT!
//
//  "Shut down the filesystem device"
//
//      return: [~]
//  ]
//
DECLARE_NATIVE(SHUTDOWN_P)
{
    INCLUDE_PARAMS_OF_SHUTDOWN_P;

    return rebNothing();
}


// Options for To_REBOL_Path
enum {
    PATH_OPT_SRC_IS_DIR = 1 << 0
};

INLINE bool Last_In_Mold_Is_Slash(Molder* mo) {
    if (mo->base.size == Flex_Used(mo->string))
        return false;  // nothing added yet

    // It's UTF-8 data, so we can just check the last byte; if it's a
    // continuation code it will not match an ASCII character.
    //
    return *Flex_Last(Byte, mo->string) == '/';
}


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
String* To_REBOL_Path(const Value* string, Flags flags)
{
    assert(Is_Text(string));

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    bool lead_slash = false;  // did we restart to insert a leading slash?
    bool saw_colon = false;  // have we hit a ':' yet?
    bool saw_slash = false;  // have we hit a '/' yet?

  restart: ;

    Length len;
    Utf8(const*) utf8 = Cell_Utf8_Len_Size_At(&len, nullptr, string);

    Codepoint c = '\0';  // for test after loop (in case loop does not run)

    Count n;
    for (n = 0; n < len;) {
        utf8 = Utf8_Next(&c, utf8);
        ++ n;

        if (c == ':') {
            //
            // Handle the vol:dir/file format:
            //
            if (saw_colon or saw_slash)
                fail ("no prior : or / allowed for vol:dir/file format");

            if (not lead_slash) {
                //
                // Drop mold so far, and change C:/ to /C/ (and C:X to /C/X)
                //
                Term_String_Len_Size(mo->string, mo->base.index, mo->base.size);
                Append_Codepoint(mo->string, '/');
                lead_slash = true; // don't do this the second time around
                goto restart;
            }

            saw_colon = true;

            Append_Codepoint(mo->string, '/');  // replace : with a /

            if (n < len) {
                utf8 = Utf8_Next(&c, utf8);
                ++ n;

                if (c == '\\' or c == '/') {  // skip / in foo:/file
                    if (n >= len)
                        break;
                    utf8 = Utf8_Next(&c, utf8);
                    ++ n;
                }
            }
        }
        else if (c == '\\' or c == '/') {  // !!! Should this use OS_DIR_SEP?
            if (Last_In_Mold_Is_Slash(mo))
                continue;  // Collapse multiple / or \ to a single slash

            c = '/';
            saw_slash = true;
        }

        Append_Codepoint(mo->string, c);
    }

    // If this is supposed to be a directory and the last character is not a
    // slash, make it one (this is Rebol's rule for FILE!s that are dirs)
    //
    if (flags & PATH_OPT_SRC_IS_DIR)
        if (not Last_In_Mold_Is_Slash(mo))
            Append_Codepoint(mo->string, '/');

    return Pop_Molded_String(mo);
}


extern bool Set_Current_Dir_Value(const Value* path);
extern Value* Get_Current_Dir_Value(void);


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
void Mold_File_To_Local(Molder* mo, const Value* file, Flags flags) {
    assert(Is_File(file));

    Length len;
    Utf8(const*) utf8 = Cell_Utf8_Len_Size_At(&len, nullptr, file);

    Count n = 0;

    Codepoint c;
    if (len == 0)
        c = '\0';
    else
        utf8 = Utf8_Next(&c, utf8);

    // Prescan for: /c/dir = c:/dir, /vol/dir = //vol/dir, //dir = ??
    //
    if (c == '/') { // %/
        if (n < len) {
            utf8 = Utf8_Next(&c, utf8);
            ++ n;
        }
        else
            c = '\0';

      #if TO_WINDOWS
        if (c != '\0' and c != '/') { // %/c or %/c/ but not %/ %// %//c
            //
            // peek ahead for a '/'
            //
            Codepoint c_peek = '/';
            Utf8(const*) utf8_peek
                = (n < len)
                    ? Utf8_Next(&c_peek, utf8)
                    : utf8;

            if (c_peek == '/') {  // %/c/ => "c:/"
                ++ n;
                Append_Codepoint(mo->string, c);
                Append_Codepoint(mo->string, ':');
                utf8 = Utf8_Next(&c, utf8_peek);
                ++ n;
            }
            else {
                // %/cc %//cc => "//cc"
                //
                Append_Codepoint(mo->string, OS_DIR_SEP);
            }
        }
      #endif

        Append_Codepoint(mo->string, OS_DIR_SEP);
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
        Value* lpath = Get_Current_Dir_Value();
        Mold_File_To_Local(mo, lpath, REB_FILETOLOCAL_0);
        rebRelease(lpath);
    }

    // Prescan each file segment for: . .. directory names.  (Note the top of
    // this loop always follows / or start).  Each iteration takes care of one
    // segment of the path, i.e. stops after OS_DIR_SEP
    //
    for (; n < len; utf8 = Utf8_Next(&c, utf8), ++ n) {
        if (flags & REB_FILETOLOCAL_FULL) {
            //
            // While file and directory names like %.foo or %..foo/ are legal,
            // lone %. and %.. have special meaning.  If a file path component
            // starts with `.` then look ahead for special consideration.
            //
            if (c == '.') {
                utf8 = Utf8_Next(&c, utf8);
                ++ n;

                if (c == '\0') {
                    assert(n == len);
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
                    Append_Codepoint(mo->string, '.');
                    goto segment_loop;
                }

                // We've seen two sequential dots, so .. or ../ or ..xxx

                utf8 = Utf8_Next(&c, utf8);
                ++ n;
                assert(c != '\0' or n == len);

                if (c == '\0' or c == '/') {  // .. or ../ means back up a dir
                    //
                    // Seek back to the previous slash in the mold buffer and
                    // truncate it there, to trim off one path segment.
                    //
                    Count n_seek = String_Len(mo->string);
                    Codepoint c_seek;  // character in mold buffer
                    if (n_seek > mo->base.index) {
                        Utf8(*) utf8_seek = String_Tail(mo->string);

                        -- n_seek;
                        utf8_seek = Utf8_Back(&c_seek, utf8_seek);
                        assert(c_seek == OS_DIR_SEP);

                        if (n_seek > mo->base.index) {
                            -- n_seek;  // don't want the *ending* slash
                            utf8_seek = Utf8_Back(&c_seek, utf8_seek);
                        }

                        while (
                            n_seek > mo->base.index
                            and c_seek != OS_DIR_SEP
                        ){
                            -- n_seek;
                            utf8_seek = Utf8_Back(&c_seek, utf8_seek);
                        }

                        utf8_seek = Utf8_Back(&c_seek, utf8_seek);

                        // Terminate, loses '/' (or '\'), but added back below
                        //
                        Term_String_Len_Size(
                            mo->string,
                            n_seek,
                            utf8_seek - String_Head(mo->string) + 1
                        );
                    }

                    // Add separator and keep looking (%../../ can happen)
                    //
                    Append_Codepoint(mo->string, OS_DIR_SEP);

                    if (n == len) {
                        assert(c == '\0');  // don't run Utf8_Next() again!
                        break;
                    }
                    continue;
                }

                // Files named `..foo` are ordinary files.  Account for the
                // pending `..` and fall through to the loop that doesn't look
                // further at .
                //
                Append_Codepoint(mo->string, '.');
                Append_Codepoint(mo->string, '.');
            }
        }

      segment_loop: ;

        for (; n < len; utf8 = Utf8_Next(&c, utf8), ++ n) {
            //
            // Keep copying characters out of the path segment until we find
            // a slash or hit the end of the input path string.
            //
            if (c != '/') {
                Append_Codepoint(mo->string, c);
                continue;
            }

            Size mo_size = String_Size(mo->string);
            if (
                mo_size > mo->base.size
                and *Binary_At(mo->string, mo_size - 1) == OS_DIR_SEP
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
            Append_Codepoint(mo->string, OS_DIR_SEP);
            break;
        }

        // If we're past the end of the content, we don't want to run the
        // outer loop test and Utf8_Next() again...that's past the terminator.
        //
        assert(n <= len);
        if (n == len) {
            assert(c == '\0');
            break;
        }
    }

    // Some operations on directories in various OSes will fail if the slash
    // is included in the filename (move, delete), so it might not be wanted.
    //
    if (flags & REB_FILETOLOCAL_NO_TAIL_SLASH) {
        Size mo_size = String_Size(mo->string);
        if (
            mo_size > mo->base.size
            and *Binary_At(mo->string, mo_size - 1) == OS_DIR_SEP
        ){
            Term_String_Len_Size(
                mo->string,
                String_Len(mo->string) - 1,
                mo_size - 1
            );
        }
    }
}


//
//  To_Local_Path: C
//
// Convert Rebol-format filename to a local-format filename.  This is the
// opposite operation of To_REBOL_Path.
//
String* To_Local_Path(const Value* file, Flags flags) {
    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    Mold_File_To_Local(mo, file, flags);
    return Pop_Molded_String(mo);
}


//
//  export local-to-file: native [
//
//  "Converts a local system file path TEXT! to a Rebol FILE! path"
//
//      return: [file!]
//      path "Path to convert (by default, only TEXT! for type safety)"
//          [<maybe> text! file!]
//      :pass "Convert TEXT!, but pass thru FILE!, assuming it's canonized"
//      :dir "Ensure input path is treated as a directory"
//  ]
//
DECLARE_NATIVE(LOCAL_TO_FILE)
{
    INCLUDE_PARAMS_OF_LOCAL_TO_FILE;

    Value* path = ARG(PATH);
    if (Is_File(path)) {
        if (not Bool_ARG(PASS))
            return "fail -{LOCAL-TO-FILE needs :PASS to passthru FILE!}-";

        return Init_File(OUT, Copy_String_At(path));  // many callers modify
    }

    return Init_File(
        OUT,
        To_REBOL_Path(path, Bool_ARG(DIR) ? PATH_OPT_SRC_IS_DIR : 0)
    );
}


//
//  export file-to-local: native [
//
//  "Converts a Rebol FILE! path to TEXT! of the local system file path"
//
//      return: "A TEXT! like \foo\bar is not a 'natural' FILE! %\foo\bar"
//          [text!]
//      path "Path to convert (by default, only FILE! for type safety)"
//          [<maybe> file! text!]
//      :pass "Convert FILE!s, but pass thru TEXT!, assuming it's local"
//      :full "For relative paths, prepends current dir for full path"
//      :no-tail-slash "do not add a slash or backslash to directory tail"
//  ]
//
DECLARE_NATIVE(FILE_TO_LOCAL)
{
    INCLUDE_PARAMS_OF_FILE_TO_LOCAL;

    Value* path = ARG(PATH);
    if (Is_Text(path)) {
        if (not Bool_ARG(PASS))
            return "-{FILE-TO-LOCAL needs :PASS to passthru STRING!}-";

        return Init_Text(OUT, Copy_String_At(path));  // callers modify
    }

    return Init_Text(
        OUT,
        To_Local_Path(
            path,
            REB_FILETOLOCAL_0
                | (Bool_ARG(FULL) ? REB_FILETOLOCAL_FULL : 0)
                | (Bool_ARG(NO_TAIL_SLASH) ? REB_FILETOLOCAL_NO_TAIL_SLASH : 0)
        )
    );
}


//
//  export what-dir: native [
//
//  "Returns the current directory path"
//
//      return: [~null~ file! url!]
//  ]
//
DECLARE_NATIVE(WHAT_DIR)
{
    INCLUDE_PARAMS_OF_WHAT_DIR;

    Value* current_path = Get_System(SYS_OPTIONS, OPTIONS_CURRENT_PATH);

    if (Is_File(current_path) or Is_Nulled(current_path)) {
        //
        // !!! Because of the need to track a notion of "current path" which
        // could be a URL! as well as a FILE!, the state is stored in the
        // system options.  For now--however--it is "duplicate" in the case
        // of a FILE!, because the OS has its own tracked state.  We let the
        // OS state win for files if they have diverged somehow--because the
        // code was already here and it would be more compatible.  But
        // reconsider the duplication.

        Value* refresh = Get_Current_Dir_Value();
        Copy_Cell(current_path, refresh);
        rebRelease(refresh);
    }
    else if (not Is_Url(current_path)) {
        //
        // Lousy error, but ATM the user can directly edit system.options.
        // They shouldn't be able to (or if they can, it should be validated)
        //
        return FAIL(current_path);
    }

    return rebValue(CANON(TRY), CANON(COPY), current_path);  // caller mutates
}


//
//  export change-dir: native [
//
//  "Changes the current path (where scripts with relative paths will be run)"
//
//      return: [~null~ file! url!]
//      path [<maybe> file! url!]
//  ]
//
DECLARE_NATIVE(CHANGE_DIR)
{
    INCLUDE_PARAMS_OF_CHANGE_DIR;

    Value* arg = ARG(PATH);
    Value* current_path = Get_System(SYS_OPTIONS, OPTIONS_CURRENT_PATH);

    if (Is_Url(arg)) {
        // There is no directory listing protocol for HTTP (although this
        // needs to be methodized to work for SFTP etc.)  So this takes
        // your word for it for the moment that it's a valid "directory".
        //
        // !!! Should it at least check for a trailing `/`?
    }
    else {
        assert(Is_File(arg));

        bool success = Set_Current_Dir_Value(arg);

        if (not success)
            return FAIL(PARAM(PATH));
    }

    Copy_Cell(current_path, arg);

    return COPY(arg);
}


extern Value* Get_Current_Exec(void);

//
//  export get-current-exec: native [
//
//  "Get the current path to the running executable"
//
//      return: [~null~ file!]
//  ]
//
DECLARE_NATIVE(GET_CURRENT_EXEC)
{
    INCLUDE_PARAMS_OF_GET_CURRENT_EXEC;

    return Get_Current_Exec();
}
