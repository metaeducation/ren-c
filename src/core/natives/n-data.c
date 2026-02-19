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
//  /bind1: native [
//
//  "Bind argument in current evaluative context (synonym for $)"
//
//      return: [element?]
//      value [element?]
//  ]
//
DECLARE_NATIVE(BIND1)
//
// It's believed that this will likely become the meaning of BIND.  While it
// seems a loss to use the word for a single-arity function instead of a more
// complex dialect, it helps those who do not like reading $ as symboly-code.
{
    INCLUDE_PARAMS_OF_BIND1;

    Element* v = ARG(VALUE);

    Copy_Cell_May_Bind(OUT, v, Level_Binding(level_));
    return OUT;
}


//
//  /bind: native [
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
            Copy_Cell_May_Bind(Stub_Cell(use), at, Cell_Binding(spec));
            KIND_BYTE(Stub_Cell(use)) = TYPE_WORD;

            Element* overbind = As_Element(Stub_Cell(use));
            if (not IS_WORD_BOUND(overbind))
                panic (Error_Not_Bound_Raw(overbind));

            Tweak_Cell_Binding(v, use);
        }

        return COPY_TO_OUT(v);
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

        return COPY_TO_OUT(v);
    }

    if (Is_Cell_Wordlike(v)) {
        //
        // Bind a single word (also works on refinements, `/a` ...or `a.`, etc.

        if (Try_Bind_Word(context, v))
            return COPY_TO_OUT(v);

        panic (Error_Not_In_Context_Raw(v));
    }

    if (not Is_Cell_Listlike(v))  // QUOTED? could have wrapped any type
        panic (Error_Invalid_Arg(level_, PARAM(VALUE)));

    require (
      Use* use = Alloc_Use_Inherits(Cell_Binding(v))
    );
    Copy_Cell(Stub_Cell(use), context);
    Tweak_Cell_Binding(v, use);

    return COPY_TO_OUT(v);
}


//
//  /bindable?: native [
//
//  "Return whether a datatype is bindable or not"
//
//      return: [logic!]
//      value [any-stable?]  ; takes antiforms for fail, good idea?
//  ]
//
DECLARE_NATIVE(BINDABLE_Q)
{
    INCLUDE_PARAMS_OF_BINDABLE_Q;

    Stable* v = ARG(VALUE);
    if (Is_Antiform(v))
        return fail ("ANTIFORM! values are not bindable");  // caller can TRY

    return LOGIC_OUT(Is_Cell_Bindable(As_Element(v)));
}


//
//  /binding-of: native:generic [
//
//  "Get the binding of a value (binding is a loooong work in progress...)"
//
//      return: [<null> any-context?]
//      value [fundamental?]
//  ]
//
DECLARE_NATIVE(BINDING_OF)
{
    INCLUDE_PARAMS_OF_BINDING_OF;

    Element* elem = Element_ARG(VALUE);
    Clear_Cell_Sigil(elem);  // drop [@ $ ^] sigils

    return Dispatch_Generic(BINDING_OF, elem, LEVEL);
}


//
//  /inside: native [
//
//  "Returns a view of the input bound virtually to the context"
//
//      return: [<null> any-stable?]
//      where [any-context? any-list? any-sequence?]
//      value [element?]  ; QUOTED? support?
//  ]
//
DECLARE_NATIVE(INSIDE)
{
    INCLUDE_PARAMS_OF_INSIDE;

    Element* element = ARG(VALUE);
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

    Copy_Cell_May_Bind(OUT, element, context);
    return OUT;
}


//
//  /overbind: native [
//
//  "Add definitions from context to environment of value"
//
//      return: [<null> any-stable?]
//      definitions [word! any-context?]
//      value [any-list?]  ; QUOTED? support?
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

    return COPY_TO_OUT(v);
}


