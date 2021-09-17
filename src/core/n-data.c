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
//      return: [logic!]
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
//      return: [logic!]
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
//      return: [pair!]
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
//      return: [action! any-array! any-path! any-word! quoted!]
//      value "Value whose binding is to be set (modified) (returned)"
//          [action! any-array! any-path! any-word! quoted!]
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
    REBLEN num_quotes = Dequotify(v);

    REBVAL *target = ARG(target);

    REBLEN flags = REF(only) ? BIND_0 : BIND_DEEP;

    REBU64 bind_types = TS_WORD | FLAGIT_KIND(REB_SYMBOL);

    REBU64 add_midstream_types;
    if (REF(new)) {
        add_midstream_types = TS_WORD;
    }
    else if (REF(set)) {
        add_midstream_types = FLAGIT_KIND(REB_SET_WORD);
    }
    else
        add_midstream_types = 0;

    const RELVAL *context;

    // !!! For now, force reification before doing any binding.

    if (ANY_CONTEXT(target)) {
        //
        // Get target from an OBJECT!, ERROR!, PORT!, MODULE!, FRAME!
        //
        context = target;
    }
    else {
        assert(ANY_WORD(target));

        if (not Did_Get_Binding_Of(D_SPARE, target))
            fail (Error_Not_Bound_Raw(target));

        context = D_SPARE;
    }

    if (ANY_WORD(v)) {
        //
        // Bind a single word

        if (Try_Bind_Word(context, v))
            RETURN (Quotify(v, num_quotes));

        // not in context, bind/new means add it if it's not.
        //
        if (REF(new) or (IS_SET_WORD(v) and REF(set))) {
            Init_Unset(Append_Context(VAL_CONTEXT(context), v, nullptr));
            RETURN (Quotify(v, num_quotes));
        }

        fail (Error_Not_In_Context_Raw(v));
    }

    // Binding an ACTION! to a context means it will obey derived binding
    // relative to that context.  See METHOD for usage.  (Note that the same
    // binding pointer is also used in cases like RETURN to link them to the
    // FRAME! that they intend to return from.)
    //
    if (IS_ACTION(v)) {
        Copy_Cell(D_OUT, v);
        INIT_VAL_ACTION_BINDING(D_OUT, VAL_CONTEXT(context));
        return Quotify(D_OUT, num_quotes);
    }

    if (not ANY_ARRAY_OR_SEQUENCE(v)) {  // QUOTED! could have wrapped any type
        Quotify(v, num_quotes);  // put quotes back on
        fail (Error_Invalid_Arg(frame_, PAR(value)));
    }

    RELVAL *at;
    const RELVAL *tail;
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
        tail = ARR_TAIL(copy);
        Init_Any_Array(D_OUT, VAL_TYPE(v), copy);
    }
    else {
        ENSURE_MUTABLE(v);  // use IN for virtual binding
        at = VAL_ARRAY_AT_MUTABLE_HACK(&tail, v);  // !!! only *after* index!
        Copy_Cell(D_OUT, v);
    }

    Bind_Values_Core(
        at,
        tail,
        context,
        bind_types,
        add_midstream_types,
        flags
    );

    return Quotify(D_OUT, num_quotes);
}


//
//  in: native [
//
//  "Returns a view of the input bound virtually to the context"
//
//      return: [<opt> any-word! any-array!]
//      context [any-context!]
//      value [<const> <blank> any-word! any-array!]  ; QUOTED! support?
//  ]
//
REBNATIVE(in)
{
    INCLUDE_PARAMS_OF_IN;

    REBCTX *ctx = VAL_CONTEXT(ARG(context));
    REBVAL *v = ARG(value);

    // !!! Note that BIND of a WORD! in historical Rebol/Red would return the
    // input word as-is if the word wasn't in the requested context, while
    // IN would return NONE! on failure.  We carry forward the NULL-failing
    // here in IN, but BIND's behavior on words may need revisiting.
    //
    if (ANY_WORD(v)) {
        const REBSYM *symbol = VAL_WORD_SYMBOL(v);
        const bool strict = true;
        REBLEN index = Find_Symbol_In_Context(ARG(context), symbol, strict);
        if (index == 0)
            return nullptr;
        return Init_Any_Word_Bound(D_OUT, VAL_TYPE(v), ctx, symbol, index);
    }

    assert(ANY_ARRAY(v));
    Virtual_Bind_Deep_To_Existing_Context(v, ctx, nullptr, REB_WORD);
    RETURN (v);
}


