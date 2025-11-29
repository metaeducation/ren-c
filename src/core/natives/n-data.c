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
//      return: [frame! action! any-list? any-sequence? any-word? quoted!]
//      spec "Target context or a word whose binding should be the target"
//          [block! @word! any-context?]
//      value "Value whose bound form is to be returned"
//          [any-list? any-sequence? any-word? quoted!]
//  ]
//
DECLARE_NATIVE(BIND)
//
// !!! The "BIND dialect" is just being mapped out.  Right now, it accepts
// a context, or an @WORD!, or a block of @WORD!s.
{
    INCLUDE_PARAMS_OF_BIND;

    Element* v = Element_ARG(VALUE);
    Element* spec = Element_ARG(SPEC);

    if (Is_Block(spec)) {
        const Element* tail;
        const Element* at = List_At(&tail, spec);

        if (not Is_Cell_Listlike(v))  // QUOTED? could have wrapped any type
            panic (Error_Invalid_Arg(level_, PARAM(VALUE)));

        for (; at != tail; ++at) {
            if (not Is_Pinned_Form_Of(WORD, at))
                panic ("BLOCK! binds all @word for the moment");

            require (
              Use* use = Alloc_Use_Inherits(Cell_Binding(v))
            );
            Derelativize(Stub_Cell(use), at, Cell_Binding(spec));
            KIND_BYTE(Stub_Cell(use)) = TYPE_WORD;

            Element* overbind = Known_Element(Stub_Cell(use));
            if (not IS_WORD_BOUND(overbind))
                panic (Error_Not_Bound_Raw(overbind));

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
        assert(Is_Pinned_Form_Of(WORD, spec));
        if (not IS_WORD_BOUND(spec))
            panic (Error_Not_Bound_Raw(spec));

        if (not Is_Cell_Listlike(v))  // QUOTED? could have wrapped any type
            panic (Error_Invalid_Arg(level_, PARAM(VALUE)));

        require (
          Use* use = Alloc_Use_Inherits(Cell_Binding(v))
        );
        Copy_Cell(Stub_Cell(use), spec);
        KIND_BYTE(Stub_Cell(use)) = TYPE_WORD;

        Tweak_Cell_Binding(v, use);

        return COPY(v);
    }

    if (Is_Cell_Wordlike(v)) {
        //
        // Bind a single word (also works on refinements, `/a` ...or `a.`, etc.

        if (Try_Bind_Word(context, v))
            return COPY(v);

        panic (Error_Not_In_Context_Raw(v));
    }

    if (not Is_Cell_Listlike(v))  // QUOTED? could have wrapped any type
        panic (Error_Invalid_Arg(level_, PARAM(VALUE)));

    require (
      Use* use = Alloc_Use_Inherits(Cell_Binding(v))
    );
    Copy_Cell(Stub_Cell(use), context);
    Tweak_Cell_Binding(v, use);

    return COPY(v);
}


//
//  bindable?: native [
//
//  "Return whether a datatype is bindable or not"
//
//      return: [logic?]
//      value [<opt-out> any-stable?]  ; takes antiforms for fail, good idea?
//  ]
//
DECLARE_NATIVE(BINDABLE_Q)
{
    INCLUDE_PARAMS_OF_BINDABLE_Q;

    Value* v = ARG(VALUE);
    if (Is_Antiform(v))
        return fail ("ANTIFORM! values are not bindable");  // caller can TRY

    return LOGIC(Is_Cell_Bindable(Known_Element(v)));
}


//
//  binding-of: native:generic [
//
//  "Get the binding of a value (binding is a loooong work in progress...)"
//
//      return: [<null> any-context?]
//      value [<opt-out> fundamental?]
//  ]
//
DECLARE_NATIVE(BINDING_OF)
{
    INCLUDE_PARAMS_OF_BINDING_OF;

    Element* elem = Element_ARG(VALUE);
    Plainify(elem);  // drop [@ $ ^] sigils

    return Dispatch_Generic(BINDING_OF, elem, LEVEL);
}


//
//  inside: native [
//
//  "Returns a view of the input bound virtually to the context"
//
//      return: [<null> any-stable?]
//      where [any-context? any-list? any-sequence?]
//      value [<opt-out> element?]  ; QUOTED? support?
//  ]
//
DECLARE_NATIVE(INSIDE)
{
    INCLUDE_PARAMS_OF_INSIDE;

    Element* element = Element_ARG(VALUE);
    Element* where = Element_ARG(WHERE);

    Context* context;
    if (Any_Context(where))
        context = Cell_Context(where);
    else if (Any_List(where))
        context = Cell_Binding(where);
    else {
        assert(Any_Sequence(where));
        context = Sequence_Binding(where);
    }

    Derelativize(OUT, element, context);
    return OUT;
}


//
//  overbind: native [
//
//  "Add definitions from context to environment of value"
//
//      return: [<null> any-stable?]
//      definitions [word! any-context?]
//      value [<opt-out> any-list?]  ; QUOTED? support?
//  ]
//
DECLARE_NATIVE(OVERBIND)
{
    INCLUDE_PARAMS_OF_OVERBIND;

    Element* v = Element_ARG(VALUE);
    Element* defs = Element_ARG(DEFINITIONS);

    if (Is_Word(defs)) {
        if (IS_WORD_UNBOUND(defs))
            panic (Error_Not_Bound_Raw(defs));
    }
    else
        assert(Any_Context(defs));

    require (
      Use* use = Alloc_Use_Inherits(List_Binding(v))
    );
    Copy_Cell(Stub_Cell(use), defs);

    Tweak_Cell_Binding(v, use);

    return COPY(v);
}


//
//  has: native [
//
//  "Returns a word bound into the context, if it's available, else null"
//
//      return: "Preserves Sigil ('@foo = has obj '@foo)"
//          [<null> any-word?]
//      context [any-context?]
//      value [<opt-out> any-word?]  ; QUOTED? support?
//  ]
//
DECLARE_NATIVE(HAS)
{
    INCLUDE_PARAMS_OF_HAS;

    Element* v = Element_ARG(VALUE);
    assert(Any_Word(v));  // want to preserve sigil

    Element* context = Element_ARG(CONTEXT);

    const Symbol* symbol = Word_Symbol(v);
    const bool strict = true;
    Option(Index) index = Find_Symbol_In_Context(context, symbol, strict);
    if (not index)
        return NULLED;

    if (not Is_Module(context)) {
        VarList* varlist = Cell_Varlist(context);
        Element* out = Init_Word_Bound(OUT, symbol, varlist);
        Tweak_Word_Index(out, unwrap index);
        Copy_Kind_Byte(out, v);
        return OUT;
    }

    SeaOfVars* sea = Cell_Module_Sea(context);
    Element* out = Init_Word(OUT, symbol);
    Copy_Kind_Byte(out, v);
    Tweak_Cell_Binding(out, sea);
    return OUT;
}


//
//  without: native [
//
//  "Remove a virtual binding from a value"
//
//      return: [<null> any-word? any-list?]
//      context "If integer, then removes that number of virtual bindings"
//          [integer! any-context?]
//      value [<const> <opt-out> any-word? any-list?]  ; QUOTED? support?
//  ]
//
DECLARE_NATIVE(WITHOUT)
{
    INCLUDE_PARAMS_OF_WITHOUT;

    VarList* ctx = Cell_Varlist(ARG(CONTEXT));
    Element* v = Element_ARG(VALUE);

    // !!! Note that BIND of a WORD! in historical Rebol/Red would return the
    // input word as-is if the word wasn't in the requested context, while
    // IN would return trash on failure.  We carry forward the NULL-failing
    // here in IN, but BIND's behavior on words may need revisiting.
    //
    if (Any_Word(v)) {
        const Symbol* symbol = Word_Symbol(v);
        const bool strict = true;
        Option(Index) index = Find_Symbol_In_Context(
            Element_ARG(CONTEXT), symbol, strict
        );
        if (not index)
            return NULLED;
        Init_Word_Bound(
            OUT,
            symbol,  // !!! incoming case...consider impact of strict if false?
            ctx
        );
        Tweak_Word_Index(OUT, unwrap index);
        Copy_Kind_Byte(Known_Element(OUT), v);
        return OUT;
    }

    require (
      Use* use = Alloc_Use_Inherits(List_Binding(v))
    );
    Copy_Cell(Stub_Cell(use), Varlist_Archetype(ctx));

    Tweak_Cell_Binding(v, use);

    return COPY(v);
}


//
//  use: native [
//
//  "Defines words local to a block (See also: LET)"
//
//      return: [any-stable?]
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

    require (
      VarList* varlist = Create_Loop_Context_May_Bind_Body(body, vars)
    );
    UNUSED(varlist);  // managed, but [1]

    if (Eval_Any_List_At_Throws(OUT, body, SPECIFIED))
        return THROWN;

    return OUT;
}


