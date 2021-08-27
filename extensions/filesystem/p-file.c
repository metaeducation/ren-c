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

#define MAX_READ_MASK 0x7FFFFFFF  // max size per chunk

enum act_open_mask {
    AM_OPEN_NEW = 1 << 0,
    AM_OPEN_READ = 1 << 1,
    AM_OPEN_WRITE = 1 << 2,
    AM_OPEN_SEEK = 1 << 3,
    AM_OPEN_ALLOW = 1 << 4
};


extern REBVAL *Open_File(FILEREQ *file);
extern REBVAL *Close_File(FILEREQ *file);
extern REBVAL *Read_File(size_t *actual, FILEREQ *file, REBYTE *data, size_t length);
extern REBVAL *Write_File(FILEREQ *file, const REBYTE *data, size_t length);
extern REBVAL *Query_File(FILEREQ *file);
extern REBVAL *Create_File(FILEREQ *file);
extern REBVAL *Delete_File(FILEREQ *file);
extern REBVAL *Rename_File(FILEREQ *file, const REBVAL *to);


//
//  Setup_File: C
//
// Convert native action refinements to file modes.
//
static void Setup_File(FILEREQ *file, REBFLGS flags, REBVAL *path)
{
    if (flags & AM_OPEN_WRITE)
        file->modes |= RFM_WRITE;
    if (flags & AM_OPEN_READ)
        file->modes |= RFM_READ;
    if (flags & AM_OPEN_SEEK)
        file->modes |= RFM_SEEK;

    if (flags & AM_OPEN_NEW) {
        file->modes |= RFM_NEW;
        if (not (flags & AM_OPEN_WRITE))
            fail (Error_Bad_File_Mode_Raw(path));
    }

    file->path = path;

    // !!! For the moment, assume `path` has a lifetime that will exceed
    // the operation.  This will be easier to ensure once the REQ state is
    // Rebol-structured data, visible to the GC.
}


//
//  Query_File_Or_Dir: C
//
// Produces a STD_FILE_INFO object.
//
REBVAL *Query_File_Or_Dir(const REBVAL *port, FILEREQ *file)
{
    assert(IS_FILE(file->path));

    REBVAL *timestamp = File_Time_To_Rebol(file);

    return rebValue(
        "make ensure object! (", port , ")/scheme/info [",
            "name:", file->path,
            "size:", rebI(file->size),
            "type:", (file->modes & RFM_DIR) ? "'dir" : "'file",
            "date:", rebR(timestamp),
        "]"
    );
}


//
//  Open_File_Port: C
//
// Open a file port.
//
static void Open_File_Port(
    REBVAL *port,
    FILEREQ *file,
    REBVAL *path
){
    UNUSED(port);

    if (file->modes & RFM_OPEN)
        fail (Error_Already_Open_Raw(path));

    // Don't use OS_DO_DEVICE_SYNC() here, because we want to tack the file
    // name onto any error we get back.

    REBVAL *error = Open_File(file);
    if (error)
        fail (Error_Cannot_Open_Raw(file->path, error));

    file->modes |= RFM_OPEN;  // open it
}


//
//  Read_File_Port: C
//
// Common function that was used by both READ and COPY actions on FILE! port.
//
static void Read_File_Port(
    REBVAL *out,
    FILEREQ *file,
    REBLEN len
){
    // Make buffer for read result
    //
    REBBIN *bin = Make_Binary(len);
    TERM_BIN_LEN(bin, len);
    Init_Binary(out, bin);

    // Do the read, check for errors
    //
    size_t actual;

    REBVAL *error = Read_File(&actual, file, BIN_HEAD(bin), len);
    if (error)
        fail (error);

    TERM_BIN_LEN(bin, actual);
}


//
//  Write_File_Port: C
//
// Factored out from FILE! port for WRITE, but only called once.
//
// !!! `len` comes from /PART, it should be in characters if a string and
// in bytes if a BINARY!.  It seems to disregard it if the data is BLOCK!
//
static void Write_File_Port(
    FILEREQ *file,
    REBVAL *data,
    REBLEN limit,
    bool lines
){
    if (IS_BLOCK(data)) {
        // Form the values of the block
        // !! Could be made more efficient if we broke the FORM
        // into 32K chunks for writing.
        DECLARE_MOLD (mo);
        Push_Mold(mo);
        if (lines)
            SET_MOLD_FLAG(mo, MOLD_FLAG_LINES);
        Form_Value(mo, data);
        Init_Text(data, Pop_Molded_String(mo)); // fall to next section
        limit = VAL_LEN_HEAD(data);
    }

    const REBYTE *data_ptr;
    size_t length;

    if (IS_TEXT(data)) {
        REBSIZ size;
        REBCHR(const*) utf8 = VAL_UTF8_LEN_SIZE_AT_LIMIT(
            nullptr,
            &size,
            data,
            limit
        );

        data_ptr = utf8;
        length = size;

        file->modes |= RFM_TEXT;  // do LF => CR LF, e.g. on Windows
    }
    else {
        data_ptr = m_cast(REBYTE*, VAL_BINARY_AT(data));
        length = limit;
        file->modes &= ~RFM_TEXT;  // don't do LF => CR LF, e.g. on Windows
    }

    REBVAL *error = Write_File(file, data_ptr, length);
    if (error)
        fail (error);
}


