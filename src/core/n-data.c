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
**  Module:  n-data.c
**  Summary: native functions for data and context
**  Section: natives
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


static int Check_Char_Range(REBVAL *val, REBINT limit)
{
    REBCNT len;

    if (IS_CHAR(val)) {
        if (VAL_CHAR(val) > limit) return R_FALSE;
        return R_TRUE;
    }

    if (IS_INTEGER(val)) {
        if (VAL_INT64(val) > limit) return R_FALSE;
        return R_TRUE;
    }

    len = VAL_LEN(val);
    if (VAL_BYTE_SIZE(val)) {
        REBYTE *bp = VAL_BIN_DATA(val);
        if (limit == 0xff) return R_TRUE; // by definition
        for (; len > 0; len--, bp++)
            if (*bp > limit) return R_FALSE;
    } else {
        REBUNI *up = VAL_UNI_DATA(val);
        for (; len > 0; len--, up++)
            if (*up > limit) return R_FALSE;
    }

    return R_TRUE;
}


//
//  ascii?: native [
//  
//  {Returns TRUE if value or string is in ASCII character range (below 128).}
//  
//      value [any-string! char! integer!]
//  ]
//
REBNATIVE(asciiq)
{
    return Check_Char_Range(D_ARG(1), 0x7f);
}


//
//  latin1?: native [
//  
//  {Returns TRUE if value or string is in Latin-1 character range (below 256).}
//  
//      value [any-string! char! integer!]
//  ]
//
REBNATIVE(latin1q)
{
    return Check_Char_Range(D_ARG(1), 0xff);
}


//
//  Is_Type_Of: C
// 
// Types can be: word or block. Each element must be either
// a datatype or a typeset.
//
static REBOOL Is_Type_Of(const REBVAL *value, REBVAL *types)
{
    const REBVAL *val;

    val = IS_WORD(types) ? GET_VAR(types) : types;

    if (IS_DATATYPE(val)) {
        return (VAL_TYPE_KIND(val) == VAL_TYPE(value));
    }

    if (IS_TYPESET(val)) {
        return (TYPE_CHECK(val, VAL_TYPE(value)));
    }

    if (IS_BLOCK(val)) {
        for (types = VAL_BLK_DATA(val); NOT_END(types); types++) {
            val = IS_WORD(types) ? GET_VAR(types) : types;
            if (IS_DATATYPE(val)) {
                if (VAL_TYPE_KIND(val) == VAL_TYPE(value)) return TRUE;
            }
            else if (IS_TYPESET(val)) {
                if (TYPE_CHECK(val, VAL_TYPE(value))) return TRUE;
            }
            else
                fail (Error(RE_INVALID_TYPE, Type_Of(val)));
        }
        return FALSE;
    }

    fail (Error_Invalid_Arg(types));
}


//
//  assert: native [
//  
//  {Assert that condition is true, else cause an assertion error.}
//  
//      conditions [block!]
//      /type {Safely check datatypes of variables (words and paths)}
//  ]
//
REBNATIVE(assert)
{
    REBVAL *value = D_ARG(1);  // block, logic, or none

    if (!D_REF(2)) {
        REBSER *block = VAL_SERIES(value);
        REBCNT index = VAL_INDEX(value);
        REBCNT i;

        while (index < SERIES_TAIL(block)) {
            i = index;
            DO_NEXT_MAY_THROW(index, D_OUT, block, index);

            if (index == THROWN_FLAG) return R_OUT_IS_THROWN;

            if (IS_CONDITIONAL_FALSE(D_OUT)) {
                // !!! Only copies 3 values (and flaky), see CC#2231
                Val_Init_Block(D_OUT, Copy_Array_At_Max_Shallow(block, i, 3));
                fail (Error(RE_ASSERT_FAILED, D_OUT));
            }
        }
        SET_TRASH_SAFE(D_OUT);
    }
    else {
        // /types [var1 integer!  var2 [integer! decimal!]]
        const REBVAL *val;
        REBVAL *type;

        for (value = VAL_BLK_DATA(value); NOT_END(value); value += 2) {
            if (IS_WORD(value)) {
                val = GET_VAR(value);
            }
            else if (IS_PATH(value)) {
                const REBVAL *refinements = value;
                Do_Path(D_OUT, &refinements, 0);
                val = D_OUT;
            }
            else
                fail (Error_Invalid_Arg(value));

            type = value+1;
            if (IS_END(type)) fail (Error(RE_MISSING_ARG));
            if (IS_BLOCK(type) || IS_WORD(type) || IS_TYPESET(type) || IS_DATATYPE(type)) {
                if (!Is_Type_Of(val, type))
                    fail (Error(RE_WRONG_TYPE, value));
            }
            else
                fail (Error_Invalid_Arg(type));
        }
    }

    return R_TRUE;
}