//
//  without: native [
//
//  "Remove a virtual binding from a value"
//
//      return: [<opt> any-word! any-array!]
//      context "If integer, then removes that number of virtual bindings"
//          [integer! any-context!]
//      value [<const> <blank> any-word! any-array!]  ; QUOTED! support?
//  ]
//
REBNATIVE(without)
{
    INCLUDE_PARAMS_OF_IN;

    REBCTX *ctx = VAL_CONTEXT(ARG(context));
    REBVAL *v = ARG(value);

    // !!! Note that BIND of a WORD! in historical Rebol/Red would return the
    // input word as-is if the word wasn't in the requested context, while
    // IN would return NONE! on failure.  We carry forward the NULL-failing
    // here in IN, but BIND's behavior on words may need revisiting.
    //
    if (ANY_WORD(v)) {
        const REBSYM *symbol = VAL_WORD_SYMBOL(v);
        const bool strict = true;
        REBLEN index = Find_Symbol_In_Context(ARG(context), symbol, strict);
        if (index == 0)
            return nullptr;
        return Init_Any_Word_Bound(
            D_OUT,
            VAL_TYPE(v),
            ctx,
            symbol,  // !!! incoming case...consider impact of strict if false?
            index
        );
    }

    assert(ANY_ARRAY(v));
    Virtual_Bind_Deep_To_Existing_Context(v, ctx, nullptr, REB_WORD);
    RETURN (v);
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
        REBCTX *binding = VAL_ACTION_BINDING(v); // e.g. METHOD, RETURNs
        if (not binding)
            return false;

        Init_Frame(out, binding, ANONYMOUS);  // !!! Review ANONYMOUS
        break; }

    case REB_WORD:
    case REB_SET_WORD:
    case REB_GET_WORD:
    case REB_META_WORD:
    case REB_THE_WORD: {
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

        // If it's a FRAME! we want the phase to match the execution phase at
        // the current moment of execution.
        //
        if (CTX_TYPE(c) == REB_FRAME) {
            REBFRM *f = CTX_FRAME_IF_ON_STACK(c);
            if (f == nullptr)
                Copy_Cell(out, CTX_ARCHETYPE(c));
            else
                Copy_Cell(out, f->rootvar);  // rootvar has phase, binding
        }
        else
            Copy_Cell(out, CTX_ARCHETYPE(c));
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
            INIT_VAL_FRAME_PHASE(out, FRM_PHASE(f));
            INIT_VAL_FRAME_BINDING(out, FRM_BINDING(f));
        }
        else {
            // !!! Assume the canon FRAME! value in varlist[0] is useful?
            //
            assert(VAL_FRAME_BINDING(out) == UNBOUND); // canon, no binding
        }
    }

    return true;
}


//
//  value?: native [
//
//  "Test if an optional cell contains a value (e.g. `value? null` is FALSE)"
//
//      return: [logic!]
//      optional [<opt> any-value!]
//  ]
//
REBNATIVE(value_q)
{
    INCLUDE_PARAMS_OF_VALUE_Q;

    return Init_Logic(D_OUT, ANY_VALUE(ARG(optional)));
}


//
//  any-inert?: native [
//
//  "Test if a value type is inert"
//
//      return: [logic!]
//      optional [<opt> any-value!]
//  ]
//
REBNATIVE(any_inert_q)
//
// This could be done via a typeset bit the way ANY-BLOCK! and other tests are
// done.  However, the types are organized to make this particular test fast.
{
    INCLUDE_PARAMS_OF_ANY_INERT_Q;

    REBVAL *v = ARG(optional);

    return Init_Logic(D_OUT, not IS_NULLED(v) and ANY_INERT(v));
}


//
//  unbind: native [
//
//  "Unbinds words from context."
//
//      return: [block! any-word!]
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
        assert(IS_BLOCK(word));

        const RELVAL *tail;
        RELVAL *at = VAL_ARRAY_AT_ENSURE_MUTABLE(&tail, word);
        option(REBCTX*) context = nullptr;
        Unbind_Values_Core(at, tail, context, did REF(deep));
    }

    RETURN (word);
}


//
//  collect-words: native [
//
//  {Collect unique words used in a block (used for context construction)}
//
//      return: [block!]
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

    const RELVAL *tail;
    const RELVAL *at = VAL_ARRAY_AT(&tail, ARG(block));
    return Init_Block(
        D_OUT,
        Collect_Unique_Words_Managed(at, tail, flags, ARG(ignore))
    );
}


//
//  Get_Var_May_Fail: C
//
void Get_Var_May_Fail(
    REBVAL *out,
    const RELVAL *source,  // ANY-WORD! or ANY-PATH! (maybe quoted)
    REBSPC *specifier,
    bool any  // transform stable voids into isotopes without erroring
){
    enum Reb_Kind kind = CELL_KIND(VAL_UNESCAPED(source));

    if (ANY_WORD_KIND(kind) or kind == REB_SYMBOL) {
        Copy_Cell(out, Lookup_Word_May_Fail(source, specifier));
    }
    else if (ANY_SEQUENCE_KIND(kind)) {
        //
        // `get 'foo/bar` acts as `:foo/bar`
        // except GET doesn't allow GROUP!s in the PATH!, unless you use
        // the `hard` option and it treats them literally
        //
        if (Eval_Path_Throws_Core(
            out,
            source,  // !!! Review
            specifier,
            EVAL_MASK_DEFAULT | EVAL_FLAG_NO_PATH_GROUPS
        )){
            panic (out); // shouldn't be possible... no executions!
        }
    }
    else
        fail (Error_Bad_Value(source));

    if (IS_BAD_WORD(out)) {
        if (GET_CELL_FLAG(out, ISOTOPE)) {
            if (not any)
                fail (Error_Bad_Word_Get(source, out));
        }
    }

    // !!! Variables should not store null isotopes, but they currently can...
    // look into a systemic answer of how and where to stop this.
    //
    Decay_If_Isotope(out);
}


