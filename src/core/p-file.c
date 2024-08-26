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
#define AS_FILE(s) cast(REBREQ*, Cell_Binary_Head(s))
#define READ_MAX ((REBLEN)(-1))
#define HL64(v) (v##l + (v##h << 32))
#define MAX_READ_MASK 0x7FFFFFFF // max size per chunk


//
//  Setup_File: C
//
// Convert native action refinements to file modes.
//
static void Setup_File(struct devreq_file *file, REBFLGS flags, Value* path)
{
    REBREQ *req = AS_REBREQ(file);

    if (flags & AM_OPEN_WRITE)
        req->modes |= RFM_WRITE;
    if (flags & AM_OPEN_READ)
        req->modes |= RFM_READ;
    if (flags & AM_OPEN_SEEK)
        req->modes |= RFM_SEEK;

    if (flags & AM_OPEN_NEW) {
        req->modes |= RFM_NEW;
        if (not (flags & AM_OPEN_WRITE))
            fail (Error_Bad_File_Mode_Raw(path));
    }

    file->path = path;

    // !!! For the moment, assume `path` has a lifetime that will exceed
    // the operation.  This will be easier to ensure once the REQ state is
    // Rebol-structured data, visible to the GC.
}


//
//  Cleanup_File: C
//
static void Cleanup_File(struct devreq_file *file)
{
    REBREQ *req = AS_REBREQ(file);
    req->flags &= ~RRF_OPEN;
}


//
//  Query_File_Or_Dir: C
//
// Produces a STD_FILE_INFO object.
//
void Query_File_Or_Dir(Value* out, Value* port, struct devreq_file *file)
{
    REBREQ *req = AS_REBREQ(file);

    Value* info = rebValue(
        "copy ensure object! (", port , ")/scheme/info"
    ); // shallow copy

    REBCTX *ctx = VAL_CONTEXT(info);

    Init_Word(
        CTX_VAR(ctx, STD_FILE_INFO_TYPE),
        (req->modes & RFM_DIR) ? Canon(SYM_DIR) : Canon(SYM_FILE)
    );
    Init_Integer(CTX_VAR(ctx, STD_FILE_INFO_SIZE), file->size);

    Value* timestamp = OS_FILE_TIME(file);
    Copy_Cell(CTX_VAR(ctx, STD_FILE_INFO_DATE), timestamp);
    rebRelease(timestamp);

    assert(Is_File(file->path));
    Copy_Cell(CTX_VAR(ctx, STD_FILE_INFO_NAME), file->path);

    Copy_Cell(out, info);
    rebRelease(info);
}


//
//  Open_File_Port: C
//
// Open a file port.
//
static void Open_File_Port(
    Value* port,
    struct devreq_file *file,
    Value* path
){
    UNUSED(port);

    REBREQ *req = AS_REBREQ(file);
    if (req->flags & RRF_OPEN)
        fail (Error_Already_Open_Raw(path));

    OS_DO_DEVICE_SYNC(req, RDC_OPEN);

    req->flags |= RRF_OPEN; // open it
}


//
//  Read_File_Port: C
//
// Read from a file port.
//
static void Read_File_Port(
    Value* out,
    Value* port,
    struct devreq_file *file,
    Value* path,
    REBFLGS flags,
    REBLEN len
) {
    assert(Is_File(path));

    UNUSED(path);
    UNUSED(flags);
    UNUSED(port);

    REBREQ *req = AS_REBREQ(file);

    Blob* flex = Make_Blob(len);  // read result buffer
    Init_Binary(out, flex);

    // Do the read, check for errors:
    req->common.data = Blob_Head(flex);
    req->length = len;

    OS_DO_DEVICE_SYNC(req, RDC_READ);

    Set_Flex_Len(flex, req->actual);
    Term_Non_Array_Flex(flex);
}


