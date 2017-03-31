//
//  File: %n-data.c
//  Summary: "native functions for data and context"
//  Section: natives
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


static REBOOL Check_Char_Range(REBVAL *val, REBINT limit)
{
    if (IS_CHAR(val)) {
        if (VAL_CHAR(val) > limit) return FALSE;
        return TRUE;
    }

    if (IS_INTEGER(val)) {
        if (VAL_INT64(val) > limit) return FALSE;
        return TRUE;
    }

    REBCNT len = VAL_LEN_AT(val);
    if (VAL_BYTE_SIZE(val)) {
        REBYTE *bp = VAL_BIN_AT(val);
        if (limit == 0xff) return TRUE; // by definition
        for (; len > 0; len--, bp++)
            if (*bp > limit) return FALSE;
    }
    else {
        REBUNI *up = VAL_UNI_AT(val);
        for (; len > 0; len--, up++)
            if (*up > limit) return FALSE;
    }

    return TRUE;
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
    INCLUDE_PARAMS_OF_ASCII_Q;

    return R_FROM_BOOL(Check_Char_Range(ARG(value), 0x7f));
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
    INCLUDE_PARAMS_OF_LATIN1_Q;

    return R_FROM_BOOL(Check_Char_Range(ARG(value), 0xff));
}


//
//  verify: native [
//
//  {Ensure conditions are TRUE?, even when not debugging (see also: ASSERT)}
//
//      return: [<opt>]
//      conditions [logic! block!]
//          {Block of conditions to evaluate, void and FALSE? trigger alerts}
//  ]
//
REBNATIVE(verify)
{
    INCLUDE_PARAMS_OF_VERIFY;

    if (IS_LOGIC(ARG(conditions))) {
        if (VAL_LOGIC(ARG(conditions)))
            return R_VOID;

        fail (Error_Verify_Failed_Raw(FALSE_VALUE));
    }

    REBFRM f;
    Push_Frame(&f, ARG(conditions));

    DECLARE_LOCAL (temp);

    while (NOT_END(f.value)) {
        const RELVAL *start = f.value;
        if (Do_Next_In_Frame_Throws(D_OUT, &f)) {
            Drop_Frame(&f);
            return R_OUT_IS_THROWN;
        }

        if (!IS_VOID(D_OUT) && IS_CONDITIONAL_TRUE(D_OUT))
            continue;

        Init_Block(
            temp,
            Copy_Values_Len_Shallow(start, f.specifier, f.value - start)
        );

        if (IS_VOID(D_OUT))
            fail (Error_Verify_Void_Raw(temp));

        fail (Error_Verify_Failed_Raw(temp));
    }

    Drop_Frame(&f);
    return R_VOID;
}


// Test used iteratively by MAYBE native.  Returns R_BLANK if the test fails,
// R_OUT if success, or R_OUT_IS_THROWN if a test throws.
//
inline static REB_R Do_Test_For_Maybe(
    REBVAL *out,
    const REBVAL *value,
    const RELVAL *test
) {
    if (IS_DATATYPE(test)) {
        if (VAL_TYPE_KIND(test) != VAL_TYPE(value))
            return R_BLANK;
        Move_Value(out, value);
        return R_OUT;
    }

    if (IS_TYPESET(test)) {
        if (!TYPE_CHECK(test, VAL_TYPE(value)))
            return R_BLANK;
        Move_Value(out, value);
        return R_OUT;
    }

    if (IS_FUNCTION(test)) {
        if (Apply_Only_Throws(out, TRUE, const_KNOWN(test), value, END))
            return R_OUT_IS_THROWN;

        if (IS_VOID(out))
            fail (Error_No_Return_Raw());

        if (IS_CONDITIONAL_FALSE(out))
            return R_BLANK;

        Move_Value(out, value);
        return R_OUT;
    }

    fail (Error_Invalid_Type(VAL_TYPE(test)));
}


