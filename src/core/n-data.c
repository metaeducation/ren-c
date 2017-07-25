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

    DECLARE_FRAME (f);
    Push_Frame(f, ARG(conditions));

    DECLARE_LOCAL (temp);

    while (NOT_END(f->value)) {
        const RELVAL *start = f->value;
        if (Do_Next_In_Frame_Throws(D_OUT, f)) {
            Drop_Frame(f);
            return R_OUT_IS_THROWN;
        }

        if (!IS_VOID(D_OUT) && IS_TRUTHY(D_OUT))
            continue;

        Init_Block(
            temp,
            Copy_Values_Len_Shallow(start, f->specifier, f->value - start)
        );

        if (IS_VOID(D_OUT))
            fail (Error_Verify_Void_Raw(temp));

        fail (Error_Verify_Failed_Raw(temp));
    }

    Drop_Frame(f);
    return R_VOID;
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
//          {If the source looks up to a value, that value--else void}
//      source [blank! any-word! any-path! block!]
//          {Word or path to get, or block of words or paths (blank is no-op)}
//      /only
//          {Return void if no value instead of blank}
//  ]
//
REBNATIVE(get)
//
// Note: GET* cannot be the fundamental operation, because GET could not be
// written for blocks (since voids can't be put in blocks, so they couldn't
// be "blankified").  Well, technically it *could* be fundamental, but GET
// would have to make multiple calls to GET* in order to process a block and
// deal with any voids.
{
    INCLUDE_PARAMS_OF_GET;

    RELVAL *source;
    REBVAL *dest;
    REBSPC *specifier;

    REBARR *results;

    if (IS_BLOCK(ARG(source))) {
        //
        // If a BLOCK! of gets are performed, voids cannot be put into the
        // resulting BLOCK!.  Hence for /ONLY to be legal, it would have to
        // give back a BLANK! or other placeholder.  However, since GET-VALUE
        // is built on GET/ONLY, we defer the error an unset variable is
        // actually encountered, which produces that error case that could not
        // be done by "checking the block for voids"

        source = VAL_ARRAY_AT(ARG(source));
        specifier = VAL_SPECIFIER(ARG(source));

        results = Make_Array(VAL_LEN_AT(ARG(source)));
        TERM_ARRAY_LEN(results, VAL_LEN_AT(ARG(source)));
        dest = SINK(ARR_HEAD(results));
    }
    else {
        // Move the argument into the single cell in the frame if it's not a
        // block, so the same enumeration-up-to-an-END marker can work on it
        // as for handling a block of items.
        //
        Move_Value(D_CELL, ARG(source));
        source = D_CELL;
        specifier = SPECIFIED;
        dest = D_OUT;
        results = NULL; // wasteful but avoids maybe-used-uninitalized warning
    }

    DECLARE_LOCAL (get_path_hack); // runs prep code, don't put inside loop

    for (; NOT_END(source); ++source, ++dest) {
        if (IS_BAR(source)) {
            //
            // `a: 10 | b: 20 | get [a | b]` will give back `[10 | 20]`.
            // While seemingly not a very useful feature standalone, this
            // compatibility with SET could come in useful so that blocks
            // don't have to be rearranged to filter out BAR!s.
            //
            Init_Bar(dest);
        }
        else if (IS_BLANK(source)) {
            Init_Void(dest); // may be turned to blank after loop, or error
        }
        else if (ANY_WORD(source)) {
            Copy_Opt_Var_May_Fail(dest, source, specifier);
        }
        else if (ANY_PATH(source)) {
            //
            // Make sure the path does not contain any GROUP!s, because that
            // would trigger evaluations.  GET does not sound like something
            // that should have such a side-effect, the user should go with
            // a REDUCE operation if that's what they want.
            //
            RELVAL *temp = VAL_ARRAY_AT(source);
            for (; NOT_END(temp); ++temp)
                if (IS_GROUP(temp))
                    fail ("GROUP! can't be in paths with GET, use REDUCE");

            // Piggy-back on the GET-PATH! mechanic by copying to a temp
            // value and changing its type bits.
            //
            // !!! Review making a more efficient method of doing this.
            //
            Derelativize(get_path_hack, source, specifier);
            VAL_SET_TYPE_BITS(get_path_hack, REB_GET_PATH);

            // Here we DO it, which means that `get 'foo/bar` will act the
            // same as `:foo/bar` for all types.
            //
            if (Do_Path_Throws_Core(
                dest,
                NULL,
                get_path_hack,
                SPECIFIED,
                NULL
            )){
                // Should not be possible if there's no GROUP!
                //
                fail (Error_No_Catch_For_Throw(dest));
            }
        }

        if (IS_VOID(dest)) {
            if (REF(only)) {
                if (IS_BLOCK(ARG(source))) // can't put voids in blocks
                    fail (Error_No_Value_Core(source, specifier));
            }
            else
                Init_Blank(dest);
        }
    }

    if (IS_BLOCK(ARG(source)))
        Init_Block(D_OUT, results);

    return R_OUT;
}


