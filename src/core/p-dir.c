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
//  Read_Dir_May_Fail: C
//
// Provide option to get file info too.
// Provide option to prepend dir path.
// Provide option to use wildcards.
//
static REBARR *Read_Dir_May_Fail(struct devreq_file *dir)
{
    struct devreq_file file;
    CLEARS(&file);

    TRASH_POINTER_IF_DEBUG(file.path); // file is output (not input)

    REBREQ *req = AS_REBREQ(dir);
    req->modes |= RFM_DIR;
    req->common.data = cast(Byte*, &file);

    REBDSP dsp_orig = DSP;

    while (true) {
        OS_DO_DEVICE_SYNC(req, RDC_READ);

        if (req->flags & RRF_DONE)
            break;

        DS_PUSH_TRASH;
        Move_Value(DS_TOP, file.path);

        // Assume the file.devreq gets blown away on each loop, so there's
        // nowhere to free the file->path unless we do it here.
        //
        // !!! To the extent any of this code is going to stick around, it
        // should be considered whether whatever the future analogue of a
        // "devreq" is can protect its own state, e.g. be a Rebol object,
        // so there'd not be any API handles to free here.
        //
        rebRelease(m_cast(Value*, file.path));
    }

    // !!! This is some kind of error tolerance, review what it is for.
    //
    bool enabled = false;
    if (enabled
        and (
            NOT_FOUND != Find_Str_Char(
                '*',
                VAL_SERIES(dir->path),
                0, // !!! "lowest return index?"
                VAL_INDEX(dir->path), // first index to examine
                SER_LEN(VAL_SERIES(dir->path)) + 1, // highest return + 1
                0, // skip
                AM_FIND_CASE // not relevant
            )
            or
            NOT_FOUND != Find_Str_Char(
                '?',
                VAL_SERIES(dir->path),
                0, // !!! "lowest return index?"
                VAL_INDEX(dir->path), // first index to examine
                SER_LEN(VAL_SERIES(dir->path)) + 1, // highest return + 1
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
    struct devreq_file *dir,
    const Value* path,
    REBLEN policy
){
    UNUSED(policy);

    REBREQ *req = AS_REBREQ(dir);
    req->modes |= RFM_DIR;

    dir->path = path;
}


//
//  Dir_Actor: C
//
// Internal port handler for file directories.
//
static REB_R Dir_Actor(REBFRM *frame_, Value* port, Value* verb)
{
    REBCTX *ctx = VAL_CONTEXT(port);
    Value* spec = CTX_VAR(ctx, STD_PORT_SPEC);
    if (not IS_OBJECT(spec))
        fail (Error_Invalid_Spec_Raw(spec));

    Value* path = Obj_Value(spec, STD_PORT_SPEC_HEAD_REF);
    if (path == nullptr)
        fail (Error_Invalid_Spec_Raw(spec));

    if (IS_URL(path))
        path = Obj_Value(spec, STD_PORT_SPEC_HEAD_PATH);
    else if (not IS_FILE(path))
        fail (Error_Invalid_Spec_Raw(path));

    Value* state = CTX_VAR(ctx, STD_PORT_STATE); // BLOCK! means port open

    //const Byte *flags = Security_Policy(SYM_FILE, path);

    // Get or setup internal state data:

    struct devreq_file dir;
    CLEARS(&dir);
    dir.devreq.port_ctx = ctx;
    dir.devreq.device = RDI_FILE;

    switch (Cell_Word_Id(verb)) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // implicitly supplied as `port`
        Option(SymId) property = Cell_Word_Id(ARG(property));

        switch (property) {
        case SYM_LENGTH: {
            REBLEN len = IS_BLOCK(state) ? VAL_ARRAY_LEN_AT(state) : 0;
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
        if (REF(part)) {
            UNUSED(ARG(limit));
            fail (Error_Bad_Refines_Raw());
        }
        if (REF(seek)) {
            UNUSED(ARG(index));
            fail (Error_Bad_Refines_Raw());
        }
        UNUSED(PAR(string)); // handled in dispatcher
        UNUSED(PAR(lines)); // handled in dispatcher

        if (not IS_BLOCK(state)) {     // !!! ignores /SKIP and /PART, for now
            Init_Dir_Path(&dir, path, POL_READ);
            Init_Block(D_OUT, Read_Dir_May_Fail(&dir));
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
                    ARRAY_FLAG_FILE_LINE, // flags
                    TS_STRING // types
                )
            );
        }
        return D_OUT; }

    case SYM_CREATE: {
        if (IS_BLOCK(state))
            fail (Error_Already_Open_Raw(path));
    create:
        Init_Dir_Path(&dir, path, POL_WRITE); // Sets RFM_DIR too

        Value* result = OS_DO_DEVICE(&dir.devreq, RDC_CREATE);
        assert(result != nullptr);  // should be synchronous

        if (rebDid("error?", result)) {
            rebRelease(result); // !!! throws away details
            fail (Error_No_Create_Raw(path)); // higher level error
        }

        rebRelease(result); // ignore result

        if (Cell_Word_Id(verb) != SYM_CREATE)
            Init_Blank(state);

        RETURN (port); }

    case SYM_RENAME: {
        INCLUDE_PARAMS_OF_RENAME;

        if (IS_BLOCK(state))
            fail (Error_Already_Open_Raw(path));

        Init_Dir_Path(&dir, path, POL_WRITE); // Sets RFM_DIR

        UNUSED(ARG(from)); // implicit
        dir.devreq.common.data = cast(Byte*, ARG(to)); // !!! hack!

        Value* result = OS_DO_DEVICE(&dir.devreq, RDC_RENAME);
        assert(result != nullptr); // should be synchronous

        if (rebDid("error?", result)) {
            rebRelease(result); // !!! throws away details
            fail (Error_No_Rename_Raw(path)); // higher level error
        }

        rebRelease(result); // ignore result
        RETURN (port); }

    case SYM_DELETE: {
        Init_Blank(state);

        Init_Dir_Path(&dir, path, POL_WRITE);

        // !!! add *.r deletion
        // !!! add recursive delete (?)
        Value* result = OS_DO_DEVICE(&dir.devreq, RDC_DELETE);
        assert(result != nullptr);  // should be synchronous

        if (rebDid("error?", result)) {
            rebRelease(result); // !!! throws away details
            fail (Error_No_Delete_Raw(path)); // higher level error
        }

        rebRelease(result); // ignore result
        RETURN (port); }

    case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;

        UNUSED(PAR(spec));
        if (REF(read))
            fail (Error_Bad_Refines_Raw());
        if (REF(write))
            fail (Error_Bad_Refines_Raw());
        if (REF(seek))
            fail (Error_Bad_Refines_Raw());
        if (REF(allow)) {
            UNUSED(ARG(access));
            fail (Error_Bad_Refines_Raw());
        }

        // !! If open fails, what if user does a READ w/o checking for error?
        if (IS_BLOCK(state))
            fail (Error_Already_Open_Raw(path));

        if (REF(new))
            goto create;

        Init_Dir_Path(&dir, path, POL_READ);
        Init_Block(state, Read_Dir_May_Fail(&dir));
        RETURN (port); }

    case SYM_CLOSE:
        Init_Blank(state);
        RETURN (port);

    case SYM_QUERY: {
        Init_Blank(state);

        Init_Dir_Path(&dir, path, POL_READ);
        Value* result = OS_DO_DEVICE(&dir.devreq, RDC_QUERY);
        assert(result != nullptr);  // should be synchronous

        if (rebDid("error?", result)) {
            rebRelease(result); // !!! R3-Alpha threw out error, returns null
            return nullptr;
        }

        rebRelease(result); // ignore result

        Query_File_Or_Dir(D_OUT, port, &dir);
        return D_OUT; }

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_PORT, verb));
}


//
//  get-dir-actor-handle: native [
//
//  {Retrieve handle to the native actor for directories}
//
//      return: [handle!]
//  ]
//
DECLARE_NATIVE(get_dir_actor_handle)
{
    Make_Port_Actor_Handle(D_OUT, &Dir_Actor);
    return D_OUT;
}
