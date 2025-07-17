//
//  file: %n-data.c
//  summary: "native functions for data and context"
//  section: natives
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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

    REBLEN len = Series_Len_At(val);
    Ucs2(const*) up = String_At(val);

    for (; len > 0; len--) {
        Ucs2Unit c;
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
DECLARE_NATIVE(ASCII_Q)
{
    INCLUDE_PARAMS_OF_ASCII_Q;

    return Init_Logic(OUT, Check_Char_Range(ARG(VALUE), 0x7f));
}


//
//  latin1?: native [
//
//  {Returns TRUE if value or string is in Latin-1 character range (below 256).}
//
//      value [any-string! char! integer!]
//  ]
//
DECLARE_NATIVE(LATIN1_Q)
{
    INCLUDE_PARAMS_OF_LATIN1_Q;

    return Init_Logic(OUT, Check_Char_Range(ARG(VALUE), 0xff));
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
DECLARE_NATIVE(AS_PAIR)
{
    INCLUDE_PARAMS_OF_AS_PAIR;

    Value* x = ARG(X);
    Value* y = ARG(Y);

    if (
        not (Is_Integer(x) or Is_Decimal(x))
        or not (Is_Integer(y) or Is_Decimal(y))
    ){
        panic ("PAIR! must currently have INTEGER! or DECIMAL! x and y values");
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
DECLARE_NATIVE(BIND)
{
    INCLUDE_PARAMS_OF_BIND;

    Value* v = ARG(VALUE);
    Value* target = ARG(TARGET);

    REBLEN flags = Bool_ARG(ONLY) ? BIND_0 : BIND_DEEP;

    REBU64 bind_types = TS_WORD;

    REBU64 add_midstream_types;
    if (Bool_ARG(NEW)) {
        add_midstream_types = TS_WORD;
    }
    else if (Bool_ARG(SET)) {
        add_midstream_types = FLAG_TYPE(TYPE_SET_WORD);
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
            panic (Error_Not_Bound_Raw(target));

        panic ("Binding to WORD! only implemented via INSIDE at this time.");
    }

    if (Any_Word(v)) {
        //
        // Bind a single word

        if (Try_Bind_Word(context, v))
            RETURN (v);

        // not in context, bind/new means add it if it's not.
        //
        if (Bool_ARG(NEW) or (Is_Set_Word(v) and Bool_ARG(SET))) {
            Append_Context(context, v, nullptr);
            RETURN (v);
        }

        panic (Error_Not_In_Context_Raw(v));
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
    if (Bool_ARG(COPY)) {
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
        Init_Any_List(OUT, Type_Of(v), copy);
    }
    else {
        at = List_At(v); // only affects binding from current index
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
//      return: [any-value!]
//      vars [block! word!]
//          {Local word(s) to the block}
//      body [block!]
//          {Block to evaluate}
//  ]
//
DECLARE_NATIVE(USE)
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
        ARG(BODY), // may be replaced with rebound copy, or left the same
        &context, // winds up managed; if no references exist, GC is ok
        ARG(VARS) // similar to the "spec" of a loop: WORD!/LIT-WORD!/BLOCK!
    );

    if (Eval_List_At_Throws(OUT, ARG(BODY)))
        return BOUNCE_THROWN;

    return OUT;
}


//
//  Did_Get_Binding_Of: C
//
bool Did_Get_Binding_Of(Value* out, const Value* v)
{
    switch (Type_Of(v)) {
    case TYPE_ACTION: {
        Stub *binding = VAL_BINDING(v); // see METHOD, RETURNs also have it
        if (not binding)
            return false;

        Init_Frame(out, CTX(binding));
        break; }

    case TYPE_WORD:
    case TYPE_SET_WORD:
    case TYPE_GET_WORD:
    case TYPE_LIT_WORD:
    case TYPE_REFINEMENT:
    case TYPE_ISSUE: {
        if (IS_WORD_UNBOUND(v))
            return false;

        // Requesting the context of a word that is relatively bound may
        // result in that word having a FRAME! incarnated as a Stub (if
        // it was not already reified.)
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
        Option(Level*) L = Level_Of_Varlist_If_Running(c);
        if (L) {
            out->payload.any_context.phase = Level_Phase(unwrap L);
            INIT_BINDING(out, LVL_BINDING(unwrap L));
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
//  element?: native [
//
//  "Test if value can be put in a block (e.g. `element? null` is FALSE)"
//
//      value [any-stable!]
//  ]
//
DECLARE_NATIVE(ELEMENT_Q)
{
    INCLUDE_PARAMS_OF_ELEMENT_Q;

    return Init_Logic(OUT, Any_Element(ARG(VALUE)));
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
DECLARE_NATIVE(UNBIND)
{
    INCLUDE_PARAMS_OF_UNBIND;

    Value* word = ARG(WORD);

    if (Any_Word(word))
        Unbind_Any_Word(word);
    else
        Unbind_Values_Core(List_At(word), nullptr, Bool_ARG(DEEP));

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
DECLARE_NATIVE(COLLECT_WORDS)
{
    INCLUDE_PARAMS_OF_COLLECT_WORDS;

    Flags flags;
    if (Bool_ARG(SET))
        flags = COLLECT_ONLY_SET_WORDS;
    else
        flags = COLLECT_ANY_WORD;

    if (Bool_ARG(DEEP))
        flags |= COLLECT_DEEP;

    UNUSED(Bool_ARG(IGNORE)); // implied used or unused by ARG(HIDDEN)'s voidness

    Cell* head = List_At(ARG(BLOCK));
    return Init_Block(
        OUT,
        Collect_Unique_Words_Managed(head, flags, ARG(HIDDEN))
    );
}


INLINE void Get_Opt_Polymorphic_May_Panic(
    Value* out,
    const Cell* v,
    Specifier* specifier,
    bool any
){
    if (Is_Void(v)) {
        Init_Nulled(out);  // may be turned to undefined after loop, or error
    }
    else if (Any_Word(v)) {
        Move_Opt_Var_May_Panic(out, v, specifier);
    }
    else if (Any_Path(v)) {
        //
        // `get 'foo/bar` acts as `:foo/bar`
        // except Get_Path_Core() doesn't allow GROUP!s in the PATH!
        //
        Get_Path_Core(out, v, specifier);
    }
    else
        panic (Error_Invalid_Core(v, specifier));

    if (not any and Is_Trash(out))
        panic (Error_No_Value_Core(v, specifier));
}


//
//  get: native [
//
//  {Gets the value of a word or path, or block of words/paths.}
//
//      return: [any-stable!]
//      source [<opt-out> any-word! any-path! block!]
//          {Word or path to get, or block of words or paths}
//      /any "Retrieve ANY-VALUE! (e.g. do not error on trash)"
//  ]
//
DECLARE_NATIVE(GET)
//
// Note: `get [x y] [some-var :some-unset-var]` would panic without TRY
{
    INCLUDE_PARAMS_OF_GET;

    Value* source = ARG(SOURCE);

    if (not Is_Block(source)) {
        Get_Opt_Polymorphic_May_Panic(OUT, source, SPECIFIED, Bool_ARG(ANY));
        return OUT;
    }

    Array* results = Make_Array(Series_Len_At(source));
    Value* dest = KNOWN(Array_Head(results));
    Cell* item = List_At(source);

    for (; NOT_END(item); ++item, ++dest) {
        Get_Opt_Polymorphic_May_Panic(
            dest,
            item,
            VAL_SPECIFIER(source),
            Bool_ARG(ANY)
        );
        Trashify_Branched(dest);  // !!! can't put nulls in blocks (blankify?)
    }

    Term_Array_Len(results, Series_Len_At(source));
    return Init_Block(OUT, results);
}


//
//  get*: native [
//
//  {Gets the value of a word or path, allows trash}
//
//      return: [any-stable!]
//      source "Word or path to get"
//          [<opt-out> <dequote> any-word! any-path!]
//  ]
//
DECLARE_NATIVE(GET_P)
//
// This is added as a compromise, as `:var` won't efficiently get ANY-VALUE!.
// At least `get* 'var` doesn't make you pay for path processing, and it's
// not a specialization so it doesn't incur that overhead.
{
    INCLUDE_PARAMS_OF_GET_P;

    Get_Opt_Polymorphic_May_Panic(
        OUT,
        ARG(SOURCE),
        SPECIFIED,
        true  // allow trash, e.g. GET/ANY
    );

    return OUT;
}


//
//  Set_Opt_Polymorphic_May_Panic: C
//
static void Set_Opt_Polymorphic_May_Panic(
    const Cell* target,
    Specifier* target_specifier,
    const Cell* value,
    Specifier* value_specifier
){
    if (Any_Word(target)) {
        Value* var = Sink_Var_May_Panic(target, target_specifier);
        Derelativize(var, value, value_specifier);
    }
    else if (Any_Path(target)) {
        DECLARE_VALUE (specific);
        Derelativize(specific, value, value_specifier);

        // `set 'foo/bar 1` acts as `foo/bar: 1`
        // Set_Path_Core() will raise an error if there are any GROUP!s
        //
        // Though you can't dispatch infix from a path (at least not at
        // present), the flag tells it to infix a word in a context, or
        // it will error if that's not what it looks up to.
        //
        Set_Path_Core(target, target_specifier, specific);
    }
    else
        panic (Error_Invalid_Core(target, target_specifier));
}


//
//  infix: native [
//
//  {Give a version of the action with the infix bit set to on or off}
//
//      return: [action!]
//      action [action!]
//      /defer "evaluate one expression on the left hand side"
//      /off "turn the infix bit off instead of on"
//  ]
//
DECLARE_NATIVE(INFIX)
//
// !!! See CELL_FLAG_INFIX_IF_ACTION regarding old vs. modern interpretation.
{
    INCLUDE_PARAMS_OF_INFIX;

    Value* v = ARG(ACTION);
    Copy_Cell(OUT, v);

    if (Bool_ARG(OFF)) {
        if (Bool_ARG(DEFER))
            panic ("Cannot use /OFF with /DEFER");
        Clear_Cell_Flag(OUT, INFIX_IF_ACTION);
        Clear_Cell_Flag(OUT, DEFER_INFIX_IF_ACTION);
        return OUT;
    }

    Set_Cell_Flag(OUT, INFIX_IF_ACTION);
    if (Bool_ARG(DEFER))
        Set_Cell_Flag(OUT, DEFER_INFIX_IF_ACTION);
    else
        Clear_Cell_Flag(OUT, DEFER_INFIX_IF_ACTION);
    return OUT;
}



//
//  set: native [
//
//  {Sets a word, path, or block of words and paths to specified value(s).}
//
//      return: [any-stable!]
//          {Will be the values set to, or void if any set values are void}
//      target [<opt-out> any-word! any-path! block!]
//          {Word or path, or block of words and paths}
//      value [any-stable! trash!]
//          "Value or block of values"
//      /single "If target and value are blocks, set each to the same value"
//      /some "blank values (or values past end of block) are not set."
//      /any "do not error on unset words"
//  ]
//
DECLARE_NATIVE(SET)
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
// be done e.g. with `set var non [trash!] (...)` or more narrow ideas like
// `set numeric-var ensure integer! (...)`.  SET thus mirrors SET-WORD! in
// allowing void assignments.
{
    INCLUDE_PARAMS_OF_SET;

    Value* target = ARG(TARGET);
    Value* value = ARG(VALUE);

    UNUSED(Bool_ARG(ANY));  // !!!provided for bootstrap at this time

    if (not Is_Block(target)) {
        assert(Any_Word(target) or Any_Path(target));

        Set_Opt_Polymorphic_May_Panic(
            target,
            SPECIFIED,
            Is_Blank(value) and Bool_ARG(SOME) ? NULLED_CELL : value,
            SPECIFIED
        );

        RETURN (value);
    }

    const Cell* item = List_At(target);

    const Cell* v;
    if (Is_Block(value) and not Bool_ARG(SINGLE))
        v = List_At(value);
    else
        v = value;

    for (
        ;
        NOT_END(item);
        ++item, (Bool_ARG(SINGLE) or IS_END(v)) ? NOOP : (++v, NOOP)
     ){
        if (Bool_ARG(SOME)) {
            if (IS_END(v))
                break; // won't be setting any further values
            if (Is_Blank(v))
                continue; // /SOME means treat blanks as no-ops
        }

        Set_Opt_Polymorphic_May_Panic(
            item,
            VAL_SPECIFIER(target),
            IS_END(v) ? BLANK_VALUE : v, // R3-Alpha/Red blank after END
            (Is_Block(value) and not Bool_ARG(SINGLE))
                ? VAL_SPECIFIER(value)
                : SPECIFIED
        );
    }

    RETURN (ARG(VALUE));
}


//
//  optional: native [
//
//  {Convert nulls to voids, pass through most other values}
//
//      return: [any-value!]
//      value [any-value!]
//      /veto "Instead of turning into a void, turn into a VETO"
//  ]
//
DECLARE_NATIVE(OPTIONAL)
{
    INCLUDE_PARAMS_OF_OPTIONAL;

    if (Is_Nulled(ARG(VALUE)))
        return Bool_ARG(VETO) ? Copy_Cell(OUT, g_error_veto) : Init_Void(OUT);

    RETURN (ARG(VALUE));
}


//
//  void: native [
//
//  {Generate transient void state}
//
//      return: [~void~]
//  ]
//
DECLARE_NATIVE(VOID)
{
    INCLUDE_PARAMS_OF_VOID;

    return Init_Void(OUT);
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
DECLARE_NATIVE(IN)
//
// !!! The argument names here are bad... not necessarily a context and not
// necessarily a word.  `code` or `source` to be bound in a `target`, perhaps?
{
    INCLUDE_PARAMS_OF_IN;

    Value* val = ARG(CONTEXT); // object, error, port, block
    Value* word = ARG(WORD);

    DECLARE_VALUE (safe);

    if (Is_Block(val) || Is_Group(val)) {
        if (Is_Word(word)) {
            const Value* v;
            REBLEN i;
            for (i = VAL_INDEX(val); i < VAL_LEN_HEAD(val); i++) {
                Get_Simple_Value_Into(
                    safe,
                    List_At_Head(val, i),
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
                            Type_Of(word),
                            Word_Symbol(word),
                            context,
                            index
                        );
                }
            }
            return nullptr;
        }

        panic (Error_Invalid(word));
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
        Type_Of(word),
        Word_Symbol(word),
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
DECLARE_NATIVE(RESOLVE)
{
    INCLUDE_PARAMS_OF_RESOLVE;

    if (Is_Integer(ARG(FROM))) {
        // check range and sign
        Int32s(ARG(FROM), 1);
    }

    UNUSED(Bool_ARG(ONLY)); // handled by noticing if ARG(FROM) is void
    Resolve_Context(
        Cell_Varlist(ARG(TARGET)),
        Cell_Varlist(ARG(SOURCE)),
        ARG(FROM),
        Bool_ARG(ALL),
        Bool_ARG(EXTEND)
    );

    RETURN (ARG(TARGET));
}


//
//  infix?: native [
//
//  {TRUE if function gets first argument before the call}
//
//      action [action!]
//  ]
//
DECLARE_NATIVE(INFIX_Q)
{
    INCLUDE_PARAMS_OF_INFIX_Q;

    Value* action = ARG(ACTION);
    return Init_Logic(OUT, Get_Cell_Flag(action, INFIX_IF_ACTION));
}


//
//  identity: native [
//
//  {Function for returning the same value that it got in (identity function)}
//
//      return: [any-stable!]
//      value [<end> any-stable!]
//          {!!! <end> flag is hack to limit infix reach to the left}
//  ]
//
DECLARE_NATIVE(IDENTITY)
//
// https://en.wikipedia.org/wiki/Identity_function
// https://stackoverflow.com/q/3136338
//
// This is assigned to <- for convenience, but cannot be used under that name
// in bootstrap with R3-Alpha.  It uses the <end>-ability to stop left reach,
// since there is no specific flag for that.
{
    INCLUDE_PARAMS_OF_IDENTITY;

    Copy_Cell(OUT, ARG(VALUE));

    return OUT;
}


//
//  free: native [
//
//  {Releases the underlying data of a value so it can no longer be accessed}
//
//      return: [~]
//      memory [any-series! any-context! handle!]
//  ]
//
DECLARE_NATIVE(FREE)
{
    INCLUDE_PARAMS_OF_FREE;

    Value* v = ARG(MEMORY);

    if (Any_Context(v) or Is_Handle(v))
        panic ("FREE only implemented for ANY-SERIES! at the moment");

    Flex* s = Cell_Flex(v);
    if (Get_Flex_Info(s, INACCESSIBLE))
        panic ("Cannot FREE already freed series");
    Panic_If_Read_Only_Flex(s);

    Decay_Flex(s);
    return Init_Trash(OUT);  // !!! Should it return freed, not-useful value?
}


//
//  free?: native [
//
//  {Tells if data has been released with FREE}
//
//      return: "Returns false if value wouldn't be FREEable (e.g. LOGIC!)"
//          [logic!]
//      value [any-stable!]
//  ]
//
DECLARE_NATIVE(FREE_Q)
{
    INCLUDE_PARAMS_OF_FREE_Q;

    Value* v = ARG(VALUE);

    Flex* s;
    if (Any_Context(v))
        s = v->payload.any_context.varlist;  // Cell_Varlist fails if freed
    else if (Is_Handle(v))
        s = v->extra.singular;
    else if (Any_Series(v))
        s = v->payload.any_series.series;  // VAL_SERIES fails if freed
    else
        return LOGIC(false);

    return Init_Logic(OUT, Get_Flex_Info(s, INACCESSIBLE));
}


//
//  as: native [
//
//  {Aliases underlying data of one series to act as another of same class}
//
//      return: [~null~ any-series! any-word!]
//      type [datatype!]
//      value [<opt-out> any-series! any-word!]
//  ]
//
DECLARE_NATIVE(AS)
{
    INCLUDE_PARAMS_OF_AS;

    Value* v = ARG(VALUE);
    Type new_type = Datatype_Type(ARG(TYPE));

    switch (new_type) {
    case TYPE_BLOCK:
    case TYPE_GROUP:
    case TYPE_PATH:
    case TYPE_LIT_PATH:
    case TYPE_GET_PATH:
        if (new_type == Type_Of(v))
            RETURN (v); // no-op

        if (not Any_List(v))
            goto bad_cast;
        break;

    case TYPE_TEXT:
    case TYPE_TAG:
    case TYPE_FILE:
    case TYPE_URL:
    case TYPE_EMAIL: {
        if (new_type == Type_Of(v))
            RETURN (v); // no-op

        // !!! Until UTF-8 Everywhere, turning ANY-WORD! into an ANY-STRING!
        // means it has to be UTF-8 decoded into Ucs2Unit (UCS-2).  We do that
        // but make sure it is locked, so that when it does give access to
        // WORD! you won't think you can mutate the data.  (Though mutable
        // WORD! should become a thing, if they're not bound or locked.)
        //
        if (Any_Word(v)) {
            Symbol* symbol = Word_Symbol(v);
            Flex* string = Make_Sized_String_UTF8(
                Symbol_Head(symbol),
                Symbol_Size(symbol)
            );
            Set_Flex_Info(string, FROZEN_DEEP);
            return Init_Any_Series(OUT, new_type, string);
        }

        // !!! Similarly, until UTF-8 Everywhere, we can't actually alias
        // the UTF-8 bytes in a binary as a WCHAR string.
        //
        if (Is_Binary(v)) {
            Flex* string = Make_Sized_String_UTF8(
                s_cast(Blob_At(v)),
                Series_Len_At(v)
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
            return Init_Any_Series(OUT, new_type, string);
        }

        if (not Any_String(v))
            goto bad_cast;
        break; }

    case TYPE_WORD:
    case TYPE_GET_WORD:
    case TYPE_SET_WORD:
    case TYPE_LIT_WORD:
    case TYPE_ISSUE:
    case TYPE_REFINEMENT: {
        if (new_type == Type_Of(v))
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
                &offset, &utf8_size, v, Series_Len_At(v)
            );
            return Init_Any_Word(
                OUT,
                new_type,
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
                new_type,
                Intern_UTF8_Managed(Blob_At(v), Series_Len_At(v))
            );
        }

        if (not Any_Word(v))
            goto bad_cast;
        break; }

    case TYPE_BINARY: {
        if (new_type == Type_Of(v))
            RETURN (v); // no-op

        // !!! A locked BINARY! shouldn't (?) complain if it exposes a
        // Symbol holding UTF-8 data, even prior to the UTF-8 conversion.
        //
        if (Any_Word(v)) {
            assert(Is_Value_Immutable(v));
            return Init_Blob(OUT, Word_Symbol(v));
        }

        if (Any_String(v)) {
            Binary* bin = Make_Utf8_From_String_At_Limit(v, Series_Len_At(v));

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

        panic (v); }

    bad_cast:;
    default:
        // all applicable types should be handled above
        panic (Error_Bad_Cast_Raw(v, ARG(TYPE)));
    }

    Copy_Cell(OUT, v);
    CHANGE_VAL_TYPE_BITS(OUT, new_type);
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
DECLARE_NATIVE(ALIASES_Q)
{
    INCLUDE_PARAMS_OF_ALIASES_Q;

    return Init_Logic(OUT, Cell_Flex(ARG(VALUE1)) == Cell_Flex(ARG(VALUE2)));
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
        return not Is_Trash(Get_Opt_Var_May_Panic(location, SPECIFIED));

    DECLARE_VALUE (temp); // result may be generated
    Get_Path_Core(temp, location, SPECIFIED);
    return not Is_Trash(temp);
}


//
//  set?: native [
//
//  "Whether a bound word or path is set (!!! shouldn't eval GROUP!s)"
//
//      location [any-word! any-path!]
//  ]
//
DECLARE_NATIVE(SET_Q)
{
    INCLUDE_PARAMS_OF_SET_Q;

    return Init_Logic(OUT, Is_Set(ARG(LOCATION)));
}


//
//  unset?: native [
//
//  "Whether a bound word or path is unset (!!! shouldn't eval GROUP!s)"
//
//      location [any-word! any-path!]
//  ]
//
DECLARE_NATIVE(UNSET_Q)
{
    INCLUDE_PARAMS_OF_UNSET_Q;

    return Init_Logic(OUT, not Is_Set(ARG(LOCATION)));
}


//
//  antiform?: native [
//
//  "Bootstrap way of responding if something is an antiform"
//
//      return: [logic!]
//      value [any-value!]
//  ]
//
DECLARE_NATIVE(ANTIFORM_Q)
{
    INCLUDE_PARAMS_OF_ANTIFORM_Q;

    Value* v = ARG(VALUE);
    switch (Type_Of(v)) {
      case TYPE_OKAY:
      case TYPE_NULLED:
      case TYPE_VOID:
      case TYPE_TRASH:
        return Init_Logic(OUT, true);

      default:
        return Init_Logic(OUT, false);
    }
}


//
//  the: native [
//
//  "Returns value passed in without evaluation."
//
//      return: {The input value, verbatim--unless /SOFT and soft quoted type}
//          [any-stable!]
//      :value {Value to literalize, ~null~ is impossible (see UNEVAL)}
//          [any-element!]
//      /soft {Evaluate if a GROUP!, GET-WORD!, or GET-PATH!}
//  ]
//
DECLARE_NATIVE(THE)
{
    INCLUDE_PARAMS_OF_THE;

    Value* v = ARG(VALUE);

    if (Bool_ARG(SOFT) and IS_QUOTABLY_SOFT(v))
        panic ("QUOTE/SOFT not currently implemented, should clone EVAL");

    Copy_Cell(OUT, v);
    return OUT;
}


//
//  noop: native [
//
//  "Do nothing, and return TRASH"
//
//      return: [~]
//  ]
//
DECLARE_NATIVE(NOOP)
//
// Having a function called "TRASH" would be deceiving, as if you fetched it
// with (get 'trash) it would be a function, not a TRASH! value.
{
    INCLUDE_PARAMS_OF_NOOP;

    return Init_Trash(OUT);
}


//
//  logical: native [
//
//  "Produces ~null~ antiform for 0, or ~okay~ antiform for all other integers"
//
//      return: [logic!]
//      number [integer!]
//  ]
//
DECLARE_NATIVE(LOGICAL)
{
    INCLUDE_PARAMS_OF_LOGICAL;

    Value* num = ARG(NUMBER);
    return Init_Logic(OUT, VAL_INT64(num) != 0);
}
