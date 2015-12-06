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
REBNATIVE(ascii_q)
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
REBNATIVE(latin1_q)
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
                if (Do_Path_Throws(D_OUT, NULL, value, 0))
                    fail (Error_No_Catch_For_Throw(D_OUT));

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
//  "Binds words or words in arrays to the specified context."
//  
//      value [any-array! any-word!]
//          "A word or array (modified) (returned)"
//      target [any-word! any-context!]
//          "The target context or a word whose binding should be the target"
//      /copy
//          "Bind and return a deep copy of a block, don't modify original"
//      /only
//          "Bind only first block (not deep)"
//      /new
//          "Add to context any new words found"
//      /set
//          "Add to context any new set-words found"
//  ]
//
REBNATIVE(bind)
{
    PARAM(1, value);
    PARAM(2, target);
    REFINE(3, copy);
    REFINE(4, only);
    REFINE(5, new);
    REFINE(6, set);

    REBSER *target_series;
    REBSER *array;
    REBCNT flags;
    REBFLG is_relative;

    flags = REF(only) ? 0 : BIND_DEEP;
    if (REF(new)) flags |= BIND_ALL;
    if (REF(set)) flags |= BIND_SET;

    if (ANY_CONTEXT(ARG(target))) {
        //
        // Get target from an OBJECT!, ERROR!, PORT!, MODULE!
        //
        target_series = FRAME_VARLIST(VAL_FRAME(ARG(target)));
        is_relative = FALSE;
    }
    else {
        //
        // Extract target from whatever word we were given
        //
        // !!! Should we allow binding into FUNCTION! paramlists?  If not,
        // why not?  Were it a closure, it would be legal because a closure's
        // frame is just an object frame (at the moment).
        //
        assert(ANY_WORD(ARG(target)));
        is_relative = (VAL_WORD_INDEX(ARG(target)) < 0);
        target_series = VAL_WORD_TARGET(ARG(target));
        if (!target_series) fail (Error(RE_NOT_BOUND, ARG(target)));
    }

    // Bind single word:
    if (ANY_WORD(ARG(value))) {
        if (is_relative) {
            Bind_Stack_Word(target_series, ARG(value));
            return R_ARG1;
        }
        if (!Bind_Word(AS_FRAME(target_series), ARG(value))) {
            if (flags & BIND_ALL) {
                //
                // not in context, BIND_ALL means add it if it's not.
                //
                Append_Frame(AS_FRAME(target_series), ARG(value), 0);
            }
            else
                fail (Error(RE_NOT_IN_CONTEXT, ARG(value)));
        }
        return R_ARG1;
    }

    // Copy block if necessary (/copy):
    if (REF(copy)) {
        array = Copy_Array_At_Deep_Managed(
            VAL_SERIES(ARG(value)), VAL_INDEX(ARG(value))
        );
        Val_Init_Series_Index(
            D_OUT, VAL_TYPE(ARG(value)), array, 0
        );
    }
    else {
        array = VAL_SERIES(ARG(value));
        Val_Init_Series_Index(
            D_OUT, VAL_TYPE(ARG(value)), array, VAL_INDEX(ARG(value))
        );
    }

    if (is_relative)
        Bind_Relative(target_series, array); //!! needs deep
    else
        Bind_Values_Core(BLK_HEAD(array), AS_FRAME(target_series), flags);

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
REBNATIVE(bound_q)
{
    REBVAL *word = D_ARG(1);

    if (!HAS_TARGET(word)) return R_NONE;

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

        *D_OUT = *BLK_HEAD(VAL_WORD_TARGET(word));

        // You should never get a way to a stack relative binding of a
        // closure.  They make an object on each call.

        assert(IS_FUNCTION(D_OUT));
    }
    else {
        // It's an OBJECT!, ERROR!, MODULE!, PORT...we're not creating it
        // so we have to take its word on its canon value (which lives in
        // [0] of the varlist)
        //
        *D_OUT = *FRAME_CONTEXT(AS_FRAME(VAL_WORD_TARGET(word)));

        assert(ANY_CONTEXT(D_OUT));
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
REBNATIVE(set_q)
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
//      /deep
//          "Include nested blocks"
//      /set
//          "Only include set-words"
//      /ignore
//          "Ignore prior words"
//      words [any-context! block! none!]
//          "Words to ignore"
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
        if (ANY_CONTEXT(obj))
            prior_values = BLK_SKIP(FRAME_KEYLIST(VAL_FRAME(obj)), 1);
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
        if (!D_REF(2) && !IS_SET(val)) fail (Error(RE_NO_VALUE, word));
        *D_OUT = *val;
    }
    else if (ANY_PATH(word)) {
        if (Do_Path_Throws(D_OUT, NULL, word, 0))
            fail (Error_No_Catch_For_Throw(D_OUT));

        if (!D_REF(2) && !IS_SET(D_OUT)) fail (Error(RE_NO_VALUE, word));
    }
    else if (IS_OBJECT(word)) {
        Assert_Public_Object(word);

        // !!! This is a questionable feature, e.g.
        //
        //     >> get object [a: 10 b: 20]
        //     == [10 20]
        //
        // Certainly an oddity for GET.  Should either be turned into a
        // VALUES-OF reflector or otherwise gotten rid of.
        //
        Val_Init_Block(
            D_OUT, Copy_Array_At_Shallow(FRAME_VARLIST(VAL_FRAME(word)), 1)
        );
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
//  "Returns the word or block bound into the given context."
//  
//      context [any-context! block!]
//      word [any-word! block! paren!] "(modified if series)"
//  ]
//
REBNATIVE(in)
{
    REBVAL *val  = D_ARG(1); // object, error, port, block
    REBVAL *word = D_ARG(2);
    REBCNT index;
    REBFRM *frame;

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
                    frame = VAL_FRAME(v);
                    index = Find_Word_Index(frame, VAL_WORD_SYM(word), FALSE);
                    if (index > 0) {
                        VAL_WORD_INDEX(word) = index;
                        VAL_WORD_TARGET(word) = FRAME_VARLIST(frame);
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

    frame = IS_ERROR(val) ? VAL_FRAME(val) : VAL_FRAME(val);

    // Special form: IN object block
    if (IS_BLOCK(word) || IS_PAREN(word)) {
        Bind_Values_Deep(VAL_BLK_HEAD(word), frame);
        return R_ARG2;
    }

    index = Find_Word_Index(frame, VAL_WORD_SYM(word), FALSE);

    if (index > 0) {
        VAL_WORD_INDEX(word) = index;
        VAL_WORD_TARGET(word) = FRAME_VARLIST(frame);
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
REBNATIVE(and_q)
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
REBNATIVE(not_q)
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
REBNATIVE(or_q)
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
REBNATIVE(xor_q)
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
//      target [any-context!] "(modified)"
//      source [any-context!]
//      /only
//          "Only specific words (exports) or new words in target"
//      from [block! integer!]
//          "(index to tail)"
//      /all 
//          "Set all words, even those in the target that already have a value"
//      /extend
//          "Add source words to the target if necessary"
//  ]
//
REBNATIVE(resolve)
{
    PARAM(1, target);
    PARAM(2, source);
    REFINE(3, only);
    PARAM(4, from);
    REFINE(5, all);
    REFINE(6, extend);

    if (IS_INTEGER(ARG(from))) {
        // check range and sign
        Int32s(ARG(from), 1);
    }

    Resolve_Context(
        VAL_FRAME(ARG(target)),
        VAL_FRAME(ARG(source)),
        ARG(from),
        REF(all),
        REF(extend)
    );

    return R_ARG1;
}


//
//  set: native [
//  
//  {Sets a word, path, block of words, or context to specified value(s).}
//  
//      target [any-word! any-path! block! any-context!]
//          {Word, block of words, path, or object to be set (modified)}
//      value [any-value!]
//          "Value or block of values"
//      /any
//          "Allows setting words to any value, including unset"
//      /pad
//          {For objects, set remaining words to NONE if block is too short}
//  ]
//
REBNATIVE(set)
{
    PARAM(1, target);
    PARAM(2, value);
    REFINE(3, any);
    REFINE(4, pad);

    // Pointers independent from the arguments.  If we change them, they can
    // be reset to point at the original argument again.
    //
    REBVAL *target = ARG(target);
    REBVAL *value = ARG(value);
    REBOOL set_with_block;

    // Though Ren-C permits `x: ()`, you still have to say `set/any x ()` to
    // keep from getting an error.
    //
    // !!! This would likely be more clear as `set/opt`
    //
    if (!REF(any) && !IS_SET(value))
        fail (Error(RE_NEED_VALUE, target));

    // Simple request to set a word variable.  Allows ANY-WORD, which means
    // for instance that `set quote x: (expression)` would mean that the
    // locals gathering facility of FUNCTION would still gather x.
    //
    if (ANY_WORD(target)) {
        Set_Var(target, value);
        return R_ARG2;
    }

    if (ANY_PATH(target)) {
        REBVAL dummy;
        if (Do_Path_Throws(&dummy, NULL, target, value))
            fail (Error_No_Catch_For_Throw(&dummy));

        // If not a throw, then there is no result out of a setting a path,
        // we should return the value we passed in to set with.
        //
        return R_ARG2;
    }

    // If the target is either a context or a block, and the value used
    // to set with is ablock, then we want to do the assignments in
    // corresponding order to the elements:
    //
    //     >> set [a b] [1 2]
    //     >> print a
    //     1
    //     >> print b
    //     2
    //
    // Extract the value from the block at its index position.  (It may
    // be recovered again with `value = VAL_BLK_DATA(ARG(value))` if
    // it is changed.)
    //
    if ((set_with_block = IS_BLOCK(value))) {
        value = VAL_BLK_DATA(value);

        // If it's an empty block it's just going to be a no-op, so go ahead
        // and return now so the later code doesn't have to check for it.
        //
        if (IS_END(value))
            return R_ARG2;
    }

    if (ANY_CONTEXT(target)) {
        //
        // !!! The functionality of using a block to set ordered arguments
        // in an object depends on a notion of the object retaining a
        // guaranteed ordering of keys.  This is a somewhat restrictive
        // model which might need review.  Also, the idea that something
        // like `set object [a: 0 b: 0 c: 0] 1020` will set all the fields
        // to 1020 is a bit of a strange feature for the primitive.

        REBVAL *key = FRAME_KEYS_HEAD(VAL_FRAME(target));
        REBVAL *var;

        // To make SET somewhat atomic, before setting any of the object's
        // vars we make sure none of them are protected...and if we're not
        // tolerating unsets we check that the value being assigned is set.
        //
        for (; NOT_END(key); key++) {
            //
            // Hidden words are not shown in the WORDS-OF, and should not
            // count for consideration in positional setting.  Just skip.
            //
            if (VAL_GET_EXT(key, EXT_WORD_HIDE))
                continue;

            // Locked words cannot be modified, so a SET should error instead
            // of going ahead and changing them
            //
            if (VAL_GET_EXT(key, EXT_WORD_LOCK))
                fail (Error_Protected_Key(key));

            // If we're setting to a single value and not a block, then
            // we only need to check protect status (have to check all the
            // keys because all of them are set to the value).  We also
            // have to check all keys if we are going to pad the object.
            //
            if (!set_with_block) continue;

            if (!REF(any) && IS_UNSET(value)) {
                REBVAL key_name;
                Val_Init_Word_Unbound(
                    &key_name, REB_WORD, VAL_TYPESET_SYM(key)
                );
                fail (Error(RE_NEED_VALUE, &key_name));
            }

            // We knew it wasn't an end from the earlier check, but when we
            // increment it then it may become one.
            //
            value++;
            if (IS_END(value)) {
                if (REF(pad)) continue;
                break;
            }
        }

        // Refresh value from the arg data if we changed it during checking
        //
        if (set_with_block)
            value = VAL_BLK_DATA(ARG(value));
        else
            assert(value == VAL_BLK_DATA(ARG(value))); // didn't change

        // Refresh the key so we can check and skip hidden fields
        //
        key = FRAME_KEYS_HEAD(VAL_FRAME(target));
        var = FRAME_VARS_HEAD(VAL_FRAME(target));

        // With the assignments validated, set the variables in the object,
        // padding to NONE if requested
        //
        for (; NOT_END(key); key++, var++) {
            if (VAL_GET_EXT(key, EXT_WORD_HIDE))
                continue;

            if (IS_END(value)) {
                if (!REF(pad)) break;
                SET_NONE(var);
                continue;
            }
            *var = *value;
            if (set_with_block) value++;
        }

        return R_ARG2;
    }

    // Otherwise, it must be a BLOCK!... extract the value at index position
    //
    assert(IS_BLOCK(target));
    target = VAL_BLK_DATA(target);

    // SET should be somewhat atomic.  So if we're setting a block of
    // words and giving an alert on unsets, check for any unsets before
    // setting half the values and interrupting.
    //
    if (!REF(any)) {
        for (; NOT_END(target) && NOT_END(value); target++) {
            switch (VAL_TYPE(target)) {
            case REB_WORD:
            case REB_SET_WORD:
            case REB_LIT_WORD:
                if (!IS_SET(value)) {
                    assert(set_with_block); // if not, caught earlier...!
                    fail (Error(RE_NEED_VALUE, target));
                }
                break;

            case REB_GET_WORD:
                //
                // In this case, even if we're setting all the block
                // elements to the same value, it makes a difference if
                // it's a get-word for the !set_with_block too.
                //
                if (!IS_SET(IS_WORD(value) ? GET_VAR(value) : value))
                    fail (Error(RE_NEED_VALUE, target));
                break;
            }

            if (set_with_block)
                value++;
        }

        // Refresh the target and data pointers from the function args
        //
        target = VAL_BLK_DATA(ARG(target));
        if (set_with_block)
            value = VAL_BLK_DATA(ARG(value));
        else
            assert(value == VAL_BLK_DATA(ARG(value))); // didn't change
    }

    // With the assignments checked, do them
    //
    for (; NOT_END(target); target++) {
        if (IS_WORD(target) || IS_SET_WORD(target) || IS_LIT_WORD(target))
            Set_Var(target, value);
        else if (IS_GET_WORD(target)) {
            //
            // !!! Does a get of a WORD!, but what about of a PATH!?
            // Should parens be evaluated?  (They are in the function
            // arg handling of get-words as "hard quotes", for instance)
            // Not exactly the same thing, but worth contemplating.
            //
            Set_Var(target, IS_WORD(value) ? GET_VAR(value) : value);
        }
        else
            fail (Error_Invalid_Arg(target));

        if (set_with_block) {
            value++;
            if (IS_END(value)) {
                if (!REF(pad)) break;
                set_with_block = FALSE;
                value = NONE_VALUE;
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

        if (!HAS_TARGET(word)) fail (Error(RE_NOT_BOUND, word));

        var = GET_MUTABLE_VAR(word);
        SET_UNSET(var);
    }
    else {
        assert(IS_BLOCK(value));

        for (word = VAL_BLK_DATA(value); NOT_END(word); word++) {
            if (!ANY_WORD(word))
                fail (Error_Invalid_Arg(word));

            if (!HAS_TARGET(word)) fail (Error(RE_NOT_BOUND, word));

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
REBNATIVE(infix_q)
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
REBNATIVE(value_q)
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


#if !defined(NDEBUG)

//
//  VAL_TYPE_Debug: C
//
// Variant of VAL_TYPE() macro for the debug build which checks to ensure that
// you never call it on an END marker
//
enum Reb_Kind VAL_TYPE_Debug(const REBVAL *v) {
    assert(NOT_END(v));
    assert(!IS_TRASH_DEBUG(v)); // REB_TRASH is not a valid type to check for
    return cast(enum Reb_Kind, (v)->flags.bitfields.type);
}


//
//  VAL_SERIES_Ptr_Debug: C
//
// Variant of VAL_SERIES() macro for the debug build which checks to ensure
// that you have an ANY-SERIES! value you're calling it on (or one of the
// exception types that use REBSERs)
//
REBSER **VAL_SERIES_Ptr_Debug(const REBVAL *v) {
    assert(ANY_SERIES(v) || IS_MAP(v) || IS_VECTOR(v) || IS_IMAGE(v));
    return &(m_cast(REBVAL *, v))->data.position.series;
}


//
//  VAL_FRAME_Ptr_Debug: C
//
// Variant of VAL_FRAME() macro for the debug build which checks to ensure
// that you have an ANY-CONTEXT! value you're calling it on.
//
// !!! Unfortunately this loses const correctness; fix in C++ build.
//
REBFRM **VAL_FRAME_Ptr_Debug(const REBVAL *v) {
    assert(ANY_CONTEXT(v));
    return &(m_cast(REBVAL *, v))->data.context.frame;
}


//
//  IS_CONDITIONAL_FALSE_Debug: C
//
// Variant of IS_CONDITIONAL_FALSE() macro for the debug build which checks to
// ensure you never call it on an UNSET!
//
REBFLG IS_CONDITIONAL_FALSE_Debug(const REBVAL *v) {
    assert(!IS_UNSET(v));
    if (VAL_GET_OPT(v, OPT_VALUE_FALSE)) {
        assert(IS_NONE(v) || (IS_LOGIC(v) && !VAL_LOGIC(v)));
        return TRUE;
    }
    assert(!IS_NONE(v) && !(IS_LOGIC(v) && !VAL_LOGIC(v)));
    return FALSE;
}

#endif