//
//  get: native [
//
//  {Gets the value of a word or path, or block of words/paths}
//
//      return: [<opt> any-value!]
//      steps: "Allow GROUP! evals, returns block of reusable PICK/POKE steps"
//          [block! word! symbol!]
//
//      source "Word or path to get, or block of PICK steps"
//          [<blank> any-word! symbol! any-sequence! block!]
//      /any "Do not error on BAD-WORD! isotopes"
//  ]
//
REBNATIVE(get)
{
    INCLUDE_PARAMS_OF_GET;

    REBVAL *source = ARG(source);
    REBVAL *steps = ARG(steps);

    // Simple WORD! or symbol GET.  (Should this also handle /foo, .foo, etc?)
    //
    if (ANY_WORD(source) or IS_SYMBOL(source)) {

      get_source:

        Copy_Cell(D_OUT, Lookup_Word_May_Fail(source, SPECIFIED));

        if (REF(steps)) {
            if (not IS_SYMBOL(source)) {
                mutable_KIND3Q_BYTE(source) = REB_WORD;
                mutable_HEART_BYTE(source) = REB_WORD;
            }
            Quotify(source, 1);
            Quotify(steps, 1);
            rebElide(Lib(SET), steps, source);
        }
        goto check_result;
    }

    if (IS_BLOCK(source))
        goto pick_steps;  // assume already a BLOCK! in "pick step" format

  blockscope {
    assert (ANY_SEQUENCE(source));

    switch (HEART_BYTE(source)) {
      case REB_BYTES:
        fail ("Cannot GET a sequence of all INTEGER!");

      case REB_WORD:  // Note: will likely become SYMBOL! instances
        assert(
            VAL_STRING(source) == PG_Dot_1_Canon
            or VAL_STRING(source) == PG_Slash_1_Canon
        );
        mutable_KIND3Q_BYTE(source) = REB_WORD;
        goto get_source;

      case REB_GET_WORD:  // `/a` or `.a`
        mutable_KIND3Q_BYTE(source) = REB_GET_WORD;
        goto get_source;

      case REB_META_WORD:  // `a/` or `a.`
        mutable_KIND3Q_BYTE(source) = REB_META_WORD;
        //
        // !!! If this is a PATH!, it should error if it's not an action...
        // and if it's a TUPLE! it should error if it is an action.  Review.
        //
        goto get_source;

      case REB_GET_GROUP:  // `/(a)` or `.(a)`
      case REB_GET_BLOCK:  // `/[a]` or `.[a]`
      case REB_META_GROUP:  // `(a)/` or `(a).`
      case REB_META_BLOCK:  // `[a]/` or `[a].`
        fail ("Single GROUP! or BLOCK! variants not handled in GET right now");

      case REB_BLOCK:
        mutable_KIND3Q_BYTE(source) = REB_BLOCK;
        break;

      default:
        panic(source);
    }

    const RELVAL *tail;
    const RELVAL *head = VAL_ARRAY_AT(&tail, source);

    // Check to see if there are no GROUP!s to evaluate.  If there are not
    // the path can be used as pick steps.
    //
    // !!! This could be optimized if sequences cached if there were any
    // groups in them.
    //
    const RELVAL *item = head;
    for (; item != tail; ++item) {
        if (IS_GROUP(item))
            break;
    }
    if (item == tail)
        goto pick_steps;

    if (not REF(steps))
        fail ("GET won't evaluate GROUP!s unless /STEPS output requested");

    // If there are GROUP!s, we need a new array.  We know the array will
    // be the same size as the input.
    //
    REBLEN len = VAL_LEN_AT(source);
    REBARR *a = Make_Array(len);
    RELVAL *dest = ARR_HEAD(a);

    item = head;
    for (; item != tail; ++item, ++dest) {
        if (not IS_GROUP(item)) {
            //
            // !!! Technically this block doesn't need to be derelativized.
            // Only the first value will have a binding, and the block
            // could have the same relativism as the original sequence.
            // Review such micro-optimizations after proof-of-concept.
            //
            Derelativize(dest, item, VAL_SPECIFIER(source));
            continue;
        }

        if (Eval_Value_Throws(D_OUT, item, VAL_SPECIFIER(source))) {
            Free_Unmanaged_Series(a);
            return R_THROWN;
        }
        Move_Cell(dest, D_OUT);

        // By convention, picker steps quote the first item if it was a
        // GROUP!.  It has to be somehow different because `('a).b` is
        // trying to pick B out of the WORD! a...not out of what is
        // fetched from A.  So if the convention is that the first item
        // of a "steps" block needs to be "fetched" we quote it.
        //
        if (item == head)
            Quotify(dest, 1);
    }
    SET_SERIES_LEN(a, len);
    Init_Block(source, a);
  }

  pick_steps: {

    assert(IS_BLOCK(source));

    if (REF(steps)) {
        Quotify(steps, 1);
        rebElide(Lib(SET), ARG(steps), source);
    }

    const RELVAL *first = VAL_ARRAY_ITEM_AT(source);
    if (IS_QUOTED(first)) {
        Derelativize(D_SPARE, first, VAL_SPECIFIER(source));
    }
    else if (IS_WORD(first)) {
        Copy_Cell(
            D_SPARE,
            Lookup_Word_May_Fail(first, VAL_SPECIFIER(source))
        );
        if (IS_BAD_WORD(D_SPARE) and GET_CELL_FLAG(D_SPARE, ISOTOPE))
            fail (Error_Bad_Word_Get(first, D_SPARE));

        Quotify(D_SPARE, 1);
    }
    else
        fail (source);

    ++VAL_INDEX_RAW(source);

    if (rebRunThrows(D_OUT, true, Lib(PICK_P), D_SPARE, source)) {
        //
        // !!! We've already evaluated all the groups, so there should
        // (probably?) be a rule that PICK* is not able to throw.  Review.
        //
        fail (Error_No_Catch_For_Throw(D_OUT));
    }
  }

  check_result:

    if (IS_BAD_WORD(D_OUT)) {
        if (GET_CELL_FLAG(D_OUT, ISOTOPE)) {
            if (not REF(any))
                fail (Error_Bad_Word_Get(source, D_OUT));
        }
    }

    // !!! Variables should not store null isotopes, but they currently can...
    // look into a systemic answer of how and where to stop this.
    //
    Decay_If_Isotope(D_OUT);

    return D_OUT;
}