//
//  as-pair: native [
//  
//  "Combine X and Y values into a pair."
//  
//      x [any-number!]
//      y [any-number!]
//  ]
//
REBNATIVE(as_pair)
{
    REBVAL *val = D_ARG(1);

    VAL_SET(D_OUT, REB_PAIR);

    if (IS_INTEGER(val)) {
        VAL_PAIR_X(D_OUT) = cast(REBD32, VAL_INT64(val));
    }
    else {
        VAL_PAIR_X(D_OUT) = cast(REBD32, VAL_DECIMAL(val));
    }

    val = D_ARG(2);
    if (IS_INTEGER(val)) {
        VAL_PAIR_Y(D_OUT) = cast(REBD32, VAL_INT64(val));
    }
    else {
        VAL_PAIR_Y(D_OUT) = cast(REBD32, VAL_DECIMAL(val));
    }

    return R_OUT;
}


//
//  bind: native [
//  
//  "Binds words to the specified context."
//  
//      word [block! any-word!] "A word or block (modified) (returned)"
//      context [any-word! any-object!] "A reference to the target context"
//      /copy {Bind and return a deep copy of a block, don't modify original}
//      /only "Bind only first block (not deep)"
//      /new "Add to context any new words found"
//      /set "Add to context any new set-words found"
//  ]
//
REBNATIVE(bind)
{
    REBVAL *arg;
    REBSER *blk;
    REBSER *frame;
    REBCNT flags;
    REBFLG rel = FALSE;

    flags = D_REF(4) ? 0 : BIND_DEEP;
    if (D_REF(5)) flags |= BIND_ALL;
    if (D_REF(6)) flags |= BIND_SET;

    // Get context from a word, object (or port);
    arg = D_ARG(2);
    if (ANY_OBJECT(arg))
        frame = VAL_OBJ_FRAME(arg);
    else {
        assert(ANY_WORD(arg));
        rel = (VAL_WORD_INDEX(arg) < 0);
        frame = VAL_WORD_FRAME(arg);
        if (!frame) fail (Error(RE_NOT_BOUND, arg));
    }

    // Block or word to bind:
    arg = D_ARG(1);

    // Bind single word:
    if (ANY_WORD(arg)) {
        if (rel) {
            Bind_Stack_Word(frame, arg);
            return R_ARG1;
        }
        if (!Bind_Word(frame, arg)) {
            if (flags & BIND_ALL)
                Append_Frame(frame, arg, 0); // not in context, so add it.
            else
                fail (Error(RE_NOT_IN_CONTEXT, arg));
        }
        return R_ARG1;
    }

    // Copy block if necessary (/copy):
    blk = D_REF(3)
        ? Copy_Array_At_Deep_Managed(VAL_SERIES(arg), VAL_INDEX(arg))
        : VAL_SERIES(arg);

    Val_Init_Block_Index(D_OUT, blk, D_REF(3) ? 0 : VAL_INDEX(arg));

    if (rel)
        Bind_Stack_Block(frame, blk); //!! needs deep
    else
        Bind_Values_Core(BLK_HEAD(blk), frame, flags);

    return R_OUT;
}


//
//  bound?: native [
//  
//  "Returns the context in which a word is bound."
//  
//      word [any-word!]
//  ]
//
REBNATIVE(boundq)
{
    REBVAL *word = D_ARG(1);

    if (!HAS_FRAME(word)) return R_NONE;

    if (VAL_WORD_INDEX(word) < 0) {
        // Function frames use negative numbers to indicate they are
        // "stack relative" bindings.  Hence there is no way to get
        // their value if the function is not running.  (This is why
        // if you leak a local word to your caller and they look it
        // up they get an error).
        //
        // Historically there was nothing you could do with a function
        // word frame.  But then slot 0 (which had been unused, as the
        // params start at 1) was converted to hold the value of the
        // function the params belong to.  This returns that stored value.

        *D_OUT = *BLK_HEAD(VAL_WORD_FRAME(word));

        // You should never get a way to a stack relative binding of a
        // closure.  They make an object on each call.

        assert(IS_FUNCTION(D_OUT));
    }
    else {
        // It's just an object.  Note that if objects were adapted to use
        // the same technique (they will be) then it would be possible to
        // get a "full value"-sized objects (if an object were not entirely
        // recoverable from a series value alone, which for instance a
        // MODULE! would not be.)

        Val_Init_Object(D_OUT, VAL_WORD_FRAME(word));
    }

    return R_OUT;
}


