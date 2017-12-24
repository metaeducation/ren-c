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

#define FN_PAD 2    // pad file name len for adding /, /*, and /?


//
//  To_REBOL_Path: C
//
// Convert local filename to a REBOL filename.
//
// Allocate and return a new series with the converted path.
// Return NULL on error.
//
// Adds extra space at end for appending a dir /(star)
//     (Note: don't put actual star, as "/" "*" ends this comment)
//
// Note: This routine apparently once appended the current directory to the
// volume when no root slash was provided.  It was an odd case to support
// the MSDOS convention of `c:file`.  That is not done here.
//
REBSER *To_REBOL_Path(const REBUNI *up, REBCNT len, REBFLGS flags)
{
#ifdef TO_WINDOWS
    REBOOL saw_colon = FALSE;  // have we hit a ':' yet?
    REBOOL saw_slash = FALSE; // have we hit a '/' yet?
#endif

    REBSER *dst = Make_Unicode(len + FN_PAD);

    REBUNI c = '\0'; // for test after loop (in case loop does not run)

    REBCNT i;
    REBCNT n = 0;
    for (i = 0; i < len;) {
        c = up[i];
        i++;
#ifdef TO_WINDOWS
        if (c == ':') {
            // Handle the vol:dir/file format:
            if (saw_colon || saw_slash)
                return NULL; // no prior : or / allowed
            saw_colon = TRUE;
            if (i < len) {
                c = up[i];
                if (c == '\\' || c == '/') i++; // skip / in foo:/file
            }
            c = '/'; // replace : with a /
        }
        else if (c == '\\' || c== '/') {
            if (saw_slash)
                continue;
            c = '/';
            saw_slash = TRUE;
        }
        else
            saw_slash = FALSE;
#endif
        SET_ANY_CHAR(dst, n++, c);
    }
    if ((flags & PATH_OPT_SRC_IS_DIR) && c != '/') {  // watch for %/c/ case
        SET_ANY_CHAR(dst, n++, '/');
    }
    TERM_SEQUENCE_LEN(dst, n);

#ifdef TO_WINDOWS
    // Change C:/ to /C/ (and C:X to /C/X):
    if (saw_colon)
        Insert_Char(dst, 0, '/');
#endif

    return dst;
}


//
//  To_Local_Path: C
//
// Convert REBOL filename to a local filename.
//
// Allocate and return a new series with the converted path.
// Return 0 on error.
//
// Adds extra space at end for appending a dir /(star)
//     (Note: don't put actual star, as "/" "*" ends this comment)
//
// Expands width for OS's that require it.
//
REBSER *To_Local_Path(
    const REBUNI *up,
    REBCNT len,
    REBOOL full
){
    REBCNT n = 0;

    // Prescan for: /c/dir = c:/dir, /vol/dir = //vol/dir, //dir = ??
    //
    REBCNT i = 0;
    REBUNI c = up[i];

    // may be longer (if lpath is encoded)
    //
    REBSER *result;
    REBUNI *out;

    if (c == '/') {         // %/
        result = Make_Unicode(len + FN_PAD);
        out = UNI_HEAD(result);
    #ifdef TO_WINDOWS
        i++;
        if (i < len) {
            c = up[i];
            i++;
        }
        if (c != '/') {     // %/c or %/c/ but not %/ %// %//c
            // peek ahead for a '/':
            REBUNI d = '/';
            if (i < len)
                d = up[i];
            if (d == '/') { // %/c/ => "c:/"
                i++;
                out[n++] = c;
                out[n++] = ':';
            }
            else {
                out[n++] = OS_DIR_SEP;  // %/cc %//cc => "//cc"
                i--;
            }
        }
    #endif
        out[n++] = OS_DIR_SEP;
    }
    else {
        if (full) {
            REBVAL *lpath = OS_GET_CURRENT_DIR();

            // !!! For now the wchar_t routines do not work on POSIX.  Given
            // that all is going to change with UTF-8 Everywhere, a bit of
            // inefficiency here going via UTF-8 is not that big a deal.
            //
            size_t lpath_size;
            const REBOOL full = TRUE;
            char *lpath_utf8 = rebFileToLocalAlloc(
                &lpath_size, lpath, full
            );

            // !!! Overestimate: lpath_size is going to be greater than
            // number of codepoints.
            //
            result = Make_Unicode(len + lpath_size + FN_PAD);

            const REBOOL crlf_to_lf = FALSE;
            Append_UTF8_May_Fail(result, lpath_utf8, lpath_size, crlf_to_lf);

            OS_FREE(lpath_utf8);
            rebRelease(lpath);
        }
        else
            result = Make_Unicode(len + FN_PAD);

        out = UNI_HEAD(result);
        n = SER_LEN(result);
    }

    // Prescan each file segment for: . .. directory names.  (Note the top of
    // this loop always follows / or start).  Each iteration takes care of one
    // segment of the path, i.e. stops after OS_DIR_SEP
    //
    while (i < len) {
        if (full) {
            // Peek for: . ..
            c = up[i];
            if (c == '.') {     // .
                i++;
                c = up[i];
                if (c == '.') { // ..
                    c = up[i + 1];
                    if (c == 0 || c == '/') { // ../ or ..
                        i++;
                        // backup a dir
                        n -= (n > 2) ? 2 : n;
                        for (; n > 0 && out[n] != OS_DIR_SEP; n--)
                            NOOP;
                        c = c ? 0 : OS_DIR_SEP; // add / if necessary
                    }
                    // fall through on invalid ..x combination:
                }
                else {  // .a or . or ./
                    if (c == '/') {
                        c = 0; // ignore it
                    }
                    else if (c) c = '.'; // for store below
                }
                if (c) out[n++] = c;
            }
        }
        for (; i < len; i++) {
            c = up[i];
            if (c == '/') {
                if (n == 0 || out[n-1] != OS_DIR_SEP)
                    out[n++] = OS_DIR_SEP;
                i++;
                break;
            }
            out[n++] = c;
        }
    }
    out[n] = '\0';
    SET_SERIES_LEN(result, n);
    ASSERT_SERIES_TERM(result);

    return result;
}
