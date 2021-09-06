//
//  File: %t-gob.c
//  Summary: "graphical object datatype"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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

#include "reb-gob.h"

const struct {
    SYMID sym;
    uintptr_t flags;
} Gob_Flag_Words[] = {
    {SYM_RESIZE,      GOBF_RESIZE},
    {SYM_NO_TITLE,    GOBF_NO_TITLE},
    {SYM_NO_BORDER,   GOBF_NO_BORDER},
    {SYM_DROPABLE,    GOBF_DROPABLE},
    {SYM_TRANSPARENT, GOBF_TRANSPARENT},
    {SYM_POPUP,       GOBF_POPUP},
    {SYM_MODAL,       GOBF_MODAL},
    {SYM_ON_TOP,      GOBF_ON_TOP},
    {SYM_HIDDEN,      GOBF_HIDDEN},
    {SYM_ACTIVE,      GOBF_ACTIVE},
    {SYM_MINIMIZE,    GOBF_MINIMIZE},
    {SYM_MAXIMIZE,    GOBF_MAXIMIZE},
    {SYM_RESTORE,     GOBF_RESTORE},
    {SYM_FULLSCREEN,  GOBF_FULLSCREEN},
    {SYM_0, 0}
};


//
//  CT_Gob: C
//
REBINT CT_Gob(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    UNUSED(strict);
    if (VAL_GOB(a) != VAL_GOB(b))
        return VAL_GOB(a) > VAL_GOB(b) ? 1 : -1;  // !!! For sorting?
    if (VAL_GOB_INDEX(a) != VAL_GOB_INDEX(b))
        return VAL_GOB_INDEX(a) > VAL_GOB_INDEX(b) ? 1 : -1;
    return 0;
}

//
//  Make_Gob: C
//
// Creates a REBARR* which contains a compact representation of information
// describing a GOB!.  Does not include the GOB's index, which is unique to
// each GOB! value and lives in the cell's payload.
//
REBGOB *Make_Gob(void)
{
    REBGOB *a = Make_Array_Core(
        IDX_GOB_MAX,
        FLAG_FLAVOR(GOBLIST)
            | SERIES_FLAG_FIXED_SIZE
            | SERIES_FLAG_LINK_NODE_NEEDS_MARK
            | SERIES_FLAG_MISC_NODE_NEEDS_MARK
    );

    SET_GOB_PARENT(a, nullptr);  // in LINK(), is a REBNOD*, GC must mark
    SET_GOB_OWNER(a, nullptr);  // in MISC(), is a REBNOD*, GC must mark

    Init_Blank(ARR_AT(a, IDX_GOB_PANE));
    Init_Blank(ARR_AT(a, IDX_GOB_CONTENT));
    Init_Blank(ARR_AT(a, IDX_GOB_DATA));

    Init_XYF(ARR_AT(a, IDX_GOB_OFFSET_AND_FLAGS), 100, 100);  // !!! Why 100?
    GOB_FLAGS(a) = 0;

    Init_XYF(ARR_AT(a, IDX_GOB_SIZE_AND_ALPHA), 0, 0);
    GOB_ALPHA(a) = 255;

    Init_XYF(ARR_AT(a, IDX_GOB_OLD_OFFSET), 0, 0);

    Init_XYF(ARR_AT(a, IDX_GOB_TYPE_AND_OLD_SIZE), 0, 0);
    GOB_TYPE(a) = GOBT_NONE;

    SET_SERIES_LEN(a, IDX_GOB_MAX);
    return a;  // REBGOB is-a REBARR
}


//
//  Cmp_Gob: C
//
REBINT Cmp_Gob(REBCEL(const*) g1, REBCEL(const*) g2)
{
    REBINT n;

    n = VAL_GOB(g2) - VAL_GOB(g1);
    if (n != 0) return n;
    n = VAL_GOB_INDEX(g2) - VAL_GOB_INDEX(g1);
    if (n != 0) return n;
    return 0;
}


//
//  Did_Set_XYF: C
//
static bool Did_Set_XYF(RELVAL *xyf, const REBVAL *val)
{
    if (IS_PAIR(val)) {
        VAL_XYF_X(xyf) = VAL_PAIR_X_DEC(val);
        VAL_XYF_Y(xyf) = VAL_PAIR_Y_DEC(val);
    }
    else if (IS_INTEGER(val)) {
        VAL_XYF_X(xyf) = VAL_XYF_Y(xyf) = cast(REBD32, VAL_INT64(val));
    }
    else if (IS_DECIMAL(val)) {
        VAL_XYF_X(xyf) = VAL_XYF_Y(xyf) = cast(REBD32, VAL_DECIMAL(val));
    }
    else
        return false;

    return true;
}


