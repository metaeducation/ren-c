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


static bool Check_Char_Range(const REBVAL *val, REBLEN limit)
{
    if (IS_CHAR(val))
        return VAL_CHAR(val) <= limit;

    if (IS_INTEGER(val))
        return VAL_INT64(val) <= cast(REBI64, limit);

    assert(ANY_STRING(val));

    REBLEN len;
    REBCHR(const*) up = VAL_UTF8_LEN_SIZE_AT(&len, nullptr, val);

    for (; len > 0; len--) {
        REBUNI c;
        up = NEXT_CHR(&c, up);

        if (c > limit)
            return false;
    }

    return true;
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

    return Init_Logic(D_OUT, Check_Char_Range(ARG(value), 0x7f));
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

    return Init_Logic(D_OUT, Check_Char_Range(ARG(value), 0xff));
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

    return Init_Pair(D_OUT, ARG(x), ARG(y));
}


//
//  bind: native [
//
//  {Binds words or words in arrays to the specified context}
//
//      return: [<requote> action! any-array! any-path! any-word!]
//      value "Value whose binding is to be set (modified) (returned)"
//          [<dequote> action! any-array! any-path! any-word!]
//      target "Target context or a word whose binding should be the target"
//          [any-word! any-context!]
//      /copy "Bind and return a deep copy of a block, don't modify original"
//      /only "Bind only first block (not deep)"
//      /new "Add to context any new words found"
//      /set "Add to context any new set-words found"
//  ]
//
REBNATIVE(bind)
{
    INCLUDE_PARAMS_OF_BIND;

    REBVAL *v = ARG(value);

    REBVAL *target = ARG(target);
    if (IS_QUOTED(target)) {
        Dequotify(target);
        if (not IS_WORD(target))
            fail ("Only quoted as BIND target is WORD! (replaces ANY-WORD!)");
    }

    REBLEN flags = REF(only) ? BIND_0 : BIND_DEEP;

    REBU64 bind_types = TS_WORD;

    REBU64 add_midstream_types;
    if (REF(new)) {
        add_midstream_types = TS_WORD;
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

        if (Try_Bind_Word(context, v))
            RETURN (v);

        // not in context, bind/new means add it if it's not.
        //
        if (REF(new) or (IS_SET_WORD(v) and REF(set))) {
            Append_Context(context, v, NULL);
            RETURN (v);
        }

        fail (Error_Not_In_Context_Raw(v));
    }

    // Binding an ACTION! to a context means it will obey derived binding
    // relative to that context.  See METHOD for usage.  (Note that the same
    // binding pointer is also used in cases like RETURN to link them to the
    // FRAME! that they intend to return from.)
    //
    if (IS_ACTION(v)) {
        Move_Value(D_OUT, v);
        INIT_BINDING(D_OUT, context);
        return D_OUT;
    }

    if (not ANY_ARRAY_OR_PATH(v))
        fail (PAR(value)); // QUOTED! could have been any type

    RELVAL *at;
    if (REF(copy)) {
        REBARR *copy = Copy_Array_Core_Managed(
            VAL_ARRAY(v),
            VAL_INDEX(v), // at
            VAL_SPECIFIER(v),
            ARR_LEN(VAL_ARRAY(v)), // tail
            0, // extra
            ARRAY_MASK_HAS_FILE_LINE, // flags
            TS_ARRAY // types to copy deeply
        );
        at = ARR_HEAD(copy);
        Init_Any_Array(D_OUT, VAL_TYPE(v), copy);
    }
    else {
        at = VAL_ARRAY_AT_MUTABLE_HACK(v);  // only affects bindings after index
        Move_Value(D_OUT, v);
    }

    Bind_Values_Core(
        at,
        context,
        bind_types,
        add_midstream_types,
        flags
    );

    return D_OUT;
}


//
//  in: native [
//
//  "Returns the word or block bound into the given context."
//
//      return: [<opt> <requote> any-word! block! group!]
//      context [any-context! block!]
//      word [<dequote> any-word! block! group!] "(modified if series)"
//  ]
//
REBNATIVE(in)
//
// !!! Currently this is just the same as BIND, with the arguments reordered.
// That may change... IN is proposed to do virtual biding.
//
// !!! The argument names here are bad... not necessarily a context and not
// necessarily a word.  `code` or `source` to be bound in a `target`, perhaps?
{
    INCLUDE_PARAMS_OF_IN;

    REBVAL *val = ARG(context); // object, error, port, block
    REBVAL *word = ARG(word);

    REBLEN num_quotes = VAL_NUM_QUOTES(word);
    Dequotify(word);

    DECLARE_LOCAL (safe);

    if (IS_BLOCK(val) || IS_GROUP(val)) {
        if (IS_WORD(word)) {
            const REBVAL *v;
            REBLEN i;
            for (i = VAL_INDEX(val); i < VAL_LEN_HEAD(val); i++) {
                Get_Simple_Value_Into(
                    safe,
                    VAL_ARRAY_AT_HEAD(val, i),
                    VAL_SPECIFIER(val)
                );

                v = safe;
                if (IS_OBJECT(v)) {
                    REBCTX *context = VAL_CONTEXT(v);
                    REBLEN index = Find_Canon_In_Context(
                        context, VAL_WORD_CANON(word), false
                    );
                    if (index != 0)
                        return Init_Any_Word_Bound(
                            D_OUT,
                            VAL_TYPE(word),
                            VAL_WORD_SPELLING(word),
                            context,
                            index
                        );
                }
            }
            return nullptr;
        }

        fail (word);
    }

    REBCTX *context = VAL_CONTEXT(val);

    // Special form: IN object block
    if (IS_BLOCK(word) or IS_GROUP(word)) {
        Bind_Values_Deep(VAL_ARRAY_AT_MUTABLE_HACK(word), context);
        Quotify(word, num_quotes);
        RETURN (word);
    }

    REBLEN index = Find_Canon_In_Context(context, VAL_WORD_CANON(word), false);
    if (index == 0)
        return nullptr;

    Init_Any_Word_Bound(
        D_OUT,
        VAL_TYPE(word),
        VAL_WORD_SPELLING(word),
        context,
        index
    );
    return Quotify(D_OUT, num_quotes);
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

    if (Do_Any_Array_At_Throws(D_OUT, ARG(body), SPECIFIED))
        return R_THROWN;

    return D_OUT;
}