//
//  Set_Var_May_Fail: C
//
// Note this is used by both SET and the SET-BLOCK! data type in %c-eval.c
//
void Set_Var_May_Fail(
    const RELVAL *target,
    REBSPC *target_specifier,
    const RELVAL *setval,
    REBSPC *setval_specifier
){
    if (Is_Blackhole(target))  // name for a space-bearing ISSUE! ('#')
        return;

    if (Is_Nulled_Isotope(setval))
        setval = Lib(NULL);

    enum Reb_Kind kind = CELL_KIND(VAL_UNESCAPED(target));

    if (ANY_WORD_KIND(kind) or kind == REB_SYMBOL) {
        REBVAL *var = Sink_Word_May_Fail(target, target_specifier);
        Derelativize(var, setval, setval_specifier);
    }
    else if (ANY_SEQUENCE_KIND(kind)) {
        DECLARE_LOCAL (setval_specific);
        Derelativize(setval_specific, setval, setval_specifier);
        PUSH_GC_GUARD(setval_specific);
        Quotify(setval_specific, 1);

        // `set 'foo/bar 1` acts as `foo/bar: 1`
        // SET will raise an error if there are any GROUP!s, unless you use
        // the hard option, in which case they are literal.
        //
        // Though you can't dispatch enfix from a path (at least not at
        // present), the flag tells it to enfix a word in a context, or
        // it will error if that's not what it looks up to.
        //
        DECLARE_LOCAL (target_specific);
        Derelativize(target_specific, target, target_specifier);
        PUSH_GC_GUARD(target_specific);
        Quotify(target_specific, 1);

        DECLARE_LOCAL (dummy);
        if (rebRunThrows(
            dummy,
            true,
            Lib(SET), target_specific, setval_specific
        )){
            panic (dummy);  // shouldn't be possible, no executions!
        }

        DROP_GC_GUARD(target_specific);
        DROP_GC_GUARD(setval_specific);
    }
    else
        fail (Error_Bad_Value(target));
}


