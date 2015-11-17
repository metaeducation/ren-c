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
**  Module:  t-gob.c
**  Summary: graphical object datatype
**  Section: datatypes
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"

const REBCNT Gob_Flag_Words[] = {
    SYM_RESIZE,      GOBF_RESIZE,
    SYM_NO_TITLE,    GOBF_NO_TITLE,
    SYM_NO_BORDER,   GOBF_NO_BORDER,
    SYM_DROPABLE,    GOBF_DROPABLE,
    SYM_TRANSPARENT, GOBF_TRANSPARENT,
    SYM_POPUP,       GOBF_POPUP,
    SYM_MODAL,       GOBF_MODAL,
    SYM_ON_TOP,      GOBF_ON_TOP,
    SYM_HIDDEN,      GOBF_HIDDEN,
    SYM_ACTIVE,      GOBF_ACTIVE,
    SYM_MINIMIZE,    GOBF_MINIMIZE,
    SYM_MAXIMIZE,    GOBF_MAXIMIZE,
    SYM_RESTORE,     GOBF_RESTORE,
    SYM_FULLSCREEN,  GOBF_FULLSCREEN,
    0, 0
};


//
//  CT_Gob: C
//
REBINT CT_Gob(REBVAL *a, REBVAL *b, REBINT mode)
{
    if (mode >= 0)
        return VAL_GOB(a) == VAL_GOB(b) && VAL_GOB_INDEX(a) == VAL_GOB_INDEX(b);
    return -1;
}

//
//  Make_Gob: C
// 
// Allocate a new GOB.
//
REBGOB *Make_Gob(void)
{
    REBGOB *gob = cast(REBGOB*, Make_Node(GOB_POOL));
    CLEAR(gob, sizeof(REBGOB));
    GOB_W(gob) = 100;
    GOB_H(gob) = 100;
    GOB_ALPHA(gob) = 255;
    USE_GOB(gob);
    if ((GC_Ballast -= Mem_Pools[GOB_POOL].wide) <= 0) SET_SIGNAL(SIG_RECYCLE);
    return gob;
}


//
//  Cmp_Gob: C
//
REBINT Cmp_Gob(const REBVAL *g1, const REBVAL *g2)
{
    REBINT n;

    n = VAL_GOB(g2) - VAL_GOB(g1);
    if (n != 0) return n;
    n = VAL_GOB_INDEX(g2) - VAL_GOB_INDEX(g1);
    if (n != 0) return n;
    return 0;
}


//
//  Set_Pair: C
//
static REBFLG Set_Pair(REBXYF *pair, const REBVAL *val)
{
    if (IS_PAIR(val)) {
        pair->x = VAL_PAIR_X(val);
        pair->y = VAL_PAIR_Y(val);
    }
    else if (IS_INTEGER(val)) {
        pair->x = pair->y = (REBD32)VAL_INT64(val);
    }
    else if (IS_DECIMAL(val)) {
        pair->x = pair->y = (REBD32)VAL_DECIMAL(val);
    }
    else
        return FALSE;

    return TRUE;
}


//
//  Find_Gob: C
// 
// Find a target GOB within the pane of another gob.
// Return the index, or a -1 if not found.
//
static REBCNT Find_Gob(REBGOB *gob, REBGOB *target)
{
    REBCNT len;
    REBCNT n;
    REBGOB **ptr;

    if (GOB_PANE(gob)) {
        len = GOB_TAIL(gob);
        ptr = GOB_HEAD(gob);
        for (n = 0; n < len; n++, ptr++)
            if (*ptr == target) return n;
    }
    return NOT_FOUND;
}


//
//  Detach_Gob: C
// 
// Remove a gob value from its parent.
// Done normally in advance of inserting gobs into new parent.
//
static void Detach_Gob(REBGOB *gob)
{
    REBGOB *par;
    REBCNT i;

    par = GOB_PARENT(gob);
    if (par && GOB_PANE(par) && (i = Find_Gob(par, gob)) != NOT_FOUND) {
        Remove_Series(GOB_PANE(par), i, 1);
    }
    GOB_PARENT(gob) = 0;
}


