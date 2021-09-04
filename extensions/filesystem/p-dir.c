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
// Copyright 2012-2021 Ren-C Open Source Contributors
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
// In R3-Alpha, there was an attempt to have a different "port scheme" and
// "port actor" for directories from files.  So the idea was (presumably) to
// take narrow operations like `make-dir %foo` and fit them into a unified
// pattern where that would be done by something like `create %foo/`.
//
// That is a good example of where it makes for some confusion, because if you
// CREATE a directory like that you presumably don't mean to get a PORT!
// handle back that you have to CLOSE.  But this bubbled over into semantics
// for `create %regular-file.txt`, where it seems you *would* want a port
// back so you could put data in the file you just created...but to be
// consistent with directories it created a 0 byte file and closed it.
//
// For Ren-C the file is being translated to use libuv, but beyond that the
// the semantics of directory operations are in limbo and still need to be
// figured out by some sufficiently-motivated-individual.
//

#include "uv.h"  // includes windows.h
#ifdef TO_WINDOWS
    #undef IS_ERROR  // windows.h defines, contentious with IS_ERROR in Ren-C
#endif

#include "sys-core.h"

#include "file-req.h"


extern REBVAL *Query_File_Or_Directory(const REBVAL *port);
extern REBVAL *Create_Directory(const REBVAL *port);
extern REBVAL *Delete_File_Or_Directory(const REBVAL *port);
extern REBVAL *Rename_File_Or_Directory(const REBVAL *port, const REBVAL *to);

REBVAL *Try_Read_Directory_Entry(FILEREQ *dir);


