//
//  File: %t-blank.c
//  Summary: "Blank datatype"
//  Section: datatypes
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

//
//  CT_Unit: C
//
REBINT CT_Unit(const Cell* a, const Cell* b, REBINT mode)
{
    if (mode >= 0) return (VAL_TYPE(a) == VAL_TYPE(b));
    return -1;
}


//
//  MAKE_Unit: C
//
// MAKE is disallowed, with the general rule that a blank in will give
// a null out... for e.g. `make object! maybe select data spec else [...]`
//
Bounce MAKE_Unit(Value* out, enum Reb_Kind kind, const Value* arg) {
    UNUSED(out);
    fail (Error_Bad_Make(kind, arg));
}


//
//  TO_Unit: C
//
// TO is disallowed, e.g. you can't TO convert an integer of 0 to a blank.
//
Bounce TO_Unit(Value* out, enum Reb_Kind kind, const Value* data) {
    UNUSED(out);
    fail (Error_Bad_Make(kind, data));
}


//
//  MF_Unit: C
//
void MF_Unit(Molder* mo, const Cell* v, bool form)
{
    UNUSED(form); // no distinction between MOLD and FORM

    switch (VAL_TYPE(v)) {
      case REB_BLANK:
        Append_Unencoded(mo->utf8flex, "_");
        break;

      case REB_NOTHING:  // In modern Ren-C, nothing is an antiform of blank
        Append_Unencoded(mo->utf8flex, "~");
        break;

      case REB_VOID:  // In modern Ren-C, void is the antiform of the word VOID
        Append_Unencoded(mo->utf8flex, "~void~");
        break;

      default:
        panic (v);
    }
}


//
//  PD_Blank: C
//
// It is not possible to "poke" into a blank (and as an attempt at modifying
// operation, it is not swept under the rug).  But if picking with GET-PATH!
// or GET, we indicate no result with void.  (Ordinary path selection will
// treat this as an error.)
//
Bounce PD_Blank(
    REBPVS *pvs,
    const Value* picker,
    const Value* opt_setval
){
    UNUSED(picker);
    UNUSED(pvs);

    if (opt_setval != nullptr)
        return BOUNCE_UNHANDLED;

    return nullptr;
}


//
//  REBTYPE: C
//
// Asking to read a property of a VOID value is handled as a "light"
// failure, in the sense that it just returns NULL.  Returning NULL instead
// helps establish error locality in chains of operations:
//
//     if not find select next first x [
//        ;
//        ; If voids propagated too far, what actually went wrong, here?
//        ; (reader might just assume it was the last FIND, but it could
//        ; have been anything)
//     ]
//
// Giving back NULL instead of an error means the situation can be handled
// precisely with operations like ELSE or ALSO, or just converted to a VOID
// to continue the chain.  Converting NULL to VOID is done with MAYBE.
//
REBTYPE(Unit)
{
    Value* val = D_ARG(1);
    assert(not Is_Nulled(val));

    if (not Is_Void(val) and not Is_Blank(val))
        fail (Error_Invalid(val));

    switch (Cell_Word_Id(verb)) {

    // !!! The category of "non-mutating type actions" should be knowable via
    // some meta information.  Any new such actions should get the behavior
    // of returning void, while any mutating actions return errors.

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value)); // covered by val above

        // !!! If reflectors had specs the way actions do, it might be that
        // the return type could be searched to see if void was an option,
        // and that would mean it would be legal.  For now, carry over ad
        // hoc things that R3-Alpha returned BLANK! for.

        switch (Cell_Word_Id(ARG(property))) {
        case SYM_INDEX:
        case SYM_LENGTH:
            return nullptr;

        default:
            break;
        }
        break; }

    case SYM_SELECT:
    case SYM_FIND:
    case SYM_SKIP:
    case SYM_AT:
    case SYM_TAKE:
        return nullptr;

    case SYM_COPY:
        if (Is_Blank(val))
            return Init_Blank(OUT);
        return nullptr;

    default:
        break;
    }

    fail (Error_Illegal_Action(VAL_TYPE(val), verb));
}


//
//  CT_Handle: C
//
REBINT CT_Handle(const Cell* a, const Cell* b, REBINT mode)
{
    // Would it be meaningful to allow user code to compare HANDLE!?
    //
    UNUSED(a);
    UNUSED(b);
    UNUSED(mode);

    fail ("Currently comparing HANDLE! types is not allowed.");
}


//
//  MF_Handle: C
//
void MF_Handle(Molder* mo, const Cell* v, bool form)
{
    // Value has no printable form, so just print its name.

    if (form)
        Emit(mo, "?T?", v);
    else
        Emit(mo, "+T", v);
}


//
// REBTYPE: C
//
REBTYPE(Handle)
{
    UNUSED(level_);

    fail (Error_Illegal_Action(REB_HANDLE, verb));
}