//
//  Did_Get_Binding_Of: C
//
bool Did_Get_Binding_Of(REBVAL *out, const REBVAL *v)
{
    switch (VAL_TYPE(v)) {
    case REB_ACTION: {
        REBNOD *n = VAL_BINDING(v); // see METHOD... RETURNs also have binding
        if (not n)
            return false;

        Init_Frame(out, CTX(n), ANONYMOUS);  // !!! Review ANONYMOUS
        break; }

    case REB_WORD:
    case REB_SET_WORD:
    case REB_GET_WORD:
    case REB_SYM_WORD: {
        if (IS_WORD_UNBOUND(v))
            return false;

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
        Move_Value(out, CTX_ARCHETYPE(c));
        break; }

    default:
        //
        // Will OBJECT!s or FRAME!s have "contexts"?  Or if they are passed
        // in should they be passed trough as "the context"?  For now, keep
        // things clear?
        //
        assert(false);
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
        if (f) {
            INIT_VAL_CONTEXT_PHASE(out, FRM_PHASE(f));
            INIT_BINDING(out, FRM_BINDING(f));
        }
        else {
            // !!! Assume the canon FRAME! value in varlist[0] is useful?
            //
            assert(VAL_BINDING(out) == UNBOUND); // canons have no binding
        }

        assert(
            VAL_PHASE(out) == nullptr
            or GET_ARRAY_FLAG(
                ACT_PARAMLIST(VAL_PHASE(out)),
                IS_PARAMLIST
            )
        );
    }

    return true;
}


//
//  value?: native [
//
//  "Test if an optional cell contains a value (e.g. `value? null` is FALSE)"
//
//      optional [<opt> any-value!]
//  ]
//
REBNATIVE(value_q)
{
    INCLUDE_PARAMS_OF_VALUE_Q;

    return Init_Logic(D_OUT, ANY_VALUE(ARG(optional)));
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
    else {
        REBCTX *opt_context = nullptr;
        Unbind_Values_Core(
            VAL_ARRAY_AT_ENSURE_MUTABLE(word),
            opt_context,
            did REF(deep)
        );
    }

    RETURN (word);
}


//
//  collect-words: native [
//
//  {Collect unique words used in a block (used for context construction)}
//
//      block [block!]
//      /deep "Include nested blocks"
//      /set "Only include set-words"
//      /ignore "Ignore prior words"
//          [any-context! block!]
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

    const RELVAL *head = VAL_ARRAY_AT(ARG(block));
    return Init_Block(
        D_OUT,
        Collect_Unique_Words_Managed(head, flags, ARG(ignore))
    );
}


inline static void Get_Var_May_Fail(
    REBVAL *out,
    const RELVAL *source_orig,  // ANY-WORD! or ANY-PATH!
    REBSPC *specifier,
    bool any,  // should a VOID! value be gotten normally vs. error
    bool hard  // should GROUP!s in paths not be evaluated
){
    REBCEL(const*) source = VAL_UNESCAPED(source_orig);
    enum Reb_Kind kind = CELL_TYPE(source);

    if (ANY_WORD_KIND(kind)) {
        Move_Value(out, Lookup_Word_May_Fail(source, specifier));
    }
    else if (ANY_SEQUENCE_KIND(kind)) {
        //
        // `get 'foo/bar` acts as `:foo/bar`
        // except GET doesn't allow GROUP!s in the PATH!, unless you use
        // the `hard` option and it treats them literally
        //
        if (Eval_Path_Throws_Core(
            out,
            CELL_TO_VAL(source),
            specifier,
            NULL, // not requesting value to set means it's a get
            hard ? EVAL_FLAG_PATH_HARD_QUOTE : EVAL_FLAG_NO_PATH_GROUPS
        )){
            panic (out); // shouldn't be possible... no executions!
        }
    }
    else
        fail (Error_Bad_Value_Core(source_orig, specifier));

    if (IS_VOID(out) and not any)
        fail (Error_Need_Non_Void_Core(source_orig, specifier));
}


//
//  get: native [
//
//  {Gets the value of a word or path, or block of words/paths}
//
//      return: [<opt> any-value!]
//      source "Word or path to get, or block of words or paths"
//          [<blank> <dequote> any-word! any-sequence! block!]
//      /any "Retrieve ANY-VALUE! (e.g. do not error on VOID!)"
//      /hard "Do not evaluate GROUP!s in PATH! (assume pre-COMPOSE'd)"
//  ]
//
REBNATIVE(get)
{
    INCLUDE_PARAMS_OF_GET;

    REBVAL *source = ARG(source);

    if (not IS_BLOCK(source)) {
        Get_Var_May_Fail(
            D_OUT,
            source,
            SPECIFIED,
            did REF(any),
            did REF(hard)
        );
        return D_OUT;  // IS_NULLED() is okay
    }

    REBARR *results = Make_Array(VAL_LEN_AT(source));
    RELVAL *dest = ARR_HEAD(results);
    const RELVAL *item = VAL_ARRAY_AT(source);

    for (; NOT_END(item); ++item, ++dest) {
        DECLARE_LOCAL (temp);
        Get_Var_May_Fail(
            temp,  // don't want to write directly into movable memory
            item,
            VAL_SPECIFIER(source),
            did REF(any),
            did REF(hard)
        );
        if (IS_NULLED(temp))
            Init_Void(dest);  // blocks can't contain nulls
        else
            Move_Value(dest, temp);
    }

    TERM_ARRAY_LEN(results, VAL_LEN_AT(source));
    return Init_Block(D_OUT, results);
}