//
//  /has: native [
//
//  "Returns a word bound into the context, if it's available, else null"
//
//      return: [<null> any-word?]
//      context [any-context?]
//      value "Can carry Sigil, is preserved ('@foo = has obj '@foo)"
//          [any-word?]  ; QUOTED? support?
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
    Option(Ordinal) n = Find_Symbol_In_Context(context, symbol, strict);
    if (not n)
        return NULL_OUT;

    if (not Is_Module(context)) {
        VarList* varlist = Cell_Varlist(context);
        Element* out = Init_Word_Bound(OUT, symbol, varlist);
        Tweak_Word_Index(out, unwrap n);
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
//  /without: native [
//
//  "Remove a virtual binding from a value"
//
//      return: [<null> any-word? any-list?]
//      context "If integer, then removes that number of virtual bindings"
//          [integer! any-context?]
//      value [<const> any-word? any-list?]  ; QUOTED? support?
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
        Option(Ordinal) n = Find_Symbol_In_Context(
            Element_ARG(CONTEXT), symbol, strict
        );
        if (not n)
            return NULL_OUT;
        Init_Word_Bound(
            OUT,
            symbol,  // !!! incoming case...consider impact of strict if false?
            ctx
        );
        Tweak_Word_Index(OUT, unwrap n);
        Copy_Kind_Byte(As_Element(OUT), v);
        return OUT;
    }

    require (
      Use* use = Alloc_Use_Inherits(List_Binding(v))
    );
    Init_Context_Cell(Stub_Cell(use), ctx);

    Tweak_Cell_Binding(v, use);

    return COPY_TO_OUT(v);
}


//
//  /use: native [
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
//  /refinement?: pure native:intrinsic [
//
//  "Test if an argument is a chain with a leading space"
//
//      return: [logic!]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(REFINEMENT_Q)
{
    INCLUDE_PARAMS_OF_REFINEMENT_Q;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    return LOGIC_OUT(Is_Get_Word(v));
}


//
//  /set-word?: pure native:intrinsic [
//
//  "Test if an argument is a chain with a word and trailing space"
//
//      return: [logic!]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(SET_WORD_Q)
{
    INCLUDE_PARAMS_OF_SET_WORD_Q;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    return LOGIC_OUT(Is_Set_Word(v));
}


//
//  /set-run-word?: pure native:intrinsic [
//
//  "Test if argument is a path like /WORD: (for setting action variables)"
//
//      return: [logic!]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(SET_RUN_WORD_Q)
{
    INCLUDE_PARAMS_OF_SET_RUN_WORD_Q;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    return LOGIC_OUT(Is_Set_Run_Word(v));
}


//
//  /run-word?: pure native:intrinsic [
//
//  "Test if argument is a path like /WORD"
//
//      return: [logic!]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(RUN_WORD_Q)
{
    INCLUDE_PARAMS_OF_RUN_WORD_Q;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    if (not Is_Path(v))
        return LOGIC_OUT(false);

    Option(SingleHeart) single = Try_Get_Sequence_Singleheart(v);
    return LOGIC_OUT(single == LEADING_BLANK_AND(WORD));
}


//
//  /get-word?: pure native:intrinsic [
//
//  "Test if an argument is a chain with a leading space and a word"
//
//      return: [logic!]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(GET_WORD_Q)
{
    INCLUDE_PARAMS_OF_GET_WORD_Q;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    return LOGIC_OUT(Is_Get_Word(v));
}


//
//  /set-tuple?: pure native:intrinsic [
//
//  "Test if an argument is a chain with a tuple and trailing space"
//
//      return: [logic!]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(SET_TUPLE_Q)
{
    INCLUDE_PARAMS_OF_SET_TUPLE_Q;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    return LOGIC_OUT(Is_Set_Tuple(v));
}


//
//  /get-tuple?: pure native:intrinsic [
//
//  "Test if an argument is a chain with a leading space and a tuple"
//
//      return: [logic!]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(GET_TUPLE_Q)
{
    INCLUDE_PARAMS_OF_GET_TUPLE_Q;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    return LOGIC_OUT(Is_Get_Tuple(v));
}


//
//  /set-group?: pure native:intrinsic [
//
//  "Test if an argument is a chain with a group and trailing space"
//
//      return: [logic!]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(SET_GROUP_Q)
{
    INCLUDE_PARAMS_OF_SET_GROUP_Q;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    return LOGIC_OUT(Is_Set_Group(v));
}