//
//  refinement?: native:intrinsic [
//
//  "Test if an argument is a chain with a leading space"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(REFINEMENT_Q)
{
    INCLUDE_PARAMS_OF_REFINEMENT_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_Get_Word(e));
}


//
//  set-word?: native:intrinsic [
//
//  "Test if an argument is a chain with a word and trailing space"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(SET_WORD_Q)
{
    INCLUDE_PARAMS_OF_SET_WORD_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_Set_Word(e));
}


//
//  set-run-word?: native:intrinsic [
//
//  "Test if argument is a path like /WORD: (for setting action variables)"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(SET_RUN_WORD_Q)
{
    INCLUDE_PARAMS_OF_SET_RUN_WORD_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_Set_Run_Word(e));
}


//
//  run-word?: native:intrinsic [
//
//  "Test if argument is a path like /WORD"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(RUN_WORD_Q)
{
    INCLUDE_PARAMS_OF_SET_RUN_WORD_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    if (not Is_Path(e))
        return LOGIC(false);

    Option(SingleHeart) single = Try_Get_Sequence_Singleheart(e);
    return LOGIC(single == LEADING_SPACE_AND(WORD));
}


//
//  get-word?: native:intrinsic [
//
//  "Test if an argument is a chain with a leading space and a word"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(GET_WORD_Q)
{
    INCLUDE_PARAMS_OF_GET_WORD_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_Get_Word(e));
}


