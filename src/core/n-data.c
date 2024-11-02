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





//
//  /bind: native [
//
//  "Binds words or words in lists to the specified context"
//
//      return: [frame! action? any-list? any-path? any-word? quoted?]
//      value "Value whose binding is to be set (modified) (returned)"
//          [any-list? any-path? any-word? quoted?]
//      target "Target context or a word whose binding should be the target"
//          [any-word? any-context?]
//      :copy "Bind and return a deep copy of a block, don't modify original"
//      :only "Bind only first block (not deep)"
//      :new "Add to context any new words found"
//      :set "Add to context any new set-words found"
//  ]
//
DECLARE_NATIVE(bind)
{
    INCLUDE_PARAMS_OF_BIND;

    Value* v = ARG(value);
    Value* target = ARG(target);

    REBLEN flags = REF(only) ? BIND_0 : BIND_DEEP;

    Option(SymId) add_midstream_types;
    if (REF(new)) {
        add_midstream_types = SYM_ANY;
    }
    else if (REF(set)) {
        add_midstream_types = SYM_SET;
    }
    else
        add_midstream_types = SYM_0;

    const Value* context;

    // !!! For now, force reification before doing any binding.

    if (Any_Context(target)) {
        //
        // Get target from an OBJECT!, ERROR!, PORT!, MODULE!, FRAME!
        //
        context = target;
    }
    else {
        assert(Any_Word(target));

        if (IS_WORD_UNBOUND(target))
            return FAIL(Error_Not_Bound_Raw(target));

        return FAIL("Bind to WORD! only implemented via INSIDE at this time");
    }

    if (Wordlike_Cell(v)) {
        //
        // Bind a single word (also works on refinements, `/a` ...or `a.`, etc.

        if (Try_Bind_Word(context, v))
            return COPY(v);

        // not in context, BIND:NEW means add it if it's not.
        //
        if (REF(new) or (Is_Set_Word(v) and REF(set))) {
            Init_Nothing(Append_Context_Bind_Word(Cell_Varlist(context), v));
            return COPY(v);
        }

        return FAIL(Error_Not_In_Context_Raw(v));
    }

    if (not Listlike_Cell(v))  // QUOTED? could have wrapped any type
        return FAIL(Error_Invalid_Arg(level_, PARAM(value)));

    Element* at;
    const Element* tail;
    if (REF(copy)) {
        bool deeply = true;
        Source* copy = cast(Source*, Copy_Array_Core_Managed(
            FLEX_MASK_MANAGED_SOURCE,
            Cell_Array(v),
            VAL_INDEX(v), // at
            Array_Len(Cell_Array(v)), // tail
            0, // extra
            deeply  // !!! types to copy deeply (was once just TS_ARRAY)
        ));
        at = Array_Head(copy);
        tail = Array_Tail(copy);
        Init_Any_List(OUT, Cell_Heart_Ensure_Noquote(v), copy);
        BINDING(OUT) = BINDING(v);
    }
    else {
        Ensure_Mutable(v);  // use IN for virtual binding
        at = Cell_List_At_Mutable_Hack(&tail, v);  // !!! only *after* index!
        Copy_Cell(OUT, v);
    }

    Bind_Values_Core(
        at,
        tail,
        context,
        add_midstream_types,
        flags
    );

    return OUT;
}


//
//  /inside: native [
//
//  "Returns a view of the input bound virtually to the context"
//
//      return: [~null~ any-value?]
//      where [any-context? any-list? any-sequence?]
//      element [<maybe> element?]  ; QUOTED? support?
//  ]
//
DECLARE_NATIVE(inside)
{
    INCLUDE_PARAMS_OF_INSIDE;

    Element* element = cast(Element*, ARG(element));
    Value* where = ARG(where);

    Context* context;
    if (Any_Context(where))
        context = Cell_Varlist(where);
    else if (Any_List(where))
        context = BINDING(where);
    else {
        assert(Any_Sequence(where));
        context = Cell_Sequence_Binding(where);
    }

    Derelativize(OUT, element, context);
    return OUT;
}


//
//  /overbind: native [
//
//  "Add definitions from context to environment of value"
//
//      return: [~null~ any-value?]
//      definitions [word! any-context?]
//      value [<maybe> any-list?]  ; QUOTED? support?
//  ]
//
DECLARE_NATIVE(overbind)
{
    INCLUDE_PARAMS_OF_OVERBIND;

    Element* v = cast(Element*, ARG(value));
    Element* defs = cast(Element*, ARG(definitions));

    if (Is_Word(defs)) {
        if (IS_WORD_UNBOUND(defs))
            return FAIL(Error_Not_Bound_Raw(defs));
    }
    else
        assert(Any_Context(defs));

    BINDING(v) = Make_Use_Core(defs, Cell_List_Binding(v), CELL_MASK_0);

    return COPY(v);
}


//
//  /has: native [
//
//  "Returns a word bound into the context, if it's available, else null"
//
//      return: [~null~ any-word?]
//      context [any-context?]
//      value [<maybe> any-word?]  ; QUOTED? support?
//  ]
//
DECLARE_NATIVE(has)
{
    INCLUDE_PARAMS_OF_HAS;

    VarList* ctx = Cell_Varlist(ARG(context));
    Value* v = ARG(value);

    assert(Any_Word(v));
    Heart heart = Cell_Heart(v);

    const Symbol* symbol = Cell_Word_Symbol(v);
    const bool strict = true;
    Option(Index) index = Find_Symbol_In_Context(ARG(context), symbol, strict);
    if (not index)
        return nullptr;
    if (CTX_TYPE(ctx) != REB_MODULE)
        return Init_Any_Word_Bound(OUT, heart, symbol, ctx, unwrap index);

    Init_Any_Word(OUT, heart, symbol);
    Tweak_Cell_Word_Index(OUT, INDEX_PATCHED);
    BINDING(OUT) = MOD_PATCH(cast(SeaOfVars*, ctx), symbol, strict);
    return OUT;
}


