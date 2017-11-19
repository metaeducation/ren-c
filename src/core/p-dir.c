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

// Special policy: Win32 does not wanting tail slash for dir info
#define REMOVE_TAIL_SLASH (1<<10)


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
    req->common.data = cast(REBYTE*, &file);

    REBDSP dsp_orig = DSP;

    REBINT result;
    while (
        (result = OS_DO_DEVICE(req, RDC_READ)) == 0
        && NOT(req->flags & RRF_DONE)
    ){
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
        rebFree(file.path);
    }

    if (
        result < 0
        && req->error != -RFE_OPEN_FAIL
        && (
            NOT_FOUND != Find_Str_Char(
                '*',
                VAL_SERIES(dir->path),
                0, // !!! "lowest return index?"
                VAL_INDEX(dir->path), // first index to examine
                SER_LEN(VAL_SERIES(dir->path)) + 1, // highest return + 1
                0, // skip
                AM_FIND_CASE // not relevant
            )
            ||
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
        result = 0;  // no matches found, but not an error
    }

    if (result < 0)
        fail (Error_On_Port(
            RE_CANNOT_OPEN, cast(REBCTX*, req->port), dir->devreq.error
        ));

    return Pop_Stack_Values(dsp_orig);
}


//
//  Init_Dir_Path: C
//
// Convert REBOL dir path to file system path.
// On Windows, we will also need to append a * if necessary.
//
// ARGS:
// Wild:
//     0 - no wild cards, path must end in / else error
//     1 - accept wild cards * and ?, and * if need
//    -1 - not wild, if path does not end in /, add it
//
static void Init_Dir_Path(
    struct devreq_file *dir,
    const REBVAL *path,
    REBINT wild,
    REBCNT policy
){
    REBREQ *req = AS_REBREQ(dir);
    req->modes |= RFM_DIR;

    Secure_Port(SYM_FILE, req, path /* , dir->path */);

    // !!! This code wants to do some mutations on the result.  When the idea
    // of "local file translation" was known to the core, it used FN_PAD to
    // make sure the generated path had enough at least 2 extra characters
    // so it could mutate it for / and *.  For the moment, we just make a
    // copy of the incoming path value with 2 extra chars so we can mutate it,
    // and hope that the mutation wasn't dependent on the "local conversion".
    //
    dir->path = rebCopyExtra(path, 2);

    REBUNI *up = VAL_UNI_AT(dir->path);
    REBCNT len = VAL_LEN_AT(dir->path);
    if (len == 1 && up[0] == '.') {
        if (wild > 0) {
            up[0] = '*';
            up[1] = '\0';
        }
    }
    else if (len == 2 && up[0] == '.' && up[1] == '.') {
        // Insert * if needed:
        if (wild > 0) {
            up[len++] = '/';
            up[len++] = '*';
            up[len] = '\0';
        }
    }
    else if (up[len - 1] == '/' || up[len - 1] == '\\') {
        if ((policy & REMOVE_TAIL_SLASH) && len > 1) {
            up[len - 1] = '\0';
        }
        else {
            // Insert * if needed:
            if (wild > 0) {
                up[len++] = '*';
                up[len] = '\0';
            }
        }
    } else {
        // Path did not end with /, so we better be wild:
        if (wild == 0) {
            rebFree(dir->path);
            fail (Error_Bad_File_Path_Raw(path));
        }

        if (wild < 0) {
            up[len++] = OS_DIR_SEP;
            up[len] = '\0';
        }
    }

    TERM_UNI_LEN(VAL_SERIES(dir->path), len + VAL_INDEX(dir->path));

    // !!! For the moment, dir->path's lifetime is managed explicitly, and
    // must be freed in Cleanup_Dir_Path()
}


//
//  Cleanup_Dir_Path: C
//
// !!! Temporary attempt to get leak-free behavior out of very old and creaky
// R3-Alpha code, that had a very laissez-faire model of whose responsibility
// it was to manage memory.
//
static void Cleanup_Dir_Path(struct devreq_file *dir)
{
    assert(dir->path != NULL);
    rebFree(dir->path);
}