//
//  Find_Gob: C
//
// Find a target GOB within the pane of another gob.
// Return the index, or a -1 if not found.
//
static REBLEN Find_Gob(REBGOB *gob, REBGOB *target)
{
    if (not GOB_PANE(gob))
        return NOT_FOUND;

    REBLEN len = GOB_LEN(gob);
    REBVAL *item = GOB_HEAD(gob);

    REBLEN n;
    for (n = 0; n < len; ++n, ++item)
        if (VAL_GOB(item) == target)
            return n;

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
    REBGOB *par = GOB_PARENT(gob);
    if (not par)
        return;

    if (GOB_PANE(par)) {
        REBLEN i = Find_Gob(par, gob);
        if (i != NOT_FOUND)
            Remove_Series_Units(GOB_PANE(par), i, 1);
        else
            assert(!"Detaching GOB from parent that didn't find it"); // !!! ?
    }

    SET_GOB_PARENT(gob, nullptr);
}


//
//  Insert_Gobs: C
//
// Insert one or more gobs into a pane at the given index.
// If index >= tail, an append occurs. Each gob has its parent
// gob field set. (Call Detach_Gobs() before inserting.)
//
static void Insert_Gobs(
    REBGOB *gob,
    const RELVAL *arg,
    REBLEN index,
    REBLEN len,
    bool change
) {
    REBLEN n, count;
    const RELVAL *val;
    const RELVAL *sarg;
    REBINT i;

    // Verify they are gobs:
    sarg = arg;
    for (n = count = 0; n < len; n++, val++) {
        val = arg++;
        if (IS_WORD(val)) {
            //
            // For the moment, assume this GOB-or-WORD! containing block
            // only contains non-relative values.
            //
            val = Lookup_Word_May_Fail(val, SPECIFIED);
        }
        if (IS_GOB(val)) {
            count++;
            if (GOB_PARENT(VAL_GOB(val))) {
                // Check if inserting into same parent:
                i = -1;
                if (GOB_PARENT(VAL_GOB(val)) == gob) {
                    i = Find_Gob(gob, VAL_GOB(val));
                    if (i > 0 && i == (REBINT)index-1) { // a no-op
                        SET_GOB_FLAG(VAL_GOB(val), GOBS_NEW);
                        return;
                    }
                }
                Detach_Gob(VAL_GOB(val));
                if (i >= 0 && (REBINT)index > i) index--;
            }
        }
        else
            fail (Error_Bad_Value_Core(val, SPECIFIED));
    }
    arg = sarg;

    // Create or expand the pane series:

    REBARR *pane = GOB_PANE(gob);

    if (not pane) {
        pane = Make_Array_Core(
            count + 1,
            FLAG_FLAVOR(GOBLIST) | NODE_FLAG_MANAGED
        );
        SET_SERIES_LEN(pane, count);
        index = 0;
    }
    else {
        if (change) {
            if (index + count > ARR_LEN(pane)) {
                EXPAND_SERIES_TAIL(pane, index + count - ARR_LEN(pane));
            }
        } else {
            Expand_Series(pane, index, count);
            if (index >= ARR_LEN(pane))
                index = ARR_LEN(pane) - 1;
        }
    }

    RELVAL *item = ARR_AT(pane, index);
    for (n = 0; n < len; n++) {
        val = arg++;
        if (IS_WORD(val)) {
            //
            // Again, assume no relative values
            //
            val = Lookup_Word_May_Fail(val, SPECIFIED);
        }
        if (IS_GOB(val)) {
            if (GOB_PARENT(VAL_GOB(val)) != NULL)
                fail ("GOB! not expected to have parent");
            Copy_Cell(item, SPECIFIC(val));
            ++item;

            SET_GOB_PARENT(VAL_GOB(val), gob);
            SET_GOB_FLAG(VAL_GOB(val), GOBS_NEW);
        }
    }

  #ifdef DEBUG_TERM_ARRAYS
    if (IS_SER_DYNAMIC(pane))
        SET_CELL_FREE(ARR_TAIL(pane));
  #endif

    Init_Block(RESET(ARR_AT(gob, IDX_GOB_PANE)), pane);  // maybe already set
}


//
//  Remove_Gobs: C
//
// Remove one or more gobs from a pane at the given index.
//
static void Remove_Gobs(REBGOB *gob, REBLEN index, REBLEN len)
{
    REBVAL *item = GOB_AT(gob, index);

    REBLEN n;
    for (n = 0; n < len; ++n, ++item)
        SET_GOB_PARENT(VAL_GOB(item), nullptr);

    Remove_Series_Units(GOB_PANE(gob), index, len);
}


//
//  Gob_Flags_To_Array: C
//
static REBARR *Gob_Flags_To_Array(REBGOB *gob)
{
    REBARR *a = Make_Array(3);

    REBINT i;
    for (i = 0; Gob_Flag_Words[i].sym != SYM_0; ++i) {
        if (GET_GOB_FLAG(gob, Gob_Flag_Words[i].flags))
            Init_Word(Alloc_Tail_Array(a), Canon_Symbol(Gob_Flag_Words[i].sym));
    }

    return a;
}