//
//  /get-group?: pure native:intrinsic [
//
//  "Test if an argument is a chain with a leading space and a group"
//
//      return: [logic!]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(GET_GROUP_Q)
{
    INCLUDE_PARAMS_OF_GET_GROUP_Q;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    return LOGIC_OUT(Is_Get_Group(v));
}


//
//  /set-block?: pure native:intrinsic [
//
//  "Test if an argument is a chain with a block and trailing space"
//
//      return: [logic!]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(SET_BLOCK_Q)
{
    INCLUDE_PARAMS_OF_SET_BLOCK_Q;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    return LOGIC_OUT(Is_Set_Block(v));
}


//
//  /get-block?: pure native:intrinsic [
//
//  "Test if an argument is a chain with a leading space and a block"
//
//      return: [logic!]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(GET_BLOCK_Q)
{
    INCLUDE_PARAMS_OF_GET_BLOCK_Q;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    return LOGIC_OUT(Is_Get_Block(v));
}


//
//  /any-set-value?: pure native:intrinsic [
//
//  "Test if an argument is a 2-element chain with a trailing space"
//
//      return: [logic!]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(ANY_SET_VALUE_Q)
{
    INCLUDE_PARAMS_OF_ANY_SET_VALUE_Q;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    return LOGIC_OUT(Any_Set_Value(v));
}


//
//  /any-get-value?: pure native:intrinsic [
//
//  "Test if an argument is a 2-element chain with a leading space"
//
//      return: [logic!]
//      value '[any-value?]
//  ]
//
DECLARE_NATIVE(ANY_GET_VALUE_Q)
{
    INCLUDE_PARAMS_OF_ANY_GET_VALUE_Q;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    return LOGIC_OUT(Any_Get_Value(v));
}


//
//  /quasi-word?: pure native:intrinsic [
//
//  "Test if an argument is an QUASI form of word"
//
//      return: [logic!]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(QUASI_WORD_Q)
{
    INCLUDE_PARAMS_OF_QUASI_WORD_Q;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    return LOGIC_OUT(Is_Quasiform(v) and Heart_Of(v) == TYPE_WORD);
}


//
//  /char?: pure native:intrinsic [
//
//  "Test if an argument is a rune with one codepoint (or #{00} NUL blob)"
//
//      return: [logic!]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(CHAR_Q)
{
    INCLUDE_PARAMS_OF_CHAR_Q;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return LOGIC_OUT(false);

    return LOGIC_OUT(Is_Rune_And_Is_Char(v));
}


//
//  /lit-word?: pure native:intrinsic [
//
//  "Test if an argument is quoted word"
//
//      return: [logic!]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(LIT_WORD_Q)
{
    INCLUDE_PARAMS_OF_LIT_WORD_Q;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    return LOGIC_OUT(
        LIFT_BYTE(v) == ONEQUOTE_NONQUASI_5 and Heart_Of(v) == TYPE_WORD
    );
}


//
//  /lit-path?: pure native:intrinsic [
//
//  "Test if an argument is a quoted path"
//
//      return: [logic!]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(LIT_PATH_Q)
{
    INCLUDE_PARAMS_OF_LIT_PATH_Q;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    return LOGIC_OUT(Heart_Of(v) == TYPE_PATH and Quotes_Of(v) == 1);
}


//
//  /unbind: native:intrinsic [
//
//  "If a value is bindable, unbind it at the 'tip' (if it's bound)"
//
//      return: [any-stable?]
//      value '[any-stable?]
//  ]
//
DECLARE_NATIVE(UNBIND)
//
// !!! Binding needs more complicated versions, that may make copies or do
// deep unbinding.  This is just a simple "unbind the tip" operation for now.
// It is permissive to be easy to use.
{
    INCLUDE_PARAMS_OF_UNBIND;

    Stable* v = ARG(VALUE);

    Unbind_Cell_If_Bindable_Core(v);

    /*  // the old code (with :DEEP option)
    if (Any_Word(word) or Is_Set_Word(word))
        Unbind_Any_Word(word);
    else {
        assert(Is_Block(word));

        const Element* tail;
        Element* at = List_At_Ensure_Mutable(&tail, word);
        Option(VarList*) context = nullptr;
        Unbind_Values_Core(at, tail, context, did ARG(DEEP));
    }
    */

    return COPY_TO_OUT(v);
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

    return COPY_TO_OUT(v);
}