//
//  get*: native [
//
//  {Gets the value of a word or path, allows VOID!}
//
//      return: [<opt> any-value!]
//      source "Word or path to get"
//          [<blank> <dequote> any-word! any-path!]
//  ]
//
REBNATIVE(get_p)
//
// This is added as a compromise, as `:var` won't efficiently get ANY-VALUE!.
// At least `get* 'var` doesn't make you pay for path processing, and it's
// not a specialization so it doesn't incur that overhead.
{
    INCLUDE_PARAMS_OF_GET_P;

    Get_Var_May_Fail(
        D_OUT,
        ARG(source),
        SPECIFIED,
        true,  // allow VOID!, e.g. GET/ANY
        false  // evaluate GROUP!s, e.g. not GET/HARD
    );
    return D_OUT;
}


//
//  Set_Var_May_Fail: C
//
// Note this is used by both SET and the SET-BLOCK! data type in %c-eval.c
//
void Set_Var_May_Fail(
    const RELVAL *target_orig,
    REBSPC *target_specifier,
    const RELVAL *setval,
    REBSPC *setval_specifier,
    bool hard
){
    if (Is_Blackhole(target_orig))  // name for a space-bearing ISSUE! ('#')
        return;

    REBCEL(const*) target = VAL_UNESCAPED(target_orig);
    enum Reb_Kind kind = CELL_TYPE(target);

    if (ANY_WORD_KIND(kind)) {
        REBVAL *var = Sink_Word_May_Fail(target, target_specifier);
        Derelativize(var, setval, setval_specifier);
    }
    else if (ANY_SEQUENCE_KIND(kind)) {
        DECLARE_LOCAL (specific);
        Derelativize(specific, setval, setval_specifier);
        PUSH_GC_GUARD(specific);

        // `set 'foo/bar 1` acts as `foo/bar: 1`
        // SET will raise an error if there are any GROUP!s, unless you use
        // the hard option, in which case they are literal.
        //
        // Though you can't dispatch enfix from a path (at least not at
        // present), the flag tells it to enfix a word in a context, or
        // it will error if that's not what it looks up to.
        //
        REBFLGS flags = 0;
        if (hard)
            flags |= EVAL_FLAG_PATH_HARD_QUOTE;
        else
            flags |= EVAL_FLAG_NO_PATH_GROUPS;

        DECLARE_LOCAL (dummy);
        if (Eval_Path_Throws_Core(
            dummy,
            CELL_TO_VAL(target),
            target_specifier,
            specific,
            flags
        )){
            panic (dummy); // shouldn't be possible, no executions!
        }

        DROP_GC_GUARD(specific);
    }
    else
        fail (Error_Bad_Value_Core(target_orig, target_specifier));
}


//
//  set: native [
//
//  {Sets a word, path, or block of words and paths to specified value(s).}
//
//      return: [<opt> any-value!]
//          {Will be the values set to, or void if any set values are void}
//      target [blackhole! any-word! any-path! block! quoted!]
//          {Word or path, or block of words and paths}
//      value [<opt> any-value!]
//          "Value or block of values (NULL means unset)"
//      /hard "Do not evaluate GROUP!s in PATH! (assume pre-COMPOSE'd)"
//      /single "If target and value are blocks, set each to the same value"
//      /some "blank values (or values past end of block) are not set."
//  ]
//
REBNATIVE(set)
//
// R3-Alpha and Red let you write `set [a b] 10`, since the thing you were
// setting to was not a block, would assume you meant to set all the values to
// that.  BUT since you can set things to blocks, this has the problem of
// `set [a b] [10]` being treated differently, which can bite you if you
// `set [a b] value` for some generic value.
//
// Hence by default without /SINGLE, blocks are supported only as:
//
//     >> set [a b] [1 2]
//     >> print a
//     1
//     >> print b
//     2
{
    INCLUDE_PARAMS_OF_SET;

    REBVAL *target = ARG(target);
    REBVAL *value = ARG(value);

    if (not IS_BLOCK(target)) {
        Set_Var_May_Fail(
            target,
            SPECIFIED,
            IS_BLANK(value) and REF(some) ? NULLED_CELL : value,
            SPECIFIED,
            did REF(hard)
        );

        RETURN (value);
    }

    const RELVAL *item = VAL_ARRAY_AT(target);

    const RELVAL *v;
    if (IS_BLOCK(value) and not REF(single))
        v = VAL_ARRAY_AT(value);
    else {
        Init_True(ARG(single));
        v = value;
    }

    for (
        ;
        NOT_END(item);
        ++item, (REF(single) or IS_END(v)) ? NOOP : (++v, NOOP)
     ){
        if (REF(some)) {
            if (IS_END(v))
                break; // won't be setting any further values
            if (IS_BLANK(v))
                continue; // /SOME means treat blanks as no-ops
        }

        Set_Var_May_Fail(
            item,
            VAL_SPECIFIER(target),
            IS_END(v) ? BLANK_VALUE : v, // R3-Alpha/Red blank after END
            (IS_BLOCK(value) and not REF(single))
                ? VAL_SPECIFIER(value)
                : SPECIFIED,
            did REF(hard)
        );
    }

    RETURN (ARG(value));
}


