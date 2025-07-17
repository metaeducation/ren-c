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


//
//  CT_Port: C
//
REBINT CT_Port(const Cell* a, const Cell* b, REBINT mode)
{
    if (mode < 0) return -1;
    return Cell_Varlist(a) == Cell_Varlist(b);
}


//
//  MAKE_Port: C
//
// Create a new port. This is done by calling the MAKE_PORT
// function stored in the system/intrinsic object.
//
Bounce MAKE_Port(Value* out, Type type, const Value* arg)
{
    assert(type == TYPE_PORT);
    UNUSED(type);

    const bool fully = true; // error if not all arguments consumed

    Value* make_port_helper = Varlist_Slot(Sys_Context, SYS_CTX_MAKE_PORT_P);
    assert(Is_Action(make_port_helper));

    assert(not Is_Nulled(arg)); // would need to DEVOID it otherwise
    if (Apply_Only_Throws(out, fully, make_port_helper, arg, rebEND))
        panic (Error_No_Catch_For_Throw(out));

    // !!! Shouldn't this be testing for !Is_Port( ) ?
    if (Is_Blank(out))
        panic (Error_Invalid_Spec_Raw(arg));

    return out;
}


//
//  TO_Port: C
//
Bounce TO_Port(Value* out, Type type, const Value* arg)
{
    assert(type == TYPE_PORT);
    UNUSED(type);

    if (!Is_Object(arg))
        panic (Error_Bad_Make(TYPE_PORT, arg));

    // !!! cannot convert TO a PORT! without copying the whole context...
    // which raises the question of why convert an object to a port,
    // vs. making it as a port to begin with (?)  Look into why
    // system/standard/port is made with CONTEXT and not with MAKE PORT!
    //
    VarList* context = Copy_Context_Shallow_Managed(Cell_Varlist(arg));
    RESET_CELL(Varlist_Archetype(context), TYPE_PORT);

    return Init_Port(out, context);
}


//
//  Retrigger_Append_As_Write: C
//
// !!! In R3-Alpha, for the convenience of being able to APPEND to something
// that may be a FILE!-based PORT! or a BINARY! or STRING! with a unified
// interface, the APPEND command was re-interpreted as a WRITE/APPEND.  But
// it was done with presumption that APPEND and WRITE had compatible frames,
// which generally speaking they do not.
//
// This moves the functionality to an actual retriggering which calls whatever
// WRITE/APPEND would do in a generic fashion with a new frame.  Not all
// ports do this, as some have their own interpretation of APPEND.  It's
// hacky, but still not as bad as it was.  Review.
//
Bounce Retrigger_Append_As_Write(Level* level_) {
    INCLUDE_PARAMS_OF_APPEND;

    // !!! Something like `write/append %foo.txt "data"` knows to convert
    // %foo.txt to a port before trying the write, but if you say
    // `append %foo.txt "data"` you get `%foo.txtdata`.  Some actions are like
    // this, e.g. PICK, where they can't do the automatic conversion.
    //
    assert(Is_Port(ARG(SERIES))); // !!! poorly named
    UNUSED(ARG(SERIES));
    if (not (
        Is_Binary(ARG(VALUE))
        or Is_Text(ARG(VALUE))
        or Is_Block(ARG(VALUE)))
    ){
        panic (Error_Invalid(ARG(VALUE)));
    }

    if (Bool_ARG(PART)) {
        UNUSED(ARG(LIMIT));
        panic (Error_Bad_Refines_Raw());
    }
    if (Bool_ARG(ONLY))
        panic (Error_Bad_Refines_Raw());
    if (Bool_ARG(DUP)) {
        UNUSED(ARG(COUNT));
        panic (Error_Bad_Refines_Raw());
    }
    if (Bool_ARG(LINE))
        panic (Error_Bad_Refines_Raw());

    return rebValue("write/append", D_ARG(1), D_ARG(2));
}


//
//  REBTYPE: C
//
// !!! The concept of port dispatch from R3-Alpha is that it delegates to a
// handler which may be native code or user code.
//
REBTYPE(Port)
{
    // !!! The ability to transform some BLOCK!s into PORT!s for some actions
    // was hardcoded in a fairly ad-hoc way in R3-Alpha, which was based on
    // an integer range of action numbers.  Ren-C turned these numbers into
    // symbols, where order no longer applied.  The mechanism needs to be
    // rethought, see:
    //
    // https://github.com/metaeducation/ren-c/issues/311
    //
    if (not Is_Port(D_ARG(1))) {
        switch (maybe Word_Id(verb)) {

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
            const Value* made = rebValue("make port!", D_ARG(1));
            assert(Is_Port(made));
            Copy_Cell(D_ARG(1), made);
            rebRelease(made);
            break; }

        case SYM_ON_WAKE_UP:
            break;

        // Once handled SYM_REFLECT here by delegating to T_Context(), but
        // common reflectors now in Context_Common_Action_Or_End()

        default:
            break;
        }
    }

    if (not Is_Port(D_ARG(1)))
        panic (Error_Illegal_Action(Type_Of(D_ARG(1)), verb));

    Value* port = D_ARG(1);

    Bounce bounce = Context_Common_Action_Maybe_Unhandled(level_, verb);
    if (bounce != BOUNCE_UNHANDLED)
        return bounce;

    return Do_Port_Action(level_, port, verb);
}