//
//  maybe: native [
//
//  {Check value using tests (match types, TRUE? or FALSE?, filter function)}
//
//      return: [<opt> any-value!]
//          {The input value or BLANK! if no match, void if FALSE? and matched}
//      test [function! datatype! typeset! block! logic!]
//      value [<opt> any-value!]
//      /?
//          "Return LOGIC! of match vs. pass-through of value or blank"
//  ]
//
REBNATIVE(maybe)
{
    INCLUDE_PARAMS_OF_MAYBE; // ? is renamed as "q"

    REBVAL *test = ARG(test);
    REBVAL *value = ARG(value);

    if (IS_LOGIC(test)) {
        if (!IS_VOID(value) && VAL_LOGIC(test) == IS_CONDITIONAL_TRUE(value))
            goto type_matched;
        return REF(q) ? R_FALSE : R_BLANK;
    }

    REB_R r;
    if (IS_BLOCK(test)) {
        //
        // !!! What should the behavior for `MAYBE [] ...` be?  Should that be
        // an error?  People wouldn't write it literally, but could wind up
        // with an empty array as the product of a COMPOSE or something.
        // Consider it ambiguous for now and give back void...
        //
        r = R_VOID;

        const RELVAL *item;
        for (item = VAL_ARRAY_AT(test); NOT_END(item); ++item) {
            r = Do_Test_For_Maybe(
                D_OUT,
                value,
                IS_WORD(item)
                    ? Get_Opt_Var_May_Fail(item, VAL_SPECIFIER(test))
                    : item
            );

            if (r != R_BLANK)
                goto type_matched;
        }
    }
    else
        r = Do_Test_For_Maybe(D_OUT, value, test);

    if (r == R_OUT_IS_THROWN)
        return r;

    if (REF(q))
        return r == R_BLANK ? R_FALSE : R_TRUE;

    if (r == R_BLANK)
        return r;

    assert(r == R_OUT); // must have matched!

type_matched:
    if (REF(q))
        return R_TRUE;

    // Because there may be usages like `if maybe logic! x [print "logic!"]`,
    // it would be bad to take in a FALSE and pass back a FALSE.  This is
    // why /? (and its specialization MAYBE?) exist, but to help avoid
    // likely mistakes this returns a void.
    //
    if (IS_CONDITIONAL_FALSE(value))
        return R_VOID;

    return R_OUT;
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
    INCLUDE_PARAMS_OF_AS_PAIR;

    REBVAL *x = ARG(x);
    REBVAL *y = ARG(y);

    SET_PAIR(
        D_OUT,
        IS_INTEGER(x) ? VAL_INT64(x) : VAL_DECIMAL(x),
        IS_INTEGER(y) ? VAL_INT64(y) : VAL_DECIMAL(y)
    );

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
    INCLUDE_PARAMS_OF_BIND;

    REBVAL *value = ARG(value);
    REBVAL *target = ARG(target);

    REBCTX *context;

    REBARR *array;
    REBCNT flags = REF(only) ? BIND_0 : BIND_DEEP;

    REBU64 bind_types = TS_ANY_WORD;

    REBU64 add_midstream_types;
    if (REF(new)) {
        add_midstream_types = TS_ANY_WORD;
    }
    else if (REF(set)) {
        add_midstream_types = FLAGIT_KIND(REB_SET_WORD);
    }
    else
        add_midstream_types = 0;

    if (ANY_CONTEXT(target)) {
        //
        // Get target from an OBJECT!, ERROR!, PORT!, MODULE!, FRAME!
        //
        context = VAL_CONTEXT(target);
    }
    else {
        //
        // Extract target from whatever word we were given
        //
        assert(ANY_WORD(target));
        if (IS_WORD_UNBOUND(target))
            fail (Error_Not_Bound_Raw(target));

        // The word in hand may be a relatively bound one.  To return a
        // specific frame, this needs to ensure that the Reb_Frame's data
        // is a real context, not just a chunk of data.
        //
        context = VAL_WORD_CONTEXT(target);
    }

    if (ANY_WORD(value)) {
        //
        // Bind a single word

        if (Try_Bind_Word(context, value)) {
            Move_Value(D_OUT, value);
            return R_OUT;
        }

        // not in context, bind/new means add it if it's not.
        //
        if (REF(new) || (IS_SET_WORD(value) && REF(set))) {
            Append_Context(context, value, NULL);
            Move_Value(D_OUT, value);
            return R_OUT;
        }

        fail (Error_Not_In_Context_Raw(ARG(value)));
    }

    // Copy block if necessary (/copy)
    //
    // !!! NOTE THIS IS IGNORING THE INDEX!  If you ask to bind, it should
    // bind forward only from the index you specified, leaving anything
    // ahead of that point alone.  Not changing it now when finding it
    // because there could be code that depends on the existing (mis)behavior
    // but it should be followed up on.
    //
    Move_Value(D_OUT, value);
    if (REF(copy)) {
        array = Copy_Array_At_Deep_Managed(
            VAL_ARRAY(value), VAL_INDEX(value), VAL_SPECIFIER(value)
        );
        INIT_VAL_ARRAY(D_OUT, array); // warning: macro copies args
    }
    else
        array = VAL_ARRAY(value);

    Bind_Values_Core(
        ARR_HEAD(array),
        context,
        bind_types,
        add_midstream_types,
        flags
    );

    return R_OUT;
}


//
//  context-of: native [
//
//  "Returns the context in which a word is bound."
//
//      word [any-word!]
//  ]
//
REBNATIVE(context_of)
{
    INCLUDE_PARAMS_OF_CONTEXT_OF;

    if (IS_WORD_UNBOUND(ARG(word))) return R_BLANK;

    // Requesting the context of a word that is relatively bound may result
    // in that word having a FRAME! incarnated as a REBSER node (if it
    // was not already reified.)
    //
    // !!! Mechanically it is likely that in the future, all FRAME!s for
    // user functions will be reified from the moment of invocation.
    //
    Move_Value(D_OUT, CTX_VALUE(VAL_WORD_CONTEXT(ARG(word))));

    return R_OUT;
}


//
//  any-value?: native [
//
//  "Returns whether a data cell contains a value."
//
//      cell [<opt> any-value!]
//  ]
//
REBNATIVE(any_value_q)
{
    INCLUDE_PARAMS_OF_ANY_VALUE_Q;

    if (IS_VOID(ARG(cell)))
        return R_FALSE;
    return R_TRUE;
}