//
//  try: native [
//
//  {Turn nulls into blanks, all non-voids pass through (see also: OPT)}
//
//      return: "blank if input was null, or original value otherwise"
//          [any-value!]
//      optional [<opt> any-value!]
//  ]
//
REBNATIVE(try)
{
    INCLUDE_PARAMS_OF_TRY;

    if (IS_NULLED(ARG(optional)))
        return Init_Blank(D_OUT);

    RETURN (ARG(optional));  // Accepts VOID! for generality (once it didn't)
}


//
//  opt: native [
//
//  {Convert blanks to nulls, pass through most other values (See Also: TRY)}
//
//      return: "null on blank, void if input was null, else original value"
//          [<opt> any-value!]
//      optional [<opt> <blank> any-value!]
//  ]
//
REBNATIVE(opt)
{
    INCLUDE_PARAMS_OF_OPT;

    if (IS_VOID(ARG(optional)))
        fail ("OPT cannot accept VOID! values");

    // !!! Experimental idea: opting a null gives you a void.  You generally
    // don't put OPT on expressions you believe can be null, so this permits
    // creating a likely error in those cases.  To get around it, OPT TRY
    //
    if (IS_NULLED(ARG(optional)))
        return Init_Void(D_OUT);

    RETURN (ARG(optional));
}


//
//  resolve: native [
//
//  {Copy context by setting values in the target from those in the source.}
//
//      target [any-context!] "(modified)"
//      source [any-context!]
//      /only "Only specific words (exports) or new words in target"
//          [block! integer!]
//      /all "Set all words, even those in the target that already have a value"
//      /extend "Add source words to the target if necessary"
//  ]
//
REBNATIVE(resolve)
{
    INCLUDE_PARAMS_OF_RESOLVE;

    if (IS_INTEGER(ARG(only)))
        Int32s(ARG(only), 1);  // check range and sign

    Resolve_Context(
        VAL_CONTEXT(ARG(target)),
        VAL_CONTEXT(ARG(source)),
        ARG(only),
        did REF(all),
        did REF(extend)
    );

    RETURN (ARG(target));
}


//
//  enfixed?: native [
//
//  {TRUE if looks up to a function and gets first argument before the call}
//
//      action [action!]
//  ]
//
REBNATIVE(enfixed_q)
{
    INCLUDE_PARAMS_OF_ENFIXED_Q;

    return Init_Logic(
        D_OUT,
        GET_ACTION_FLAG(VAL_ACTION(ARG(action)), ENFIXED)
    );
}


//
//  enfixed: native [
//
//  {For making enfix functions, e.g `+: enfixed :add` (copies)}
//
//      action [action!]
//  ]
//
REBNATIVE(enfixed)
//
// !!! Because ENFIX was non mutating previously in terms of behavior, the
// new more traditional native has had its name changed.  For efficiency,
// a mutating version for ENFIX may be introduced.
{
    INCLUDE_PARAMS_OF_ENFIXED;

    if (GET_ACTION_FLAG(VAL_ACTION(ARG(action)), ENFIXED))
        fail (
            "ACTION! is already enfixed (review callsite, enfix changed"
            " https://forum.rebol.info/t/1156"
        );

    REBVAL *copy = rebValueQ("copy", ARG(action), rebEND);
    SET_ACTION_FLAG(VAL_ACTION(copy), ENFIXED);
    return copy;
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

    const REBVAL *var = Lookup_Word_May_Fail(ARG(parameter), SPECIFIED);

    return Init_Logic(D_OUT, GET_CELL_FLAG(var, UNEVALUATED));
}


//
//  identity: native [
//
//  {Returns input value (https://en.wikipedia.org/wiki/Identity_function)}
//
//      return: [<opt> any-value!]
//      value [<end> <opt> any-value!]
//  ]
//
REBNATIVE(identity) // sample uses: https://stackoverflow.com/q/3136338
{
    INCLUDE_PARAMS_OF_IDENTITY;

    RETURN (ARG(value));
}


//
//  free: native [
//
//  {Releases the underlying data of a value so it can no longer be accessed}
//
//      return: [void!]
//      memory [any-series! any-context! handle!]
//  ]
//
REBNATIVE(free)
{
    INCLUDE_PARAMS_OF_FREE;

    REBVAL *v = ARG(memory);

    if (ANY_CONTEXT(v) or IS_HANDLE(v))
        fail ("FREE only implemented for ANY-SERIES! at the moment");

    REBSER *s = VAL_SERIES_ENSURE_MUTABLE(v);
    if (GET_SERIES_INFO(s, INACCESSIBLE))
        fail ("Cannot FREE already freed series");

    Decay_Series(s);
    return Init_Void(D_OUT); // !!! Should it return the freed, not-useful value?
}


//
//  free?: native [
//
//  {Tells if data has been released with FREE}
//
//      return: "Returns false if value wouldn't be FREEable (e.g. LOGIC!)"
//          [logic!]
//      value [any-value!]
//  ]
//
REBNATIVE(free_q)
{
    INCLUDE_PARAMS_OF_FREE_Q;

    REBVAL *v = ARG(value);

    // All freeable values put their freeable series in the payload's "first".
    //
    if (NOT_CELL_FLAG(v, FIRST_IS_NODE))
        return Init_False(D_OUT);

    REBNOD *n = PAYLOAD(Any, v).first.node;

    // If the node is not a series (e.g. a pairing), it cannot be freed (as
    // a freed version of a pairing is the same size as the pairing).
    //
    // !!! Technically speaking a PAIR! could be freed as an array could, it
    // would mean converting the node.  Review.
    //
    if (Is_Node_Cell(n))
        return Init_False(D_OUT);

    return Init_Logic(D_OUT, GET_SERIES_INFO(n, INACCESSIBLE));
}