//
//  set: native [
//
//  {Sets a word or path to specified value (see also: UNPACK)}
//
//      return: "Same value as input"
//          [<opt> any-value!]
//      steps: "Allow GROUP! evals, returns block of reusable PICK/POKE steps"
//          [block! word! symbol!]
//
//      target "Word or path (# means ignore assignment, just return value)"
//          [blackhole! any-word! symbol! any-sequence! block!]
//      ^value [<opt> any-value!]
//  ]
//
REBNATIVE(set)
{
    INCLUDE_PARAMS_OF_SET;

    REBVAL *steps = ARG(steps);
    REBVAL *target = ARG(target);
    REBVAL *value = Meta_Unquotify(ARG(value));
    //
    // !!! Variables should not store ~null~/~blank~/~false~ isotopes, but they
    // currently can...would this be a good place to catch it?  The overall
    // return result should not be decayed to keep these matching:
    //
    //     '~null~ = x: if true [null]
    //     '~null~ = set 'x if true [null]
    //
    // See `Decay_If_Isotope()`

    if (Is_Blackhole(target))
        RETURN (value);

    if (ANY_WORD(target) or IS_SYMBOL(target)) {

      set_target:

        Copy_Cell(Sink_Word_May_Fail(target, SPECIFIED), value);

        if (REF(steps)) {
            if (not IS_SYMBOL(target)) {
                mutable_KIND3Q_BYTE(target) = REB_WORD;
                mutable_HEART_BYTE(target) = REB_WORD;
            }
            Quotify(target, 1);
            Quotify(steps, 1);
            rebElide(Lib(SET), steps, target);  // note: recursive call
        }
        RETURN (value);
    }

    if (IS_BLOCK(target))
        goto poke_steps;  // assume already a BLOCK! in "pick step" format

  blockscope {
    assert (ANY_SEQUENCE(target));

    switch (HEART_BYTE(target)) {
      case REB_BYTES:
        fail ("Cannot GET a sequence of all INTEGER!");

      case REB_WORD:  // Note: will likely become SYMBOL! instances
        assert(
            VAL_STRING(target) == PG_Dot_1_Canon
            or VAL_STRING(target) == PG_Slash_1_Canon
        );
        mutable_KIND3Q_BYTE(target) = REB_WORD;
        goto set_target;

      case REB_GET_WORD:  // `/a` or `.a`
        mutable_KIND3Q_BYTE(target) = REB_GET_WORD;
        goto set_target;

      case REB_META_WORD:  // `a/` or `a.`
        mutable_KIND3Q_BYTE(target) = REB_META_WORD;
        //
        // !!! If this is a PATH!, it should error if it's not an action...
        // and if it's a TUPLE! it should error if it is an action.  Review.
        //
        goto set_target;

      case REB_GET_GROUP:  // `/(a)` or `.(a)`
      case REB_GET_BLOCK:  // `/[a]` or `.[a]`
      case REB_META_GROUP:  // `(a)/` or `(a).`
      case REB_META_BLOCK:  // `[a]/` or `[a].`
        fail ("Single GROUP! or BLOCK! variants not handled in SET right now");

      case REB_BLOCK:
        mutable_KIND3Q_BYTE(target) = REB_BLOCK;
        break;

      default:
        panic (target);
    }

    const RELVAL *tail;
    const RELVAL *head = VAL_ARRAY_AT(&tail, target);

    // Check to see if there are no GROUP!s to evaluate.  If there are not
    // the path can be used as pick steps.
    //
    // !!! This could be optimized if sequences cached if there were any
    // groups in them.
    //
    const RELVAL *item = head;
    for (; item != tail; ++item) {
        if (IS_GROUP(item))
            break;
    }
    if (item == tail)
        goto poke_steps;

    if (not REF(steps))
        fail ("SET won't evaluate GROUP!s unless /STEPS output requested");

    // If there are GROUP!s, we need a new array.  We know the array will
    // be the same size as the input.
    //
    REBLEN len = VAL_LEN_AT(target);
    REBARR *a = Make_Array(len);
    RELVAL *dest = ARR_HEAD(a);

    item = head;
    for (; item != tail; ++item, ++dest) {
        if (not IS_GROUP(item)) {
            //
            // !!! Technically this block doesn't need to be derelativized.
            // Only the first value will have a binding, and the block
            // could have the same relativism as the original sequence.
            // Review such micro-optimizations after proof-of-concept.
            //
            Derelativize(dest, item, VAL_SPECIFIER(target));
            continue;
        }

        if (Eval_Value_Throws(D_OUT, item, VAL_SPECIFIER(target))) {
            Free_Unmanaged_Series(a);
            return R_THROWN;
        }
        Move_Cell(dest, D_OUT);

        // By convention, picker steps quote the first item if it was a
        // GROUP!.  It has to be somehow different because `('a).b` is
        // trying to pick B out of the WORD! a...not out of what is
        // fetched from A.  So if the convention is that the first item
        // of a "steps" block needs to be "fetched" we quote it.
        //
        if (item == head)
            Quotify(dest, 1);
    }
    SET_SERIES_LEN(a, len);
    Init_Block(target, a);
  }

  poke_steps:

    assert(IS_BLOCK(target));

    if (REF(steps)) {
        Quotify(steps, 1);
        rebElide(Lib(SET), ARG(steps), target);  // note: recursive call
    }

    const RELVAL *first = VAL_ARRAY_ITEM_AT(target);
    if (IS_QUOTED(first)) {
        Derelativize(D_SPARE, first, VAL_SPECIFIER(target));
    }
    else if (IS_WORD(first)) {
        Copy_Cell(
            D_SPARE,
            Lookup_Word_May_Fail(first, VAL_SPECIFIER(target))
        );
        if (IS_BAD_WORD(D_SPARE) and GET_CELL_FLAG(D_SPARE, ISOTOPE))
            fail (Error_Bad_Word_Get(first, D_SPARE));

        Quotify(D_SPARE, 1);
    }
    else
        fail (target);

    ++VAL_INDEX_RAW(target);

    if (rebRunThrows(D_OUT, true, Lib(POKE_P), D_SPARE, target, rebQ(value))) {
        //
        // !!! We've already evaluated all the groups, so there should
        // (probably?) be a rule that PICK* is not able to throw.  Review.
        //
        fail (Error_No_Catch_For_Throw(D_OUT));
    }

    // No writeback needed...simple case.
    //
    if (IS_NULLED(D_OUT))
        RETURN (value);

    // If POKE* did not return NULL then that means the poke wanted to write
    // back bits.  This only works in the WORD! case.
    //
    if (not IS_WORD(first))
        fail ("Cannot writeback immediate value in SET");

    Copy_Cell(Sink_Word_May_Fail(first, VAL_SPECIFIER(target)), D_OUT);

    RETURN (value);
}


//
//  try: native [
//
//  {Turn nulls into blanks, everything else passes through (see also: OPT)}
//
//      return: "blank if input was null, or original value otherwise"
//          [any-value!]
//      optional [<opt> any-value!]
//  ]
//
REBNATIVE(try)
{
    INCLUDE_PARAMS_OF_TRY;  // Was once known as TO-VALUE, but TRY has stuck

    REBVAL *optional = ARG(optional);

    if (IS_NULLED(optional))
        return Init_Blank(D_OUT);

    RETURN (optional);
}


//
//  opt: native [
//
//  {Convert blanks to nulls, pass through most other values (See Also: TRY)}
//
//      return: "null on blank, ~null~ if input was NULL, or original value"
//          [<opt> any-value!]
//      optional [<opt> <blank> any-value!]
//  ]
//
REBNATIVE(opt)
{
    INCLUDE_PARAMS_OF_OPT;

    // !!! Experimental: opting a null gives you a bad word.  You generally
    // don't put OPT on expressions you believe can be null, so this permits
    // creating a likely error in those cases.  To get around it, OPT TRY
    //
    if (IS_NULLED(ARG(optional)))
        return Init_Isotope(D_OUT, Canon(NULL));

    RETURN (ARG(optional));
}


