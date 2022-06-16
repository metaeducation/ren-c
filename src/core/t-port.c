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
REBINT CT_Port(noquote(const Cell*) a, noquote(const Cell*) b, bool strict)
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
REB_R MAKE_Port(
    REBVAL *out,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    assert(kind == REB_PORT);
    if (parent)
        fail (Error_Bad_Make_Parent(kind, unwrap(parent)));

    assert(not IS_NULLED(arg)); // API would require NULLIFY_NULLED

    if (rebRunThrows(
        out,  // <-- output cell
        Sys(SYM_MAKE_PORT_P), rebQ(arg)
    )){
        fail (Error_No_Catch_For_Throw(FS_TOP));
    }

    if (not IS_PORT(out))  // should always create a port
        fail (out);

    return out;
}


//
//  TO_Port: C
//
REB_R TO_Port(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_PORT);
    UNUSED(kind);

    if (!IS_OBJECT(arg))
        fail (Error_Bad_Make(REB_PORT, arg));

    // !!! cannot convert TO a PORT! without copying the whole context...
    // which raises the question of why convert an object to a port,
    // vs. making it as a port to begin with (?)  Look into why
    // system.standard.port is made with CONTEXT and not with MAKE PORT!
    //
    REBCTX *context = Copy_Context_Shallow_Managed(VAL_CONTEXT(arg));
    REBVAL *rootvar = CTX_ROOTVAR(context);
    mutable_HEART_BYTE(rootvar) = REB_PORT;

    return Init_Port(out, context);
}


//
//  REBTYPE: C
//
// !!! The concept of port dispatch from R3-Alpha is that it delegates to a
// handler which may be native code or user code.
//
REBTYPE(Port)
{
    SYMID id = ID_OF_SYMBOL(verb);

    // !!! The ability to transform some BLOCK!s into PORT!s for some actions
    // was hardcoded in a fairly ad-hoc way in R3-Alpha, which was based on
    // an integer range of action numbers.  Ren-C turned these numbers into
    // symbols, where order no longer applied.  The mechanism needs to be
    // rethought, see:
    //
    // https://github.com/metaeducation/ren-c/issues/311
    //
    if (not IS_PORT(D_ARG(1))) {
        switch (id) {

        case SYM_READ:
        case SYM_WRITE:
        case SYM_QUERY:
        case SYM_OPEN:
        case SYM_CREATE:
        case SYM_DELETE:
        case SYM_RENAME: {
            //
            // !!! We are going to "re-apply" the call frame with routines we
            // are going to read the D_ARG(1) slot *implicitly* regardless of
            // what value points to.
            //
            const REBVAL *made = rebValue("make port! @", D_ARG(1));
            assert(IS_PORT(made));
            Copy_Cell(D_ARG(1), made);
            rebRelease(made);
            break; }

        // Once handled SYM_REFLECT here by delegating to T_Context(), but
        // common reflectors now in Context_Common_Action_Or_End()

        default:
            break;
        }
    }

    if (not IS_PORT(D_ARG(1)))
        fail (D_ARG(1));

    REBVAL *port = D_ARG(1);

    if (id == SYM_PICK_P or id == SYM_POKE_P)
        return T_Context(frame_, verb);

    REB_R r = Context_Common_Action_Maybe_Unhandled(frame_, verb);
    if (r != R_UNHANDLED)
        return r;

    return Do_Port_Action(frame_, port, verb);
}


//
//  CT_Url: C
//
REBINT CT_Url(noquote(const Cell*) a, noquote(const Cell*) b, bool strict)
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
        return_value (url);
    }
    else if (GET_CELL_FLAG(url, UNEVALUATED)) {
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

    return Do_Port_Action(frame_, D_ARG(1), verb);
}