//
//  Write_File_Port: C
//
static void Write_File_Port(struct devreq_file *file, Value* data, REBLEN len, bool lines)
{
    Blob* bin;
    REBREQ *req = AS_REBREQ(file);

    if (Is_Block(data)) {
        // Form the values of the block
        // !! Could be made more efficient if we broke the FORM
        // into 32K chunks for writing.
        DECLARE_MOLD (mo);
        Push_Mold(mo);
        if (lines)
            SET_MOLD_FLAG(mo, MOLD_FLAG_LINES);
        Form_Value(mo, data);
        Init_Text(data, Pop_Molded_String(mo)); // fall to next section
        len = VAL_LEN_HEAD(data);
    }

    if (Is_Text(data)) {
        bin = Make_Utf8_From_Cell_String_At_Limit(data, len);
        Manage_Flex(bin);
        req->common.data = Blob_Head(bin);
        len = Flex_Len(bin);
        req->modes |= RFM_TEXT; // do LF => CR LF, e.g. on Windows
    }
    else {
        req->common.data = Cell_Binary_At(data);
        req->modes &= ~RFM_TEXT; // don't do LF => CR LF, e.g. on Windows
    }
    req->length = len;

    OS_DO_DEVICE_SYNC(req, RDC_WRITE);
}


//
//  Set_Length: C
//
// Note: converts 64bit number to 32bit. The requested size
// can never be greater than 4GB.  If limit isn't negative it
// constrains the size of the requested read.
//
static REBLEN Set_Length(const struct devreq_file *file, REBI64 limit)
{
    REBI64 len;

    // Compute and bound bytes remaining:
    len = file->size - file->index; // already read
    if (len < 0) return 0;
    len &= MAX_READ_MASK; // limit the size

    // Return requested length:
    if (limit < 0) return (REBLEN)len;

    // Limit size of requested read:
    if (limit > len) return cast(REBLEN, len);
    return cast(REBLEN, limit);
}