//
//  /resolve: native [
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
        return COPY_TO_OUT(source);
    }

    if (Is_Tuple(source)) {
        KIND_BYTE(source) = TYPE_TUPLE;
        return COPY_TO_OUT(source);
    }

    if (Is_Path(source)) {  // !!! For now: (resolve '/a:) -> a
        SingleHeart single;
        if (not (single = opt Try_Get_Sequence_Singleheart(source)))
            panic (source);

        if (
            single == LEADING_BLANK_AND(WORD)  // /a
            or single == LEADING_BLANK_AND(TUPLE)  // /a.b.c or /.a
            or single == TRAILING_BLANK_AND(WORD)  // a/
            or single == TRAILING_BLANK_AND(TUPLE)  // a.b.c/ or .a/
        ){
            assume (
              Unsingleheart_Sequence(source)
            );
            return COPY_TO_OUT(source);
        }
        if (
            single == LEADING_BLANK_AND(CHAIN)  // /a: or /a:b:c or /:a
            or single == TRAILING_BLANK_AND(CHAIN)  // a:/ or a:b:c/ or :a/
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
        single == LEADING_BLANK_AND(WORD)  // a:
        or single == LEADING_BLANK_AND(TUPLE)  // a.b.c:
        or single == TRAILING_BLANK_AND(WORD)  // :a
        or single == TRAILING_BLANK_AND(TUPLE)  // :a.b.c
    ){
        assume (
          Unsingleheart_Sequence(source)
        );
        return COPY_TO_OUT(source);
    }

    panic (source);
}


//
//  /proxy-exports: native [
//
//  "Copy context by setting values in the target from those in the source"
//
//      return: [module!]
//      where "(modified), will also be return result"
//          [module!]
//      source [module!]
//      exports "Which words to export from the source"
//          [block!]
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
              Read_Slot_Meta(Slot_Init_Hack(dest), src)
            );
        }
        else {
            require (
              Read_Slot_Meta(Append_Context(where, symbol), src)
            );
        }
    }

    return COPY_TO_OUT(ARG(WHERE));
}


//
//  /infix?: pure native [
//
//  "non-null if a function that gets first argument before the call"
//
//      return: [logic!]
//      frame [frame!]
//  ]
//
DECLARE_NATIVE(INFIX_Q)
{
    INCLUDE_PARAMS_OF_INFIX_Q;

    Element* frame = ARG(FRAME);
    return LOGIC_OUT(Is_Frame_Infix(frame));
}


//
//  /infix: native [
//
//  "For functions that gets 1st argument from left, e.g (+: infix add/)"
//
//      return: [action! frame!]
//      ^value [action! frame!]
//      :off "Give back a non-infix version of the passed in function"
//      :defer "Allow one full expression on the left to evaluate"
//      :postpone "Allow arbitrary numbers of expressions on left to evaluate"
//  ]
//
DECLARE_NATIVE(INFIX)
{
    INCLUDE_PARAMS_OF_INFIX;

    Copy_Cell(OUT, ARG(VALUE));

    if (ARG(OFF)) {
        if (ARG(DEFER) or ARG(POSTPONE))
            panic (Error_Bad_Refines_Raw());
        Tweak_Frame_Infix_Mode(OUT, PREFIX_0);
    }
    else if (ARG(DEFER)) {  // not OFF, already checked
        if (ARG(POSTPONE))
            panic (Error_Bad_Refines_Raw());
        Tweak_Frame_Infix_Mode(OUT, INFIX_DEFER);
    }
    else if (ARG(POSTPONE)) {  // not OFF or DEFER, we checked
        Tweak_Frame_Infix_Mode(OUT, INFIX_POSTPONE);
    }
    else
        Tweak_Frame_Infix_Mode(OUT, INFIX_TIGHT);

    return OUT;
}