//
//  set-tuple?: native:intrinsic [
//
//  "Test if an argument is a chain with a tuple and trailing space"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(SET_TUPLE_Q)
{
    INCLUDE_PARAMS_OF_SET_TUPLE_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_Set_Tuple(e));
}


//
//  get-tuple?: native:intrinsic [
//
//  "Test if an argument is a chain with a leading space and a tuple"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(GET_TUPLE_Q)
{
    INCLUDE_PARAMS_OF_GET_TUPLE_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_Get_Tuple(e));
}


//
//  set-group?: native:intrinsic [
//
//  "Test if an argument is a chain with a group and trailing space"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(SET_GROUP_Q)
{
    INCLUDE_PARAMS_OF_SET_GROUP_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_Set_Group(e));
}


//
//  get-group?: native:intrinsic [
//
//  "Test if an argument is a chain with a leading space and a group"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(GET_GROUP_Q)
{
    INCLUDE_PARAMS_OF_GET_GROUP_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_Get_Group(e));
}


//
//  set-block?: native:intrinsic [
//
//  "Test if an argument is a chain with a block and trailing space"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(SET_BLOCK_Q)
{
    INCLUDE_PARAMS_OF_SET_BLOCK_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_Set_Block(e));
}


//
//  get-block?: native:intrinsic [
//
//  "Test if an argument is a chain with a leading space and a block"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(GET_BLOCK_Q)
{
    INCLUDE_PARAMS_OF_GET_BLOCK_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_Get_Block(e));
}


