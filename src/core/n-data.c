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


static REBOOL Check_Char_Range(const REBVAL *val, REBINT limit)
{
    if (IS_CHAR(val))
        return NOT(VAL_CHAR(val) > limit);

    if (IS_INTEGER(val))
        return NOT(VAL_INT64(val) > limit);

    assert(ANY_STRING(val));

    REBCNT len = VAL_LEN_AT(val);
    REBCHR(const *) up = VAL_UNI_AT(val);

    for (; len > 0; len--) {
        REBUNI c;
        up = NEXT_CHR(&c, up);

        if (c > limit)
            return FALSE;
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

    while (FRM_HAS_MORE(f)) {
        const RELVAL *start = f->value;
        if (Do_Next_In_Frame_Throws(D_OUT, f)) {
            Drop_Frame(f);
            return R_OUT_IS_THROWN;
        }

        if (!IS_VOID(D_OUT) && IS_TRUTHY(D_OUT))
            continue;

        Init_Block(
            D_CELL,
            Copy_Values_Len_Shallow(start, f->specifier, f->value - start)
        );

        if (IS_VOID(D_OUT))
            fail (Error_Verify_Void_Raw(D_CELL));

        fail (Error_Verify_Failed_Raw(D_CELL));
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

    REBVAL *v = ARG(value);
    REBVAL *target = ARG(target);

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

    REBCTX *context;

    // !!! For now, force reification before doing any binding.

    if (ANY_CONTEXT(target)) {
        //
        // Get target from an OBJECT!, ERROR!, PORT!, MODULE!, FRAME!
        //
        context = VAL_CONTEXT(target);
    }
    else {
        assert(ANY_WORD(target));
        if (IS_WORD_UNBOUND(target))
            fail (Error_Not_Bound_Raw(target));

        context = VAL_WORD_CONTEXT(target);
    }

    if (ANY_WORD(v)) {
        //
        // Bind a single word

        if (Try_Bind_Word(context, v)) {
            Move_Value(D_OUT, v);
            return R_OUT;
        }

        // not in context, bind/new means add it if it's not.
        //
        if (REF(new) || (IS_SET_WORD(v) && REF(set))) {
            Append_Context(context, v, NULL);
            Move_Value(D_OUT, v);
            return R_OUT;
        }

        fail (Error_Not_In_Context_Raw(v));
    }

    // Copy block if necessary (/copy)
    //
    // !!! NOTE THIS IS IGNORING THE INDEX!  If you ask to bind, it should
    // bind forward only from the index you specified, leaving anything
    // ahead of that point alone.  Not changing it now when finding it
    // because there could be code that depends on the existing (mis)behavior
    // but it should be followed up on.
    //
    Move_Value(D_OUT, v);

    REBARR *array;
    if (REF(copy)) {
        array = Copy_Array_Core_Managed(
            VAL_ARRAY(v),
            VAL_INDEX(v), // at
            VAL_SPECIFIER(v),
            ARR_LEN(VAL_ARRAY(v)), // tail
            0, // extra
            SERIES_FLAG_FILE_LINE, // flags
            TS_ARRAY // types to copy deeply
        );
        INIT_VAL_ARRAY(D_OUT, array); // warning: macro copies args
    }
    else
        array = VAL_ARRAY(v);

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
//  use: native [
//
//  {Defines words local to a block.}
//
//      return: [<opt> any-value!]
//      vars [block! word!]
//          {Local word(s) to the block}
//      body [block!]
//          {Block to evaluate}
//  ]
//
REBNATIVE(use)
//
// !!! R3-Alpha's USE was written in userspace and was based on building a
// CLOSURE! that it would DO.  Hence it took advantage of the existing code
// for tying function locals to a block, and could be relatively short.  This
// was wasteful in terms of creating an unnecessary function that would only
// be called once.  The fate of CLOSURE-like semantics is in flux in Ren-C
// (how much automatic-gathering and indefinite-lifetime will be built-in),
// yet it's also more efficient to just make a native.
//
// As it stands, the code already existed for loop bodies to do this more
// efficiently.  The hope is that with virtual binding, such constructs will
// become even more efficient--for loops, BIND, and USE.
//
// !!! Should USE allow LIT-WORD!s to mean basically a no-op, just for common
// interface with the loops?
{
    INCLUDE_PARAMS_OF_USE;

    REBCTX *context;
    Virtual_Bind_Deep_To_New_Context(
        ARG(body), // may be replaced with rebound copy, or left the same
        &context, // winds up managed; if no references exist, GC is ok
        ARG(vars) // similar to the "spec" of a loop: WORD!/LIT-WORD!/BLOCK!
    );

    if (Do_Any_Array_At_Throws(D_OUT, ARG(body)))
        return R_OUT_IS_THROWN;

    return R_OUT;
}


//
//  Get_Context_Of: C
//
REBOOL Get_Context_Of(REBVAL *out, const REBVAL *v)
{
    switch (VAL_TYPE(v)) {
    case REB_FUNCTION: {
        //
        // The only examples of functions bound to contexts that exist at the
        // moment are RETURN and LEAVE.  While there are archetypal natives
        // for these functions, the REBVAL instances can contain a binding
        // to the specific frame they exit.  Assume what is desired is that
        // frame...
        //
        REBNOD *n = VAL_BINDING(v);
        if (n == UNBOUND)
            return FALSE;

        REBCTX *c;
        if (n->header.bits & NODE_FLAG_CELL) {
            REBFRM *f = cast(REBFRM*, n);
            c = Context_For_Frame_May_Reify_Managed(f);
        }
        else {
            assert(n->header.bits & (SERIES_FLAG_ARRAY | ARRAY_FLAG_VARLIST));
            c = cast(REBCTX*, n);
        }
        Move_Value(out, CTX_VALUE(c));
        assert(IS_FRAME(out));
        break; }

    case REB_WORD:
    case REB_SET_WORD:
    case REB_GET_WORD:
    case REB_LIT_WORD:
    case REB_REFINEMENT:
    case REB_ISSUE: {
        if (IS_WORD_UNBOUND(v))
            return FALSE;

        // Requesting the context of a word that is relatively bound may
        // result in that word having a FRAME! incarnated as a REBSER node (if
        // it was not already reified.)
        //
        // !!! In the future Reb_Context will refer to a REBNOD*, and only
        // be reified based on the properties of the cell into which it is
        // moved (e.g. OUT would be examined here to determine if it would
        // have a longer lifetime than the REBFRM* or other node)
        //
        REBCTX *c = VAL_WORD_CONTEXT(v);
        Move_Value(out, CTX_VALUE(c));
        break; }

    default:
        //
        // Will OBJECT!s or FRAME!s have "contexts"?  Or if they are passed
        // in should they be passed trough as "the context"?  For now, keep
        // things clear?
        //
        assert(FALSE);
    }

    // A FRAME! has special properties of ->phase and ->binding which
    // affect the interpretation of which layer of a function composition
    // they correspond to.  If you REDO a FRAME! value it will restart at
    // different points based on these properties.  Assume the time of
    // asking is the layer in the composition the user is interested in.
    //
    // !!! This may not be the correct answer, but it seems to work in
    // practice...keep an eye out for counterexamples.
    //
    if (IS_FRAME(out)) {
        REBCTX *c = VAL_CONTEXT(out);
        REBFRM *f = CTX_FRAME_IF_ON_STACK(c);
        if (f != NULL) {
            out->payload.any_context.phase = f->phase;
            INIT_BINDING(out, f->binding);
        }
        else {
            // !!! Assume the canon FRAME! value in varlist[0] is useful?
            //
            assert(VAL_BINDING(out) == UNBOUND); // canons have no binding
        }

        assert(
            SER(FUNC_PARAMLIST(out->payload.any_context.phase))->header.bits
            & ARRAY_FLAG_PARAMLIST
        );
    }

    return TRUE;
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

    REBFLGS flags;
    if (REF(set))
        flags = COLLECT_ONLY_SET_WORDS;
    else
        flags = COLLECT_ANY_WORD;

    if (REF(deep))
        flags |= COLLECT_DEEP;

    UNUSED(REF(ignore)); // implied used or unused by ARG(hidden)'s voidness

    RELVAL *head = VAL_ARRAY_AT(ARG(block));

    Init_Block(D_OUT, Collect_Unique_Words_Managed(head, flags, ARG(hidden)));
    return R_OUT;
}


//
//  get: native [
//
//  {Gets the value of a word or path, or values of a context.}
//
//      return: [<opt> any-value!]
//          {If the source looks up to a value, that value--else blank}
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
            // `get 'foo/bar` acts like `:foo/bar`
            // Get_Path_Core() will raise an error if there are any GROUP!s.
            //
            Get_Path_Core(dest, source, specifier);
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
//      /single
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

    REBOOL single;
    if (IS_BLOCK(ARG(target))) {
        //
        // R3-Alpha and Red let you write `set [a b] 10`, since the thing
        // you were setting to was not a block, would assume you meant to set
        // all the values to that.  BUT since you can set things to blocks,
        // this has a bad characteristic of `set [a b] [10]` being treated
        // differently, which can bite you if you `set [a b] value` for some
        // generic value.
        //
        if (IS_BLOCK(ARG(value)) && NOT(REF(single))) {
            //
            // There is no need to check values for voidness in this case,
            // since arrays cannot contain voids.
            //
            value = VAL_ARRAY_AT(ARG(value));
            value_specifier = VAL_SPECIFIER(ARG(value));
            single = FALSE;
        }
        else {
            if (IS_VOID(ARG(value)) && NOT(REF(only)))
                fail (Error_No_Value(ARG(value)));

            value = ARG(value);
            value_specifier = SPECIFIED;
            single = TRUE;
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
        single = TRUE;
    }

    DECLARE_LOCAL (set_path_hack); // runs prep code, don't put inside loop

    for (
        ;
        NOT_END(target);
        ++target, single || IS_END(value) ? NOOP : (++value, NOOP)
     ){
        if (REF(some)) {
            if (IS_END(value))
                break; // won't be setting any further values
            if (IS_BLANK(value))
                continue;
        }

        if (REF(enfix) && NOT(IS_FUNCTION(ARG(value))))
            fail ("Attempt to SET/ENFIX on a non-function");

        if (IS_BAR(target)) {
            //
            // Just skip it, e.g. `set [a | b] [1 2 3]` sets a to 1, and b
            // to 3, but drops the 2.  This functionality was achieved
            // initially with blanks, but with setting in particular there
            // are cases of `in obj 'word` which give back blank if the word
            // is not there, so it leads to too many silent errors.
        }
        else if (ANY_WORD(target)) {
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
            DECLARE_LOCAL (specific);
            if (IS_END(value))
                Init_Blank(specific);
            else
                Derelativize(specific, value, value_specifier);

            // `set 'foo/bar 1` acts as `foo/bar: 1`
            // Set_Path_Core() will raise an error if there are any GROUP!s
            //
            // Though you can't dispatch enfix from a path (at least not at
            // present), the flag tells it to enfix a word in a context, or
            // it will error if that's not what it looks up to.
            //
            Set_Path_Core(target, target_specifier, specific, REF(enfix));
        }
        else
            fail (Error_Invalid_Arg_Core(target, target_specifier));
    }

    Move_Value(D_OUT, ARG(value));
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

    return R_FROM_BOOL(
        LOGICAL(VAL_SERIES(ARG(value1)) == VAL_SERIES(ARG(value2)))
    );
}


// Common routine for both SET? and UNSET?
//
//     SET? 'UNBOUND-WORD -> will error
//     SET? 'OBJECT/NON-MEMBER -> will return false
//     SET? 'OBJECT/NON-MEMBER/XXX -> will error
//     SET? 'DATE/MONTH -> is true, even though not a variable resolution
//
inline static REBOOL Is_Set(const REBVAL *location)
{
    if (ANY_WORD(location))
        return IS_ANY_VALUE(Get_Opt_Var_May_Fail(location, SPECIFIED));

    DECLARE_LOCAL (temp); // result may be generated
    Get_Path_Core(temp, location, SPECIFIED);
    return IS_ANY_VALUE(temp);
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

    return R_FROM_BOOL(Is_Set(ARG(location)));
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

    return R_FROM_BOOL(NOT(Is_Set(ARG(location))));
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
//  "Returns value passed in without evaluation."
//
//      return: [<opt> any-value!]
//          {The input value, verbatim--unless /SOFT and soft quoted type}
//      :value [any-value!]
//          {Value to quote, with BAR! or <opt> not allowed (use UNEVAL)}
//      /soft
//          {Evaluate if a GROUP!, GET-WORD!, or GET-PATH!}
//  ][
//      if bar? :value [
//          fail "Cannot quote expression barrier" ;-- not actual error
//      ]
//      if any [group! :value | get-word? :value | get-path? :value] [
//          reduce value
//      ] else [
//          :value ;-- also sets unevaluated bit, how could a user do so?
//      ]
//  ]
//
REBNATIVE(quote)
{
    INCLUDE_PARAMS_OF_QUOTE;

    REBVAL *v = ARG(value);

    // At the moment, a hard quoting operation is permitted to quote BAR! if
    // it really wants to.  The general advice is to fail in this case, but it
    // is not enforced.  (Hard quotes are also generally not recommended in
    // situations where a soft quote would do.)
    //
    if (IS_BAR(v))
        fail (Error_Expression_Barrier_Raw());

    // While we could use Eval_Value_Throws() here and do the evaluation
    // ourself, that call would need to spawn a new frame.  Using the same
    // re-evaluation feature that EVAL is based on is more efficient, since it
    // just runs in the current frame.
    //
    if (REF(soft) && IS_QUOTABLY_SOFT(v)) {
        Move_Value(D_CELL, v);
        return R_REEVALUATE_CELL;
    }

    // We cannot set the VALUE_FLAG_UNEVALUATED bit here and make it stick,
    // because the bit would just get cleared off by Do_Core when the
    // function finished.  Ask evaluator to add the bit for us.
    //
    Move_Value(D_OUT, v);
    return R_OUT_UNEVALUATED;
}


//
//  uneval: native/body [
//
//  "Returns value passed in without evaluation (including BAR! and void)"
//
//      return: [<opt> any-value!]
//          {The input value, verbatim.}
//      :value [<opt> any-value!]
//          {Value to quote.  Voids are only possible via C rebDo() API.}
//  ][
//      :value ;-- also sets unevaluated bit, how could a user do so?
//  ]
//
REBNATIVE(uneval)
{
    INCLUDE_PARAMS_OF_UNEVAL;

    Move_Value(D_OUT, ARG(value));
    return R_OUT_UNEVALUATED;
}


//
//  void?: native/body [
//
//  "Tells you if the argument is not a value (e.g. `void? do []` is TRUE)"
//
//      value [<opt> any-value!]
//  ][
//      blank? type of :value
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