//
//  set?: native [
//  
//  "Returns whether a value is set."
//  
//      cell [unset! any-value!]
//  ]
//
REBNATIVE(setq)
{
    return IS_UNSET(D_ARG(1)) ? R_FALSE : R_TRUE;
}


//
//  unbind: native [
//  
//  "Unbinds words from context."
//  
//      word [block! any-word!] "A word or block (modified) (returned)"
//      /deep "Process nested blocks"
//  ]
//
REBNATIVE(unbind)
//
// word | context
// /deep
{
    REBVAL *word = D_ARG(1);

    if (ANY_WORD(word))
        UNBIND_WORD(word);
    else
        Unbind_Values_Core(VAL_BLK_DATA(word), NULL, D_REF(2));

    return R_ARG1;
}


//
//  collect-words: native [
//  
//  {Collect unique words used in a block (used for context construction).}
//  
//      block [block!]
//      /deep "Include nested blocks"
//      /set "Only include set-words"
//      /ignore "Ignore prior words"
//      words [any-object! block! none!] "Words to ignore"
//  ]
//
REBNATIVE(collect_words)
{
    REBSER *words;
    REBCNT modes = 0;
    REBVAL *values = VAL_BLK_DATA(D_ARG(1));
    REBVAL *prior_values = NULL;
    REBVAL *obj;

    if (D_REF(2)) modes |= BIND_DEEP;
    if (!D_REF(3)) modes |= BIND_ALL;

    // If ignore, then setup for it:
    if (D_REF(4)) {
        obj = D_ARG(5);
        if (ANY_OBJECT(obj))
            prior_values = BLK_SKIP(VAL_OBJ_KEYLIST(obj), 1);
        else if (IS_BLOCK(obj))
            prior_values = VAL_BLK_DATA(obj);
        // else stays 0
    }

    words = Collect_Words(values, prior_values, modes);
    Val_Init_Block(D_OUT, words);
    return R_OUT;
}


//
//  get: native [
//  
//  {Gets the value of a word or path, or values of an object.}
//  
//      word "Word, path, object to get"
//      /any "Allows word to have no value (allows unset)"
//  ]
//
REBNATIVE(get)
{
    REBVAL *word = D_ARG(1);

    if (ANY_WORD(word)) {
        const REBVAL *val = GET_VAR(word);
        if (IS_FRAME(val)) {
            Init_Obj_Value(D_OUT, VAL_WORD_FRAME(word));
            return R_OUT;
        }
        if (!D_REF(2) && !IS_SET(val)) fail (Error(RE_NO_VALUE, word));
        *D_OUT = *val;
    }
    else if (ANY_PATH(word)) {
        const REBVAL *refinements = word;
        REBVAL *val = Do_Path(D_OUT, &refinements, 0);
        if (!val) {
            val = D_OUT;
        }
        if (!D_REF(2) && !IS_SET(val)) fail (Error(RE_NO_VALUE, word));
    }
    else if (IS_OBJECT(word)) {
        Assert_Public_Object(word);
        Val_Init_Block(D_OUT, Copy_Array_At_Shallow(VAL_OBJ_FRAME(word), 1));
    }
    else *D_OUT = *word; // all other values

    return R_OUT;
}


//
//  to-value: native [
//  
//  {Turns unset to NONE, with ANY-VALUE! passing through. (See: OPT)}
//  
//      value [any-value!]
//  ]
//
REBNATIVE(to_value)
{
    return IS_UNSET(D_ARG(1)) ? R_NONE : R_ARG1;
}


//
//  opt: native [
//  
//  {NONEs become unset, all other value types pass through. (See: TO-VALUE)}
//  
//      value [any-value!]
//  ]
//
REBNATIVE(opt)
{
    return IS_NONE(D_ARG(1)) ? R_UNSET : R_ARG1;
}


