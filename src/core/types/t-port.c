//
//  file: %t-port.c
//  summary: "port datatype"
//  section: datatypes
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
REBINT CT_Port(const Element* a, const Element* b, bool strict)
{
    UNUSED(strict);
    if (Cell_Varlist(a) == Cell_Varlist(b))
        return 0;
    return Cell_Varlist(a) > Cell_Varlist(b) ? 1 : -1;  // !!! Review
}


//
//  open?: native:generic [
//
//  "Test if a port is open (or other type?)"
//
//      return: [logic?]
//      value [fundamental?]
//  ]
//
DECLARE_NATIVE(OPEN_Q)
{
    INCLUDE_PARAMS_OF_OPEN_Q;

    return Dispatch_Generic(OPEN_Q, Element_ARG(VALUE), LEVEL);
}


IMPLEMENT_GENERIC(EQUAL_Q, Is_Port)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;
    bool strict = not ARG(RELAX);

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    return LOGIC(CT_Port(v1, v2, strict) == 0);
}


// Create a new port. This is done by calling the MAKE-PORT* function in
// the system context.
//
IMPLEMENT_GENERIC(MAKE, Is_Port)
{
    INCLUDE_PARAMS_OF_MAKE;

    assert(Datatype_Builtin_Heart(ARG(TYPE)) == TYPE_PORT);
    UNUSED(ARG(TYPE));

    Element* arg = Element_ARG(DEF);

    if (Is_Object(arg)) {
        //
        // !!! cannot convert to a PORT! without copying the whole context...
        // which raises the question of why convert an object to a port,
        // vs. making it as a port to begin with (?)  Look into why
        // system.standard.port is made with CONTEXT and not with MAKE PORT!
        //
        VarList* context = Copy_Varlist_Shallow_Managed(Cell_Varlist(arg));
        Stable* rootvar = Rootvar_Of_Varlist(context);
        KIND_BYTE(rootvar) = TYPE_PORT;
        return Init_Port(OUT, context);
    }

    Sink(Stable) out = OUT;
    if (rebRunThrows(
        out,  // <-- output cell
        "sys.util/make-port*", rebQ(arg)
    )){
        panic (Error_No_Catch_For_Throw(TOP_LEVEL));
    }

    if (not Is_Port(out))  // should always create a port
        return fail (out);

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

    VarList* ctx = Cell_Varlist(port);

    Sink(Stable) spare_actor = SPARE;

    require (
      Read_Slot(
        spare_actor,
        Varlist_Slot(ctx, STD_PORT_ACTOR)
    ));

    // If actor is an ACTION!, it should be an OLDGENERIC Dispatcher for PORT!
    //
    if (Is_Action(spare_actor)) {
        level_->u.action.label = verb;  // legacy hack, used by Level_Verb()

        Details* details = Ensure_Frame_Details(spare_actor);
        Dispatcher* dispatcher = Details_Dispatcher(details);
        Bounce b = opt Irreducible_Bounce(
            LEVEL,
            Apply_Cfunc(dispatcher, LEVEL)
        );
        if (b)  // couldn't reduce to being something in OUT
            return b;

        if (Is_Error(OUT))
            return OUT;

        goto post_process_output;
    }

    if (not Is_Object(spare_actor))
        panic (Error_Invalid_Actor_Raw());

    // Dispatch object function:

    const bool strict = false;
    Option(Index) index = Find_Symbol_In_Context(
        Known_Element(spare_actor), verb, strict
    );

    Sink(Stable) scratch_action = SCRATCH;
    if (not index)
        Init_Nulled(scratch_action);
    else {
        require (
          Read_Slot(
            scratch_action,
            Varlist_Slot(Cell_Varlist(spare_actor), unwrap index)
        ));
    }

    if (not Is_Action(scratch_action))
        panic (Error_No_Port_Action_Raw(verb));

    Push_Redo_Action_Level(OUT, level_, scratch_action);

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

        UNUSED(PARAM(SOURCE));
        UNUSED(PARAM(PART));
        UNUSED(PARAM(SEEK));

        trap (
          Stable* out = Decay_If_Unstable(OUT)
        );

        if (Is_Nulled(out))
            return nullptr;  // !!! `read dns://` returns nullptr on failure

        if ((ARG(STRING) or ARG(LINES)) and not Is_Text(out)) {
            if (not Is_Blob(out))
                panic (
                    "READ :STRING or :LINES used on a non-BLOB!/TEXT! read"
                );

            Size size;
            const Byte* data = Blob_Size_At(&size, out);
            Strand* decoded = Make_Sized_Strand_UTF8(s_cast(data), size);
            Init_Text(OUT, decoded);
        }

        if (ARG(LINES)) { // caller wants BLOCK! of STRING!s, not one
            assert(Is_Text(out));

            DECLARE_ELEMENT (temp);
            Move_Cell(temp, Known_Element(out));
            Init_Block(OUT, Split_Lines(temp));
        }
    }

    return OUT;
}}


// Copy is a "new generic"; in order to make `copy port` delegate to the port
// actor for things like the old ODBC scheme, it has to bridge here.
//
IMPLEMENT_GENERIC(COPY, Is_Port)
{
    INCLUDE_PARAMS_OF_COPY;

    USED(ARG(VALUE));  // arguments passed through via level_
    USED(ARG(PART));
    USED(ARG(DEEP));

    level_->u.action.label = Canon_Symbol(SYM_COPY);  // !!! Level_Verb() hack
    return GENERIC_CFUNC(OLDGENERIC, Is_Port)(level_);
}


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

    switch (opt id) {
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
        panic ("URL! must be used with IO annotation if intentional");
    }

    Api(Stable*) port = rebStable("make port!", url);
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

    USED(ARG(TYPE));  // deferred to string via LEVEL
    USED(ARG(VALUE));

    return GENERIC_CFUNC(TO, Any_String)(LEVEL);
}


//
//  Get_Port_Path_From_Spec: C
//
// Previously the FileReq would store a pointer to a Stable* that was the path,
// which was assumed to live in the spec somewhere.  Object Slots are now
// abstracted, so you don't use direct pointers like that.  Instead this
// reads the path from the port spec each time its needed...which should
// still work because it was extracted and assigned once anyway.
//
Result(Option(Stable*)) Get_Port_Path_From_Spec(
    Sink(Stable) out,
    const Stable* port
){
    VarList* ctx = Cell_Varlist(port);

    DECLARE_STABLE (spec);
    require (
      Read_Slot(spec, Varlist_Slot(ctx, STD_PORT_SPEC))
    );
    if (not Is_Object(spec))
        return fail (Error_Invalid_Spec_Raw(spec));

    require (
      Read_Slot(out, Obj_Slot(spec, STD_PORT_SPEC_HEAD_REF))
    );
    if (Is_Nulled(out))
        return fail (Error_Invalid_Spec_Raw(spec));

    if (Is_Url(out)) {
        require (
          Read_Slot(out, Obj_Slot(spec, STD_PORT_SPEC_HEAD_PATH))
        );
    }
    else if (not Is_File(out))
        return fail (Error_Invalid_Spec_Raw(spec));

    return out;
}