//
//  /vanishable: native [
//
//  "Make function's invocation not turn VOID! results to empty PACK!"
//
//      return: [action! frame!]
//      ^value [action! frame!]
//      :off "Give back non-vanishable version of the passed in function"
//  ]
//
DECLARE_NATIVE(VANISHABLE)
{
    INCLUDE_PARAMS_OF_VANISHABLE;

    Copy_Cell(OUT, ARG(VALUE));

    if (ARG(OFF))
        Clear_Cell_Flag(OUT, WEIRD_VANISHABLE);
    else
        Set_Cell_Flag(OUT, WEIRD_VANISHABLE);

    return OUT;
}


//
//  /vanishable?: pure native [
//
//  "Return if a function naturally suppresses VOID! to HEAVY VOID conversion"
//
//      return: [logic!]
//      action [frame!]
//  ]
//
DECLARE_NATIVE(VANISHABLE_Q)
{
    INCLUDE_PARAMS_OF_VANISHABLE_Q;

    return LOGIC_OUT(Get_Cell_Flag(ARG(ACTION), WEIRD_VANISHABLE));
}


//
//  /pure: native [
//
//  "Modifier: Can't call IMPURE functions or consult non-FINAL external state"
//
//      return: [action! frame!]
//      ^value [action! frame!]
//      :off "Give back non-pure version of the passed in function"
//  ]
//
DECLARE_NATIVE(PURE)
{
    INCLUDE_PARAMS_OF_PURE;

  purify_action_or_frame: {

  // There is not a CELL_FLAG_PURE which means the flag is carried on the
  // Phase (either ParamList or a Details) of a given action/frame.  Currently
  // this mutates that bit, which it probably shouldn't...but making a copy
  // would have cost.  It would be good to know if the function had just been
  // created and had no references yet, to make it safe to mutate.  Or more
  // probably it should be in the spec, perhaps something like #pure.

    Copy_Cell(OUT, ARG(VALUE));

    Phase* phase = Frame_Phase(ARG(VALUE));
    if (Get_Stub_Flag(phase, PHASE_IMPURE))
        panic ("Can't mark impure function pure (use IMPURE:OFF)");

    if (ARG(OFF))
        Clear_Stub_Flag(phase, PHASE_PURE);
    else
        Set_Stub_Flag(phase, PHASE_PURE);

    return OUT;
}}


//
//  /impure: native [
//
//  "Modifier: Non-deterministic or consults non-FINAL external state"
//
//      return: [action! frame!]
//      ^value [action! frame!]
//      :off "Give back non-pure version of the passed in function"
//  ]
//
DECLARE_NATIVE(IMPURE)
{
    INCLUDE_PARAMS_OF_IMPURE;

    Copy_Cell(OUT, ARG(VALUE));

    Phase* phase = Frame_Phase(ARG(VALUE));
    if (Get_Stub_Flag(phase, PHASE_PURE))
        panic ("Can't mark pure function impure (use PURE:OFF)");

    if (ARG(OFF))
        Clear_Stub_Flag(phase, PHASE_IMPURE);
    else
        Set_Stub_Flag(phase, PHASE_IMPURE);

    return OUT;
}


//
//  /identity: vanishable native:intrinsic [  ; vanishable to not distort [1]
//
//  "Returns input value (https://en.wikipedia.org/wiki/Identity_function)"
//
//      return: [any-value?]
//      ^value '[any-value?]
//  ]
//
DECLARE_NATIVE(IDENTITY)  // '<-' defined as non-vanishable version [2]
//
// 1. Being "vanishable" may seem like a weird choice for IDENTITY, as if it
//    makes it "do something" (vanish).  But it's actually *non*-vanishing
//    that "does something", namely convert VOID! to an empty PACK! for the
//    purposes of safety.
//
//    One might complain a bit that if one's goal is to suppress the VOID!
//    to PACK! conversion, then seeing the word IDENTITY in the source isn't
//    the best way to convey that intent to a reader.  That's fair, but if
//    you see a `^` operator in the source you know something is up... and
//    that's probably more common to see.  IDENTITY just has to keep its
//    behavior in sync.
//
//    (Note that GHOSTLY is *not* a synonym for IDENTITY, because it will
//    turn HEAVY VOID into VOID!, which IDENTITY does not do.)
//
// 2. A peculiar definition in the default setup for a *NON*-vanishable
//    identity function is the meaning of `<-` ... this strange choice gives
//    you the ability to annotate when information is flowing leftward:
//
//      https://rebol.metaeducation.com/t/weird-old-idea-for-identity/2165
{
    INCLUDE_PARAMS_OF_IDENTITY;

    Value* v = ARG(VALUE);

    return COPY_TO_OUT(v);
}