//
//  Insert_Gobs: C
// 
// Insert one or more gobs into a pane at the given index.
// If index >= tail, an append occurs. Each gob has its parent
// gob field set. (Call Detach_Gobs() before inserting.)
//
static void Insert_Gobs(REBGOB *gob, const REBVAL *arg, REBCNT index, REBCNT len, REBFLG change)
{
    REBGOB **ptr;
    REBCNT n, count;
    const REBVAL *val;
    const REBVAL *sarg;
    REBINT i;

    // Verify they are gobs:
    sarg = arg;
    for (n = count = 0; n < len; n++, val++) {
        val = arg++;
        if (IS_WORD(val)) val = GET_VAR(val);
        if (IS_GOB(val)) {
            count++;
            if (GOB_PARENT(VAL_GOB(val))) {
                // Check if inserting into same parent:
                i = -1;
                if (GOB_PARENT(VAL_GOB(val)) == gob) {
                    i = Find_Gob(gob, VAL_GOB(val));
                    if (i > 0 && i == (REBINT)index-1) { // a no-op
                        SET_GOB_STATE(VAL_GOB(val), GOBS_NEW);
                        return;
                    }
                }
                Detach_Gob(VAL_GOB(val));
                if (i >= 0 && (REBINT)index > i) index--;
            }
        }
        else
            fail (Error_Invalid_Arg(val));
    }
    arg = sarg;

    // Create or expand the pane series:
    if (!GOB_PANE(gob)) {
        GOB_PANE(gob) = Make_Series(count + 1, sizeof(REBGOB*), MKS_NONE);
        LABEL_SERIES(GOB_PANE(gob), "gob pane");
        GOB_TAIL(gob) = count;
        index = 0;

        // !!! A GOB_PANE could theoretically be MKS_UNTRACKED and manually
        // memory managed, if that made sense.  Does it?

        MANAGE_SERIES(GOB_PANE(gob));
    }
    else {
        if (change) {
            if (index + count > GOB_TAIL(gob)) {
                EXPAND_SERIES_TAIL(GOB_PANE(gob), index + count - GOB_TAIL(gob));
            }
        } else {
            Expand_Series(GOB_PANE(gob), index, count);
            if (index >= GOB_TAIL(gob)) index = GOB_TAIL(gob)-1;
        }
    }

    ptr = GOB_SKIP(gob, index);
    for (n = 0; n < len; n++) {
        val = arg++;
        if (IS_WORD(val)) val = GET_VAR(val);
        if (IS_GOB(val)) {
            // !!! Temporary error of some kind (supposed to trap, not panic?)
            if (GOB_PARENT(VAL_GOB(val))) fail (Error(RE_MISC));
            *ptr++ = VAL_GOB(val);
            GOB_PARENT(VAL_GOB(val)) = gob;
            SET_GOB_STATE(VAL_GOB(val), GOBS_NEW);
        }
    }
}


//
//  Remove_Gobs: C
// 
// Remove one or more gobs from a pane at the given index.
//
static void Remove_Gobs(REBGOB *gob, REBCNT index, REBCNT len)
{
    REBGOB **ptr;
    REBCNT n;

    ptr = GOB_SKIP(gob, index);
    for (n = 0; n < len; n++, ptr++) {
        GOB_PARENT(*ptr) = 0;
    }

    Remove_Series(GOB_PANE(gob), index, len);
}


//
//  Pane_To_Block: C
// 
// Convert pane list of gob pointers to a block of GOB!s.
//
static REBSER *Pane_To_Block(REBGOB *gob, REBCNT index, REBINT len)
{
    REBSER *ser;
    REBGOB **gp;
    REBVAL *val;

    if (len == -1 || (len + index) > GOB_TAIL(gob)) len = GOB_TAIL(gob) - index;
    if (len < 0) len = 0;

    ser = Make_Array(len);
    ser->tail = len;
    val = BLK_HEAD(ser);
    gp = GOB_HEAD(gob);
    for (; len > 0; len--, val++, gp++) {
        SET_GOB(val, *gp);
    }
    SET_END(val);

    return ser;
}


//
//  Flags_To_Block: C
//
static REBSER *Flags_To_Block(REBGOB *gob)
{
    REBSER *ser;
    REBVAL *val;
    REBINT i;

    ser = Make_Array(3);

    for (i = 0; Gob_Flag_Words[i]; i += 2) {
        if (GET_GOB_FLAG(gob, Gob_Flag_Words[i+1])) {
            val = Alloc_Tail_Array(ser);
            Val_Init_Word_Unbound(val, REB_WORD, Gob_Flag_Words[i]);
        }
    }

    return ser;
}