//
//  Set_Seek: C
//
// Computes the number of bytes that should be skipped.
//
static void Set_Seek(struct devreq_file *file, Value* arg)
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
static REB_R File_Actor(Level* level_, Value* port, Value* verb)
{
    REBCTX *ctx = VAL_CONTEXT(port);
    Value* spec = CTX_VAR(ctx, STD_PORT_SPEC);
    if (!Is_Object(spec))
        fail (Error_Invalid_Spec_Raw(spec));

    Value* path = Obj_Value(spec, STD_PORT_SPEC_HEAD_REF);
    if (path == nullptr)
        fail (Error_Invalid_Spec_Raw(spec));

    if (Is_Url(path))
        path = Obj_Value(spec, STD_PORT_SPEC_HEAD_PATH);
    else if (!Is_File(path))
        fail (Error_Invalid_Spec_Raw(path));

    REBREQ *req = Ensure_Port_State(port, RDI_FILE);
    struct devreq_file *file = DEVREQ_FILE(req);

    // !!! R3-Alpha never implemented quite a number of operations on files,
    // including FLUSH, POKE, etc.

    switch (Cell_Word_Id(verb)) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // implicitly comes from `port`
        Option(SymId) property = Cell_Word_Id(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_INDEX:
            return Init_Integer(OUT, file->index + 1);;

        case SYM_LENGTH:
            //
            // Comment said "clip at zero"
            ///
            return Init_Integer(OUT, file->size - file->index);;

        case SYM_HEAD:
            file->index = 0;
            req->modes |= RFM_RESEEK;
            RETURN (port);

        case SYM_TAIL:
            file->index = file->size;
            req->modes |= RFM_RESEEK;
            RETURN (port);

        case SYM_HEAD_Q:
            return Init_Logic(OUT, file->index == 0);

        case SYM_TAIL_Q:
            return Init_Logic(OUT, file->index >= file->size);

        case SYM_PAST_Q:
            return Init_Logic(OUT, file->index > file->size);

        case SYM_OPEN_Q:
            return Init_Logic(OUT, did (req->flags & RRF_OPEN));

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

        bool opened;
        if (req->flags & RRF_OPEN)
            opened = false; // was already open
        else {
            REBLEN nargs = AM_OPEN_READ;
            if (REF(seek))
                nargs |= AM_OPEN_SEEK;
            Setup_File(file, nargs, path);
            Open_File_Port(port, file, path);
            opened = true; // had to be opened (shortcut case)
        }

        if (REF(seek))
            Set_Seek(file, ARG(index));

        REBLEN len = Set_Length(file, REF(part) ? VAL_INT64(ARG(limit)) : -1);
        Read_File_Port(OUT, port, file, path, flags, len);

        if (opened) {
            Value* result = OS_DO_DEVICE(req, RDC_CLOSE);
            assert(result != nullptr);  // should be synchronous

            Cleanup_File(file);

            if (rebDid("error?", result))
                rebJumps("FAIL", result);

            rebRelease(result); // ignore result
        }

        return OUT; }

    case SYM_APPEND:
        //
        // !!! This is hacky, but less hacky than falling through to SYM_WRITE
        // assuming the frame is the same for APPEND and WRITE (which is what
        // R3-Alpha did).  Review.
        //
        return Retrigger_Append_As_Write(level_);

    case SYM_WRITE: {
        INCLUDE_PARAMS_OF_WRITE;

        UNUSED(PAR(destination));

        if (REF(allow)) {
            UNUSED(ARG(access));
            fail (Error_Bad_Refines_Raw());
        }

        Value* data = ARG(data); // binary, string, or block

        // Handle the WRITE %file shortcut case, where the FILE! is converted
        // to a PORT! but it hasn't been opened yet.

        bool opened;
        if (req->flags & RRF_OPEN) {
            if (not (req->modes & RFM_WRITE))
                fail (Error_Read_Only_Raw(path));

            opened = false; // already open
        }
        else {
            REBLEN nargs = AM_OPEN_WRITE;
            if (REF(seek) || REF(append))
                nargs |= AM_OPEN_SEEK;
            else
                nargs |= AM_OPEN_NEW;
            Setup_File(file, nargs, path);
            Open_File_Port(port, file, path);
            opened = true;
        }

        if (REF(append)) {
            file->index = -1; // append
            req->modes |= RFM_RESEEK;
        }
        if (REF(seek))
            Set_Seek(file, ARG(index));

        // Determine length. Clip /PART to size of string if needed.
        REBLEN len = VAL_LEN_AT(data);
        if (REF(part)) {
            REBLEN n = Int32s(ARG(limit), 0);
            if (n <= len) len = n;
        }

        Write_File_Port(file, data, len, REF(lines));

        if (opened) {
            Value* result = OS_DO_DEVICE(req, RDC_CLOSE);
            assert(result != nullptr);  // should be synchronous

            Cleanup_File(file);

            if (rebDid("error?", result))
                rebJumps("FAIL", result);

            rebRelease(result);
        }

        RETURN (port); }

    case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;

        UNUSED(PAR(spec));
        if (REF(allow)) {
            UNUSED(ARG(access));
            fail (Error_Bad_Refines_Raw());
        }

        REBFLGS flags = (
            (REF(new) ? AM_OPEN_NEW : 0)
            | (REF(read) or not REF(write) ? AM_OPEN_READ : 0)
            | (REF(write) or not REF(read) ? AM_OPEN_WRITE : 0)
            | (REF(seek) ? AM_OPEN_SEEK : 0)
            | (REF(allow) ? AM_OPEN_ALLOW : 0)
        );
        Setup_File(file, flags, path);

        // !!! need to change file modes to R/O if necessary

        Open_File_Port(port, file, path);

        RETURN (port); }

    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PAR(value));
        if (REF(deep))
            fail (Error_Bad_Refines_Raw());
        if (REF(types)) {
            UNUSED(ARG(kinds));
            fail (Error_Bad_Refines_Raw());
        }

        if (not (req->flags & RRF_OPEN))
            fail (Error_Not_Open_Raw(path)); // !!! wrong msg

        REBLEN len = Set_Length(file, REF(part) ? VAL_INT64(ARG(limit)) : -1);
        REBFLGS flags = 0;
        Read_File_Port(OUT, port, file, path, flags, len);
        return OUT; }

    case SYM_CLOSE: {
        INCLUDE_PARAMS_OF_CLOSE;
        UNUSED(PAR(port));

        if (req->flags & RRF_OPEN) {
            Value* result = OS_DO_DEVICE(req, RDC_CLOSE);
            assert(result != nullptr);  // should be synchronous

            Cleanup_File(file);

            if (rebDid("error?", result))
                rebJumps("FAIL", result);

            rebRelease(result); // ignore error
        }
        RETURN (port); }

    case SYM_DELETE: {
        INCLUDE_PARAMS_OF_DELETE;
        UNUSED(PAR(port));

        if (req->flags & RRF_OPEN)
            fail (Error_No_Delete_Raw(path));
        Setup_File(file, 0, path);

        Value* result = OS_DO_DEVICE(req, RDC_DELETE);
        assert(result != nullptr);  // should be synchronous

        if (rebDid("error?", result))
            rebJumps("FAIL", result);

        rebRelease(result); // ignore result
        RETURN (port); }

    case SYM_RENAME: {
        INCLUDE_PARAMS_OF_RENAME;

        if (req->flags & RRF_OPEN)
            fail (Error_No_Rename_Raw(path));

        Setup_File(file, 0, path);

        req->common.data = cast(Byte*, ARG(to)); // !!! hack!

        Value* result = OS_DO_DEVICE(req, RDC_RENAME);
        assert(result != nullptr);  // should be synchronous
        if (rebDid("error?", result))
            rebJumps("FAIL", result);
        rebRelease(result); // ignore result

        RETURN (ARG(from)); }

    case SYM_CREATE: {
        if (not (req->flags & RRF_OPEN)) {
            Setup_File(file, AM_OPEN_WRITE | AM_OPEN_NEW, path);

            Value* cr_result = OS_DO_DEVICE(req, RDC_CREATE);
            assert(cr_result != nullptr);
            if (rebDid("error?", cr_result))
                rebJumps("FAIL", cr_result);
            rebRelease(cr_result);

            Value* cl_result = OS_DO_DEVICE(req, RDC_CLOSE);
            assert(cl_result != nullptr);
            if (rebDid("error?", cl_result))
                rebJumps("FAIL", cl_result);
            rebRelease(cl_result);
        }

        // !!! should it leave file open???

        RETURN (port); }

    case SYM_QUERY: {
        INCLUDE_PARAMS_OF_QUERY;

        UNUSED(PAR(target));
        if (REF(mode)) {
            UNUSED(ARG(field));
            fail (Error_Bad_Refines_Raw());
        }

        if (not (req->flags & RRF_OPEN)) {
            Setup_File(file, 0, path);
            Value* result = OS_DO_DEVICE(req, RDC_QUERY);
            assert(result != nullptr);
            if (rebDid("error?", result)) {
                rebRelease(result); // !!! R3-Alpha returned blank on error
                return nullptr;
            }
            rebRelease(result); // ignore result
        }
        Query_File_Or_Dir(OUT, port, file);

        // !!! free file path?

        return OUT; }

    case SYM_MODIFY: {
        INCLUDE_PARAMS_OF_MODIFY;

        UNUSED(PAR(target));
        UNUSED(PAR(field));
        UNUSED(PAR(value));

        // !!! Set_Mode_Value() was called here, but a no-op in R3-Alpha
        if (not (req->flags & RRF_OPEN)) {
            Setup_File(file, 0, path);

            Value* result = OS_DO_DEVICE(req, RDC_MODIFY);
            assert(result != nullptr);
            if (rebDid("error?", result)) {
                rebRelease(result); // !!! R3-Alpha returned blank on error
                return Init_False(OUT);
            }
            rebRelease(result); // ignore result
        }
        return Init_True(OUT); }

    case SYM_SKIP: {
        INCLUDE_PARAMS_OF_SKIP;

        UNUSED(PAR(series));
        UNUSED(REF(only)); // !!! Should /ONLY behave differently?

        file->index += Get_Num_From_Arg(ARG(offset));
        req->modes |= RFM_RESEEK;
        RETURN (port); }

    case SYM_CLEAR: {
        // !! check for write enabled?
        req->modes |= RFM_RESEEK;
        req->modes |= RFM_TRUNCATE;
        req->length = 0;

        OS_DO_DEVICE_SYNC(req, RDC_WRITE);
        RETURN (port); }

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_PORT, verb));
}


//
//  get-file-actor-handle: native [
//
//  {Retrieve handle to the native actor for files}
//
//      return: [handle!]
//  ]
//
DECLARE_NATIVE(get_file_actor_handle)
{
    Make_Port_Actor_Handle(OUT, &File_Actor);
    return OUT;
}