//
//  Set_Gob_Flag: C
//
static void Set_Gob_Flag(REBGOB *gob, const REBSYM *name)
{
    SYMID sym = ID_OF_SYMBOL(name);
    if (sym == SYM_0) return; // !!! fail?

    REBINT i;
    for (i = 0; Gob_Flag_Words[i].sym != SYM_0; ++i) {
        if (Same_Nonzero_Symid(sym, Gob_Flag_Words[i].sym)) {
            REBLEN flag = Gob_Flag_Words[i].flags;
            SET_GOB_FLAG(gob, flag);
            //handle mutual exclusive states
            switch (flag) {
                case GOBF_RESTORE:
                    CLR_GOB_FLAG(gob, GOBF_MINIMIZE);
                    CLR_GOB_FLAG(gob, GOBF_MAXIMIZE);
                    CLR_GOB_FLAG(gob, GOBF_FULLSCREEN);
                    break;
                case GOBF_MINIMIZE:
                    CLR_GOB_FLAG(gob, GOBF_MAXIMIZE);
                    CLR_GOB_FLAG(gob, GOBF_RESTORE);
                    CLR_GOB_FLAG(gob, GOBF_FULLSCREEN);
                    break;
                case GOBF_MAXIMIZE:
                    CLR_GOB_FLAG(gob, GOBF_MINIMIZE);
                    CLR_GOB_FLAG(gob, GOBF_RESTORE);
                    CLR_GOB_FLAG(gob, GOBF_FULLSCREEN);
                    break;
                case GOBF_FULLSCREEN:
                    SET_GOB_FLAG(gob, GOBF_NO_TITLE);
                    SET_GOB_FLAG(gob, GOBF_NO_BORDER);
                    CLR_GOB_FLAG(gob, GOBF_MINIMIZE);
                    CLR_GOB_FLAG(gob, GOBF_RESTORE);
                    CLR_GOB_FLAG(gob, GOBF_MAXIMIZE);
            }
            break;
        }
    }
}


