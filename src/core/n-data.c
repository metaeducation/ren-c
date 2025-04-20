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
//  bind: native [
//
//  "Binds words or words in lists to the specified context"
//
//      return: [frame! action! any-list? any-path? any-word? quoted!]
//      spec "Target context or a word whose binding should be the target"
//          [block! the-word? any-context?]
//      value "Value whose bound form is to be returned"
//          [any-list? any-path? any-word? quoted!]
//  ]
//
DECLARE_NATIVE(BIND)
//
// !!! The "BIND dialect" is just being mapped out.  Right now, it accepts
// a context, or a THE-WORD!, or a block of THE-WORD!s.
{
    INCLUDE_PARAMS_OF_BIND;

    Value* v = ARG(VALUE);
    Element* spec = Element_ARG(SPEC);

    if (Is_Block(spec)) {
        const Element* tail;
        const Element* at = Cell_List_At(&tail, spec);

        if (not Listlike_Cell(v))  // QUOTED? could have wrapped any type
            return FAIL(Error_Invalid_Arg(level_, PARAM(VALUE)));

        for (; at != tail; ++at) {
            if (not Is_The_Word(at))
                return FAIL("BLOCK! binds all @word for the moment");

            Use* use = Alloc_Use_Inherits(Cell_Binding(v));
            Derelativize(Stub_Cell(use), at, Cell_Binding(spec));
            HEART_BYTE(Stub_Cell(use)) = TYPE_WORD;

            if (not IS_WORD_BOUND(Stub_Cell(use)))
                return FAIL(Error_Not_Bound_Raw(Stub_Cell(use)));

            Tweak_Cell_Binding(v, use);
        }

        return COPY(v);
    }

    const Element* context;

    if (Any_Context(spec)) {
        //
        // Get target from an OBJECT!, ERROR!, PORT!, MODULE!, FRAME!
        //
        context = spec;
    }
    else {
        assert(Is_The_Word(spec));
        if (not IS_WORD_BOUND(spec))
            return FAIL(Error_Not_Bound_Raw(spec));

        if (not Listlike_Cell(v))  // QUOTED? could have wrapped any type
            return FAIL(Error_Invalid_Arg(level_, PARAM(VALUE)));

        Use* use = Alloc_Use_Inherits(Cell_Binding(v));
        Copy_Cell(Stub_Cell(use), spec);
        HEART_BYTE(Stub_Cell(use)) = TYPE_WORD;

        Tweak_Cell_Binding(v, use);

        return COPY(v);
    }

    if (Wordlike_Cell(v)) {
        //
        // Bind a single word (also works on refinements, `/a` ...or `a.`, etc.

        if (Try_Bind_Word(context, v))
            return COPY(v);

        return FAIL(Error_Not_In_Context_Raw(v));
    }

    if (not Listlike_Cell(v))  // QUOTED? could have wrapped any type
        return FAIL(Error_Invalid_Arg(level_, PARAM(VALUE)));

    Use* use = Alloc_Use_Inherits(Cell_Binding(v));
    Copy_Cell(Stub_Cell(use), context);
    Tweak_Cell_Binding(v, use);

    return COPY(v);
}


//
//  binding-of: native:generic [
//
//  "Get the binding of a value (binding is a loooong work in progress...)"
//
//      return: [~null~ any-context? 'let]
//      element [<maybe> fundamental?]
//  ]
//
DECLARE_NATIVE(BINDING_OF)
{
    INCLUDE_PARAMS_OF_BINDING_OF;

    return Dispatch_Generic(BINDING_OF, Element_ARG(ELEMENT), LEVEL);
}


//
//  inside: native [
//
//  "Returns a view of the input bound virtually to the context"
//
//      return: [~null~ any-value?]
//      where [any-context? any-list? any-sequence?]
//      element [<maybe> element?]  ; QUOTED? support?
//  ]
//
DECLARE_NATIVE(INSIDE)
{
    INCLUDE_PARAMS_OF_INSIDE;

    Element* element = Element_ARG(ELEMENT);
    Value* where = ARG(WHERE);

    Context* context;
    if (Any_Context(where))
        context = Cell_Context(where);
    else if (Any_List(where))
        context = Cell_Binding(where);
    else {
        assert(Any_Sequence(where));
        context = Cell_Sequence_Binding(where);
    }

    Derelativize(OUT, element, context);
    return OUT;
}