//
//  in: native [
//  
//  "Returns the word or block in the object's context."
//  
//      object [any-object! block!]
//      word [any-word! block! paren!] "(modified if series)"
//  ]
//
REBNATIVE(in)
{
    REBVAL *val  = D_ARG(1); // object, error, port, block
    REBVAL *word = D_ARG(2);
    REBCNT index;
    REBSER *frame;

    if (IS_BLOCK(val) || IS_PAREN(val)) {
        if (IS_WORD(word)) {
            const REBVAL *v;
            REBCNT i;
            for (i = VAL_INDEX(val); i < VAL_TAIL(val); i++) {
                REBVAL safe;
                v = VAL_BLK_SKIP(val, i);
                Get_Simple_Value_Into(&safe, v);
                v = &safe;
                if (IS_OBJECT(v)) {
                    frame = VAL_OBJ_FRAME(v);
                    index = Find_Word_Index(frame, VAL_WORD_SYM(word), FALSE);
                    if (index > 0) {
                        VAL_WORD_INDEX(word) = (REBCNT)index;
                        VAL_WORD_FRAME(word) = frame;
                        *D_OUT = *word;
                        return R_OUT;
                    }
                }
            }
            return R_NONE;
        }
        else
            fail (Error_Invalid_Arg(word));
    }

    frame = IS_ERROR(val) ? VAL_ERR_OBJECT(val) : VAL_OBJ_FRAME(val);

    // Special form: IN object block
    if (IS_BLOCK(word) || IS_PAREN(word)) {
        Bind_Values_Deep(VAL_BLK_HEAD(word), frame);
        return R_ARG2;
    }

    index = Find_Word_Index(frame, VAL_WORD_SYM(word), FALSE);

    if (index > 0) {
        VAL_WORD_INDEX(word) = (REBCNT)index;
        VAL_WORD_FRAME(word) = frame;
        *D_OUT = *word;
    } else
        return R_NONE;
    return R_OUT;
}


//
//  and?: native [
//  
//  {Returns true if both values are conditionally true (no "short-circuit")}
//  
//      value1
//      value2
//  ]
//
REBNATIVE(andq)
{
    if (IS_CONDITIONAL_TRUE(D_ARG(1)) && IS_CONDITIONAL_TRUE(D_ARG(2)))
        return R_TRUE;
    else
        return R_FALSE;
}


//
//  not?: native [
//  
//  "Returns the logic complement."
//  
//      value "(Only FALSE and NONE return TRUE)"
//  ]
//
REBNATIVE(notq)
{
    return IS_CONDITIONAL_FALSE(D_ARG(1)) ? R_TRUE : R_FALSE;
}


//
//  or?: native [
//  
//  {Returns true if either value is conditionally true (no "short-circuit")}
//  
//      value1
//      value2
//  ]
//
REBNATIVE(orq)
{
    if (IS_CONDITIONAL_TRUE(D_ARG(1)) || IS_CONDITIONAL_TRUE(D_ARG(2)))
        return R_TRUE;
    else
        return R_FALSE;
}


//
//  xor?: native [
//  
//  {Returns true if only one of the two values is conditionally true.}
//  
//      value1
//      value2
//  ]
//
REBNATIVE(xorq)
{
    // Note: no boolean ^^ in C; normalize to booleans and check unequal
    if (!IS_CONDITIONAL_TRUE(D_ARG(1)) != !IS_CONDITIONAL_TRUE(D_ARG(2)))
        return R_TRUE;
    else
        return R_FALSE;
}


//
//  resolve: native [
//  
//  {Copy context by setting values in the target from those in the source.}
//  
//      target [any-object!] "(modified)"
//      source [any-object!]
//      /only from [block! integer!] 
//      {Only specific words (exports) or new words in target (index to tail)}
//      /all 
//      {Set all words, even those in the target that already have a value}
//      /extend "Add source words to the target if necessary"
//  ]
//
REBNATIVE(resolve)
{
    REBSER *target = VAL_OBJ_FRAME(D_ARG(1));
    REBSER *source = VAL_OBJ_FRAME(D_ARG(2));
    if (IS_INTEGER(D_ARG(4))) Int32s(D_ARG(4), 1); // check range and sign
    Resolve_Context(target, source, D_ARG(4), D_REF(5), D_REF(6)); // /from /all /expand
    return R_ARG1;
}