//
//  Did_Set_GOB_Var: C
//
static bool Did_Set_GOB_Var(REBGOB *gob, const RELVAL *word, const REBVAL *val)
{
    switch (VAL_WORD_ID(word)) {
      case SYM_OFFSET:
        return Did_Set_XYF(ARR_AT(gob, IDX_GOB_OFFSET_AND_FLAGS), val);

      case SYM_SIZE:
        return Did_Set_XYF(ARR_AT(gob, IDX_GOB_SIZE_AND_ALPHA), val);

      case SYM_IMAGE:
        CLR_GOB_OPAQUE(gob);
        if (rebDid("image?", val)) {
            REBVAL *size = rebValue("pick", val, "'size");
            int32_t w = rebUnboxInteger("pick", size, "'x");
            int32_t h = rebUnboxInteger("pick", size, "'y");
            rebRelease(size);

            GOB_W(gob) = cast(REBD32, w);
            GOB_H(gob) = cast(REBD32, h);
            SET_GOB_TYPE(gob, GOBT_IMAGE);
        }
        else if (IS_BLANK(val))
            SET_GOB_TYPE(gob, GOBT_NONE);
        else
            return false;

        Overwrite_Cell(GOB_CONTENT(gob), val);
        break;

      case SYM_DRAW:
        CLR_GOB_OPAQUE(gob);
        if (IS_BLOCK(val))
            SET_GOB_TYPE(gob, GOBT_DRAW);
        else if (IS_BLANK(val))
            SET_GOB_TYPE(gob, GOBT_NONE);
        else
            return false;

        Overwrite_Cell(GOB_CONTENT(gob), val);
        break;

      case SYM_TEXT:
        CLR_GOB_OPAQUE(gob);
        if (IS_BLOCK(val))
            SET_GOB_TYPE(gob, GOBT_TEXT);
        else if (IS_TEXT(val))
            SET_GOB_TYPE(gob, GOBT_STRING);
        else if (IS_BLANK(val))
            SET_GOB_TYPE(gob, GOBT_NONE);
        else
            return false;

        Overwrite_Cell(GOB_CONTENT(gob), val);
        break;

      case SYM_EFFECT:
        CLR_GOB_OPAQUE(gob);
        if (IS_BLOCK(val))
            SET_GOB_TYPE(gob, GOBT_EFFECT);
        else if (IS_BLANK(val))
            SET_GOB_TYPE(gob, GOBT_NONE);
        else
            return false;

        Overwrite_Cell(GOB_CONTENT(gob), val);
        break;

      case SYM_COLOR:
        CLR_GOB_OPAQUE(gob);
        if (IS_TUPLE(val)) {
            SET_GOB_TYPE(gob, GOBT_COLOR);
            if (
                VAL_SEQUENCE_LEN(val) < 4
                or VAL_SEQUENCE_BYTE_AT(val, 3) == 0
            ){
                SET_GOB_OPAQUE(gob);
            }
        }
        else if (IS_BLANK(val))
            SET_GOB_TYPE(gob, GOBT_NONE);
        else
            return false;

        Overwrite_Cell(GOB_CONTENT(gob), val);
        break;

      case SYM_PANE:
        if (GOB_PANE(gob))
            Clear_Series(GOB_PANE(gob));

        if (IS_BLOCK(val)) {
            REBLEN len;
            const RELVAL *head = VAL_ARRAY_LEN_AT(&len, val);
            Insert_Gobs(gob, head, 0, len, false);
        }
        else if (IS_GOB(val))
            Insert_Gobs(gob, val, 0, 1, false);
        else if (IS_BLANK(val))
            Init_Blank(ARR_AT(gob, IDX_GOB_PANE)); // pane array will GC
        else
            return false;
        break;

      case SYM_ALPHA:
        GOB_ALPHA(gob) = VAL_UINT8(val); // !!! "clip" instead of range error?
        break;

      case SYM_DATA:
        if (IS_OBJECT(val)) {
        }
        else if (IS_BLOCK(val)) {
        }
        else if (IS_TEXT(val)) {
        }
        else if (IS_BINARY(val)) {
        }
        else if (IS_INTEGER(val)) {
        }
        else if (IS_BLANK(val)) {
            SET_GOB_TYPE(gob, GOBT_NONE); // !!! Why touch the content?
            Init_Blank(GOB_CONTENT(gob));
        }
        else
            return false;

        Overwrite_Cell(GOB_DATA(gob), val);
        break;

      case SYM_FLAGS:
        if (IS_WORD(val))
            Set_Gob_Flag(gob, VAL_WORD_SYMBOL(val));
        else if (IS_BLOCK(val)) {
            //clear only flags defined by words
            REBINT i;
            for (i = 0; Gob_Flag_Words[i].sym != 0; ++i)
                CLR_GOB_FLAG(gob, Gob_Flag_Words[i].flags);

            const RELVAL *item = ARR_HEAD(VAL_ARRAY(val));
            const RELVAL *tail = ARR_TAIL(VAL_ARRAY(val));
            for (; item != tail; ++item)
                if (IS_WORD(item))
                    Set_Gob_Flag(gob, VAL_WORD_SYMBOL(item));
        }
        break;

      case SYM_OWNER:
        if (IS_GOB(val))
            SET_GOB_OWNER(gob, VAL_GOB(val));
        else
            return false;
        break;

    default:
        return false;
    }
    return true;
}


//
//  Did_Get_GOB_Var: C
//
// Returns true if the field name is a known GOB! property.  `out` may be set
// to a NULL cell even for known fields, if not applicable to this GOB!'s type.
//
static bool Did_Get_GOB_Var(
    RELVAL *out,
    REBGOB *gob,
    const RELVAL *word
){
    switch (VAL_WORD_ID(word)) {
      case SYM_OFFSET:
        Init_Pair_Dec(out, GOB_X(gob), GOB_Y(gob));
        break;

      case SYM_SIZE:
        Init_Pair_Dec(out, GOB_W(gob), GOB_H(gob));
        break;

      case SYM_IMAGE:
        if (GOB_TYPE(gob) == GOBT_IMAGE) {
            assert(rebDid("image?", GOB_CONTENT(gob)));
            Copy_Cell(out, GOB_CONTENT(gob));
        }
        else
            Init_Nulled(out);
        break;

      case SYM_DRAW:
        if (GOB_TYPE(gob) == GOBT_DRAW) {
            assert(IS_BLOCK(GOB_CONTENT(gob)));
            Copy_Cell(out, GOB_CONTENT(gob));
        }
        else
            Init_Nulled(out);
        break;

      case SYM_TEXT:
        if (GOB_TYPE(gob) == GOBT_TEXT) {
            assert(IS_BLOCK(GOB_CONTENT(gob)));
            Copy_Cell(out, GOB_CONTENT(gob));
        }
        else if (GOB_TYPE(gob) == GOBT_STRING) {
            assert(IS_TEXT(GOB_CONTENT(gob)));
            Copy_Cell(out, GOB_CONTENT(gob));
        }
        else
            Init_Nulled(out);
        break;

      case SYM_EFFECT:
        if (GOB_TYPE(gob) == GOBT_EFFECT) {
            assert(IS_BLOCK(GOB_CONTENT(gob)));
            Copy_Cell(out, GOB_CONTENT(gob));
        }
        else
            Init_Nulled(out);
        break;

      case SYM_COLOR:
        if (GOB_TYPE(gob) == GOBT_COLOR) {
            assert(IS_TUPLE(GOB_CONTENT(gob)));
            Copy_Cell(out, GOB_CONTENT(gob));
        }
        else
            Init_Nulled(out);
        break;

      case SYM_ALPHA:
        Init_Integer(out, GOB_ALPHA(gob));
        break;

      case SYM_PANE: {
        REBARR *pane = GOB_PANE(gob);
        if (not pane)
            Init_Block(out, Make_Array(0));
        else
            Init_Block(out, Copy_Array_Shallow(pane, SPECIFIED));
        break; }

      case SYM_PARENT:
        if (GOB_PARENT(gob))
            Init_Gob(out, GOB_PARENT(gob));
        else
            Init_Nulled(out);
        break;

      case SYM_DATA: {
        enum Reb_Kind kind = VAL_TYPE(GOB_DATA(gob));
        if (
            kind == REB_OBJECT
            or kind == REB_BLOCK
            or kind == REB_TEXT
            or kind == REB_BINARY
            or kind == REB_INTEGER
        ){
            Copy_Cell(out, GOB_DATA(gob));
        }
        else {
            assert(kind == REB_BLANK);
            Init_Nulled(out);
        }
        break; }

      case SYM_FLAGS:
        Init_Block(out, Gob_Flags_To_Array(gob));
        break;

      default:
        return false;  // unknown GOB! field
    }

    return true;  // known GOB! field
}