//
//  any-set-value?: native:intrinsic [
//
//  "Test if an argument is a 2-element chain with a trailing space"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(ANY_SET_VALUE_Q)
{
    INCLUDE_PARAMS_OF_ANY_SET_VALUE_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Any_Set_Value(e));
}


//
//  any-get-value?: native:intrinsic [
//
//  "Test if an argument is a 2-element chain with a leading space"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_NATIVE(ANY_GET_VALUE_Q)
{
    INCLUDE_PARAMS_OF_ANY_GET_VALUE_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Any_Get_Value(e));
}


//
//  quasi-word?: native:intrinsic [
//
//  "Test if an argument is an QUASI form of word"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(QUASI_WORD_Q)
{
    INCLUDE_PARAMS_OF_QUASI_WORD_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_Quasiform(e) and Heart_Of(e) == TYPE_WORD);
}


//
//  char?: native:intrinsic [
//
//  "Test if an argument is a rune with one codepoint (or #{00} NUL blob)"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(CHAR_Q)
{
    INCLUDE_PARAMS_OF_CHAR_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_Rune_And_Is_Char(e));
}


//
//  lit-word?: native:intrinsic [
//
//  "Test if an argument is quoted word"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(LIT_WORD_Q)
{
    INCLUDE_PARAMS_OF_LIT_WORD_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(
        LIFT_BYTE(e) == ONEQUOTE_NONQUASI_4 and Heart_Of(e) == TYPE_WORD
    );
}