//
//  overbind: native [
//
//  "Add definitions from context to environment of value"
//
//      return: [~null~ any-value?]
//      definitions [word! any-context?]
//      value [<maybe> any-list?]  ; QUOTED? support?
//  ]
//
DECLARE_NATIVE(OVERBIND)
{
    INCLUDE_PARAMS_OF_OVERBIND;

    Element* v = Element_ARG(VALUE);
    Element* defs = Element_ARG(DEFINITIONS);

    if (Is_Word(defs)) {
        if (IS_WORD_UNBOUND(defs))
            return FAIL(Error_Not_Bound_Raw(defs));
    }
    else
        assert(Any_Context(defs));

    Use* use = Alloc_Use_Inherits(Cell_List_Binding(v));
    Copy_Cell(Stub_Cell(use), defs);

    Tweak_Cell_Binding(v, use);

    return COPY(v);
}


//
//  has: native [
//
//  "Returns a word bound into the context, if it's available, else null"
//
//      return: [~null~ any-word?]
//      context [any-context?]
//      value [<maybe> any-word?]  ; QUOTED? support?
//  ]
//
DECLARE_NATIVE(HAS)
{
    INCLUDE_PARAMS_OF_HAS;

    Value* v = ARG(VALUE);

    assert(Any_Word(v));
    Heart heart = Heart_Of_Builtin(v);

    const Symbol* symbol = Cell_Word_Symbol(v);
    const bool strict = true;
    Option(Index) index = Find_Symbol_In_Context(ARG(CONTEXT), symbol, strict);
    if (not index)
        return nullptr;

    if (not Is_Module(ARG(CONTEXT))) {
        VarList* varlist = Cell_Varlist(ARG(CONTEXT));
        return Init_Any_Word_Bound(OUT, heart, symbol, varlist, unwrap index);
    }

    SeaOfVars* sea = Cell_Module_Sea(ARG(CONTEXT));
    Init_Any_Word(OUT, heart, symbol);
    Tweak_Cell_Word_Index(OUT, INDEX_PATCHED);
    Tweak_Cell_Binding(OUT, Sea_Patch(sea, symbol, strict));
    return OUT;
}


//
//  without: native [
//
//  "Remove a virtual binding from a value"
//
//      return: [~null~ any-word? any-list?]
//      context "If integer, then removes that number of virtual bindings"
//          [integer! any-context?]
//      value [<const> <maybe> any-word? any-list?]  ; QUOTED? support?
//  ]
//
DECLARE_NATIVE(WITHOUT)
{
    INCLUDE_PARAMS_OF_WITHOUT;

    VarList* ctx = Cell_Varlist(ARG(CONTEXT));
    Value* v = ARG(VALUE);

    // !!! Note that BIND of a WORD! in historical Rebol/Red would return the
    // input word as-is if the word wasn't in the requested context, while
    // IN would return trash on failure.  We carry forward the NULL-failing
    // here in IN, but BIND's behavior on words may need revisiting.
    //
    if (Any_Word(v)) {
        const Symbol* symbol = Cell_Word_Symbol(v);
        const bool strict = true;
        Option(Index) index = Find_Symbol_In_Context(
            ARG(CONTEXT), symbol, strict
        );
        if (not index)
            return nullptr;
        return Init_Any_Word_Bound(
            OUT,
            Heart_Of_Builtin_Fundamental(v),
            symbol,  // !!! incoming case...consider impact of strict if false?
            ctx,
            unwrap index
        );
    }

    Use* use = Alloc_Use_Inherits(Cell_List_Binding(v));
    Copy_Cell(Stub_Cell(use), Varlist_Archetype(ctx));

    Tweak_Cell_Binding(v, use);

    return COPY(v);
}