//
//  Set_GOB_Vars: C
//
static void Set_GOB_Vars(
    REBGOB *gob,
    const RELVAL *block,
    REBSPC *specifier
){
    DECLARE_LOCAL (var);
    DECLARE_LOCAL (val);

    const RELVAL *tail;
    const RELVAL *item = VAL_ARRAY_AT(&tail, block);
    while (item != tail) {
        Derelativize(var, item, specifier);
        ++item;

        if (!IS_SET_WORD(var))
            fail (Error_Unexpected_Type(REB_SET_WORD, VAL_TYPE(var)));

        if (item == tail)
            fail (Error_Need_Non_End_Raw(var));

        Derelativize(val, item, specifier);
        ++item;

        if (IS_SET_WORD(val))
            fail (Error_Need_Non_End_Raw(var));

        if (not Did_Set_GOB_Var(gob, var, val))
            fail (Error_Bad_Field_Set_Raw(var, Type_Of(val)));
    }
}


// Used by MOLD to create a block.
//
static REBARR *Gob_To_Array(REBGOB *gob)
{
    REBARR *arr = Make_Array(10);
    SYMID words[] = {SYM_OFFSET, SYM_SIZE, SYM_ALPHA, SYM_0};
    RELVAL *vals[3];

    REBINT n;
    for (n = 0; words[n] != SYM_0; ++n) {
        Init_Set_Word(Alloc_Tail_Array(arr), Canon_Symbol(words[n]));
        vals[n] = Alloc_Tail_Array(arr);
    }

    Init_Pair_Dec(vals[0], GOB_X(gob), GOB_Y(gob));
    Init_Pair_Dec(vals[1], GOB_W(gob), GOB_H(gob));
    Init_Integer(vals[2], GOB_ALPHA(gob));

    if (!GOB_TYPE(gob)) return arr;

    if (GOB_CONTENT(gob)) {
        SYMID sym;
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
        default:
            fail ("Unknown GOB! type");
        }

        REBVAL *name = Init_Set_Word(Alloc_Tail_Array(arr), Canon_Symbol(sym));
        RELVAL *slot = Alloc_Tail_Array(arr);
        bool known = Did_Get_GOB_Var(slot, gob, name);
        assert(known);  // should have known that sym
        UNUSED(known);

        // !!! Can't have nulls in arrays.  Is using BAD-WORD! correct here?
        // Gobs used none (blank) historically.
        //
        if (IS_NULLED(slot))
            Init_Bad_Word(slot, Canon(NULL));
    }

    return arr;
}


//
//  Extend_Gob_Core: C
//
// !!! R3-Alpha's MAKE has been unified with construction syntax, which has
// no "parent" slot (just type and value).  To try and incrementally keep
// code working, this parameterized function is called by both REBNATIVE(make)
// REBNATIVE(construct).
//
void Extend_Gob_Core(REBGOB *gob, const REBVAL *arg) {
    //
    // !!! See notes about derivation in REBNATIVE(make).  When deriving, it
    // appeared to copy the variables while nulling out the pane and parent
    // fields.  Then it applied the variables.  It also *said* in the case of
    // passing in another gob "merge gob provided as argument", but didn't
    // seem to do any merging--it just overwrote.  So the block and pair cases
    // were the only ones "merging".

    if (IS_BLOCK(arg)) {
        Set_GOB_Vars(gob, arg, VAL_SPECIFIER(arg));
    }
    else if (IS_PAIR(arg)) {
        GOB_X(gob) = VAL_PAIR_X_DEC(arg);
        GOB_Y(gob) = VAL_PAIR_Y_DEC(arg);
    }
    else
        fail (Error_Bad_Make(REB_CUSTOM, arg));
}


