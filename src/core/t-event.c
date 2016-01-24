/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  t-event.c
**  Summary: event datatype
**  Section: datatypes
**  Author:  Carl Sassenrath
**  Notes:
**    Events are kept compact in order to fit into normal 128 bit
**    values cells. This provides high performance for high frequency
**    events and also good memory efficiency using standard series.
**
***********************************************************************/

#include "sys-core.h"
#include "reb-evtypes.h"
#include "reb-net.h"


//
//  CT_Event: C
//
REBINT CT_Event(const REBVAL *a, const REBVAL *b, REBINT mode)
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
REBINT Cmp_Event(const REBVAL *t1, const REBVAL *t2)
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
static REBOOL Set_Event_Var(REBVAL *value, const REBVAL *word, const REBVAL *val)
{
    REBVAL *arg;
    REBINT n;
    REBCNT w;

    switch (VAL_WORD_CANON(word)) {

    case SYM_TYPE:
        if (!IS_WORD(val) && !IS_LIT_WORD(val)) return FALSE;
        arg = Get_System(SYS_VIEW, VIEW_EVENT_TYPES);
        if (IS_BLOCK(arg)) {
            w = VAL_WORD_CANON(val);
            for (n = 0, arg = VAL_ARRAY_HEAD(arg); NOT_END(arg); arg++, n++) {
                if (IS_WORD(arg) && VAL_WORD_CANON(arg) == w) {
                    VAL_EVENT_TYPE(value) = n;
                    return TRUE;
                }
            }
            fail (Error_Invalid_Arg(val));
        }
        return FALSE;

    case SYM_PORT:
        if (IS_PORT(val)) {
            VAL_EVENT_MODEL(value) = EVM_PORT;
            VAL_EVENT_SER(value) = ARR_SERIES(CTX_VARLIST(VAL_CONTEXT(val)));
        }
        else if (IS_OBJECT(val)) {
            VAL_EVENT_MODEL(value) = EVM_OBJECT;
            VAL_EVENT_SER(value) = ARR_SERIES(CTX_VARLIST(VAL_CONTEXT(val)));
        }
        else if (IS_NONE(val)) {
            VAL_EVENT_MODEL(value) = EVM_GUI;
        } else return FALSE;
        break;

    case SYM_WINDOW:
    case SYM_GOB:
        if (IS_GOB(val)) {
            VAL_EVENT_MODEL(value) = EVM_GUI;
            VAL_EVENT_SER(value) = cast(REBSER*, VAL_GOB(val));
            break;
        }
        return FALSE;

    case SYM_OFFSET:
        if (IS_PAIR(val)) {
            SET_EVENT_XY(value, Float_Int16(VAL_PAIR_X(val)), Float_Int16(VAL_PAIR_Y(val)));
        }
        else return FALSE;
        break;

    case SYM_KEY:
        //VAL_EVENT_TYPE(value) != EVT_KEY && VAL_EVENT_TYPE(value) != EVT_KEY_UP)
        VAL_EVENT_MODEL(value) = EVM_GUI;
        if (IS_CHAR(val)) {
            VAL_EVENT_DATA(value) = VAL_CHAR(val);
        }
        else if (IS_LIT_WORD(val) || IS_WORD(val)) {
            arg = Get_System(SYS_VIEW, VIEW_EVENT_KEYS);
            if (IS_BLOCK(arg)) {
                arg = VAL_ARRAY_AT(arg);
                for (n = VAL_INDEX(arg); NOT_END(arg); n++, arg++) {
                    if (IS_WORD(arg) && VAL_WORD_CANON(arg) == VAL_WORD_CANON(val)) {
                        VAL_EVENT_DATA(value) = (n+1) << 16;
                        break;
                    }
                }
                if (IS_END(arg)) return FALSE;
                break;
            }
            return FALSE;
        }
        else return FALSE;
        break;

    case SYM_CODE:
        if (IS_INTEGER(val)) {
            VAL_EVENT_DATA(value) = VAL_INT32(val);
        }
        else return FALSE;
        break;

    case SYM_FLAGS:
        if (IS_BLOCK(val)) {
            VAL_EVENT_FLAGS(value) &= ~(1<<EVF_DOUBLE | 1<<EVF_CONTROL | 1<<EVF_SHIFT);
            for (val = VAL_ARRAY_HEAD(val); NOT_END(val); val++)
                if (IS_WORD(val))
                    switch (VAL_WORD_CANON(val)) {
                        case SYM_CONTROL:
                            SET_FLAG(VAL_EVENT_FLAGS(value), EVF_CONTROL);
                            break;
                        case SYM_SHIFT:
                            SET_FLAG(VAL_EVENT_FLAGS(value), EVF_SHIFT);
                            break;
                        case SYM_DOUBLE:
                            SET_FLAG(VAL_EVENT_FLAGS(value), EVF_DOUBLE);
                            break;
                    }
        }
        else return FALSE;
        break;

    default:
            return FALSE;
    }

    return TRUE;
}