//
//  use: native [
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
DECLARE_NATIVE(USE)
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

    Element* vars = Element_ARG(VARS);
    Element* body = Element_ARG(BODY);

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
    switch (Type_Of(v)) {
    case TYPE_WORD:
    case TYPE_META_WORD:
    case TYPE_THE_WORD: {
        if (IS_WORD_UNBOUND(v))
            return false;

        if (Is_Stub_Let(Cell_Binding(v))) {  // temporary (LETs not exposed)
            Init_Word(out, CANON(LET));
            return true;
        }

        // Requesting the context of a word that is relatively bound may
        // result in that word having a FRAME! incarnated as a Stub (if
        // it was not already reified.)
        //
        Context* c = VAL_WORD_CONTEXT(v);

        // If it's a FRAME! we want the phase to match the execution phase at
        // the current moment of execution.
        //
        if (CTX_TYPE(c) == TYPE_FRAME) {
            Level* L = Level_Of_Varlist_If_Running(cast(VarList*, c));
            if (L == nullptr)
                Init_Frame(out, cast(ParamList*, c), ANONYMOUS, NONMETHOD);
            else {
                Phase* lens = Level_Phase(L);
                Init_Lensed_Frame(
                    out,
                    Varlist_Of_Level_Force_Managed(L),
                    lens,
                    Level_Coupling(L)
                );
            }
        }
        else if (CTX_TYPE(c) == TYPE_MODULE)
            Init_Module(out, cast(SeaOfVars*, c));
        else
            Copy_Cell(out, Varlist_Archetype(cast(VarList*, c)));
        break; }

      default:
        //
        // Will OBJECT!s or FRAME!s have "contexts"?  Or if they are passed
        // in should they be passed trough as "the context"?  For now, keep
        // things clear?
        //
        assert(false);
    }

    return true;
}


//
//  refinement?: native:intrinsic [
//
//  "Test if an argument is a chain with a leading blank"
//
//      return: [logic?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(REFINEMENT_Q)
{
    INCLUDE_PARAMS_OF_REFINEMENT_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(Is_Get_Word(e));
}


//
//  set-word?: native:intrinsic [
//
//  "Test if an argument is a chain with a word and trailing blank"
//
//      return: [logic?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(SET_WORD_Q)
{
    INCLUDE_PARAMS_OF_SET_WORD_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(Is_Set_Word(e));
}


//
//  set-run-word?: native:intrinsic [
//
//  "Test if argument is a path like /WORD: (for setting action variables)"
//
//      return: [logic?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(SET_RUN_WORD_Q)
{
    INCLUDE_PARAMS_OF_SET_RUN_WORD_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(Is_Path(e) and Try_Get_Settable_Word_Symbol(nullptr, e));
}


//
//  run-word?: native:intrinsic [
//
//  "Test if argument is a path like /WORD"
//
//      return: [logic?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(RUN_WORD_Q)
{
    INCLUDE_PARAMS_OF_SET_RUN_WORD_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    if (not Is_Path(e))
        return nullptr;

    Option(SingleHeart) single = Try_Get_Sequence_Singleheart(e);
    return LOGIC(single == LEADING_BLANK_AND(WORD));
}


//
//  get-word?: native:intrinsic [
//
//  "Test if an argument is a chain with a leading blank and a word"
//
//      return: [logic?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(GET_WORD_Q)
{
    INCLUDE_PARAMS_OF_GET_WORD_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(Is_Get_Word(e));
}


//
//  set-tuple?: native:intrinsic [
//
//  "Test if an argument is a chain with a tuple and trailing blank"
//
//      return: [logic?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(SET_TUPLE_Q)
{
    INCLUDE_PARAMS_OF_SET_TUPLE_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(Is_Set_Tuple(e));
}


//
//  get-tuple?: native:intrinsic [
//
//  "Test if an argument is a chain with a leading blank and a tuple"
//
//      return: [logic?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(GET_TUPLE_Q)
{
    INCLUDE_PARAMS_OF_GET_TUPLE_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(Is_Get_Tuple(e));
}


//
//  set-group?: native:intrinsic [
//
//  "Test if an argument is a chain with a group and trailing blank"
//
//      return: [logic?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(SET_GROUP_Q)
{
    INCLUDE_PARAMS_OF_SET_GROUP_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(Is_Set_Group(e));
}


//
//  get-group?: native:intrinsic [
//
//  "Test if an argument is a chain with a leading blank and a group"
//
//      return: [logic?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(GET_GROUP_Q)
{
    INCLUDE_PARAMS_OF_GET_GROUP_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(Is_Get_Group(e));
}


//
//  set-block?: native:intrinsic [
//
//  "Test if an argument is a chain with a block and trailing blank"
//
//      return: [logic?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(SET_BLOCK_Q)
{
    INCLUDE_PARAMS_OF_SET_BLOCK_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(Is_Set_Block(e));
}