//
//  set: native [
//  
//  {Sets a word, path, block of words, or object to specified value(s).}
//  
//      word [any-word! any-path! block! object!] 
//      {Word, block of words, path, or object to be set (modified)}
//      value [any-value!] "Value or block of values"
//      /any "Allows setting words to any value, including unset"
//      /pad 
//      {For objects, if block is too short, remaining words are set to NONE}
//  ]
//
REBNATIVE(set)
//
// word [any-word! block! object!] {Word or words to set}
// value [any-value!] {Value or block of values}
// /any {Allows setting words to any value.}
// /pad {For objects, if block is too short, remaining words are set to NONE.}
{
    const REBVAL *word = D_ARG(1);
    REBVAL *val    = D_ARG(2);
    REBVAL *tmp    = NULL;
    REBOOL not_any = !D_REF(3);
    REBOOL is_blk  = FALSE;

    if (not_any && !IS_SET(val))
        fail (Error(RE_NEED_VALUE, word));

    if (ANY_WORD(word)) {
        Set_Var(word, val);
        return R_ARG2;
    }

    if (ANY_PATH(word)) {
        REBVAL dummy;
        Do_Path(&dummy, &word, val);
        // !!! ignores results?
        return R_ARG2;
    }

    // Is value a block?
    if (IS_BLOCK(val)) {
        val = VAL_BLK_DATA(val);
        if (IS_END(val)) val = NONE_VALUE;
        else is_blk = TRUE;
    }

    // Is target an object?
    if (IS_OBJECT(word)) {
        REBVAL *key = VAL_OBJ_KEY(word, 1); // skip self
        REBVAL *obj_value = VAL_OBJ_VALUES(D_ARG(1)) + 1;

        Assert_Public_Object(word);
        // Check for protected or unset before setting anything.
        tmp = val;
        for (; NOT_END(key); key++) {
            if (VAL_GET_EXT(key, EXT_WORD_LOCK))
                fail (Error_Protected_Key(key));
            if (not_any && is_blk && !IS_END(tmp) && IS_UNSET(tmp++)) {
                // (Loop won't advance past end)
                REBVAL key_name;
                Val_Init_Word_Unbound(
                    &key_name, REB_WORD, VAL_TYPESET_SYM(key)
                );
                fail (Error(RE_NEED_VALUE, &key_name));
            }
        }

        for (; NOT_END(obj_value); obj_value++) { // skip self
            // WARNING: Unwinds that make it here are assigned. All unwinds
            // should be screened earlier (as is done in e.g. REDUCE, or for
            // function arguments) so they don't even get into this function.
            *obj_value = *val;
            if (is_blk) {
                val++;
                if (IS_END(val)) {
                    if (!D_REF(4)) break; // /pad not provided
                    is_blk = FALSE;
                    val = NONE_VALUE;
                }
            }
        }
    } else { // Set block of words:
        if (not_any && is_blk) { // Check for unset before setting anything.
            for (tmp = val, word = VAL_BLK_DATA(word); NOT_END(word) && NOT_END(tmp); word++, tmp++) {
                switch (VAL_TYPE(word)) {
                case REB_WORD:
                case REB_SET_WORD:
                case REB_LIT_WORD:
                    if (!IS_SET(tmp)) fail (Error(RE_NEED_VALUE, word));
                    break;
                case REB_GET_WORD:
                    if (!IS_SET(IS_WORD(tmp) ? GET_VAR(tmp) : tmp))
                        fail (Error(RE_NEED_VALUE, word));
                }
            }
        }
        for (word = VAL_BLK_DATA(D_ARG(1)); NOT_END(word); word++) {
            if (IS_WORD(word) || IS_SET_WORD(word) || IS_LIT_WORD(word))
                Set_Var(word, val);
            else if (IS_GET_WORD(word))
                Set_Var(word, IS_WORD(val) ? GET_VAR(val) : val);
            else
                fail (Error_Invalid_Arg(word));

            if (is_blk) {
                val++;
                if (IS_END(val)) is_blk = FALSE, val = NONE_VALUE;
            }
        }
    }

    return R_ARG2;
}


//
//  type-of: native [
//  
//  "Returns the datatype of a value."
//  
//      value [any-value!]
//  ]
//
REBNATIVE(type_of)
{
    REBCNT type = VAL_TYPE(D_ARG(1));
    Val_Init_Datatype(D_OUT, type);
    return R_OUT;
}


