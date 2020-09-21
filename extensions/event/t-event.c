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
// Copyright 2012-2019 Ren-C Open Source Contributors
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
// See %extensions/event/README.md
//

#include "sys-core.h"
#include "reb-event.h"


//
//  Cmp_Event: C
//
// Given two events, compare them.
//
// !!! Like much of the comprarison code in R3-Alpha, this isn't very good.
// It doesn't check key codes, doesn't check if EVF_HAS_XY but still compares
// the x and y coordinates anyway...
//
REBINT Cmp_Event(REBCEL(const*) t1, REBCEL(const*) t2)
{
    REBINT  diff;

    if (
           (diff = VAL_EVENT_MODEL(t1) - VAL_EVENT_MODEL(t2))
        || (diff = VAL_EVENT_TYPE(t1) - VAL_EVENT_TYPE(t2))
        || (diff = VAL_EVENT_X(t1) - VAL_EVENT_X(t2))
        || (diff = VAL_EVENT_Y(t1) - VAL_EVENT_Y(t2))
    ) return diff;

    return 0;
}


//
//  CT_Event: C
//
REBINT CT_Event(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    UNUSED(strict);
    return Cmp_Event(a, b);
}



//
//  Set_Event_Var: C
//
static bool Set_Event_Var(REBVAL *event, const RELVAL *word, const REBVAL *val)
{
    switch (VAL_WORD_SYM(word)) {
      case SYM_TYPE: {
        //
        // !!! Rather limiting symbol-to-integer transformation for event
        // type, based on R3-Alpha-era optimization ethos.

        if (not IS_WORD(val) and not IS_QUOTED_WORD(val))
            return false;

        REBSYM type_sym = VAL_WORD_SYM(val);

        RELVAL *typelist = Get_System(SYS_VIEW, VIEW_EVENT_TYPES);
        assert(IS_BLOCK(typelist));
        UNUSED(typelist);  // We now support a wider range of words...

        if (type_sym == SYM_0)  // !!! ...but for now, only symbols
            fail ("EVENT! only takes types that are compile-time symbols");

        SET_VAL_EVENT_TYPE(event, type_sym);
        return true; }

      case SYM_PORT:
        if (IS_PORT(val)) {
            mutable_VAL_EVENT_MODEL(event) = EVM_PORT;
            SET_VAL_EVENT_NODE(event, CTX_VARLIST(VAL_CONTEXT(val)));
        }
        else if (IS_OBJECT(val)) {
            mutable_VAL_EVENT_MODEL(event) = EVM_OBJECT;
            SET_VAL_EVENT_NODE(event, CTX_VARLIST(VAL_CONTEXT(val)));
        }
        else if (IS_BLANK(val)) {
            mutable_VAL_EVENT_MODEL(event) = EVM_GUI;
            SET_VAL_EVENT_NODE(event, nullptr);
        }
        else
            return false;
        break;

      case SYM_WINDOW:
      case SYM_GOB:
        if (rebDid("gob?", val, rebEND)) {  // optimized to extract GOB's node
            mutable_VAL_EVENT_MODEL(event) = EVM_GUI;
            SET_VAL_EVENT_NODE(event, VAL_GOB(val));
            break;
        }
        return false;

      case SYM_OFFSET:
        if (IS_NULLED(val)) {  // use null to unset the coordinates
            mutable_VAL_EVENT_FLAGS(event) &= ~EVF_HAS_XY;
          #if !defined(NDEBUG)
            SET_VAL_EVENT_X(event, 1020);
            SET_VAL_EVENT_Y(event, 304);
          #endif
            return true;
        }

        if (not IS_PAIR(val))  // historically seems to have only taken PAIR!
            return false;

        mutable_VAL_EVENT_FLAGS(event) |= EVF_HAS_XY;
        SET_VAL_EVENT_X(event, VAL_PAIR_X_INT(val));
        SET_VAL_EVENT_Y(event, VAL_PAIR_Y_INT(val));
        return true;

      case SYM_KEY:
        mutable_VAL_EVENT_MODEL(event) = EVM_GUI;
        if (IS_CHAR(val)) {
            SET_VAL_EVENT_KEYCODE(event, VAL_CHAR(val));
            SET_VAL_EVENT_KEYSYM(event, SYM_NONE);
        }
        else if (IS_WORD(val) or IS_QUOTED_WORD(val)) {
            RELVAL *event_keys = Get_System(SYS_VIEW, VIEW_EVENT_KEYS);
            assert(IS_BLOCK(event_keys));
            UNUSED(event_keys);  // we can use any key name, but...

            REBSYM sym = VAL_WORD_SYM(val);  // ...has to be symbol (for now)
            if (sym == SYM_0)
                fail ("EVENT! only takes keys that are compile-time symbols");

            SET_VAL_EVENT_KEYSYM(event, sym);
            SET_VAL_EVENT_KEYCODE(event, 0);  // should this be set?
            return true;
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

      case SYM_FLAGS: {
        if (not IS_BLOCK(val))
            return false;

        mutable_VAL_EVENT_FLAGS(event)
            &= ~(EVF_DOUBLE | EVF_CONTROL | EVF_SHIFT);

        const RELVAL *item;
        for (item = ARR_HEAD(VAL_ARRAY(val)); NOT_END(item); ++item) {
            if (not IS_WORD(item))
                continue;

            switch (VAL_WORD_SYM(item)) {
            case SYM_CONTROL:
                mutable_VAL_EVENT_FLAGS(event) |= EVF_CONTROL;
                break;

            case SYM_SHIFT:
                mutable_VAL_EVENT_FLAGS(event) |= EVF_SHIFT;
                break;

            case SYM_DOUBLE:
                mutable_VAL_EVENT_FLAGS(event) |= EVF_DOUBLE;
                break;

            default:
                fail (Error_Bad_Value_Core(item, VAL_SPECIFIER(val)));
            }
        }
        break; }

      default:
        return false;
    }

    return true;
}


//
//  Set_Event_Vars: C
//
void Set_Event_Vars(REBVAL *evt, const RELVAL *head, REBSPC *specifier)
{
    DECLARE_LOCAL (var);
    DECLARE_LOCAL (val);

    while (NOT_END(head)) {
        Derelativize(var, head, specifier);
        ++head;

        if (not IS_SET_WORD(var))
            fail (var);

        if (IS_END(head))
            Init_Blank(val);
        else
            Get_Simple_Value_Into(val, head, specifier);

        ++head;

        if (!Set_Event_Var(evt, var, val))
            fail (Error_Bad_Field_Set_Raw(var, Type_Of(val)));
    }
}


//
//  Get_Event_Var: C
//
// Will return BLANK! if the variable is not available.
//
static REBVAL *Get_Event_Var(RELVAL *out, REBCEL(const*) v, REBSTR *name)
{
    switch (STR_SYMBOL(name)) {
      case SYM_TYPE: {
        if (VAL_EVENT_TYPE(v) == SYM_NONE)  // !!! Should this ever happen?
            return nullptr;

        REBSYM typesym = VAL_EVENT_TYPE(v);
        return Init_Word(out, Canon(typesym)); }

      case SYM_PORT: {
        if (VAL_EVENT_MODEL(v) == EVM_GUI)  // "most events are for the GUI"
            return Move_Value(out, Get_System(SYS_VIEW, VIEW_EVENT_PORT));

        if (VAL_EVENT_MODEL(v) == EVM_PORT)
            return Init_Port(out, CTX(VAL_EVENT_NODE(v)));

        if (VAL_EVENT_MODEL(v) == EVM_OBJECT)
            return Init_Object(out, CTX(VAL_EVENT_NODE(v)));

        if (VAL_EVENT_MODEL(v) == EVM_CALLBACK)
            return Move_Value(out, Get_System(SYS_PORTS, PORTS_CALLBACK));

        assert(VAL_EVENT_MODEL(v) == EVM_DEVICE);  // holds IO request w/PORT!
        REBREQ *req = cast(REBREQ*, VAL_EVENT_NODE(v));
        if (not req or not ReqPortCtx(req))
            return nullptr;

        return Init_Port(out, CTX(ReqPortCtx(req))); }

      case SYM_WINDOW:
      case SYM_GOB: {
        if (VAL_EVENT_MODEL(v) == EVM_GUI) {
            if (VAL_EVENT_NODE(v))
                return Init_Gob(out, cast(REBGOB*, VAL_EVENT_NODE(v)));
        }
        return nullptr; }

      case SYM_OFFSET: {
        if (not (VAL_EVENT_FLAGS(v) & EVF_HAS_XY))
            return nullptr;

        return Init_Pair_Int(out, VAL_EVENT_X(v), VAL_EVENT_Y(v)); }

      case SYM_KEY: {
        if (VAL_EVENT_TYPE(v) != SYM_KEY and VAL_EVENT_TYPE(v) != SYM_KEY_UP)
            return nullptr;

        if (VAL_EVENT_KEYSYM(v) != SYM_0)
            return Init_Word(out, Canon(VAL_EVENT_KEYSYM(v)));

        return Init_Char_May_Fail(out, VAL_EVENT_KEYCODE(v)); }

      case SYM_FLAGS:
        if (
            (VAL_EVENT_FLAGS(v) & (EVF_DOUBLE | EVF_CONTROL | EVF_SHIFT)) != 0
        ){
            REBARR *arr = Make_Array(3);

            if (VAL_EVENT_FLAGS(v) & EVF_DOUBLE)
                Init_Word(Alloc_Tail_Array(arr), Canon(SYM_DOUBLE));

            if (VAL_EVENT_FLAGS(v) & EVF_CONTROL)
                Init_Word(Alloc_Tail_Array(arr), Canon(SYM_CONTROL));

            if (VAL_EVENT_FLAGS(v) & EVF_SHIFT)
                Init_Word(Alloc_Tail_Array(arr), Canon(SYM_SHIFT));

            return Init_Block(out, arr);
        }
        return nullptr;

      case SYM_CODE: {
        if (VAL_EVENT_TYPE(v) != SYM_KEY and VAL_EVENT_TYPE(v) != SYM_KEY_UP)
            return nullptr;

        return Init_Integer(out, VAL_EVENT_KEYCODE(v)); }

      case SYM_DATA: {  // Event holds a FILE!'s string
        if (VAL_EVENT_TYPE(v) != SYM_DROP_FILE)
            return nullptr;

        if (not (VAL_EVENT_FLAGS(v) & EVF_COPIED)) {
            void *str = VAL_EVENT_NODE(v);  // !!! can only store nodes!

            // !!! This modifies a const-marked values's bits, which
            // is generally a bad thing.  The reason it appears to be doing
            // this is to let clients can put ordinary malloc'd arrays of
            // bytes into a field which are then on-demand turned into
            // string series when seen here.  This flips a bit to say the
            // conversion has been done.  Review this implementation.
            //
            REBVAL *writable = m_cast(REBVAL*, SPECIFIC(CELL_TO_VAL(v)));

            SET_VAL_EVENT_NODE(writable, Copy_Bytes(cast(REBYTE*, str), -1));
            mutable_VAL_EVENT_FLAGS(writable) |= EVF_COPIED;

            free(str);
        }
        return Init_File(out, STR(VAL_EVENT_NODE(v))); }

      default:
        return nullptr;
    }
}


//
//  MAKE_Event: C
//
REB_R MAKE_Event(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    assert(kind == REB_EVENT);
    UNUSED(kind);

    if (opt_parent) {  // faster shorthand for COPY and EXTEND
        if (not IS_BLOCK(arg))
            fail (Error_Bad_Make(REB_EVENT, arg));

        Move_Value(out, opt_parent);  // !!! "shallow" clone of the event
        Set_Event_Vars(out, VAL_ARRAY_AT(arg), VAL_SPECIFIER(arg));
        return out;
    }

    if (not IS_BLOCK(arg))
        fail (Error_Unexpected_Type(REB_EVENT, VAL_TYPE(arg)));

    RESET_CELL(out, REB_EVENT, CELL_FLAG_FIRST_IS_NODE);
    INIT_VAL_NODE(out, nullptr);
    SET_VAL_EVENT_TYPE(out, SYM_NONE);  // SYM_0 shouldn't be used
    mutable_VAL_EVENT_FLAGS(out) = EVF_MASK_NONE;
    mutable_VAL_EVENT_MODEL(out) = EVM_PORT;  // ?

    Set_Event_Vars(out, VAL_ARRAY_AT(arg), VAL_SPECIFIER(arg));
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
    fail (arg);
}


//
//  PD_Event: C
//
REB_R PD_Event(
    REBPVS *pvs,
    const RELVAL *picker,
    const REBVAL *opt_setval
){
    if (IS_WORD(picker)) {
        if (opt_setval == NULL) {
            return Get_Event_Var(
                pvs->out, pvs->out, VAL_WORD_CANON(picker)
            );
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
    UNUSED(verb);

    return R_UNHANDLED;
}


//
//  MF_Event: C
//
void MF_Event(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    UNUSED(form);

    REBLEN field;
    REBSYM fields[] = {
        SYM_TYPE, SYM_PORT, SYM_GOB, SYM_OFFSET, SYM_KEY,
        SYM_FLAGS, SYM_CODE, SYM_DATA, SYM_0
    };

    Pre_Mold(mo, v);
    Append_Codepoint(mo->series, '[');
    mo->indent++;

    DECLARE_LOCAL (var); // declare outside loop (has init code)

    for (field = 0; fields[field] != SYM_0; field++) {
        if (not Get_Event_Var(var, v, Canon(fields[field])))
            continue;

        New_Indented_Line(mo);

        REBSTR *canon = Canon(fields[field]);
        Append_Utf8(mo->series, STR_UTF8(canon), STR_SIZE(canon));
        Append_Ascii(mo->series, ": ");
        if (IS_WORD(var))
            Append_Codepoint(mo->series, '\'');
        Mold_Value(mo, var);
    }

    mo->indent--;
    New_Indented_Line(mo);
    Append_Codepoint(mo->series, ']');

    End_Mold(mo);
}

