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
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"


//
//  Read_Dir_May_Fail: C
//
// Provide option to get file info too.
// Provide option to prepend dir path.
// Provide option to use wildcards.
//
static REBARR *Read_Dir_May_Fail(REBREQ *dir)
{
    REBREQ *file = OS_MAKE_DEVREQ(RDI_FILE);

    TRASH_POINTER_IF_DEBUG(ReqFile(file)->path); // is output (not input)

    struct rebol_devreq *req = Req(dir);
    req->modes |= RFM_DIR;
    req->common.data = cast(REBYTE*, file);

    REBDSP dsp_orig = DSP;

    while (true) {
        OS_DO_DEVICE_SYNC(dir, RDC_READ);

        if (req->flags & RRF_DONE)
            break;

        Move_Value(DS_PUSH(), ReqFile(file)->path);

        // Assume the file.devreq gets blown away on each loop, so there's
        // nowhere to free the file->path unless we do it here.
        //
        // !!! To the extent any of this code is going to stick around, it
        // should be considered whether whatever the future analogue of a
        // "devreq" is can protect its own state, e.g. be a Rebol object,
        // so there'd not be any API handles to free here.
        //
        rebRelease(m_cast(REBVAL*, ReqFile(file)->path));
    }

    Free_Req(file);

    // !!! This is some kind of error tolerance, review what it is for.
    //
    bool enabled = false;
    if (enabled
        and (
            NOT_FOUND != Find_Char_In_Str(
                '*',
                VAL_STRING(ReqFile(dir)->path),
                VAL_INDEX(ReqFile(dir)->path), // first index to examine
                STR_LEN(VAL_STRING(ReqFile(dir)->path)) + 1, // highest return + 1
                0, // skip
                AM_FIND_CASE // not relevant
            )
            or NOT_FOUND != Find_Char_In_Str(
                '?',
                VAL_STRING(ReqFile(dir)->path),
                VAL_INDEX(ReqFile(dir)->path), // first index to examine
                STR_LEN(VAL_STRING(ReqFile(dir)->path)) + 1, // highest return + 1
                0, // skip
                AM_FIND_CASE // not relevant
            )
        )
    ){
        // no matches found, but not an error
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
static void Init_Dir_Path(
    REBREQ *dir,
    const REBVAL *path,
    REBCNT policy
){
    UNUSED(policy);

    struct rebol_devreq *req = Req(dir);
    req->modes |= RFM_DIR;

    Secure_Port(Canon(SYM_FILE), dir, path /* , dir->path */);

    ReqFile(dir)->path = path;
}


//
//  Dir_Actor: C
//
// Internal port handler for file directories.
//
static REB_R Dir_Actor(REBFRM *frame_, REBVAL *port, const REBVAL *verb)
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

    //const REBYTE *flags = Security_Policy(SYM_FILE, path);


    switch (VAL_WORD_SYM(verb)) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // implicitly supplied as `port`
        REBSYM property = VAL_WORD_SYM(ARG(property));

        switch (property) {
        case SYM_LENGTH: {
            REBCNT len = IS_BLOCK(state) ? VAL_ARRAY_LEN_AT(state) : 0;
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
            REBREQ *dir = OS_MAKE_DEVREQ(RDI_FILE);
            ReqPortCtx(dir) = ctx;

            Init_Dir_Path(dir, path, POL_READ);
            Init_Block(D_OUT, Read_Dir_May_Fail(dir));

            Free_Req(dir);
        }
        else {
            // !!! This copies the strings in the block, shallowly.  What is
            // the purpose of doing this?  Why copy at all?
            Init_Block(
                D_OUT,
                Copy_Array_Core_Managed(
                    VAL_ARRAY(state),
                    0, // at
                    VAL_SPECIFIER(state),
                    VAL_ARRAY_LEN_AT(state), // tail
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

        REBREQ *dir = OS_MAKE_DEVREQ(RDI_FILE);
        ReqPortCtx(dir) = ctx;

        Init_Dir_Path(dir, path, POL_WRITE); // Sets RFM_DIR too

        REBVAL *result = OS_DO_DEVICE(dir, RDC_CREATE);
        assert(result != NULL); // should be synchronous

        Free_Req(dir);

        if (rebDid("error?", result, rebEND)) {
            rebRelease(result); // !!! throws away details
            fail (Error_No_Create_Raw(path)); // higher level error
        }

        rebRelease(result); // ignore result

        if (VAL_WORD_SYM(verb) != SYM_CREATE)
            Init_Blank(state);

        RETURN (port); }

    case SYM_RENAME: {
        INCLUDE_PARAMS_OF_RENAME;

        if (IS_BLOCK(state))
            fail (Error_Already_Open_Raw(path));

        REBREQ *dir = OS_MAKE_DEVREQ(RDI_FILE);
        ReqPortCtx(dir) = ctx;

        Init_Dir_Path(dir, path, POL_WRITE); // Sets RFM_DIR

        UNUSED(ARG(from)); // implicit
        Req(dir)->common.data = cast(REBYTE*, ARG(to)); // !!! hack!

        REBVAL *result = OS_DO_DEVICE(dir, RDC_RENAME);
        assert(result != NULL); // should be synchronous

        Free_Req(dir);

        if (rebDid("error?", result, rebEND)) {
            rebRelease(result); // !!! throws away details
            fail (Error_No_Rename_Raw(path)); // higher level error
        }

        rebRelease(result); // ignore result
        RETURN (port); }

    case SYM_DELETE: {
        Init_Blank(state);

        REBREQ *dir = OS_MAKE_DEVREQ(RDI_FILE);
        ReqPortCtx(dir) = ctx;

        Init_Dir_Path(dir, path, POL_WRITE);

        // !!! add *.r deletion
        // !!! add recursive delete (?)
        REBVAL *result = OS_DO_DEVICE(dir, RDC_DELETE);
        assert(result != NULL); // should be synchronous

        Free_Req(dir);

        if (rebDid("error?", result, rebEND)) {
            rebRelease(result); // !!! throws away details
            fail (Error_No_Delete_Raw(path)); // higher level error
        }

        rebRelease(result); // ignore result
        RETURN (port); }

    case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;

        UNUSED(PAR(spec));

        if (REF(read) or REF(write) or REF(seek) or REF(allow))
            fail (Error_Bad_Refines_Raw());

        // !! If open fails, what if user does a READ w/o checking for error?
        if (IS_BLOCK(state))
            fail (Error_Already_Open_Raw(path));

        if (REF(new))
            goto create;

        REBREQ *dir = OS_MAKE_DEVREQ(RDI_FILE);
        ReqPortCtx(dir) = ctx;

        Init_Dir_Path(dir, path, POL_READ);
        Init_Block(state, Read_Dir_May_Fail(dir));

        Free_Req(dir);
        RETURN (port); }

    case SYM_CLOSE:
        Init_Blank(state);
        RETURN (port);

    case SYM_QUERY: {
        Init_Blank(state);

        REBREQ *dir = OS_MAKE_DEVREQ(RDI_FILE);
        ReqPortCtx(dir) = ctx;

        Init_Dir_Path(dir, path, POL_READ);
        REBVAL *result = OS_DO_DEVICE(dir, RDC_QUERY);
        assert(result != NULL); // should be synchronous

        if (rebDid("error?", result, rebEND)) {
            Free_Req(dir);
            rebRelease(result); // !!! R3-Alpha threw out error, returns null
            return nullptr;
        }

        rebRelease(result); // ignore result

        Query_File_Or_Dir(D_OUT, port, dir);
        Free_Req(dir);
        return D_OUT; }

    default:
        break;
    }

    return R_UNHANDLED;
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
