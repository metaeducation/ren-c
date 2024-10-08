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
    if (Is_Char(val))
        return not (VAL_CHAR(val) > limit);

    if (Is_Integer(val))
        return not (VAL_INT64(val) > limit);

    assert(Any_String(val));

    REBLEN len = Cell_Series_Len_At(val);
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

    return Init_Logic(OUT, Check_Char_Range(ARG(value), 0x7f));
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

    return Init_Logic(OUT, Check_Char_Range(ARG(value), 0xff));
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
        not (Is_Integer(x) or Is_Decimal(x))
        or not (Is_Integer(y) or Is_Decimal(y))
    ){
        fail ("PAIR! must currently have INTEGER! or DECIMAL! x and y values");
    }

    return Init_Pair(OUT, x, y);
}


//
//  bind: native [
//
//  "Binds words or words in arrays to the specified context."
//
//      value [action! any-list! any-word!]
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

    VarList* context;

    // !!! For now, force reification before doing any binding.

    if (Any_Context(target)) {
        //
        // Get target from an OBJECT!, ERROR!, PORT!, MODULE!, FRAME!
        //
        context = Cell_Varlist(target);
    }
    else {
        assert(Any_Word(target));
        if (IS_WORD_UNBOUND(target))
            fail (Error_Not_Bound_Raw(target));

        fail ("Binding to WORD! only implemented via INSIDE at this time.");
    }

    if (Any_Word(v)) {
        //
        // Bind a single word

        if (Try_Bind_Word(context, v))
            RETURN (v);

        // not in context, bind/new means add it if it's not.
        //
        if (REF(new) or (Is_Set_Word(v) and REF(set))) {
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
    if (Is_Action(v)) {
        Copy_Cell(OUT, v);
        INIT_BINDING(OUT, context);
        return OUT;
    }

    assert(Any_List(v));

    Cell* at;
    if (REF(copy)) {
        Array* copy = Copy_Array_Core_Managed(
            Cell_Array(v),
            VAL_INDEX(v), // at
            VAL_SPECIFIER(v),
            Array_Len(Cell_Array(v)), // tail
            0, // extra
            ARRAY_FLAG_HAS_FILE_LINE, // flags
            TS_LIST // types to copy deeply
        );
        at = Array_Head(copy);
        Init_Any_List(OUT, VAL_TYPE(v), copy);
    }
    else {
        at = Cell_List_At(v); // only affects binding from current index
        Copy_Cell(OUT, v);
    }

    Bind_Values_Core(
        at,
        context,
        bind_types,
        add_midstream_types,
        flags
    );

    return OUT;
}


//
//  use: native [
//
//  {Defines words local to a block.}
//
//      return: [~null~ any-value!]
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

    VarList* context;
    Virtual_Bind_Deep_To_New_Context(
        ARG(body), // may be replaced with rebound copy, or left the same
        &context, // winds up managed; if no references exist, GC is ok
        ARG(vars) // similar to the "spec" of a loop: WORD!/LIT-WORD!/BLOCK!
    );

    if (Eval_List_At_Throws(OUT, ARG(body)))
        return BOUNCE_THROWN;

    return OUT;
}