//
//  Set_Gob_Flag: C
//
static void Set_Gob_Flag(REBGOB *gob, const REBVAL *word)
{
    REBINT i;

    for (i = 0; Gob_Flag_Words[i]; i += 2) {
        if (VAL_WORD_CANON(word) == Gob_Flag_Words[i]) {
            REBCNT flag = Gob_Flag_Words[i+1];
            SET_GOB_FLAG(gob, flag);
            //handle mutual exclusive states
            switch (flag) {
                case GOBF_RESTORE:
                    CLR_GOB_FLAGS(gob, GOBF_MINIMIZE, GOBF_MAXIMIZE);
                    CLR_GOB_FLAG(gob, GOBF_FULLSCREEN);
                    break;
                case GOBF_MINIMIZE:
                    CLR_GOB_FLAGS(gob, GOBF_MAXIMIZE, GOBF_RESTORE);
                    CLR_GOB_FLAG(gob, GOBF_FULLSCREEN);
                    break;
                case GOBF_MAXIMIZE:
                    CLR_GOB_FLAGS(gob, GOBF_MINIMIZE, GOBF_RESTORE);
                    CLR_GOB_FLAG(gob, GOBF_FULLSCREEN);
                    break;
                case GOBF_FULLSCREEN:
                    SET_GOB_FLAG(gob, GOBF_NO_TITLE);
                    SET_GOB_FLAG(gob, GOBF_NO_BORDER);
                    CLR_GOB_FLAGS(gob, GOBF_MINIMIZE, GOBF_RESTORE);
                    CLR_GOB_FLAG(gob, GOBF_MAXIMIZE);
            }
            break;
        }
    }
}