//
//  get-block?: native:intrinsic [
//
//  "Test if an argument is a chain with a leading blank and a block"
//
//      return: [logic?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(GET_BLOCK_Q)
{
    INCLUDE_PARAMS_OF_GET_BLOCK_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(Is_Get_Block(e));
}


//
//  any-set-value?: native:intrinsic [
//
//  "Test if an argument is a 2-element chain with a trailing blank"
//
//      return: [logic?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(ANY_SET_VALUE_Q)
{
    INCLUDE_PARAMS_OF_ANY_SET_VALUE_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(Any_Set_Value(e));
}


//
//  any-get-value?: native:intrinsic [
//
//  "Test if an argument is a 2-element chain with a leading blank"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_NATIVE(ANY_GET_VALUE_Q)
{
    INCLUDE_PARAMS_OF_ANY_GET_VALUE_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(Any_Get_Value(e));
}


//
//  quasi-word?: native:intrinsic [
//
//  "Test if an argument is an QUASI form of word"
//
//      return: [logic?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(QUASI_WORD_Q)
{
    INCLUDE_PARAMS_OF_QUASI_WORD_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(Is_Quasiform(e) and HEART_BYTE(e) == TYPE_WORD);
}


//
//  char?: native:intrinsic [
//
//  "Test if an argument is an issue with one codepoint (or #{00} NUL blob)"
//
//      return: [logic?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(CHAR_Q)
{
    INCLUDE_PARAMS_OF_CHAR_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(IS_CHAR(e));
}


//
//  lit-word?: native:intrinsic [
//
//  "Test if an argument is quoted word"
//
//      return: [logic?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(LIT_WORD_Q)
{
    INCLUDE_PARAMS_OF_LIT_WORD_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(
        QUOTE_BYTE(e) == ONEQUOTE_NONQUASI_3 and HEART_BYTE(e) == TYPE_WORD
    );
}


