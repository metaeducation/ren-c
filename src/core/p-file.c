//
//  File: %p-file.c
//  Summary: "file port interface"
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

// For reference to port/state series that holds the file structure:
#define AS_FILE(s) ((REBREQ*)VAL_BIN(s))
#define READ_MAX ((REBCNT)(-1))
#define HL64(v) (v##l + (v##h << 32))
#define MAX_READ_MASK 0x7FFFFFFF // max size per chunk


//
//  Setup_File: C
//
// Convert native action refinements to file modes.
//
static void Setup_File(struct devreq_file *file, REBFLGS flags, REBVAL *path)
{
    REBSER *ser;
    REBREQ *req = AS_REBREQ(file);

    if (flags & AM_OPEN_WRITE)
        req->modes |= RFM_WRITE;
    if (flags & AM_OPEN_READ)
        req->modes |= RFM_READ;
    if (flags & AM_OPEN_SEEK)
        req->modes |= RFM_SEEK;

    if (flags & AM_OPEN_NEW) {
        req->modes |= RFM_NEW;
        if (NOT(flags & AM_OPEN_WRITE))
            fail (Error_Bad_File_Mode_Raw(path));
    }

    if (!(ser = Value_To_OS_Path(path, TRUE)))
        fail (Error_Bad_File_Path_Raw(path));

    // !!! Original comment said "Convert file name to OS format, let
    // it GC later."  Then it grabs the series data from inside of it.
    // It's not clear what lifetime req->file.path is supposed to have,
    // and saying "good until whenever the GC runs" is not rigorous.
    // The series should be kept manual and freed when the data is
    // no longer used, or the managed series saved in a GC-safe place
    // as long as the bytes are needed.
    //
    MANAGE_SERIES(ser);

    file->path = SER_HEAD(REBCHR, ser);

    req->modes |= RFM_NAME_MEM;

    Secure_Port(SYM_FILE, req, path, ser);
}


//
//  Cleanup_File: C
//
static void Cleanup_File(struct devreq_file *file)
{
    REBREQ *req = AS_REBREQ(file);

    if (req->modes & RFM_NAME_MEM) {
        //NOTE: file->path will get GC'd
        file->path = 0;
        req->modes &= ~RFM_NAME_MEM;
    }
    req->flags &= ~RRF_OPEN;
}


//
//  Ret_Query_File: C
//
// Query file and set RET value to resulting STD_FILE_INFO object.
//
void Ret_Query_File(REBCTX *port, struct devreq_file *file, REBVAL *ret)
{
    REBREQ *req = AS_REBREQ(file);

    REBVAL *info = In_Object(port, STD_PORT_SCHEME, STD_SCHEME_INFO, 0);

    if (!info || !IS_OBJECT(info))
        fail (Error_On_Port(RE_INVALID_SPEC, port, -10));

    REBCTX *context = Copy_Context_Shallow(VAL_CONTEXT(info));

    Init_Object(ret, context);
    Init_Word(
        CTX_VAR(context, STD_FILE_INFO_TYPE),
        LOGICAL(req->modes & RFM_DIR) ? Canon(SYM_DIR) : Canon(SYM_FILE)
    );
    Init_Integer(
        CTX_VAR(context, STD_FILE_INFO_SIZE), file->size
    );
    OS_FILE_TIME(CTX_VAR(context, STD_FILE_INFO_DATE), file);

    REBSER *ser = To_REBOL_Path(
        file->path, 0, (OS_WIDE ? PATH_OPT_UNI_SRC : 0)
    );

    Init_File(CTX_VAR(context, STD_FILE_INFO_NAME), ser);
}


//
//  Open_File_Port: C
//
// Open a file port.
//
static void Open_File_Port(REBCTX *port, struct devreq_file *file, REBVAL *path)
{
    REBREQ *req = AS_REBREQ(file);

    if (Is_Port_Open(port))
        fail (Error_Already_Open_Raw(path));

    if (OS_DO_DEVICE(req, RDC_OPEN) < 0)
        fail (Error_On_Port(RE_CANNOT_OPEN, port, req->error));

    Set_Port_Open(port, TRUE);
}


REBINT Mode_Syms[] = {
    SYM_OWNER_READ,
    SYM_OWNER_WRITE,
    SYM_OWNER_EXECUTE,
    SYM_GROUP_READ,
    SYM_GROUP_WRITE,
    SYM_GROUP_EXECUTE,
    SYM_WORLD_READ,
    SYM_WORLD_WRITE,
    SYM_WORLD_EXECUTE,
    0
};


