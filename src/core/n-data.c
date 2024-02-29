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


static bool Check_Char_Range(const Value* val, REBINT limit)
{
    if (IS_CHAR(val))
        return not (VAL_CHAR(val) > limit);

    if (IS_INTEGER(val))
        return not (VAL_INT64(val) > limit);

    assert(ANY_STRING(val));

    REBLEN len = VAL_LEN_AT(val);
    Ucs2(const*) up = Cell_String_At(val);

    for (; len > 0; len--) {
        REBUNI c;
        up = Ucs2_Next(&c, up);

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
DECLARE_NATIVE(ascii_q)
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
DECLARE_NATIVE(latin1_q)
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
DECLARE_NATIVE(as_pair)
{
    INCLUDE_PARAMS_OF_AS_PAIR;

    Value* x = ARG(x);
    Value* y = ARG(y);

    if (
        not (IS_INTEGER(x) or IS_DECIMAL(x))
        or not (IS_INTEGER(y) or IS_DECIMAL(y))
    ){
        fail ("PAIR! must currently have INTEGER! or DECIMAL! x and y values");
    }

    return Init_Pair(D_OUT, x, y);
}


//
//  bind: native [
//
//  "Binds words or words in arrays to the specified context."
//
//      value [action! any-array! any-word!]
//          "Value whose binding is to be set (modified) (returned)"
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
DECLARE_NATIVE(bind)
{
    INCLUDE_PARAMS_OF_BIND;

    Value* v = ARG(value);
    Value* target = ARG(target);

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
            Append_Context(context, v, nullptr);
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

    assert(ANY_ARRAY(v));

    Cell* at;
    if (REF(copy)) {
        REBARR *copy = Copy_Array_Core_Managed(
            VAL_ARRAY(v),
            VAL_INDEX(v), // at
            VAL_SPECIFIER(v),
            ARR_LEN(VAL_ARRAY(v)), // tail
            0, // extra
            ARRAY_FLAG_FILE_LINE, // flags
            TS_ARRAY // types to copy deeply
        );
        at = ARR_HEAD(copy);
        Init_Any_Array(D_OUT, VAL_TYPE(v), copy);
    }
    else {
        at = VAL_ARRAY_AT(v); // only affects binding from current index
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
DECLARE_NATIVE(use)
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
        return R_THROWN;

    return D_OUT;
}


//
//  Did_Get_Binding_Of: C
//
bool Did_Get_Binding_Of(Value* out, const Value* v)
{
    switch (VAL_TYPE(v)) {
    case REB_ACTION: {
        REBNOD *n = VAL_BINDING(v); // see METHOD... RETURNs also have binding
        if (not n)
            return false;

        Init_Frame(out, CTX(n));
        break; }

    case REB_WORD:
    case REB_SET_WORD:
    case REB_GET_WORD:
    case REB_LIT_WORD:
    case REB_REFINEMENT:
    case REB_ISSUE: {
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
            out->payload.any_context.phase = FRM_PHASE(f);
            INIT_BINDING(out, FRM_BINDING(f));
        }
        else {
            // !!! Assume the canon FRAME! value in varlist[0] is useful?
            //
            assert(VAL_BINDING(out) == UNBOUND); // canons have no binding
        }

        assert(
            not out->payload.any_context.phase
            or GET_SER_FLAG(
                ACT_PARAMLIST(out->payload.any_context.phase),
                ARRAY_FLAG_PARAMLIST
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
DECLARE_NATIVE(value_q)
{
    INCLUDE_PARAMS_OF_VALUE_Q;

    return Init_Logic(D_OUT, ANY_VALUE(ARG(optional)));
}


//
//  element?: native [
//
//  "Test if value can be put in a block (e.g. `element? null` is FALSE)"
//
//      optional [<opt> any-value!]
//  ]
//
DECLARE_NATIVE(element_q)
{
    INCLUDE_PARAMS_OF_ELEMENT_Q;

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
DECLARE_NATIVE(unbind)
{
    INCLUDE_PARAMS_OF_UNBIND;

    Value* word = ARG(word);

    if (ANY_WORD(word))
        Unbind_Any_Word(word);
    else
        Unbind_Values_Core(VAL_ARRAY_AT(word), nullptr, REF(deep));

    RETURN (word);
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
DECLARE_NATIVE(collect_words)
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

    Cell* head = VAL_ARRAY_AT(ARG(block));
    return Init_Block(
        D_OUT,
        Collect_Unique_Words_Managed(head, flags, ARG(hidden))
    );
}


INLINE void Get_Opt_Polymorphic_May_Fail(
    Value* out,
    const Cell* v,
    REBSPC *specifier,
    bool any
){
    if (IS_BAR(v)) {
        //
        // `a: 10 | b: 20 | get [a | b]` will give back `[10 | 20]`.
        // While seemingly not a very useful feature standalone, this
        // compatibility with SET could come in useful so that blocks
        // don't have to be rearranged to filter out BAR!s.
        //
        Init_Bar(out);
    }
    else if (IS_BLANK(v)) {
        Init_Nulled(out);  // may be turned to VOID! after loop, or error
    }
    else if (ANY_WORD(v)) {
        Move_Opt_Var_May_Fail(out, v, specifier);
    }
    else if (ANY_PATH(v)) {
        //
        // `get 'foo/bar` acts as `:foo/bar`
        // except Get_Path_Core() doesn't allow GROUP!s in the PATH!
        //
        Get_Path_Core(out, v, specifier);
    }
    else
        fail (Error_Invalid_Core(v, specifier));

    if (not any and IS_VOID(out))
        fail (Error_Need_Non_Void_Core(v, specifier));
}


//
//  get: native [
//
//  {Gets the value of a word or path, or block of words/paths.}
//
//      return: [<opt> any-value!]
//      source [blank! any-word! any-path! block!]
//          {Word or path to get, or block of words or paths (blank is no-op)}
//      /any "Retrieve ANY-VALUE! (e.g. do not error on VOID!)"
//  ]
//
DECLARE_NATIVE(get)
//
// Note: `get [x y] [some-var :some-unset-var]` would fail without /TRY
{
    INCLUDE_PARAMS_OF_GET;

    Value* source = ARG(source);

    if (not IS_BLOCK(source)) {
        Get_Opt_Polymorphic_May_Fail(D_OUT, source, SPECIFIED, REF(any));
        return D_OUT;
    }

    REBARR *results = Make_Arr(VAL_LEN_AT(source));
    Value* dest = KNOWN(ARR_HEAD(results));
    Cell* item = VAL_ARRAY_AT(source);

    for (; NOT_END(item); ++item, ++dest) {
        Get_Opt_Polymorphic_May_Fail(
            dest,
            item,
            VAL_SPECIFIER(source),
            REF(any)
        );
        Voidify_If_Nulled(dest);  // !!! can't put nulls in blocks (blankify?)
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
DECLARE_NATIVE(get_p)
//
// This is added as a compromise, as `:var` won't efficiently get ANY-VALUE!.
// At least `get* 'var` doesn't make you pay for path processing, and it's
// not a specialization so it doesn't incur that overhead.
{
    INCLUDE_PARAMS_OF_GET_P;

    Get_Opt_Polymorphic_May_Fail(
        D_OUT,
        ARG(source),
        SPECIFIED,
        true  // allow VOID!, e.g. GET/ANY
    );

    return D_OUT;
}


//
//  Set_Opt_Polymorphic_May_Fail: C
//
INLINE void Set_Opt_Polymorphic_May_Fail(
    const Cell* target,
    REBSPC *target_specifier,
    const Cell* value,
    REBSPC *value_specifier,
    bool enfix
){
    if (enfix and not IS_ACTION(value))
        fail ("Attempt to SET/ENFIX on a non-ACTION!");

    if (IS_BAR(target)) {
        //
        // Just skip it, e.g. `set [a | b] [1 2 3]` sets a to 1, and b
        // to 3, but drops the 2.  This functionality was achieved
        // initially with blanks, but with setting in particular there
        // are cases of `in obj 'word` which give back blank if the word
        // is not there, so it leads to too many silent errors.
    }
    else if (ANY_WORD(target)) {
        Value* var = Sink_Var_May_Fail(target, target_specifier);
        Derelativize(var, value, value_specifier);
        if (enfix)
            SET_VAL_FLAG(var, VALUE_FLAG_ENFIXED);
    }
    else if (ANY_PATH(target)) {
        DECLARE_VALUE (specific);
        Derelativize(specific, value, value_specifier);

        // `set 'foo/bar 1` acts as `foo/bar: 1`
        // Set_Path_Core() will raise an error if there are any GROUP!s
        //
        // Though you can't dispatch enfix from a path (at least not at
        // present), the flag tells it to enfix a word in a context, or
        // it will error if that's not what it looks up to.
        //
        Set_Path_Core(target, target_specifier, specific, enfix);
    }
    else
        fail (Error_Invalid_Core(target, target_specifier));
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
//      /single "If target and value are blocks, set each to the same value"
//      /some "blank values (or values past end of block) are not set."
//      /enfix "ACTION! calls through this word get first arg from left"
//      /any "do not error on unset words"
//  ]
//
DECLARE_NATIVE(set)
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
//
// Note: Initial prescriptivisim about not allowing VOID! in SET has been
// changed to allow void assignments, with the idea that preventing it can
// be done e.g. with `set var non void! (...)` or more narrow ideas like
// `set numeric-var ensure integer (...)`.  SET thus mirrors SET-WORD! in
// allowing void assignments.
{
    INCLUDE_PARAMS_OF_SET;

    Value* target = ARG(target);
    Value* value = ARG(value);

    UNUSED(REF(any));  // !!!provided for bootstrap at this time

    if (not IS_BLOCK(target)) {
        assert(ANY_WORD(target) or ANY_PATH(target));

        Set_Opt_Polymorphic_May_Fail(
            target,
            SPECIFIED,
            IS_BLANK(value) and REF(some) ? NULLED_CELL : value,
            SPECIFIED,
            REF(enfix)
        );

        RETURN (value);
    }

    const Cell* item = VAL_ARRAY_AT(target);

    const Cell* v;
    if (IS_BLOCK(value) and not REF(single))
        v = VAL_ARRAY_AT(value);
    else
        v = value;

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

        Set_Opt_Polymorphic_May_Fail(
            item,
            VAL_SPECIFIER(target),
            IS_END(v) ? BLANK_VALUE : v, // R3-Alpha/Red blank after END
            (IS_BLOCK(value) and not REF(single))
                ? VAL_SPECIFIER(value)
                : SPECIFIED,
            REF(enfix)
        );
    }

    RETURN (ARG(value));
}


//
//  try: native [
//
//  {Turn nulls/voids into blanks, all else passes through (see also: OPT)}
//
//      return: [any-value!]
//          {blank if input was null, or original value otherwise}
//      optional [<opt> any-value!]
//  ]
//
DECLARE_NATIVE(try)
{
    INCLUDE_PARAMS_OF_TRY;

    if (IS_VOID(ARG(optional)))
        fail ("TRY cannot accept VOID! values");

    if (IS_NULLED(ARG(optional)))
        return Init_Blank(D_OUT);

    RETURN (ARG(optional));
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
DECLARE_NATIVE(opt)
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
//  in: native [
//
//  "Returns the word or block bound into the given context."
//
//      return: [<opt> any-word! block! group!]
//      context [any-context! block!]
//      word [any-word! block! group!] "(modified if series)"
//  ]
//
DECLARE_NATIVE(in)
//
// !!! The argument names here are bad... not necessarily a context and not
// necessarily a word.  `code` or `source` to be bound in a `target`, perhaps?
{
    INCLUDE_PARAMS_OF_IN;

    Value* val = ARG(context); // object, error, port, block
    Value* word = ARG(word);

    DECLARE_VALUE (safe);

    if (IS_BLOCK(val) || IS_GROUP(val)) {
        if (IS_WORD(word)) {
            const Value* v;
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
                            Cell_Word_Symbol(word),
                            context,
                            index
                        );
                }
            }
            return nullptr;
        }

        fail (Error_Invalid(word));
    }

    REBCTX *context = VAL_CONTEXT(val);

    // Special form: IN object block
    if (IS_BLOCK(word) or IS_GROUP(word)) {
        Bind_Values_Deep(VAL_ARRAY_HEAD(word), context);
        RETURN (word);
    }

    REBLEN index = Find_Canon_In_Context(context, VAL_WORD_CANON(word), false);
    if (index == 0)
        return nullptr;

    return Init_Any_Word_Bound(
        D_OUT,
        VAL_TYPE(word),
        Cell_Word_Symbol(word),
        context,
        index
    );
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
DECLARE_NATIVE(resolve)
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

    RETURN (ARG(target));
}


//
//  enfixed?: native [
//
//  {TRUE if looks up to a function and gets first argument before the call}
//
//      source [any-word! any-path!]
//  ]
//
DECLARE_NATIVE(enfixed_q)
{
    INCLUDE_PARAMS_OF_ENFIXED_Q;

    Value* source = ARG(source);

    if (ANY_WORD(source)) {
        const Value* var = Get_Opt_Var_May_Fail(source, SPECIFIED);

        assert(NOT_VAL_FLAG(var, VALUE_FLAG_ENFIXED) or IS_ACTION(var));
        return Init_Logic(D_OUT, GET_VAL_FLAG(var, VALUE_FLAG_ENFIXED));
    }
    else {
        assert(ANY_PATH(source));

        DECLARE_VALUE (temp);
        Get_Path_Core(temp, source, SPECIFIED);
        assert(NOT_VAL_FLAG(temp, VALUE_FLAG_ENFIXED) or IS_ACTION(temp));
        return Init_Logic(D_OUT, GET_VAL_FLAG(temp, VALUE_FLAG_ENFIXED));
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
DECLARE_NATIVE(semiquoted_q)
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

    const Value* var = Get_Opt_Var_May_Fail(ARG(parameter), SPECIFIED);

    return Init_Logic(D_OUT, GET_VAL_FLAG(var, VALUE_FLAG_UNEVALUATED));
}


//
//  identity: native [
//
//  {Function for returning the same value that it got in (identity function)}
//
//      return: [<opt> any-value!]
//      value [<end> <opt> any-value!]
//          {!!! <end> flag is hack to limit enfix reach to the left}
//      /quote
//          {Make it seem that the return result was quoted}
//  ]
//
DECLARE_NATIVE(identity)
//
// https://en.wikipedia.org/wiki/Identity_function
// https://stackoverflow.com/q/3136338
//
// !!! Quoting version is currently specialized as SEMIQUOTE, for convenience.
//
// This is assigned to <- for convenience, but cannot be used under that name
// in bootstrap with R3-Alpha.  It uses the <end>-ability to stop left reach,
// since there is no specific flag for that.
{
    INCLUDE_PARAMS_OF_IDENTITY;

    Move_Value(D_OUT, ARG(value));

    if (REF(quote))
        SET_VAL_FLAG(D_OUT, VALUE_FLAG_UNEVALUATED);

    return D_OUT;
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
DECLARE_NATIVE(free)
{
    INCLUDE_PARAMS_OF_FREE;

    Value* v = ARG(memory);

    if (ANY_CONTEXT(v) or IS_HANDLE(v))
        fail ("FREE only implemented for ANY-SERIES! at the moment");

    REBSER *s = VAL_SERIES(v);
    if (GET_SER_INFO(s, SERIES_INFO_INACCESSIBLE))
        fail ("Cannot FREE already freed series");
    FAIL_IF_READ_ONLY_SERIES(s);

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
DECLARE_NATIVE(free_q)
{
    INCLUDE_PARAMS_OF_FREE_Q;

    Value* v = ARG(value);

    REBSER *s;
    if (ANY_CONTEXT(v))
        s = SER(v->payload.any_context.varlist); // VAL_CONTEXT fails if freed
    else if (IS_HANDLE(v))
        s = SER(v->extra.singular);
    else if (ANY_SERIES(v))
        s = v->payload.any_series.series; // VAL_SERIES fails if freed
    else
        return Init_False(D_OUT);

    return Init_Logic(D_OUT, GET_SER_INFO(s, SERIES_INFO_INACCESSIBLE));
}


//
//  as: native [
//
//  {Aliases underlying data of one series to act as another of same class}
//
//      return: [<opt> any-series! any-word!]
//      type [datatype!]
//      value [<blank> any-series! any-word!]
//  ]
//
DECLARE_NATIVE(as)
{
    INCLUDE_PARAMS_OF_AS;

    Value* v = ARG(value);
    enum Reb_Kind new_kind = VAL_TYPE_KIND(ARG(type));

    switch (new_kind) {
    case REB_BLOCK:
    case REB_GROUP:
    case REB_PATH:
    case REB_LIT_PATH:
    case REB_GET_PATH:
        if (new_kind == VAL_TYPE(v))
            RETURN (v); // no-op

        if (not ANY_ARRAY(v))
            goto bad_cast;
        break;

    case REB_TEXT:
    case REB_TAG:
    case REB_FILE:
    case REB_URL:
    case REB_EMAIL: {
        if (new_kind == VAL_TYPE(v))
            RETURN (v); // no-op

        // !!! Until UTF-8 Everywhere, turning ANY-WORD! into an ANY-STRING!
        // means it has to be UTF-8 decoded into REBUNI (UCS-2).  We do that
        // but make sure it is locked, so that when it does give access to
        // WORD! you won't think you can mutate the data.  (Though mutable
        // WORD! should become a thing, if they're not bound or locked.)
        //
        if (ANY_WORD(v)) {
            Symbol* symbol = Cell_Word_Symbol(v);
            REBSER *string = Make_Sized_String_UTF8(
                Symbol_Head(symbol),
                Symbol_Size(symbol)
            );
            SET_SER_INFO(string, SERIES_INFO_FROZEN);
            return Init_Any_Series(D_OUT, new_kind, string);
        }

        // !!! Similarly, until UTF-8 Everywhere, we can't actually alias
        // the UTF-8 bytes in a binary as a WCHAR string.
        //
        if (IS_BINARY(v)) {
            REBSER *string = Make_Sized_String_UTF8(
                cs_cast(Cell_Binary_At(v)),
                VAL_LEN_AT(v)
            );
            if (Is_Value_Immutable(v))
                SET_SER_INFO(string, SERIES_INFO_FROZEN);
            else {
                // !!! Catch any cases of people who were trying to alias the
                // binary, make mutations via the string, and see those
                // changes show up in the binary.  That can't work until UTF-8
                // everywhere.  Most callsites don't need the binary after
                // conversion...if so, tthey should AS a COPY of it for now.
                //
                Decay_Series(VAL_SERIES(v));
            }
            return Init_Any_Series(D_OUT, new_kind, string);
        }

        if (not ANY_STRING(v))
            goto bad_cast;
        break; }

    case REB_WORD:
    case REB_GET_WORD:
    case REB_SET_WORD:
    case REB_LIT_WORD:
    case REB_ISSUE:
    case REB_REFINEMENT: {
        if (new_kind == VAL_TYPE(v))
            RETURN (v); // no-op

        // !!! Until UTF-8 Everywhere, turning ANY-STRING! into an ANY-WORD!
        // means you have to have an interning of it.
        //
        if (ANY_STRING(v)) {
            //
            // Don't give misleading impression that mutations of the input
            // string will change the output word, by freezing the input.
            // This will be relaxed when mutable words exist.
            //
            Freeze_Sequence(VAL_SERIES(v));

            REBSIZ utf8_size;
            REBSIZ offset;
            Binary* temp = Temp_UTF8_At_Managed(
                &offset, &utf8_size, v, VAL_LEN_AT(v)
            );
            return Init_Any_Word(
                D_OUT,
                new_kind,
                Intern_UTF8_Managed(Binary_At(temp, offset), utf8_size)
            );
        }

        // !!! Since pre-UTF8-everywhere ANY-WORD! was saved in UTF-8 it would
        // be sort of possible to alias a binary as a WORD!.  But modification
        // wouldn't be allowed (as there are no mutable words), and also the
        // interning logic would have to take ownership of the binary if it
        // was read-only.  No one is converting binaries to words yet, so
        // wait to implement the logic until the appropriate time...just lock
        // the binary for now.
        //
        if (IS_BINARY(v)) {
            Freeze_Sequence(VAL_SERIES(v));
            return Init_Any_Word(
                D_OUT,
                new_kind,
                Intern_UTF8_Managed(Cell_Binary_At(v), VAL_LEN_AT(v))
            );
        }

        if (not ANY_WORD(v))
            goto bad_cast;
        break; }

    case REB_BINARY: {
        if (new_kind == VAL_TYPE(v))
            RETURN (v); // no-op

        // !!! A locked BINARY! shouldn't (?) complain if it exposes a
        // Symbol holding UTF-8 data, even prior to the UTF-8 conversion.
        //
        if (ANY_WORD(v)) {
            assert(Is_Value_Immutable(v));
            return Init_Binary(D_OUT, Cell_Word_Symbol(v));
        }

        if (ANY_STRING(v)) {
            Binary* bin = Make_UTF8_From_Any_String(v, VAL_LEN_AT(v));

            // !!! Making a binary out of a UCS-2 encoded string currently
            // frees the string data if it's mutable, and if that's not
            // satisfactory you can make a copy before the AS.
            //
            if (Is_Value_Immutable(v))
                Freeze_Sequence(bin);
            else
                Decay_Series(VAL_SERIES(v));

            return Init_Binary(D_OUT, bin);
        }

        fail (v); }

    bad_cast:;
    default:
        // all applicable types should be handled above
        fail (Error_Bad_Cast_Raw(v, ARG(type)));
    }

    Move_Value(D_OUT, v);
    CHANGE_VAL_TYPE_BITS(D_OUT, new_kind);
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
DECLARE_NATIVE(aliases_q)
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
INLINE bool Is_Set(const Value* location)
{
    if (ANY_WORD(location))
        return ANY_VALUE(Get_Opt_Var_May_Fail(location, SPECIFIED));

    DECLARE_VALUE (temp); // result may be generated
    Get_Path_Core(temp, location, SPECIFIED);
    return ANY_VALUE(temp);
}


//
//  set?: native/body [
//
//  "Whether a bound word or path is set (!!! shouldn't eval GROUP!s)"
//
//      location [any-word! any-path!]
//  ][
//      value? get location
//  ]
//
DECLARE_NATIVE(set_q)
{
    INCLUDE_PARAMS_OF_SET_Q;

    return Init_Logic(D_OUT, Is_Set(ARG(location)));
}


//
//  unset?: native/body [
//
//  "Whether a bound word or path is unset (!!! shouldn't eval GROUP!s)"
//
//      location [any-word! any-path!]
//  ][
//      null? get location
//  ]
//
DECLARE_NATIVE(unset_q)
{
    INCLUDE_PARAMS_OF_UNSET_Q;

    return Init_Logic(D_OUT, not Is_Set(ARG(location)));
}


//
//  quote: native/body [
//
//  "Returns value passed in without evaluation."
//
//      return: {The input value, verbatim--unless /SOFT and soft quoted type}
//          [<opt> any-value!]
//      :value {Value to quote, <opt> is impossible (see UNEVAL)}
//          [any-value!]
//      /soft {Evaluate if a GROUP!, GET-WORD!, or GET-PATH!}
//  ][
//      if soft and [match [group! get-word! get-path!] :value] [
//          eval value
//      ] else [
//          :value ;-- also sets unevaluated bit, how could a user do so?
//      ]
//  ]
//
DECLARE_NATIVE(quote)
{
    INCLUDE_PARAMS_OF_QUOTE;

    Value* v = ARG(value);

    if (REF(soft) and IS_QUOTABLY_SOFT(v))
        fail ("QUOTE/SOFT not currently implemented, should clone EVAL");

    Move_Value(D_OUT, v);
    SET_VAL_FLAG(D_OUT, VALUE_FLAG_UNEVALUATED);
    return D_OUT;
}


//
//  null: native [
//
//  "Generator for the absence of a value"
//
//      return: [<opt>]
//  ]
//
DECLARE_NATIVE(null)
{
    INCLUDE_PARAMS_OF_NULL;

    return nullptr;
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
DECLARE_NATIVE(null_q)
{
    INCLUDE_PARAMS_OF_NULL_Q;

    return Init_Logic(D_OUT, IS_NULLED(ARG(optional)));
}


//
//  Is_Voided: C
//
bool Is_Voided(const Value* location) {
    if (ANY_WORD(location))
        return IS_VOID(Get_Opt_Var_May_Fail(location, SPECIFIED));

    DECLARE_VALUE (temp); // result may be generated
    Get_Path_Core(temp, location, SPECIFIED);
    return IS_VOID(temp);
}


//
//  voided?: native [
//
//  {Tells you if a path or variable are set to VOID!}
//
//      return: [logic!]
//      var [word! path!]
//  ]
//
DECLARE_NATIVE(voided_q)
{
    INCLUDE_PARAMS_OF_VOIDED_Q;

    return Init_Logic(D_OUT, Is_Voided(ARG(var)));
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
DECLARE_NATIVE(voidify)
{
    INCLUDE_PARAMS_OF_VOIDIFY;

    if (IS_NULLED(ARG(optional)))
        return Init_Void(D_OUT);

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
//          blank? :value
//      ]
//  ]
//
DECLARE_NATIVE(nothing_q)
{
    INCLUDE_PARAMS_OF_NOTHING_Q;

    // !!! Should VOID! be considered "nothing" also?
    //
    return Init_Logic(D_OUT, IS_NULLED_OR_BLANK(ARG(value)));
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
DECLARE_NATIVE(something_q)
{
    INCLUDE_PARAMS_OF_SOMETHING_Q;

    return Init_Logic(D_OUT, not IS_NULLED_OR_BLANK(ARG(value)));
}