//
//  to-value: native [
//
//  {Turns voids into blanks, with ANY-VALUE! passing through. (See: OPT)}
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

        fail (word);
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
//  {Sets a word, path, or block of words and paths to specified value(s).}
//
//      return: [<opt> any-value!]
//          {Will be the values set to, or void if any set values are void}
//      target [any-word! any-path! block!]
//          {Word or path, or block of words and paths}
//      value [<opt> any-value!]
//          "Value or block of values"
//      /only
//          {Treat void values as unsetting the target instead of an error}
//      /multi
//          {If target and value are blocks, set each item to the same value}
//      /some
//          {Blank values (or values past end of block) are not set.}
//      /enfix
//          {Function calls through this word should get first arg from left}
//  ]
//
REBNATIVE(set)
//
// Blocks are supported as:
//
//     >> set [a b] [1 2]
//     >> print a
//     1
//     >> print b
//     2
{
    INCLUDE_PARAMS_OF_SET;

    const RELVAL *value;
    REBSPC *value_specifier;

    const RELVAL *target;
    REBSPC *target_specifier;

    REBOOL multi;
    if (IS_BLOCK(ARG(target))) {
        //
        // R3-Alpha and Red let you write `set [a b] 10`, since the thing
        // you were setting to was not a block, would assume you meant to set
        // all the values to that.  BUT since you can set things to blocks,
        // this has a bad characteristic of `set [a b] [10]` being treated
        // differently, which can bite you if you `set [a b] value` for some
        // generic value.
        //
        if (IS_BLOCK(ARG(value)) && NOT(REF(multi))) {
            //
            // There is no need to check values for voidness in this case,
            // since arrays cannot contain voids.
            //
            value = VAL_ARRAY_AT(ARG(value));
            value_specifier = VAL_SPECIFIER(ARG(value));
            multi = FALSE;
        }
        else {
            if (IS_VOID(ARG(value)) && NOT(REF(only)))
                fail (Error_No_Value(ARG(value)));

            value = ARG(value);
            value_specifier = SPECIFIED;
            multi = TRUE;
        }

        target = VAL_ARRAY_AT(ARG(target));
        target_specifier = VAL_SPECIFIER(ARG(target));
    }
    else {
        // Use the fact that D_CELL is implicitly terminated so that the
        // loop below can share code between `set [a b] x` and `set a x`, by
        // incrementing the target pointer and hitting an END marker
        //
        assert(
            ANY_WORD(ARG(target))
            || ANY_PATH(ARG(target))
        );

        Move_Value(D_CELL, ARG(target));
        target = D_CELL;
        target_specifier = SPECIFIED;

        if (IS_VOID(ARG(value)) && NOT(REF(only)))
            fail (Error_No_Value(ARG(value)));

        value = ARG(value);
        value_specifier = SPECIFIED;
        multi = TRUE;
    }

    DECLARE_LOCAL (get_path_hack); // runs prep code, don't put inside loop

    for (
        ;
        NOT_END(target);
        ++target, multi || IS_END(value) ? NOOP : (++value, NOOP)
     ){
        if (REF(some)) {
            if (IS_END(value))
                break; // won't be setting any further values
            if (IS_BLANK(value))
                continue;
        }

        if (IS_BAR(target)) {
            //
            // Just skip it, e.g. `set [a | b] [1 2 3]` sets a to 1, and b
            // to 3, but drops the 2.  This functionality was achieved
            // initially with blanks, but with setting in particular there
            // are cases of `in obj 'word` which give back blank if the word
            // is not there, so it leads to too many silent errors.
        }
        else if (ANY_WORD(target)) {
            if (REF(enfix) && NOT(IS_FUNCTION(ARG(value))))
                fail ("Attempt to SET/ENFIX on a non-function");

            REBVAL *var = Sink_Var_May_Fail(target, target_specifier);
            Derelativize(
                var,
                IS_END(value) ? BLANK_VALUE : value,
                value_specifier
            );
            if (REF(enfix))
                SET_VAL_FLAG(var, VALUE_FLAG_ENFIXED);
        }
        else if (ANY_PATH(target)) {
            //
            // Make sure the path does not contain any GROUP!s, because that
            // would trigger evaluations.  SET does sound like it has a
            // side effect (unlike GET), but you don't expect the side effect
            // to do things like PRINT, which arbitrary code can do.
            //
            RELVAL *temp = VAL_ARRAY_AT(target);
            for (; NOT_END(temp); ++temp)
                if (IS_GROUP(temp))
                    fail ("GROUP! can't be in paths with SET");

            // !!! For starters, just the word form is supported for enfixing.
            // Though you can't dispatch enfix from a path (at least not at
            // present), you should be able to enfix a word in a context.
            //
            if (REF(enfix))
                fail ("Cannot currently SET/ENFIX on a PATH!");

            DECLARE_LOCAL (specific);
            if (IS_END(value))
                Init_Blank(specific);
            else
                Derelativize(specific, value, value_specifier);

            // Currently we have to tweak the bits of the path so that it's a
            // GET-PATH!, since Do_Path is sensitive to the path type, and we
            // want all to act the same.
            //
            Derelativize(get_path_hack, target, target_specifier);
            VAL_SET_TYPE_BITS(get_path_hack, REB_GET_PATH);

            if (
                Do_Path_Throws_Core(
                    D_OUT,
                    NULL,
                    get_path_hack,
                    SPECIFIED,
                    specific
                )
            ){
                fail (Error_No_Catch_For_Throw(D_OUT));
            }

            // If not a throw, then there is no result out of a setting a path
        }
        else
            fail (Error_Invalid_Arg_Core(target, target_specifier));
    }

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
        Init_Void(var);
        return R_VOID;
    }

    assert(IS_BLOCK(target));

    RELVAL *word;
    for (word = VAL_ARRAY_AT(target); NOT_END(word); ++word) {
        if (!ANY_WORD(word))
            fail (Error_Invalid_Arg_Core(word, VAL_SPECIFIER(target)));

        REBVAL *var = Sink_Var_May_Fail(word, VAL_SPECIFIER(target));
        Init_Void(var);
    }

    return R_VOID;
}