//
//  Set_GOB_Var: C
//
static REBFLG Set_GOB_Var(REBGOB *gob, const REBVAL *word, const REBVAL *val)
{
    switch (VAL_WORD_CANON(word)) {
    case SYM_OFFSET:
        return Set_Pair(&(gob->offset), val);

    case SYM_SIZE:
        return Set_Pair(&gob->size, val);

    case SYM_IMAGE:
        CLR_GOB_OPAQUE(gob);
        if (IS_IMAGE(val)) {
            SET_GOB_TYPE(gob, GOBT_IMAGE);
            GOB_W(gob) = (REBD32)VAL_IMAGE_WIDE(val);
            GOB_H(gob) = (REBD32)VAL_IMAGE_HIGH(val);
            GOB_CONTENT(gob) = VAL_SERIES(val);
//          if (!VAL_IMAGE_TRANSP(val)) SET_GOB_OPAQUE(gob);
        }
        else if (IS_NONE(val)) SET_GOB_TYPE(gob, GOBT_NONE);
        else return FALSE;
        break;

    case SYM_DRAW:
        CLR_GOB_OPAQUE(gob);
        if (IS_BLOCK(val)) {
            SET_GOB_TYPE(gob, GOBT_DRAW);
            GOB_CONTENT(gob) = VAL_SERIES(val);
        }
        else if (IS_NONE(val)) SET_GOB_TYPE(gob, GOBT_NONE);
        else return FALSE;
        break;

    case SYM_TEXT:
        CLR_GOB_OPAQUE(gob);
        if (IS_BLOCK(val)) {
            SET_GOB_TYPE(gob, GOBT_TEXT);
            GOB_CONTENT(gob) = VAL_SERIES(val);
        }
        else if (IS_STRING(val)) {
            SET_GOB_TYPE(gob, GOBT_STRING);
            GOB_CONTENT(gob) = VAL_SERIES(val);
        }
        else if (IS_NONE(val)) SET_GOB_TYPE(gob, GOBT_NONE);
        else return FALSE;
        break;

    case SYM_EFFECT:
        CLR_GOB_OPAQUE(gob);
        if (IS_BLOCK(val)) {
            SET_GOB_TYPE(gob, GOBT_EFFECT);
            GOB_CONTENT(gob) = VAL_SERIES(val);
        }
        else if (IS_NONE(val)) SET_GOB_TYPE(gob, GOBT_NONE);
        else return FALSE;
        break;

    case SYM_COLOR:
        CLR_GOB_OPAQUE(gob);
        if (IS_TUPLE(val)) {
            SET_GOB_TYPE(gob, GOBT_COLOR);
            Set_Pixel_Tuple((REBYTE*)&GOB_CONTENT(gob), val);
            if (VAL_TUPLE_LEN(val) < 4 || VAL_TUPLE(val)[3] == 0)
                SET_GOB_OPAQUE(gob);
        }
        else if (IS_NONE(val)) SET_GOB_TYPE(gob, GOBT_NONE);
        break;

    case SYM_PANE:
        if (GOB_PANE(gob)) Clear_Series(GOB_PANE(gob));
        if (IS_BLOCK(val))
            Insert_Gobs(gob, VAL_BLK_DATA(val), 0, VAL_BLK_LEN(val), 0);
        else if (IS_GOB(val))
            Insert_Gobs(gob, val, 0, 1, 0);
        else if (IS_NONE(val))
            gob->pane = 0;
        else
            return FALSE;
        break;

    case SYM_ALPHA:
        GOB_ALPHA(gob) = Clip_Int(Int32(val), 0, 255);
        break;

    case SYM_DATA:
        SET_GOB_DTYPE(gob, GOBD_NONE);
        if (IS_OBJECT(val)) {
            SET_GOB_DTYPE(gob, GOBD_OBJECT);
            SET_GOB_DATA(gob, VAL_OBJ_FRAME(val));
        }
        else if (IS_BLOCK(val)) {
            SET_GOB_DTYPE(gob, GOBD_BLOCK);
            SET_GOB_DATA(gob, VAL_SERIES(val));
        }
        else if (IS_STRING(val)) {
            SET_GOB_DTYPE(gob, GOBD_STRING);
            SET_GOB_DATA(gob, VAL_SERIES(val));
        }
        else if (IS_BINARY(val)) {
            SET_GOB_DTYPE(gob, GOBD_BINARY);
            SET_GOB_DATA(gob, VAL_SERIES(val));
        }
        else if (IS_INTEGER(val)) {
            SET_GOB_DTYPE(gob, GOBD_INTEGER);
            SET_GOB_DATA(gob, cast(REBSER*, cast(REBIPT, VAL_INT64(val))));
        }
        else if (IS_NONE(val))
            SET_GOB_TYPE(gob, GOBT_NONE);
        else return FALSE;
        break;

    case SYM_FLAGS:
        if (IS_WORD(val)) Set_Gob_Flag(gob, val);
        else if (IS_BLOCK(val)) {
            REBINT i;
            //clear only flags defined by words
            for (i = 0; Gob_Flag_Words[i]; i += 2)
                CLR_FLAG(gob->flags, Gob_Flag_Words[i+1]);

            for (val = VAL_BLK_HEAD(val); NOT_END(val); val++)
                if (IS_WORD(val)) Set_Gob_Flag(gob, val);
        }
        break;

    case SYM_OWNER:
        if (IS_GOB(val))
            GOB_TMP_OWNER(gob) = VAL_GOB(val);
        else
            return FALSE;
        break;

    default:
            return FALSE;
    }
    return TRUE;
}