//
//  As_String_May_Fail: C
//
// Shared code from the refinement-bearing AS-TEXT and AS TEXT!.
//
bool Try_As_String(
    REBVAL *out,
    enum Reb_Kind new_kind,
    const REBVAL *v,
    REBLEN quotes,
    enum Reb_Strmode strmode
){
    assert(strmode == STRMODE_ALL_CODEPOINTS or strmode == STRMODE_NO_CR);

    if (ANY_WORD(v)) {  // ANY-WORD! can alias as a read only ANY-STRING!
        Init_Any_String(out, new_kind, VAL_WORD_SPELLING(v));
        Inherit_Const(Quotify(out, quotes), v);
    }
    else if (IS_BINARY(v)) {  // If valid UTF-8, BINARY! aliases as ANY-STRING!
        const REBBIN *bin = VAL_BINARY(v);
        REBSIZ offset = VAL_INDEX(v);

        // The position in the binary must correspond to an actual
        // codepoint boundary.  UTF-8 continuation byte is any byte where
        // top two bits are 10.
        //
        // !!! Should this be checked before or after the valid UTF-8?
        // Checking before keeps from constraining input on errors, but
        // may be misleading by suggesting a valid "codepoint" was seen.
        //
        const REBYTE *at_ptr = BIN_AT(bin, offset);
        if (Is_Continuation_Byte_If_Utf8(*at_ptr))
            fail ("Index at codepoint to convert binary to ANY-STRING!");

        const REBSTR *str;
        REBLEN index;
        if (
            NOT_SERIES_FLAG(bin, IS_STRING)
            or strmode != STRMODE_ALL_CODEPOINTS
        ){
            // If the binary wasn't created as a view on string data to
            // start with, there's no assurance that it's actually valid
            // UTF-8.  So we check it and cache the length if so.  We
            // can do this if it's locked, but not if it's just const...
            // because we may not have the right to.
            //
            // Regardless of aliasing, not using STRMODE_ALL_CODEPOINTS means
            // a valid UTF-8 string may have been edited to include CRs.
            //
            if (not Is_Series_Frozen(bin))
                if (GET_CELL_FLAG(v, CONST))
                    fail (Error_Alias_Constrains_Raw());

            bool all_ascii = true;
            REBLEN num_codepoints = 0;

            index = 0;

            REBSIZ bytes_left = BIN_LEN(bin);
            const REBYTE *bp = BIN_HEAD(bin);
            for (; bytes_left > 0; --bytes_left, ++bp) {
                if (bp < at_ptr)
                    ++index;

                REBUNI c = *bp;
                if (c < 0x80)
                    Validate_Ascii_Byte(bp, strmode, BIN_HEAD(bin));
                else {
                    bp = Back_Scan_UTF8_Char(&c, bp, &bytes_left);
                    if (bp == NULL)  // !!! Should Back_Scan() fail?
                        fail (Error_Bad_Utf8_Raw());

                    all_ascii = false;
                }

                ++num_codepoints;
            }
            SET_SERIES_FLAG(bin, IS_STRING);
            SET_SERIES_FLAG(bin, UTF8_NONWORD);
            str = STR(bin);

            SET_STR_LEN_SIZE(
                m_cast(REBSTR*, str),  // legal for tweaking cached data
                num_codepoints,
                BIN_LEN(bin)
            );
            LINK(bin).bookmarks = nullptr;

            // !!! TBD: cache index/offset

            UNUSED(all_ascii);  // TBD: maintain cache
        }
        else {
            // !!! It's a string series, but or mapping acceleration is
            // from index to offset... not offset to index.  Recalculate
            // the slow way for now.

            str = STR(bin);
            index = 0;

            REBCHR(const*) cp = STR_HEAD(str);
            REBLEN len = STR_LEN(str);
            while (index < len and cp != at_ptr) {
                ++index;
                cp = NEXT_STR(cp);
            }
        }

        Init_Any_String_At(out, new_kind, str, index);
        Inherit_Const(Quotify(out, quotes), v);
    }
    else if (IS_ISSUE(v)) {
        if (CELL_KIND(cast(REBCEL(const*), v)) != REB_BYTES) {
            assert(Is_Series_Frozen(SER(VAL_STRING(v))));
            goto any_string;  // ISSUE! series must be immutable
        }

        // If payload of an ISSUE! lives in the cell itself, a read-only
        // series must be created for the data...because otherwise there isn't
        // room for an index (which ANY-STRING! needs).  For behavior parity
        // with if the payload *was* in the series, this alias must be frozen.

        REBLEN len;
        REBSIZ size;
        REBCHR(const*) utf8 = VAL_UTF8_LEN_SIZE_AT(&len, &size, v);
        assert(size + 1 < sizeof(PAYLOAD(Bytes, v).at_least_8));  // must fit

        REBSTR *str = Make_String_Core(size, SERIES_FLAGS_NONE);
        memcpy(SER_DATA(SER(str)), utf8, size + 1);  // +1 to include '\0'
        SET_STR_LEN_SIZE(str, len, size);
        Freeze_Series(SER(str));
        Init_Any_String(out, new_kind, str);
    }
    else if (ANY_STRING(v)) {
      any_string:
        Move_Value(out, v);
        mutable_KIND_BYTE(out)
            = mutable_MIRROR_BYTE(out)
            = new_kind;
        Trust_Const(Quotify(out, quotes));
    }
    else
        return false;

    return true;
}