//
//  /free: native [
//
//  "Releases the underlying data of a value so it can no longer be accessed"
//
//      return: ~
//      memory [any-series? any-context? handle!]
//  ]
//
DECLARE_NATIVE(FREE)
{
    INCLUDE_PARAMS_OF_FREE;

    Stable* v = ARG(MEMORY);

    if (Any_Context(v) or Is_Handle(v))
        panic ("FREE only implemented for ANY-SERIES? at the moment");

    if (Not_Base_Readable(CELL_PAYLOAD_1(v)))
        panic ("Cannot FREE already freed series");

    Flex* f = Cell_Flex_Ensure_Mutable(v);
    Diminish_Stub(f);
    return TRASH_OUT; // !!! Could return freed value
}


//
//  /free?: native [
//
//  "Tells if data has been released with FREE"
//
//      return: [logic!]
//      value "Returns false if value wouldn't be FREEable (e.g. WORD!)"
//          [any-stable?]
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

    Stable* v = ARG(VALUE);

    if (Is_Null(v))
        return LOGIC_OUT(false);

    if (not Cell_Payload_1_Needs_Mark(v))  // freeables have Flex in payload
        return LOGIC_OUT(false);

    Base* b = CELL_PAYLOAD_1(v);
    if (b == nullptr or not Is_Base_A_Stub(b))
        return LOGIC_OUT(false);  // no decayed pairing form at this time [1]

    if (Not_Stub_Readable(cast(Stub*, b)))
        return LOGIC_OUT(true);

    return LOGIC_OUT(false);
}


//
//  /aliases?: native [
//
//  "Return whether or not the underlying data of one value aliases another"
//
//      return: [logic!]
//      value1 [any-series?]
//      value2 [any-series?]
//  ]
//
DECLARE_NATIVE(ALIASES_Q)
{
    INCLUDE_PARAMS_OF_ALIASES_Q;

    return LOGIC_OUT(Cell_Flex(ARG(VALUE1)) == Cell_Flex(ARG(VALUE2)));
}


//
//  /any-stable?: pure native:intrinsic [
//
//  "Tells you if the argument (taken as meta) is storable in a variable"
//
//      return: [logic!]
//      ^value '[any-value?]
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

    Value* v = ARG(VALUE);

    return LOGIC_OUT(Is_Cell_Stable(v));
}


//
//  /any-value?: pure native:intrinsic [
//
//  "Accepts absolutely any argument state (unstable antiforms included)"
//
//      return: [logic!]
//      ^value  '[any-value?]    ; recursion allowed due to '[...], unchecked!
//  ]
//
DECLARE_NATIVE(ANY_VALUE_Q)  // synonym for internal concept of ANY_ATOM
//
// !!! The automatic typecheckers that are built don't handle unstable
// antiforms at this time.  They need to, so things like this and PACK?
// and FAILURE? don't have to be special cased.
//
// !!! ELEMENT? isn't ANY-ELEMENT?, so should this just be VALUE?  The policy
// for putting ANY- in front of things has been in flux.
{
    INCLUDE_PARAMS_OF_ANY_VALUE_Q;

    return BOUNCE_OKAY;
}


//
//  /any-word?: pure native:intrinsic [
//
//  "!!! Temporary !!! attempt to answer if [word ^word $word @word]"
//
//      return: [logic!]
//      value '[any-stable?]
//  ]
//
DECLARE_NATIVE(ANY_WORD_Q)
//
// !!! Interim exposure of ANY-WORD?
{
    INCLUDE_PARAMS_OF_ANY_WORD_Q;

    Stable* v = ARG(VALUE);

    return LOGIC_OUT(Any_Word(v));
}


//
//  /none?: pure native:intrinsic [
//
//  "Tells you if argument is an ~[]~ antiform, e.g. an empty splice"
//
//      return: [logic!]
//      value '[any-stable?]
//  ]
//
DECLARE_NATIVE(NONE_Q)
{
    INCLUDE_PARAMS_OF_NONE_Q;

    Stable* v = ARG(VALUE);

    return LOGIC_OUT(Is_None(v));
}


