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
//  Read_Dir: C
//
// Provide option to get file info too.
// Provide option to prepend dir path.
// Provide option to use wildcards.
//
static int Read_Dir(struct devreq_file *dir, REBARR *files)
{
    REBINT result;
    REBCNT len;
    REBSER *fname;
    REBSER *name;
    struct devreq_file file;
    REBREQ *req = AS_REBREQ(dir);

    TERM_ARRAY_LEN(files, 0);
    CLEARS(&file);

    // Temporary filename storage; native OS API character size (REBCHR) varies
    //
    fname = Make_Series(MAX_FILE_NAME, sizeof(REBCHR), MKS_NONE);
    file.path = SER_HEAD(REBCHR, fname);

    SET_FLAG(req->modes, RFM_DIR);

    req->common.data = cast(REBYTE*, &file);

    while (
        (result = OS_DO_DEVICE(req, RDC_READ)) == 0
        && !GET_FLAG(req->flags, RRF_DONE)
    ) {
        len = OS_STRLEN(file.path);
        if (GET_FLAG(file.devreq.modes, RFM_DIR)) len++;
        name = Copy_OS_Str(file.path, len);
        if (GET_FLAG(file.devreq.modes, RFM_DIR))
            SET_ANY_CHAR(name, SER_LEN(name) - 1, '/');
        Init_File(Alloc_Tail_Array(files), name);
    }

    if (result < 0 && req->error != -RFE_OPEN_FAIL
        && (
            OS_STRCHR(dir->path, '*')
            || OS_STRCHR(dir->path, '?')
        )
    ) {
        result = 0;  // no matches found, but not an error
    }

    Free_Series(fname);

    return result;
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
static void Init_Dir_Path(struct devreq_file *dir, REBVAL *path, REBINT wild, REBCNT policy)
{
    REBINT len;
    REBSER *ser;
    //REBYTE *flags;
    REBREQ *req = AS_REBREQ(dir);

    SET_FLAG(req->modes, RFM_DIR);

    // We depend on To_Local_Path giving us 2 extra chars for / and *
    ser = Value_To_OS_Path(path, TRUE);
    len = SER_LEN(ser);
    dir->path = SER_HEAD(REBCHR, ser);

    Secure_Port(SYM_FILE, req, path, ser);

    if (len == 1 && OS_CH_EQUAL(dir->path[0], '.')) {
        if (wild > 0) {
            dir->path[0] = OS_MAKE_CH('*');
            dir->path[1] = OS_MAKE_CH('\0');
        }
    }
    else if (
        len == 2
        && OS_CH_EQUAL(dir->path[0], '.')
        && OS_CH_EQUAL(dir->path[1], '.')
    ) {
        // Insert * if needed:
        if (wild > 0) {
            dir->path[len++] = OS_MAKE_CH('/');
            dir->path[len++] = OS_MAKE_CH('*');
            dir->path[len] = OS_MAKE_CH('\0');
        }
    }
    else if (
        OS_CH_EQUAL(dir->path[len-1], '/')
        || OS_CH_EQUAL(dir->path[len-1], '\\')
    ) {
        if ((policy & REMOVE_TAIL_SLASH) && len > 1) {
            dir->path[len-1] = OS_MAKE_CH('\0');
        }
        else {
            // Insert * if needed:
            if (wild > 0) {
                dir->path[len++] = OS_MAKE_CH('*');
                dir->path[len] = OS_MAKE_CH('\0');
            }
        }
    } else {
        // Path did not end with /, so we better be wild:
        if (wild == 0) {
            // !!! Comment said `OS_FREE(dir->path);` (needed?)
            fail (Error_Bad_File_Path_Raw(path));
        }
        else if (wild < 0) {
            dir->path[len++] = OS_MAKE_CH(OS_DIR_SEP);
            dir->path[len] = OS_MAKE_CH('\0');
        }
    }
}


//
//  Dir_Actor: C
//
// Internal port handler for file directories.
//
static REB_R Dir_Actor(REBFRM *frame_, REBCTX *port, REBSYM action)
{
    REBVAL *spec;
    REBVAL *path;
    REBVAL *state;
    struct devreq_file dir;
    REBINT result;
    REBCNT len;
    //REBYTE *flags;

    Move_Value(D_OUT, D_ARG(1));
    CLEARS(&dir);

    // Validate and fetch relevant PORT fields:
    spec = CTX_VAR(port, STD_PORT_SPEC);
    if (!IS_OBJECT(spec)) fail (Error_Invalid_Spec_Raw(spec));
    path = Obj_Value(spec, STD_PORT_SPEC_HEAD_REF);
    if (!path) fail (Error_Invalid_Spec_Raw(spec));

    if (IS_URL(path)) path = Obj_Value(spec, STD_PORT_SPEC_HEAD_PATH);
    else if (!IS_FILE(path)) fail (Error_Invalid_Spec_Raw(path));

    state = CTX_VAR(port, STD_PORT_STATE); // if block, then port is open.

    //flags = Security_Policy(SYM_FILE, path);

    // Get or setup internal state data:
    dir.devreq.port = port;
    dir.devreq.device = RDI_FILE;

    switch (action) {

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
            Init_Block(state, Make_Array(7)); // initial guess
            result = Read_Dir(&dir, VAL_ARRAY(state));
            if (result < 0)
                fail (Error_On_Port(RE_CANNOT_OPEN, port, dir.devreq.error));
            Move_Value(D_OUT, state);
            SET_BLANK(state);
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
                    FALSE, // !deep
                    TS_STRING // types
                )
            );
        }
        break; }

    case SYM_CREATE:
        if (IS_BLOCK(state))
            fail (Error_Already_Open_Raw(path));
    create:
        Init_Dir_Path(
            &dir, path, 0, POL_WRITE | REMOVE_TAIL_SLASH
        ); // Sets RFM_DIR too
        result = OS_DO_DEVICE(&dir.devreq, RDC_CREATE);
        if (result < 0)
            fail (Error_No_Create_Raw(path));
        if (action == SYM_CREATE) {
            Move_Value(D_OUT, D_ARG(1));
            return R_OUT;
        }
        SET_BLANK(state);
        break;

    case SYM_RENAME:
        if (IS_BLOCK(state)) fail (Error_Already_Open_Raw(path));
        else {
            REBSER *target;

            Init_Dir_Path(&dir, path, 0, POL_WRITE | REMOVE_TAIL_SLASH); // Sets RFM_DIR too
            // Convert file name to OS format:
            if (!(target = Value_To_OS_Path(D_ARG(2), TRUE)))
                fail (Error_Bad_File_Path_Raw(D_ARG(2)));
            dir.devreq.common.data = BIN_HEAD(target);
            OS_DO_DEVICE(&dir.devreq, RDC_RENAME);
            Free_Series(target);
            if (dir.devreq.error) fail (Error_No_Rename_Raw(path));
        }
        break;

    case SYM_DELETE:
        //Trap_Security(flags[POL_WRITE], POL_WRITE, path);
        SET_BLANK(state);
        Init_Dir_Path(&dir, path, 0, POL_WRITE);
        // !!! add *.r deletion
        // !!! add recursive delete (?)
        result = OS_DO_DEVICE(&dir.devreq, RDC_DELETE);
        ///OS_FREE(dir.file.path);
        if (result < 0) fail (Error_No_Delete_Raw(path));
        // !!! Returned D_ARG(2) before, but there is no second argument :-/
        Move_Value(D_OUT, D_ARG(1));
        return R_OUT;

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

        Init_Block(state, Make_Array(7));
        Init_Dir_Path(&dir, path, 1, POL_READ);
        result = Read_Dir(&dir, VAL_ARRAY(state));

        if (result < 0)
            fail (Error_On_Port(RE_CANNOT_OPEN, port, dir.devreq.error));
        break; }

    case SYM_OPEN_Q:
        if (IS_BLOCK(state)) return R_TRUE;
        return R_FALSE;

    case SYM_CLOSE:
        SET_BLANK(state);
        break;

    case SYM_QUERY:
        //Trap_Security(flags[POL_READ], POL_READ, path);
        SET_BLANK(state);
        Init_Dir_Path(&dir, path, -1, REMOVE_TAIL_SLASH | POL_READ);
        if (OS_DO_DEVICE(&dir.devreq, RDC_QUERY) < 0) return R_BLANK;
        Ret_Query_File(port, &dir, D_OUT);
        ///OS_FREE(dir.file.path);
        break;

    //-- Port Series Actions (only called if opened as a port)

    case SYM_LENGTH:
        len = IS_BLOCK(state) ? VAL_ARRAY_LEN_AT(state) : 0;
        SET_INTEGER(D_OUT, len);
        break;

    default:
        fail (Error_Illegal_Action(REB_PORT, action));
    }

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
