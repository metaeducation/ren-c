//
//  File: %t-port.c
//  Summary: "port datatype"
//  Section: datatypes
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


//
//  CT_Port: C
//
REBINT CT_Port(noquote(Cell(const*)) a, noquote(Cell(const*)) b, bool strict)
{
    UNUSED(strict);
    if (VAL_CONTEXT(a) == VAL_CONTEXT(b))
        return 0;
    return VAL_CONTEXT(a) > VAL_CONTEXT(b) ? 1 : -1;  // !!! Review
}


//
//  MAKE_Port: C
//
// Create a new port. This is done by calling the MAKE_PORT
// function stored in the system/intrinsic object.
//
Bounce MAKE_Port(
    Frame(*) frame_,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    assert(kind == REB_PORT);
    if (parent)
        return FAIL(Error_Bad_Make_Parent(kind, unwrap(parent)));

    assert(not Is_Nulled(arg)); // API would require NULLIFY_NULLED

    if (rebRunThrows(
        OUT,  // <-- output cell
        SysUtil(MAKE_PORT_P), rebQ(arg)
    )){
        fail (Error_No_Catch_For_Throw(TOP_FRAME));
    }

    if (not IS_PORT(OUT))  // should always create a port
        return FAIL(OUT);

    return OUT;
}


//
//  TO_Port: C
//
Bounce TO_Port(Frame(*) frame_, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_PORT);
    UNUSED(kind);

    if (not IS_OBJECT(arg))
        return FAIL(Error_Bad_Make(REB_PORT, arg));

    // !!! cannot convert TO a PORT! without copying the whole context...
    // which raises the question of why convert an object to a port,
    // vs. making it as a port to begin with (?)  Look into why
    // system.standard.port is made with CONTEXT and not with MAKE PORT!
    //
    Context(*) context = Copy_Context_Shallow_Managed(VAL_CONTEXT(arg));
    REBVAL *rootvar = CTX_ROOTVAR(context);
    mutable_HEART_BYTE(rootvar) = REB_PORT;

    return Init_Port(OUT, context);
}