//
//  Dir_Actor: C
//
// Internal port handler for file directories.
//
static REB_R Dir_Actor(REBFRM *frame_, REBCTX *port, REBSYM action)
{
    REBVAL *spec = CTX_VAR(port, STD_PORT_SPEC);
    if (NOT(IS_OBJECT(spec)))
        fail (Error_Invalid_Spec_Raw(spec));

    REBVAL *path = Obj_Value(spec, STD_PORT_SPEC_HEAD_REF);
    if (path == NULL)
        fail (Error_Invalid_Spec_Raw(spec));

    if (IS_URL(path))
        path = Obj_Value(spec, STD_PORT_SPEC_HEAD_PATH);
    else if (NOT(IS_FILE(path)))
        fail (Error_Invalid_Spec_Raw(path));

    REBVAL *state = CTX_VAR(port, STD_PORT_STATE); // BLOCK! means port open

    //const REBYTE *flags = Security_Policy(SYM_FILE, path);

    // Get or setup internal state data:

    struct devreq_file dir;
    CLEARS(&dir);
    dir.devreq.port = port;
    dir.devreq.device = RDI_FILE;

    // Default to outputting the PORT! value as a result.
    //
    Move_Value(D_OUT, D_ARG(1));

    switch (action) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // implicitly supplied as `port`
        REBSYM property = VAL_WORD_SYM(ARG(property));

        switch (property) {
        case SYM_LENGTH: {
            REBCNT len = IS_BLOCK(state) ? VAL_ARRAY_LEN_AT(state) : 0;
            Init_Integer(D_OUT, len);
            return R_OUT; }

        case SYM_OPEN_Q:
            return R_FROM_BOOL(IS_BLOCK(state));

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

        if (!IS_BLOCK(state)) {     // !!! ignores /SKIP and /PART, for now
            Init_Dir_Path(&dir, path, 1, POL_READ);
            Init_Block(D_OUT, Read_Dir_May_Fail(&dir));
            Cleanup_Dir_Path(&dir);
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
                    SERIES_FLAG_FILE_LINE, // flags
                    TS_STRING // types
                )
            );
        }
        return R_OUT; }

    case SYM_CREATE: {
        if (IS_BLOCK(state))
            fail (Error_Already_Open_Raw(path));
    create:
        Init_Dir_Path(
            &dir, path, 0, POL_WRITE | REMOVE_TAIL_SLASH
        ); // Sets RFM_DIR too

        REBINT result = OS_DO_DEVICE(&dir.devreq, RDC_CREATE);

        Cleanup_Dir_Path(&dir);
        if (result < 0)
            fail (Error_No_Create_Raw(path));
        if (action == SYM_CREATE) {
            Move_Value(D_OUT, D_ARG(1));
            return R_OUT;
        }
        Init_Blank(state);
        goto return_port; }

    case SYM_RENAME: {
        INCLUDE_PARAMS_OF_RENAME;

        if (IS_BLOCK(state))
            fail (Error_Already_Open_Raw(path));

        // Sets RFM_DIR too
        //
        Init_Dir_Path(&dir, path, 0, POL_WRITE | REMOVE_TAIL_SLASH);

        UNUSED(ARG(from)); // implicit
        dir.devreq.common.data = cast(REBYTE*, ARG(to)); // !!! hack!
        OS_DO_DEVICE(&dir.devreq, RDC_RENAME);

        Cleanup_Dir_Path(&dir);

        if (dir.devreq.error)
            fail (Error_No_Rename_Raw(path));
        goto return_port; }

    case SYM_DELETE: {
        //Trap_Security(flags[POL_WRITE], POL_WRITE, path);

        Init_Blank(state);
        Init_Dir_Path(&dir, path, 0, POL_WRITE);

        // !!! add *.r deletion
        // !!! add recursive delete (?)
        REBINT result = OS_DO_DEVICE(&dir.devreq, RDC_DELETE);

        Cleanup_Dir_Path(&dir);

        if (result < 0)
            fail (Error_No_Delete_Raw(path));

        goto return_port; }

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

        Init_Dir_Path(&dir, path, 1, POL_READ);
        Init_Block(state, Read_Dir_May_Fail(&dir));
        Cleanup_Dir_Path(&dir);
        goto return_port; }

    case SYM_CLOSE:
        Init_Blank(state);
        goto return_port;

    case SYM_QUERY: {
        //Trap_Security(flags[POL_READ], POL_READ, path);
        Init_Blank(state);
        Init_Dir_Path(&dir, path, -1, REMOVE_TAIL_SLASH | POL_READ);
        int query_result = OS_DO_DEVICE(&dir.devreq, RDC_QUERY);

        if (query_result < 0) {
            Cleanup_Dir_Path(&dir);
            return R_BLANK;
        }

        Ret_Query_File(port, &dir, D_OUT);
        Cleanup_Dir_Path(&dir);
        return R_OUT; }

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_PORT, action));

return_port:
    Move_Value(D_OUT, D_ARG(1));
    return R_OUT;
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
    return R_OUT;
}