//
//  Dir_Actor: C
//
// Internal port handler for file directories.
//
REB_R Dir_Actor(REBFRM *frame_, REBVAL *port, const REBSYM *verb)
{
    REBCTX *ctx = VAL_CONTEXT(port);

    REBVAL *state = CTX_VAR(ctx, STD_PORT_STATE);
    FILEREQ *dir;
    if (IS_BINARY(state)) {
        dir = File_Of_Port(port);
    }
    else {
        assert(IS_NULLED(state));

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

        // !!! In R3-Alpha, there were manipulations on the name representing
        // the directory, for instance by adding "*" onto the end so that
        // Windows could use it for wildcard reading.  Yet this wasn't even
        // needed in the POSIX code, so it would have to strip it out.

        // !!! We are mirroring the use of the FILEREQ here, in order to make
        // the directories compatible in the PORT! calls.  This is probably
        // not useful, and files and directories can avoid using the same
        // structure...which would mean different Rename_Directory() and
        // Rename_File() calls, for instance.
        //
        REBBIN *bin = Make_Binary(sizeof(FILEREQ));
        Init_Binary(state, bin);
        TERM_BIN_LEN(bin, sizeof(FILEREQ));

        dir = File_Of_Port(port);
        dir->handle = nullptr;
        dir->id = FILEHANDLE_NONE;
        dir->is_dir = true;  // would be dispatching to File_Actor if dir
        dir->size_cache = FILESIZE_UNKNOWN;
        dir->offset = FILEOFFSET_UNKNOWN;

        // Generally speaking, you don't want to store REBVAL* or REBSER* in
        // something like this struct-embedded-in-a-BINARY! as it will be
        // invisible to the GC.  But this pointer is into the port spec, which
        // we will assume is good for the lifetime of the port.  :-/  (Not a
        // perfect assumption as there's no protection on it.)
        //
        dir->path = path;
    }

    switch (ID_OF_SYMBOL(verb)) {

    //=//// REFLECT ////////////////////////////////////////////////////////=//

      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));  // implicitly supplied as `port`

        SYMID property = VAL_WORD_ID(ARG(property));

        switch (property) {
            //
            // !!! Previously the directory synchronously read all the entries
            // on OPEN.  That method is being rethought.
            //
          case SYM_LENGTH:
            return rebValue("length of read", port);

            // !!! Directories were never actually really "opened" in R3-Alpha.
            // It is likely desirable to allow someone to open a directory and
            // hold it open--to lock it from being deleted, or to be able to
            // enumerate it one item at a time (e.g. to shortcut enumerating
            // all of it).
            //
          case SYM_OPEN_Q:
            return Init_Logic(D_OUT, false);

          default:
            break;
        }

        break; }

    //=//// READ ///////////////////////////////////////////////////////////=//

      case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PAR(source));

        if (REF(part) or REF(seek) or REF(string) or REF(lines))
            fail (Error_Bad_Refines_Raw());

        REBDSP dsp_orig = DSP;
        while (true) {
            REBVAL *result = Try_Read_Directory_Entry(dir);
            if (result == nullptr)
                break;

            // Put together the filename and the error (vs. a generic "cannot
            // find the file specified" message that doesn't say the name)
            //
            if (IS_ERROR(result))
                fail (Error_Cannot_Open_Raw(dir->path, result));

            assert(IS_FILE(result));
            Copy_Cell(DS_PUSH(), result);
            rebRelease(result);
        }

        Init_Block(D_OUT, Pop_Stack_Values(dsp_orig));
        return D_OUT; }

    //=//// CREATE /////////////////////////////////////////////////////////=//

      case SYM_CREATE: {
        if (IS_BLOCK(state))
            fail (Error_Already_Open_Raw(dir->path));

        REBVAL *error = Create_Directory(port);
        if (error) {
            rebRelease(error);  // !!! throws away details
            fail (Error_No_Create_Raw(dir->path));  // higher level error
        }

        RETURN (port); }

    //=//// RENAME /////////////////////////////////////////////////////////=//

      case SYM_RENAME: {
        INCLUDE_PARAMS_OF_RENAME;
        UNUSED(ARG(from));  // already have as port parameter

        REBVAL *error = Rename_File_Or_Directory(port, ARG(to));
        if (error) {
            rebRelease(error);  // !!! throws away details
            fail (Error_No_Rename_Raw(dir->path));  // higher level error
        }

        Copy_Cell(dir->path, ARG(to));  // !!! this mutates the spec, bad?

        RETURN (port); }

    //=//// DELETE /////////////////////////////////////////////////////////=//

      case SYM_DELETE: {
        REBVAL *error = Delete_File_Or_Directory(port);
        if (error) {
            rebRelease(error);  // !!! throws away details
            fail (Error_No_Delete_Raw(dir->path));  // higher level error
        }
        RETURN (port); }

    //=//// OPEN ///////////////////////////////////////////////////////////=//
    //
    // !!! In R3-Alpha, the act of OPEN on a directory would also go to the
    // filesystem and fill a buffer with the files...as opposed to waiting for
    // a READ request.  This meant there were two places that the reading
    // logic was implemented.
    //
    // Generally thus OPEN is a no-op unless you say /NEW.  There was no such
    // thing really as an "open directory" in R3-Alpha, and it only meant you
    // would be getting potentially stale caches of the entries.

      case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;

        UNUSED(PAR(spec));

        if (REF(read) or REF(write))
            fail (Error_Bad_Refines_Raw());

        if (REF(new)) {
            REBVAL *error = Create_Directory(port);
            if (error) {
                rebRelease(error);  // !!! throws away details
                fail (Error_No_Create_Raw(dir->path));  // higher level error
            }
        }

        RETURN (port); }

    //=//// CLOSE //////////////////////////////////////////////////////////=//

      case SYM_CLOSE:
        Init_Nulled(state);
        RETURN (port);

    //=//// QUERY //////////////////////////////////////////////////////////=//
    //
    // !!! One of the attributes you get back from QUERY is the answer to the
    // question "is this a file or a directory".  Yet the concept behind the
    // directory scheme is to be able to tell which you intend just from
    // looking at the terminal slash...so the directory scheme should always
    // give back that something is a directory.

      case SYM_QUERY: {
        REBVAL *info = Query_File_Or_Directory(port);
        if (IS_ERROR(info)) {
            rebRelease(info);  // !!! R3-Alpha threw out error, returns null
            return nullptr;
        }

        return info; }

      default:
        break;
    }

    return R_UNHANDLED;
}
