//
//  file: %t-event.c
//  summary: "event datatype"
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
// Events are kept compact in order to fit into normal 128 bit
// values cells. This provides high performance for high frequency
// events and also good memory efficiency using standard series.
//

#include "sys-core.h"


//
//  CT_Event: C
//
REBINT CT_Event(const Cell* a, const Cell* b, REBINT mode)
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
REBINT Cmp_Event(const Cell* t1, const Cell* t2)
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
static bool Set_Event_Var(Value* event, const Value* word, const Value* val)
{
    switch (Cell_Word_Id(word)) {
    case SYM_TYPE: {
        if (!Is_Word(val) && !Is_Lit_Word(val))
            return false;
        Option(SymId) id = Cell_Word_Id(val);
        if (id == SYM_0)
            return false;
        VAL_EVENT_TYPE(event) = id;
        return true; }

    case SYM_PORT:
        if (Is_Port(val)) {
            VAL_EVENT_MODEL(event) = EVM_PORT;
            VAL_EVENT_FLEX(event) = Varlist_Array(Cell_Varlist(val));
        }
        else if (Is_Object(val)) {
            VAL_EVENT_MODEL(event) = EVM_OBJECT;
            VAL_EVENT_FLEX(event) = Varlist_Array(Cell_Varlist(val));
        }
        else
            return false;
        break;

    case SYM_CODE:
        if (Is_Integer(val)) {
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
void Set_Event_Vars(Value* evt, Cell* blk, Specifier* specifier)
{
    DECLARE_VALUE (var);
    DECLARE_VALUE (val);

    while (NOT_END(blk)) {
        Derelativize(var, blk, specifier);
        ++blk;

        if (not Is_Set_Word(var))
            fail (Error_Invalid(var));

        if (IS_END(blk))
            Init_Blank(val);
        else
            Get_Simple_Value_Into(val, blk, specifier);

        ++blk;

        if (!Set_Event_Var(evt, var, val))
            fail (Error_Bad_Field_Set_Raw(var, Datatype_Of(val)));
    }
}


//
//  Get_Event_Var: C
//
// Will return BLANK! if the variable is not available.
//
static Value* Get_Event_Var(Cell* out, const Cell* v, Symbol* name)
{
    switch (Symbol_Id(name)) {
    case SYM_TYPE: {
        if (VAL_EVENT_TYPE(v) == 0)
            return Init_Blank(out);

        return Init_Word(out, Canon_From_Id(cast(SymId, VAL_EVENT_TYPE(v)))); }

    case SYM_PORT: {
        if (IS_EVENT_MODEL(v, EVM_PORT))
            return Init_Port(out, CTX(VAL_EVENT_FLEX(v)));

        if (IS_EVENT_MODEL(v, EVM_OBJECT))
            return Init_Object(out, CTX(VAL_EVENT_FLEX(v)));

        if (IS_EVENT_MODEL(v, EVM_CALLBACK))
            return Copy_Cell(out, Get_System(SYS_PORTS, PORTS_CALLBACK));

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
Bounce MAKE_Event(Value* out, enum Reb_Kind kind, const Value* arg) {
    assert(kind == TYPE_EVENT);
    UNUSED(kind);

    if (not Is_Block(arg))
        fail (Error_Unexpected_Type(TYPE_EVENT, Type_Of(arg)));

    RESET_CELL(out, TYPE_EVENT);
    Set_Event_Vars(
        out,
        Cell_List_At(arg),
        VAL_SPECIFIER(arg)
    );
    return out;
}


//
//  TO_Event: C
//
Bounce TO_Event(Value* out, enum Reb_Kind kind, const Value* arg)
{
    assert(kind == TYPE_EVENT);
    UNUSED(kind);

    UNUSED(out);
    fail (Error_Invalid(arg));
}


//
//  PD_Event: C
//
Bounce PD_Event(
    REBPVS *pvs,
    const Value* picker,
    const Value* opt_setval
){
    if (Is_Word(picker)) {
        if (opt_setval == nullptr) {
            if (Is_Blank(Get_Event_Var(
                pvs->out, pvs->out, VAL_WORD_CANON(picker)
            ))){
                return BOUNCE_UNHANDLED;
            }

            return pvs->out;
        }
        else {
            if (!Set_Event_Var(pvs->out, picker, opt_setval))
                return BOUNCE_UNHANDLED;

            return BOUNCE_INVISIBLE;
        }
    }

    return BOUNCE_UNHANDLED;
}


//
//  REBTYPE: C
//
REBTYPE(Event)
{
    UNUSED(level_);

    fail (Error_Illegal_Action(TYPE_EVENT, verb));
}


//
//  MF_Event: C
//
void MF_Event(Molder* mo, const Cell* v, bool form)
{
    UNUSED(form);

    REBLEN field;
    SymId fields[] = {
        SYM_TYPE, SYM_PORT, SYM_0_internal
    };

    Begin_Non_Lexical_Mold(mo, v);
    Append_Codepoint(mo->utf8flex, '[');
    mo->indent++;

    DECLARE_VALUE (var); // declare outside loop (has init code)

    for (field = 0; fields[field] != SYM_0; field++) {
        Get_Event_Var(var, v, Canon_From_Id(fields[field]));
        if (Is_Blank(var))
            continue;

        New_Indented_Line(mo);

        Symbol* canon = Canon_From_Id(fields[field]);
        Append_Utf8_Utf8(mo->utf8flex, Symbol_Head(canon), Symbol_Size(canon));
        Append_Unencoded(mo->utf8flex, ": ");
        if (Is_Word(var))
            Append_Codepoint(mo->utf8flex, '\'');
        Mold_Value(mo, var);
    }

    mo->indent--;
    New_Indented_Line(mo);
    Append_Codepoint(mo->utf8flex, ']');

    End_Non_Lexical_Mold(mo);
}
