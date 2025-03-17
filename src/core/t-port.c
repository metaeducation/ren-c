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
REBINT CT_Port(const Cell* a, const Cell* b, bool strict)
{
    UNUSED(strict);
    if (Cell_Varlist(a) == Cell_Varlist(b))
        return 0;
    return Cell_Varlist(a) > Cell_Varlist(b) ? 1 : -1;  // !!! Review
}


//
//  /open?: native:generic [
//
//  "Test if a port is open (or other type?)"
//
//      return: [logic?]
//      element [fundamental?]
//  ]
//
DECLARE_NATIVE(open_q)
{
    INCLUDE_PARAMS_OF_OPEN_Q;

    return Dispatch_Generic(OPEN_Q, Element_ARG(element), LEVEL);
}


IMPLEMENT_GENERIC(EQUAL_Q, Is_Port)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;

    return LOGIC(CT_Port(ARG(value1), ARG(value2), REF(strict)) == 0);
}


// Create a new port. This is done by calling the MAKE-PORT* function in
// the system context.
//
IMPLEMENT_GENERIC(MAKE, Is_Port)
{
    INCLUDE_PARAMS_OF_MAKE;

    assert(VAL_TYPE_HEART(ARG(type)) == REB_PORT);
    UNUSED(ARG(type));

    Element* arg = Element_ARG(def);

    if (Is_Object(arg)) {
        //
        // !!! cannot convert to a PORT! without copying the whole context...
        // which raises the question of why convert an object to a port,
        // vs. making it as a port to begin with (?)  Look into why
        // system.standard.port is made with CONTEXT and not with MAKE PORT!
        //
        VarList* context = Copy_Varlist_Shallow_Managed(Cell_Varlist(arg));
        Value* rootvar = Rootvar_Of_Varlist(context);
        HEART_BYTE(rootvar) = REB_PORT;
        return Init_Port(OUT, context);
    }

    if (rebRunThrows(
        cast(Value*, OUT),  // <-- output cell
        rebRUN(SYS_UTIL(MAKE_PORT_P)), rebQ(arg)
    )){
        return FAIL(Error_No_Catch_For_Throw(TOP_LEVEL));
    }

    if (not Is_Port(OUT))  // should always create a port
        return RAISE(OUT);

    return OUT;
}


// !!! The concept of port dispatch from R3-Alpha is that it delegates to a
// handler which may be native code or user code.
//
IMPLEMENT_GENERIC(OLDGENERIC, Is_Port)
{
    const Symbol* verb = Level_Verb(LEVEL);
    Option(SymId) id = Symbol_Id(verb);

    Element* port = cast(Element*, ARG_N(1));
    assert(Is_Port(port));

    enum {
        ST_TYPE_PORT_INITIAL_ENTRY = STATE_0,
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

    if (id == SYM_PICK or id == SYM_POKE)
        return GENERIC_CFUNC(OLDGENERIC, Any_Context)(level_);

    VarList* ctx = Cell_Varlist(port);
    Value* actor = Varlist_Slot(ctx, STD_PORT_ACTOR);

    // If actor is a HANDLE!, it should be a PAF
    //
    // !!! Review how user-defined types could make this better/safer, as if
    // it's some other kind of handle value this could crash.
    //
    if (Is_Native_Port_Actor(actor)) {
        Bounce b = cast(PORT_HOOK*, Cell_Handle_Cfunc(actor))(level_, port, verb);
        if (b == BOUNCE_FAIL)
            return b;

        if (b == nullptr)
           Init_Nulled(OUT);
        else if (b != OUT) {
            Atom* r = Atom_From_Bounce(b);
            assert(Is_Api_Value(r));
            Copy_Cell(OUT, r);
            Release_Api_Value_If_Unmanaged(r);
        }

        goto post_process_output;
    }

    if (not Is_Object(actor))
        return FAIL(Error_Invalid_Actor_Raw());

    // Dispatch object function:

    const bool strict = false;
    Option(Index) index = Find_Symbol_In_Context(actor, verb, strict);

    Value* action;
    if (not index)
        action = nullptr;
    else
        action = Varlist_Slot(Cell_Varlist(actor), unwrap index);

    if (not action or not Is_Action(action)) {
        DECLARE_ATOM (verb_cell);
        Init_Word(verb_cell, verb);
        return FAIL(Error_No_Port_Action_Raw(verb_cell));
    }

    Push_Redo_Action_Level(OUT, level_, action);

    STATE = ST_TYPE_PORT_RUNNING_ACTOR;
    return CONTINUE_SUBLEVEL(TOP_LEVEL);

} post_process_output: {  ////////////////////////////////////////////////////

    // !!! READ's /LINES and /STRING refinements are something that should
    // work regardless of data source.  But R3-Alpha only implemented it in
    // %p-file.c, so it got ignored.  Ren-C caught that it was being ignored,
    // so the code was moved to here as a quick fix.
    //
    // !!! Note this code is incorrect for files read in chunks!!!

    if (id == SYM_READ) {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PARAM(source));
        UNUSED(PARAM(part));
        UNUSED(PARAM(seek));

        if (Is_Nulled(OUT))
            return nullptr;  // !!! `read dns://` returns nullptr on failure

        if ((REF(string) or REF(lines)) and not Is_Text(OUT)) {
            if (not Is_Blob(OUT))
                return FAIL(
                    "READ :STRING or :LINES used on a non-BLOB!/STRING! read"
                );

            Size size;
            const Byte* data = Cell_Blob_Size_At(&size, OUT);
            String* decoded = Make_Sized_String_UTF8(cs_cast(data), size);
            Init_Text(OUT, decoded);
        }

        if (REF(lines)) { // caller wants a BLOCK! of STRING!s, not one string
            assert(Is_Text(OUT));

            DECLARE_ELEMENT (temp);
            Move_Cell(temp, cast(Element*, OUT));
            Init_Block(OUT, Split_Lines(temp));
        }
    }

    return OUT;
}}


// The idea for dispatching a URL! is that it will dispatch to port schemes.
// So it translates the request to open the port, then retriggers the action
// on that port, then closes the port.
//
IMPLEMENT_GENERIC(OLDGENERIC, Url)
{
    const Symbol* verb = Level_Verb(LEVEL);
    Option(SymId) id = Symbol_Id(verb);

    Element* url = cast(Element*, ARG_N(1));
    assert(Is_Url(url));

    if (id == SYM_COPY) {
        //
        // https://forum.rebol.info/t/copy-and-port/1699
        //
        return COPY(url);
    }
    else switch (id) {
      /*case SYM_REFLECT:*/  // !!! Review separate XXX-OF and XXX? generics
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
        return FAIL("URL! must be used with IO annotation if intentional");
    }

    Value* port = rebValue("make port!", url);
    assert(Is_Port(port));

    // The frame was built for the verb we want to apply, so tweak it so that
    // it has the PORT! in the argument slot, and run the action.
    //
    Copy_Cell(ARG_N(1), port);  // can't Move_Cell() on an API cell
    rebRelease(port);

    assert(STATE == STATE_0);  // retriggered frame must act like initial entry
    return BOUNCE_CONTINUE;
}


// defer to String (handles non-node-having case too)
//
IMPLEMENT_GENERIC(TO, Url)
{
    INCLUDE_PARAMS_OF_TO;

    USED(ARG(type));  // deferred to string via LEVEL
    USED(ARG(element));

    return GENERIC_CFUNC(TO, Any_String)(LEVEL);
}