//
//  /tripwire?: pure native:intrinsic [
//
//  "Tells you if argument is an ~ antiform, e.g. a tripwire TRASH! form"
//
//      return: [logic!]
//      value '[any-value?]
//  ]
//
DECLARE_NATIVE(TRIPWIRE_Q)
{
    INCLUDE_PARAMS_OF_TRIPWIRE_Q;

    Value* v = Unchecked_ARG(VALUE);

    return LOGIC_OUT(Is_Tripwire(v));
}


//
//  /noop: pure native [  ; native:intrinsic needs at least 1 argument atm
//
//  "Returns TRASH! (alternative to [] branches returning VOID)"  ; [1]
//
//      return: ~
//  ]
//
DECLARE_NATIVE(NOOP)  // lack of a hyphen common, e.g. jQuery.noop
//
// 1. Using something like `switch [x [noop] ...]` vs `switch [x [] ...]` is
//    precisely to avoid the default void behavior of the branch, and generate
//    something more "problematic" that won't just opt out of whatever uses
//    the result of the switch.
{
    INCLUDE_PARAMS_OF_NOOP;

    return TRASH_OUT;
}


//
//  /quasar?: pure native:intrinsic [
//
//  "Tells you if argument is a quasiform space (~)"
//
//      return: [logic!]
//      value '[any-stable?]
//  ]
//
DECLARE_NATIVE(QUASAR_Q)
{
    INCLUDE_PARAMS_OF_QUASAR_Q;

    Stable* v = ARG(VALUE);

    return LOGIC_OUT(Is_Quasar(v));
}


//
//  /space?: pure native:intrinsic [
//
//  "Is VALUE the RUNE! representing a single space character [_]"
//
//      return: [logic!]
//      value '[any-stable?]
//  ]
//
DECLARE_NATIVE(SPACE_Q)
{
    INCLUDE_PARAMS_OF_SPACE_Q;

    Stable* v = ARG(VALUE);

    return LOGIC_OUT(Is_Space(v));
}


//
//  /newline?: pure native:intrinsic [
//
//  "Is VALUE the RUNE! representing a single newline character [#]"
//
//      return: [logic!]
//      value '[any-stable?]
//  ]
//
DECLARE_NATIVE(NEWLINE_Q)
{
    INCLUDE_PARAMS_OF_NEWLINE_Q;

    Stable* v = ARG(VALUE);

    return LOGIC_OUT(Is_Newline(v));
}


//
//  /spaces?: pure native:intrinsic [
//
//  "Is VALUE a RUNE! consisting only of spaces [_ __ ____ _______ ...]"
//
//      return: [logic!]
//      value '[any-stable?]
//  ]
//
DECLARE_NATIVE(SPACES_Q)
{
    INCLUDE_PARAMS_OF_SPACES_Q;

    Stable* v = ARG(VALUE);

    if (not Is_Rune(v))
        return LOGIC_OUT(false);

    const Utf8Byte* utf8 = Cell_Utf8_At(v);
    if (*utf8 == '\0')
        return LOGIC_OUT(false);

    for (; *utf8 != '\0'; ++utf8) {
        if (*utf8 != ' ')
            return LOGIC_OUT(false);
    }

    return LOGIC_OUT(true);
}


//
//  /heavy: pure native:intrinsic [
//
//  "Make the heavy forms of NULL (null in PACK!) and VOID (empty PACK!)"
//
//      return: [any-value? pack!]
//      ^value '[any-value?]
//  ]
//
DECLARE_NATIVE(HEAVY)
{
    INCLUDE_PARAMS_OF_HEAVY;

    Value* v = ARG(VALUE);

    if (Is_Light_Null(v))
        return Init_Heavy_Null(OUT);

    if (Is_Void(v))
        return Init_Heavy_Void(OUT);

    return COPY_TO_OUT(v);
}