//
//  Did_Get_Binding_Of: C
//
bool Did_Get_Binding_Of(Value* out, const Value* v)
{
    switch (VAL_TYPE(v)) {
    case REB_ACTION: {
        Stub *binding = VAL_BINDING(v); // see METHOD, RETURNs also have it
        if (not binding)
            return false;

        Init_Frame(out, CTX(binding));
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
        // result in that word having a FRAME! incarnated as a Stub node (if
        // it was not already reified.)
        //
        // !!! In the future Reb_Context will refer to a Node*, and only
        // be reified based on the properties of the cell into which it is
        // moved (e.g. OUT would be examined here to determine if it would
        // have a longer lifetime than the Level* or other node)
        //
        VarList* c = VAL_WORD_CONTEXT(v);
        Copy_Cell(out, Varlist_Archetype(c));
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
    if (Is_Frame(out)) {
        VarList* c = Cell_Varlist(out);
        Level* L = Level_Of_Varlist_If_Running(c);
        if (L) {
            out->payload.any_context.phase = Level_Phase(L);
            INIT_BINDING(out, LVL_BINDING(L));
        }
        else {
            // !!! Assume the canon FRAME! value in varlist[0] is useful?
            //
            assert(VAL_BINDING(out) == UNBOUND); // canons have no binding
        }

        assert(
            not out->payload.any_context.phase
            or Get_Array_Flag(
                ACT_PARAMLIST(out->payload.any_context.phase),
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
//      optional [~null~ any-value!]
//  ]
//
DECLARE_NATIVE(value_q)
{
    INCLUDE_PARAMS_OF_VALUE_Q;

    return Init_Logic(OUT, Any_Value(ARG(optional)));
}


//
//  element?: native [
//
//  "Test if value can be put in a block (e.g. `element? null` is FALSE)"
//
//      optional [~null~ any-value!]
//  ]
//
DECLARE_NATIVE(element_q)
{
    INCLUDE_PARAMS_OF_ELEMENT_Q;

    return Init_Logic(OUT, Any_Value(ARG(optional)));
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

    if (Any_Word(word))
        Unbind_Any_Word(word);
    else
        Unbind_Values_Core(Cell_List_At(word), nullptr, REF(deep));

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

    Flags flags;
    if (REF(set))
        flags = COLLECT_ONLY_SET_WORDS;
    else
        flags = COLLECT_ANY_WORD;

    if (REF(deep))
        flags |= COLLECT_DEEP;

    UNUSED(REF(ignore)); // implied used or unused by ARG(hidden)'s voidness

    Cell* head = Cell_List_At(ARG(block));
    return Init_Block(
        OUT,
        Collect_Unique_Words_Managed(head, flags, ARG(hidden))
    );
}


INLINE void Get_Opt_Polymorphic_May_Fail(
    Value* out,
    const Cell* v,
    Specifier* specifier,
    bool any
){
    if (Is_Void(v)) {
        Init_Nulled(out);  // may be turned to undefined after loop, or error
    }
    else if (Any_Word(v)) {
        Move_Opt_Var_May_Fail(out, v, specifier);
    }
    else if (Any_Path(v)) {
        //
        // `get 'foo/bar` acts as `:foo/bar`
        // except Get_Path_Core() doesn't allow GROUP!s in the PATH!
        //
        Get_Path_Core(out, v, specifier);
    }
    else
        fail (Error_Invalid_Core(v, specifier));

    if (not any and Is_Nothing(out))
        fail (Error_No_Value_Core(v, specifier));
}


//
//  get: native [
//
//  {Gets the value of a word or path, or block of words/paths.}
//
//      return: [~null~ any-value!]
//      source [<maybe> any-word! any-path! block!]
//          {Word or path to get, or block of words or paths}
//      /any "Retrieve ANY-VALUE! (e.g. do not error on trash)"
//  ]
//
DECLARE_NATIVE(get)
//
// Note: `get [x y] [some-var :some-unset-var]` would fail without /TRY
{
    INCLUDE_PARAMS_OF_GET;

    Value* source = ARG(source);

    if (not Is_Block(source)) {
        Get_Opt_Polymorphic_May_Fail(OUT, source, SPECIFIED, REF(any));
        return OUT;
    }

    Array* results = Make_Array(Cell_Series_Len_At(source));
    Value* dest = KNOWN(Array_Head(results));
    Cell* item = Cell_List_At(source);

    for (; NOT_END(item); ++item, ++dest) {
        Get_Opt_Polymorphic_May_Fail(
            dest,
            item,
            VAL_SPECIFIER(source),
            REF(any)
        );
        Nothingify_Branched(dest);  // !!! can't put nulls in blocks (blankify?)
    }

    Term_Array_Len(results, Cell_Series_Len_At(source));
    return Init_Block(OUT, results);
}


//
//  get*: native [
//
//  {Gets the value of a word or path, allows trash}
//
//      return: [~null~ any-value!]
//      source "Word or path to get"
//          [<maybe> <dequote> any-word! any-path!]
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
        OUT,
        ARG(source),
        SPECIFIED,
        true  // allow trash, e.g. GET/ANY
    );

    return OUT;
}


//
//  Set_Opt_Polymorphic_May_Fail: C
//
INLINE void Set_Opt_Polymorphic_May_Fail(
    const Cell* target,
    Specifier* target_specifier,
    const Cell* value,
    Specifier* value_specifier
){
    if (Any_Word(target)) {
        Value* var = Sink_Var_May_Fail(target, target_specifier);
        Derelativize(var, value, value_specifier);
    }
    else if (Any_Path(target)) {
        DECLARE_VALUE (specific);
        Derelativize(specific, value, value_specifier);

        // `set 'foo/bar 1` acts as `foo/bar: 1`
        // Set_Path_Core() will raise an error if there are any GROUP!s
        //
        // Though you can't dispatch enfix from a path (at least not at
        // present), the flag tells it to enfix a word in a context, or
        // it will error if that's not what it looks up to.
        //
        Set_Path_Core(target, target_specifier, specific);
    }
    else
        fail (Error_Invalid_Core(target, target_specifier));
}


//
//  enfix: native [
//
//  {Give a version of the action with the infix bit set to on or off}
//
//      return: [action!]
//      action [action!]
//      /off "turn the infix bit off instead of on"
//  ]
//
DECLARE_NATIVE(enfix) {
    INCLUDE_PARAMS_OF_ENFIX;

    Value* v = ARG(action);

    Copy_Cell(OUT, v);
    if (REF(off))
        CLEAR_VAL_FLAG(OUT, VALUE_FLAG_ENFIXED);
    else
        SET_VAL_FLAG(OUT, VALUE_FLAG_ENFIXED);

    return OUT;
}



//
//  set: native [
//
//  {Sets a word, path, or block of words and paths to specified value(s).}
//
//      return: [~null~ any-value!]
//          {Will be the values set to, or void if any set values are void}
//      target [<maybe> any-word! any-path! block!]
//          {Word or path, or block of words and paths}
//      value [~null~ any-value!]
//          "Value or block of values"
//      /single "If target and value are blocks, set each to the same value"
//      /some "blank values (or values past end of block) are not set."
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
// Note: Initial prescriptivisim about not allowing trash in SET has been
// changed to allow void assignments, with the idea that preventing it can
// be done e.g. with `set var non [nothing!] (...)` or more narrow ideas like
// `set numeric-var ensure integer! (...)`.  SET thus mirrors SET-WORD! in
// allowing void assignments.
{
    INCLUDE_PARAMS_OF_SET;

    Value* target = ARG(target);
    Value* value = ARG(value);

    UNUSED(REF(any));  // !!!provided for bootstrap at this time

    if (not Is_Block(target)) {
        assert(Any_Word(target) or Any_Path(target));

        Set_Opt_Polymorphic_May_Fail(
            target,
            SPECIFIED,
            Is_Blank(value) and REF(some) ? NULLED_CELL : value,
            SPECIFIED
        );

        RETURN (value);
    }

    const Cell* item = Cell_List_At(target);

    const Cell* v;
    if (Is_Block(value) and not REF(single))
        v = Cell_List_At(value);
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
            if (Is_Blank(v))
                continue; // /SOME means treat blanks as no-ops
        }

        Set_Opt_Polymorphic_May_Fail(
            item,
            VAL_SPECIFIER(target),
            IS_END(v) ? BLANK_VALUE : v, // R3-Alpha/Red blank after END
            (Is_Block(value) and not REF(single))
                ? VAL_SPECIFIER(value)
                : SPECIFIED
        );
    }

    RETURN (ARG(value));
}


//
//  void: native [
//
//  {Absence of a value, used to opt out of many routines (appending, etc.)}
//
//      return: [~void~]
//  ]
//
DECLARE_NATIVE(void) {
    INCLUDE_PARAMS_OF_VOID;

    return Init_Void(OUT);
}


//
//  maybe: native [
//
//  {Convert nulls to voids, pass through most other values}
//
//      return: [any-value!]
//      optional [~null~ any-value!]
//  ]
//
DECLARE_NATIVE(maybe)
{
    INCLUDE_PARAMS_OF_MAYBE;

    if (Is_Nulled(ARG(optional)))
        return Init_Void(OUT);

    RETURN (ARG(optional));
}


//
//  in: native [
//
//  "Returns the word or block bound into the given context."
//
//      return: [~null~ any-word! block! group!]
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

    if (Is_Block(val) || Is_Group(val)) {
        if (Is_Word(word)) {
            const Value* v;
            REBLEN i;
            for (i = VAL_INDEX(val); i < VAL_LEN_HEAD(val); i++) {
                Get_Simple_Value_Into(
                    safe,
                    Cell_List_At_Head(val, i),
                    VAL_SPECIFIER(val)
                );

                v = safe;
                if (Is_Object(v)) {
                    VarList* context = Cell_Varlist(v);
                    REBLEN index = Find_Canon_In_Context(
                        context, VAL_WORD_CANON(word), false
                    );
                    if (index != 0)
                        return Init_Any_Word_Bound(
                            OUT,
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

    VarList* context = Cell_Varlist(val);

    // Special form: IN object block
    if (Is_Block(word) or Is_Group(word)) {
        Bind_Values_Deep(VAL_ARRAY_HEAD(word), context);
        RETURN (word);
    }

    REBLEN index = Find_Canon_In_Context(context, VAL_WORD_CANON(word), false);
    if (index == 0)
        return nullptr;

    return Init_Any_Word_Bound(
        OUT,
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

    if (Is_Integer(ARG(from))) {
        // check range and sign
        Int32s(ARG(from), 1);
    }

    UNUSED(REF(only)); // handled by noticing if ARG(from) is void
    Resolve_Context(
        Cell_Varlist(ARG(target)),
        Cell_Varlist(ARG(source)),
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

    if (Any_Word(source)) {
        const Value* var = Get_Opt_Var_May_Fail(source, SPECIFIED);

        assert(NOT_VAL_FLAG(var, VALUE_FLAG_ENFIXED) or Is_Action(var));
        return Init_Logic(OUT, GET_VAL_FLAG(var, VALUE_FLAG_ENFIXED));
    }
    else {
        assert(Any_Path(source));

        DECLARE_VALUE (temp);
        Get_Path_Core(temp, source, SPECIFIED);
        assert(NOT_VAL_FLAG(temp, VALUE_FLAG_ENFIXED) or Is_Action(temp));
        return Init_Logic(OUT, GET_VAL_FLAG(temp, VALUE_FLAG_ENFIXED));
    }
}


//
//  identity: native [
//
//  {Function for returning the same value that it got in (identity function)}
//
//      return: [~null~ any-value!]
//      value [<end> ~null~ any-value!]
//          {!!! <end> flag is hack to limit enfix reach to the left}
//  ]
//
DECLARE_NATIVE(identity)
//
// https://en.wikipedia.org/wiki/Identity_function
// https://stackoverflow.com/q/3136338
//
// This is assigned to <- for convenience, but cannot be used under that name
// in bootstrap with R3-Alpha.  It uses the <end>-ability to stop left reach,
// since there is no specific flag for that.
{
    INCLUDE_PARAMS_OF_IDENTITY;

    Copy_Cell(OUT, ARG(value));

    return OUT;
}


//
//  free: native [
//
//  {Releases the underlying data of a value so it can no longer be accessed}
//
//      return: [nothing!]
//      memory [any-series! any-context! handle!]
//  ]
//
DECLARE_NATIVE(free)
{
    INCLUDE_PARAMS_OF_FREE;

    Value* v = ARG(memory);

    if (Any_Context(v) or Is_Handle(v))
        fail ("FREE only implemented for ANY-SERIES! at the moment");

    Flex* s = Cell_Flex(v);
    if (Get_Flex_Info(s, INACCESSIBLE))
        fail ("Cannot FREE already freed series");
    Fail_If_Read_Only_Flex(s);

    Decay_Flex(s);
    return Init_Nothing(OUT);  // !!! Should it return freed, not-useful value?
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

    Flex* s;
    if (Any_Context(v))
        s = v->payload.any_context.varlist;  // Cell_Varlist fails if freed
    else if (Is_Handle(v))
        s = v->extra.singular;
    else if (Any_Series(v))
        s = v->payload.any_series.series;  // VAL_SERIES fails if freed
    else
        return Init_False(OUT);

    return Init_Logic(OUT, Get_Flex_Info(s, INACCESSIBLE));
}


//
//  as: native [
//
//  {Aliases underlying data of one series to act as another of same class}
//
//      return: [~null~ any-series! any-word!]
//      type [datatype!]
//      value [<maybe> any-series! any-word!]
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

        if (not Any_List(v))
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
        if (Any_Word(v)) {
            Symbol* symbol = Cell_Word_Symbol(v);
            Flex* string = Make_Sized_String_UTF8(
                Symbol_Head(symbol),
                Symbol_Size(symbol)
            );
            Set_Flex_Info(string, FROZEN_DEEP);
            return Init_Any_Series(OUT, new_kind, string);
        }

        // !!! Similarly, until UTF-8 Everywhere, we can't actually alias
        // the UTF-8 bytes in a binary as a WCHAR string.
        //
        if (Is_Binary(v)) {
            Flex* string = Make_Sized_String_UTF8(
                cs_cast(Cell_Blob_At(v)),
                Cell_Series_Len_At(v)
            );
            if (Is_Value_Immutable(v))
                Set_Flex_Info(string, FROZEN_DEEP);
            else {
                // !!! Catch any cases of people who were trying to alias the
                // binary, make mutations via the string, and see those
                // changes show up in the binary.  That can't work until UTF-8
                // everywhere.  Most callsites don't need the binary after
                // conversion...if so, tthey should AS a COPY of it for now.
                //
                Decay_Flex(Cell_Flex(v));
            }
            return Init_Any_Series(OUT, new_kind, string);
        }

        if (not Any_String(v))
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
        if (Any_String(v)) {
            //
            // Don't give misleading impression that mutations of the input
            // string will change the output word, by freezing the input.
            // This will be relaxed when mutable words exist.
            //
            Freeze_Non_Array_Flex(Cell_Flex(v));

            Size utf8_size;
            Size offset;
            Binary* temp = Temp_UTF8_At_Managed(
                &offset, &utf8_size, v, Cell_Series_Len_At(v)
            );
            return Init_Any_Word(
                OUT,
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
        if (Is_Binary(v)) {
            Freeze_Non_Array_Flex(Cell_Flex(v));
            return Init_Any_Word(
                OUT,
                new_kind,
                Intern_UTF8_Managed(Cell_Blob_At(v), Cell_Series_Len_At(v))
            );
        }

        if (not Any_Word(v))
            goto bad_cast;
        break; }

    case REB_BINARY: {
        if (new_kind == VAL_TYPE(v))
            RETURN (v); // no-op

        // !!! A locked BINARY! shouldn't (?) complain if it exposes a
        // Symbol holding UTF-8 data, even prior to the UTF-8 conversion.
        //
        if (Any_Word(v)) {
            assert(Is_Value_Immutable(v));
            return Init_Blob(OUT, Cell_Word_Symbol(v));
        }

        if (Any_String(v)) {
            Binary* bin = Make_Utf8_From_Cell_String_At_Limit(v, Cell_Series_Len_At(v));

            // !!! Making a binary out of a UCS-2 encoded string currently
            // frees the string data if it's mutable, and if that's not
            // satisfactory you can make a copy before the AS.
            //
            if (Is_Value_Immutable(v))
                Freeze_Non_Array_Flex(bin);
            else
                Decay_Flex(Cell_Flex(v));

            return Init_Blob(OUT, bin);
        }

        fail (v); }

    bad_cast:;
    default:
        // all applicable types should be handled above
        fail (Error_Bad_Cast_Raw(v, ARG(type)));
    }

    Copy_Cell(OUT, v);
    CHANGE_VAL_TYPE_BITS(OUT, new_kind);
    return OUT;
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

    return Init_Logic(OUT, Cell_Flex(ARG(value1)) == Cell_Flex(ARG(value2)));
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
    if (Any_Word(location))
        return not Is_Nothing(Get_Opt_Var_May_Fail(location, SPECIFIED));

    DECLARE_VALUE (temp); // result may be generated
    Get_Path_Core(temp, location, SPECIFIED);
    return not Is_Nothing(temp);
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

    return Init_Logic(OUT, Is_Set(ARG(location)));
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

    return Init_Logic(OUT, not Is_Set(ARG(location)));
}


//
//  the: native [
//
//  "Returns value passed in without evaluation."
//
//      return: {The input value, verbatim--unless /SOFT and soft quoted type}
//          [~null~ any-value!]
//      :value {Value to literalize, ~null~ is impossible (see UNEVAL)}
//          [any-value!]
//      /soft {Evaluate if a GROUP!, GET-WORD!, or GET-PATH!}
//  ]
//
DECLARE_NATIVE(the)
{
    INCLUDE_PARAMS_OF_THE;

    Value* v = ARG(value);

    if (REF(soft) and IS_QUOTABLY_SOFT(v))
        fail ("QUOTE/SOFT not currently implemented, should clone EVAL");

    Copy_Cell(OUT, v);
    return OUT;
}


//
//  null: native [
//
//  "Generator for the absence of a value"
//
//      return: [~null~]
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
//      optional [~null~ any-value!]
//  ][
//      null = type of :optional
//  ]
//
DECLARE_NATIVE(null_q)
{
    INCLUDE_PARAMS_OF_NULL_Q;

    return Init_Logic(OUT, Is_Nulled(ARG(optional)));
}