//
//  Set_Event_Vars: C
//
static void Set_Event_Vars(REBVAL *evt, REBVAL *blk)
{
    REBVAL *var;
    const REBVAL *val;

    while (NOT_END(blk)) {
        REBVAL safe;
        VAL_INIT_WRITABLE_DEBUG(&safe);

        var = blk++;
        val = blk++;
        if (IS_END(val))
            val = NONE_VALUE;
        else {
            Get_Simple_Value_Into(&safe, val);
            val = &safe;
        }
        if (!Set_Event_Var(evt, var, val))
            fail (Error(RE_BAD_FIELD_SET, var, Type_Of(val)));
    }
}


//
//  Get_Event_Var: C
//
static REBOOL Get_Event_Var(const REBVAL *value, REBSYM sym, REBVAL *val)
{
    REBVAL *arg;
    REBREQ *req;
    REBINT n;

    switch (sym) {

    case SYM_TYPE:
        if (VAL_EVENT_TYPE(value) == 0) goto is_none;
        arg = Get_System(SYS_VIEW, VIEW_EVENT_TYPES);
        if (IS_BLOCK(arg) && VAL_LEN_HEAD(arg) >= EVT_MAX) {
            *val = *VAL_ARRAY_AT_HEAD(arg, VAL_EVENT_TYPE(value));
            break;
        }
        return FALSE;

    case SYM_PORT:
        // Most events are for the GUI:
        if (IS_EVENT_MODEL(value, EVM_GUI)) {
            *val = *Get_System(SYS_VIEW, VIEW_EVENT_PORT);
        }
        // Event holds a port:
        else if (IS_EVENT_MODEL(value, EVM_PORT)) {
            Val_Init_Port(
                val, AS_CONTEXT(VAL_EVENT_SER(m_cast(REBVAL*, value)))
            );
        }
        // Event holds an object:
        else if (IS_EVENT_MODEL(value, EVM_OBJECT)) {
            Val_Init_Object(
                val, AS_CONTEXT(VAL_EVENT_SER(m_cast(REBVAL*, value)))
            );
        }
        else if (IS_EVENT_MODEL(value, EVM_CALLBACK)) {
            *val = *Get_System(SYS_PORTS, PORTS_CALLBACK);
        }
        else {
            // assumes EVM_DEVICE
            // Event holds the IO-Request, which has the PORT:
            req = VAL_EVENT_REQ(value);
            if (!req || !req->port) goto is_none;
            Val_Init_Port(val, AS_CONTEXT(cast(REBSER*, req->port)));
        }
        break;

    case SYM_WINDOW:
    case SYM_GOB:
        if (IS_EVENT_MODEL(value, EVM_GUI)) {
            if (VAL_EVENT_SER(m_cast(REBVAL*, value))) {
                SET_GOB(val, cast(REBGOB*, VAL_EVENT_SER(m_cast(REBVAL*, value))));
                break;
            }
        }
        return FALSE;

    case SYM_OFFSET:
        if (VAL_EVENT_TYPE(value) == EVT_KEY || VAL_EVENT_TYPE(value) == EVT_KEY_UP)
            goto is_none;
        VAL_RESET_HEADER(val, REB_PAIR);
        VAL_PAIR_X(val) = (REBD32)VAL_EVENT_X(value);
        VAL_PAIR_Y(val) = (REBD32)VAL_EVENT_Y(value);
        break;

    case SYM_KEY:
        if (VAL_EVENT_TYPE(value) != EVT_KEY && VAL_EVENT_TYPE(value) != EVT_KEY_UP)
            goto is_none;
        n = VAL_EVENT_DATA(value); // key-words in top 16, chars in lower 16
        if (n & 0xffff0000) {
            arg = Get_System(SYS_VIEW, VIEW_EVENT_KEYS);
            n = (n >> 16) - 1;
            if (IS_BLOCK(arg) && n < cast(REBINT, VAL_LEN_HEAD(arg))) {
                *val = *VAL_ARRAY_AT_HEAD(arg, n);
                break;
            }
            return FALSE;
        }
        SET_CHAR(val, n);
        break;

    case SYM_FLAGS:
        if (
            VAL_EVENT_FLAGS(value)
            & (1<<EVF_DOUBLE | 1<<EVF_CONTROL | 1<<EVF_SHIFT)
        ) {
            REBARR *array = Make_Array(3);

            if (GET_FLAG(VAL_EVENT_FLAGS(value), EVF_DOUBLE))
                Val_Init_Word(Alloc_Tail_Array(array), REB_WORD, SYM_DOUBLE);

            if (GET_FLAG(VAL_EVENT_FLAGS(value), EVF_CONTROL))
                Val_Init_Word(Alloc_Tail_Array(array), REB_WORD, SYM_CONTROL);

            if (GET_FLAG(VAL_EVENT_FLAGS(value), EVF_SHIFT))
                Val_Init_Word(Alloc_Tail_Array(array), REB_WORD, SYM_SHIFT);

            Val_Init_Block(val, array);
        }
        else
            SET_NONE(val);
        break;

    case SYM_CODE:
        if (VAL_EVENT_TYPE(value) != EVT_KEY && VAL_EVENT_TYPE(value) != EVT_KEY_UP)
            goto is_none;
        n = VAL_EVENT_DATA(value); // key-words in top 16, chars in lower 16
        SET_INTEGER(val, n);
        break;

    case SYM_DATA:
        // Event holds a file string:
        if (VAL_EVENT_TYPE(value) != EVT_DROP_FILE) goto is_none;
        if (!GET_FLAG(VAL_EVENT_FLAGS(value), EVF_COPIED)) {
            // !!! EVIL mutability cast !!!
            // This should be done a better way.  What's apparently going on
            // is that VAL_EVENT_SER is supposed to be a FILE! series
            // for a string, but some client is allowed to put an ordinary
            // OS_ALLOC'd string of bytes into the field.  If a flag
            // doesn't tell us that it's "copied" and hence holds that
            // string form, it gets turned into a series "on-demand" even
            // in const-like contexts.  (So VAL_EVENT_SER is what C++ would
            // consider a "mutable" field.)  No way to tell C that, though.

            void *str = VAL_EVENT_SER(m_cast(REBVAL*, value));
            VAL_EVENT_SER(m_cast(REBVAL*, value)) = Copy_Bytes(cast(REBYTE*, str), -1);
            SET_FLAG(VAL_EVENT_FLAGS(m_cast(REBVAL*, value)), EVF_COPIED);
            OS_FREE(str);
        }
        Val_Init_File(val, VAL_EVENT_SER(m_cast(REBVAL*, value)));
        break;

    default:
        return FALSE;
    }

    return TRUE;

is_none:
    SET_NONE(val);
    return TRUE;
}


