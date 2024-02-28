//
//  File: %t-event.c
//  Summary: "event datatype"
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
// Events are kept compact in order to fit into normal 128 bit
// values cells. This provides high performance for high frequency
// events and also good memory efficiency using standard series.
//

#include "sys-core.h"


//
//  CT_Event: C
//
REBINT CT_Event(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    REBINT diff = Cmp_Event(a, b);
    if (mode >=0) return diff == 0;
    return -1;
}


//
//  Cmp_Event: C
//
// Given two events, compare them.
//
REBINT Cmp_Event(const RELVAL *t1, const RELVAL *t2)
{
    REBINT  diff;

    if (
           (diff = VAL_EVENT_MODEL(t1) - VAL_EVENT_MODEL(t2))
        || (diff = VAL_EVENT_TYPE(t1) - VAL_EVENT_TYPE(t2))
        || (diff = VAL_EVENT_XY(t1) - VAL_EVENT_XY(t2))
    ) return diff;

    return 0;
}


//
//  Set_Event_Var: C
//
static bool Set_Event_Var(REBVAL *event, const REBVAL *word, const REBVAL *val)
{
    switch (VAL_WORD_SYM(word)) {
    case SYM_TYPE: {
        if (!IS_WORD(val) && !IS_LIT_WORD(val))
            return false;
        OPT_REBSYM id = VAL_WORD_SYM(val);
        if (id == SYM_0)
            return false;
        VAL_EVENT_TYPE(event) = id;
        return true; }

    case SYM_PORT:
        if (IS_PORT(val)) {
            VAL_EVENT_MODEL(event) = EVM_PORT;
            VAL_EVENT_SER(event) = SER(CTX_VARLIST(VAL_CONTEXT(val)));
        }
        else if (IS_OBJECT(val)) {
            VAL_EVENT_MODEL(event) = EVM_OBJECT;
            VAL_EVENT_SER(event) = SER(CTX_VARLIST(VAL_CONTEXT(val)));
        }
        else
            return false;
        break;

    case SYM_CODE:
        if (IS_INTEGER(val)) {
            VAL_EVENT_DATA(event) = VAL_INT32(val);
        }
        else
            return false;
        break;

    default:
        return false;
    }

    return true;
}


//
//  Set_Event_Vars: C
//
void Set_Event_Vars(REBVAL *evt, RELVAL *blk, REBSPC *specifier)
{
    DECLARE_LOCAL (var);
    DECLARE_LOCAL (val);

    while (NOT_END(blk)) {
        Derelativize(var, blk, specifier);
        ++blk;

        if (not IS_SET_WORD(var))
            fail (Error_Invalid(var));

        if (IS_END(blk))
            Init_Blank(val);
        else
            Get_Simple_Value_Into(val, blk, specifier);

        ++blk;

        if (!Set_Event_Var(evt, var, val))
            fail (Error_Bad_Field_Set_Raw(var, Type_Of(val)));
    }
}


//
//  Get_Event_Var: C
//
// Will return BLANK! if the variable is not available.
//
static REBVAL *Get_Event_Var(RELVAL *out, const RELVAL *v, REBSTR *name)
{
    switch (STR_SYMBOL(name)) {
    case SYM_TYPE: {
        if (VAL_EVENT_TYPE(v) == 0)
            return Init_Blank(out);

        return Init_Word(out, Canon(cast(REBSYM, VAL_EVENT_TYPE(v)))); }

    case SYM_PORT: {
        if (IS_EVENT_MODEL(v, EVM_PORT))
            return Init_Port(out, CTX(VAL_EVENT_SER(v)));

        if (IS_EVENT_MODEL(v, EVM_OBJECT))
            return Init_Object(out, CTX(VAL_EVENT_SER(v)));

        if (IS_EVENT_MODEL(v, EVM_CALLBACK))
            return Move_Value(out, Get_System(SYS_PORTS, PORTS_CALLBACK));

        assert(IS_EVENT_MODEL(v, EVM_DEVICE)); // holds IO request w/PORT!
        REBREQ *req = VAL_EVENT_REQ(v);
        if (not req or not req->port_ctx)
            return Init_Blank(out);

        return Init_Port(out, CTX(req->port_ctx)); }

    default:
        return Init_Blank(out);
    }
}


//
//  MAKE_Event: C
//
REB_R MAKE_Event(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    assert(kind == REB_EVENT);
    UNUSED(kind);

    if (not IS_BLOCK(arg))
        fail (Error_Unexpected_Type(REB_EVENT, VAL_TYPE(arg)));

    RESET_CELL(out, REB_EVENT);
    Set_Event_Vars(
        out,
        VAL_ARRAY_AT(arg),
        VAL_SPECIFIER(arg)
    );
    return out;
}


//
//  TO_Event: C
//
REB_R TO_Event(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_EVENT);
    UNUSED(kind);

    UNUSED(out);
    fail (Error_Invalid(arg));
}


//
//  PD_Event: C
//
REB_R PD_Event(
    REBPVS *pvs,
    const REBVAL *picker,
    const REBVAL *opt_setval
){
    if (IS_WORD(picker)) {
        if (opt_setval == NULL) {
            if (IS_BLANK(Get_Event_Var(
                pvs->out, pvs->out, VAL_WORD_CANON(picker)
            ))){
                return R_UNHANDLED;
            }

            return pvs->out;
        }
        else {
            if (!Set_Event_Var(pvs->out, picker, opt_setval))
                return R_UNHANDLED;

            return R_INVISIBLE;
        }
    }

    return R_UNHANDLED;
}


//
//  REBTYPE: C
//
REBTYPE(Event)
{
    UNUSED(frame_);

    fail (Error_Illegal_Action(REB_EVENT, verb));
}


//
//  MF_Event: C
//
void MF_Event(REB_MOLD *mo, const RELVAL *v, bool form)
{
    UNUSED(form);

    REBCNT field;
    REBSYM fields[] = {
        SYM_TYPE, SYM_PORT, SYM_0
    };

    Pre_Mold(mo, v);
    Append_Utf8_Codepoint(mo->series, '[');
    mo->indent++;

    DECLARE_LOCAL (var); // declare outside loop (has init code)

    for (field = 0; fields[field] != SYM_0; field++) {
        Get_Event_Var(var, v, Canon(fields[field]));
        if (IS_BLANK(var))
            continue;

        New_Indented_Line(mo);

        REBSTR *canon = Canon(fields[field]);
        Append_Utf8_Utf8(mo->series, STR_HEAD(canon), STR_SIZE(canon));
        Append_Unencoded(mo->series, ": ");
        if (IS_WORD(var))
            Append_Utf8_Codepoint(mo->series, '\'');
        Mold_Value(mo, var);
    }

    mo->indent--;
    New_Indented_Line(mo);
    Append_Utf8_Codepoint(mo->series, ']');

    End_Mold(mo);
}