//
//  resolve: native [
//
//  {Copy context by setting values in the target from those in the source.}
//
//      return: "Same as the target module"
//          [module!]
//      where [<blank> module!] "(modified)"
//      source [<blank> module!]
//      exports "Which words to export from the source"
//          [<blank> block!]
//  ]
//
REBNATIVE(resolve)
{
    INCLUDE_PARAMS_OF_RESOLVE;

    REBCTX *where = VAL_CONTEXT(ARG(where));
    REBCTX *source = VAL_CONTEXT(ARG(source));

    const RELVAL *tail;
    const RELVAL *v = VAL_ARRAY_AT(&tail, ARG(exports));
    for (; v != tail; ++v) {
        if (not IS_WORD(v))
            fail (ARG(exports));

        const REBSYM *symbol = VAL_WORD_SYMBOL(v);

        bool strict = true;

        const REBVAL *src = MOD_VAR(source, symbol, strict);
        if (src == nullptr)
            fail (v);  // fail if unset value, also?

        REBVAL *dest = MOD_VAR(where, symbol, strict);
        if (dest != nullptr) {
            // Fail if found?
            RESET(dest);
        }
        else {
            dest = Append_Context(where, nullptr, symbol);
        }

        Copy_Cell(dest, src);
    }

    RETURN (ARG(where));
}


//
//  enfixed?: native [
//
//  {TRUE if looks up to a function and gets first argument before the call}
//
//      return: [logic!]
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
//  enfix: native [
//
//  {For making enfix functions, e.g `+: enfixed :add` (copies)}
//
//      return: [action!]
//      action [action!]
//  ]
//
REBNATIVE(enfix)
{
    INCLUDE_PARAMS_OF_ENFIX;

    REBVAL *action = ARG(action);

    if (GET_ACTION_FLAG(VAL_ACTION(action), ENFIXED))
        fail (
            "ACTION! is already enfixed (review callsite, enfix changed"
            " https://forum.rebol.info/t/1156"
        );

    SET_ACTION_FLAG(VAL_ACTION(action), ENFIXED);

    RETURN (action);
}