//
//  REBTYPE: C
//
// !!! The concept of port dispatch from R3-Alpha is that it delegates to a
// handler which may be native code or user code.
//
REBTYPE(Port)
{
    REBVAL *port = D_ARG(1);
    assert(IS_PORT(port));

    SYMID id = ID_OF_SYMBOL(verb);

    enum {
        ST_TYPE_PORT_INITIAL_ENTRY = 0,
        ST_TYPE_PORT_RUNNING_ACTOR
    };

    switch (STATE) {
      case ST_TYPE_PORT_INITIAL_ENTRY :
        goto initial_entry;

      case ST_TYPE_PORT_RUNNING_ACTOR :
        goto post_process_output;

      default : assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    // See Context_Common_Action_Maybe_Unhandled() for why general delegation
    // to T_Context() is not performed.
    //
    if (id == SYM_PICK_P or id == SYM_POKE_P)
        return T_Context(frame_, verb);

    Context(*) ctx = VAL_CONTEXT(port);
    REBVAL *actor = CTX_VAR(ctx, STD_PORT_ACTOR);

    // If actor is a HANDLE!, it should be a PAF
    //
    // !!! Review how user-defined types could make this better/safer, as if
    // it's some other kind of handle value this could crash.
    //
    if (Is_Native_Port_Actor(actor)) {
        Bounce b = cast(PORT_HOOK*, VAL_HANDLE_CFUNC(actor))(frame_, port, verb);

        if (b == nullptr)
           Init_Nulled(OUT);
        else if (b != OUT) {
            REBVAL *r = Value_From_Bounce(b);
            assert(Is_Api_Value(r));
            Copy_Cell(OUT, r);
            Release_Api_Value_If_Unmanaged(r);
        }

        goto post_process_output;
    }

    if (not IS_OBJECT(actor))
        fail (Error_Invalid_Actor_Raw());

    // Dispatch object function:

    const bool strict = false;
    REBLEN n = Find_Symbol_In_Context(actor, verb, strict);

    REBVAL *action = (n == 0)
        ? cast(REBVAL*, nullptr)  // C++98 ambiguous w/o cast
        : CTX_VAR(VAL_CONTEXT(actor), n);

    if (not action or not IS_ACTION(action)) {
        DECLARE_LOCAL (verb_cell);
        Init_Word(verb_cell, verb);
        fail (Error_No_Port_Action_Raw(verb_cell));
    }

    Push_Redo_Action_Frame(OUT, frame_, action);

    STATE = ST_TYPE_PORT_RUNNING_ACTOR;
    return CONTINUE_SUBFRAME(TOP_FRAME);

} post_process_output: {  ////////////////////////////////////////////////////

    // !!! READ's /LINES and /STRING refinements are something that should
    // work regardless of data source.  But R3-Alpha only implemented it in
    // %p-file.c, so it got ignored.  Ren-C caught that it was being ignored,
    // so the code was moved to here as a quick fix.
    //
    // !!! Note this code is incorrect for files read in chunks!!!

    if (id == SYM_READ) {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PAR(source));
        UNUSED(PAR(part));
        UNUSED(PAR(seek));

        if (Is_Nulled(OUT))
            return nullptr;  // !!! `read dns://` returns nullptr on failure

        if ((REF(string) or REF(lines)) and not IS_TEXT(OUT)) {
            if (not IS_BINARY(OUT))
                fail ("/STRING or /LINES used on a non-BINARY!/STRING! read");

            Size size;
            const Byte* data = VAL_BINARY_SIZE_AT(&size, OUT);
            String(*) decoded = Make_Sized_String_UTF8(cs_cast(data), size);
            Init_Text(OUT, decoded);
        }

        if (REF(lines)) { // caller wants a BLOCK! of STRING!s, not one string
            assert(IS_TEXT(OUT));

            DECLARE_LOCAL (temp);
            Move_Cell(temp, OUT);
            Init_Block(OUT, Split_Lines(temp));
        }
    }

    return OUT;
}}


//
//  CT_Url: C
//
REBINT CT_Url(noquote(Cell(const*)) a, noquote(Cell(const*)) b, bool strict)
{
    return CT_String(a, b, strict);
}


//
//  REBTYPE: C
//
// The idea for dispatching a URL! is that it will dispatch to port schemes.
// So it translates the request to open the port, then retriggers the action
// on that port, then closes the port.
//
REBTYPE(Url)
{
    REBVAL *url = D_ARG(1);

    SYMID id = ID_OF_SYMBOL(verb);
    if (id == SYM_COPY) {
        //
        // https://forum.rebol.info/t/copy-and-port/1699
        //
        return COPY(url);
    }
    else if (Get_Cell_Flag(url, UNEVALUATED)) {
        //
        // There are risks associated when common terms like APPEND can too
        // carelessly be interpreted as IO.  Because what was intended as a
        // local operation can wind up corrupting files or using up network
        // bandwidth in crazy ways:
        //
        //   https://forum.rebol.info/t/1697
        //
        // Literal usages are interpreted as intentional, so if the URL was
        // written at the callsite then accept that.
        //
    }
    else switch (id) {
      case SYM_REFLECT:
      case SYM_READ:
      case SYM_WRITE:
      case SYM_QUERY:
      case SYM_OPEN:
      case SYM_CREATE:
      case SYM_DELETE:
      case SYM_RENAME:
        //
        // !!! A tentative concept is that some words are "greenlit" as being
        // "IO words", hence not needing any annotation in order to be used
        // with an evaluative product or variable lookup that is a URL! to
        // work with implicit PORT!s.
        //
        break;

      default:
        fail ("URL! must be used with IO annotation if intentional");
    }

    REBVAL *port = rebValue("make port!", url);
    assert(IS_PORT(port));

    // The frame was built for the verb we want to apply, so tweak it so that
    // it has the PORT! in the argument slot, and run the action.
    //
    Move_Cell(D_ARG(1), port);
    rebRelease(port);

    return BOUNCE_CONTINUE;
}