//
//  MT_Event: C
//
REBOOL MT_Event(REBVAL *out, REBVAL *data, enum Reb_Kind type)
{
    if (IS_BLOCK(data)) {
        CLEARS(out);
        Set_Event_Vars(out, VAL_ARRAY_AT(data));
        VAL_RESET_HEADER(out, REB_EVENT);
        return TRUE;
    }

    return FALSE;
}


//
//  PD_Event: C
//
REBINT PD_Event(REBPVS *pvs)
{
    if (IS_WORD(pvs->select)) {
        if (pvs->setval == 0 || NOT_END(pvs->path+1)) {
            if (!Get_Event_Var(pvs->value, VAL_WORD_CANON(pvs->select), pvs->store)) return PE_BAD_SELECT;
            return PE_USE;
        } else {
            if (!Set_Event_Var(pvs->value, pvs->select, pvs->setval)) return PE_BAD_SET;
            return PE_OK;
        }
    }
    return PE_BAD_SELECT;
}


//
//  REBTYPE: C
//
REBTYPE(Event)
{
    REBVAL *value;
    REBVAL *arg;

    value = D_ARG(1);
    arg = D_ARG(2);

    if (action == A_MAKE) {
        // Clone an existing event?
        if (IS_EVENT(value)) {
            *D_OUT = *D_ARG(1);
            return R_OUT;
        }
        else if (IS_DATATYPE(value)) {
            if (IS_EVENT(arg)) {
                *D_OUT = *D_ARG(2);
                return R_OUT;
            }
            //fail (Error_Bad_Make(REB_EVENT, value));
            VAL_RESET_HEADER(D_OUT, REB_EVENT);
            CLEARS(&(D_OUT->payload.event));
        }
        else
is_arg_error:
            fail (Error_Unexpected_Type(REB_EVENT, VAL_TYPE(arg)));

        // Initialize GOB from block:
        if (IS_BLOCK(arg)) Set_Event_Vars(D_OUT, VAL_ARRAY_AT(arg));
        else goto is_arg_error;
    }
    else
        fail (Error_Illegal_Action(REB_EVENT, action));

    return R_OUT;
}

