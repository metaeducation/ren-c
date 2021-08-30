//
//  File: %p-dir.c
//  Summary: "file directory port interface"
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

#include "sys-core.h"

#include "file-req.h"


extern REBVAL *Open_File(FILEREQ *file);
extern REBVAL *Close_File(FILEREQ *file);
extern REBVAL *Read_File(size_t *actual, FILEREQ *file, REBYTE *data, size_t length);
extern REBVAL *Write_File(FILEREQ *file, const REBYTE *data, size_t length);
extern REBVAL *Query_File(FILEREQ *file);
extern REBVAL *Create_File(FILEREQ *file);
extern REBVAL *Delete_File(FILEREQ *file);
extern REBVAL *Rename_File(FILEREQ *file, const REBVAL *to);

REBVAL *Read_Directory(bool *done, FILEREQ *dir, FILEREQ *file);


//
//  Read_Dir_May_Fail: C
//
// Provide option to get file info too.
// Provide option to prepend dir path.
// Provide option to use wildcards.
//
static REBARR *Read_Dir_May_Fail(FILEREQ *dir)
{
    FILEREQ file;

    TRASH_POINTER_IF_DEBUG(file.path); // is output (not input)

    dir->modes |= RFM_DIR;

    REBDSP dsp_orig = DSP;

    while (true) {
        //
        // Put together the filename and the error (vs. a generic "cannot find
        // the file specified" message that doesn't say the name)
        //
        bool done;
        REBVAL *error = Read_Directory(&done, dir, &file);
        if (error)
            fail (Error_Cannot_Open_Raw(dir->path, error));

        if (done)
            break;

        Copy_Cell(DS_PUSH(), file.path);

        // Assume the file.devreq gets blown away on each loop, so there's
        // nowhere to free the file->path unless we do it here.
        //
        // !!! To the extent any of this code is going to stick around, it
        // should be considered whether whatever the future analogue of a
        // "devreq" is can protect its own state, e.g. be a Rebol object,
        // so there'd not be any API handles to free here.
        //
        rebRelease(m_cast(REBVAL*, file.path));
    }

    return Pop_Stack_Values(dsp_orig);
}


//
//  Init_Dir_Path: C
//
// !!! In R3-Alpha, this routine would do manipulations on the FILE! which was
// representing the directory, for instance by adding "*" onto the end of
// the directory so that Windows could use it for wildcard reading.  Yet this
// wasn't even needed in the POSIX code, so it would have to strip it out.
// The code has been changed so that any necessary transformations are done
// in the "device" code, during the File_To_Local translation.
//
static void Init_Dir_Path(FILEREQ *dir, const REBVAL *path) {
    memset(dir, 0, sizeof(FILEREQ));

    dir->modes |= RFM_DIR;
    dir->path = path;
}