//
//  /without: native [
//
//  "Remove a virtual binding from a value"
//
//      return: [~null~ any-word? any-list?]
//      context "If integer, then removes that number of virtual bindings"
//          [integer! any-context?]
//      value [<const> <maybe> any-word? any-list?]  ; QUOTED? support?
//  ]
//
DECLARE_NATIVE(without)
{
    INCLUDE_PARAMS_OF_WITHOUT;

    VarList* ctx = Cell_Varlist(ARG(context));
    Value* v = ARG(value);

    // !!! Note that BIND of a WORD! in historical Rebol/Red would return the
    // input word as-is if the word wasn't in the requested context, while
    // IN would return TRASH! on failure.  We carry forward the NULL-failing
    // here in IN, but BIND's behavior on words may need revisiting.
    //
    if (Any_Word(v)) {
        const Symbol* symbol = Cell_Word_Symbol(v);
        const bool strict = true;
        Option(Index) index = Find_Symbol_In_Context(
            ARG(context), symbol, strict
        );
        if (not index)
            return nullptr;
        return Init_Any_Word_Bound(
            OUT,
            Cell_Heart_Ensure_Noquote(v),
            symbol,  // !!! incoming case...consider impact of strict if false?
            ctx,
            unwrap index
        );
    }

    BINDING(v) = Make_Use_Core(
        Varlist_Archetype(ctx),
        Cell_List_Binding(v),
        CELL_MASK_0
    );

    return COPY(v);
}


//
//  /use: native [
//
//  "Defines words local to a block (See also: LET)"
//
//      return: [any-value?]
//      vars "Local word(s) to the block"
//          [block! word!]
//      body "Block to evaluate"
//          [block!]
//  ]
//
DECLARE_NATIVE(use)
//
// !!! USE is somewhat deprecated, because LET does something very similar
// without bringing in indentation and an extra block.  The USE word is being
// considered for a more interesting purpose--of being able to import an
// object into a scope, like a WITH statement.
//
// 1. The new context created here winds up being managed.  So if no references
//    exist, GC is ok.  For instance, someone can write `use [x] [print "hi"]`
{
    INCLUDE_PARAMS_OF_USE;

    Value* vars = ARG(vars);
    Value* body = ARG(body);

    VarList* context = Virtual_Bind_Deep_To_New_Context(
        body,  // may be replaced with rebound copy, or left the same
        vars  // similar to the "spec" of a loop: WORD!/LIT-WORD!/BLOCK!
    );
    UNUSED(context);  // managed, but [1]

    if (Eval_Any_List_At_Throws(OUT, body, SPECIFIED))
        return THROWN;

    return OUT;
}


//
//  Try_Get_Binding_Of: C
//
bool Try_Get_Binding_Of(Sink(Value) out, const Value* v)
{
    switch (VAL_TYPE(v)) {
    case REB_WORD:
    case REB_META_WORD:
    case REB_THE_WORD: {
        if (IS_WORD_UNBOUND(v))
            return false;

        if (Is_Stub_Let(BINDING(v))) {  // temporary (LETs not exposed)
            Init_Word(out, Canon(LET));
            return true;
        }

        // Requesting the context of a word that is relatively bound may
        // result in that word having a FRAME! incarnated as a Stub (if
        // it was not already reified.)
        //
        VarList* c = VAL_WORD_CONTEXT(v);

        // If it's a FRAME! we want the phase to match the execution phase at
        // the current moment of execution.
        //
        if (CTX_TYPE(c) == REB_FRAME) {
            Level* L = Level_Of_Varlist_If_Running(c);
            if (L == nullptr)
                Copy_Cell(out, Varlist_Archetype(c));
            else
                Copy_Cell(out, L->rootvar);  // rootvar has phase, binding
        }
        else
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
            Tweak_Cell_Frame_Phase(out, Level_Phase(L));
            Tweak_Cell_Frame_Coupling(out, Level_Coupling(L));
        }
        else {
            // !!! Assume the canon FRAME! value in varlist[0] is useful?
            //
            assert(not Cell_Frame_Coupling(out));  // canon, no binding
        }
    }

    return true;
}


//
//  /refinement?: native:intrinsic [
//
//  "Test if an argument is a path with a leading blank"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(refinement_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Get_Word(arg));
}


//
//  /set-word?: native:intrinsic [
//
//  "Test if an argument is a chain with a word and trailing blank"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(set_word_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Set_Word(arg));
}


//
//  /set-run-word?: native:intrinsic [
//
//  "Test if argument is a path like /WORD: (for setting action variables)"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(set_run_word_q)
{
    UNUSED(phase);

    Init_Logic(
        out,
        Is_Path(arg)
        and Try_Get_Settable_Word_Symbol(nullptr, cast(Element*, arg))
    );
}


//
//  /get-word?: native:intrinsic [
//
//  "Test if an argument is a chain with a leading blank and a word"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(get_word_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Get_Word(arg));
}


//
//  /set-tuple?: native:intrinsic [
//
//  "Test if an argument is a chain with a tuple and trailing blank"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(set_tuple_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Set_Tuple(arg));
}


//
//  /get-tuple?: native:intrinsic [
//
//  "Test if an argument is a chain with a leading blank and a tuple"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(get_tuple_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Get_Tuple(arg));
}


//
//  /set-group?: native:intrinsic [
//
//  "Test if an argument is a chain with a group and trailing blank"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(set_group_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Set_Group(arg));
}


//
//  /get-group?: native:intrinsic [
//
//  "Test if an argument is a chain with a leading blank and a group"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(get_group_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Get_Group(arg));
}


//
//  /set-block?: native:intrinsic [
//
//  "Test if an argument is a chain with a block and trailing blank"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(set_block_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Set_Block(arg));
}


//
//  /get-block?: native:intrinsic [
//
//  "Test if an argument is a chain with a leading blank and a block"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(get_block_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Get_Block(arg));
}


//
//  /any-set-value?: native:intrinsic [
//
//  "Test if an argument is a 2-element chain with a trailing blank"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(any_set_value_q)
{
    UNUSED(phase);

    Init_Logic(out, Any_Set_Value(arg));
}


//
//  /any-get-value?: native:intrinsic [
//
//  "Test if an argument is a 2-element chain with a leading blank"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(any_get_value_q)
{
    UNUSED(phase);

    Init_Logic(out, Any_Get_Value(arg));
}


//
//  /quasi-word?: native:intrinsic [
//
//  "Test if an argument is an QUASI form of word"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(quasi_word_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Quasiform(arg) and HEART_BYTE(arg) == REB_WORD);
}


//
//  /char?: native:intrinsic [
//
//  "Test if an argument is an issue with one character"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(char_q)
{
    UNUSED(phase);

    Init_Logic(out, IS_CHAR(arg));
}