//
//  unbind: native [
//
//  "Unbinds words from context."
//
//      word [block! any-word!]
//          "A word or block (modified) (returned)"
//      /deep
//          "Process nested blocks"
//  ]
//
REBNATIVE(unbind)
{
    INCLUDE_PARAMS_OF_UNBIND;

    REBVAL *word = ARG(word);

    if (ANY_WORD(word))
        Unbind_Any_Word(word);
    else
        Unbind_Values_Core(VAL_ARRAY_AT(word), NULL, REF(deep));

    Move_Value(D_OUT, word);
    return R_OUT;
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
//      hidden [any-context! block!]
//          "Words to ignore"
//  ]
//
REBNATIVE(collect_words)
{
    INCLUDE_PARAMS_OF_COLLECT_WORDS;

    REBARR *words;
    REBCNT modes;
    RELVAL *values = VAL_ARRAY_AT(ARG(block));
    RELVAL *prior_values;

    if (REF(set))
        modes = COLLECT_ONLY_SET_WORDS;
    else
        modes = COLLECT_ANY_WORD;

    if (REF(deep)) modes |= COLLECT_DEEP;

    // If ignore, then setup for it:
    if (REF(ignore)) {
        if (ANY_CONTEXT(ARG(hidden))) {
            //
            // !!! These are typesets and not words.  Is Collect_Words able
            // to handle that?
            //
            prior_values = CTX_KEYS_HEAD(VAL_CONTEXT(ARG(hidden)));
        }
        else {
            assert(IS_BLOCK(ARG(hidden)));
            prior_values = VAL_ARRAY_AT(ARG(hidden));
        }
    }
    else
        prior_values = NULL;

    words = Collect_Words(values, prior_values, modes);
    Init_Block(D_OUT, words);
    return R_OUT;
}


//
//  get: native [
//
//  {Gets the value of a word or path, or values of a context.}
//
//      return: [<opt> any-value!]
//      source [blank! any-word! any-path! any-context! block!]
//          {Word, path, context to get}
//      /opt
//          "Optionally return no value if the source is not SET?"
//  ]
//
REBNATIVE(get)
//
// !!! Review if handling ANY-CONTEXT! is a good idea, or if that should be
// an independent reflector like VALUES-OF.
{
    INCLUDE_PARAMS_OF_GET;

    RELVAL *source;
    REBVAL *dest;
    REBSPC *specifier;

    if (ANY_CONTEXT(ARG(source))) {
        //
        // !!! This is a questionable feature, a shallow copy of the vars of
        // the context being put into a BLOCK!:
        //
        //     >> get make object! [[a b][a: 10 b: 20]]
        //     == [10 20]
        //
        // Certainly an oddity for GET.  Should either be turned into a
        // VARS-OF reflector or otherwise gotten rid of.  It is also another
        // potentially "order-dependent" exposure of the object's fields,
        // which may lead to people expecting an order.

        // !!! The array we create may have extra unused capacity, due to
        // the LEN including hidden fields which aren't going to be copied.
        //
        source = ARG(source);
        REBARR *array = Make_Array(CTX_LEN(VAL_CONTEXT(source)));
        REBVAL *dest = SINK(ARR_HEAD(array));

        REBVAL *key = CTX_KEYS_HEAD(VAL_CONTEXT(source));
        REBVAL *var = CTX_VARS_HEAD(VAL_CONTEXT(source));

        for (; NOT_END(key); key++, var++) {
            if (GET_VAL_FLAG(key, TYPESET_FLAG_HIDDEN))
                continue;

            // This only copies the value bits, so this is a "shallow" copy
            //
            Move_Value(dest, var);
            ++dest;
        }

        TERM_ARRAY_LEN(array, cast(RELVAL*, dest) - ARR_HEAD(array));
        Init_Block(D_OUT, array);
        return R_OUT;
    }

    // Move the argument into the single cell in the frame if it's not a
    // block, so the same enumeration-up-to-an-END marker can work on it
    // as for handling a block of items.
    //
    REBARR *results;

    if (IS_BLOCK(ARG(source))) {
        //
        // If a BLOCK! of gets are performed, voids cannot be put into the
        // resulting BLOCK!.  Hence for /OPT to be legal, it would have to
        // give back a BLANK! or other placeholder.  For now, error.
        //
        if (REF(opt))
            fail (Error_Bad_Refines_Raw());

        source = VAL_ARRAY_AT(ARG(source));
        specifier = VAL_SPECIFIER(ARG(source));

        results = Make_Array(VAL_LEN_AT(ARG(source)));
        TERM_ARRAY_LEN(results, VAL_LEN_AT(ARG(source)));
        dest = SINK(ARR_HEAD(results));
    }
    else {
        Move_Value(D_CELL, ARG(source));
        source = D_CELL;
        specifier = SPECIFIED;
        dest = D_OUT;
        results = NULL; // wasteful but avoids maybe-used-uninitalized warning
    }

    DECLARE_LOCAL (temp);

    for (; NOT_END(source); ++source, ++dest) {
        if (IS_BAR(source)) {
            //
            // `a: 10 | b: 20 | get [a | b]` will give back `[10 | 20]`.
            // While seemingly not a very useful feature standalone, this
            // compatibility with SET could come in useful so that blocks
            // don't have to be rearranged to filter out BAR!s.
            //
            SET_BAR(dest);
        }
        else if (IS_BLANK(source)) {
            //
            // R3-Alpha had the notion of "none propagation" where a routine
            // that it didn't make sense to take a non-value would use that
            // to allow a pass-thru to chain "failure" to other routines.
            // Ren-C has routines that tolerate blank inputs but that give
            // back a void, which avoids an immediate error alert but which
            // is more of a "hot potato" that needs to be handled.
            //
            SET_VOID(dest);
        }
        else if (ANY_WORD(source)) {
            Copy_Opt_Var_May_Fail(dest, source, specifier);
        }
        else if (ANY_PATH(source)) {
            //
            // Piggy-back on the GET-PATH! mechanic by copying to a temp
            // value and changing its type bits.
            //
            // !!! Review making a more efficient method of doing this.
            //
            Derelativize(temp, source, specifier);
            VAL_SET_TYPE_BITS(temp, REB_GET_PATH);

            // Here we DO it, which means that `get 'foo/bar` will act the same
            // as `:foo/bar` for all types.
            //
            if (Do_Path_Throws_Core(dest, NULL, temp, specifier, NULL))
                return R_OUT_IS_THROWN;

            // !!! Should this prohibit GROUP! evaluations?  Failure to do so
            // could make a GET able to have side-effects, which may not be
            // desirable, at least without a refinement.
        }

        if (NOT(REF(opt)) && IS_VOID(dest))
            fail (Error_No_Value_Core(source, specifier));
    }

    if (IS_BLOCK(ARG(source)))
        Init_Block(D_OUT, results);

    return R_OUT;
}


//
//  to-value: native [
//
//  {Turns unset to NONE, with ANY-VALUE! passing through. (See: OPT)}
//
//      return: [any-value!]
//      value [<opt> any-value!]
//  ]
//
REBNATIVE(to_value)
{
    INCLUDE_PARAMS_OF_TO_VALUE;

    if (IS_VOID(ARG(value)))
        return R_BLANK;

    Move_Value(D_OUT, ARG(value));
    return R_OUT;
}


//
//  opt: native [
//
//  {Convert blanks to optionals. (See Also: TO-VALUE)}
//
//      return: [<opt> any-value!]
//          {void if input was a BLANK!, or original value otherwise}
//      value [<opt> any-value!]
//  ]
//
REBNATIVE(opt)
{
    INCLUDE_PARAMS_OF_OPT;

    if (IS_BLANK(ARG(value)))
        return R_VOID;

    Move_Value(D_OUT, ARG(value));
    return R_OUT;
}


//
//  in: native [
//
//  "Returns the word or block bound into the given context."
//
//      context [any-context! block!]
//      word [any-word! block! group!] "(modified if series)"
//  ]
//
REBNATIVE(in)
//
// !!! The argument names here are bad... not necessarily a context and not
// necessarily a word.  `code` or `source` to be bound in a `target`, perhaps?
{
    INCLUDE_PARAMS_OF_IN;

    REBVAL *val = ARG(context); // object, error, port, block
    REBVAL *word = ARG(word);

    DECLARE_LOCAL (safe);

    if (IS_BLOCK(val) || IS_GROUP(val)) {
        if (IS_WORD(word)) {
            const REBVAL *v;
            REBCNT i;
            for (i = VAL_INDEX(val); i < VAL_LEN_HEAD(val); i++) {
                Get_Simple_Value_Into(
                    safe,
                    VAL_ARRAY_AT_HEAD(val, i),
                    VAL_SPECIFIER(val)
                );

                v = safe;
                if (IS_OBJECT(v)) {
                    REBCTX *context = VAL_CONTEXT(v);
                    REBCNT index = Find_Canon_In_Context(
                        context, VAL_WORD_CANON(word), FALSE
                    );
                    if (index != 0) {
                        CLEAR_VAL_FLAG(word, VALUE_FLAG_RELATIVE);
                        SET_VAL_FLAG(word, WORD_FLAG_BOUND);
                        INIT_WORD_CONTEXT(word, context);
                        INIT_WORD_INDEX(word, index);
                        Move_Value(D_OUT, word);
                        return R_OUT;
                    }
                }
            }
            return R_BLANK;
        }

        fail (Error_Invalid_Arg(word));
    }

    REBCTX *context = VAL_CONTEXT(val);

    // Special form: IN object block
    if (IS_BLOCK(word) || IS_GROUP(word)) {
        Bind_Values_Deep(VAL_ARRAY_HEAD(word), context);
        Move_Value(D_OUT, word);
        return R_OUT;
    }

    REBCNT index = Find_Canon_In_Context(context, VAL_WORD_CANON(word), FALSE);
    if (index == 0)
        return R_BLANK;

    Init_Any_Word_Bound(
        D_OUT,
        VAL_TYPE(word),
        VAL_WORD_SPELLING(word),
        context,
        index
    );
    return R_OUT;
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
    INCLUDE_PARAMS_OF_RESOLVE;

    if (IS_INTEGER(ARG(from))) {
        // check range and sign
        Int32s(ARG(from), 1);
    }

    UNUSED(REF(only)); // handled by noticing if ARG(from) is void
    Resolve_Context(
        VAL_CONTEXT(ARG(target)),
        VAL_CONTEXT(ARG(source)),
        ARG(from),
        REF(all),
        REF(extend)
    );

    Move_Value(D_OUT, ARG(target));
    return R_OUT;
}


//
//  set: native [
//
//  {Sets a word, path, block of words, or context to specified value(s).}
//
//      return: [<opt> any-value!]
//      target [any-word! any-path! block! any-context!]
//          {Word, block of words, path, or object to be set (modified)}
//      value [<opt> any-value!]
//          "Value or block of values"
//      /opt
//          "Value is optional, and if no value is provided unset the target"
//      /pad
//          {For objects, set remaining words to NONE if block is too short}
//      /lookback
//          {Function uses evaluator lookahead to "look back" (see SET-INFIX)}
//  ]
//
REBNATIVE(set)
{
    INCLUDE_PARAMS_OF_SET;

    if (NOT(REF(opt)) && IS_VOID(ARG(value)))
        fail (Error_Need_Value_Raw( ARG(target)));

    if (REF(lookback)) {
        if (!IS_FUNCTION(ARG(value)))
            fail (Error_Misc_Raw());

        // SET-INFIX checks for properties of the function to ensure it is
        // actually infix, and INFIX? tests specifically for that.  The only
        // things that should be checked here are to make sure things that
        // are impossible aren't being requested... e.g. a "look back quote
        // of a WORD!" (the word can't be quoted because it's evaluated
        // before the evaluator lookahead that would see the infix function).
        //
        // !!! Should arity 0 functions be prohibited?
    }

    // Simple request to set a word variable.  Allows ANY-WORD, which means
    // for instance that `set quote x: (expression)` would mean that the
    // locals gathering facility of FUNCTION would still gather x.
    //
    if (ANY_WORD(ARG(target))) {
        REBVAL *var = Get_Var_Core(ARG(target), SPECIFIED, GETVAR_MUTABLE);
        Move_Value(var, ARG(value));
        if (REF(lookback))
            SET_VAL_FLAG(var, VALUE_FLAG_ENFIXED);

        goto return_value_arg;
    }

    // !!! For starters, just the word form is supported for lookback.  Though
    // you can't dispatch a lookback from a path, you should be able to set
    // a word in a context to one.
    //
    if (REF(lookback))
        fail (Error_Misc_Raw());

    if (ANY_PATH(ARG(target))) {
        DECLARE_LOCAL (dummy);
        if (
            Do_Path_Throws_Core(
                dummy, NULL, ARG(target), SPECIFIED, ARG(value)
            )
        ) {
            fail (Error_No_Catch_For_Throw(dummy));
        }

        // If not a throw, then there is no result out of a setting a path,
        // we should return the value we passed in to set with.
        //
        goto return_value_arg;
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
    // be recovered again with `value = VAL_ARRAY_AT(ARG(value))` if
    // it is changed.)
    //
    REBOOL set_with_block; // goto would cross initialization
    set_with_block = IS_BLOCK(ARG(value));

    const RELVAL *value;
    REBSPC *value_specifier;
    if (set_with_block) {
        value = VAL_ARRAY_AT(ARG(value));
        value_specifier = VAL_SPECIFIER(ARG(value));

        // If it's an empty block it's just going to be a no-op, so go ahead
        // and return now so the later code doesn't have to check for it.
        //
        if (IS_END(value))
            goto return_value_arg;
    }
    else {
        value = ARG(value);
        value_specifier = SPECIFIED;
    }

    if (ANY_CONTEXT(ARG(target))) {
        //
        // !!! The functionality of using a block to set ordered arguments
        // in an object depends on a notion of the object retaining a
        // guaranteed ordering of keys.  This is a somewhat restrictive
        // model which might need review.  Also, the idea that something
        // like `set object [a: 0 b: 0 c: 0] 1020` will set all the fields
        // to 1020 is a bit of a strange feature for the primitive.

        REBVAL *key = CTX_KEYS_HEAD(VAL_CONTEXT(ARG(target)));
        REBVAL *var = CTX_VARS_HEAD(VAL_CONTEXT(ARG(target)));

        // To make SET somewhat atomic, before setting any of the object's
        // vars we make sure none of them are protected...and if we're not
        // tolerating unsets we check that the value being assigned is set.
        //
        for (; NOT_END(key); ++key, ++var) {
            //
            // Hidden words are not shown in the WORDS-OF, and should not
            // count for consideration in positional setting.  Just skip.
            //
            if (GET_VAL_FLAG(key, TYPESET_FLAG_HIDDEN))
                continue;

            // Protected words cannot be modified, so a SET should error
            // instead of going ahead and changing them
            //
            if (GET_VAL_FLAG(var, VALUE_FLAG_PROTECTED))
                fail (Error_Protected_Key(key));

            // If we're setting to a single value and not a block, then
            // we only need to check protect status (have to check all the
            // keys because all of them are set to the value).  We also
            // have to check all keys if we are going to pad the object.
            //
            if (!set_with_block) continue;

            if (NOT(REF(opt)) && IS_VOID(value)) {
                DECLARE_LOCAL (key_name);
                Init_Word(key_name, VAL_KEY_SPELLING(key));

                fail (Error_Need_Value_Raw( key_name));
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
            value = VAL_ARRAY_AT(ARG(value));
        else
            assert(value == VAL_ARRAY_AT(ARG(value))); // didn't change

        // Refresh the key so we can check and skip hidden fields
        //
        key = CTX_KEYS_HEAD(VAL_CONTEXT(ARG(target)));
        var = CTX_VARS_HEAD(VAL_CONTEXT(ARG(target)));

        // With the assignments validated, set the variables in the object,
        // padding to NONE if requested
        //
        for (; NOT_END(key); key++, var++) {
            if (GET_VAL_FLAG(key, TYPESET_FLAG_HIDDEN))
                continue;

            if (IS_END(value)) {
                if (NOT(REF(pad)))
                    break;
                SET_BLANK(var);
                continue;
            }
            Derelativize(var, value, value_specifier);
            if (set_with_block) value++;
        }

        goto return_value_arg;
    }

    // Otherwise, it must be a BLOCK!... extract the value at index position
    //
    assert(IS_BLOCK(ARG(target)));

    RELVAL *target; // goto would cross initialization
    target = VAL_ARRAY_AT(ARG(target));

    REBSPC *target_specifier; // goto would cross initialization
    target_specifier = VAL_SPECIFIER(ARG(target));

    // SET should be somewhat atomic.  So if we're setting a block of
    // words and giving an alert on unsets, check for any unsets before
    // setting half the values and interrupting.
    //
    if (NOT(REF(opt))) {
        for (; NOT_END(target) && NOT_END(value); target++) {
            assert(!IS_VOID(value)); // blocks may not contain voids

            if (IS_BAR(value))
                if (!IS_BAR(target))
                    fail (Error_Misc_Raw());

            switch (VAL_TYPE(target)) {
            case REB_BLANK:
                //
                // Throws away whatever value is there
                //
            case REB_WORD:
            case REB_SET_WORD:
            case REB_LIT_WORD:
                break;

            case REB_GET_WORD:
                //
                // In this case, even if we're setting all the block
                // elements to the same value, it makes a difference if
                // it's a get-word for the !set_with_block too.
                //
                if (IS_WORD(value)) // !!! why just WORD!, and not ANY-WORD!
                    if (IS_VOID(Get_Opt_Var_May_Fail(value, value_specifier)))
                        fail (Error_Need_Value_Raw( target));
                break;

            case REB_BAR:
                //
                // A BAR! must match a BAR!, e.g. `set [a | b] [1 | 2]` is
                // legal but `set [a | b] [1 2 3]` is not.
                //
                if (!IS_BAR(value))
                    fail (Error_Misc_Raw());
                break;

            default:
                // !!! Error is not caught here, but in the second loop...
                // Why two passes if the first pass isn't going to screen
                // for all errors?
                break;
            }

            if (set_with_block)
                value++;
        }

        // Refresh the target and data pointers from the function args
        //
        target = VAL_ARRAY_AT(ARG(target));
        if (set_with_block)
            value = VAL_ARRAY_AT(ARG(value));
        else
            assert(value == ARG(value)); // didn't change
    }

    // With the assignments checked, do them
    //
    for (; NOT_END(target); target++) {
        if (IS_BAR(target) || IS_BLANK(target)) {
            //
            // Just skip it (bars have already been checked that they line
            // up with other bars)
            //
        }
        else if (
            IS_WORD(target)
            || IS_SET_WORD(target)
            || IS_LIT_WORD(target)
        ) {
            Derelativize(
                Sink_Var_May_Fail(target, target_specifier),
                value,
                value_specifier
            );
        }
        else if (IS_GET_WORD(target)) {
            //
            // !!! Does a get of a WORD!, but what about of a PATH!?
            // Should parens be evaluated?  (They are in the function
            // arg handling of get-words as "hard quotes", for instance)
            // Not exactly the same thing, but worth contemplating.
            //
            if (IS_WORD(value)) {
                Copy_Opt_Var_May_Fail(
                    Sink_Var_May_Fail(target, target_specifier),
                    value,
                    value_specifier
                );
            }
            else {
                Derelativize(
                    Sink_Var_May_Fail(target, target_specifier),
                    value,
                    value_specifier
                );
            }
        }
        else
            fail (Error_Invalid_Arg_Core(target, target_specifier));

        if (set_with_block) {
            value++;
            if (IS_END(value)) {
                if (NOT(REF(pad)))
                    break;
                set_with_block = FALSE;
                value = BLANK_VALUE;
                value_specifier = SPECIFIED;
            }
        }
    }

return_value_arg:
    Move_Value(D_OUT, ARG(value));
    return R_OUT;
}


//
//  type-of: native [
//
//  "Returns the datatype of a value."
//
//      value [<opt> any-value!]
//  ]
//
REBNATIVE(type_of)
{
    INCLUDE_PARAMS_OF_TYPE_OF;

    enum Reb_Kind kind = VAL_TYPE(ARG(value));
    if (kind == REB_MAX_VOID)
        return R_BLANK;

    Val_Init_Datatype(D_OUT, kind);
    return R_OUT;
}



//
//  unset: native [
//
//  {Unsets the value of a word (in its current context.)}
//
//      return: [<opt>]
//      target [any-word! block!]
//          "Word or block of words"
//  ]
//
REBNATIVE(unset)
{
    INCLUDE_PARAMS_OF_UNSET;

    REBVAL *target = ARG(target);

    if (ANY_WORD(target)) {
        REBVAL *var = Sink_Var_May_Fail(target, SPECIFIED);
        SET_VOID(var);
        return R_VOID;
    }

    assert(IS_BLOCK(target));

    RELVAL *word;
    for (word = VAL_ARRAY_AT(target); NOT_END(word); ++word) {
        if (!ANY_WORD(word))
            fail (Error_Invalid_Arg_Core(word, VAL_SPECIFIER(target)));

        REBVAL *var = Sink_Var_May_Fail(word, VAL_SPECIFIER(target));
        SET_VOID(var);
    }

    return R_VOID;
}


//
//  lookback?: native [
//
//  {TRUE if looks up to a function and gets first argument before the call}
//
//      source [any-word! any-path!]
//  ]
//
REBNATIVE(lookback_q)
{
    INCLUDE_PARAMS_OF_LOOKBACK_Q;

    REBVAL *source = ARG(source);

    if (ANY_WORD(source)) {
        const REBVAL *var = Get_Var_Core(
            source, SPECIFIED, GETVAR_READ_ONLY // may fail()
        );

        if (!IS_FUNCTION(var))
            return R_FALSE;

        return R_FROM_BOOL(GET_VAL_FLAG(var, VALUE_FLAG_ENFIXED));
    }
    else {
        assert(ANY_PATH(source));

        // Not implemented yet...

        fail (Error_Misc_Raw());
    }
}


//
//  semiquoted?: native [
//
//  {Discern if a function parameter came from an "active" evaluation.}
//
//      parameter [word!]
//  ]
//
REBNATIVE(semiquoted_q)
//
// This operation is somewhat dodgy.  So even though the flag is carried by
// all values, and could be generalized in the system somehow to query on
// anything--we don't.  It's strictly for function parameters, and
// even then it should be restricted to functions that have labeled
// themselves as absolutely needing to do this for ergonomic reasons.
{
    INCLUDE_PARAMS_OF_SEMIQUOTED_Q;

    // !!! TBD: Enforce this is a function parameter (specific binding branch
    // makes the test different, and easier)

    const REBVAL *var = Get_Var_Core( // may fail
        ARG(parameter), SPECIFIED, GETVAR_READ_ONLY
    );
    return R_FROM_BOOL(GET_VAL_FLAG(var, VALUE_FLAG_UNEVALUATED));
}


//
//  semiquote: native [
//
//  {Marks a function argument to be treated as if it had been literal source}
//
//      value [any-value!]
//  ]
//
REBNATIVE(semiquote)
{
    INCLUDE_PARAMS_OF_SEMIQUOTE;

    Move_Value(D_OUT, ARG(value));

    // We cannot set the VALUE_FLAG_UNEVALUATED bit here and make it stick,
    // because the bit would just get cleared off by Do_Core when the
    // function finished.  So ask the evaluator to set the bit for us.

    return R_OUT_UNEVALUATED;
}


//
//  as: native [
//
//  {Aliases the underlying data of one series to act as another of same class}
//
//      type [datatype!]
//      value [any-series! any-word!]
//  ]
//
REBNATIVE(as)
{
    INCLUDE_PARAMS_OF_AS;

    enum Reb_Kind kind = VAL_TYPE_KIND(ARG(type));
    REBVAL *value = ARG(value);

    switch (kind) {
    case REB_BLOCK:
    case REB_GROUP:
    case REB_PATH:
    case REB_LIT_PATH:
    case REB_GET_PATH:
        if (!ANY_ARRAY(value))
            fail (Error_Invalid_Arg(value));
        break;

    case REB_STRING:
    case REB_TAG:
    case REB_FILE:
    case REB_URL:
        if (!ANY_BINSTR(value) || IS_BINARY(value))
            fail (Error_Invalid_Arg(value));
        break;

    case REB_WORD:
    case REB_GET_WORD:
    case REB_SET_WORD:
    case REB_LIT_WORD:
    case REB_ISSUE:
    case REB_REFINEMENT:
        if (!ANY_WORD(value))
            fail (Error_Invalid_Arg(value));
        break;

    default:
        panic(value); // all applicable types should be handled above
    }

    VAL_SET_TYPE_BITS(value, kind);
    Move_Value(D_OUT, value);
    return R_OUT;
}


//
//  aliases?: native [
//
//  {Return whether or not the underlying data of one value aliases another}
//
//     value1 [any-series!]
//     value2 [any-series!]
//  ]
//
REBNATIVE(aliases_q)
{
    INCLUDE_PARAMS_OF_ALIASES_Q;

    if (VAL_SERIES(ARG(value1)) == VAL_SERIES(ARG(value2)))
        return R_TRUE;

    return R_FALSE;
}


// Common routine for both SET? and UNSET?  Note that location is modified
// into a GET-PATH! value if it is originally a path (okay for the natives,
// since they can modify values in their frames.)
//
inline static REBOOL Is_Set_Modifies(REBVAL *location)
{
    if (ANY_WORD(location)) {
        //
        // Note this will fail if unbound
        //
        const RELVAL *var = Get_Opt_Var_May_Fail(location, SPECIFIED);
        if (IS_VOID(var))
            return FALSE;
    }
    else {
        assert(ANY_PATH(location));

    #if !defined(NDEBUG)
        REBDSP dsp_orig = DSP;
    #endif

        // !!! We shouldn't be evaluating but currently the path machinery
        // doesn't "turn off" GROUP! evaluations for GET-PATH!.
        //
        VAL_SET_TYPE_BITS(location, REB_GET_PATH);

        DECLARE_LOCAL (temp);
        if (Do_Path_Throws_Core(
            temp, NULL, location, VAL_SPECIFIER(location), NULL
        )) {
            // !!! Shouldn't be evaluating, much less throwing--so fail
            //
            fail (Error_No_Catch_For_Throw(temp));
        }

        // We did not pass in a symbol ID
        //
        assert(DSP == dsp_orig);
        if (IS_VOID(temp))
            return FALSE;
    }

    return TRUE;
}


//
//  set?: native/body [
//
//  "Whether a bound word or path is set (!!! shouldn't eval GROUP!s)"
//
//      location [any-word! any-path!]
//  ][
//      any-value? get/opt location
//  ]
//
REBNATIVE(set_q)
{
    INCLUDE_PARAMS_OF_SET_Q;

    return R_FROM_BOOL(Is_Set_Modifies(ARG(location)));
}


//
//  unset?: native/body [
//
//  "Whether a bound word or path is unset (!!! shouldn't eval GROUP!s)"
//
//      location [any-word! any-path!]
//  ][
//      void? get/opt location
//  ]
//
REBNATIVE(unset_q)
{
    INCLUDE_PARAMS_OF_UNSET_Q;

    return R_FROM_BOOL(NOT(Is_Set_Modifies(ARG(location))));
}


//
//  true?: native/body [
//
//  "Returns true if a value can be used as true."
//
//      value [any-value!] ; Note: No [<opt> any-value!] - void must fail
//  ][
//      not not :val
//  ]
//
REBNATIVE(true_q)
{
    INCLUDE_PARAMS_OF_TRUE_Q;

    return R_FROM_BOOL(IS_CONDITIONAL_TRUE(ARG(value)));
}


//
//  false?: native/body [
//
//  "Returns false if a value is either LOGIC! false or a NONE!."
//
//      value [any-value!] ; Note: No [<opt> any-value!] - void must fail.
//  ][
//      either any [
//          blank? :value
//          :value = false
//      ][
//          true
//      ][
//          false
//      ]
//  ]
//
REBNATIVE(false_q)
{
    INCLUDE_PARAMS_OF_FALSE_Q;

    return R_FROM_BOOL(IS_CONDITIONAL_FALSE(ARG(value)));
}


//
//  quote: native/body [
//
//  "Returns the value passed to it without evaluation."
//
//      return: [any-value!]
//      :value [any-value!]
//  ][
//      :value ;-- actually also sets unevaluated bit, how could a user do so?
//  ]
//
REBNATIVE(quote)
{
    INCLUDE_PARAMS_OF_QUOTE;

    Move_Value(D_OUT, ARG(value));

    // We cannot set the VALUE_FLAG_UNEVALUATED bit here and make it stick,
    // because the bit would just get cleared off by Do_Core when the
    // function finished.  Ask evaluator to add the bit for us.

    return R_OUT_UNEVALUATED;
}


//
//  void?: native/body [
//
//  "Tells you if the argument is not a value (e.g. `void? do []` is TRUE)"
//
//      value [<opt> any-value!]
//  ][
//      blank? type-of :value
//  ]
//
REBNATIVE(void_q)
{
    INCLUDE_PARAMS_OF_VOID_Q;

    return R_FROM_BOOL(IS_VOID(ARG(value)));
}


//
//  void: native/body [
//
//  "Function returning no result (alternative for `()` or `do []`)"
//
//      return: [<opt>] ;-- how to say <opt> no-value! ?
//  ][
//  ]
//
REBNATIVE(void)
{
    UNUSED(frame_);
    return R_VOID;
}


//
//  nothing?: native/body [
//
//  "Returns TRUE if argument is either a NONE! or no value is passed in"
//
//      value [<opt> any-value!]
//  ][
//      any [
//          void? :value
//          blank? :value
//      ]
//  ]
//
REBNATIVE(nothing_q)
{
    INCLUDE_PARAMS_OF_NOTHING_Q;

    return R_FROM_BOOL(
        LOGICAL(IS_BLANK(ARG(value)) || IS_VOID(ARG(value)))
    );
}


//
//  something?: native/body [
//
//  "Returns TRUE if a value is passed in and it isn't a NONE!"
//
//      value [<opt> any-value!]
//  ][
//      all [
//          any-value? :value
//          not blank? value
//      ]
//  ]
//
REBNATIVE(something_q)
{
    INCLUDE_PARAMS_OF_SOMETHING_Q;

    return R_FROM_BOOL(
        NOT(IS_BLANK(ARG(value)) || IS_VOID(ARG(value)))
    );
}