//
//  Dir_Actor: C
//
// Internal port handler for file directories.
//
REB_R Dir_Actor(REBFRM *frame_, REBVAL *port, const REBVAL *verb)
{
    REBCTX *ctx = VAL_CONTEXT(port);
    REBVAL *spec = CTX_VAR(ctx, STD_PORT_SPEC);
    if (not IS_OBJECT(spec))
        fail (Error_Invalid_Spec_Raw(spec));

    REBVAL *path = Obj_Value(spec, STD_PORT_SPEC_HEAD_REF);
    if (path == NULL)
        fail (Error_Invalid_Spec_Raw(spec));

    if (IS_URL(path))
        path = Obj_Value(spec, STD_PORT_SPEC_HEAD_PATH);
    else if (not IS_FILE(path))
        fail (Error_Invalid_Spec_Raw(path));

    REBVAL *state = CTX_VAR(ctx, STD_PORT_STATE); // BLOCK! means port open

    switch (VAL_WORD_ID(verb)) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // implicitly supplied as `port`
        SYMID property = VAL_WORD_ID(ARG(property));

        switch (property) {
        case SYM_LENGTH: {
            if (not IS_BLOCK(state))
                return 0;
            REBLEN len;
            VAL_ARRAY_LEN_AT(&len, state);
            return Init_Integer(D_OUT, len); }

        case SYM_OPEN_Q:
            return Init_Logic(D_OUT, IS_BLOCK(state));

        default:
            break;
        }

        break; }

    case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PAR(source));

        if (REF(part) or REF(seek))
            fail (Error_Bad_Refines_Raw());

        UNUSED(PAR(string)); // handled in dispatcher
        UNUSED(PAR(lines)); // handled in dispatcher

        if (not IS_BLOCK(state)) {     // !!! ignores /SKIP and /PART, for now
            FILEREQ dir;
            Init_Dir_Path(&dir, path);
            Init_Block(D_OUT, Read_Dir_May_Fail(&dir));
        }
        else {
            // !!! This copies the strings in the block, shallowly.  What is
            // the purpose of doing this?  Why copy at all?

            REBLEN len;
            VAL_ARRAY_LEN_AT(&len, state);
            Init_Block(
                D_OUT,
                Copy_Array_Core_Managed(
                    VAL_ARRAY(state),
                    0, // at
                    VAL_SPECIFIER(state),
                    len, // tail
                    0, // extra
                    ARRAY_MASK_HAS_FILE_LINE, // flags
                    TS_STRING // types
                )
            );
        }
        return D_OUT; }

    case SYM_CREATE: {
        if (IS_BLOCK(state))
            fail (Error_Already_Open_Raw(path));

      create:;

        FILEREQ dir;
        Init_Dir_Path(&dir, path);

        REBVAL *error = Create_File(&dir);
        if (error) {
            rebRelease(error);  // !!! throws away details
            fail (Error_No_Create_Raw(path));  // higher level error
        }

        if (VAL_WORD_ID(verb) != SYM_CREATE)
            Init_Nulled(state);

        RETURN (port); }

    case SYM_RENAME: {
        INCLUDE_PARAMS_OF_RENAME;

        if (IS_BLOCK(state))
            fail (Error_Already_Open_Raw(path));

        FILEREQ dir;
        Init_Dir_Path(&dir, path);

        UNUSED(ARG(from));  // already have as port parameter

        REBVAL *error = Rename_File(&dir, ARG(to));
        if (error) {
            rebRelease(error);  // !!! throws away details
            fail (Error_No_Rename_Raw(path));  // higher level error
        }

        RETURN (port); }

    case SYM_DELETE: {
        Init_Nulled(state);

        FILEREQ dir;
        Init_Dir_Path(&dir, path);

        REBVAL *error = Delete_File(&dir);
        if (error) {
            rebRelease(error);  // !!! throws away details
            fail (Error_No_Delete_Raw(path));  // higher level error
        }

        RETURN (port); }

    case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;

        UNUSED(PAR(spec));

        if (REF(read) or REF(write))
            fail (Error_Bad_Refines_Raw());

        // !! If open fails, what if user does a READ w/o checking for error?
        if (IS_BLOCK(state))
            fail (Error_Already_Open_Raw(path));

        if (REF(new))
            goto create;

        FILEREQ dir;
        Init_Dir_Path(&dir, path);

        Init_Block(state, Read_Dir_May_Fail(&dir));

        RETURN (port); }

    case SYM_CLOSE:
        Init_Nulled(state);
        RETURN (port);

    case SYM_QUERY: {
        Init_Nulled(state);

        FILEREQ dir;
        Init_Dir_Path(&dir, path);

        REBVAL *error = Query_File(&dir);
        if (error) {
            rebRelease(error); // !!! R3-Alpha threw out error, returns null
            return nullptr;
        }

        REBVAL *info = Query_File_Or_Dir(port, &dir);
        return info; }

    default:
        break;
    }

    return R_UNHANDLED;
}
