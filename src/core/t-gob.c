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

const struct {
    REBSYM sym;
    REBFLGS flags;
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
REBINT CT_Gob(const RELVAL *a, const RELVAL *b, REBINT mode)
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
    gob->header.bits = NODE_FLAG_NODE;
    if ((GC_Ballast -= Mem_Pools[GOB_POOL].wide) <= 0) SET_SIGNAL(SIG_RECYCLE);
    return gob;
}


//
//  Cmp_Gob: C
//
REBINT Cmp_Gob(const RELVAL *g1, const RELVAL *g2)
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
static bool Set_Pair(REBXYF *pair, const REBVAL *val)
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
        return false;

    return true;
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
        len = GOB_LEN(gob);
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
static void Insert_Gobs(
    REBGOB *gob,
    const RELVAL *arg,
    REBCNT index,
    REBCNT len,
    bool change
) {
    REBGOB **ptr;
    REBCNT n, count;
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
            val = Get_Opt_Var_May_Fail(val, SPECIFIED);
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
            fail (Error_Invalid_Core(val, SPECIFIED));
    }
    arg = sarg;

    // Create or expand the pane series:
    if (!GOB_PANE(gob)) {
        GOB_PANE(gob) = Make_Ser(count + 1, sizeof(REBGOB*));
        SET_GOB_LEN(gob, count);
        index = 0;

        // !!! A GOB_PANE could theoretically be MKS_UNTRACKED and manually
        // memory managed, if that made sense.  Does it?

        MANAGE_SERIES(GOB_PANE(gob));
    }
    else {
        if (change) {
            if (index + count > GOB_LEN(gob)) {
                EXPAND_SERIES_TAIL(GOB_PANE(gob), index + count - GOB_LEN(gob));
            }
        } else {
            Expand_Series(GOB_PANE(gob), index, count);
            if (index >= GOB_LEN(gob)) index = GOB_LEN(gob)-1;
        }
    }

    ptr = GOB_AT(gob, index);
    for (n = 0; n < len; n++) {
        val = arg++;
        if (IS_WORD(val)) {
            //
            // Again, assume no relative values
            //
            val = Get_Opt_Var_May_Fail(val, SPECIFIED);
        }
        if (IS_GOB(val)) {
            if (GOB_PARENT(VAL_GOB(val)) != NULL)
                fail ("GOB! not expected to have parent");
            *ptr++ = VAL_GOB(val);
            GOB_PARENT(VAL_GOB(val)) = gob;
            SET_GOB_FLAG(VAL_GOB(val), GOBS_NEW);
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

    ptr = GOB_AT(gob, index);
    for (n = 0; n < len; n++, ptr++) {
        GOB_PARENT(*ptr) = 0;
    }

    Remove_Series(GOB_PANE(gob), index, len);
}


//
//  Pane_To_Array: C
//
// Convert pane list of gob pointers to a Rebol array of GOB! REBVALs.
//
static REBARR *Pane_To_Array(REBGOB *gob, REBCNT index, REBINT len)
{
    if (len == -1 || (len + index) > GOB_LEN(gob)) len = GOB_LEN(gob) - index;
    if (len < 0) len = 0;

    REBARR *a = Make_Arr(len);
    RELVAL *dest = ARR_HEAD(a);
    REBGOB **gp = GOB_HEAD(gob);
    for (; len > 0; len--, ++dest, ++gp)
        Init_Gob(dest, *gp);

    TERM_ARRAY_LEN(a, len);
    return a;
}


//
//  Gob_Flags_To_Array: C
//
static REBARR *Gob_Flags_To_Array(REBGOB *gob)
{
    REBARR *a = Make_Arr(3);

    REBINT i;
    for (i = 0; Gob_Flag_Words[i].sym != SYM_0; ++i) {
        if (GET_GOB_FLAG(gob, Gob_Flag_Words[i].flags))
            Init_Word(Alloc_Tail_Array(a), Canon(Gob_Flag_Words[i].sym));
    }

    return a;
}


//
//  Set_Gob_Flag: C
//
static void Set_Gob_Flag(REBGOB *gob, REBSTR *name)
{
    REBSYM sym = STR_SYMBOL(name);
    if (sym == SYM_0) return; // !!! fail?

    REBINT i;
    for (i = 0; Gob_Flag_Words[i].sym != SYM_0; ++i) {
        if (SAME_SYM_NONZERO(sym, Gob_Flag_Words[i].sym)) {
            REBCNT flag = Gob_Flag_Words[i].flags;
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
//  Set_GOB_Var: C
//
static bool Set_GOB_Var(REBGOB *gob, const REBVAL *word, const REBVAL *val)
{
    switch (VAL_WORD_SYM(word)) {
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
        else if (IS_BLANK(val))
            SET_GOB_TYPE(gob, GOBT_NONE);
        else
            return false;
        break;

    case SYM_DRAW:
        CLR_GOB_OPAQUE(gob);
        if (IS_BLOCK(val)) {
            SET_GOB_TYPE(gob, GOBT_DRAW);
            GOB_CONTENT(gob) = VAL_SERIES(val);
        }
        else if (IS_BLANK(val))
            SET_GOB_TYPE(gob, GOBT_NONE);
        else
            return false;
        break;

    case SYM_TEXT:
        CLR_GOB_OPAQUE(gob);
        if (IS_BLOCK(val)) {
            SET_GOB_TYPE(gob, GOBT_TEXT);
            GOB_CONTENT(gob) = VAL_SERIES(val);
        }
        else if (IS_TEXT(val)) {
            SET_GOB_TYPE(gob, GOBT_STRING);
            GOB_CONTENT(gob) = VAL_SERIES(val);
        }
        else if (IS_BLANK(val))
            SET_GOB_TYPE(gob, GOBT_NONE);
        else
            return false;
        break;

    case SYM_EFFECT:
        CLR_GOB_OPAQUE(gob);
        if (IS_BLOCK(val)) {
            SET_GOB_TYPE(gob, GOBT_EFFECT);
            GOB_CONTENT(gob) = VAL_SERIES(val);
        }
        else if (IS_BLANK(val))
            SET_GOB_TYPE(gob, GOBT_NONE);
        else
            return false;
        break;

    case SYM_COLOR:
        CLR_GOB_OPAQUE(gob);
        if (IS_TUPLE(val)) {
            SET_GOB_TYPE(gob, GOBT_COLOR);
            Set_Pixel_Tuple((REBYTE*)&GOB_CONTENT(gob), val);
            if (VAL_TUPLE_LEN(val) < 4 || VAL_TUPLE(val)[3] == 0)
                SET_GOB_OPAQUE(gob);
        }
        else if (IS_BLANK(val)) SET_GOB_TYPE(gob, GOBT_NONE);
        break;

    case SYM_PANE:
        if (GOB_PANE(gob)) Clear_Series(GOB_PANE(gob));
        if (IS_BLOCK(val))
            Insert_Gobs(
                gob, VAL_ARRAY_AT(val), 0, VAL_ARRAY_LEN_AT(val), false
            );
        else if (IS_GOB(val))
            Insert_Gobs(gob, val, 0, 1, false);
        else if (IS_BLANK(val))
            gob->pane = 0;
        else
            return false;
        break;

    case SYM_ALPHA:
        GOB_ALPHA(gob) = VAL_UINT8(val); // !!! "clip" instead of range error?
        break;

    case SYM_DATA:
        SET_GOB_DTYPE(gob, GOBD_NONE);
        if (IS_OBJECT(val)) {
            SET_GOB_DTYPE(gob, GOBD_OBJECT);
            SET_GOB_DATA(gob, SER(CTX_VARLIST(VAL_CONTEXT(val))));
        }
        else if (IS_BLOCK(val)) {
            SET_GOB_DTYPE(gob, GOBD_BLOCK);
            SET_GOB_DATA(gob, VAL_SERIES(val));
        }
        else if (IS_TEXT(val)) {
            SET_GOB_DTYPE(gob, GOBD_STRING);
            SET_GOB_DATA(gob, VAL_SERIES(val));
        }
        else if (IS_BINARY(val)) {
            SET_GOB_DTYPE(gob, GOBD_BINARY);
            SET_GOB_DATA(gob, VAL_SERIES(val));
        }
        else if (IS_INTEGER(val)) {
            SET_GOB_DTYPE(gob, GOBD_INTEGER);
            SET_GOB_DATA(gob, cast(REBSER*, cast(intptr_t, VAL_INT64(val))));
        }
        else if (IS_BLANK(val))
            SET_GOB_TYPE(gob, GOBT_NONE);
        else
            return false;
        break;

    case SYM_FLAGS:
        if (IS_WORD(val))
            Set_Gob_Flag(gob, VAL_WORD_SPELLING(val));
        else if (IS_BLOCK(val)) {
            //clear only flags defined by words
            REBINT i;
            for (i = 0; Gob_Flag_Words[i].sym != 0; ++i)
                CLR_GOB_FLAG(gob, Gob_Flag_Words[i].flags);

            RELVAL* item;
            for (item = VAL_ARRAY_HEAD(val); NOT_END(item); item++)
                if (IS_WORD(item)) Set_Gob_Flag(gob, VAL_WORD_CANON(item));
        }
        break;

    case SYM_OWNER:
        if (IS_GOB(val))
            GOB_TMP_OWNER(gob) = VAL_GOB(val);
        else
            return false;
        break;

    default:
        return false;
    }
    return true;
}


//
//  Get_GOB_Var: C
//
// !!! Things like this Get_GOB_Var routine could be replaced with ordinary
// OBJECT!-style access if GOB! was an ANY-CONTEXT.
//
static REBVAL *Get_GOB_Var(RELVAL *out, REBGOB *gob, const REBVAL *word)
{
    switch (VAL_WORD_SYM(word)) {
      case SYM_OFFSET:
        return Init_Pair_Dec(out, GOB_X(gob), GOB_Y(gob));

      case SYM_SIZE:
        return Init_Pair_Dec(out, GOB_W(gob), GOB_H(gob));

      case SYM_IMAGE:
        if (GOB_TYPE(gob) == GOBT_IMAGE)
            fail ("Support for GOBT_IMAGE seems to be missing.");
        return Init_Blank(out);

      case SYM_DRAW:
        if (GOB_TYPE(gob) == GOBT_DRAW)
            return Init_Block(out, ARR(GOB_CONTENT(gob)));
        return Init_Blank(out);

      case SYM_TEXT:
        if (GOB_TYPE(gob) == GOBT_TEXT)
            return Init_Block(out, ARR(GOB_CONTENT(gob)));
        if (GOB_TYPE(gob) == GOBT_STRING)
            return Init_Text(out, GOB_CONTENT(gob));
        return Init_Blank(out);

      case SYM_EFFECT:
        if (GOB_TYPE(gob) == GOBT_EFFECT)
            return Init_Block(out, ARR(GOB_CONTENT(gob)));
        return Init_Blank(out);

      case SYM_COLOR:
        if (GOB_TYPE(gob) == GOBT_COLOR) {
            return Init_Tuple_From_Pixel(
                out,
                cast(REBYTE*, &GOB_CONTENT(gob))
            );
        }
        return Init_Blank(out);

      case SYM_ALPHA:
        return Init_Integer(out, GOB_ALPHA(gob));

      case SYM_PANE:
        if (GOB_PANE(gob))
            return Init_Block(out, Pane_To_Array(gob, 0, -1));
        return Init_Block(out, Make_Arr(0));

      case SYM_PARENT:
        if (GOB_PARENT(gob))
            return Init_Gob(out, GOB_PARENT(gob));
        return Init_Blank(out);

      case SYM_DATA:
        if (GOB_DTYPE(gob) == GOBD_OBJECT)
            return Init_Object(out, CTX(GOB_DATA(gob)));
        if (GOB_DTYPE(gob) == GOBD_BLOCK)
            return Init_Block(out, ARR(GOB_DATA(gob)));
        if (GOB_DTYPE(gob) == GOBD_STRING)
            return Init_Text(out, GOB_DATA(gob));
        if (GOB_DTYPE(gob) == GOBD_BINARY)
            return Init_Binary(out, GOB_DATA(gob));
        if (GOB_DTYPE(gob) == GOBD_INTEGER)
            return Init_Integer(out, cast(intptr_t, GOB_DATA(gob)));
        return Init_Blank(out);

      case SYM_FLAGS:
        return Init_Block(out, Gob_Flags_To_Array(gob));

      default:
        return Init_Blank(out);
    }
}


//
//  Set_GOB_Vars: C
//
static void Set_GOB_Vars(REBGOB *gob, const RELVAL *blk, REBSPC *specifier)
{
    DECLARE_LOCAL (var);
    DECLARE_LOCAL (val);

    while (NOT_END(blk)) {
        assert(not IS_NULLED(blk));

        Derelativize(var, blk, specifier);
        ++blk;

        if (!IS_SET_WORD(var))
            fail (Error_Unexpected_Type(REB_SET_WORD, VAL_TYPE(var)));

        if (IS_END(blk))
            fail (Error_Need_Non_End_Raw(var));

        Derelativize(val, blk, specifier);
        ++blk;

        if (IS_SET_WORD(val))
            fail (Error_Need_Non_End_Raw(var));

        if (!Set_GOB_Var(gob, var, val))
            fail (Error_Bad_Field_Set_Raw(var, Type_Of(val)));
    }
}


// Used by MOLD to create a block.
//
static REBARR *Gob_To_Array(REBGOB *gob)
{
    REBARR *arr = Make_Arr(10);
    REBSYM words[] = {SYM_OFFSET, SYM_SIZE, SYM_ALPHA, SYM_0};
    REBVAL *vals[6];

    REBINT n;
    for (n = 0; words[n] != SYM_0; ++n) {
        Init_Set_Word(Alloc_Tail_Array(arr), Canon(words[n]));
        vals[n] = Init_Blank(Alloc_Tail_Array(arr));
    }

    Init_Pair_Dec(vals[0], GOB_X(gob), GOB_Y(gob));
    Init_Pair_Dec(vals[1], GOB_W(gob), GOB_H(gob));
    Init_Integer(vals[2], GOB_ALPHA(gob));

    if (!GOB_TYPE(gob)) return arr;

    if (GOB_CONTENT(gob)) {
        REBSYM sym;
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

        REBVAL *name = Init_Set_Word(Alloc_Tail_Array(arr), Canon(sym));
        Get_GOB_Var(Alloc_Tail_Array(arr), gob, name); // BLANK! if not set
    }

    return arr;
}


//
//  Map_Gob_Inner: C
//
// Map a higher level gob coordinate to a lower level.
// Returns GOB and sets new offset pair.
//
static REBGOB *Map_Gob_Inner(REBGOB *gob, REBXYF *offset)
{
    REBD32 xo = offset->x;
    REBD32 yo = offset->y;
    REBINT n;
    REBINT len;
    REBGOB **gop;
    REBD32 x = 0;
    REBD32 y = 0;
    REBINT max_depth = 1000; // avoid infinite loops

    while (GOB_PANE(gob) && (max_depth-- > 0)) {
        len = GOB_LEN(gob);
        gop = GOB_HEAD(gob) + len - 1;
        for (n = 0; n < len; n++, gop--) {
            if (
                (xo >= x + GOB_X(*gop)) &&
                (xo <  x + GOB_X(*gop) + GOB_W(*gop)) &&
                (yo >= y + GOB_Y(*gop)) &&
                (yo <  y + GOB_Y(*gop) + GOB_H(*gop))
            ){
                x += GOB_X(*gop);
                y += GOB_Y(*gop);
                gob = *gop;
                break;
            }
        }
        if (n >= len) break; // not found
    }

    offset->x -= x;
    offset->y -= y;

    return gob;
}


//
//  map-event: native [
//
//  {Returns event with inner-most graphical object and coordinate.}
//
//      event [event!]
//  ]
//
REBNATIVE(map_event)
{
    INCLUDE_PARAMS_OF_MAP_EVENT;

    REBVAL *val = ARG(event);
    REBGOB *gob = cast(REBGOB*, VAL_EVENT_SER(val));
    REBXYF xy;

    if (gob != NULL && (VAL_EVENT_FLAGS(val) & EVF_HAS_XY)) {
        xy.x = (REBD32)VAL_EVENT_X(val);
        xy.y = (REBD32)VAL_EVENT_Y(val);
        VAL_EVENT_SER(val) = cast(REBSER*, Map_Gob_Inner(gob, &xy));
        SET_EVENT_XY(val, ROUND_TO_INT(xy.x), ROUND_TO_INT(xy.y));
    }

    RETURN (ARG(event));
}


//
//  map-gob-offset: native [
//
//  {Translate gob and offset to deepest gob and offset in it}
//
//      return: [block!]
//          "[GOB! PAIR!] 2-element block"
//      gob [gob!]
//          "Starting object"
//      xy [pair!]
//          "Staring offset"
//      /reverse
//          "Translate from deeper gob to top gob."
//  ]
//
REBNATIVE(map_gob_offset)
{
    INCLUDE_PARAMS_OF_MAP_GOB_OFFSET;
    UNUSED(ARG(gob));
    UNUSED(ARG(xy));
    UNUSED(REF(reverse));

    REBGOB *gob = VAL_GOB(ARG(gob));
    REBD32 xo = VAL_PAIR_X(ARG(xy));
    REBD32 yo = VAL_PAIR_Y(ARG(xy));

    if (REF(reverse)) {
        REBINT max_depth = 1000; // avoid infinite loops
        while (
            GOB_PARENT(gob)
            && (max_depth-- > 0)
            && !GET_GOB_FLAG(gob, GOBF_WINDOW)
        ){
            xo += GOB_X(gob);
            yo += GOB_Y(gob);
            gob = GOB_PARENT(gob);
        }
    }
    else {
        REBXYF xy;
        xy.x = VAL_PAIR_X(ARG(xy));
        xy.x = VAL_DECIMAL(ARG(xy)->payload.pair + 1);
        xy.y = VAL_PAIR_Y(ARG(xy));
        gob = Map_Gob_Inner(gob, &xy);
        xo = xy.x;
        yo = xy.y;
    }

    REBARR *arr = Make_Arr(2);
    Init_Gob(Alloc_Tail_Array(arr), gob);
    Init_Pair_Dec(Alloc_Tail_Array(arr), xo, yo);

    return Init_Block(D_OUT, arr);
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
        Set_GOB_Vars(gob, VAL_ARRAY_AT(arg), VAL_SPECIFIER(arg));
    }
    else if (IS_PAIR(arg)) {
        gob->size.x = VAL_PAIR_X(arg);
        gob->size.y = VAL_PAIR_Y(arg);
    }
    else
        fail (Error_Bad_Make(REB_GOB, arg));
}


//
//  MAKE_Gob: C
//
REB_R MAKE_Gob(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_GOB);
    UNUSED(kind);

    REBGOB *gob = Make_Gob();

    if (IS_GOB(arg)) {
        //
        // !!! See notes in Extend_Gob_Core; previously a parent was allowed
        // here, but completely overwritten with a GOB! argument.
        //
        *gob = *VAL_GOB(arg);
        gob->pane = NULL;
        gob->parent = NULL;
    }
    else
        Extend_Gob_Core(gob, arg);

    return Init_Gob(out, gob);
}


//
//  TO_Gob: C
//
REB_R TO_Gob(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_GOB);
    UNUSED(kind);

    UNUSED(out);

    fail (Error_Invalid(arg));
}


//
//  PD_Gob: C
//
REB_R PD_Gob(
    REBPVS *pvs,
    const REBVAL *picker,
    const REBVAL *opt_setval
){
    REBGOB *gob = VAL_GOB(pvs->out);
    REBCNT index;
    REBCNT tail;

    if (IS_WORD(picker)) {
        if (opt_setval == NULL) {
            if (IS_BLANK(Get_GOB_Var(pvs->out, gob, picker)))
                return R_UNHANDLED;

            // !!! Comment here said: "Check for SIZE/X: types of cases".
            // See %c-path.c for an explanation of why this code steps
            // outside the ordinary path processing to "look ahead" in the
            // case of wanting to make it possible to use a generated PAIR!
            // as a way of "writing back" into the values in the GOB! that
            // were used to generate the PAIR!.  There should be some
            // overall solution to facilitating this kind of need.
            //
            if (PVS_IS_SET_PATH(pvs) and IS_PAIR(pvs->out)) {
                //
                // !!! Adding to the reasons that this is dodgy, the picker
                // can be pointing to a temporary memory cell, and when
                // Next_Path_Throws runs arbitrary code it could be GC'd too.
                // Have to copy -and- protect.
                //
                DECLARE_LOCAL (orig_picker);
                Move_Value(orig_picker, picker);
                PUSH_GC_GUARD(orig_picker);

                if (Next_Path_Throws(pvs)) // sets value in pvs->store
                    fail (Error_No_Catch_For_Throw(pvs->out)); // Review

                // write it back to gob
                //
                Set_GOB_Var(gob, orig_picker, pvs->out);
                DROP_GC_GUARD(orig_picker);
            }
            return pvs->out;
        }
        else {
            if (!Set_GOB_Var(gob, picker, opt_setval))
                return R_UNHANDLED;
            return R_INVISIBLE;
        }
    }

    if (IS_INTEGER(picker)) {
        if (!GOB_PANE(gob)) return Init_Blank(pvs->out);

        tail = GOB_PANE(gob) ? GOB_LEN(gob) : 0;
        index = VAL_GOB_INDEX(pvs->out);
        index += Int32(picker) - 1;

        if (index >= tail) return Init_Blank(pvs->out);

        gob = *GOB_AT(gob, index);
        RESET_CELL(pvs->out, REB_GOB);
        VAL_GOB(pvs->out) = gob;
        VAL_GOB_INDEX(pvs->out) = 0;
        return pvs->out;
    }

    return R_UNHANDLED;
}


//
//  MF_Gob: C
//
void MF_Gob(REB_MOLD *mo, const RELVAL *v, bool form)
{
    UNUSED(form);

    Pre_Mold(mo, v);

    REBARR *array = Gob_To_Array(VAL_GOB(v));
    Mold_Array_At(mo, array, 0, "[]");
    Free_Unmanaged_Array(array);

    End_Mold(mo);
}


//
//  REBTYPE: C
//
REBTYPE(Gob)
{
    const REBVAL *val = D_ARG(1);

    REBGOB *gob = VAL_GOB(val);
    REBCNT index = VAL_GOB_INDEX(val);
    REBCNT tail = GOB_PANE(gob) ? GOB_LEN(gob) : 0;

    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    // unary actions
    switch (VAL_WORD_SYM(verb)) {
    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // covered by `val`
        REBSYM property = VAL_WORD_SYM(ARG(property));
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
            Init_Integer(D_OUT, index + 1);
            break;

        case SYM_LENGTH:
            index = (tail > index) ? tail - index : 0;
            Init_Integer(D_OUT, index);
            break;

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
            fail (Error_Invalid(arg));

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

        UNUSED(PAR(series));
        UNUSED(PAR(value)); // handled as `arg`

        if (!IS_GOB(arg))
            fail (Error_Unexpected_Type(REB_GOB, VAL_TYPE(arg)));

        if (REF(line))
            fail (Error_Bad_Refines_Raw());

        if (!GOB_PANE(gob) || index >= tail)
            fail (Error_Past_End_Raw());
        if (
            VAL_WORD_SYM(verb) == SYM_CHANGE
            && (REF(part) || REF(only) || REF(dup))
        ){
            UNUSED(PAR(limit));
            UNUSED(PAR(count));
            fail (Error_Not_Done_Raw());
        }

        Insert_Gobs(gob, arg, index, 1, false);
        if (VAL_WORD_SYM(verb) == SYM_POKE) {
            Move_Value(D_OUT, arg);
            return D_OUT;
        }
        index++;
        goto set_index; }

    case SYM_APPEND:
        index = tail;
        // falls through
    case SYM_INSERT: {
        INCLUDE_PARAMS_OF_INSERT;

        if (IS_NULLED_OR_BLANK(arg))
            RETURN (val); // don't fail on read only if it would be a no-op

        UNUSED(PAR(series));
        UNUSED(PAR(value));

        if (REF(line))
            fail (Error_Bad_Refines_Raw());

        if (REF(part) || REF(only) || REF(dup)) {
            UNUSED(PAR(limit));
            UNUSED(PAR(count));
            fail (Error_Not_Done_Raw());
        }

        REBCNT len;
        if (IS_GOB(arg)) {
            len = 1;
        }
        else if (IS_BLOCK(arg)) {
            len = VAL_ARRAY_LEN_AT(arg);
            arg = KNOWN(VAL_ARRAY_AT(arg)); // !!! REVIEW
        }
        else
            fail (Error_Unexpected_Type(REB_GOB, VAL_TYPE(arg)));

        Insert_Gobs(gob, arg, index, len, false);

        return Move_Value(D_OUT, val); }

    case SYM_CLEAR:
        if (tail > index)
            Remove_Gobs(gob, index, tail - index);

        Move_Value(D_OUT, val);
        return D_OUT;

    case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;

        UNUSED(PAR(series));

        if (REF(map)) {
            UNUSED(ARG(key));
            fail (Error_Bad_Refines_Raw());
        }

        REBCNT len = REF(part) ? Get_Num_From_Arg(ARG(limit)) : 1;
        if (index + len > tail)
            len = tail - index;
        if (index < tail && len != 0)
            Remove_Gobs(gob, index, len);

        Move_Value(D_OUT, val);
        return D_OUT; }

    case SYM_TAKE: {
        INCLUDE_PARAMS_OF_TAKE;

        UNUSED(PAR(series));

        if (REF(deep))
            fail (Error_Bad_Refines_Raw());
        if (REF(last))
            fail (Error_Bad_Refines_Raw());

        REBCNT len = REF(part) ? Get_Num_From_Arg(ARG(limit)) : 1;
        if (index + len > tail)
            len = tail - index;
        if (index >= tail)
            return nullptr;

        if (!REF(part)) { // just one value
            RESET_CELL(D_OUT, REB_GOB);
            VAL_GOB(D_OUT) = *GOB_AT(gob, index);
            VAL_GOB_INDEX(D_OUT) = 0;
            Remove_Gobs(gob, index, 1);
            return D_OUT;
        }

        Init_Block(D_OUT, Pane_To_Array(gob, index, len));
        Remove_Gobs(gob, index, len);
        Move_Value(D_OUT, val);
        return D_OUT; }

    case SYM_AT:
        index--;
        // falls through
    case SYM_SKIP:
        index += VAL_INT32(arg);
        goto set_index;

    case SYM_FIND:
        if (IS_GOB(arg)) {
            index = Find_Gob(gob, VAL_GOB(arg));
            if (index == NOT_FOUND)
                return nullptr;
            goto set_index;
        }
        return nullptr;

    case SYM_REVERSE:
        for (index = 0; index < tail/2; index++) {
            REBGOB *ngob = *GOB_AT(gob, tail-index-1);
            *GOB_AT(gob, tail-index-1) = *GOB_AT(gob, index);
            *GOB_AT(gob, index) = ngob;
        }
        RETURN (D_ARG(1));

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_GOB, verb));

set_index:
    RESET_CELL(D_OUT, REB_GOB);
    VAL_GOB(D_OUT) = gob;
    VAL_GOB_INDEX(D_OUT) = index;
    return D_OUT;
}