//
//  lit-path?: native:intrinsic [
//
//  "Test if an argument is a quoted path"
//
//      return: [logic?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(LIT_PATH_Q)
{
    INCLUDE_PARAMS_OF_LIT_PATH_Q;

    DECLARE_ELEMENT (elem);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(elem, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(
        Heart_Of(elem) == TYPE_PATH and Quotes_Of(elem) == 1
    );
}


//
//  any-inert?: native:intrinsic [
//
//  "Test if a value type always produces itself in the evaluator"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_NATIVE(ANY_INERT_Q)
{
    INCLUDE_PARAMS_OF_ANY_INERT_Q;

    DECLARE_VALUE (v);
    Option(Bounce) bounce = Trap_Bounce_Decay_Value_Intrinsic(v, LEVEL);
    if (bounce)
        return unwrap bounce;

    return LOGIC(Not_Antiform(v) and Any_Inert(v));
}


//
//  unbind: native [
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
DECLARE_NATIVE(UNBIND)
{
    INCLUDE_PARAMS_OF_UNBIND;

    Value* word = ARG(WORD);

    if (Any_Word(word) or Is_Set_Word(word))
        Unbind_Any_Word(word);
    else {
        assert(Is_Block(word));

        const Element* tail;
        Element* at = Cell_List_At_Ensure_Mutable(&tail, word);
        Option(VarList*) context = nullptr;
        Unbind_Values_Core(at, tail, context, Bool_ARG(DEEP));
    }

    return COPY(word);
}


//
//  bindable: native [
//
//  "Remove Tip Binding of a Value"
//
//      return: [any-list? any-word?]
//      value [any-list? any-word?]
//  ]
//
DECLARE_NATIVE(BINDABLE)
{
    INCLUDE_PARAMS_OF_BINDABLE;

    Value* v = ARG(VALUE);

    if (Any_Word(v))
        Unbind_Any_Word(v);
    else {
        assert(Any_List(v));

        Tweak_Cell_Binding(v, UNBOUND);
    }

    return COPY(v);
}


//
//  resolve: native [
//
//  "Extract the inner variable target, e.g. (/a: -> a)"
//
//      return: [word! tuple!]
//      source [any-word? any-tuple? any-chain? path!]
//  ]
//
DECLARE_NATIVE(RESOLVE)
{
    INCLUDE_PARAMS_OF_RESOLVE;

    Element* source = Element_ARG(SOURCE);

    if (Any_Word(source)) {
        HEART_BYTE(source) = TYPE_WORD;
        return COPY(source);
    }

    if (Any_Tuple(source)) {
        HEART_BYTE(source) = TYPE_TUPLE;
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
//  proxy-exports: native [
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
DECLARE_NATIVE(PROXY_EXPORTS)
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

    SeaOfVars* where = Cell_Module_Sea(ARG(WHERE));
    SeaOfVars* source = Cell_Module_Sea(ARG(SOURCE));

    const Element* tail;
    const Element* v = Cell_List_At(&tail, ARG(EXPORTS));
    for (; v != tail; ++v) {
        if (not Is_Word(v))
            return FAIL(ARG(EXPORTS));

        const Symbol* symbol = Cell_Word_Symbol(v);

        bool strict = true;

        const Value* src = Sea_Var(source, symbol, strict);
        if (src == nullptr)
            return FAIL(v);  // fail if unset value, also?

        Value* dest = Sea_Var(where, symbol, strict);
        if (dest != nullptr) {
            // Fail if found?
        }
        else {
            dest = Append_Context(where, symbol);
        }

        Copy_Cell(dest, src);
    }

    return COPY(ARG(WHERE));
}


//
//  infix?: native [
//
//  "non-null if a function that gets first argument before the call"
//
//      return: [logic?]
//      frame [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(INFIX_Q)
{
    INCLUDE_PARAMS_OF_INFIX_Q;

    Element* frame = Element_ARG(FRAME);
    return Init_Logic(OUT, Is_Cell_Frame_Infix(frame));
}


//
//  infix: native [
//
//  "For functions that gets 1st argument from left, e.g (/+: infix get $add)"
//
//      return: "Antiform action"
//          [action!]
//      action [<unrun> frame!]
//      :off "Give back a non-infix version of the passed in function"
//      :defer "Allow one full expression on the left to evaluate"
//      :postpone "Allow arbitrary numbers of expressions on left to evaluate"
//  ]
//
DECLARE_NATIVE(INFIX)
{
    INCLUDE_PARAMS_OF_INFIX;

    Actionify(Copy_Cell(OUT, ARG(ACTION)));

    if (Bool_ARG(OFF)) {
        if (Bool_ARG(DEFER) or Bool_ARG(POSTPONE))
            return FAIL(Error_Bad_Refines_Raw());
        Tweak_Cell_Frame_Infix_Mode(OUT, PREFIX_0);
    }
    else if (Bool_ARG(DEFER)) {  // not OFF, already checked
        if (Bool_ARG(POSTPONE))
            return FAIL(Error_Bad_Refines_Raw());
        Tweak_Cell_Frame_Infix_Mode(OUT, INFIX_DEFER);
    }
    else if (Bool_ARG(POSTPONE)) {  // not OFF or DEFER, we checked
        Tweak_Cell_Frame_Infix_Mode(OUT, INFIX_POSTPONE);
    }
    else
        Tweak_Cell_Frame_Infix_Mode(OUT, INFIX_TIGHT);

    return OUT;
}


//
//  identity: native [
//
//  "Returns input value (https://en.wikipedia.org/wiki/Identity_function)"
//
//      return: [any-value? pack!]
//      ^value [any-value? pack!]
//  ]
//
DECLARE_NATIVE(IDENTITY) // sample uses: https://stackoverflow.com/q/3136338
{
    INCLUDE_PARAMS_OF_IDENTITY;

    Element* meta = Element_ARG(VALUE);

    return UNMETA(meta);
}


//
//  free: native [
//
//  "Releases the underlying data of a value so it can no longer be accessed"
//
//      return: [~]
//      memory [<maybe> blank! any-series? any-context? handle!]
//  ]
//
DECLARE_NATIVE(FREE)
{
    INCLUDE_PARAMS_OF_FREE;

    Value* v = ARG(MEMORY);
    if (Is_Blank(v))
        return NOTHING;

    if (Any_Context(v) or Is_Handle(v))
        return FAIL("FREE only implemented for ANY-SERIES? at the moment");

    if (Not_Node_Readable(CELL_NODE1(v)))
        return FAIL("Cannot FREE already freed series");

    Flex* f = Cell_Flex_Ensure_Mutable(v);
    Diminish_Stub(f);
    return NOTHING; // !!! Could return freed value
}


//
//  free?: native [
//
//  "Tells if data has been released with FREE"
//
//      return: "Returns false if value wouldn't be FREEable (e.g. LOGIC!)"
//          [logic?]
//      value [any-value?]
//  ]
//
DECLARE_NATIVE(FREE_Q)
//
// 1. Currently we don't have a "diminished" Pairing...because Cells use
//    the NODE_FLAG_UNREADABLE for meaningfully unreadable cells, that have a
//    different purpose than canonizing references to a diminished form.
//
//    (We could use something like the CELL_FLAG_NOTE or other signal on
//    pairings to cue that references should be canonized to a single freed
//    pair instance, but this isn't a priority at the moment.)
{
    INCLUDE_PARAMS_OF_FREE_Q;

    Value* v = ARG(VALUE);

    if (Is_Void(v) or Is_Nulled(v))
        return nullptr;

    if (not Cell_Has_Node1(v))  // freeable values have Flex in payload node1
        return nullptr;

    Node* n = CELL_NODE1(v);
    if (n == nullptr or Is_Node_A_Cell(n))
        return nullptr;  // no decayed pairing form at this time [1]

    if (Is_Stub_Diminished(cast(Stub*, n)))
        return Init_Okay(OUT);  // decayed is as "free" as outstanding refs get

    return nullptr;
}


//
//  aliases?: native [
//
//  "Return whether or not the underlying data of one value aliases another"
//
//      return: [logic?]
//      value1 [any-series?]
//      value2 [any-series?]
//  ]
//
DECLARE_NATIVE(ALIASES_Q)
{
    INCLUDE_PARAMS_OF_ALIASES_Q;

    return Init_Logic(OUT, Cell_Flex(ARG(VALUE1)) == Cell_Flex(ARG(VALUE2)));
}


//
//  any-value?: native:intrinsic [
//
//  "Tells you if the argument (taken as meta) is storable in a variable"
//
//      return: [logic?]
//      ^atom
//  ]
//
DECLARE_NATIVE(ANY_VALUE_Q)
//
// This works in concert with the decaying mechanisms of typechecking.  So
// if you say your function has [return: [any-value?]] and you try to return
// something like an unstable antiform pack, the type check will fail...but
// it will try again after decaying.
{
    INCLUDE_PARAMS_OF_ANY_VALUE_Q;

    Option(Heart) heart;
    QuoteByte quote_byte;
    Get_Heart_And_Quote_Of_Atom_Intrinsic(&heart, &quote_byte, LEVEL);

    if (quote_byte != ANTIFORM_0)
        return LOGIC(true);

    return LOGIC(Is_Stable_Antiform_Heart(heart));
}


//
//  non-void-value?: native:intrinsic [
//
//  "If the argument (taken as meta) non void, and storable in a variable"
//
//      return: [logic?]
//      ^atom
//  ]
//
DECLARE_NATIVE(NON_VOID_VALUE_Q)
//
// Being able to specify that a function does not accept voids on its type
// checking is fundamentally different from taking ANY-VALUE? and then failing
// if a void is received.  Functions like REDUCE test for if predicates will
// accept voids, and only pass them if they do.  So a function like REIFY
// needs to use NON-VOID-VALUE? in its type spec to work with REDUCE.
{
    INCLUDE_PARAMS_OF_NON_VOID_VALUE_Q;

    DECLARE_ELEMENT (meta);
    Get_Meta_Atom_Intrinsic(meta, LEVEL);

     if (not Is_Quasiform(meta))
        return OKAY;

    if (Is_Meta_Of_Void(meta))
        return nullptr;

    return LOGIC(Is_Stable_Antiform_Heart(Heart_Of(meta)));
}


//
//  any-atom?: native:intrinsic [
//
//  "Accepts absolutely any argument state (unstable antiforms included)"
//
//      return: [logic?]
//      ^atom
//  ]
//
DECLARE_NATIVE(ANY_ATOM_Q)
//
// !!! ELEMENT? isn't ANY-ELEMENT?, so should this just be ATOM?  The policy
// for putting ANY- in front of things has been in flux.
{
    INCLUDE_PARAMS_OF_ANY_ATOM_Q;

    return OKAY;
}



//
//  nihil?: native:intrinsic [
//
//  "Tells you if argument is an ~[]~ antiform, e.g. an empty pack"
//
//      return: [logic?]
//      ^atom
//  ]
//
DECLARE_NATIVE(NIHIL_Q)
{
    INCLUDE_PARAMS_OF_NIHIL_Q;

    DECLARE_ELEMENT (temp);
    Get_Meta_Atom_Intrinsic(temp, LEVEL);

    return LOGIC(Is_Meta_Of_Nihil(temp));
}


//
//  barrier?: native:intrinsic [
//
//  "Tells you if argument is a comma antiform (unstable)"
//
//      return: [logic?]
//      ^atom
//  ]
//
DECLARE_NATIVE(BARRIER_Q)
{
    INCLUDE_PARAMS_OF_BARRIER_Q;

    Option(Heart) heart;
    QuoteByte quote_byte;
    Get_Heart_And_Quote_Of_Atom_Intrinsic(&heart, &quote_byte, LEVEL);

    return LOGIC(quote_byte == ANTIFORM_0 and heart == TYPE_COMMA);
}


//
//  elision?: native:intrinsic [
//
//  "If argument is either nihil or a barrier (empty pack or antiform comma)"
//
//      return: [logic?]
//      ^atom
//  ]
//
DECLARE_NATIVE(ELISION_Q)
{
    INCLUDE_PARAMS_OF_ELISION_Q;

    DECLARE_ELEMENT (meta);
    Get_Meta_Atom_Intrinsic(meta, LEVEL);

    return LOGIC(Is_Meta_Of_Elision(meta));
}


//
//  void?: native:intrinsic [
//
//  "Tells you if argument is void"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_NATIVE(VOID_Q)
{
    INCLUDE_PARAMS_OF_VOID_Q;

    DECLARE_VALUE (v);
    Option(Bounce) bounce = Trap_Bounce_Decay_Value_Intrinsic(v, LEVEL);
    if (bounce)
        return unwrap bounce;

    return LOGIC(Is_Void(v));
}


//
//  nothing?: native:intrinsic [
//
//  "Is argument antiform blank (the state used to indicate an unset variable)"
//
//      return: [logic?]
//      ^value [any-value?]  ; must be ^META
//  ]
//
DECLARE_NATIVE(NOTHING_Q)
//
// 1. Antiform blanks are considered to be unspecialized slots, as they are
//    what is used to fill arguments in MAKE FRAME!.  So if you try to invoke
//    a frame with NOTHING in slots, that gives an "unspecified parameter"
//    error.  It could have been that MAKE FRAME! gave you antiform parameters
//    in unused slots, but that was "cluttered" and the empty slots would not
//    work with DEFAULT or other functions that tried to detect emptiness.
{
    INCLUDE_PARAMS_OF_NOTHING_Q;

    DECLARE_ELEMENT (meta);
    Option(Bounce) bounce = Trap_Bounce_Meta_Decay_Value_Intrinsic(
        meta, LEVEL
    );
    if (bounce)
        return unwrap bounce;

    return LOGIC(Is_Meta_Of_Nothing(meta));
}


//
//  noop: native [  ; native:intrinsic currently needs at least 1 argument
//
//  "Has no effect, besides returning antiform BLANK! (aka NOTHING)"
//
//      return: [~]
//  ]
//
DECLARE_NATIVE(NOOP)  // lack of a hyphen has wide precedent, e.g. jQuery.noop
//
// This function is preferred to having a function called NOTHING, due to the
// potential confusion of people not realizing that (get $nothing) would be
// a function, and not the antiform blank state.
{
    INCLUDE_PARAMS_OF_NOOP;

    return Init_Nothing(OUT);
}


//
//  something?: native:intrinsic [
//
//  "Tells you if the argument is not antiform blank (e.g. not nothing)"
//
//      return: [logic?]
//      ^value [any-value?]
//  ]
//
DECLARE_NATIVE(SOMETHING_Q)
//
// See notes on NOTHING?  This is useful because comparisons in particular do
// not allow you to compare against NOTHING.
//
//   https://forum.rebol.info/t/2068
{
    INCLUDE_PARAMS_OF_SOMETHING_Q;

    DECLARE_ELEMENT (meta);
    Option(Bounce) bounce = Trap_Bounce_Meta_Decay_Value_Intrinsic(
        meta, LEVEL
    );
    if (bounce)
        return unwrap bounce;

    return LOGIC(not Is_Meta_Of_Nothing(meta));
}


//
//  quasar?: native:intrinsic [
//
//  "Tells you if argument is a quasiform blank (~)"
//
//      return: [logic?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(QUASAR_Q)
{
    INCLUDE_PARAMS_OF_QUASAR_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(Is_Quasar(e));
}


//
//  space?: native:intrinsic [
//
//  "Tells you if argument is a space character (#)"
//
//      return: [logic?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(SPACE_Q)
{
    INCLUDE_PARAMS_OF_SPACE_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(Is_Space(e));
}


//
//  heavy: native [
//
//  "Make the heavy form of NULL or VOID (passes through all other values)"
//
//      return: [any-value? pack!]
//      ^atom [any-value? pack!]
//  ]
//
DECLARE_NATIVE(HEAVY) {
    INCLUDE_PARAMS_OF_HEAVY;

    Element* meta = Element_ARG(ATOM);

    if (Is_Meta_Of_Void(meta))
        return Init_Heavy_Void(OUT);

    if (Is_Meta_Of_Null(meta))
        return Init_Heavy_Null(OUT);

    return UNMETA(meta);
}


//
//  light: native [
//
//  "Make the light form of NULL or VOID (passes through all other values)"
//
//      return: [any-value? pack!]
//      ^atom [any-value? pack!]
//  ]
//
DECLARE_NATIVE(LIGHT) {
    INCLUDE_PARAMS_OF_LIGHT;

    Element* meta = Element_ARG(ATOM);

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
//  decay: native:intrinsic [
//
//  "Handle unstable isotopes like assignments do, pass through other values"
//
//      return: [any-value?]
//      atom
//  ]
//
DECLARE_NATIVE(DECAY)
//
// 1. We take the argument as a plain (non-^META) parameter in order to make
//    the decay process happen in the parameter fulfillment, because an idea
//    with intrinsics is that they do not raise errors.  If we called
//    Meta_Unquotify_Decayed() in the body of this intrinsic, that would
//    break the contract in the case of an error.  So we let the parameter
//    fulfillment cause the problem.
{
    INCLUDE_PARAMS_OF_DECAY;

    Option(Bounce) bounce = Trap_Bounce_Decay_Value_Intrinsic(OUT, LEVEL);
    if (bounce)
        return unwrap bounce;

    Assert_Cell_Stable(OUT);  // Value* should always be stable
    return OUT;  // pre-decayed by non-^META argument [1]
}


//
//  reify: native:intrinsic [
//
//  "Make antiforms into their quasiforms, quote all other values"
//
//      return: [element?]
//      value
//  ]
//
DECLARE_NATIVE(REIFY)
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
    INCLUDE_PARAMS_OF_REIFY;

    Option(Bounce) bounce = Trap_Bounce_Decay_Value_Intrinsic(OUT, LEVEL);
    if (bounce)
        return unwrap bounce;

    return Reify(OUT);
}


//
//  noquasi: native:intrinsic [
//
//  "Make quasiforms into their plain forms, pass through all other elements"
//
//      return: [element?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(NOQUASI)
{
    INCLUDE_PARAMS_OF_NOQUASI;

    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(OUT, LEVEL);
    if (b)
        return unwrap b;

    if (Is_Quasiform(OUT))
        QUOTE_BYTE(OUT) = NOQUOTE_1;
    return OUT;
}


//
//  degrade: native [
//
//  "Make quasiforms into their antiforms, pass thru other values"
//
//      return: [any-atom?]
//      element [element?]
//  ]
//
DECLARE_NATIVE(DEGRADE)
{
    INCLUDE_PARAMS_OF_DEGRADE;

    Element* elem = Element_ARG(ELEMENT);
    if (not Is_Quasiform(elem))
        return COPY(elem);

    Copy_Cell(OUT, elem);

    Option(Error*) e = Trap_Coerce_To_Antiform(OUT);
    if (e)
        return FAIL(unwrap e);

    return OUT;
}


//
//  noantiform: native:intrinsic [
//
//  "Turn antiforms into their plain forms, pass thru other values"
//
//      return: [element?]
//      value
//  ]
//
DECLARE_NATIVE(NOANTIFORM)
{
    INCLUDE_PARAMS_OF_NOANTIFORM;

    DECLARE_VALUE (v);
    Option(Bounce) bounce = Trap_Bounce_Decay_Value_Intrinsic(v, LEVEL);
    if (bounce)
        return unwrap bounce;

    if (Is_Antiform(v))
        QUOTE_BYTE(v) = NOQUOTE_1;
    return COPY(v);
}