#ifdef ndef
//  case A_PATH:
        if (IS_WORD(arg)) {
            switch (VAL_WORD_CANON(arg)) {
            case SYM_TYPE:    index = EF_TYPE; break;
            case SYM_PORT:    index = EF_PORT; break;
            case SYM_KEY:     index = EF_KEY; break;
            case SYM_OFFSET:  index = EF_OFFSET; break;
            case SYM_MODE:    index = EF_MODE; break;
            case SYM_TIME:    index = EF_TIME; break;
//!!! return these as options flags, not refinements.
//          case SYM_SHIFT:   index = EF_SHIFT; break;
//          case SYM_CONTROL: index = EF_CONTROL; break;
//          case SYM_DOUBLE_CLICK: index = EF_DCLICK; break;
            default: fail (Error(RE_INVALID_PATH, arg));
            }
            goto pick_it;
        }
        else if (!IS_INTEGER(arg))
            fail (Error(RE_INVALID_PATH, arg));
        // fall thru


    case A_PICK:
        index = num = Get_Num_Arg(arg);
        if (num > 0) index--;
        if (num == 0 || index < 0 || index > EF_DCLICK) {
            if (action == A_POKE) fail (Error_Out_Of_Range(arg));
            goto is_none;
        }
pick_it:
        switch(index) {
        case EF_TYPE:
            if (VAL_EVENT_TYPE(value) == 0) goto is_none;
            arg = Get_System(SYS_VIEW, VIEW_EVENT_TYPES);
            if (IS_BLOCK(arg) && VAL_LEN_HEAD(arg) >= EVT_MAX) {
                *D_OUT = *VAL_ARRAY_AT_HEAD(arg, VAL_EVENT_TYPE(value));
                return R_OUT;
            }
            return R_NONE;

        case EF_PORT:
            // Most events are for the GUI:
            if (GET_FLAG(VAL_EVENT_FLAGS(value), EVF_NO_REQ))
                *D_OUT = *Get_System(SYS_VIEW, VIEW_EVENT_PORT);
            else {
                req = VAL_EVENT_REQ(value);
                if (!req || !req->port) goto is_none;
                Val_Init_Port(D_OUT, cast(REBSER*, req->port));
            }
            return R_OUT;

        case EF_KEY:
            if (VAL_EVENT_TYPE(value) != EVT_KEY) goto is_none;
            if (VAL_EVENT_FLAGS(value)) {  // !!!!!!!!!!!!! needs mask
                VAL_RESET_HEADER(D_OUT, REB_CHAR);
                VAL_CHAR(D_OUT) = VAL_EVENT_KEY(value) & 0xff;
            } else
                Val_Init_Word_Bound(D_OUT, VAL_EVENT_XY(value));
            return R_OUT;

        case EF_OFFSET:
            VAL_RESET_HEADER(D_OUT, REB_PAIR);
            VAL_PAIR_X(D_OUT) = VAL_EVENT_X(value);
            VAL_PAIR_Y(D_OUT) = VAL_EVENT_Y(value);
            return R_OUT;

        case EF_TIME:
            VAL_RESET_HEADER(D_OUT, REB_INTEGER);
//!!            VAL_INT64(D_OUT) = VAL_EVENT_TIME(value);
            return R_OUT;

        case EF_SHIFT:
            VAL_RESET_HEADER(D_OUT, REB_LOGIC);
            SET_LOGIC(D_OUT, GET_FLAG(VAL_EVENT_FLAGS(value), EVF_SHIFT) != 0));
            return R_OUT;

        case EF_CONTROL:
            VAL_RESET_HEADER(D_OUT, REB_LOGIC);
            SET_LOGIC(D_OUT, GET_FLAG(VAL_EVENT_FLAGS(value), EVF_CONTROL) != 0);
            return R_OUT;

        case EF_DCLICK:
            VAL_RESET_HEADER(D_OUT, REB_LOGIC);
            SET_LOGIC(D_OUT, GET_FLAG(VAL_EVENT_FLAGS(value), EVF_DOUBLE) != 0);
            return R_OUT;