//
//  /heavy-null?: pure native:intrinsic [
//
//  "Determine if argument is the heavy form of NULL, ~(~null~)~ antiform"
//
//      return: [logic!]
//      ^value '[any-value?]
//  ]
//
DECLARE_NATIVE(HEAVY_NULL_Q)
{
    INCLUDE_PARAMS_OF_HEAVY_NULL_Q;

    Value* v = ARG(VALUE);

    return LOGIC_OUT(Is_Heavy_Null(v));
}


//
//  /light: pure native:intrinsic [
//
//  "Make the light form of NULL (passes through all other values)"
//
//      return: [any-value?]
//      ^value '[any-value?]
//  ]
//
DECLARE_NATIVE(LIGHT)
{
    INCLUDE_PARAMS_OF_LIGHT;

    Value* v = ARG(VALUE);

    if (not Is_Pack(v))
        return COPY_TO_OUT(v);

    Length len;
    const Element* first = List_Len_At(&len, v);

    if (len != 1)
        return COPY_TO_OUT(v);

    if (Is_Lifted_Null(first))  // only case we care about, pack of one null
        return NULL_OUT;  // return the null, no longer in a pack

    return COPY_TO_OUT(v);
}


//
//  /decay: native:intrinsic [
//
//  "Handle unstable isotopes like assignments do, pass through other values"
//
//      return: [any-stable?]
//      value '[any-stable?]
//  ]
//
DECLARE_NATIVE(DECAY)
{
    INCLUDE_PARAMS_OF_DECAY;

    Stable* v = ARG(VALUE);

    return COPY_TO_OUT(v);
}


//
//  /decayable?: native:intrinsic [
//
//  "Answer if a value is decayable"
//
//      return: [logic!]
//      ^value '[any-value?]
//  ]
//
DECLARE_NATIVE(DECAYABLE_Q)
{
    INCLUDE_PARAMS_OF_DECAYABLE_Q;

    Value* v = ARG(VALUE);

    Ensure_No_Failures_Including_In_Packs(v) except (Error* e) {
        UNUSED(e);
        return LOGIC_OUT(false);
    }

    return LOGIC_OUT(true);
}


//
//  /reify: native [  ; !!! selectively decays, can't be intrinsic
//
//  "Make antiforms into their quasiforms, quote all other values"
//
//      return: [element? failure!]
//      ^value "PACK!s decayed, and FAILURE! is passed through"
//         '[any-value?]
//  ]
//
DECLARE_NATIVE(REIFY)
//
// There isn't a :NOQUASI refinement to REIFY so it can be an intrinsic.  This
// speeds up all REIFY operations, and (noquasi reify ...) will be faster
// than (reify:noquasi ...).  (Note: intrinsic temporarily not possible.)
{
    INCLUDE_PARAMS_OF_REIFY;

    Value* v = ARG(VALUE);
    if (Is_Failure(v))
        return COPY_TO_OUT(v);

    if (Is_Pack(v)) {
      require(
        Decay_If_Unstable(v)
      );
    }

    Copy_Cell(OUT, v);
    return Reify_If_Antiform(OUT);
}


//
//  /noquasi: pure native:intrinsic [
//
//  "Make quasiforms into their plain forms, pass through all other elements"
//
//      return: [element?]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(NOQUASI)
{
    INCLUDE_PARAMS_OF_NOQUASI;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    Copy_Cell(OUT, v);
    if (LIFT_BYTE(OUT) == QUASIFORM_4)
        LIFT_BYTE(OUT) = NOQUOTE_3;
    return OUT;
}


//
//  /degrade: native [
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

    Element* elem = ARG(VALUE);
    if (not Is_Quasiform(elem))
        return COPY_TO_OUT(elem);

    Copy_Cell(OUT, elem);

    require (
      Coerce_To_Antiform(OUT)
    );
    return OUT;
}


//
//  /noantiform: native:intrinsic [
//
//  "Turn antiforms into their plain forms, pass thru other values"
//
//      return: [element?]
//      value '[any-stable?]
//  ]
//
DECLARE_NATIVE(NOANTIFORM)
{
    INCLUDE_PARAMS_OF_NOANTIFORM;

    Stable* v = ARG(VALUE);

    if (Is_Antiform(v))
        LIFT_BYTE(v) = NOQUOTE_3;
    return COPY_TO_OUT(v);
}