//
//  MAKE_Gob: C
//
REB_R MAKE_Gob(
    REBVAL *out,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    assert(kind == REB_CUSTOM);
    UNUSED(kind);

    if (not IS_GOB(arg)) { // call Extend() on an empty GOB with BLOCK!, etc.
        REBGOB *gob = Make_Gob();
        Extend_Gob_Core(gob, arg);
        Manage_Series(gob);
        return Init_Gob(out, gob);
    }

    if (parent) {
        assert(IS_GOB(unwrap(parent)));  // invariant for MAKE dispatch

        if (not IS_BLOCK(arg))
            fail (arg);

        // !!! Compatibility for `MAKE gob [...]` or `MAKE gob NxN` from
        // R3-Alpha GUI.  Start by copying the gob (minus pane and parent),
        // then apply delta to its properties from arg.  Doesn't save memory,
        // or keep any parent linkage--could be done in user code as a copy
        // and then apply the difference.
        //
        REBGOB *gob = Copy_Array_Shallow(VAL_GOB(unwrap(parent)), SPECIFIED);
        Init_Blank(ARR_AT(gob, IDX_GOB_PANE));
        SET_GOB_PARENT(gob, nullptr);
        Extend_Gob_Core(gob, arg);
        return Init_Gob(out, gob);
    }

    // !!! Previously a parent was allowed here, but completely overwritten
    // if a GOB! argument were provided.
    //
    REBGOB *gob = Copy_Array_Shallow(VAL_GOB(arg), SPECIFIED);
    Init_Blank(GOB_PANE_VALUE(gob));
    SET_GOB_PARENT(gob, nullptr);
    Manage_Series(gob);
    return Init_Gob(out, gob);
}


//
//  TO_Gob: C
//
REB_R TO_Gob(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_CUSTOM);
    UNUSED(kind);

    UNUSED(out);

    fail (arg);
}


//
//  PD_Gob: C
//
REB_R PD_Gob(
    REBPVS *pvs,
    const RELVAL *picker
){
    REBGOB *gob = VAL_GOB(pvs->out);

    if (IS_WORD(picker)) {
        if (not Did_Get_GOB_Var(RESET(pvs->out), gob, picker))
            return R_UNHANDLED;

        return pvs->out;
    }

    if (IS_INTEGER(picker))
        return rebValue(
            Lib(PICK),
                "@", SPECIFIC(ARR_AT(gob, IDX_GOB_PANE)),
                "@", SPECIFIC(picker)
        );

    return R_UNHANDLED;
}


//
//  MF_Gob: C
//
void MF_Gob(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    UNUSED(form);

    Pre_Mold(mo, v);

    REBARR *array = Gob_To_Array(VAL_GOB(v));
    Mold_Array_At(mo, array, 0, "[]");
    Free_Unmanaged_Series(array);

    End_Mold(mo);
}