//
//  /lit-word?: native:intrinsic [
//
//  "Test if an argument is quoted word"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(lit_word_q)
{
    UNUSED(phase);

    Init_Logic(
        out,
        QUOTE_BYTE(arg) == ONEQUOTE_3 and HEART_BYTE(arg) == REB_WORD
    );
}


//
//  /lit-path?: native:intrinsic [
//
//  "Test if an argument is a quoted path"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(lit_path_q)
{
    UNUSED(phase);

    Init_Logic(out, IS_QUOTED_PATH(arg));
}


//
//  /any-inert?: native:intrinsic [
//
//  "Test if a value type always produces itself in the evaluator"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(any_inert_q)
{
    UNUSED(phase);

    Init_Logic(
        out,
        Not_Antiform(arg) and Any_Inert(arg)
    );
}


//
//  /unbind: native [
//
//  "Unbinds words from context"
//
//      return: [block! any-word? set-word?]
//      word [block! any-word? set-word?]
//          "A word or block (modified) (returned)"
//      :deep
//          "Process nested blocks"
//  ]
//
DECLARE_NATIVE(unbind)
{
    INCLUDE_PARAMS_OF_UNBIND;

    Value* word = ARG(word);

    if (Any_Word(word) or Is_Set_Word(word))
        Unbind_Any_Word(word);
    else {
        assert(Is_Block(word));

        const Element* tail;
        Element* at = Cell_List_At_Ensure_Mutable(&tail, word);
        Option(VarList*) context = nullptr;
        Unbind_Values_Core(at, tail, context, REF(deep));
    }

    return COPY(word);
}


//
//  /bindable: native [
//
//  "Remove Tip Binding of a Value"
//
//      return: [any-list? any-word?]
//      value [any-list? any-word?]
//  ]
//
DECLARE_NATIVE(bindable)
{
    INCLUDE_PARAMS_OF_BINDABLE;

    Value* v = ARG(value);

    if (Any_Word(v))
        Unbind_Any_Word(v);
    else {
        assert(Any_List(v));

        BINDING(v) = UNBOUND;
    }

    return COPY(v);
}


//
//  /resolve: native [
//
//  "Extract the inner variable target, e.g. (/a: -> a)"
//
//      return: [word! tuple!]
//      source [any-word? any-tuple? any-chain? path!]
//  ]
//
DECLARE_NATIVE(resolve)
{
    INCLUDE_PARAMS_OF_RESOLVE;

    Element* source = cast(Element*, ARG(source));

    if (Any_Word(source)) {
        HEART_BYTE(source) = REB_WORD;
        return COPY(source);
    }

    if (Any_Tuple(source)) {
        HEART_BYTE(source) = REB_TUPLE;
        return COPY(source);
    }

    if (Is_Path(source)) {  // !!! For now: (resolve '/a:) -> a
        SingleHeart single;
        if (not (single = maybe Try_Get_Sequence_Singleheart(source)))
            return FAIL(source);

        if (
            single == LEADING_BLANK_AND(WORD)  // /a
            or single == LEADING_BLANK_AND(TUPLE)  // /a.b.c or /.a
            or single == TRAILING_BLANK_AND(WORD)  // a/
            or single == TRAILING_BLANK_AND(TUPLE)  // a.b.c/ or .a/
        ){
            return COPY(Unpath(source));
        }
        if (
            single == LEADING_BLANK_AND(CHAIN)  // /a: or /a:b:c or /:a
            or single == TRAILING_BLANK_AND(CHAIN)  // a:/ or a:b:c/ or :a/
        ){
            Unpath(source);
            // fall through to chain decoding.
        }
        else
            return FAIL(source);
    }

    SingleHeart single = maybe Try_Get_Sequence_Singleheart(source);
    if (single == NOT_SINGLEHEART_0) {
        // fall through
    }
    else if (
        single == LEADING_BLANK_AND(WORD)  // a:
        or single == LEADING_BLANK_AND(TUPLE)  // a.b.c:
        or single == TRAILING_BLANK_AND(WORD)  // :a
        or single == TRAILING_BLANK_AND(TUPLE)  // :a.b.c
    ){
        return COPY(Unchain(source));
    }

    return FAIL(source);
}


//
//  /proxy-exports: native [
//
//  "Copy context by setting values in the target from those in the source"
//
//      return: "Same as the target module"
//          [module!]
//      where [<maybe> module!] "(modified)"
//      source [<maybe> module!]
//      exports "Which words to export from the source"
//          [<maybe> block!]
//  ]
//
DECLARE_NATIVE(proxy_exports)
//
// PROXY-EXPORTS is a renaming of what remains of the R3-Alpha concept of
// "RESOLVE" (a word that has been repurposed).  It was a function that was
// theoretically somewhat simple...that it would let you give a list of words
// that you wanted to transfer the keys of from one context to another.  In
// practice there are a lot of variant behaviors, regarding whether you want
// to add keys that don't exist yet or only update variables that are common
// between the two contexts.
//
// Historically this was offered for ANY-CONTEXT?.  But its only notable use
// was as the mechanism by which the IMPORT command would transfer the
// variables named by the `Exports:` block of a module to the module that was
// doing the importing.  Some of the most convoluted code dealt with managing
// the large growing indexes of modules as items were added.
//
// Ren-C's "Sea of Words" model means MODULE! leverages the existing hash table
// for global symbols.  The binding tables and complex mechanics are thus not
// necessary for that purpose.  So at time of writing, PROXY-EXPORTS has been
// pared back as what remains of "RESOLVE", and only works on MODULE!.
//
// Longer term it seems that PROXY-EXPORTS should be folded into a more
// traditional EXTEND primitive, perhaps with a /WORDS refinement to take a
// BLOCK! of words.
{
    INCLUDE_PARAMS_OF_PROXY_EXPORTS;

    SeaOfVars* where = cast(SeaOfVars*, Cell_Varlist(ARG(where)));
    SeaOfVars* source = cast(SeaOfVars*, Cell_Varlist(ARG(source)));

    const Element* tail;
    const Element* v = Cell_List_At(&tail, ARG(exports));
    for (; v != tail; ++v) {
        if (not Is_Word(v))
            return FAIL(ARG(exports));

        const Symbol* symbol = Cell_Word_Symbol(v);

        bool strict = true;

        const Value* src = MOD_VAR(source, symbol, strict);
        if (src == nullptr)
            return FAIL(v);  // fail if unset value, also?

        Value* dest = MOD_VAR(where, symbol, strict);
        if (dest != nullptr) {
            // Fail if found?
            Freshen_Cell(dest);
        }
        else {
            dest = Append_Context(where, symbol);
        }

        Copy_Cell(dest, src);
    }

    return COPY(ARG(where));
}