//
//  lit-path?: native:intrinsic [
//
//  "Test if an argument is a quoted path"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(LIT_PATH_Q)
{
    INCLUDE_PARAMS_OF_LIT_PATH_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Heart_Of(e) == TYPE_PATH and Quotes_Of(e) == 1);
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
    require (
      Bounce b = Bounce_Decay_Value_Intrinsic(v, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

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

    Element* word = Element_ARG(WORD);

    if (Any_Word(word) or Is_Set_Word(word))
        Unbind_Any_Word(word);
    else {
        assert(Is_Block(word));

        const Element* tail;
        Element* at = List_At_Ensure_Mutable(&tail, word);
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

    Element* v = Element_ARG(VALUE);

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
//      source [any-word? tuple! chain! path!]
//  ]
//
DECLARE_NATIVE(RESOLVE)
{
    INCLUDE_PARAMS_OF_RESOLVE;

    Element* source = Element_ARG(SOURCE);

    if (Any_Word(source)) {
        KIND_BYTE(source) = TYPE_WORD;
        return COPY(source);
    }

    if (Is_Tuple(source)) {
        KIND_BYTE(source) = TYPE_TUPLE;
        return COPY(source);
    }

    if (Is_Path(source)) {  // !!! For now: (resolve '/a:) -> a
        SingleHeart single;
        if (not (single = opt Try_Get_Sequence_Singleheart(source)))
            panic (source);

        if (
            single == LEADING_SPACE_AND(WORD)  // /a
            or single == LEADING_SPACE_AND(TUPLE)  // /a.b.c or /.a
            or single == TRAILING_SPACE_AND(WORD)  // a/
            or single == TRAILING_SPACE_AND(TUPLE)  // a.b.c/ or .a/
        ){
            assume (
              Unsingleheart_Sequence(source)
            );
            return COPY(source);
        }
        if (
            single == LEADING_SPACE_AND(CHAIN)  // /a: or /a:b:c or /:a
            or single == TRAILING_SPACE_AND(CHAIN)  // a:/ or a:b:c/ or :a/
        ){
            assume (
              Unsingleheart_Sequence(source)
            );
            // fall through to chain decoding.
        }
        else
            panic (source);
    }

    SingleHeart single = opt Try_Get_Sequence_Singleheart(source);
    if (single == NOT_SINGLEHEART_0) {
        // fall through
    }
    else if (
        single == LEADING_SPACE_AND(WORD)  // a:
        or single == LEADING_SPACE_AND(TUPLE)  // a.b.c:
        or single == TRAILING_SPACE_AND(WORD)  // :a
        or single == TRAILING_SPACE_AND(TUPLE)  // :a.b.c
    ){
        assume (
          Unsingleheart_Sequence(source)
        );
        return COPY(source);
    }

    panic (source);
}


//
//  proxy-exports: native [
//
//  "Copy context by setting values in the target from those in the source"
//
//      return: "Same as the target module"
//          [module!]
//      where [<opt-out> module!] "(modified)"
//      source [<opt-out> module!]
//      exports "Which words to export from the source"
//          [<opt-out> block!]
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
// variables named by the `exports:` block of a module to the module that was
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
    const Element* v = List_At(&tail, ARG(EXPORTS));
    for (; v != tail; ++v) {
        if (not Is_Word(v))
            panic (ARG(EXPORTS));

        const Symbol* symbol = Word_Symbol(v);

        bool strict = true;

        const Slot* src = opt Sea_Slot(source, symbol, strict);
        if (not src)
            panic (v);  // panic if unset value, also?

        Slot* dest = opt Sea_Slot(where, symbol, strict);
        if (dest) {
            // Fail if found?
            require (
              Read_Slot(Slot_Init_Hack(dest), src)
            );
        }
        else {
            require (
              Read_Slot(Append_Context(where, symbol), src)
            );
        }
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
    return LOGIC(Is_Frame_Infix(frame));
}


//
//  infix: native [
//
//  "For functions that gets 1st argument from left, e.g (/+: infix get $add)"
//
//      return: [action!]
//      action [<unrun> frame!]
//      :off "Give back a non-infix version of the passed in function"
//      :defer "Allow one full expression on the left to evaluate"
//      :postpone "Allow arbitrary numbers of expressions on left to evaluate"
//  ]
//
DECLARE_NATIVE(INFIX)
{
    INCLUDE_PARAMS_OF_INFIX;

    Value* out = Actionify(Copy_Cell(OUT, ARG(ACTION)));

    if (Bool_ARG(OFF)) {
        if (Bool_ARG(DEFER) or Bool_ARG(POSTPONE))
            panic (Error_Bad_Refines_Raw());
        Tweak_Frame_Infix_Mode(out, PREFIX_0);
    }
    else if (Bool_ARG(DEFER)) {  // not OFF, already checked
        if (Bool_ARG(POSTPONE))
            panic (Error_Bad_Refines_Raw());
        Tweak_Frame_Infix_Mode(out, INFIX_DEFER);
    }
    else if (Bool_ARG(POSTPONE)) {  // not OFF or DEFER, we checked
        Tweak_Frame_Infix_Mode(out, INFIX_POSTPONE);
    }
    else
        Tweak_Frame_Infix_Mode(out, INFIX_TIGHT);

    return UNSURPRISING(OUT);
}


//
//  ghostable: native [
//
//  "Make a function's invocations not default to turn GHOST! results to VOID"
//
//      return: [action! frame!]
//      action [action! frame!]
//      :off "Give back non-ghostable version of the passed in function"
//  ]
//
DECLARE_NATIVE(GHOSTABLE)
{
    INCLUDE_PARAMS_OF_GHOSTABLE;

    Value* out = Copy_Cell(OUT, ARG(ACTION));

    if (Bool_ARG(OFF))
        Clear_Cell_Flag(out, WEIRD_GHOSTABLE);
    else
        Set_Cell_Flag(out, WEIRD_GHOSTABLE);

    if (Is_Action(out))
        return UNSURPRISING(OUT);

    return OUT;
}


//
//  ghostable?: native [
//
//  "Return whether a function naturally suppresses GHOST! to VOID conversion"
//
//      return: [logic?]
//      action [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(GHOSTABLE_Q)
{
    INCLUDE_PARAMS_OF_GHOSTABLE_Q;

    return LOGIC(Get_Cell_Flag(ARG(ACTION), WEIRD_GHOSTABLE));
}


//
//  identity: native:intrinsic [
//
//  "Returns input value (https://en.wikipedia.org/wiki/Identity_function)"
//
//      return: [any-value?]
//      ^value [any-value?]
//  ]
//
DECLARE_NATIVE(IDENTITY)  // sample uses: https://stackoverflow.com/q/3136338
//
// Note: a peculiar definition in the default setup for identity is as the
// meaning of the left arrow `<-` ... this strange choice gives you the
// ability to annotate when information is flowing leftward:
//
//   https://rebol.metaeducation.com/t/weird-old-idea-for-identity/2165
{
    INCLUDE_PARAMS_OF_IDENTITY;

    Atom* atom = Intrinsic_Atom_ARG(LEVEL);

    return COPY(atom);
}


//
//  free: native [
//
//  "Releases the underlying data of a value so it can no longer be accessed"
//
//      return: []
//      memory [<opt-out> any-series? any-context? handle!]
//  ]
//
DECLARE_NATIVE(FREE)
{
    INCLUDE_PARAMS_OF_FREE;

    Value* v = ARG(MEMORY);

    if (Any_Context(v) or Is_Handle(v))
        panic ("FREE only implemented for ANY-SERIES? at the moment");

    if (Not_Base_Readable(CELL_PAYLOAD_1(v)))
        panic ("Cannot FREE already freed series");

    Flex* f = Cell_Flex_Ensure_Mutable(v);
    Diminish_Stub(f);
    return TRIPWIRE; // !!! Could return freed value
}


//
//  free?: native [
//
//  "Tells if data has been released with FREE"
//
//      return: "Returns false if value wouldn't be FREEable (e.g. LOGIC!)"
//          [logic?]
//      value [<opt-out> any-stable?]
//  ]
//
DECLARE_NATIVE(FREE_Q)
//
// 1. Currently we don't have a "diminished" Pairing...because Cells use
//    the BASE_FLAG_UNREADABLE for meaningfully unreadable cells, that have a
//    different purpose than canonizing references to a diminished form.
//
//    (We could use something like the CELL_FLAG_NOTE or other signal on
//    pairings to cue that references should be canonized to a single freed
//    pair instance, but this isn't a priority at the moment.)
{
    INCLUDE_PARAMS_OF_FREE_Q;

    Value* v = ARG(VALUE);

    if (Is_Nulled(v))
        return LOGIC(false);

    if (not Cell_Payload_1_Needs_Mark(v))  // freeable values have Flex in payload payload1
        return LOGIC(false);

    Base* b = CELL_PAYLOAD_1(v);
    if (b == nullptr or not Is_Base_A_Stub(b))
        return LOGIC(false);  // no decayed pairing form at this time [1]

    if (Is_Stub_Diminished(cast(Stub*, b)))
        return LOGIC(true);  // decayed is as "free" as outstanding refs get

    return LOGIC(false);
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

    return LOGIC(Cell_Flex(ARG(VALUE1)) == Cell_Flex(ARG(VALUE2)));
}


//
//  any-stable?: native:intrinsic [
//
//  "Tells you if the argument (taken as meta) is storable in a variable"
//
//      return: [logic?]
//      ^value [any-value?]
//  ]
//
DECLARE_NATIVE(ANY_STABLE_Q)
//
// This works in concert with the decaying mechanisms of typechecking.  So
// if you say your function has [return: [any-stable?]] and you try to return
// something like an unstable antiform pack, the type check will fail...but
// it will try again after decaying.
{
    INCLUDE_PARAMS_OF_ANY_STABLE_Q;

    const Atom* atom = Intrinsic_Typechecker_Atom_ARG(LEVEL);

    return LOGIC(Is_Cell_Stable(atom));
}


//
//  any-value?: native:intrinsic [
//
//  "Accepts absolutely any argument state (unstable antiforms included)"
//
//      return: [logic?]
//      ^value  ; can't use any-value? - recursive
//  ]
//
DECLARE_NATIVE(ANY_VALUE_Q)  // synonym for internal concept of ANY_ATOM
//
// !!! The automatic typecheckers that are built don't handle unstable
// antiforms at this time.  They need to, so things like this and PACK?
// and ERROR? don't have to be special cased.
//
// !!! ELEMENT? isn't ANY-ELEMENT?, so should this just be VALUE?  The policy
// for putting ANY- in front of things has been in flux.
{
    INCLUDE_PARAMS_OF_ANY_VALUE_Q;

    return OKAY;
}


//
//  any-word?: native:intrinsic [
//
//  "!!! Temporary !!! attempt to answer if [word ^word $word @word]"
//
//      return: [logic?]
//      value [<opt-out> any-stable?]
//  ]
//
DECLARE_NATIVE(ANY_WORD_Q)
//
// !!! Interim exposure of ANY-WORD?
{
    INCLUDE_PARAMS_OF_ANY_WORD_Q;

    DECLARE_VALUE (v);
    require (
      Bounce b = Bounce_Decay_Value_Intrinsic(v, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Any_Word(v));
}


//
//  void?: native:intrinsic [
//
//  "Tells you if argument is an ~[]~ antiform, e.g. an empty pack"
//
//      return: [logic?]
//      ^value [any-value?]
//  ]
//
DECLARE_NATIVE(VOID_Q)
{
    INCLUDE_PARAMS_OF_VOID_Q;

    const Atom* atom = Intrinsic_Typechecker_Atom_ARG(LEVEL);

    return LOGIC(Is_Void(atom));
}


//
//  blank?: native:intrinsic [
//
//  "Tells you if argument is an ~()~ antiform, e.g. an empty splice"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_NATIVE(BLANK_Q)
{
    INCLUDE_PARAMS_OF_BLANK_Q;

    DECLARE_VALUE (v);
    require (
      Bounce b = Bounce_Decay_Value_Intrinsic(v, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_Blank(v));
}


//
//  tripwire?: native:intrinsic [
//
//  "Tells you if argument is an ~ antiform, e.g. an tripwire TRASH! form"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_NATIVE(TRIPWIRE_Q)
{
    INCLUDE_PARAMS_OF_TRIPWIRE_Q;

    DECLARE_VALUE (v);
    require (
      Bounce b = Bounce_Decay_Value_Intrinsic(v, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_Tripwire(v));
}


//
//  noop: native [  ; native:intrinsic currently needs at least 1 argument
//
//  "Returns antiform SPACE (aka TRIPWIRE)"
//
//      return: []
//  ]
//
DECLARE_NATIVE(NOOP)  // lack of a hyphen common, e.g. jQuery.noop
//
// What a NOOP returns could be debated, but tripwire is chosen as TRIPWIRE is
// a non-function, so that (^tripwire) will produce it.  While (^void) or ~[]~
// is needed to make void, you can also produce it with just plain ().  GHOST!
// has no particularly clean way to make it other than (^ghost) or ~,~.  But
// vanishing functions are weird, and the desire to mark someplace as "this
// branch intentionally left blank" with a noop is a more normal response, so
// NIHIL is used to make GHOST!
{
    INCLUDE_PARAMS_OF_NOOP;

    return Init_Tripwire(OUT);
}


//
//  quasar?: native:intrinsic [
//
//  "Tells you if argument is a quasiform space (~)"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(QUASAR_Q)
{
    INCLUDE_PARAMS_OF_QUASAR_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_Quasar(e));
}


//
//  space?: native:intrinsic [
//
//  "Tells you if argument is a space character (#)"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(SPACE_Q)
{
    INCLUDE_PARAMS_OF_SPACE_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_Space(e));
}


//
//  heavy: native:intrinsic [
//
//  "Make the heavy form of NULL (passes through all other values)"
//
//      return: [any-value?]
//      ^value [any-value?]
//  ]
//
DECLARE_NATIVE(HEAVY)
{
    INCLUDE_PARAMS_OF_HEAVY;

    Atom* atom = Intrinsic_Atom_ARG(LEVEL);

    if (Is_Light_Null(atom))
        return Init_Heavy_Null(OUT);

    return COPY(atom);
}


//
//  heavy-null?: native:intrinsic [
//
//  "Determine if argument is the heavy form of NULL, ~[~null~]~ antiform"
//
//      return: [logic?]
//      ^value [any-value?]
//  ]
//
DECLARE_NATIVE(HEAVY_NULL_Q)
{
    INCLUDE_PARAMS_OF_HEAVY_NULL_Q;

    const Atom* atom = Intrinsic_Atom_ARG(LEVEL);

    return LOGIC(Is_Heavy_Null(atom));
}


//
//  light: native:intrinsic [
//
//  "Make the light form of NULL (passes through all other values)"
//
//      return: [any-value?]
//      ^value [any-value?]
//  ]
//
DECLARE_NATIVE(LIGHT)
{
    INCLUDE_PARAMS_OF_LIGHT;

    Atom* atom = Intrinsic_Atom_ARG(LEVEL);

    if (not Is_Pack(atom))
        return COPY(atom);

    Length len;
    const Element* first = List_Len_At(&len, atom);

    if (len != 1)
        return COPY(atom);

    if (Is_Lifted_Null(first))  // only case we care about, pack of one null
        return NULLED;  // return the null, no longer in a pack

    return COPY(atom);
}


//
//  decay: native:intrinsic [
//
//  "Handle unstable isotopes like assignments do, pass through other values"
//
//      return: [any-stable?]
//      value
//  ]
//
DECLARE_NATIVE(DECAY)
{
    INCLUDE_PARAMS_OF_DECAY;

    require (
      Bounce b = Bounce_Decay_Value_Intrinsic(OUT, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return OUT;
}


//
//  decayable?: native:intrinsic [
//
//  "Answer if a value is decayable"
//
//      return: [logic?]
//      ^value  ; constrain to PACK? (extra typecheck work for intrinsic...)
//  ]
//
DECLARE_NATIVE(DECAYABLE_Q)
{
    INCLUDE_PARAMS_OF_DECAYABLE_Q;

    Atom* atom = Intrinsic_Atom_ARG(LEVEL);

    Decay_If_Unstable(atom) except (Error* e) {
        UNUSED(e);
        return LOGIC(false);
    }

    return LOGIC(true);
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
// be a reification of the first value in the pack.  And REIFY of an error
// will panic.  We could have REIFY:EXCEPT and REIFY:PACK, if they seem to be
// important...but let's see if we can get away without them and have this be
// an intrinsic.
{
    INCLUDE_PARAMS_OF_REIFY;

    require (
      Bounce b = Bounce_Decay_Value_Intrinsic(OUT, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    Assert_Cell_Stable(OUT);  // Value* should always be stable
    return Reify(OUT);
}


//
//  noquasi: native:intrinsic [
//
//  "Make quasiforms into their plain forms, pass through all other elements"
//
//      return: [element?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(NOQUASI)
{
    INCLUDE_PARAMS_OF_NOQUASI;

    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(OUT, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    if (Is_Quasiform(OUT))
        LIFT_BYTE(OUT) = NOQUOTE_2;
    return OUT;
}


//
//  degrade: native [
//
//  "Make quasiforms into their antiforms, pass thru other values"
//
//      return: [any-value?]
//      value [element?]
//  ]
//
DECLARE_NATIVE(DEGRADE)
{
    INCLUDE_PARAMS_OF_DEGRADE;

    Element* elem = Element_ARG(VALUE);
    if (not Is_Quasiform(elem))
        return COPY(elem);

    Copy_Cell(OUT, elem);

    require (
      Coerce_To_Antiform(OUT)
    );
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
    require (
      Bounce b = Bounce_Decay_Value_Intrinsic(v, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    if (Is_Antiform(v))
        LIFT_BYTE(v) = NOQUOTE_2;
    return COPY(v);
}