//
//  as: native [
//
//  {Aliases underlying data of one value to act as another of same class}
//
//      return: [<opt> integer! issue! any-sequence! any-series! any-word!]
//      type [datatype!]
//      value [
//          <dequote> <blank>
//          integer! issue! any-sequence! any-series! any-word!
//      ]
//  ]
//
REBNATIVE(as)
{
    INCLUDE_PARAMS_OF_AS;

    REBVAL *v = ARG(value);

    REBVAL *t = ARG(type);
    enum Reb_Kind new_kind = VAL_TYPE_KIND(t);
    if (new_kind == VAL_TYPE(v))
        RETURN (v);

    switch (new_kind) {
      case REB_INTEGER: {
        if (not IS_CHAR(v))
            fail ("AS INTEGER! only supports what-were-CHAR! issues ATM");
        return Init_Integer(D_OUT, VAL_CHAR(v)); }

      case REB_BLOCK:
      case REB_GROUP:
        if (ANY_SEQUENCE(v)) {  // internals vary based on optimization
            switch (MIRROR_BYTE(v)) {
              case REB_ISSUE:
                fail ("Array Conversions of byte-oriented sequences TBD");

              case REB_WORD:
                assert(
                    VAL_WORD_SPELLING(v) == PG_Dot_1_Canon
                    or VAL_WORD_SPELLING(v) == PG_Slash_1_Canon
                );
                Init_Block(v, PG_2_Blanks_Array);
                break;

              case REB_GET_WORD: {
                REBARR *a = Make_Array_Core(2, NODE_FLAG_MANAGED);
                Init_Blank(ARR_HEAD(a));
                Blit_Cell(ARR_AT(a, 1), v);
                mutable_KIND_BYTE(ARR_AT(a, 1)) = REB_WORD;
                mutable_MIRROR_BYTE(ARR_AT(a, 1)) = REB_WORD;
                TERM_ARRAY_LEN(a, 2);
                Init_Block(v, a);
                break; }

              case REB_SYM_WORD: {
                REBARR *a = Make_Array_Core(2, NODE_FLAG_MANAGED);
                Blit_Cell(ARR_HEAD(a), v);
                mutable_KIND_BYTE(ARR_HEAD(a)) = REB_WORD;
                mutable_MIRROR_BYTE(ARR_HEAD(a)) = REB_WORD;
                Init_Blank(ARR_AT(a, 1));
                TERM_ARRAY_LEN(a, 2);
                Init_Block(v, a);
                break; }

              case REB_BLOCK:
                mutable_KIND_BYTE(v) = REB_BLOCK;
                assert(Is_Array_Frozen_Shallow(VAL_ARRAY(v)));
                assert(VAL_INDEX(v) == 0);
                break;

              default:
                assert(false);
            }
            break;
        }

        if (not ANY_ARRAY(v))
            goto bad_cast;
        break;

      case REB_TUPLE:
      case REB_GET_TUPLE:
      case REB_SET_TUPLE:
      case REB_SYM_TUPLE:
      case REB_PATH:
      case REB_GET_PATH:
      case REB_SET_PATH:
      case REB_SYM_PATH:
        if (ANY_ARRAY(v)) {
            //
            // Even if we optimize the array, we don't want to give the
            // impression that we would not have frozen it.
            //
            if (not Is_Array_Frozen_Shallow(VAL_ARRAY(v)))
                Freeze_Array_Shallow(VAL_ARRAY_ENSURE_MUTABLE(v));

            if (Try_Init_Any_Sequence_At_Arraylike_Core(
                D_OUT,  // if failure, nulled if too short...else bad element
                new_kind,
                VAL_ARRAY(v),
                VAL_SPECIFIER(v),
                VAL_INDEX(v)
            )){
                return D_OUT;
            }

            fail (Error_Bad_Sequence_Init(D_OUT));
        }

        if (ANY_PATH(v)) {
            Move_Value(D_OUT, v);
            mutable_KIND_BYTE(D_OUT)
                = new_kind;
            return Trust_Const(D_OUT);
        }

        goto bad_cast;

      case REB_ISSUE: {
        if (IS_INTEGER(v))
            return Init_Char_May_Fail(D_OUT, VAL_UINT32(v));

        if (ANY_STRING(v)) {
            REBLEN len;
            REBSIZ utf8_size = VAL_SIZE_LIMIT_AT(&len, v, UNLIMITED);

            if (utf8_size + 1 <= sizeof(PAYLOAD(Bytes, v).at_least_8)) {
                //
                // Payload can fit in a single issue cell.
                //
                RESET_CELL(D_OUT, REB_BYTES, CELL_MASK_NONE);
                memcpy(
                    PAYLOAD(Bytes, D_OUT).at_least_8,
                    VAL_STRING_AT(v),
                    utf8_size + 1  // copy the '\0' terminator
                );
                EXTRA(Bytes, D_OUT).exactly_4[IDX_EXTRA_USED] = utf8_size;
                EXTRA(Bytes, D_OUT).exactly_4[IDX_EXTRA_LEN] = len;
            }
            else {
                if (not Try_As_String(
                    D_OUT,
                    REB_TEXT,
                    v,
                    0,  // no quotes
                    STRMODE_ALL_CODEPOINTS  // See AS-TEXT/STRICT for stricter
                )){
                    goto bad_cast;
                }
            }
            mutable_KIND_BYTE(D_OUT) = REB_ISSUE;
            return D_OUT;
        }

        goto bad_cast; }

      case REB_TEXT:
      case REB_TAG:
      case REB_FILE:
      case REB_URL:
      case REB_EMAIL:
        if (not Try_As_String(
            D_OUT,
            new_kind,
            v,
            0,  // no quotes
            STRMODE_ALL_CODEPOINTS  // See AS-TEXT/STRICT for stricter
        )){
            goto bad_cast;
        }
        return D_OUT;

      case REB_WORD:
      case REB_GET_WORD:
      case REB_SET_WORD:
      case REB_SYM_WORD: {
        if (IS_ISSUE(v)) {
            if (CELL_KIND(cast(REBCEL(const*), v)) == REB_TEXT) {
                //
                // Handle the same way we'd handle any other read-only text
                // with a series allocation...e.g. reuse it if it's already
                // been validated as a WORD!, or mark it word-valid if it's
                // frozen and hasn't been marked yet.
                //
                // Note: We may jump back up to use the intern_utf8 branch if
                // that falls through.
                //
                goto any_string;
            }

            // Data that's just living in the payload needs to be handled
            // and validated as a WORD!.

          intern_utf8: {
            //
            // !!! This uses the same path as Scan_Word() to try and run
            // through the same validation.  Review efficiency.
            //
            REBSIZ size;
            REBCHR(const*) utf8 = VAL_UTF8_SIZE_AT(&size, v);
            if (nullptr == Scan_Any_Word(D_OUT, new_kind, utf8, size))
                fail (Error_Bad_Char_Raw(v));

            return Inherit_Const(D_OUT, v);
          }
        }

        if (ANY_STRING(v)) {  // aliasing data as an ANY-WORD! freezes data
          any_string: {
            const REBSTR *s = VAL_STRING(v);

            if (not Is_Series_Frozen(SER(s))) {
                //
                // We always force strings used with AS to frozen, so that the
                // effect of freezing doesn't appear to mystically happen just
                // in those cases where the efficient reuse works out.

                if (GET_CELL_FLAG(v, CONST))
                    fail (Error_Alias_Constrains_Raw());

                Freeze_Series(VAL_SERIES(v));
            }

            if (VAL_INDEX(v) != 0)  // can't reuse non-head series AS WORD!
                goto intern_utf8;

            if (IS_STR_SYMBOL(s)) {
                //
                // This string's content was already frozen and checked, e.g.
                // the string came from something like `as text! 'some-word`
            }
            else {
                // !!! If this spelling is already interned we'd like to
                // reuse the existing series, and if not we'd like to promote
                // this series to be the interned one.  This efficiency has
                // not yet been implemented, so we just intern it.
                //
                goto intern_utf8;
            }

            Init_Any_Word(D_OUT, new_kind, s);
            return Inherit_Const(D_OUT, v);
          }
        }

        if (IS_BINARY(v)) {
            if (VAL_INDEX(v) != 0)  // ANY-WORD! stores binding, not position
                fail ("Cannot convert BINARY! to WORD! unless at the head");

            // We have to permanently freeze the underlying series from any
            // mutation to use it in a WORD! (and also, may add STRING flag);
            //
            const REBBIN *bin = VAL_BINARY(v);
            if (not Is_Series_Frozen(bin))
                if (GET_CELL_FLAG(v, CONST))  // can't freeze or add IS_STRING
                    fail (Error_Alias_Constrains_Raw());

            const REBSTR *str;
            if (IS_SER_STRING(bin) and IS_STR_SYMBOL(STR(bin)))
                str = STR(bin);
            else {
                // !!! There isn't yet a mechanic for interning an existing
                // string series.  That requires refactoring.  It would need
                // to still check for invalid patterns for words (e.g.
                // invalid UTF-8 or even just internal spaces/etc.).
                //
                // We do a new interning for now.  But we do that interning
                // *before* freezing the old string, so that if there's an
                // error converting we don't add any constraints to the input.
                //
                str = Intern_UTF8_Managed(VAL_BIN_AT(v), VAL_LEN_AT(v));

                // Constrain the input in the way it would be if we were doing
                // the more efficient reuse.
                //
                SET_SERIES_FLAG(bin, IS_STRING);  // might be set already
                Freeze_Series(bin);
            }

            return Inherit_Const(Init_Any_Word(D_OUT, new_kind, str), v);
        }

        if (not ANY_WORD(v))
            goto bad_cast;
        break; }

      case REB_BINARY: {
        if (IS_ISSUE(v)) {
            if (CELL_KIND(cast(REBCEL(const*), v)) == REB_TEXT)
                goto any_string_as_binary;  // had a series allocation

            // Data lives in payload--make new frozen series for BINARY!

            REBSIZ size;
            REBCHR(const*) utf8 = VAL_UTF8_SIZE_AT(&size, v);
            REBBIN *bin = Make_Binary_Core(size, NODE_FLAG_MANAGED);
            memcpy(BIN_HEAD(bin), utf8, size + 1);
            SET_SERIES_USED(bin, size);
            Freeze_Series(bin);
            Init_Binary(D_OUT, bin);
            return Inherit_Const(D_OUT, v);
        }

        if (ANY_WORD(v) or ANY_STRING(v)) {
          any_string_as_binary:
            Init_Binary_At(
                D_OUT,
                SER(VAL_STRING(v)),
                ANY_WORD(v) ? 0 : VAL_OFFSET(v)
            );
            return Inherit_Const(D_OUT, v);
        }

        fail (v); }

      bad_cast:;
      default:
        // all applicable types should be handled above
        fail (Error_Bad_Cast_Raw(v, ARG(type)));
    }

    // Fallthrough for cases where changing the type byte and potentially
    // updating the quotes is enough.
    //
    Move_Value(D_OUT, v);
    mutable_KIND_BYTE(D_OUT)
        = mutable_MIRROR_BYTE(D_OUT)
        = new_kind;
    return Trust_Const(D_OUT);
}