//
//  semiquoted?: native [
//
//  {Discern if a function parameter came from an "active" evaluation.}
//
//      return: [logic!]
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
//      return: <none>
//      memory [<blank> any-series! any-context! handle!]
//  ]
//
REBNATIVE(free)
{
    INCLUDE_PARAMS_OF_FREE;

    REBVAL *v = ARG(memory);

    if (ANY_CONTEXT(v) or IS_HANDLE(v))
        fail ("FREE only implemented for ANY-SERIES! at the moment");

    REBSER *s = VAL_SERIES_ENSURE_MUTABLE(v);
    if (GET_SERIES_FLAG(s, INACCESSIBLE))
        fail ("Cannot FREE already freed series");

    Decay_Series(s);
    return Init_None(D_OUT); // !!! Could return freed value
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

    REBNOD *n = VAL_NODE1(v);

    // If the node is not a series (e.g. a pairing), it cannot be freed (as
    // a freed version of a pairing is the same size as the pairing).
    //
    // !!! Technically speaking a PAIR! could be freed as an array could, it
    // would mean converting the node.  Review.
    //
    if (n == nullptr or Is_Node_Cell(n))
        return Init_False(D_OUT);

    return Init_Logic(D_OUT, GET_SERIES_FLAG(SER(n), INACCESSIBLE));
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
        Init_Any_String(out, new_kind, VAL_WORD_SYMBOL(v));
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
            not IS_SER_UTF8(bin)
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
            mutable_SER_FLAVOR(m_cast(REBBIN*, bin)) = FLAVOR_STRING;
            str = STR(bin);

            TERM_STR_LEN_SIZE(
                m_cast(REBSTR*, str),  // legal for tweaking cached data
                num_codepoints,
                BIN_LEN(bin)
            );
            mutable_LINK(Bookmarks, m_cast(REBBIN*, bin)) = nullptr;

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
        if (CELL_HEART(cast(REBCEL(const*), v)) != REB_BYTES) {
            assert(Is_Series_Frozen(VAL_STRING(v)));
            goto any_string;  // ISSUE! series must be immutable
        }

        // If payload of an ISSUE! lives in the cell itself, a read-only
        // series must be created for the data...because otherwise there isn't
        // room for an index (which ANY-STRING! needs).  For behavior parity
        // with if the payload *was* in the series, this alias must be frozen.

        REBLEN len;
        REBSIZ size;
        REBCHR(const*) utf8 = VAL_UTF8_LEN_SIZE_AT(&len, &size, v);
        assert(size + 1 <= sizeof(PAYLOAD(Bytes, v).at_least_8));  // must fit

        REBSTR *str = Make_String_Core(size, SERIES_FLAGS_NONE);
        memcpy(SER_DATA(str), utf8, size + 1);  // +1 to include '\0'
        TERM_STR_LEN_SIZE(str, len, size);  // !!! SET_STR asserts size, review
        Freeze_Series(str);
        Init_Any_String(out, new_kind, str);
    }
    else if (ANY_STRING(v) or IS_URL(v)) {
      any_string:
        Copy_Cell(out, v);
        mutable_KIND3Q_BYTE(out) = new_kind;
        if (new_kind == REB_URL)
            mutable_HEART_BYTE(out) = REB_TEXT;
        else
            mutable_HEART_BYTE(out) = new_kind;
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
//      return: [
//          <opt> integer!
//          issue! url!
//          any-sequence! any-series! any-word!
//          frame! action!
//      ]
//      type [datatype!]
//      value [
//          <blank>
//          integer!
//          issue! url!
//          any-sequence! any-series! any-word! frame! action!
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
            switch (HEART_BYTE(v)) {
              case REB_ISSUE:
                fail ("Array Conversions of byte-oriented sequences TBD");

              case REB_WORD:
                assert(
                    VAL_WORD_SYMBOL(v) == PG_Dot_1_Canon
                    or VAL_WORD_SYMBOL(v) == PG_Slash_1_Canon
                );
                Init_Block(v, PG_2_Blanks_Array);
                break;

              case REB_GET_WORD: {
                REBARR *a = Make_Array_Core(2, NODE_FLAG_MANAGED);
                Init_Blank(ARR_HEAD(a));
                Copy_Cell(ARR_AT(a, 1), v);
                mutable_KIND3Q_BYTE(ARR_AT(a, 1)) = REB_WORD;
                mutable_HEART_BYTE(ARR_AT(a, 1)) = REB_WORD;
                SET_SERIES_LEN(a, 2);
                Init_Block(v, a);
                break; }

              case REB_META_WORD: {
                REBARR *a = Make_Array_Core(2, NODE_FLAG_MANAGED);
                Copy_Cell(ARR_HEAD(a), v);
                mutable_KIND3Q_BYTE(ARR_HEAD(a)) = REB_WORD;
                mutable_HEART_BYTE(ARR_HEAD(a)) = REB_WORD;
                Init_Blank(ARR_AT(a, 1));
                SET_SERIES_LEN(a, 2);
                Init_Block(v, a);
                break; }

              case REB_BLOCK:
                mutable_KIND3Q_BYTE(v) = REB_BLOCK;
                assert(Is_Array_Frozen_Shallow(VAL_ARRAY(v)));
                assert(VAL_INDEX(v) == 0);
                break;

              default:
                assert(false);
            }
        }
        else if (not ANY_ARRAY(v))
            goto bad_cast;

        goto adjust_v_kind;

      case REB_TUPLE:
      case REB_GET_TUPLE:
      case REB_SET_TUPLE:
      case REB_META_TUPLE:
      case REB_THE_TUPLE:
      case REB_PATH:
      case REB_GET_PATH:
      case REB_SET_PATH:
      case REB_META_PATH:
      case REB_THE_PATH:
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

        if (ANY_SEQUENCE(v)) {
            Copy_Cell(D_OUT, v);
            mutable_KIND3Q_BYTE(D_OUT)
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
                Reset_Cell_Header_Untracked(
                    TRACK(D_OUT),
                    REB_BYTES,
                    CELL_MASK_NONE
                );
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
                Freeze_Series(VAL_SERIES(D_OUT));  // must be frozen
            }
            mutable_KIND3Q_BYTE(D_OUT) = REB_ISSUE;
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
      case REB_META_WORD:
      case REB_THE_WORD: {
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

            if (not Is_Series_Frozen(s)) {
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

            if (IS_INTERN(s)) {
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

            Init_Any_Word(D_OUT, new_kind, SYM(s));
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
            if (IS_INTERN(bin))
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
                REBSIZ size;
                const REBYTE *data = VAL_BINARY_SIZE_AT(&size, v);
                str = Intern_UTF8_Managed(data, size);

                // Constrain the input in the way it would be if we were doing
                // the more efficient reuse.
                //
                mutable_SER_FLAVOR(m_cast(REBBIN*, bin)) = FLAVOR_STRING;
                Freeze_Series(bin);
            }

            return Inherit_Const(Init_Any_Word(D_OUT, new_kind, SYM(str)), v);
        }

        if (not ANY_WORD(v))
            goto bad_cast;
        goto adjust_v_kind; }

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
                VAL_STRING(v),
                ANY_WORD(v) ? 0 : VAL_OFFSET(v)
            );
            return Inherit_Const(D_OUT, v);
        }

        fail (v); }

    case REB_ACTION: {
      if (IS_FRAME(v)) {
        //
        // We want AS ACTION! AS FRAME! of an action to be basically a no-op.
        // So that means that it uses the dispatcher and details it encoded
        // in the phase.  This means COPY of a FRAME! needs to create a new
        // action identity at that moment.  There is no Make_Action() here,
        // because all frame references to this frame are the same action.
        //
        assert(ACT_EXEMPLAR(VAL_FRAME_PHASE(v)) == VAL_CONTEXT(v));
        Freeze_Array_Shallow(CTX_VARLIST(VAL_CONTEXT(v)));
        return Init_Action(
            D_OUT,
            VAL_FRAME_PHASE(v),
            ANONYMOUS,  // see note, we might have stored this in varlist slot
            VAL_FRAME_BINDING(v)
        );
      }

      fail (v); }

      default:  // all applicable types should be handled above
        break;
    }

  bad_cast:
    fail (Error_Bad_Cast_Raw(v, ARG(type)));

  adjust_v_kind:
    //
    // Fallthrough for cases where changing the type byte and potentially
    // updating the quotes is enough.
    //
    Copy_Cell(D_OUT, v);
    mutable_KIND3Q_BYTE(D_OUT)
        = mutable_HEART_BYTE(D_OUT)
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
//      return: [logic!]
//      value1 [any-series!]
//      value2 [any-series!]
//  ]
//
REBNATIVE(aliases_q)
{
    INCLUDE_PARAMS_OF_ALIASES_Q;

    return Init_Logic(D_OUT, VAL_SERIES(ARG(value1)) == VAL_SERIES(ARG(value2)));
}


