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

#include "reb-config.h"

#if defined(_MSC_VER)
    #pragma warning(disable : 4668)  // allow #if of undefined things
#endif
#include "uv.h"  // includes windows.h, with bad #ifs in <winioctl.h>
#if defined(_MSC_VER)
    #pragma warning(error : 4668)   // disallow #if of undefined things
#endif

#if TO_WINDOWS
    #undef OUT  // %minwindef.h defines this, we have a better use for it
    #undef VOID  // %winnt.h defines this, we have a better use for it
#endif

#include "sys-core.h"
#include "tmp-mod-filesystem.h"

#include "tmp-paramlists.h"  // !!! for INCLUDE_PARAMS_OF_OPEN, etc.

#include "file-req.h"


extern Value* Query_File_Or_Directory(const Value* port);
extern Value* Create_Directory(const Value* port);
extern Value* Delete_File_Or_Directory(const Value* port);
extern Value* Rename_File_Or_Directory(const Value* port, const Value* to);

Value* Try_Read_Directory_Entry(FILEREQ *dir);


//
//  export dir-actor: native [
//
//  "Handler for OLDGENERIC dispatch on Directory PORT!s"
//
//      return: [any-value?]
//  ]
//
DECLARE_NATIVE(DIR_ACTOR)
{
    Value* port = ARG_N(1);
    const Symbol* verb = Level_Verb(LEVEL);
    VarList* ctx = Cell_Varlist(port);

    Value* state = Varlist_Slot(ctx, STD_PORT_STATE);
    FILEREQ *dir;
    if (Is_Blob(state)) {
        dir = File_Of_Port(port);
    }
    else {
        assert(Is_Nulled(state));

        Value* spec = Varlist_Slot(ctx, STD_PORT_SPEC);
        if (not Is_Object(spec))
            return FAIL(Error_Invalid_Spec_Raw(spec));

        Value* path = Obj_Value(spec, STD_PORT_SPEC_HEAD_REF);
        if (path == nullptr)
            return FAIL(Error_Invalid_Spec_Raw(spec));

        if (Is_Url(path))
            path = Obj_Value(spec, STD_PORT_SPEC_HEAD_PATH);
        else if (not Is_File(path))
            return FAIL(Error_Invalid_Spec_Raw(path));

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
        Binary* b = Make_Binary(sizeof(FILEREQ));
        Init_Blob(state, b);
        Term_Binary_Len(b, sizeof(FILEREQ));

        dir = File_Of_Port(port);
        dir->handle = nullptr;
        dir->id = FILEHANDLE_NONE;
        dir->is_dir = true;  // would be dispatching to File Actor if dir
        dir->size_cache = FILESIZE_UNKNOWN;
        dir->offset = FILEOFFSET_UNKNOWN;

        // Generally speaking, you don't want to store Value* or Flex* in
        // something like this struct-embedded-in-a-BLOB! as it will be
        // invisible to the GC.  But this pointer is into the port spec, which
        // we will assume is good for the lifetime of the port.  :-/  (Not a
        // perfect assumption as there's no protection on it.)
        //
        dir->path = path;
    }

    Option(SymId) id = Symbol_Id(verb);

    switch (id) {

        // !!! Previously the directory synchronously read all the entries
        // on OPEN.  That method is being rethought.
        //
      case SYM_LENGTH_OF:
        return rebValue("length of read", port);

        // !!! Directories were never actually really "opened" in R3-Alpha.
        // It is likely desirable to allow someone to open a directory and
        // hold it open--to lock it from being deleted, or to be able to
        // enumerate it one item at a time (e.g. to shortcut enumerating
        // all of it).
        //
      case SYM_OPEN_Q:
        return Init_Logic(OUT, false);

    //=//// READ ///////////////////////////////////////////////////////////=//

      case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PARAM(SOURCE));

        if (Bool_ARG(PART) or Bool_ARG(SEEK) or Bool_ARG(STRING) or Bool_ARG(LINES))
            return FAIL(Error_Bad_Refines_Raw());

        assert(TOP_INDEX == STACK_BASE);
        while (true) {
            Value* result = Try_Read_Directory_Entry(dir);
            if (result == nullptr)
                break;

            // Put together the filename and the error (vs. a generic "cannot
            // find the file specified" message that doesn't say the name)
            //
            if (Is_Error(result))
                return FAIL(Error_Cannot_Open_Raw(dir->path, result));

            assert(Is_File(result));
            Copy_Cell(PUSH(), result);
            rebRelease(result);
        }

        Init_Block(OUT, Pop_Source_From_Stack(STACK_BASE));
        return OUT; }

    //=//// CREATE /////////////////////////////////////////////////////////=//

      case SYM_CREATE: {
        if (Is_Block(state))
            return FAIL(Error_Already_Open_Raw(dir->path));

        Value* error = Create_Directory(port);
        if (error) {
            rebRelease(error);  // !!! throws away details
            return FAIL(Error_No_Create_Raw(dir->path));  // higher level error
        }

        return COPY(port); }

    //=//// RENAME /////////////////////////////////////////////////////////=//

      case SYM_RENAME: {
        INCLUDE_PARAMS_OF_RENAME;
        UNUSED(ARG(FROM));  // already have as port parameter

        Value* error = Rename_File_Or_Directory(port, ARG(TO));
        if (error) {
            rebRelease(error);  // !!! throws away details
            return FAIL(Error_No_Rename_Raw(dir->path));  // higher level error
        }

        Copy_Cell(dir->path, ARG(TO));  // !!! this mutates the spec, bad?

        return COPY(port); }

    //=//// DELETE /////////////////////////////////////////////////////////=//

      case SYM_DELETE: {
        Value* error = Delete_File_Or_Directory(port);
        if (error) {
            rebRelease(error);  // !!! throws away details
            return FAIL(Error_No_Delete_Raw(dir->path));  // higher level error
        }
        return COPY(port); }

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

        UNUSED(PARAM(SPEC));

        if (Bool_ARG(READ) or Bool_ARG(WRITE))
            return FAIL(Error_Bad_Refines_Raw());

        if (Bool_ARG(NEW)) {
            Value* error = Create_Directory(port);
            if (error) {
                rebRelease(error);  // !!! throws away details
                return FAIL(Error_No_Create_Raw(dir->path));  // hi-level error
            }
        }

        return COPY(port); }

    //=//// CLOSE //////////////////////////////////////////////////////////=//

      case SYM_CLOSE:
        Init_Nulled(state);
        return COPY(port);

    //=//// QUERY //////////////////////////////////////////////////////////=//
    //
    // !!! One of the attributes you get back from QUERY is the answer to the
    // question "is this a file or a directory".  Yet the concept behind the
    // directory scheme is to be able to tell which you intend just from
    // looking at the terminal slash...so the directory scheme should always
    // give back that something is a directory.

      case SYM_QUERY: {
        Value* info = Query_File_Or_Directory(port);
        if (Is_Error(info)) {
            rebRelease(info);  // !!! R3-Alpha threw out error, returns null
            return nullptr;
        }

        return info; }

      default:
        break;
    }

    return UNHANDLED;
}