//
//  Read_File_Port: C
//
// Read from a file port.
//
static void Read_File_Port(
    REBVAL *out,
    REBCTX *port,
    struct devreq_file *file,
    REBVAL *path,
    REBFLGS flags,
    REBCNT len
) {
#ifdef NDEBUG
    UNUSED(path);
#else
    assert(IS_FILE(path));
#endif
    UNUSED(flags);

    REBREQ *req = AS_REBREQ(file);

    REBSER *ser = Make_Binary(len); // read result buffer
    Init_Binary(out, ser);

    // Do the read, check for errors:
    req->common.data = BIN_HEAD(ser);
    req->length = len;
    if (OS_DO_DEVICE(req, RDC_READ) < 0)
        fail (Error_On_Port(RE_READ_ERROR, port, req->error));

    SET_SERIES_LEN(ser, req->actual);
    TERM_SEQUENCE(ser);
}


//
//  Write_File_Port: C
//
static void Write_File_Port(struct devreq_file *file, REBVAL *data, REBCNT len, REBOOL lines)
{
    REBSER *ser;
    REBREQ *req = AS_REBREQ(file);

    if (IS_BLOCK(data)) {
        // Form the values of the block
        // !! Could be made more efficient if we broke the FORM
        // into 32K chunks for writing.
        DECLARE_MOLD (mo);
        Push_Mold(mo);
        if (lines)
            SET_MOLD_FLAG(mo, MOLD_FLAG_LINES);
        Form_Value(mo, data);
        Init_String(data, Pop_Molded_String(mo)); // fall to next section
        len = VAL_LEN_HEAD(data);
    }

    // Auto convert string to UTF-8
    if (IS_STRING(data)) {
        ser = Make_UTF8_From_Any_String(data, len, OPT_ENC_CRLF_MAYBE);
        MANAGE_SERIES(ser);
        req->common.data = BIN_HEAD(ser);
        len = SER_LEN(ser);
    }
    else {
        req->common.data = VAL_BIN_AT(data);
    }
    req->length = len;
    OS_DO_DEVICE(req, RDC_WRITE);
}


//
//  Set_Length: C
//
// Note: converts 64bit number to 32bit. The requested size
// can never be greater than 4GB.  If limit isn't negative it
// constrains the size of the requested read.
//
static REBCNT Set_Length(const struct devreq_file *file, REBI64 limit)
{
    REBI64 len;

    // Compute and bound bytes remaining:
    len = file->size - file->index; // already read
    if (len < 0) return 0;
    len &= MAX_READ_MASK; // limit the size

    // Return requested length:
    if (limit < 0) return (REBCNT)len;

    // Limit size of requested read:
    if (limit > len) return cast(REBCNT, len);
    return cast(REBCNT, limit);
}


//
//  Set_Seek: C
//
// Computes the number of bytes that should be skipped.
//
static void Set_Seek(struct devreq_file *file, REBVAL *arg)
{
    REBI64 cnt;
    REBREQ *req = AS_REBREQ(file);

    cnt = Int64s(arg, 0);

    if (cnt > file->size) cnt = file->size;

    file->index = cnt;

    req->modes |= RFM_RESEEK; // force a seek
}