//
//  enfixed?: native [
//
//  {TRUE if looks up to a function and gets first argument before the call}
//
//      source [any-word! any-path!]
//  ]
//
REBNATIVE(enfixed_q)
{
    INCLUDE_PARAMS_OF_ENFIXED_Q;

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

        fail ("ENFIXED? testing is not currently implemented on PATH!");
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
//  identity: native [
//
//  {Function for returning the same value that it got in (identity function)}
//
//      return: [<opt> any-value!]
//      value [<opt> any-value!]
//      /quote
//          {Make it seem that the return result was quoted}
//  ]
//
REBNATIVE(identity)
//
// https://en.wikipedia.org/wiki/Identity_function
// https://stackoverflow.com/q/3136338
//
// !!! Quoting version is currently specialized as SEMIQUOTE, for convenience.
{
    INCLUDE_PARAMS_OF_IDENTITY;

    Move_Value(D_OUT, ARG(value));

    if (REF(quote)) {
        //
        // We can't set the VALUE_FLAG_UNEVALUATED bit here and make it stick,
        // because the bit would just get cleared off by Do_Core when the
        // function finished.  So ask the evaluator to set the bit for us.
        //
        return R_OUT_UNEVALUATED;
    }

    return R_OUT; // clears VALUE_FLAG_UNEVALUATED by default
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
            fail (Error_Bad_Cast_Raw(value, ARG(type)));
        break;

    case REB_STRING:
    case REB_TAG:
    case REB_FILE:
    case REB_URL:
        if (!ANY_BINSTR(value) || IS_BINARY(value))
            fail (Error_Bad_Cast_Raw(value, ARG(type)));
        break;

    case REB_WORD:
    case REB_GET_WORD:
    case REB_SET_WORD:
    case REB_LIT_WORD:
    case REB_ISSUE:
    case REB_REFINEMENT:
        if (!ANY_WORD(value))
            fail (value);
        break;

    default:
        fail (Error_Bad_Cast_Raw(value, ARG(type))); // all applicable types should be handled above
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
//      any-value? get/only location
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
//      void? get/only location
//  ]
//
REBNATIVE(unset_q)
{
    INCLUDE_PARAMS_OF_UNSET_Q;

    return R_FROM_BOOL(NOT(Is_Set_Modifies(ARG(location))));
}


//
//  to-logic: native/body [
//
//  "Returns true for all values that would cause IF to take the branch"
//
//      return: [logic!]
//          {true if the supplied value is NOT a LOGIC! false or BLANK!}
//      value [any-value!] ; Note: No [<opt> any-value!] - void must fail
//  ][
//      not not :val
//  ]
//
REBNATIVE(to_logic)
{
    INCLUDE_PARAMS_OF_TO_LOGIC;

    return R_FROM_BOOL(IS_TRUTHY(ARG(value)));
}


//
//  quote: native/body [
//
//  "Returns the value passed to it without evaluation."
//
//      return: [any-value!]
//      :value [any-value!]
//  ][
//      if bar? :value [
//          fail "Cannot quote expression barrier" ;-- not actual error
//      ]
//      :value ;-- actually also sets unevaluated bit, how could a user do so?
//  ]
//
REBNATIVE(quote)
{
    INCLUDE_PARAMS_OF_QUOTE;

    // Generally speaking, a hard quoting operation is permitted to quote
    // BAR! if it really wants to.  The general advice is to fail in this
    // case, but it is not enforced.
    //
    if (IS_BAR(ARG(value)))
        fail (Error_Expression_Barrier_Raw());

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