//
//  Set_Length: C
//
// Note: converts 64bit number to 32bit. The requested size
// can never be greater than 4GB.  If limit isn't negative it
// constrains the size of the requested read.
//
static REBLEN Set_Length(FILEREQ *file, REBI64 limit)
{
    REBI64 len = file->size - file->index;  // how much is already used

    // Compute and bound bytes remaining

    if (len < 0)
        return 0;
    len &= MAX_READ_MASK;  // limit the size

    if (limit < 0)  // return requested length
        return cast(REBLEN, len);

    if (limit > len)  // limit size of requested read
        return cast(REBLEN, len);

    return cast(REBLEN, limit);
}


//
//  Set_Seek: C
//
// Computes the number of bytes that should be skipped.
//
static void Set_Seek(FILEREQ *file, REBVAL *arg)
{
    REBI64 i = Int64s(arg, 0);

    if (i > file->size)
        i = file->size;

    file->index = i;

    file->modes |= RFM_RESEEK;  // force a seek
}


//
//  File_Actor: C
//
// Internal port handler for files.
//
REB_R File_Actor(REBFRM *frame_, REBVAL *port, const REBVAL *verb)
{
    REBCTX *ctx = VAL_CONTEXT(port);
    REBVAL *spec = CTX_VAR(ctx, STD_PORT_SPEC);
    if (!IS_OBJECT(spec))
        fail (Error_Invalid_Spec_Raw(spec));

    REBVAL *path = Obj_Value(spec, STD_PORT_SPEC_HEAD_REF);
    if (path == NULL)
        fail (Error_Invalid_Spec_Raw(spec));

    if (IS_URL(path))
        path = Obj_Value(spec, STD_PORT_SPEC_HEAD_PATH);
    else if (!IS_FILE(path))
        fail (Error_Invalid_Spec_Raw(path));

    FILEREQ *file;

    REBVAL *state = CTX_VAR(ctx, STD_PORT_STATE);
    if (IS_NULLED(state)) {
        //
        // !!! The Make_Devreq() code would zero out the struct, so to keep
        // things compatible while ripping out the devreq code this must too.
        //
        REBBIN *bin = Make_Binary(sizeof(FILEREQ));
        memset(BIN_HEAD(bin), 0, sizeof(FILEREQ));
        Init_Binary(state, bin);
        file = cast(FILEREQ*, VAL_BINARY_AT_ENSURE_MUTABLE(state));
    }
    else {
        file = cast(FILEREQ*, VAL_BINARY_AT_ENSURE_MUTABLE(state));
    }

    // !!! R3-Alpha never implemented quite a number of operations on files,
    // including FLUSH, POKE, etc.

    switch (VAL_WORD_ID(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // implicitly comes from `port`
        SYMID property = VAL_WORD_ID(ARG(property));
        assert(property != SYM_0);

        switch (property) {
          case SYM_INDEX:
            return Init_Integer(D_OUT, file->index + 1);

          case SYM_LENGTH:
            //
            // Comment said "clip at zero"
            ///
            return Init_Integer(D_OUT, file->size - file->index);

          case SYM_HEAD:
            file->index = 0;
            file->modes |= RFM_RESEEK;
            RETURN (port);

          case SYM_TAIL:
            file->index = file->size;
            file->modes |= RFM_RESEEK;
            RETURN (port);

          case SYM_HEAD_Q:
            return Init_Logic(D_OUT, file->index == 0);

          case SYM_TAIL_Q:
            return Init_Logic(D_OUT, file->index >= file->size);

          case SYM_PAST_Q:
            return Init_Logic(D_OUT, file->index > file->size);

          case SYM_OPEN_Q:
            return Init_Logic(D_OUT, did (file->modes & RFM_OPEN));

          default:
            break;
        }

        break; }

      case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PAR(source));
        UNUSED(PAR(string)); // handled in dispatcher
        UNUSED(PAR(lines)); // handled in dispatcher

        // Handle the READ %file shortcut case, where the FILE! has been
        // converted into a PORT! but has not been opened yet.

        bool opened;
        if (file->modes & RFM_OPEN)
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
            Set_Seek(file, ARG(seek));

        REBLEN len = Set_Length(file, REF(part) ? VAL_INT64(ARG(part)) : -1);
        Read_File_Port(D_OUT, file, len);

        if (opened) {
            REBVAL *error = Close_File(file);
            file->modes &= ~RFM_OPEN;
            if (error)
                fail (error);
        }

        return D_OUT; }

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

        if (REF(allow))
            fail (Error_Bad_Refines_Raw());

        REBVAL *data = ARG(data); // binary, string, or block

        // Handle the WRITE %file shortcut case, where the FILE! is converted
        // to a PORT! but it hasn't been opened yet.

        bool opened;
        if (file->modes & RFM_OPEN) {
            if (not (file->modes & RFM_WRITE))
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
            file->index = -1;  // append
            file->modes |= RFM_RESEEK;
        }
        if (REF(seek))
            Set_Seek(file, ARG(seek));

        // Determine length. Clip /PART to size of string if needed.
        REBLEN len = VAL_LEN_AT(data);
        if (REF(part)) {
            REBLEN n = Int32s(ARG(part), 0);
            if (n <= len)
                len = n;
        }

        Write_File_Port(file, data, len, did REF(lines));

        if (opened) {
            REBVAL *error = Close_File(file);
            file->modes &= ~RFM_OPEN;
            if (error)
                fail (error);
        }

        RETURN (port); }

      case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;

        UNUSED(PAR(spec));

        if (REF(allow))
            fail (Error_Bad_Refines_Raw());

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

        if (REF(deep) or REF(types))
            fail (Error_Bad_Refines_Raw());

        if (not (file->modes & RFM_OPEN))
            fail (Error_Not_Open_Raw(path)); // !!! wrong msg

        REBLEN len = Set_Length(file, REF(part) ? VAL_INT64(ARG(part)) : -1);
        Read_File_Port(D_OUT, file, len);
        return D_OUT; }

      case SYM_CLOSE: {
        INCLUDE_PARAMS_OF_CLOSE;
        UNUSED(PAR(port));

        if (file->modes & RFM_OPEN) {
            REBVAL *error = Close_File(file);
            file->modes &= ~RFM_OPEN;
            if (error)
                fail (error);
        }
        RETURN (port); }

      case SYM_DELETE: {
        INCLUDE_PARAMS_OF_DELETE;
        UNUSED(PAR(port));

        if (file->modes & RFM_OPEN)
            fail (Error_No_Delete_Raw(path));
        Setup_File(file, 0, path);

        REBVAL *error = Delete_File(file);
        if (error)
            fail (error);

        RETURN (port); }

      case SYM_RENAME: {
        INCLUDE_PARAMS_OF_RENAME;

        if (file->modes & RFM_OPEN)
            fail (Error_No_Rename_Raw(path));

        Setup_File(file, 0, path);

        REBVAL *error = Rename_File(file, ARG(to));
        if (error)
            fail (error);

        RETURN (ARG(from)); }

      case SYM_CREATE: {
        if (not (file->modes & RFM_OPEN)) {
            Setup_File(file, AM_OPEN_WRITE | AM_OPEN_NEW, path);

            REBVAL *create_error = Create_File(file);
            if (create_error)
                fail (create_error);

            REBVAL *close_error = Close_File(file);
            if (close_error)
                fail (close_error);
        }

        // !!! should it leave file open???

        RETURN (port); }

      case SYM_QUERY: {
        INCLUDE_PARAMS_OF_QUERY;

        UNUSED(PAR(target));

        if (REF(mode))
            fail (Error_Bad_Refines_Raw());

        if (not (file->modes & RFM_OPEN)) {
            Setup_File(file, 0, path);
            REBVAL *error = Query_File(file);
            if (error)
                fail (error);
        }

        // !!! free file path?

        return Query_File_Or_Dir(port, file); }

      case SYM_SKIP: {
        INCLUDE_PARAMS_OF_SKIP;

        UNUSED(PAR(series));
        UNUSED(REF(unbounded));  // !!! Should /UNBOUNDED behave differently?

        file->index += Get_Num_From_Arg(ARG(offset));
        file->modes |= RFM_RESEEK;
        RETURN (port); }

      case SYM_CLEAR: {
        // !! check for write enabled?
        file->modes |= RFM_RESEEK;
        file->modes |= RFM_TRUNCATE;

        Write_File(file, nullptr, 0);
        RETURN (port); }

      default:
        break;
    }

    return R_UNHANDLED;
}