//
//  REBTYPE: C
//
REBTYPE(Gob)
{
    const REBVAL *v = D_ARG(1);

    REBGOB *gob = VAL_GOB(v);
    REBLEN index = VAL_GOB_INDEX(v);
    REBLEN tail = GOB_PANE(gob) ? GOB_LEN(gob) : 0;

    switch (ID_OF_SYMBOL(verb)) {

    //=//// PICK-POKE* (see %sys-pick.h for explanation) ///////////////////=//

      case SYM_PICK_POKE_P: {
        INCLUDE_PARAMS_OF_PICK_POKE_P;
        UNUSED(ARG(location));

        REBVAL *steps = ARG(steps);  // STEPS block: 'a/(1 + 2)/b => [a 3 b]
        REBLEN steps_left = VAL_LEN_AT(steps);
        if (steps_left == 0)
            fail (steps);

        const RELVAL *picker = VAL_ARRAY_ITEM_AT(steps);

        REBVAL *setval = ARG(value);
        bool poking = not IS_NULLED(setval);

        if (steps_left == 1 and poking) {
            //
            // The goal is to poke ARG(value) into this particular field, like
            // `dict.key: 10`.  So this is the end of the line.
            //
            Meta_Unquotify(setval);

          update_bits: ;
            if (IS_INTEGER(picker)) {
                rebElide(
                    Lib(POKE),
                        "@", SPECIFIC(ARR_AT(gob, IDX_GOB_PANE)),
                        "@", SPECIFIC(picker)
                );
            }
            else if (IS_WORD(picker)) {
                if (not Did_Set_GOB_Var(gob, picker, setval))
                    return R_UNHANDLED;
            }
            else
                fail (rebUnrelativize(picker));
        }
        else {
            if (IS_INTEGER(picker)) {
                if (rebRunThrows(
                    D_OUT,
                    true,
                    Lib(PICK),
                        "@", SPECIFIC(ARR_AT(gob, IDX_GOB_PANE)),
                        "@", SPECIFIC(picker)
                )){
                    return R_THROWN;
                }
            }
            else if (IS_WORD(picker)) {
                if (not Did_Get_GOB_Var(D_OUT, gob, picker))
                    return R_UNHANDLED;
            }
            else
                fail (rebUnrelativize(picker));

            if (steps_left == 1) {
                assert(not poking);
                return D_OUT;
            }

            // The GOB! stores compressed bits for things like the SIZE, but
            // when a variable is requested it synthesizes a PAIR!.  This is
            // actually wasteful if someone is going to write `gob.size.x`,
            // because that could have just given back an INTEGER! with no
            // PAIR! node synthesized.  That is hardly concerning here.
            //
            // (It is more concerning in something like the FFI, where you have
            // `some_struct.million_ints_array.1`.  Because picking the first
            // element shouldn't require you to synthesize a BLOCK! of a
            // million INTEGER!--but `some_struct.million_ints_array` might.)
            //
            // The real issue for GOB! comes up when you POKE, such as with
            // `gob.size.x: 10`.  Handing off the "pick-poke" to PAIR! will
            // have it update the synthesized pair and return nullptr to say
            // there's no reason to update bits because it handled it.  But
            // the bits in the GOB! need changing.
            //
            // So GOB! has 3 options (presuming "ignore sets" isn't one):
            //
            // 1. Don't just consume one of the ARG(steps), but go ahead and
            //    do two--e.g. take control of what `size.x` means and don't
            //    synthesize a PAIR! at all.
            //
            // 2. Synthesize a PAIR! and allow it to do whatever modifications
            //    it wishes, but ignore its `nullptr` return status and pack
            //    the full pair value down to the low-level bits in the GOB!
            //
            // 3. Drop this micro-optimization and store a PAIR! cell in the
            //    GOB! structure.
            //
            // *The best option is 3*!  However, the point of keeping the GOB!
            // code in Ren-C has been to try and imagine how to accommodate
            // some of these categories of desires for optimization.  For this
            // particular exercise, we go with option (2).
            //
            // We have to save the pair to do this, because we can't count on
            // PAIR! dispatch not mucking with frame fields like ARG(location).
            //
            bool need_pair_writeback = poking and IS_PAIR(D_OUT);
            if (need_pair_writeback) {
                assert(IS_WORD(picker));  // pane pick shouldn't make pair
                Move_Cell(DS_PUSH(), D_OUT);
            }

            // Reuse the frame built for this PICK-POKE* call for XXX, by
            // updating the location and advancing the steps index.
            //
            // !!! Assumes frame compatibility, review enforcement!
            //
            ++VAL_INDEX_RAW(ARG(steps));

            REB_R r = Run_Pickpoke_Dispatch(frame_, verb, D_OUT);
            if (r == R_THROWN) {
                if (need_pair_writeback)
                    DS_DROP();
                return R_THROWN;
            }

            if (not poking)
                return r;

            if (need_pair_writeback) {  // (see explanation above)
                assert(r == nullptr);  // PAIR! thinks we don't need it...
                Move_Cell(D_OUT, DS_TOP);
                DS_DROP();
                setval = D_OUT;
                goto update_bits;
            }

            if (r != nullptr) {  // the update needs our cell's bits to change
                //
                // Besides the trick with PAIR!, the gob doesn't know how to
                // take any other updates and write them back to its encoded
                // bits.  There shouldn't be any other cases, as strings and
                // such in the GOB! are pointed to by reference.
                //
                fail ("Unknown writeback in GOB!");
            }
        }

        return nullptr; }

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // covered by `val`
        SYMID property = VAL_WORD_ID(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_HEAD:
            index = 0;
            goto set_index;

        case SYM_TAIL:
            index = tail;
            goto set_index;

        case SYM_HEAD_Q:
            return Init_Logic(D_OUT, index == 0);

        case SYM_TAIL_Q:
            return Init_Logic(D_OUT, index >= tail);

        case SYM_PAST_Q:
            return Init_Logic(D_OUT, index > tail);

        case SYM_INDEX:
            return Init_Integer(D_OUT, index + 1);

        case SYM_LENGTH:
            index = (tail > index) ? tail - index : 0;
            return Init_Integer(D_OUT, index);

        default:
            break;
        }

        break; }

    // !!! Note: PICK and POKE were unified with path dispatch.  The general
    // goal is to unify these mechanisms.  However, GOB! is tricky in terms
    // of what it tried to do with a synthesized PAIR!, calling back into
    // Next_Path_Throws().  A logical overhaul of path dispatch is needed.
    // This code is left in case there's something to glean from it when
    // a GOB!-based path dispatch breaks.
    /*
    case SYM_PICK:
        if (!(ANY_NUMBER(arg) || IS_BLANK(arg)))
            fail (arg);

        if (!GOB_PANE(gob))
            return nullptr;
        index += Get_Num_From_Arg(arg) - 1;
        if (index >= tail)
            return nullptr;
        gob = *GOB_AT(gob, index);
        index = 0;
        goto set_index;

    case SYM_POKE:
        index += Get_Num_From_Arg(arg) - 1;
        arg = D_ARG(3);
        // fallthrough */
    case SYM_CHANGE: {
        INCLUDE_PARAMS_OF_CHANGE;
        UNUSED(PAR(series));  // covered by `v`

        REBVAL *value = ARG(value);
        if (!IS_GOB(value))
            fail (PAR(value));

        if (REF(line))
            fail (Error_Bad_Refines_Raw());

        if (!GOB_PANE(gob) || index >= tail)
            fail (Error_Index_Out_Of_Range_Raw());
        if (
            ID_OF_SYMBOL(verb) == SYM_CHANGE
            && (REF(part) || REF(dup))
        ){
            fail (Error_Not_Done_Raw());
        }

        Insert_Gobs(gob, value, index, 1, false);
        if (ID_OF_SYMBOL(verb) == SYM_POKE) {
            Copy_Cell(D_OUT, value);
            return D_OUT;
        }
        index++;
        goto set_index; }

    case SYM_APPEND:
        index = tail;
        // falls through
    case SYM_INSERT: {
        INCLUDE_PARAMS_OF_INSERT;
        UNUSED(PAR(series));  // covered by `v`

        RELVAL *value = ARG(value);

        if (IS_NULLED_OR_BLANK(value))
            RETURN (v);  // don't fail on read only if it would be a no-op

        if (REF(line))
            fail (Error_Bad_Refines_Raw());

        if (REF(part) || REF(dup))
            fail (Error_Not_Done_Raw());

        REBLEN len;
        if (IS_GOB(value)) {
            len = 1;
        }
        else if (IS_BLOCK(value)) {
            value = m_cast(RELVAL*,
                VAL_ARRAY_LEN_AT(&len, KNOWN_MUTABLE(value))
            );  // !!!
        }
        else
            fail (PAR(value));

        Insert_Gobs(gob, value, index, len, false);

        RETURN (v); }

    case SYM_CLEAR:
        if (tail > index)
            Remove_Gobs(gob, index, tail - index);

        RETURN (v);

    case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;
        UNUSED(PAR(series));  // covered by `v`

        REBLEN len = REF(part) ? Get_Num_From_Arg(ARG(part)) : 1;
        if (index + len > tail)
            len = tail - index;
        if (index < tail && len != 0)
            Remove_Gobs(gob, index, len);

        RETURN (v); }

    case SYM_TAKE: {
        INCLUDE_PARAMS_OF_TAKE;
        UNUSED(PAR(series));  // covered by `v`

        // Pane is an ordinary array, so chain to the ordinary TAKE* code.
        // Its index is always at zero, because the GOB! instances are the
        // ones with the index.  Skip to compensate.
        //
        // !!! Could make the indexed pane into a local if we had a spare
        // local, but its' good to exercise the API as much as possible).
        //
        REBVAL *pane = SPECIFIC(ARR_AT(gob, IDX_GOB_PANE));
        return rebValue(
            "applique :take [",
                "series: at", rebQ(pane), rebI(index + 1),
                "part:", REF(part),
                "deep:", REF(deep),
                "last:", REF(last),
            "]"
        ); }

    case SYM_AT:
        index--;
        // falls through
    case SYM_SKIP:
        index += VAL_INT32(D_ARG(2));
        goto set_index;

    case SYM_FIND:
        if (IS_GOB(D_ARG(2))) {
            index = Find_Gob(gob, VAL_GOB(D_ARG(2)));
            if (index == NOT_FOUND)
                return nullptr;
            goto set_index;
        }
        return nullptr;

    case SYM_REVERSE:
        return rebValue(
            "reverse @", SPECIFIC(ARR_AT(gob, IDX_GOB_PANE))
        );

    default:
        break;
    }

    return R_UNHANDLED;

  set_index:

    RESET_CUSTOM_CELL(D_OUT, EG_Gob_Type, CELL_FLAG_FIRST_IS_NODE);
    INIT_VAL_NODE1(D_OUT, gob);
    VAL_GOB_INDEX(D_OUT) = index;
    return D_OUT;
}