//
//  /infix?: native:intrinsic [
//
//  "non-null if a function that gets first argument before the call"
//
//      return: [logic?]
//      frame [<unrun> frame!]
//  ]
//
DECLARE_INTRINSIC(infix_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Cell_Infix(arg));
}


//
//  /infix: native [
//
//  "For functions that gets 1st argument from left, e.g (/+: infix get $add)"
//
//      return: "Antiform action"
//          [antiform?]  ; [action?] comes after INFIX in bootstrap
//      action [<unrun> frame!]
//      :off "Give back a non-infix version of the passed in function"
//      :defer "Allow one full expression on the left to evaluate"
//      :postpone "Allow arbitrary numbers of expressions on left to evaluate"
//  ]
//
DECLARE_NATIVE(infix)
{
    INCLUDE_PARAMS_OF_INFIX;

    Actionify(Copy_Cell(OUT, ARG(action)));

    if (REF(off)) {
        if (REF(defer) or REF(postpone))
            return FAIL(Error_Bad_Refines_Raw());
        Set_Cell_Infix_Mode(OUT, PREFIX_0);
    }
    else if (REF(defer)) {  // not OFF, already checked
        if (REF(postpone))
            return FAIL(Error_Bad_Refines_Raw());
        Set_Cell_Infix_Mode(OUT, INFIX_DEFER);
    }
    else if (REF(postpone)) {  // not OFF or DEFER, we checked
        Set_Cell_Infix_Mode(OUT, INFIX_POSTPONE);
    }
    else
        Set_Cell_Infix_Mode(OUT, INFIX_TIGHT);

    return OUT;
}


//
//  /identity: native [
//
//  "Returns input value (https://en.wikipedia.org/wiki/Identity_function)"
//
//      return: [any-value? pack?]
//      ^value [any-value? pack?]
//  ]
//
DECLARE_NATIVE(identity) // sample uses: https://stackoverflow.com/q/3136338
{
    INCLUDE_PARAMS_OF_IDENTITY;

    Element* meta = cast(Element*, ARG(value));

    return UNMETA(meta);
}


//
//  /free: native [
//
//  "Releases the underlying data of a value so it can no longer be accessed"
//
//      return: [~]
//      memory [<maybe> blank! any-series? any-context? handle!]
//  ]
//
DECLARE_NATIVE(free)
{
    INCLUDE_PARAMS_OF_FREE;

    Value* v = ARG(memory);
    if (Is_Blank(v))
        return NOTHING;

    if (Any_Context(v) or Is_Handle(v))
        return FAIL("FREE only implemented for ANY-SERIES? at the moment");

    if (Not_Node_Readable(Cell_Node1(v)))
        return FAIL("Cannot FREE already freed series");

    Flex* f = Cell_Flex_Ensure_Mutable(v);
    Decay_Flex(f);
    return NOTHING; // !!! Could return freed value
}


//
//  /free?: native [
//
//  "Tells if data has been released with FREE"
//
//      return: "Returns false if value wouldn't be FREEable (e.g. LOGIC!)"
//          [logic?]
//      value [any-value?]
//  ]
//
DECLARE_NATIVE(free_q)
//
// 1. Currently we don't have a "decayed" form of pairing...because Cells use
//    the NODE_FLAG_UNREADABLE for meaningfully unreadable cells, that have a
//    different purpose than canonizing references to a decayed form.
//
//    (We could use something like the CELL_FLAG_NOTE or other signal on
//    pairings to cue that references should be canonized to a single freed
//    pair instance, but this isn't a priority at the moment.)
{
    INCLUDE_PARAMS_OF_FREE_Q;

    Value* v = ARG(value);

    if (Is_Void(v) or Is_Nulled(v))
        return nullptr;

    if (not Cell_Has_Node1(v))  // freeable values have Flex in payload node1
        return nullptr;

    Node* n = Cell_Node1(v);
    if (n == nullptr or Is_Node_A_Cell(n))
        return nullptr;  // no decayed pairing form at this time [1]

    if (Is_Stub_Decayed(cast(Stub*, n)))
        return Init_Okay(OUT);  // decayed is as "free" as outstanding refs get

    return nullptr;
}