//
//  as-text: native [
//      {AS TEXT! variant that may disallow CR LF sequences in BINARY! alias}
//
//      return: [<opt> text!]
//      value [<blank> any-value!]
//      /strict "Don't allow CR LF sequences in the alias"
//  ]
//
REBNATIVE(as_text)
{
    INCLUDE_PARAMS_OF_AS_TEXT;

    REBVAL *v = ARG(value);
    Dequotify(v);  // number of incoming quotes not relevant
    if (not ANY_SERIES(v) and not ANY_WORD(v) and not ANY_PATH(v))
        fail (PAR(value));

    const REBLEN quotes = 0;  // constant folding (see AS behavior)

    enum Reb_Kind new_kind = REB_TEXT;
    if (new_kind == VAL_TYPE(v) and not REF(strict))
        RETURN (Quotify(v, quotes));  // just may change quotes

    if (not Try_As_String(
        D_OUT,
        REB_TEXT,
        v,
        quotes,
        REF(strict) ? STRMODE_NO_CR : STRMODE_ALL_CODEPOINTS
    )){
        fail (Error_Bad_Cast_Raw(v, Datatype_From_Kind(REB_TEXT)));
    }

    return D_OUT;
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

    return Init_Logic(D_OUT, VAL_SERIES(ARG(value1)) == VAL_SERIES(ARG(value2)));
}