//
//  Get_GOB_Var: C
//
static REBFLG Get_GOB_Var(REBGOB *gob, const REBVAL *word, REBVAL *val)
{
    switch (VAL_WORD_CANON(word)) {

    case SYM_OFFSET:
        SET_PAIR(val, GOB_X(gob), GOB_Y(gob));
        break;

    case SYM_SIZE:
        SET_PAIR(val, GOB_W(gob), GOB_H(gob));
        break;

    case SYM_IMAGE:
        if (GOB_TYPE(gob) == GOBT_IMAGE) {
            // image
        }
        else goto is_none;
        break;

    case SYM_DRAW:
        if (GOB_TYPE(gob) == GOBT_DRAW) {
            // !!! comment said "compiler optimizes" the init "calls below" (?)
            Val_Init_Block(val, GOB_CONTENT(gob));
        }
        else goto is_none;
        break;

    case SYM_TEXT:
        if (GOB_TYPE(gob) == GOBT_TEXT) {
            Val_Init_Block(val, GOB_CONTENT(gob));
        }
        else if (GOB_TYPE(gob) == GOBT_STRING) {
            Val_Init_String(val, GOB_CONTENT(gob));
        }
        else goto is_none;
        break;

    case SYM_EFFECT:
        if (GOB_TYPE(gob) == GOBT_EFFECT) {
            Val_Init_Block(val, GOB_CONTENT(gob));
        }
        else goto is_none;
        break;

    case SYM_COLOR:
        if (GOB_TYPE(gob) == GOBT_COLOR) {
            Set_Tuple_Pixel((REBYTE*)&GOB_CONTENT(gob), val);
        }
        else goto is_none;
        break;

    case SYM_ALPHA:
        SET_INTEGER(val, GOB_ALPHA(gob));
        break;

    case SYM_PANE:
        if (GOB_PANE(gob))
            Val_Init_Block(val, Pane_To_Block(gob, 0, -1));
        else
            Val_Init_Block(val, Make_Array(0));
        break;

    case SYM_PARENT:
        if (GOB_PARENT(gob)) {
            SET_GOB(val, GOB_PARENT(gob));
        }
        else
is_none:
            SET_NONE(val);
        break;

    case SYM_DATA:
        if (GOB_DTYPE(gob) == GOBD_OBJECT) {
            Val_Init_Object(val, GOB_DATA(gob));
        }
        else if (GOB_DTYPE(gob) == GOBD_BLOCK) {
            Val_Init_Block(val, GOB_DATA(gob));
        }
        else if (GOB_DTYPE(gob) == GOBD_STRING) {
            Val_Init_String(val, GOB_DATA(gob));
        }
        else if (GOB_DTYPE(gob) == GOBD_BINARY) {
            Val_Init_Binary(val, GOB_DATA(gob));
        }
        else if (GOB_DTYPE(gob) == GOBD_INTEGER) {
            SET_INTEGER(val, (REBIPT)GOB_DATA(gob));
        }
        else goto is_none;
        break;

    case SYM_FLAGS:
        Val_Init_Block(val, Flags_To_Block(gob));
        break;

    default:
        return FALSE;
    }
    return TRUE;
}


//
//  Set_GOB_Vars: C
//
static void Set_GOB_Vars(REBGOB *gob, const REBVAL *blk)
{
    const REBVAL *var;
    const REBVAL *val;

    while (NOT_END(blk)) {
        REBVAL safe;
        var = blk++;
        val = blk++;
        if (!IS_SET_WORD(var))
            fail (Error(RE_EXPECT_VAL, Get_Type(REB_SET_WORD), Type_Of(var)));
        if (IS_END(val) || IS_UNSET(val) || IS_SET_WORD(val))
            fail (Error(RE_NEED_VALUE, var));
        Get_Simple_Value_Into(&safe, val);
        if (!Set_GOB_Var(gob, var, val))
            fail (Error(RE_BAD_FIELD_SET, var, Type_Of(val)));
    }
}


//
//  Gob_To_Block: C
// 
// Used by MOLD to create a block.
//
REBSER *Gob_To_Block(REBGOB *gob)
{
    REBSER *ser = Make_Array(10);
    REBVAL *val;
    REBINT words[6] = {SYM_OFFSET, SYM_SIZE, SYM_ALPHA, 0};
    REBVAL *vals[6];
    REBINT n = 0;
    REBVAL *val1;
    REBCNT sym;

    for (n = 0; words[n]; n++) {
        val = Alloc_Tail_Array(ser);
        Val_Init_Word_Unbound(val, REB_SET_WORD, words[n]);
        vals[n] = Alloc_Tail_Array(ser);
        SET_NONE(vals[n]);
    }

    SET_PAIR(vals[0], GOB_X(gob), GOB_Y(gob));
    SET_PAIR(vals[1], GOB_W(gob), GOB_H(gob));
    SET_INTEGER(vals[2], GOB_ALPHA(gob));

    if (!GOB_TYPE(gob)) return ser;

    if (GOB_CONTENT(gob)) {
        val1 = Alloc_Tail_Array(ser);
        val = Alloc_Tail_Array(ser);
        switch (GOB_TYPE(gob)) {
        case GOBT_COLOR:
            sym = SYM_COLOR;
            break;
        case GOBT_IMAGE:
            sym = SYM_IMAGE;
            break;
        case GOBT_STRING:
        case GOBT_TEXT:
            sym = SYM_TEXT;
            break;
        case GOBT_DRAW:
            sym = SYM_DRAW;
            break;
        case GOBT_EFFECT:
            sym = SYM_EFFECT;
            break;
        }
        Val_Init_Word_Unbound(val1, REB_SET_WORD, sym);
        Get_GOB_Var(gob, val1, val);
    }

    return ser;
}