//
//  As_String_May_Fail: C
//
// Shared code from the refinement-bearing AS-TEXT and AS TEXT!.
//
bool Try_As_String(
    Sink(Value) out,
    Heart new_heart,
    const Value* v,
    REBLEN quotes,
    enum Reb_Strmode strmode
){
    assert(strmode == STRMODE_ALL_CODEPOINTS or strmode == STRMODE_NO_CR);

    if (Any_Word(v)) {  // ANY-WORD? can alias as a read only ANY-STRING?
        Init_Any_String(out, new_heart, Cell_Word_Symbol(v));
        Inherit_Const(Quotify(out, quotes), v);
    }
    else if (Is_Binary(v)) {  // If valid UTF-8, BINARY! aliases as ANY-STRING?
        const Binary* bin = Cell_Binary(v);
        Size byteoffset = VAL_INDEX(v);

        // The position in the binary must correspond to an actual
        // codepoint boundary.  UTF-8 continuation byte is any byte where
        // top two bits are 10.
        //
        // !!! Should this be checked before or after the valid UTF-8?
        // Checking before keeps from constraining input on errors, but
        // may be misleading by suggesting a valid "codepoint" was seen.
        //
        const Byte* at_ptr = Binary_At(bin, byteoffset);
        if (Is_Continuation_Byte(*at_ptr))
            fail ("Index at codepoint to convert binary to ANY-STRING?");

        const String* str;
        REBLEN index;
        if (
            not Is_Stub_String(bin)
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
            if (not Is_Flex_Frozen(bin))
                if (Get_Cell_Flag(v, CONST))
                    fail (Error_Alias_Constrains_Raw());

            bool all_ascii = true;
            REBLEN num_codepoints = 0;

            index = 0;

            Size bytes_left = Binary_Len(bin);
            const Byte* bp = Binary_Head(bin);
            for (; bytes_left > 0; --bytes_left, ++bp) {
                if (bp < at_ptr)
                    ++index;

                Codepoint c = *bp;
                if (c < 0x80)
                    Validate_Ascii_Byte(bp, strmode, Binary_Head(bin));
                else {
                    Option(Error*) e = Trap_Back_Scan_Utf8_Char(
                        &c, &bp, &bytes_left
                    );
                    if (e)
                        fail (unwrap e);

                    all_ascii = false;
                }

                ++num_codepoints;
            }
            FLAVOR_BYTE(m_cast(Binary*, bin)) = FLAVOR_NONSYMBOL;
            str = c_cast(String*, bin);

            Term_String_Len_Size(
                m_cast(String*, str),  // legal for tweaking cached data
                num_codepoints,
                Binary_Len(bin)
            );
            LINK(Bookmarks, m_cast(Binary*, bin)) = nullptr;

            // !!! TBD: cache index/offset

            UNUSED(all_ascii);  // TBD: maintain cache
        }
        else {
            // !!! It's a string series, but or mapping acceleration is
            // from index to offset... not offset to index.  Recalculate
            // the slow way for now.

            str = c_cast(String*, bin);
            index = 0;

            Utf8(const*) cp = String_Head(str);
            REBLEN len = String_Len(str);
            while (index < len and cp != at_ptr) {
                ++index;
                cp = Skip_Codepoint(cp);
            }
        }

        Init_Any_String_At(out, new_heart, str, index);
        Inherit_Const(Quotify(out, quotes), v);
    }
    else if (Is_Issue(v)) {
        if (Stringlike_Has_Node(v)) {
            assert(Is_Flex_Frozen(Cell_String(v)));
            goto any_string;  // ISSUE! series must be immutable
        }

        // If payload of an ISSUE! lives in the Cell itself, a read-only
        // Flex must be created for the data...because otherwise there isn't
        // room for an index (which ANY-STRING? needs).  For behavior parity
        // with if the payload *was* in the Cell, this alias must be frozen.

        REBLEN len;
        Size size;
        Utf8(const*) utf8 = Cell_Utf8_Len_Size_At(&len, &size, v);
        assert(size + 1 <= Size_Of(PAYLOAD(Bytes, v).at_least_8));  // must fit

        String* str = Make_String_Core(FLEX_MASK_MANAGED_STRING, size);
        memcpy(Flex_Data(str), utf8, size + 1);  // +1 to include '\0'
        Term_String_Len_Size(str, len, size);
        Freeze_Flex(str);
        Init_Any_String(out, new_heart, str);
    }
    else if (Any_String(v) or Is_Url(v)) {
      any_string:
        Copy_Cell(out, v);
        HEART_BYTE(out) = new_heart;
        Trust_Const(Quotify(out, quotes));
    }
    else
        return false;

    return true;
}


