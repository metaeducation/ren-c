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
REBINT Cmp_Event(noquote(Cell(const*)) t1, noquote(Cell(const*)) t2)
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
REBINT CT_Event(noquote(Cell(const*)) a, noquote(Cell(const*)) b, bool strict)
{
    UNUSED(strict);
    return Cmp_Event(a, b);
}



//
//  Set_Event_Var: C
//
static bool Set_Event_Var(REBVAL *event, Cell(const*) word, const REBVAL *val)
{
    switch (VAL_WORD_ID(word)) {
      case SYM_TYPE: {
        //
        // !!! Rather limiting symbol-to-integer transformation for event
        // type, based on R3-Alpha-era optimization ethos.

        if (not IS_WORD(val))
            return false;

        option(SymId) id = VAL_WORD_ID(val);
        if (not id)  // !!! ...but for now, only symbols
            fail ("EVENT! only takes types that are compile-time symbols");

        SET_VAL_EVENT_TYPE(event, unwrap(id));
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
        return false;

      case SYM_OFFSET:
        if (Is_Nulled(val)) {  // use null to unset the coordinates
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
            option(SymId) sym = VAL_WORD_ID(val);  // ...has to be symbol
            if (not sym)
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

        Cell(const*) tail;
        Cell(const*) item = VAL_ARRAY_AT(&tail, val);
        for (; item != tail; ++item) {
            if (not IS_WORD(item))
                continue;

            switch (VAL_WORD_ID(item)) {
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
                fail (Error_Bad_Value(item));
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
// !!! R3-Alpha's EVENT! was a kind of compressed object.  Hence when you would
// say `make event! [type: 'something ...]` there wasn't a normal way of
// binding the TYPE SET-WORD! to a cell.  This routine was a hacky way of
// walking across the spec block and filling the event fields without running
// the evaluator, since it wouldn't know what to do with the SET-WORD!s.
//
// (As with GOB! this code is all factored out and slated for removal, but kept
// working to study whether the desires have better answers in new mechanisms.)
//
void Set_Event_Vars(
    REBVAL *evt,
    Cell(const*) block,
    REBSPC *specifier
){
    DECLARE_LOCAL (var);
    DECLARE_LOCAL (val);

    Cell(const*) tail;
    Cell(const*) item = VAL_ARRAY_AT(&tail, block);
    while (item != tail) {
        if (IS_COMMA(item)) {
            ++item;
            continue;
        }

        Derelativize(var, item, specifier);
        ++item;

        if (not IS_SET_WORD(var))
            fail (var);

        if (item == tail)
            fail (Error_Need_Non_End_Raw(var));

        if (
            IS_WORD(item) or IS_GET_WORD(item)
            or IS_TUPLE(item) or IS_GET_TUPLE(item)
        ){
            Get_Var_May_Fail(val, item, specifier, false);
            if (IS_ACTION(val))
                fail ("MAKE EVENT! evaluation is limited; can't run ACTION!s");
        }
        else if (IS_QUOTED(item)) {
            Derelativize(val, item, specifier);
            Unquotify(val, 1);
        }
        else if (ANY_INERT(item))
            Derelativize(val, item, specifier);
        else
            fail ("MAKE EVENT! evaluation is limited; simple references only");

        ++item;

        if (!Set_Event_Var(evt, var, val))
            fail (Error_Bad_Field_Set_Raw(var, Type_Of(val)));
    }
}


//
//  Get_Event_Var: C
//
// Will return BLANK! if the variable is not available.
//
static REBVAL *Get_Event_Var(
    Value(*) out,
    noquote(Cell(const*)) v,
    Symbol(const*) symbol
){
    switch (ID_OF_SYMBOL(symbol)) {
      case SYM_TYPE: {
        if (VAL_EVENT_TYPE(v) == SYM_NONE)  // !!! Should this ever happen?
            return nullptr;

        SymId typesym = VAL_EVENT_TYPE(v);
        return Init_Word(out, Canon_Symbol(typesym)); }

      case SYM_PORT: {
        if (VAL_EVENT_MODEL(v) == EVM_GUI)  // "most events are for the GUI"
            return Init_None(out);  // !!! No applicable port at present

        if (VAL_EVENT_MODEL(v) == EVM_PORT)
            return Init_Port(out, CTX(VAL_EVENT_NODE(v)));

        if (VAL_EVENT_MODEL(v) == EVM_OBJECT)
            return Init_Object(out, CTX(VAL_EVENT_NODE(v)));

        assert(VAL_EVENT_MODEL(v) == EVM_CALLBACK);
        return Copy_Cell(out, Get_System(SYS_PORTS, PORTS_CALLBACK)); }

      case SYM_WINDOW: {
        return nullptr; }

      case SYM_OFFSET: {
        if (not (VAL_EVENT_FLAGS(v) & EVF_HAS_XY))
            return nullptr;

        return Init_Pair_Int(out, VAL_EVENT_X(v), VAL_EVENT_Y(v)); }

      case SYM_KEY: {
        if (VAL_EVENT_TYPE(v) != SYM_KEY and VAL_EVENT_TYPE(v) != SYM_KEY_UP)
            return nullptr;

        if (VAL_EVENT_KEYSYM(v) != SYM_0)
            return Init_Word(out, Canon_Symbol(VAL_EVENT_KEYSYM(v)));

        Context(*) error = Maybe_Init_Char(out, VAL_EVENT_KEYCODE(v));
        if (error)
            fail (error);
        return out; }

      case SYM_FLAGS:
        if (
            (VAL_EVENT_FLAGS(v) & (EVF_DOUBLE | EVF_CONTROL | EVF_SHIFT)) != 0
        ){
            Array(*) arr = Make_Array(3);

            if (VAL_EVENT_FLAGS(v) & EVF_DOUBLE)
                Init_Word(Alloc_Tail_Array(arr), Canon(DOUBLE));

            if (VAL_EVENT_FLAGS(v) & EVF_CONTROL)
                Init_Word(Alloc_Tail_Array(arr), Canon(CONTROL));

            if (VAL_EVENT_FLAGS(v) & EVF_SHIFT)
                Init_Word(Alloc_Tail_Array(arr), Canon(SHIFT));

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

            SET_VAL_EVENT_NODE(writable, Copy_Bytes(cast(Byte*, str), -1));
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
Bounce MAKE_Event(
    Frame(*) frame_,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    assert(kind == REB_EVENT);
    UNUSED(kind);

    if (parent) {  // faster shorthand for COPY and EXTEND
        if (not IS_BLOCK(arg))
            fail (Error_Bad_Make(REB_EVENT, arg));

        Copy_Cell(OUT, unwrap(parent));  // !!! "shallow" event clone
        Set_Event_Vars(OUT, arg, VAL_SPECIFIER(arg));
        return OUT;
    }

    if (not IS_BLOCK(arg))
        fail (Error_Unexpected_Type(REB_EVENT, VAL_TYPE(arg)));

    Reset_Unquoted_Header_Untracked(TRACK(OUT), CELL_MASK_EVENT);
    INIT_VAL_NODE1(OUT, nullptr);
    SET_VAL_EVENT_TYPE(OUT, SYM_NONE);  // SYM_0 shouldn't be used
    mutable_VAL_EVENT_FLAGS(OUT) = EVF_MASK_NONE;
    mutable_VAL_EVENT_MODEL(OUT) = EVM_PORT;  // ?

    Set_Event_Vars(OUT, arg, VAL_SPECIFIER(arg));
    return OUT;
}


//
//  TO_Event: C
//
Bounce TO_Event(Frame(*) frame_, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_EVENT);
    UNUSED(kind);

    return RAISE(arg);
}


//
//  REBTYPE: C
//
REBTYPE(Event)
{
    REBVAL *event = D_ARG(1);

    option(SymId) id = ID_OF_SYMBOL(verb);

    if (id == SYM_PICK_P) {

    //=//// PICK* (see %sys-pick.h for explanation) ////////////////////////=//

        INCLUDE_PARAMS_OF_PICK_P;
        UNUSED(ARG(location));

        Cell(const*) picker = ARG(picker);
        if (not IS_WORD(picker))
            return BOUNCE_UNHANDLED;

        Get_Event_Var(OUT, event, VAL_WORD_SYMBOL(picker));
        return OUT;
    }
    else if (id == SYM_POKE_P) {

    //=//// PICK* (see %sys-pick.h for explanation) ////////////////////////=//

        INCLUDE_PARAMS_OF_POKE_P;
        UNUSED(ARG(location));

        Cell(const*) picker = ARG(picker);
        if (not IS_WORD(picker))
            return BOUNCE_UNHANDLED;

        REBVAL *setval = ARG(value);
        if (!Set_Event_Var(event, picker, setval))
            return BOUNCE_UNHANDLED;

        // This is a case where the bits are stored in the cell, so
        // whoever owns this cell has to write it back.
        //
        return COPY(event);
    }

    return BOUNCE_UNHANDLED;
}


//
//  MF_Event: C
//
void MF_Event(REB_MOLD *mo, noquote(Cell(const*)) v, bool form)
{
    UNUSED(form);

    REBLEN field;
    option(SymId) fields[] = {
        SYM_TYPE, SYM_PORT, SYM_OFFSET, SYM_KEY,
        SYM_FLAGS, SYM_CODE, SYM_DATA, SYM_0
    };

    Pre_Mold(mo, v);
    Append_Codepoint(mo->series, '[');
    mo->indent++;

    DECLARE_LOCAL (var); // declare outside loop (has init code)

    for (field = 0; fields[field] != 0; field++) {
        if (not Get_Event_Var(var, v, Canon_Symbol(unwrap(fields[field]))))
            continue;

        New_Indented_Line(mo);

        String(const*) canon = Canon_Symbol(unwrap(fields[field]));
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