//
//  MT_Gob: C
//
REBFLG MT_Gob(REBVAL *out, REBVAL *data, enum Reb_Kind type)
{
    REBGOB *ngob;

    if (IS_BLOCK(data)) {
        ngob = Make_Gob();
        Set_GOB_Vars(ngob, VAL_BLK_DATA(data));
        SET_GOB(out, ngob);
        return TRUE;
    }

    return FALSE;
}


//
//  PD_Gob: C
//
REBINT PD_Gob(REBPVS *pvs)
{
    REBGOB *gob = VAL_GOB(pvs->value);
    REBCNT index;
    REBCNT tail;

    if (IS_WORD(pvs->select)) {
        if (pvs->setval == 0 || NOT_END(pvs->path+1)) {
            if (!Get_GOB_Var(gob, pvs->select, pvs->store)) return PE_BAD_SELECT;
            // Check for SIZE/X: types of cases:
            if (pvs->setval && IS_PAIR(pvs->store)) {
                REBVAL *sel = pvs->select;
                pvs->value = pvs->store;

                if (Next_Path_Throws(pvs)) { // sets value in pvs->store

                    // !!! Gob and Struct do "sub-dispatch" which may throw
                    // No "PE_THREW" return, however.  (should there be?)

                    fail (Error_No_Catch_For_Throw(pvs->store));
                }

                Set_GOB_Var(gob, sel, pvs->store); // write it back to gob
            }
            return PE_USE;
        } else {
            if (!Set_GOB_Var(gob, pvs->select, pvs->setval)) return PE_BAD_SET;
            return PE_OK;
        }
    }
    if (IS_INTEGER(pvs->select)) {
        if (!GOB_PANE(gob)) return PE_NONE;
        tail = GOB_PANE(gob) ? GOB_TAIL(gob) : 0;
        index = VAL_GOB_INDEX(pvs->value);
        index += Int32(pvs->select) - 1;
        if (index >= tail) return PE_NONE;
        gob = *GOB_SKIP(gob, index);
        VAL_SET(pvs->store, REB_GOB);
        VAL_GOB(pvs->store) = gob;
        VAL_GOB_INDEX(pvs->store) = 0;
        return PE_USE;
    }
    return PE_BAD_SELECT;
}