//
//  /as: native [
//
//  "Aliases underlying data of one value to act as another of same class"
//
//      return: [
//          ~null~ integer!
//          any-sequence? any-series? any-word? any-utf8?
//          frame!
//      ]
//      type [type-block!]
//      value [
//          <maybe>
//          integer!
//          any-sequence? any-series? any-word? any-utf8?
//          frame!
//      ]
//  ]
//
DECLARE_NATIVE(as)
//
// 1. Pairings are usually the same size as stubs...but not always.  If the
//    UNUSUAL_CELL_SIZE flag is set, then pairings will be in their own pool.
//    Were there a strong incentive to have separate code for that case,
//    we could reuse the node...but the case is not that strong.  It may be
//    that AS should not be willing to alias sequences since compressed
//    cases will force new allocations (e.g. aliasing a refinement has to
//    make a new array, since the symbol absolutely can't be mutated into
//    an array node).  Review.
{
    INCLUDE_PARAMS_OF_AS;

    Element* v = cast(Element*, ARG(value));

    Value* t = ARG(type);
    Kind new_kind = VAL_TYPE_KIND(t);
    if (new_kind >= REB_MAX_HEART)
        return FAIL("New kind can't be quoted/quasiform/antiform");

    Heart new_heart = cast(Heart, new_kind);
    if (new_heart == Cell_Heart_Ensure_Noquote(v))
        return COPY(v);

    if (Any_List_Kind(new_heart)) {

  //=//// CONVERSION TO ANY-ARRAY! ////////////////////////////////////////=//

        if (Any_Sequence(v)) {  // internals vary based on optimization
            if (not Sequence_Has_Node(v))
                return FAIL("Array Conversions of byte-oriented sequences TBD");

            const Node* node1 = Cell_Node1(v);
            if (Is_Node_A_Cell(node1)) {  // reusing node complicated [1]
                const Pairing* p = c_cast(Pairing*, node1);
                Context *binding = Cell_List_Binding(v);
                Source* a = Make_Source_Managed(2);
                Set_Flex_Len(a, 2);
                Derelativize(Array_At(a, 0), Pairing_First(p), binding);
                Derelativize(Array_At(a, 1), Pairing_Second(p), binding);
                Freeze_Source_Shallow(a);
                Init_Block(v, a);
            }
            else switch (Stub_Flavor(c_cast(Flex*, node1))) {
              case FLAVOR_SYMBOL: {
                Source* a = Make_Source_Managed(2);
                Set_Flex_Len(a, 2);
                if (Get_Cell_Flag(v, LEADING_BLANK)) {
                    Init_Blank(Array_At(a, 0));
                    Copy_Cell(Array_At(a, 1), v);
                    HEART_BYTE(Array_At(a, 1)) = REB_WORD;
                }
                else {
                    Copy_Cell(Array_At(a, 0), v);
                    HEART_BYTE(Array_At(a, 0)) = REB_WORD;
                    Init_Blank(Array_At(a, 1));
                }
                Freeze_Source_Shallow(a);
                Init_Block(v, a);
                break; }

              case FLAVOR_SOURCE: {
                const Source* a = Cell_Array(v);
                if (MIRROR_BYTE(a)) {  // .[a] or (xxx): compression
                    Source* two = Make_Source_Managed(2);
                    Set_Flex_Len(two, 2);
                    Cell* tweak;
                    if (Get_Cell_Flag(v, LEADING_BLANK)) {
                        Init_Blank(Array_At(two, 0));
                        tweak = Copy_Cell(Array_At(two, 1), v);
                    }
                    else {
                        tweak = Copy_Cell(Array_At(two, 0), v);
                        Init_Blank(Array_At(two, 1));
                    }
                    HEART_BYTE(tweak) = MIRROR_BYTE(a);
                    Clear_Cell_Flag(tweak, LEADING_BLANK);
                    Init_Block(v, two);
                }
                else {
                    assert(Is_Source_Frozen_Shallow(a));
                    HEART_BYTE(v) = REB_BLOCK;
                }
                break; }

              default:
                assert(false);
            }
        }
        else if (not Any_List(v))
            goto bad_cast;

        goto adjust_v_heart;
    }
    else if (Any_Sequence_Kind(new_heart)) {

  //=//// CONVERSION TO ANY-SEQUENCE! /////////////////////////////////////=//

        if (Any_List(v)) {
            //
            // Even if we optimize the array, we don't want to give the
            // impression that we would not have frozen it.
            //
            if (not Is_Source_Frozen_Shallow(Cell_Array(v)))
                Freeze_Source_Shallow(Cell_Array_Ensure_Mutable(v));

            DECLARE_ELEMENT (temp);  // need to rebind
            Option(Error*) error = Trap_Init_Any_Sequence_At_Listlike(
                temp,
                new_heart,
                Cell_Array(v),
                VAL_INDEX(v)
            );
            if (error)
                return FAIL(unwrap error);

            /* BINDING(temp) = BINDING(v); */  // may be unfit after compress
            Derelativize(OUT, temp, BINDING(v));  // try this instead (?)

            return OUT;
        }

        if (Any_Sequence(v)) {
            Copy_Cell(OUT, v);
            HEART_BYTE(OUT) = new_heart;
            return Trust_Const(OUT);
        }

        goto bad_cast;
    }
    else if (Any_Word_Kind(new_heart)) {

  //=//// CONVERSION TO ANY-WORD! /////////////////////////////////////////=//

        if (Is_Issue(v)) {
            if (Stringlike_Has_Node(v)) {
                //
                // Handle the same way we'd handle any other read-only TEXT!
                // with a String allocation...e.g. reuse it if it's already
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
            Option(Error*) error = Trap_Transcode_One(OUT, new_heart, v);
            if (error)
                return (RAISE(unwrap error));
            return OUT;
          }
        }

        if (Any_String(v)) {  // aliasing data as an ANY-WORD? freezes data
          any_string: {
            const String* s = Cell_String(v);

            if (not Is_Flex_Frozen(s)) {
                //
                // We always force strings used with AS to frozen, so that the
                // effect of freezing doesn't appear to mystically happen just
                // in those cases where the efficient reuse works out.

                if (Get_Cell_Flag(v, CONST))
                    return FAIL(Error_Alias_Constrains_Raw());

                Freeze_Flex(Cell_Flex(v));
            }

            if (VAL_INDEX(v) != 0)  // can't reuse non-head series AS WORD!
                goto intern_utf8;

            if (Is_String_Symbol(s)) {
                //
                // This string's content was already frozen and checked, e.g.
                // the string came from something like `as text! 'some-word`
            }
            else {
                // !!! If this spelling is already interned we'd like to
                // reuse the existing Symbol, and if not we'd like to promote
                // this String to be the interned one.  This efficiency has
                // not yet been implemented, so we just intern it.
                //
                goto intern_utf8;
            }

            Init_Any_Word(OUT, new_heart, c_cast(Symbol*, s));
            return Inherit_Const(stable_OUT, v);
          }
        }

        if (Is_Binary(v)) {
            if (VAL_INDEX(v) != 0)  // ANY-WORD? stores binding, not position
                return FAIL("Can't alias BINARY! as WORD! unless at head");

            // We have to permanently freeze the underlying String from any
            // mutation to use it in a WORD! (and also, may add STRING flag);
            //
            const Binary* b = Cell_Binary(v);
            if (not Is_Flex_Frozen(b))
                if (Get_Cell_Flag(v, CONST))  // can't freeze or add IS_STRING
                    return FAIL(Error_Alias_Constrains_Raw());

            const String* str;
            if (Is_Stub_String(b))
                str = c_cast(String*, b);
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
                Size size;
                const Byte* data = Cell_Binary_Size_At(&size, v);
                str = Intern_UTF8_Managed(data, size);

                // Constrain the input in the way it would be if we were doing
                // the more efficient reuse.
                //
                FLAVOR_BYTE(m_cast(Binary*, b)) = FLAVOR_NONSYMBOL;
                Freeze_Flex(b);
            }

            Init_Any_Word(OUT, new_heart, c_cast(Symbol*, str));
            return Inherit_Const(OUT, v);
        }

        if (not Any_Word(v))
            goto bad_cast;

        goto adjust_v_heart;
    }
    else switch (new_heart) {
      case REB_INTEGER: {
        if (not IS_CHAR(v))
            return FAIL("AS INTEGER! only supports what-were-CHAR! issues ATM");
        return Init_Integer(OUT, Cell_Codepoint(v)); }

      case REB_URL:
      case REB_EMAIL:
      case REB_ISSUE: {
        if (Is_Integer(v)) {
            Option(Error*) error = Trap_Init_Char(OUT, VAL_UINT32(v));
            if (error)
                return RAISE(unwrap error);
            return OUT;
        }

        if (Any_String(v)) {
            REBLEN len;
            Size utf8_size = Cell_String_Size_Limit_At(&len, v, UNLIMITED);

            if (utf8_size + 1 <= Size_Of(PAYLOAD(Bytes, v).at_least_8)) {
                //
                // Payload can fit in a single issue cell.
                //
                Reset_Cell_Header_Untracked(
                    TRACK(OUT),
                    FLAG_HEART_BYTE(new_heart) | CELL_MASK_NO_NODES
                );
                memcpy(
                    PAYLOAD(Bytes, OUT).at_least_8,
                    Cell_String_At(v),
                    utf8_size + 1  // copy the '\0' terminator
                );
                EXTRA(Bytes, OUT).at_least_4[IDX_EXTRA_USED] = utf8_size;
                EXTRA(Bytes, OUT).at_least_4[IDX_EXTRA_LEN] = len;
            }
            else {
                if (not Try_As_String(
                    OUT,
                    REB_TEXT,
                    v,
                    0,  // no quotes
                    STRMODE_ALL_CODEPOINTS  // See AS-TEXT:STRICT for stricter
                )){
                    goto bad_cast;
                }
                Freeze_Flex(Cell_Flex(OUT));  // must be frozen
            }
            HEART_BYTE(OUT) = new_heart;
            return OUT;
        }

        goto bad_cast; }

      case REB_TEXT:
      case REB_TAG:
      case REB_FILE:
        if (not Try_As_String(
            OUT,
            new_heart,
            v,
            0,  // no quotes
            STRMODE_ALL_CODEPOINTS  // See AS-TEXT:STRICT for stricter
        )){
            goto bad_cast;
        }
        return OUT;

      case REB_BINARY: {
        if (Is_Issue(v)) {
            if (Stringlike_Has_Node(v))
                goto any_string_as_binary;  // had a String allocation

            // Data lives in Cell--make new frozen String for BINARY!

            Size size;
            Utf8(const*) utf8 = Cell_Utf8_Size_At(&size, v);
            Binary* b = Make_Binary_Core(NODE_FLAG_MANAGED, size);
            memcpy(Binary_Head(b), utf8, size + 1);
            Set_Flex_Used(b, size);
            Freeze_Flex(b);
            Init_Blob(OUT, b);
            return Inherit_Const(stable_OUT, v);
        }

        if (Any_Word(v) or Any_String(v)) {
          any_string_as_binary:
            Init_Blob_At(
                OUT,
                Cell_String(v),
                Any_Word(v) ? 0 : VAL_BYTEOFFSET(v)
            );
            return Inherit_Const(stable_OUT, v);
        }

        return FAIL(v); }

    case REB_FRAME: {
      if (Is_Frame(v)) {
        //
        // We want AS ACTION! AS FRAME! of an action to be basically a no-op.
        // So that means that it uses the dispatcher and details it encoded
        // in the phase.  This means COPY of a FRAME! needs to create a new
        // action identity at that moment.  There is no Make_Action() here,
        // because all frame references to this frame are the same action.
        //
        assert(ACT_EXEMPLAR(VAL_FRAME_PHASE(v)) == Cell_Varlist(v));
        Set_Flavor_Flag(VARLIST, Cell_Varlist(v), IMMUTABLE);
        return Init_Frame_Details(
            OUT,
            VAL_FRAME_PHASE(v),
            ANONYMOUS,  // see note, we might have stored this in varlist slot
            Cell_Frame_Coupling(v)
        );
      }

      return FAIL(v); }

      default:  // all applicable types should be handled above
        break;
    }

  bad_cast:
    return FAIL(Error_Bad_Cast_Raw(v, ARG(type)));

  adjust_v_heart:
    //
    // Fallthrough for cases where changing the type byte and potentially
    // updating the quotes is enough.
    //
    Copy_Cell(OUT, v);
    HEART_BYTE(OUT) = new_heart;
    return Trust_Const(OUT);
}


