/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  s-file.c
**  Summary: file and path string handling
**  Section: strings
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

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
// Reduces width when possible to byte-size from unicode, unless the flag
// PATH_OPT_FORCE_UNI_DEST is used.
//
// Adds extra space at end for appending a dir /(star)
//     (Note: don't put actual star, as "/" "*" ends this comment)
// 
// REBDIFF: No longer appends current dir to volume when no
// root slash is provided (that odd MSDOS c:file case).
//
REBSER *To_REBOL_Path(const void *p, REBCNT len, REBFLGS flags)
{
    REBOOL saw_colon = FALSE;  // have we hit a ':' yet?
    REBOOL saw_slash = FALSE; // have we hit a '/' yet?
    REBUNI c;
    REBSER *dst;
    REBCNT n;
    REBCNT i;
    REBOOL unicode = LOGICAL(flags & PATH_OPT_UNI_SRC);

    const REBYTE *bp = unicode ? NULL : cast(const REBYTE *, p);

    const REBUNI *up = unicode ? cast(const REBUNI *, p) : NULL;

    if (len == 0)
        len = unicode ? Strlen_Uni(up) : LEN_BYTES(bp);

    n = 0;

    // The default is to scan unicode input to see if it contains any
    // codepoints over 0xFF, and if not make a byte-sized result string.
    // But this can be overridden with PATH_OPT_FORCE_UNI_DEST if (for
    // instance) the target is going to be used as a Win32 native string.
    //
    assert(
        (flags & PATH_OPT_FORCE_UNI_DEST)
            ? LOGICAL(flags & PATH_OPT_UNI_SRC)
            : TRUE
    );
    dst = ((flags & PATH_OPT_FORCE_UNI_DEST) || (unicode && Is_Wide(up, len)))
        ? Make_Unicode(len + FN_PAD)
        : Make_Binary(len + FN_PAD);

    c = '\0'; // for test after loop (in case loop does not run)
    for (i = 0; i < len;) {
        c = unicode ? up[i] : bp[i];
        i++;
#ifdef TO_WINDOWS
        if (c == ':') {
            // Handle the vol:dir/file format:
            if (saw_colon || saw_slash) return NULL; // no prior : or / allowed
            saw_colon = TRUE;
            if (i < len) {
                c = unicode ? up[i] : bp[i];
                if (c == '\\' || c == '/') i++; // skip / in foo:/file
            }
            c = '/'; // replace : with a /
        }
        else if (c == '\\' || c== '/') {
            if (saw_slash) continue;
            c = '/';
            saw_slash = TRUE;
        }
        else saw_slash = FALSE;
#endif
        SET_ANY_CHAR(dst, n++, c);
    }
    if ((flags & PATH_OPT_SRC_IS_DIR) && c != '/') {  // watch for %/c/ case
        SET_ANY_CHAR(dst, n++, '/');
    }
    SET_SERIES_LEN(dst, n);
    TERM_SEQUENCE(dst);

#ifdef TO_WINDOWS
    // Change C:/ to /C/ (and C:X to /C/X):
    if (saw_colon) Insert_Char(dst, 0, '/');
#endif

    return dst;
}