//
//  File_Actor: C
//
// Internal port handler for files.
//
static REB_R File_Actor(REBFRM *frame_, REBCTX *port, REBSYM action)
{
    REBVAL *spec = CTX_VAR(port, STD_PORT_SPEC);
    if (!IS_OBJECT(spec))
        fail (Error_Invalid_Spec_Raw(spec));

    REBVAL *path = Obj_Value(spec, STD_PORT_SPEC_HEAD_REF);
    if (!path)
        fail (Error_Invalid_Spec_Raw(spec));

    if (IS_URL(path))
        path = Obj_Value(spec, STD_PORT_SPEC_HEAD_PATH);
    else if (!IS_FILE(path))
        fail (Error_Invalid_Spec_Raw(path));

    REBREQ *req = Ensure_Port_State(port, RDI_FILE);
    struct devreq_file *file = DEVREQ_FILE(req);

    // !!! R3-Alpha never implemented quite a number of operations on files,
    // including FLUSH, POKE, etc.

    switch (action) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // implicitly comes from `port`
        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_INDEX:
            Init_Integer(D_OUT, file->index + 1);
            return R_OUT;

        case SYM_LENGTH:
            //
            // Comment said "clip at zero"
            ///
            Init_Integer(D_OUT, file->size - file->index);
            return R_OUT;

        case SYM_HEAD:
            file->index = 0;
            req->modes |= RFM_RESEEK;
            Move_Value(D_OUT, CTX_VALUE(port));
            return R_OUT;

        case SYM_TAIL:
            file->index = file->size;
            req->modes |= RFM_RESEEK;
            Move_Value(D_OUT, CTX_VALUE(port));
            return R_OUT;

        case SYM_HEAD_Q:
            return R_FROM_BOOL(LOGICAL(file->index == 0));

        case SYM_TAIL_Q:
            return R_FROM_BOOL(
                LOGICAL(file->index >= file->size)
            );

        case SYM_PAST_Q:
            return R_FROM_BOOL(
                LOGICAL(file->index > file->size)
            );

        case SYM_OPEN_Q:
            return R_FROM_BOOL(LOGICAL(req->flags & RRF_OPEN));

        default:
            break;
        }

        break; }

    case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PAR(source));
        UNUSED(PAR(string)); // handled in dispatcher
        UNUSED(PAR(lines)); // handled in dispatcher

        REBFLGS flags = 0;

        // Handle the READ %file shortcut case, where the FILE! has been
        // converted into a PORT! but has not been opened yet.

        REBOOL opened;
        if (req->flags & RRF_OPEN)
            opened = FALSE; // was already open
        else {
            REBCNT nargs = AM_OPEN_READ;
            if (REF(seek))
                nargs |= AM_OPEN_SEEK;
            Setup_File(file, nargs, path);
            Open_File_Port(port, file, path);
            opened = TRUE; // had to be opened (shortcut case)
        }

        if (REF(seek))
            Set_Seek(file, ARG(index));

        REBCNT len = Set_Length(file, REF(part) ? VAL_INT64(ARG(limit)) : -1);
        Read_File_Port(D_OUT, port, file, path, flags, len);

        if (opened) {
            OS_DO_DEVICE(req, RDC_CLOSE);
            Cleanup_File(file);
        }

        if (req->error)
            fail (Error_On_Port(RE_READ_ERROR, port, req->error));

        return R_OUT; }

    case SYM_APPEND:
        //
        // !!! This is hacky, but less hacky than falling through to SYM_WRITE
        // assuming the frame is the same for APPEND and WRITE (which is what
        // R3-Alpha did).  Review.
        //
        return Retrigger_Append_As_Write(frame_);

    case SYM_WRITE: {
        INCLUDE_PARAMS_OF_WRITE;

        UNUSED(PAR(destination));

        if (REF(allow)) {
            UNUSED(ARG(access));
            fail (Error_Bad_Refines_Raw());
        }

        REBVAL *data = ARG(data); // binary, string, or block

        // Handle the WRITE %file shortcut case, where the FILE! is converted
        // to a PORT! but it hasn't been opened yet.

        REBOOL opened;
        if (req->flags & RRF_OPEN) {
            if (NOT(req->modes & RFM_WRITE))
                fail (Error_Read_Only_Raw(path));

            opened = FALSE; // already open
        }
        else {
            REBCNT nargs = AM_OPEN_WRITE;
            if (REF(seek) || REF(append))
                nargs |= AM_OPEN_SEEK;
            else
                nargs |= AM_OPEN_NEW;
            Setup_File(file, nargs, path);
            Open_File_Port(port, file, path);
            opened = TRUE;
        }

        if (REF(append)) {
            file->index = -1; // append
            req->modes |= RFM_RESEEK;
        }
        if (REF(seek))
            Set_Seek(file, ARG(index));

        // Determine length. Clip /PART to size of string if needed.
        REBCNT len = VAL_LEN_AT(data);
        if (REF(part)) {
            REBCNT n = Int32s(ARG(limit), 0);
            if (n <= len) len = n;
        }

        Write_File_Port(file, data, len, REF(lines));

        if (opened) {
            OS_DO_DEVICE(req, RDC_CLOSE);
            Cleanup_File(file);
        }

        if (req->error) {
            DECLARE_LOCAL(i);
            Init_Integer(i, req->error);
            fail (Error_Write_Error_Raw(path, i));
        }

        Move_Value(D_OUT, CTX_VALUE(port));
        return R_OUT; }

    case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;

        UNUSED(PAR(spec));
        if (REF(allow)) {
            UNUSED(ARG(access));
            fail (Error_Bad_Refines_Raw());
        }

        REBFLGS flags = (
            (REF(new) ? AM_OPEN_NEW : 0)
            | (REF(read) || NOT(REF(write)) ? AM_OPEN_READ : 0)
            | (REF(write) || NOT(REF(read)) ? AM_OPEN_WRITE : 0)
            | (REF(seek) ? AM_OPEN_SEEK : 0)
            | (REF(allow) ? AM_OPEN_ALLOW : 0)
        );
        Setup_File(file, flags, path);

        // !!! need to change file modes to R/O if necessary

        Open_File_Port(port, file, path);

        Move_Value(D_OUT, CTX_VALUE(port));
        return R_OUT; }

    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PAR(value));
        if (REF(deep))
            fail (Error_Bad_Refines_Raw());
        if (REF(types)) {
            UNUSED(ARG(kinds));
            fail (Error_Bad_Refines_Raw());
        }

        if (NOT(req->flags & RRF_OPEN))
            fail (Error_Not_Open_Raw(path)); // !!! wrong msg

        REBCNT len = Set_Length(file, REF(part) ? VAL_INT64(ARG(limit)) : -1);
        REBFLGS flags = 0;
        Read_File_Port(D_OUT, port, file, path, flags, len);
        return R_OUT; }

    case SYM_CLOSE: {
        INCLUDE_PARAMS_OF_CLOSE;
        UNUSED(PAR(port));

        if (req->flags & RRF_OPEN) {
            OS_DO_DEVICE(req, RDC_CLOSE);
            Cleanup_File(file);
        }
        Move_Value(D_OUT, CTX_VALUE(port));
        return R_OUT; }

    case SYM_DELETE: {
        INCLUDE_PARAMS_OF_DELETE;
        UNUSED(PAR(port));

        if (req->flags & RRF_OPEN)
            fail (Error_No_Delete_Raw(path));
        Setup_File(file, 0, path);
        if (OS_DO_DEVICE(req, RDC_DELETE) < 0)
            fail (Error_No_Delete_Raw(path));

        Move_Value(D_OUT, CTX_VALUE(port));
        return R_OUT; }

    case SYM_RENAME: {
        INCLUDE_PARAMS_OF_RENAME;

        if (req->flags & RRF_OPEN)
            fail (Error_No_Rename_Raw(path));

        Setup_File(file, 0, path);

        // Convert file name to OS format:
        //
        REBSER *target = Value_To_OS_Path(ARG(to), TRUE);
        if (target == NULL)
            fail (Error_Bad_File_Path_Raw(ARG(to)));
        req->common.data = BIN_HEAD(target);
        OS_DO_DEVICE(req, RDC_RENAME);
        Free_Series(target);
        if (req->error)
            fail (Error_No_Rename_Raw(path));

        Move_Value(D_OUT, ARG(from));
        return R_OUT; }

    case SYM_CREATE: {
        if (NOT(req->flags & RRF_OPEN)) {
            Setup_File(file, AM_OPEN_WRITE | AM_OPEN_NEW, path);
            if (OS_DO_DEVICE(req, RDC_CREATE) < 0)
                fail (Error_On_Port(RE_CANNOT_OPEN, port, req->error));
            OS_DO_DEVICE(req, RDC_CLOSE);
        }

        // !!! should it leave file open???

        Move_Value(D_OUT, CTX_VALUE(port));
        return R_OUT; }

    case SYM_QUERY: {
        INCLUDE_PARAMS_OF_QUERY;

        UNUSED(PAR(target));
        if (REF(mode)) {
            UNUSED(ARG(field));
            fail (Error_Bad_Refines_Raw());
        }

        if (NOT(req->flags & RRF_OPEN)) {
            Setup_File(file, 0, path);
            if (OS_DO_DEVICE(req, RDC_QUERY) < 0) return R_BLANK;
        }
        Ret_Query_File(port, file, D_OUT);

        // !!! free file path?

        return R_OUT; }

    case SYM_MODIFY: {
        INCLUDE_PARAMS_OF_MODIFY;

        UNUSED(PAR(target));
        UNUSED(PAR(field));
        UNUSED(PAR(value));

        // !!! Set_Mode_Value() was called here, but a no-op in R3-Alpha
        if (NOT(req->flags & RRF_OPEN)) {
            Setup_File(file, 0, path);
            if (OS_DO_DEVICE(req, RDC_MODIFY) < 0) return R_BLANK;
        }
        return R_TRUE; }

    case SYM_SKIP: {
        INCLUDE_PARAMS_OF_SKIP;

        UNUSED(PAR(series));
        UNUSED(REF(only)); // !!! Should /ONLY behave differently?

        file->index += Get_Num_From_Arg(ARG(offset));
        req->modes |= RFM_RESEEK;
        Move_Value(D_OUT, CTX_VALUE(port));
        return R_OUT; }

    case SYM_CLEAR:
        // !! check for write enabled?
        req->modes |= RFM_RESEEK;
        req->modes |= RFM_TRUNCATE;
        req->length = 0;
        if (OS_DO_DEVICE(req, RDC_WRITE) < 0) {
            DECLARE_LOCAL(i);
            Init_Integer(i, req->error);
            fail (Error_Write_Error_Raw(path, i));
        }
        return R_OUT;

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_PORT, action));
}


//
//  get-file-actor-handle: native [
//
//  {Retrieve handle to the native actor for files}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_file_actor_handle)
{
    Make_Port_Actor_Handle(D_OUT, &File_Actor);
    return R_OUT;
}