//
//  /as-text: native [
//
//  "AS TEXT! variant that may disallow CR LF sequences in BINARY! alias"
//
//      return: [~null~ text!]
//      value [<maybe> any-value?]
//      :strict "Don't allow CR LF sequences in the alias"
//  ]
//
DECLARE_NATIVE(as_text)
{
    INCLUDE_PARAMS_OF_AS_TEXT;

    Value* v = ARG(value);
    Dequotify(v);  // number of incoming quotes not relevant
    if (not Any_Series(v) and not Any_Word(v) and not Any_Path(v))
        return FAIL(PARAM(value));

    const REBLEN quotes = 0;  // constant folding (see AS behavior)

    Kind new_kind = REB_TEXT;
    if (new_kind == VAL_TYPE(v) and not REF(strict))
        return COPY(Quotify(v, quotes));  // just may change quotes

    if (not Try_As_String(
        OUT,
        REB_TEXT,
        v,
        quotes,
        REF(strict) ? STRMODE_NO_CR : STRMODE_ALL_CODEPOINTS
    )){
        return FAIL(Error_Bad_Cast_Raw(v, Datatype_From_Kind(REB_TEXT)));
    }

    return OUT;
}


//
//  /aliases?: native [
//
//  "Return whether or not the underlying data of one value aliases another"
//
//      return: [logic?]
//      value1 [any-series?]
//      value2 [any-series?]
//  ]
//
DECLARE_NATIVE(aliases_q)
{
    INCLUDE_PARAMS_OF_ALIASES_Q;

    return Init_Logic(OUT, Cell_Flex(ARG(value1)) == Cell_Flex(ARG(value2)));
}


//
//  /any-value?: native:intrinsic [
//
//  "Tells you if the argument (taken as meta) is storable in a variable"
//
//      return: [logic?]
//      ^value
//  ]
//
DECLARE_INTRINSIC(any_value_q)
{
    UNUSED(phase);

    if (not Is_Quasiform(arg))  // meta
        Init_Logic(out, true);
    else
        Init_Logic(out, Is_Stable_Antiform_Heart(Cell_Heart(arg)));
}


//
//  /element?: native:intrinsic [
//
//  "Tells you if the argument is storable in a list"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(element_q)
{
    UNUSED(phase);

    Init_Logic(out, Not_Antiform(arg));
}


//
//  /non-void-value?: native:intrinsic [
//
//  "If the argument (taken as meta) non void, and storable in a variable"
//
//      return: [logic?]
//      ^value
//  ]
//
DECLARE_INTRINSIC(non_void_value_q)
//
// Being able to specify that a function does not accept voids on its type
// checking is fundamentally different from taking ANY-VALUE? and then failing
// if a void is received.  Functions like REDUCE test for if predicates will
// accept voids, and only pass them if they do.  So a function like REIFY
// needs to use NON-VOID-VALUE? in its type spec to work with REDUCE.
{
    UNUSED(phase);

    if (not Is_Quasiform(arg)) {
        if (Is_Meta_Of_Void(arg))
            Init_Logic(out, false);
        else
            Init_Logic(out, true);
    }
    else
        Init_Logic(out, Is_Stable_Antiform_Heart(Cell_Heart(arg)));
}


//
//  /any-atom?: native:intrinsic [
//
//  "Accepts absolutely any argument state (unstable antiforms included)"
//
//      return: [logic?]
//      ^value
//  ]
//
DECLARE_INTRINSIC(any_atom_q)
{
    UNUSED(phase);
    UNUSED(arg);

    Init_Logic(out, true);
}