//
//  Value_To_REBOL_Path: C
// 
// Helper to above function.
//
REBSER *Value_To_REBOL_Path(REBVAL *val, REBOOL is_dir)
{
    assert(ANY_BINSTR(val));
    return To_REBOL_Path(
        VAL_RAW_DATA_AT(val),
        VAL_LEN_AT(val),
        (
            (VAL_BYTE_SIZE(val) ? PATH_OPT_UNI_SRC : 0)
            | (is_dir ? PATH_OPT_SRC_IS_DIR : 0)
        )
    );
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
REBSER *To_Local_Path(const void *p, REBCNT len, REBOOL unicode, REBOOL full)
{
    REBUNI c, d;
    REBSER *dst;
    REBCNT i = 0;
    REBCNT n = 0;
    REBUNI *out;
    REBCHR *lpath;
    REBCNT l = 0;
    const REBYTE *bp = unicode ? NULL : cast(const REBYTE *, p);
    const REBUNI *up = unicode ? cast(const REBUNI *, p) : NULL;

    if (len == 0)
        len = unicode ? Strlen_Uni(up) : LEN_BYTES(bp);

    // Prescan for: /c/dir = c:/dir, /vol/dir = //vol/dir, //dir = ??
    c = unicode ? up[i] : bp[i];
    if (c == '/') {         // %/
        dst = Make_Unicode(len+FN_PAD);
        out = UNI_HEAD(dst);
#ifdef TO_WINDOWS
        i++;
        if (i < len) {
            c = unicode ? up[i] : bp[i];
            i++;
        }
        if (c != '/') {     // %/c or %/c/ but not %/ %// %//c
            // peek ahead for a '/':
            d = '/';
            if (i < len) d = unicode ? up[i] : bp[i];
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
        if (full) l = OS_GET_CURRENT_DIR(&lpath);
        dst = Make_Unicode(l + len + FN_PAD); // may be longer (if lpath is encoded)
        if (full) {
#ifdef TO_WINDOWS
            assert(sizeof(REBCHR) == sizeof(REBUNI));
            Append_Uni_Uni(dst, cast(const REBUNI*, lpath), l);
#else
            REBINT clen = Decode_UTF8(
                UNI_HEAD(dst), cast(const REBYTE*, lpath), l, FALSE
            );
            SET_SERIES_LEN(dst, abs(clen));
            //Append_Unencoded(dst, lpath);
#endif
            Append_Codepoint_Raw(dst, OS_DIR_SEP);
            OS_FREE(lpath);
        }
        out = UNI_HEAD(dst);
        n = SER_LEN(dst);
    }

    // Prescan each file segment for: . .. directory names:
    // (Note the top of this loop always follows / or start)
    while (i < len) {
        if (full) {
            // Peek for: . ..
            c = unicode ? up[i] : bp[i];
            if (c == '.') {     // .
                i++;
                c = unicode ? up[i] : bp[i];
                if (c == '.') { // ..
                    c = unicode ? up[i + 1] : bp[i + 1];
                    if (c == 0 || c == '/') { // ../ or ..
                        i++;
                        // backup a dir
                        n -= (n > 2) ? 2 : n;
                        for (; n > 0 && out[n] != OS_DIR_SEP; n--);
                        c = c ? 0 : OS_DIR_SEP; // add / if necessary
                    }
                    // fall through on invalid ..x combination:
                }
                else {  // .a or . or ./
                    if (c == '/') {
                        i++;
                        c = 0; // ignore it
                    }
                    else if (c) c = '.'; // for store below
                }
                if (c) out[n++] = c;
            }
        }
        for (; i < len; i++) {
            c = unicode ? up[i] : bp[i];
            if (c == '/') {
                if (n == 0 || out[n-1] != OS_DIR_SEP) out[n++] = OS_DIR_SEP;
                i++;
                break;
            }
            out[n++] = c;
        }
    }
    out[n] = 0;
    SET_SERIES_LEN(dst, n);
//  TERM_SEQUENCE(dst);
//  Debug_Uni(dst);

    return dst;
}


//
//  Value_To_Local_Path: C
// 
// Helper to above function.
//
REBSER *Value_To_Local_Path(REBVAL *val, REBOOL full)
{
    assert(ANY_BINSTR(val));
    return To_Local_Path(
        VAL_RAW_DATA_AT(val), VAL_LEN_AT(val), NOT(VAL_BYTE_SIZE(val)), full
    );
}


//
//  Value_To_OS_Path: C
// 
// Helper to above function.
//
REBSER *Value_To_OS_Path(REBVAL *val, REBOOL full)
{
    REBSER *ser; // will be unicode size
#ifndef TO_WINDOWS
    REBSER *bin;
#endif

    assert(ANY_BINSTR(val));

    ser = To_Local_Path(
        VAL_RAW_DATA_AT(val), VAL_LEN_AT(val), NOT(VAL_BYTE_SIZE(val)), full
    );

#ifndef TO_WINDOWS
    // Posix needs UTF8 conversion:
    bin = Make_UTF8_Binary(
        UNI_HEAD(ser), SER_LEN(ser), FN_PAD, OPT_ENC_UNISRC
    );
    Free_Series(ser);
    ser = bin;
#endif

    return ser;
}