//
//  null?: native [
//
//  "Tells you if the argument is not a value"
//
//      return: [logic!]
//      optional [<opt> any-value!]
//  ]
//
REBNATIVE(null_q)
{
    INCLUDE_PARAMS_OF_NULL_Q;

    return Init_Logic(D_OUT, IS_NULLED(ARG(optional)));
}


//
//  heavy: native [
//
//  {Make the heavy form of NULL (passes through all other values)}
//
//      return: [<opt> any-value!]
//      ^optional [<opt> any-value!]
//  ]
//
REBNATIVE(heavy) {
    INCLUDE_PARAMS_OF_HEAVY;

    Move_Cell(D_OUT, Meta_Unquotify(ARG(optional)));

    if (IS_NULLED(D_OUT))
        Init_Nulled_Isotope(D_OUT);

    return D_OUT;
}


//
//  light: native [
//
//  {Make the light form of NULL (passes through all other values)}
//
//      return: [<opt> any-value!]
//      ^optional [<opt> any-value!]
//  ]
//
REBNATIVE(light) {
    INCLUDE_PARAMS_OF_LIGHT;

    Move_Cell(D_OUT, Meta_Unquotify(ARG(optional)));

    Decay_If_Isotope(D_OUT);

    return D_OUT;
}


//
//  none: native [
//
//  {Make the "unfriendly" ~none~ value}
//
//      return: [bad-word!]
//  ]
//
REBNATIVE(none) {
    INCLUDE_PARAMS_OF_NONE;

    return Init_None(D_OUT);
}


//
//  decay: native [
//
//  "Turn ~null~, ~blank~ and ~false~ isotopes into their corresponding values"
//
//      return: [<opt> any-value!]
//      ^optional [<opt> any-value!]
//  ]
//
REBNATIVE(decay)
{
    INCLUDE_PARAMS_OF_DECAY;

    Move_Cell(D_OUT, Meta_Unquotify(ARG(optional)));
    Decay_If_Isotope(D_OUT);

    return D_OUT;
}


//
//  reify: native [
//
//  "Turn NULL and isotopes into plain BAD-WORD!s, pass through other values"
//
//      return: [any-value!]
//      ^optional [<opt> any-value!]
//  ]
//
REBNATIVE(reify)
{
    INCLUDE_PARAMS_OF_REIFY;

    REBVAL *v = ARG(optional);

    if (IS_NULLED(v))
        return Init_Bad_Word(D_OUT, Canon(NULL));

    if (IS_BAD_WORD(v))  // e.g. the input was an isotope form
        RETURN (v);

    assert(IS_QUOTED(v));
    RETURN (Unquotify(v, 1));
}



//
//  devoid: native [
//
//  "Make non-isotope ~void~ vanish, passing through all other values"
//
//      return: [<opt> <invisible> any-value!]
//      ^optional [<opt> any-value!]
//  ]
//
REBNATIVE(devoid)
{
    INCLUDE_PARAMS_OF_DEVOID;

    REBVAL *v = ARG(optional);

    // not quoted, so wasn't isotope...regular BAD-WORD! for examination
    //
    if (IS_BAD_WORD(v) and VAL_BAD_WORD_ID(v) == SYM_VOID)
        return D_OUT;

    RETURN (Meta_Unquotify(v));
}


//
//  denull: native [
//
//  "Make NULL vanish, passing through all other values"
//
//      return: [<opt> <invisible> any-value!]
//      ^optional [<opt> any-value!]
//  ]
//
REBNATIVE(denull)
{
    INCLUDE_PARAMS_OF_DENULL;

    REBVAL *v = ARG(optional);

    // Consider incoming NULL isotopes to be NULL as well.
    //
    // !!! What about ~void~ intents?  Is DENULL implicitly a superset of
    // DEVOID?  It seems it should be, or at least error if it's not going to
    // pass through a void intent.
    //
    if (IS_NULLED(v) or (IS_BAD_WORD(v) and VAL_BAD_WORD_ID(v) == SYM_NULL))
        return D_OUT;

    RETURN (Meta_Unquotify(v));
}


//
//  nothing?: native [
//
//  "Returns TRUE if argument is BLANK! or NULL"
//
//      return: [logic!]
//      value [<opt> any-value!]
//  ]
//
REBNATIVE(nothing_q)
//
// !!! Note that today's concept of allowing an ~unset~ isotope to be passed
// here would require this taking value as a ^META parameter.  This might
// be a good idea; when writing a function like DEFAULT it should overwrite
// the variable if it's ~unset~ too...so that might be a concept of "nothing".
// But meta functions should be rare, and it raises the question of how other
// isotopes should be handled... what if it's a ~void~ isotope?  Should it
// say it's not nothing, or error?  What about an ~asdf~ isotope?  For now
// keep it at nulled and blank.
{
    INCLUDE_PARAMS_OF_NOTHING_Q;

    return Init_Logic(D_OUT, IS_NULLED_OR_BLANK(ARG(value)));
}


//
//  something?: native [
//
//  "Returns FALSE if a argument is BLANK!, NULL, or an ~unset~ isotope"
//
//      return: [logic!]
//      value [<opt> any-value!]
//  ]
//
REBNATIVE(something_q)
//
// !!! See remarks on `nothing?` regarding ~unset~ and isotopes.
{
    INCLUDE_PARAMS_OF_SOMETHING_Q;

    return Init_Logic(D_OUT, not IS_NULLED_OR_BLANK(ARG(value)));
}