//
//  unset: native [
//  
//  {Unsets the value of a word (in its current context.)}
//  
//      word [any-word! block!] "Word or block of words"
//  ]
//
REBNATIVE(unset)
{
    REBVAL * const value = D_ARG(1);
    REBVAL *var;
    REBVAL *word;

    if (ANY_WORD(value)) {
        word = value;

        if (!HAS_FRAME(word)) fail (Error(RE_NOT_BOUND, word));

        var = GET_MUTABLE_VAR(word);
        SET_UNSET(var);
    }
    else {
        assert(IS_BLOCK(value));

        for (word = VAL_BLK_DATA(value); NOT_END(word); word++) {
            if (!ANY_WORD(word))
                fail (Error_Invalid_Arg(word));

            if (!HAS_FRAME(word)) fail (Error(RE_NOT_BOUND, word));

            var = GET_MUTABLE_VAR(word);
            SET_UNSET(var);
        }
    }
    return R_UNSET;
}


//
//  infix?: native [
//  
//  {Returns TRUE if the function gets its first argument prior to the call}
//  
//      value [any-function!]
//  ]
//
REBNATIVE(infixq)
{
    REBVAL *func = D_ARG(1);

    assert(ANY_FUNC(func));
    if (VAL_GET_EXT(func, EXT_FUNC_INFIX))
        return R_TRUE;

    return R_FALSE;
}


//
//  value?: native [
//  
//  "Returns TRUE if the word has a value."
//  
//      value
//  ]
//
REBNATIVE(valueq)
{
    const REBVAL *value = D_ARG(1);

    if (ANY_WORD(value) && !(value = TRY_GET_VAR(value)))
        return R_FALSE;
    if (IS_UNSET(value)) return R_FALSE;
    return R_TRUE;
}


//** SERIES ************************************************************


//
//  dump: native [
//  "Temporary debug dump"
//   v]
//
REBNATIVE(dump)
{
#ifdef _DEBUG
    REBVAL *arg = D_ARG(1);

    if (ANY_SERIES(arg))
        Dump_Series(VAL_SERIES(arg), "=>");
    else
        Dump_Values(arg, 1);
#endif
    return R_ARG1;
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
        len = GOB_TAIL(gob);
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
    REBVAL *val = D_ARG(1);
    REBGOB *gob = cast(REBGOB*, VAL_EVENT_SER(val));
    REBXYF xy;

    if (gob && GET_FLAG(VAL_EVENT_FLAGS(val), EVF_HAS_XY)) {
        xy.x = (REBD32)VAL_EVENT_X(val);
        xy.y = (REBD32)VAL_EVENT_Y(val);
        VAL_EVENT_SER(val) = cast(REBSER*, Map_Gob_Inner(gob, &xy));
        SET_EVENT_XY(val, ROUND_TO_INT(xy.x), ROUND_TO_INT(xy.y));
    }
    return R_ARG1;
}


//
//  Return_Gob_Pair: C
//
static void Return_Gob_Pair(REBVAL *out, REBGOB *gob, REBD32 x, REBD32 y)
{
    REBSER *blk;
    REBVAL *val;

    blk = Make_Array(2);
    Val_Init_Block(out, blk);
    val = Alloc_Tail_Array(blk);
    SET_GOB(val, gob);
    val = Alloc_Tail_Array(blk);
    VAL_SET(val, REB_PAIR);
    VAL_PAIR_X(val) = x;
    VAL_PAIR_Y(val) = y;
}


//
//  map-gob-offset: native [
//  
//  {Translates a gob and offset to the deepest gob and offset in it, returned as a block.}
//  
//      gob [gob!] "Starting object"
//      xy [pair!] "Staring offset"
//      /reverse "Translate from deeper gob to top gob."
//  ]
//
REBNATIVE(map_gob_offset)
{
    REBGOB *gob = VAL_GOB(D_ARG(1));
    REBD32 xo = VAL_PAIR_X(D_ARG(2));
    REBD32 yo = VAL_PAIR_Y(D_ARG(2));

    if (D_REF(3)) { // reverse
        REBINT max_depth = 1000; // avoid infinite loops
        while (GOB_PARENT(gob) && (max_depth-- > 0) &&
            !GET_GOB_FLAG(gob, GOBF_WINDOW)){
            xo += GOB_X(gob);
            yo += GOB_Y(gob);
            gob = GOB_PARENT(gob);
        }
    }
    else {
        REBXYF xy;
        xy.x = VAL_PAIR_X(D_ARG(2));
        xy.y = VAL_PAIR_Y(D_ARG(2));
        gob = Map_Gob_Inner(gob, &xy);
        xo = xy.x;
        yo = xy.y;
    }

    Return_Gob_Pair(D_OUT, gob, xo, yo);

    return R_OUT;
}