// Common routine for both SET? and UNSET?
//
//     SET? 'UNBOUND-WORD -> will error
//     SET? 'OBJECT/NON-MEMBER -> will return false
//     SET? 'OBJECT/NON-MEMBER/XXX -> will error
//     SET? 'DATE/MONTH -> is true, even though not a variable resolution
//
inline static bool Is_Set(const REBVAL *location)
{
    if (ANY_WORD(location))
        return not IS_NULLED(Lookup_Word_May_Fail(location, SPECIFIED));

    DECLARE_LOCAL (temp); // result may be generated
    Get_Path_Core(temp, location, SPECIFIED);
    return not IS_NULLED(temp);
}


inline static bool Is_Defined(const REBVAL *location)
{
    if (ANY_WORD(location))
        return not IS_VOID(Lookup_Word_May_Fail(location, SPECIFIED));

    DECLARE_LOCAL (temp); // result may be generated
    Get_Path_Core(temp, location, SPECIFIED);
    return not IS_VOID(temp);
}


//
//  set?: native/body [
//
//  "Whether a bound word or path is set (!!! shouldn't eval GROUP!s)"
//
//      return: [logic!]
//      location [<dequote> any-word! any-path!]
//  ][
//      not null? get/any location
//  ]
//
REBNATIVE(set_q)
{
    INCLUDE_PARAMS_OF_SET_Q;

    return Init_Logic(D_OUT, Is_Set(ARG(location)));
}


//
//  unset?: native/body [
//
//  "Whether a bound word or path is unset (!!! shouldn't eval GROUP!s)"
//
//      return: [logic!]
//      location [<dequote> any-word! any-path!]
//  ][
//      null? get/any location
//  ]
//
REBNATIVE(unset_q)
{
    INCLUDE_PARAMS_OF_UNSET_Q;

    return Init_Logic(D_OUT, not Is_Set(ARG(location)));
}


//
//  defined?: native/body [
//
//  "Whether a bound word or path is not void (!!! shouldn't eval GROUP!s)"
//
//      return: [logic!]
//      location [<dequote> any-word! any-path!]
//  ][
//      not void? get/any location
//  ]
//
REBNATIVE(defined_q)
{
    INCLUDE_PARAMS_OF_DEFINED_Q;

    return Init_Logic(D_OUT, Is_Defined(ARG(location)));
}


//
//  undefined?: native/body [
//
//  "Whether a bound word or path is void (!!! shouldn't eval GROUP!s)"
//
//      return: [logic!]
//      location [<dequote> any-word! any-path!]
//  ][
//      void? get/any location
//  ]
//
REBNATIVE(undefined_q)
{
    INCLUDE_PARAMS_OF_UNDEFINED_Q;

    return Init_Logic(D_OUT, not Is_Defined(ARG(location)));
}


//
//  null?: native/body [
//
//  "Tells you if the argument is not a value"
//
//      return: [logic!]
//      optional [<opt> any-value!]
//  ][
//      null = type of :optional
//  ]
//
REBNATIVE(null_q)
{
    INCLUDE_PARAMS_OF_NULL_Q;

    return Init_Logic(D_OUT, IS_NULLED(ARG(optional)));
}


//
//  voidify: native [
//
//  "Turn nulls into voids, passing through all other values"
//
//      return: [any-value!]
//      optional [<opt> any-value!]
//  ]
//
REBNATIVE(voidify)
{
    INCLUDE_PARAMS_OF_VOIDIFY;

    if (IS_NULLED(ARG(optional)))
        return Init_Void(D_OUT);

    RETURN (ARG(optional));
}


//
//  devoid: native [
//
//  "Turn voids into nulls, passing through all other values"
//
//      return: [<opt> any-value!]
//      optional [<opt> any-value!]
//  ]
//
REBNATIVE(devoid)
{
    INCLUDE_PARAMS_OF_DEVOID;

    if (IS_VOID(ARG(optional)))
        return Init_Nulled(D_OUT);

    RETURN (ARG(optional));
}


//
//  nothing?: native/body [
//
//  "Returns TRUE if argument is either a BLANK! or NULL"
//
//      value [<opt> any-value!]
//  ][
//      did any [
//          unset? 'value
//          blank? value
//          null? value
//      ]
//  ]
//
REBNATIVE(nothing_q)
{
    INCLUDE_PARAMS_OF_NOTHING_Q;

    return Init_Logic(
        D_OUT,
        IS_VOID(ARG(value))  // Should unset be also considered "nothing"?
            or IS_NULLED_OR_BLANK(ARG(value))
    );
}


//
//  something?: native/body [
//
//  "Returns TRUE if a value is passed in and it isn't NULL or a BLANK!"
//
//      value [<opt> any-value!]
//  ][
//      all [
//          set? 'value
//          not blank? value
//      ]
//  ]
//
REBNATIVE(something_q)
{
    INCLUDE_PARAMS_OF_SOMETHING_Q;

    return Init_Logic(D_OUT, not IS_NULLED_OR_BLANK(ARG(value)));
}
