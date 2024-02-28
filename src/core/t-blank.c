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
// a null out... for e.g. `make object! try select data spec else [...]`
//
REB_R MAKE_Unit(Value* out, enum Reb_Kind kind, const Value* arg) {
    UNUSED(out);
    fail (Error_Bad_Make(kind, arg));
}


//
//  TO_Unit: C
//
// TO is disallowed, e.g. you can't TO convert an integer of 0 to a blank.
//
REB_R TO_Unit(Value* out, enum Reb_Kind kind, const Value* data) {
    UNUSED(out);
    fail (Error_Bad_Make(kind, data));
}


//
//  MF_Unit: C
//
void MF_Unit(REB_MOLD *mo, const Cell* v, bool form)
{
    UNUSED(form); // no distinction between MOLD and FORM

    switch (VAL_TYPE(v)) {
    case REB_BAR:
        Append_Unencoded(mo->series, "|");
        break;

    case REB_LIT_BAR:
        Append_Unencoded(mo->series, "'|");
        break;

    case REB_BLANK:
        Append_Unencoded(mo->series, "_");
        break;

    case REB_VOID:
        //
        // !!! VOID! values are new, and no literal notation for them has been
        // decided yet.  One difference from things like BAR! and BLANK! is
        // that they would not be amenable to use for "stringlike" purposes,
        // as they are conditionally neither true nor false and can't be
        // assigned directly via SET-WORD! or plain SET...so choosing a
        // notation like ??? (or ?, or !) would be slippery.
        //
        Append_Unencoded(mo->series, "#[void]");
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
REB_R PD_Blank(
    REBPVS *pvs,
    const Value* picker,
    const Value* opt_setval
){
    UNUSED(picker);
    UNUSED(pvs);

    if (opt_setval != NULL)
        return R_UNHANDLED;

    return nullptr;
}


//
//  REBTYPE: C
//
// Asking to read a property of a BLANK! value is handled as a "light"
// failure, in the sense that it just returns void.  Returning void instead
// of blank helps establish error locality in chains of operations:
//
//     if not find select next first x [
//        ;
//        ; If blanks propagated too far, what actually went wrong, here?
//        ; (reader might just assume it was the last FIND, but it could
//        ; have been anything)
//     ]
//
// Giving back void instead of an error means the situation can be handled
// precisely with operations like ELSE or ALSO, or just converted to a BLANK!
// to continue the chain.  Historically this conversion was done with TO-VALUE
// but is proposed to use TRY.
//
REBTYPE(Unit)
{
    Value* val = D_ARG(1);
    assert(not IS_NULLED(val));

    switch (VAL_WORD_SYM(verb)) {

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

        switch (VAL_WORD_SYM(ARG(property))) {
        case SYM_INDEX:
        case SYM_LENGTH:
            return nullptr;

        default:
            break;
        }
        break; }

    case SYM_SELECT:
    case SYM_FIND:
    case SYM_COPY:
    case SYM_SKIP:
    case SYM_AT:
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
void MF_Handle(REB_MOLD *mo, const Cell* v, bool form)
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
    UNUSED(frame_);

    fail (Error_Illegal_Action(REB_HANDLE, verb));
}
