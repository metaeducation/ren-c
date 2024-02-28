//
//  File: %s-file.c
//  Summary: "file and path string handling"
//  Section: strings
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

#include "sys-core.h"


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
REBSER *To_REBOL_Path(const Cell* string, REBFLGS flags)
{
    assert(IS_TEXT(string));

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    bool lead_slash = false; // did we restart to insert a leading slash?
    bool saw_colon = false; // have we hit a ':' yet?
    bool saw_slash = false; // have we hit a '/' yet?
    bool last_was_slash = false; // was last character appended a slash?

restart:;
    REBCHR(const*) up = VAL_UNI_AT(string);
    REBLEN len = VAL_LEN_AT(string);

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
                // Change C:/ to /C/ (and C:X to /C/X)
                //
                TERM_SEQUENCE_LEN(mo->series, mo->start); // drop mold so far
                Append_Utf8_Codepoint(mo->series, '/'); // insert a /
                lead_slash = true; // don't do this the second time around
                goto restart;
            }

            saw_colon = true;

            Append_Utf8_Codepoint(mo->series, '/'); // replace : with a /

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

        Append_Utf8_Codepoint(mo->series, c);
    }

    // If this is supposed to be a directory and the last character is not a
    // slash, make it one (this is Rebol's rule for FILE!s that are dirs)
    //
    if ((flags & PATH_OPT_SRC_IS_DIR) and c != '/') // watch for %/c/ case
        Append_Utf8_Codepoint(mo->series, '/');

    return Pop_Molded_String(mo);
}


//
//  Mold_File_To_Local: C
//
// Implementation routine of To_Local_Path which leaves the path in the mold
// buffer (e.g. for further appending or just counting the number of bytes)
//
void Mold_File_To_Local(REB_MOLD *mo, const Cell* file, REBFLGS flags) {
    assert(IS_FILE(file));

    REBCHR(const*) up = VAL_UNI_AT(file);
    REBLEN len = VAL_LEN_AT(file);

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
                Append_Utf8_Codepoint(mo->series, c);
                Append_Utf8_Codepoint(mo->series, ':');
                up = NEXT_CHR(&c, dp);
                ++i;
            }
            else {
                // %/cc %//cc => "//cc"
                //
                Append_Utf8_Codepoint(mo->series, OS_DIR_SEP);
            }
        }
    #endif

        Append_Utf8_Codepoint(mo->series, OS_DIR_SEP);
    }
    else if (flags & REB_FILETOLOCAL_FULL) {
        //
        // When full path is requested and the source path was relative (e.g.
        // did not start with `/`) then prepend the current directory.
        //
        // OS_GET_CURRENT_DIR() comes back in Rebol-format FILE! form, hence
        // it has to be converted to the local-format before being prepended
        // to the local-format file path we're generating.  So recurse.  Don't
        // use REB_FILETOLOCAL_FULL as that would recurse (we assume a fully
        // qualified path was returned by OS_GET_CURRENT_DIR())
        //
        Value* lpath = OS_GET_CURRENT_DIR();
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
                assert(c != '\0' || i == len);

                if (c == '\0' || c == '/')
                    continue; // . or ./ mean stay in same directory

                if (c != '.') {
                    //
                    // It's a filename like %.xxx, which is legal.  Output the
                    // . character we'd found before the peek ahead and break
                    // to the next loop that copies without further `.` search
                    //
                    Append_Utf8_Codepoint(mo->series, '.');
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
                    REBLEN n = SER_LEN(mo->series);
                    if (n > mo->start) {
                        --n;
                        assert(*BIN_AT(mo->series, n) == OS_DIR_SEP);
                        if (n > mo->start)
                            --n; // don't want the *ending* slash

                        while (
                            n > mo->start
                            and *BIN_AT(mo->series, n) != OS_DIR_SEP
                        ){
                            --n;
                        }
                        TERM_SEQUENCE_LEN(mo->series, n); // loses /
                    }

                    // Add separator and keep looking (%../../ can happen)
                    //
                    Append_Utf8_Codepoint(mo->series, OS_DIR_SEP);
                    continue;
                }

                // Files named `..foo` are ordinary files.  Account for the
                // pending `..` and fall through to the loop that doesn't look
                // further at .
                //
                Append_Utf8_Codepoint(mo->series, '.');
                Append_Utf8_Codepoint(mo->series, '.');
            }
        }

    segment_loop:;
        for (; i < len; up = NEXT_CHR(&c, up), ++i) {
            //
            // Keep copying characters out of the path segment until we find
            // a slash or hit the end of the input path string.
            //
            if (c != '/') {
                Append_Utf8_Codepoint(mo->series, c);
                continue;
            }

            REBLEN n = SER_LEN(mo->series);
            if (
                n > mo->start
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
            Append_Utf8_Codepoint(mo->series, OS_DIR_SEP);
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
        REBLEN n = SER_LEN(mo->series);
        if (n > mo->start and *BIN_AT(mo->series, n - 1) == OS_DIR_SEP)
            TERM_SEQUENCE_LEN(mo->series, n - 1);
    }

    // If one is to list a directory's contents, you might want the name to
    // be `c:\foo\*` instead of just `c:\foo` (Windows needs this)
    //
    if (flags & REB_FILETOLOCAL_WILD)
        Append_Utf8_Codepoint(mo->series, '*');
}


//
//  To_Local_Path: C
//
// Convert Rebol-format filename to a local-format filename.  This is the
// opposite operation of To_REBOL_Path.
//
REBSER *To_Local_Path(const Cell* file, REBFLGS flags) {
    DECLARE_MOLD (mo);
    Push_Mold(mo);

    Mold_File_To_Local(mo, file, flags);
    return Pop_Molded_String(mo);
}