//
//  REBTYPE: C
//
REBTYPE(Gob)
{
    REBVAL *val = D_ARG(1);
    REBVAL *arg = DS_ARGC > 1 ? D_ARG(2) : NULL;
    REBGOB *gob = NULL;
    REBGOB *ngob;
    REBCNT index;
    REBCNT tail;
    REBCNT len;

    *D_OUT = *val;

    if (IS_GOB(val)) {
        gob = VAL_GOB(val);
        index = VAL_GOB_INDEX(val);
        tail = GOB_PANE(gob) ? GOB_TAIL(gob) : 0;
    }
    else if (!(IS_DATATYPE(val) && action == A_MAKE))
        fail (Error_Invalid_Arg(val));

    // unary actions
    switch(action) {

    case A_MAKE:
        ngob = Make_Gob();

        // Clone an existing GOB:
        if (IS_GOB(val)) {  // local variable "gob" is valid
            *ngob = *gob;   // Copy all values
            ngob->pane = 0;
            ngob->parent = 0;
        }
        else if (!IS_DATATYPE(val)) goto is_arg_error;

        // Initialize GOB from block:
        if (IS_BLOCK(arg)) {
            Set_GOB_Vars(ngob, VAL_BLK_DATA(arg));
        }
        // Merge GOB provided as argument:
        else if (IS_GOB(arg)) {
            *ngob = *VAL_GOB(arg);
            ngob->pane = 0;
            ngob->parent = 0;
        }
        else if (IS_PAIR(arg)) {
            ngob->size.x = VAL_PAIR_X(arg);
            ngob->size.y = VAL_PAIR_Y(arg);
        }
        else
            fail (Error_Bad_Make(REB_GOB, arg));
        // Allow NONE as argument:
//      else if (!IS_NONE(arg))
//          goto is_arg_error;
        SET_GOB(D_OUT, ngob);
        break;

    case A_PICK:
        if (!IS_NUMBER(arg) && !IS_NONE(arg)) fail (Error_Invalid_Arg(arg));
        if (!GOB_PANE(gob)) goto is_none;
        index += Get_Num_Arg(arg) - 1;
        if (index >= tail) goto is_none;
        gob = *GOB_SKIP(gob, index);
        index = 0;
        goto set_index;

    case A_POKE:
        index += Get_Num_Arg(arg) - 1;
        arg = D_ARG(3);
    case A_CHANGE:
        if (!IS_GOB(arg)) goto is_arg_error;
        if (!GOB_PANE(gob) || index >= tail) fail (Error(RE_PAST_END));
        if (
            action == A_CHANGE
            && (D_REF(AN_PART) || D_REF(AN_ONLY) || D_REF(AN_DUP))
        ) {
            fail (Error(RE_NOT_DONE));
        }
        Insert_Gobs(gob, arg, index, 1, 0);
        //ngob = *GOB_SKIP(gob, index);
        //GOB_PARENT(ngob) = 0;
        //*GOB_SKIP(gob, index) = VAL_GOB(arg);
        if (action == A_POKE) {
            *D_OUT = *arg;
            return R_OUT;
        }
        index++;
        goto set_index;

    case A_APPEND:
        index = tail;
    case A_INSERT:
        if (D_REF(AN_PART) || D_REF(AN_ONLY) || D_REF(AN_DUP))
            fail (Error(RE_NOT_DONE));
        if (IS_GOB(arg)) len = 1;
        else if (IS_BLOCK(arg)) {
            len = VAL_BLK_LEN(arg);
            arg = VAL_BLK_DATA(arg);
        }
        else goto is_arg_error;;
        Insert_Gobs(gob, arg, index, len, 0);
        break;

    case A_CLEAR:
        if (tail > index) Remove_Gobs(gob, index, tail - index);
        break;

    case A_REMOVE:
        // /PART length
        len = D_REF(2) ? Get_Num_Arg(D_ARG(3)) : 1;
        if (index + len > tail) len = tail - index;
        if (index < tail && len != 0) Remove_Gobs(gob, index, len);
        break;

    case A_TAKE:
        len = D_REF(2) ? Get_Num_Arg(D_ARG(3)) : 1;
        if (index + len > tail) len = tail - index;
        if (index >= tail) goto is_none;
        if (!D_REF(2)) { // just one value
            VAL_SET(D_OUT, REB_GOB);
            VAL_GOB(D_OUT) = *GOB_SKIP(gob, index);
            VAL_GOB_INDEX(D_OUT) = 0;
            Remove_Gobs(gob, index, 1);
            return R_OUT;
        } else {
            Val_Init_Block(D_OUT, Pane_To_Block(gob, index, len));
            Remove_Gobs(gob, index, len);
        }
        return R_OUT;

    case A_NEXT:
        if (index < tail) index++;
        goto set_index;

    case A_BACK:
        if (index > 0) index--;
        goto set_index;

    case A_AT:
        index--;
    case A_SKIP:
        index += VAL_INT32(arg);
        goto set_index;

    case A_HEAD:
        index = 0;
        goto set_index;

    case A_TAIL:
        index = tail;
        goto set_index;

    case A_HEADQ:
        if (index == 0) goto is_true;
        goto is_false;

    case A_TAILQ:
        if (index >= tail) goto is_true;
        goto is_false;

    case A_PASTQ:
        if (index > tail) goto is_true;
        goto is_false;

    case A_INDEX_OF:
        SET_INTEGER(D_OUT, index + 1);
        break;

    case A_LENGTH:
        index = (tail > index) ? tail - index : 0;
        SET_INTEGER(D_OUT, index);
        break;

    case A_FIND:
        if (IS_GOB(arg)) {
            index = Find_Gob(gob, VAL_GOB(arg));
            if (index == NOT_FOUND) goto is_none;
            goto set_index;
        }
        goto is_none;

    case A_REVERSE:
        for (index = 0; index < tail/2; index++) {
            ngob = *GOB_SKIP(gob, tail-index-1);
            *GOB_SKIP(gob, tail-index-1) = *GOB_SKIP(gob, index);
            *GOB_SKIP(gob, index) = ngob;
        }
        return R_ARG1;

    default:
        fail (Error_Illegal_Action(REB_GOB, action));
    }
    return R_OUT;

set_index:
    VAL_SET(D_OUT, REB_GOB);
    VAL_GOB(D_OUT) = gob;
    VAL_GOB_INDEX(D_OUT) = index;
    return R_OUT;

is_none:
    return R_NONE;

is_arg_error:
    fail (Error_Unexpected_Type(REB_GOB, VAL_TYPE(arg)));

is_false:
    return R_FALSE;

is_true:
    return R_TRUE;
}