//
//  /nihil?: native:intrinsic [
//
//  "Tells you if argument is an ~[]~ antiform, e.g. an empty pack"
//
//      return: [logic?]
//      ^atom
//  ]
//
DECLARE_INTRINSIC(nihil_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Meta_Of_Nihil(arg));
}


//
//  /barrier?: native:intrinsic [
//
//  "Tells you if argument is a comma antiform (unstable)"
//
//      return: [logic?]
//      ^atom
//  ]
//
DECLARE_INTRINSIC(barrier_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Meta_Of_Barrier(arg));
}


//
//  /elision?: native:intrinsic [
//
//  "If argument is either nihil or a barrier (empty pack or antiform comma)"
//
//      return: [logic?]
//      ^atom
//  ]
//
DECLARE_INTRINSIC(elision_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Meta_Of_Elision(arg));
}


//
//  /void?: native:intrinsic [
//
//  "Tells you if argument is void"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(void_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Void(arg));
}


//
//  /nothing?: native:intrinsic [
//
//  "Tells you if argument is the state used to indicate an unset variable"
//
//      return: [logic?]
//      value "Tested to see if it is antiform blank"
//          [any-value?]
//  ]
//
DECLARE_INTRINSIC(nothing_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Nothing(arg));
}


//
//  /tripwire?: native:intrinsic [
//
//  "Tells you if argument is a named variant of nothing (acts like unset)"
//
//      return: [logic?]
//      value "Tested to see if it is antiform tag"
//          [any-value?]
//  ]
//
DECLARE_INTRINSIC(tripwire_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Tripwire(arg));
}


//
//  /trash?: native:intrinsic [
//
//  "Tells you if argument is a quasiform blank (~), most routines don't take"
//
//      return: [logic?]
//      value "Tested to see if it is quasiform blank"
//          [any-value?]
//  ]
//
DECLARE_INTRINSIC(trash_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Trash(arg));
}


//
//  /space?: native:intrinsic [
//
//  "Tells you if argument is a space character (#)"
//
//      return: [logic?]
//      value [any-value?]
//  ]
//
DECLARE_INTRINSIC(space_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Space(arg));
}


//
//  /heavy: native [
//
//  "Make the heavy form of NULL or VOID (passes through all other values)"
//
//      return: [any-value? pack?]
//      ^atom [any-value? pack?]
//  ]
//
DECLARE_NATIVE(heavy) {
    INCLUDE_PARAMS_OF_HEAVY;

    Element* meta = cast(Element*, ARG(atom));

    if (Is_Meta_Of_Void(meta))
        return Init_Heavy_Void(OUT);

    if (Is_Meta_Of_Null(meta))
        return Init_Heavy_Null(OUT);

    return UNMETA(meta);
}


//
//  /light: native [
//
//  "Make the light form of NULL or VOID (passes through all other values)"
//
//      return: [any-value? pack?]
//      ^atom [any-value? pack?]
//  ]
//
DECLARE_NATIVE(light) {
    INCLUDE_PARAMS_OF_LIGHT;

    Element* meta = cast(Element*, ARG(atom));

    if (not Is_Meta_Of_Pack(meta))
        return UNMETA(meta);

    Length len;
    const Cell* first = Cell_List_Len_At(&len, meta);

    if (len != 1)
        return UNMETA(meta);

    if (Is_Meta_Of_Void(first))
        return VOID;

    if (Is_Meta_Of_Null(first))
        return nullptr;

    return UNMETA(meta);
}


//
//  /decay: native:intrinsic [
//
//  "Handle unstable isotopes like assignments do, pass through other values"
//
//      return: [any-value?]
//      atom
//  ]
//
DECLARE_INTRINSIC(decay)
//
// 1. We take the argument as a plain (non-^META) parameter in order to make
//    the decay process happen in the parameter fulfillment, because an idea
//    with intrinsics is that they do not raise errors.  If we called
//    Meta_Unquotify_Decayed() in the body of this intrinsic, that would
//    break the contract in the case of an error.  So we let the parameter
//    fulfillment cause the problem.
{
    UNUSED(phase);

    Assert_Cell_Stable(arg);  // Value* should always be stable
    Copy_Cell(out, arg);  // pre-decayed by non-^META argument [1]
}


//
//  /reify: native:intrinsic [
//
//  "Make antiforms into their quasiforms, quote all other values"
//
//      return: [element?]
//      value [any-value?]
//  ]
//
DECLARE_INTRINSIC(reify)
//
// There isn't a /NOQUASI refinement to REIFY so it can be an intrinsic.  This
// speeds up all REIFY operations, and (noquasi reify ...) will be faster
// than (reify/noquasi ...)
//
// !!! We don't handle unstable isotopes here, so REIFY of a pack will just
// be a reification of the first value in the pack.  And REIFY of an raised
// error will error.  We could have REIFY/EXCEPT and REIFY/PACK, if they
// seem to be important...but let's see if we can get away without them and
// have this be an intrinsic.
{
    UNUSED(phase);

    Reify(Copy_Cell(out, arg));
}


//
//  /noquasi: native:intrinsic [
//
//  "Make quasiforms into their plain forms, pass through all other elements"
//
//      return: [element?]
//      value [element?]
//  ]
//
DECLARE_INTRINSIC(noquasi)
{
    UNUSED(phase);

    Copy_Cell(out, arg);
    if (Is_Quasiform(out))
        QUOTE_BYTE(out) = NOQUOTE_1;
}


//
//  /degrade: native [
//
//  "Make quasiforms into their antiforms, pass thru other values"
//
//      return: [any-value?]
//      value [any-value?]  ; should input be enforced as ELEMENT?
//  ]
//
DECLARE_NATIVE(degrade)
{
    INCLUDE_PARAMS_OF_DEGRADE;

    Value* v = ARG(value);
    return Degrade(Copy_Cell(OUT, v));
}


//
//  /noantiform: native:intrinsic [
//
//  "Turn antiforms into their plain forms, pass thru other values"
//
//      return: [element?]
//      value
//  ]
//
DECLARE_INTRINSIC(noantiform)
{
    UNUSED(phase);

    Copy_Cell(out, arg);

    if (Is_Antiform(out))
        QUOTE_BYTE(out) = NOQUOTE_1;
}