/*          case EF_FACE:
            {
                REBWIN  *wp;
                // !!! Used to say 'return R_OUT None_Value;', but as this is
                // commented out code it's not possible to determine what that
                // exactly was supposed to have done.

                if (!IS_BLOCK(ARR_HEAD(Windows) + VAL_EVENT_WIN(value))) return R_OUT;
                wp = cast(REBWIN *, VAL_ARRAY_HEAD(ARR_HEAD(Windows) + VAL_EVENT_WIN(value)));
                *D_OUT = wp->masterFace;
                return R_OUT;
            }
*/
        }
        break;

// These are used to map symbols to event field cases:
enum rebol_event_fields {
    EF_TYPE,
    EF_KEY,
    EF_OFFSET,
    EF_TIME,
    EF_SHIFT,   // Keep these? !!!
    EF_CONTROL,
    EF_DCLICK,
    EF_PORT,
    EF_MODE,
};

#endif


//
//  Mold_Event: C
//
void Mold_Event(const REBVAL *value, REB_MOLD *mold)
{
    REBVAL val;
    REBCNT field;
    REBCNT fields[] = {
        SYM_TYPE, SYM_PORT, SYM_GOB, SYM_OFFSET, SYM_KEY,
        SYM_FLAGS, SYM_CODE, SYM_DATA, 0
    };

    Pre_Mold(value, mold);
    Append_Codepoint_Raw(mold->series, '[');
    mold->indent++;

    for (field = 0; fields[field]; field++) {
        Get_Event_Var(value, fields[field], &val);
        if (!IS_NONE(&val)) {
            New_Indented_Line(mold);
            Append_UTF8(mold->series, Get_Sym_Name(fields[field]), -1);
            Append_Unencoded(mold->series, ": ");
            if (IS_WORD(&val)) Append_Codepoint_Raw(mold->series, '\'');
            Mold_Value(mold, &val, TRUE);
        }
    }

    mold->indent--;
    New_Indented_Line(mold);
    Append_Codepoint_Raw(mold->series, ']');

    End_Mold(mold);
}

