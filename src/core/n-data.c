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


static bool Check_Char_Range(const Value* val, Codepoint limit)
{
    if (IS_CHAR(val))
        return Cell_Codepoint(val) <= limit;

    if (Is_Integer(val))
        return VAL_INT64(val) <= cast(REBI64, limit);

    assert(Any_String(val));

    REBLEN len;
    Utf8(const*) up = Cell_Utf8_Len_Size_At(&len, nullptr, val);

    for (; len > 0; len--) {
        Codepoint c;
        up = Utf8_Next(&c, up);

        if (c > limit)
            return false;
    }

    return true;
}


//
//  /ascii?: native [
//
//  "Returns TRUE if value or string is in ASCII character range (below 128)"
//
//      return: [logic?]
//      value [any-string? char? integer!]
//  ]
//
DECLARE_NATIVE(ascii_q)
{
    INCLUDE_PARAMS_OF_ASCII_Q;

    return Init_Logic(OUT, Check_Char_Range(ARG(value), 0x7f));
}


//
//  /latin1?: native [
//
//  "Returns TRUE if value or string is in Latin-1 character range (below 256)"
//
//      return: [logic?]
//      value [any-string? char? integer!]
//  ]
//
DECLARE_NATIVE(latin1_q)
{
    INCLUDE_PARAMS_OF_LATIN1_Q;

    return Init_Logic(OUT, Check_Char_Range(ARG(value), 0xff));
}


//
//  /as-pair: native [
//
//  "Combine X and Y values into a pair"
//
//      return: [pair!]
//      x [integer!]
//      y [integer!]
//  ]
//
DECLARE_NATIVE(as_pair)
{
    INCLUDE_PARAMS_OF_AS_PAIR;

    return Init_Pair(OUT, VAL_INT64(ARG(x)), VAL_INT64(ARG(y)));
}


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
            fail (Error_Not_Bound_Raw(target));

        fail ("Binding to WORD! only implemented via INSIDE at this time.");
    }

    if (Any_Wordlike(v)) {
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

        fail (Error_Not_In_Context_Raw(v));
    }

    if (not Any_Listlike(v))  // QUOTED? could have wrapped any type
        fail (Error_Invalid_Arg(level_, PARAM(value)));

    Element* at;
    const Element* tail;
    if (REF(copy)) {
        bool deeply = true;
        Array* copy = Copy_Array_Core_Managed(
            Cell_Array(v),
            VAL_INDEX(v), // at
            Array_Len(Cell_Array(v)), // tail
            0, // extra
            ARRAY_MASK_HAS_FILE_LINE, // flags
            deeply  // !!! types to copy deeply (was once just TS_ARRAY)
        );
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
            fail (Error_Not_Bound_Raw(defs));
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

    assert(Any_List(v));
    Virtual_Bind_Deep_To_Existing_Context(v, ctx, nullptr, CELL_MASK_0);
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
bool Try_Get_Binding_Of(Sink(Value*) out, const Value* v)
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
        Is_Path(arg) and Try_Get_Settable_Word_Symbol(cast(Element*, arg))
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
//  /set-accessor: native [
//
//  "Put a function in charge of getting/setting a variable's value"
//
//      return: [~]
//      var [word!]
//      action [action?]
//  ]
//
DECLARE_NATIVE(set_accessor)
//
// 1. While Get_Var()/Set_Var() and their variants are specially written to
//    know about accessors, lower level code is not.  Only code that is
//    sensitive to the fact that the cell contains an accessor should be
//    dealing with the raw cell.  We use the read and write protection
//    abilities to catch violators.
{
    INCLUDE_PARAMS_OF_SET_ACCESSOR;

    Element* word = cast(Element*, ARG(var));
    Value* action = ARG(action);

    Value* var = Lookup_Mutable_Word_May_Fail(word, SPECIFIED);
    Copy_Cell(var, action);
    Set_Cell_Flag(var, VAR_NOTE_ACCESSOR);

    Set_Cell_Flag(var, PROTECTED);  // help trap unintentional writes [1]
    Set_Node_Free_Bit(var);  // help trap unintentional reads [1]

    return NOTHING;
}


// This is the core implementation of Trap_Get_Any_Word(), that allows being
// called on "wordlike" sequences (like `.a` or `a/`).  But it should really
// only be called by things like Trap_Get_Any_Tuple(), because there are no
// special adjustments for sequences like `.a`
//
static Option(Error*) Trap_Get_Any_Wordlike_Maybe_Vacant(
    Sink(Value*) out,
    const Element* word,  // sigils ignored (META-WORD! doesn't "meta-get")
    Context* context  // context for `.xxx` tuples not adjusted
){
    assert(Any_Wordlike(word));

    const Value* lookup;
    Option(Error*) error = Trap_Lookup_Word(&lookup, word, context);
    if (error)
        return error;

    if (not (lookup->header.bits & CELL_FLAG_VAR_NOTE_ACCESSOR)) {
        Copy_Cell(out, lookup);  // non-accessor variable, just plain value
        return nullptr;
    }

    assert(HEART_BYTE(lookup) == REB_FRAME);  // alias accessors as WORD! ?
    assert(QUOTE_BYTE(lookup) == ANTIFORM_0);

    DECLARE_ELEMENT (accessor);
    Push_GC_Guard(accessor);
    accessor->header.bits |= (
        NODE_FLAG_NODE | NODE_FLAG_CELL  // ensure NODE+CELL
        | (lookup->header.bits & CELL_MASK_COPY & (~ NODE_FLAG_FREE))
    );
    accessor->extra = lookup->extra;
    accessor->payload = lookup->payload;
    QUOTE_BYTE(accessor) = NOQUOTE_1;

    bool threw = rebRunThrows(out, accessor);  // run accessor as GET
    Drop_GC_Guard(accessor);
    if (threw)
        return Error_No_Catch_For_Throw(TOP_LEVEL);
    return nullptr;
}


//
//  Trap_Get_Any_Word: C
//
// This is the "high-level" chokepoint for looking up a word and getting a
// value from it.  If the word is bound to a "getter" slot, then this will
// actually run a function to retrieve the value.  For that reason, almost
// all code should be going through this layer (or higher) when fetching an
// ANY-WORD! variable.
//
Option(Error*) Trap_Get_Any_Word(
    Sink(Value*) out,
    const Element* word,  // sigils ignored (META-WORD! doesn't "meta-get")
    Context* context
){
    Option(Error*) error = Trap_Get_Any_Wordlike_Maybe_Vacant(
        out, word, context
    );
    if (error)
        return error;

    if (Any_Vacancy(out))
        return Error_Bad_Word_Get(word, out);

    return nullptr;
}


//
//  Trap_Get_Any_Word_Maybe_Vacant: C
//
// High-level: see notes on Trap_Get_Any_Word().  This version just gives back
// "nothing" (antiform blank) or "tripwire" (antiform tag) vs. give an error.
//
Option(Error*) Trap_Get_Any_Word_Maybe_Vacant(
    Sink(Value*) out,
    const Element* word,  // sigils ignored (META-WORD! doesn't "meta-get")
    Context* context
){
    assert(Any_Word(word));
    return Trap_Get_Any_Wordlike_Maybe_Vacant(out, word, context);
}


//
//  Trap_Get_From_Steps_On_Stack_Maybe_Vacant: C
//
// The GET and SET operations are able to tolerate :GROUPS, whereby you can
// run somewhat-arbitrary code that appears in groups in tuples.  This can
// mean that running GET on something and then SET on it could run that code
// twice.  If you want to avoid that, a sequence of :STEPS can be requested
// that can be used to find the same location after initially calculating
// the groups, without doubly evaluating.
//
// This is a common service routine used for both tuples and "step lists",
// which uses the stack (to avoid needing to generate an intermediate array
// in the case evaluations were performed).
//
Option(Error*) Trap_Get_From_Steps_On_Stack_Maybe_Vacant(
    Sink(Value*) out,
    StackIndex base
){
    StackIndex stackindex = base + 1;

  blockscope {
    StackValue(*) at = Data_Stack_At(stackindex);
    assert(not Is_Antiform(at));
    if (Is_Quoted(at)) {
        Copy_Cell(out, at);
        Unquotify(out, 1);
    }
    else if (Is_Word(at)) {
        const Value* slot;
        Option(Error*) error = Trap_Lookup_Word(
            &slot, cast(Element*, at), SPECIFIED
        );
        if (error)
            fail (unwrap error);
        Copy_Cell(out, slot);
    }
    else
        fail (Copy_Cell(out, at));
  }

    ++stackindex;

    DECLARE_ATOM (temp);
    Push_GC_Guard(temp);

    while (stackindex != TOP_INDEX + 1) {
        Move_Cell(temp, out);
        QUOTE_BYTE(temp) = ONEQUOTE_3;
        const Node* ins = rebQ(cast(Value*, Data_Stack_At(stackindex)));
        if (rebRunCoreThrows_internal(
            out,  // <-- output cell
            EVAL_EXECUTOR_FLAG_NO_RESIDUE
                | LEVEL_FLAG_UNINTERRUPTIBLE
                | LEVEL_FLAG_RAISED_RESULT_OK,
            Canon(PICK_P), temp, ins
        )){
            Drop_Data_Stack_To(base);
            Drop_GC_Guard(temp);
            return Error_No_Catch_For_Throw(TOP_LEVEL);
        }

        if (Is_Raised(cast(Atom*, out))) {
            Error* error = Cell_Error(out);  // extract error
            bool last_step = (stackindex == TOP_INDEX);
            Erase_Cell(out);  // suppress assert about unhandled raised error

            Drop_Data_Stack_To(base);  // Note: changes TOP_INDEX
            Drop_GC_Guard(temp);
            if (last_step)
                return error;  // last step, interceptible error
            fail (error);  // intermediate step, must abrupt fail
        }

        if (Is_Antiform(cast(Atom*, out)))
            assert(not Is_Antiform_Unstable(cast(Atom*, out)));

        ++stackindex;
    }

    Drop_GC_Guard(temp);
    return nullptr;
}


// Ren-C injects the object from which a function was dispatched in a path
// into the function call, as something called a "coupling".  This coupling is
// tied in with the FRAME! for the function call, and can be used as a context
// to do special lookups in.
//
static Context* Adjust_Context_For_Coupling(Context* c) {
    for (; c != nullptr; c = Context_Parent(c)) {
        VarList* frame_varlist;
        if (Is_Stub_Varlist(c)) {  // ordinary FUNC frame context
            frame_varlist = cast(VarList*, c);
            if (CTX_TYPE(frame_varlist) != REB_FRAME)
                continue;
        }
        else if (Is_Stub_Use(c)) {  // e.g. LAMBDA or DOES uses this
            if (not Is_Frame(Stub_Cell(c)))
                continue;
            frame_varlist = Cell_Varlist(Stub_Cell(c));
        }
        else
            continue;

        Level* level = Level_Of_Varlist_If_Running(frame_varlist);
        if (not level)
            return Error_User(
                ".field access only in running functions"
            );
        VarList* coupling = maybe Level_Coupling(level);
        if (not coupling)
            return Error_User(
                ".field object used on frame with no coupling"
            );
        return coupling;
    }
    fail (".field access used but no coupling found");
}


//
//  Trap_Get_Any_Tuple_Maybe_Vacant: C
//
// 1. Using a leading dot in a tuple is a cue to look up variables in the
//    object from which a function was dispatched, so `var` and `.var` can
//    look up differently inside a function's body.
//
Option(Error*) Trap_Get_Any_Tuple_Maybe_Vacant(
    Sink(Value*) out,
    Option(Value*) steps_out,  // if NULL, then GROUP!s not legal
    const Element* tuple,
    Context* context
){
    assert(Any_Tuple(tuple));

    if (Not_Cell_Flag(tuple, SEQUENCE_HAS_NODE))  // byte compressed
        return Error_User("Cannot GET a numeric tuple");

    bool dot_at_head;  // dot at head means look in coupled context
    DECLARE_ELEMENT (detect);
    Copy_Sequence_At(detect, tuple, 0);
    if (Is_Blank(detect))
        dot_at_head = true;
    else
        dot_at_head = false;

    if (dot_at_head)  // avoid adjust if tuple has non-cache binding?
        context = Adjust_Context_For_Coupling(context);

  //=//// HANDLE SIMPLE "WORDLIKE" CASE (.a or a.) ////////////////////////=//

    const Node* node1 = Cell_Node1(tuple);
    if (Is_Node_A_Cell(node1)) { // pair compressed
        // is considered "Listlike", can answer Cell_List_At()
    }
    else switch (Stub_Flavor(x_cast(Flex*, node1))) {
      case FLAVOR_SYMBOL: {
        Option(Error*) error = Trap_Get_Any_Wordlike_Maybe_Vacant(
            out,
            tuple,  // optimized "wordlike" representation, like a. or .a
            context
        );
        if (error)
            return error;
        if (steps_out and steps_out != GROUPS_OK) {
            Derelativize(unwrap steps_out, tuple, context);
            HEART_BYTE(unwrap steps_out) = REB_THE_TUPLE;  // REB_THE_WORD ?
        }
        return nullptr; }

      case FLAVOR_ARRAY:
        break;

      default:
        panic (tuple);
    }

  //=//// PUSH PROCESSED TUPLE ELEMENTS TO STACK //////////////////////////=//

    // The tuple may contain GROUP!s that we evaluate.  Rather than process
    // tuple elements directly, we push their possibly-evaluated elements to
    // the stack.  This way we can share code with the "sequence of steps"
    // formulation of tuple processing.
    //
    // 1. By convention, picker steps quote the first item if it was a GROUP!.
    //    It has to be somehow different because `('a).b` is trying to pick B
    //    out of the WORD! a...not out of what's fetched from A.  So if the
    //    first item of a "steps" block needs to be "fetched" we ^META it.

    StackIndex base = TOP_INDEX;

    const Element* tail;
    const Element* head = Cell_List_At(&tail, tuple);
    const Element* at;
    Context* at_binding = Derive_Binding(context, tuple);
    for (at = head; at != tail; ++at) {
        if (Is_Group(at)) {
            if (not steps_out)
                return Error_User("GET:GROUPS must be used to eval in GET");

            if (Eval_Any_List_At_Throws(cast(Atom*, out), at, at_binding)) {
                Drop_Data_Stack_To(base);
                return Error_No_Catch_For_Throw(TOP_LEVEL);
            }
            Decay_If_Unstable(cast(Atom*, out));

            if (Is_Antiform(out))
                fail (Error_Bad_Antiform(out));  // can't PICK on antifoms

            Move_Cell(PUSH(), out);
            if (at == head)
                Quotify(TOP, 1);  // signify not literal
        }
        else  // Note: must keep words at head as-is for writeback!
            Derelativize(PUSH(), at, at_binding);
    }

  //=//// CALL COMMON CODE TO RUN CHAIN OF PICKS //////////////////////////=//

    // The behavior of getting a TUPLE! is generalized, and based on PICK.  So
    // in theory, as types in the system are extended, they only need to
    // implement PICK in order to have tuples work with them.

    Option(Error*) error = Trap_Get_From_Steps_On_Stack_Maybe_Vacant(
        out, base
    );
    if (error) {
        Drop_Data_Stack_To(base);
        return error;
    }

    if (steps_out and steps_out != GROUPS_OK) {
        Array* a = Pop_Stack_Values(base);
        Init_Any_List(unwrap steps_out, REB_THE_BLOCK, a);
    }
    else
        Drop_Data_Stack_To(base);

    return nullptr;
}


//
//  Trap_Get_Any_Tuple: C
//
// Convenience wrapper for getting tuples that errors on nothing and tripwires.
//
Option(Error*) Trap_Get_Any_Tuple(
    Sink(Value*) out,
    Option(Value*) steps_out,  // if NULL, then GROUP!s not legal
    const Element* tuple,
    Context* context
){
    Option(Error*) error = Trap_Get_Any_Tuple_Maybe_Vacant(
        out, steps_out, tuple, context
    );
    if (error)
        return error;

    if (Any_Vacancy(out))
        return Error_Bad_Word_Get(tuple, out);

    return nullptr;
}


//
//  Trap_Get_Var_Maybe_Vacant: C
//
// This is a generalized service routine for getting variables that will
// specialize paths into concrete actions.
//
// 1. This specialization process has cost.  So if you know you have a path in
//    your hand--and all you plan to do with the result after getting it is
//    to execute it--then use Trap_Get_Path_Push_Refinements() instead of
//    this function, and then let the Action_Executor() use the refinements
//    on the stack directly.  That avoids making an intermediate action.
//
Option(Error*) Trap_Get_Var_Maybe_Vacant(
    Sink(Value*) out,
    Option(Value*) steps_out,  // if NULL, then GROUP!s not legal
    const Element* var,
    Context* context
){
    assert(var != cast(Cell*, out));
    assert(steps_out != out);  // Legal for SET, not for GET

    if (Any_Word(var)) {
        Option(Error*) error = Trap_Get_Any_Wordlike_Maybe_Vacant(
            out, var, context
        );
        if (error)
            return error;

        if (steps_out and steps_out != GROUPS_OK) {
            Derelativize(unwrap steps_out, var, context);
            HEART_BYTE(unwrap steps_out) = REB_THE_WORD;
        }
        return nullptr;
    }

    if (
        Any_Chain(var)
        or Any_Path(var)  // META-PATH! is not META'd, all act the same
    ){
        StackIndex base = TOP_INDEX;

        DECLARE_ATOM (safe);
        Push_GC_Guard(safe);

        Option(Error*) error;
        if (Is_Chain(var))
            error = Trap_Get_Chain_Push_Refinements(
                out, safe, var, context
            );
        else
            error = Trap_Get_Path_Push_Refinements(
                out, safe, var, context
            );
        Drop_GC_Guard(safe);

        if (error)
            return error;

        assert(Is_Action(out));

        DECLARE_VALUE (action);
        Move_Cell(action, out);
        Deactivate_If_Action(action);

        Option(Value*) def = nullptr;  // !!! EMPTY_BLOCK causes problems, why?
        bool threw = Specialize_Action_Throws(  // has cost, try to avoid [1]
            out, action, def, base
        );
        assert(not threw);  // can only throw if `def`
        UNUSED(threw);

        if (steps_out and steps_out != GROUPS_OK)
            Init_Nothing(unwrap steps_out);  // !!! What to return?

        return nullptr;
    }

    if (Any_Tuple(var))
        return Trap_Get_Any_Tuple_Maybe_Vacant(
            out, steps_out, var, context
        );

    if (Is_The_Block(var)) {  // "steps"
        StackIndex base = TOP_INDEX;

        Context* at_binding = Derive_Binding(context, var);
        const Element* tail;
        const Element* head = Cell_List_At(&tail, var);
        const Element* at;
        for (at = head; at != tail; ++at)
            Derelativize(PUSH(), at, at_binding);

        Option(Error*) error = Trap_Get_From_Steps_On_Stack_Maybe_Vacant(
            out, base
        );
        Drop_Data_Stack_To(base);

        if (error)
            return error;

        if (steps_out and steps_out != GROUPS_OK)
            Copy_Cell(unwrap steps_out, var);

        return nullptr;
    }

    fail (var);
}


//
//  Trap_Get_Var: C
//
// May generate specializations for paths.  See Trap_Get_Var_Maybe_Vacant()
//
Option(Error*) Trap_Get_Var(
    Sink(Value*) out,
    Option(Value*) steps_out,  // if nullptr, then GROUP!s not legal
    const Element* var,
    Context* context
){
    Option(Error*) error = Trap_Get_Var_Maybe_Vacant(
        out, steps_out, var, context
    );
    if (error)
        return error;

    if (Any_Vacancy(out))
        return Error_Bad_Word_Get(var, out);

    return nullptr;
}


//
//  Get_Var_May_Fail: C
//
// Simplest interface.  Gets a variable, doesn't process groups, and will
// fail if the variable is vacant (holding nothing or a tripwire).  Use the
// appropriate Trap_Get_XXXX() interface if this is too simplistic.
//
Value* Get_Var_May_Fail(
    Sink(Value*) out,  // variables never store unstable Atom* values
    const Element* var,
    Context* context
){
    Value* steps_out = nullptr;  // signal groups not allowed to run

    Option(Error*) error = Trap_Get_Var(  // vacant will give error
        out, steps_out, var, context
    );
    if (error)
        fail (unwrap error);

    assert(not Any_Vacancy(out));  // shouldn't have returned it
    return out;
}


//
//  Trap_Get_Chain_Push_Refinements: C
//
Option(Error*) Trap_Get_Chain_Push_Refinements(
    Sink(Value*) out,
    Sink(Value*) spare,
    const Element* chain,
    Context* context
){
  #if DEBUG
  blockscope {
    bool leading_blank;
    Option(Heart) heart = Try_Get_Sequence_Singleheart(&leading_blank, chain);
    assert(not heart);  // don't call with chains that start or end with blank
    UNUSED(leading_blank);
  }
  #endif

    const Element* tail;
    const Element* head = Cell_List_At(&tail, chain);

    Context* derived = Derive_Binding(context, chain);

    // The first item must resolve to an action.

    if (Is_Group(head)) {  // historical Rebol didn't allow group at head
        if (Eval_Value_Throws(out, head, derived))
            return Error_No_Catch_For_Throw(TOP_LEVEL);
    }
    else if (Is_Tuple(head)) {
        fail ("TUPLE! in CHAIN! not supported at this time");
    /*
        DECLARE_VALUE (steps);
        Option(Error*) error = Trap_Get_Any_Tuple(  // vacant is error
            out, steps, head, derived
        );
        if (error)
            fail (unwrap error);  // must be abrupt
    */
    }
    else if (Is_Word(head)) {
        Option(Error*) error = Trap_Get_Any_Word(out, head, derived);
        if (error)
            fail (unwrap error);  // must be abrupt
    }
    else
        fail (head);  // what else could it have been?

    ++head;

    if (Is_Action(out))
        NOOP;  // it's good
    else if (Is_Antiform(out))
        return Error_Bad_Antiform(out);
    else if (Is_Frame(out))
        Actionify(out);
    else
        return Error_User("Head of CHAIN! did not evaluate to an ACTION!");

    // We push the remainder of the chain in *reverse order* as words to act
    // as refinements to the function.  The action execution machinery will
    // decide if they are valid or not.
    //
    const Value* at = tail - 1;

    for (; at != head - 1; --at) {
        assert(not Is_Blank(at));  // no internal blanks

        const Value* item = at;
        if (Is_Group(at)) {
            if (Eval_Value_Throws(
                cast(Atom*, spare),
                c_cast(Element*, at),
                Derive_Binding(derived, at)
            )){
                return Error_No_Catch_For_Throw(TOP_LEVEL);
            }
            item = Decay_If_Unstable(cast(Atom*, spare));

            if (Is_Void(item))
                continue;  // just skip it (voids are ignored, NULLs error)

            if (Is_Antiform(item))
                return Error_Bad_Antiform(item);
        }

        if (Is_Word(item)) {
            Init_Pushed_Refinement(PUSH(), Cell_Word_Symbol(item));
        }
        else
            fail (item);
    }

    return nullptr;
}


//
//  Trap_Get_Path_Push_Refinements: C
//
// This form of Get_Path() is low-level, and may return a non-ACTION! value
// if the path is inert (e.g. `/abc` or `.a.b/c/d`).
//
Option(Error*) Trap_Get_Path_Push_Refinements(
    Sink(Value*) out,
    Sink(Value*) safe,
    const Element* path,
    Context* context
){
    UNUSED(safe);

    if (Not_Cell_Flag(path, SEQUENCE_HAS_NODE)) {  // byte compressed
        Copy_Cell(out, path);
        goto ensure_out_is_action;  // will fail, it's not an action

      ensure_out_is_action: //////////////////////////////////////////////////

        if (Is_Action(out))
            return nullptr;
        if (Is_Frame(out)) {
            Actionify(out);
            return nullptr;
        }
        fail ("PATH! must retrieve an action or frame");
    }

    const Node* node1 = Cell_Node1(path);
    if (Is_Node_A_Cell(node1)) {
        // pairing, but "Listlike", so Cell_List_At() will work on it
    }
    else switch (Stub_Flavor(c_cast(Flex*, node1))) {
      case FLAVOR_SYMBOL : {  // `/a` or `a/`
        Option(Error*) error = Trap_Get_Any_Word(out, path, context);
        if (error)
            return error;

        goto ensure_out_is_action; }

      case FLAVOR_ARRAY : {}
        break;

      default :
        panic (path);
    }

    const Element* tail;
    const Element* at = Cell_List_At(&tail, path);

    Context* derived = Derive_Binding(context, path);

    if (Is_Blank(at)) {  // leading slash means execute (but we're GET-ing)
        ++at;
        assert(not Is_Blank(at));  // two blanks would be `/` as WORD!
    }

    if (Is_Group(at)) {
        if (Eval_Value_Throws(out, at, derived))
            return Error_No_Catch_For_Throw(TOP_LEVEL);
    }
    else if (Is_Tuple(at)) {
        DECLARE_VALUE (steps);
        Option(Error*) error = Trap_Get_Any_Tuple(  // vacant is error
            out, steps, at, derived
        );
        if (error)
            fail (unwrap error);  // must be abrupt
    }
    else if (Is_Word(at)) {
        Option(Error*) error = Trap_Get_Any_Word(out, at, derived);
        if (error)
            fail (unwrap error);  // must be abrupt
    }
    else if (Is_Chain(at)) {
        if ((at + 1 != tail) and not Is_Blank(at + 1))
            fail ("CHAIN! can only be last item in a path right now");
        Option(Error*) error = Trap_Get_Chain_Push_Refinements(
            out,
            safe,
            c_cast(Element*, at),
            Derive_Binding(derived, at)
        );
        if (error)
            return error;
        return nullptr;
    }
    else
        fail (at);  // what else could it have been?

    ++at;

    if (at == tail or Is_Blank(at))
        goto ensure_out_is_action;

    if (at + 1 != tail and not Is_Blank(at + 1))
        fail ("PATH! can only be two items max at this time");

    // When we see `lib/append` for instance, we want to pick APPEND out of
    // LIB and make sure it is an action.
    //
    if (Any_Context(out)) {
        if (Is_Chain(at)) {  // lib/append:dup
            Option(Error*) error = Trap_Get_Chain_Push_Refinements(
                out,
                safe,
                c_cast(Element*, at),
                Cell_Varlist(out)  // need to find head of chain in object
            );
            if (error)
                return error;
            return nullptr;
        }

        Quotify(out, 1);  // may be FRAME!, would run if seen literally in EVAL

        DECLARE_ATOM (temp);
        if (rebRunThrows(
            cast(RebolValue*, temp),
            Canon(PICK_P),
            cast(const RebolValue*, out),  // was quoted above
            rebQ(cast(const RebolValue*, at)))  // Cell, but is Element*
        ){
            return Error_No_Catch_For_Throw(TOP_LEVEL);
        }
        Copy_Cell(out, Decay_If_Unstable(temp));
    }
    else
        fail (path);

    goto ensure_out_is_action;
}


//
//  /resolve: native [
//
//  "Produce an invariant list structure for doing multiple GET or SET from"
//
//      return: [~[[the-word! the-tuple! the-block!] any-value?]~]
//      source [any-word? any-sequence? any-group?
//          set-word? set-tuple? set-group?
//          get-word? get-tuple? get-group?]
//  ]
//
DECLARE_NATIVE(resolve)
//
// Note: Originally, GET and SET were multi-returns, giving back a second
// parameter of "steps".  Variables couldn't themselves hold packs, so it
// seemed all right to use a multi-return.  But it complicated situations
// where people wanted to write META GET.  RESOLVE is a pretty rarely-used
// facility...and making GET and SET harder to work with brings pain points
// to everyday code.
{
    INCLUDE_PARAMS_OF_RESOLVE;

    Element* source = cast(Element*, ARG(source));
    if (Is_Chain(source))  // a: or a.b/ or .(a b) etc.
        Unchain(source);

    Option(Error*) error = Trap_Get_Var(
        OUT, stable_SPARE, source, SPECIFIED
    );
    if (error)
        fail (unwrap error);

    Array* pack = Make_Array_Core(2, NODE_FLAG_MANAGED);
    Set_Flex_Len(pack, 2);

    Copy_Meta_Cell(Array_At(pack, 0), stable_SPARE);  // the steps
    Copy_Meta_Cell(Array_At(pack, 1), stable_OUT);  // the value

    return Init_Pack(OUT, pack);
}


//
//  /get: native [
//
//  "Gets the value of a word or path, or block of words/paths"
//
//      return: [any-value?]
//      source "Word or tuple to get, or block of PICK steps (see RESOLVE)"
//          [<maybe> any-word? any-sequence? any-group? any-chain? the-block!]
//      :any "Do not error on unset words"
//      :groups "Allow GROUP! Evaluations"
//  ]
//
DECLARE_NATIVE(get)
{
    INCLUDE_PARAMS_OF_GET;

    Element* source = cast(Element*, ARG(source));
    if (Any_Chain(source)) {  // GET-WORD, SET-WORD, SET-GROUP, etc.
        bool leading_blank;
        Option(Heart) heart = Try_Get_Sequence_Singleheart(
            &leading_blank, source
        );
        if (heart)
            Unchain(source);  // want to GET or SET normally
    }

    Value* steps;
    if (REF(groups))
        steps = GROUPS_OK;
    else
        steps = nullptr;  // no GROUP! evals

    if (Any_Group(source)) {  // !!! GET-GROUP! makes sense, but SET-GROUP!?
        if (not REF(groups))
            fail (Error_Bad_Get_Group_Raw(source));

        if (steps != GROUPS_OK)
            fail ("GET on GROUP! with steps doesn't have answer ATM");

        if (Eval_Any_List_At_Throws(SPARE, source, SPECIFIED))
            return Error_No_Catch_For_Throw(LEVEL);

        Decay_If_Unstable(SPARE);

        if (Is_Void(SPARE))
            return nullptr;  // !!! Is this a good idea, or should it error?

        if (not (
            Any_Word(SPARE) or Any_Sequence(SPARE) or Is_The_Block(SPARE))
        ){
            fail (SPARE);
        }

        source = cast(Element*, SPARE);
    }

    Option(Error*) error = Trap_Get_Var_Maybe_Vacant(
        OUT, steps, source, SPECIFIED
    );
    if (error)
        return RAISE(unwrap error);

    if (not REF(any))
        if (Any_Vacancy(stable_OUT))
            return RAISE(Error_Bad_Word_Get(source, stable_OUT));

    return OUT;
}


//
//  Set_Var_Core_Updater_Throws: C
//
// This is centralized code for setting variables.  If it returns `true`, the
// out cell will contain the thrown value.  If it returns `false`, the out
// cell will have steps with any GROUP!s evaluated.
//
// It tries to improve efficiency by handling cases that don't need methodized
// calling of POKE* up front.  If a frame is needed, then it leverages that a
// frame with pushed cells is available to avoid needing more temporaries.
//
// **Almost all parts of the system should go through this code for assignment,
// even when they know they have just a WORD! in their hand and don't need path
// dispatch.**  Only a few places bypass this code for reasons of optimization,
// but they must do so carefully.
//
// It is legal to have `target == out`.  It means the target may be overwritten
// in the course of the assignment.
//
bool Set_Var_Core_Updater_Throws(
    Sink(Value*) out,  // GC-safe cell to write steps to, or put thrown value
    Option(Value*) steps_out,  // no GROUP!s if nulled
    const Element* var,
    Context* context,
    const Value* setval,  // e.g. L->out (in the evaluator, right hand side)
    const Value* updater
){
    // Note: `steps_out` can be equal to `out` can be equal to `target`

    Assert_Cell_Stable(setval);  // paranoid check (Value* means stable)

    DECLARE_ATOM (temp);  // target might be same as out (e.g. spare)

    Heart var_heart = Cell_Heart(var);

    if (Any_Word_Kind(var_heart)) {

      set_target:

        if (not updater or VAL_ACTION(updater) == VAL_ACTION(Lib(POKE_P))) {
            //
            // Shortcut past POKE for WORD! (though this subverts hijacking,
            // review that case.)
            //
            Copy_Cell(
                Sink_Word_May_Fail(var, context),
                setval
            );
        }
        else {
            // !!! This is a hack to try and get things working for PROTECT*.
            // Things are in roughly the right place, but very shaky.  Revisit
            // as BINDING OF is reviewed in terms of answers for LET.
            //
            Derelativize(temp, var, context);
            QUOTE_BYTE(temp) = ONEQUOTE_3;
            Push_GC_Guard(temp);
            if (rebRunThrows(
                out,  // <-- output cell
                rebRUN(updater), "binding of", temp, temp, rebQ(setval)
            )){
                Drop_GC_Guard(temp);
                fail (Error_No_Catch_For_Throw(TOP_LEVEL));
            }
            Drop_GC_Guard(temp);
        }

        if (steps_out and steps_out != GROUPS_OK) {
            if (steps_out != var)  // could be true if GROUP eval
                Derelativize(unwrap steps_out, var, context);

            // If the variable is a compressed path form like `a.` then turn
            // it into a plain word.
            //
            HEART_BYTE(unwrap steps_out) = REB_WORD;
        }
        return false;  // did not throw
    }

    StackIndex base = TOP_INDEX;

    // If we have a sequence, then GROUP!s must be evaluated.  (If we're given
    // a steps array as input, then a GROUP! is literally meant as a
    // GROUP! by value).  These evaluations should only be allowed if the
    // caller has asked us to return steps.

    if (Any_Sequence_Kind(var_heart)) {
        if (Not_Cell_Flag(var, SEQUENCE_HAS_NODE))  // compressed byte form
            fail (var);

        const Node* node1 = Cell_Node1(var);
        if (Is_Node_A_Cell(node1)) {  // pair optimization
            // pairings considered "Listlike", handled by Cell_List_At()
        }
        else switch (Stub_Flavor(c_cast(Flex*, node1))) {
          case FLAVOR_SYMBOL: {
            if (Get_Cell_Flag(var, LEADING_BLANK))  // `/a` or `.a`
               goto set_target;

            // `a/` or `a.`
            //
            // !!! If this is a PATH!, it should error if it's not an action...
            // and if it's a TUPLE! it should error if it is an action.  Review.
            //
            goto set_target; }

          case FLAVOR_ARRAY:
            break;  // fall through

          default:
            panic (var);
        }

        const Element* tail;
        const Element* head = Cell_List_At(&tail, var);
        const Element* at;
        Context* at_binding = Derive_Binding(context, var);
        for (at = head; at != tail; ++at) {
            if (Is_Group(at)) {
                if (not steps_out)
                    fail (Error_Bad_Get_Group_Raw(var));

                if (Eval_Any_List_At_Throws(temp, at, at_binding)) {
                    Drop_Data_Stack_To(base);
                    return true;
                }
                Decay_If_Unstable(temp);
                if (Is_Antiform(temp))
                    fail (Error_Bad_Antiform(temp));

                Move_Cell(PUSH(), cast(Element*, temp));
                if (at == head)
                    Quotify(TOP, 1);  // signal it was not literally the head
            }
            else  // Note: must keep WORD!s at head as-is for writeback
                Derelativize(PUSH(), at, at_binding);
        }
    }
    else if (Is_The_Block(var)) {
        const Element* tail;
        const Element* head = Cell_List_At(&tail, var);
        const Element* at;
        Context* at_binding = Derive_Binding(context, var);
        for (at = head; at != tail; ++at)
            Derelativize(PUSH(), at, at_binding);
    }
    else
        fail (var);

    assert(Is_Action(updater));  // we will use rebM() on it

    DECLARE_VALUE (writeback);
    Push_GC_Guard(writeback);

    Erase_Cell(temp);
    Push_GC_Guard(temp);

    StackIndex stackindex_top = TOP_INDEX;

  poke_again:
  blockscope {
    StackIndex stackindex = base + 1;

  blockscope {
    StackValue(*) at = Data_Stack_At(stackindex);
    assert(not Is_Antiform(at));
    if (Is_Quoted(at)) {
        Copy_Cell(out, at);
        Unquotify(out, 1);
    }
    else if (Is_Word(at)) {
        const Value* slot;
        Option(Error*) error = Trap_Lookup_Word(
            &slot, cast(Element*, at), SPECIFIED
        );
        if (error)
            fail (unwrap error);
        Copy_Cell(out, slot);
    }
    else
        fail (Copy_Cell(out, at));
  }

    ++stackindex;

    // Keep PICK-ing until you come to the last step.

    while (stackindex != stackindex_top) {
        Move_Cell(temp, out);
        Quotify(temp, 1);
        const Node* ins = rebQ(cast(Value*, Data_Stack_At(stackindex)));
        if (rebRunThrows(
            out,  // <-- output cell
            Canon(PICK_P), temp, ins
        )){
            Drop_GC_Guard(temp);
            Drop_GC_Guard(writeback);
            fail (Error_No_Catch_For_Throw(TOP_LEVEL));  // don't let PICKs throw
        }
        ++stackindex;
    }

    // Now do a the final step, an update (often a poke)

    Move_Cell(temp, out);
    Byte quote_byte = QUOTE_BYTE(temp);
    QUOTE_BYTE(temp) = ONEQUOTE_3;
    const Node* ins = rebQ(cast(Value*, Data_Stack_At(stackindex)));
    assert(Is_Action(updater));
    if (rebRunThrows(
        out,  // <-- output cell
        rebRUN(updater), temp, ins, rebQ(setval)
    )){
        Drop_GC_Guard(temp);
        Drop_GC_Guard(writeback);
        fail (Error_No_Catch_For_Throw(TOP_LEVEL));  // don't let POKEs throw
    }

    // Subsequent updates become pokes, regardless of initial updater function

    updater = Lib(POKE_P);

    if (not Is_Nulled(out)) {
        Move_Cell(writeback, out);
        QUOTE_BYTE(writeback) = quote_byte;
        setval = writeback;

        --stackindex_top;

        if (stackindex_top != base + 1)
            goto poke_again;

        // can't use POKE, need to use SET
        if (not Is_Word(Data_Stack_At(base + 1)))
            fail ("Can't POKE back immediate value unless it's to a WORD!");

        Copy_Cell(
            Sink_Word_May_Fail(
                cast(Element*, Data_Stack_At(base + 1)),
                SPECIFIED
            ),
            setval
        );
    }
  }

    Drop_GC_Guard(temp);
    Drop_GC_Guard(writeback);

    if (steps_out and steps_out != GROUPS_OK)
        Init_Block(unwrap steps_out, Pop_Stack_Values(base));
    else
        Drop_Data_Stack_To(base);

    return false;
}


//
//  Set_Var_Core_Throws: C
//
bool Set_Var_Core_Throws(
    Sink(Value*) out,  // GC-safe cell to write steps to
    Option(Value*) steps_out,  // no GROUP!s if nulled
    const Element* var,
    Context* context,
    const Value* setval  // e.g. L->out (in the evaluator, right hand side)
){
    return Set_Var_Core_Updater_Throws(
        out,
        steps_out,
        var,
        context,
        setval,
        Lib(POKE_P)
    );
}


//
//  Set_Var_May_Fail: C
//
// Simpler function, where GROUP! is not ok...and there's no interest in
// preserving the "steps" to reuse in multiple assignments.
//
void Set_Var_May_Fail(
    const Element* var,
    Context* context,
    const Value* setval
){
    Option(Value*) steps_out = nullptr;

    DECLARE_ATOM (dummy);
    if (Set_Var_Core_Throws(dummy, steps_out, var, context, setval))
        fail (Error_No_Catch_For_Throw(TOP_LEVEL));
}


//
//  /set: native [
//
//  "Sets a word or path to specified value (see also: UNPACK)"
//
//      return: "Same value as input (pass through if target is void)"
//          [any-value?]
//      target "Word or tuple, or calculated sequence steps (from GET)"
//          [~void~ any-word? any-sequence? any-group?
//          any-get-value? any-set-value? the-block!]
//      ^value [raised? any-value?]  ; tunnels failure
//      :any "Do not error on unset words"
//      :groups "Allow GROUP! Evaluations"
//  ]
//
DECLARE_NATIVE(set)
//
// 1. We want parity between (set $x expression) and (x: expression).  It is
//    very useful that you can write (e: trap [x: expression]) and in the case
//    of a raised definitional error, have the assignment skipped and the
//    error trapped.  Hence SET has to take its argument ^META and receive
//    definitional errors to pass through
//
// 2. SET of a BLOCK! should probably expose the implementation of the
//    multi-return mechanics used by SET-BLOCK!.  That would require some
//    refactoring that isn't a priority at time of writing...but were it to
//    be implemented, we would need to not decay packs like this.
//
// 3. Plain POKE can't throw (e.g. from a GROUP!) because it won't evaluate
//    them.  However, we can get errors.  Confirm we only are raising errors
//    unless steps_out were passed.
{
    INCLUDE_PARAMS_OF_SET;

    Value* setval = ARG(value);

    if (Is_Meta_Of_Raised(setval))
        return UNMETA(cast(Element*, setval));  // passthru raised errors [1]

    Meta_Unquotify_Decayed(setval);  // in future, no decay for SET BLOCK! [2]

    if (Is_Void(ARG(target)))
        return COPY(setval);   // same behavior for SET as [10 = (void): 10]

    Element* target = cast(Element*, ARG(target));
    if (Any_Chain(target))  // GET-WORD, SET-WORD, SET-GROUP, etc.
        Unchain(target);

    Value* steps;
    if (REF(groups))
        steps = GROUPS_OK;
    else
        steps = nullptr;  // no GROUP! evals

    if (not REF(any)) {
        // !!! The only SET prohibitions will be on antiform actions, TBD
        // (more general filtering available via accessors)
    }

    if (Any_Group(target)) {  // !!! maybe SET-GROUP!, but GET-GROUP!?
        if (not REF(groups))
            fail (Error_Bad_Get_Group_Raw(target));

        if (Eval_Any_List_At_Throws(SPARE, target, SPECIFIED))
            fail (Error_No_Catch_For_Throw(LEVEL));

        Decay_If_Unstable(SPARE);

        if (Is_Void(SPARE))
            return COPY(setval);

        if (not (
            Any_Word(SPARE) or Any_Sequence(SPARE) or Is_The_Block(SPARE)
        )){
            fail (SPARE);
        }

        target = cast(Element*, SPARE);
    }

    if (Set_Var_Core_Throws(OUT, steps, target, SPECIFIED, setval)) {
        assert(steps or Is_Throwing_Failure(LEVEL));  // [3]
        return THROWN;
    }

    return COPY(setval);
}


//
//  /try: native [
//
//  "Suppress failure from raised errors or VOID, by returning NULL"
//
//      return: [any-value?]
//      ^atom [any-atom?]  ; e.g. TRY on a pack returns the pack
//  ]
//
DECLARE_NATIVE(try)
{
    INCLUDE_PARAMS_OF_TRY;

    Element* meta = cast(Element*, ARG(atom));

    if (Is_Meta_Of_Void(meta) or Is_Meta_Of_Null(meta))
        return Init_Nulled(OUT);

    if (Is_Meta_Of_Raised(meta))
        return nullptr;

    return UNMETA(meta);  // !!! also tolerates other antiforms, should it?
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
            fail (ARG(exports));

        const Symbol* symbol = Cell_Word_Symbol(v);

        bool strict = true;

        const Value* src = MOD_VAR(source, symbol, strict);
        if (src == nullptr)
            fail (v);  // fail if unset value, also?

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
//  /enfix?: native:intrinsic [
//
//  "TRUE if looks up to a function and gets first argument before the call"
//
//      return: [logic?]
//      frame [<unrun> frame!]
//  ]
//
DECLARE_INTRINSIC(enfix_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Enfixed(arg));
}


//
//  /enfix: native:intrinsic [
//
//  "For making enfix functions, e.g (/+: enfix :add)"
//
//      return: "Antiform action action"
//          [antiform?]  ; [action?] comes after ENFIX in bootstrap
//      original [<unrun> frame!]
//  ]
//
DECLARE_INTRINSIC(enfix)
{
    UNUSED(phase);

    Actionify(Copy_Cell(out, arg));
    Set_Cell_Flag(out, ENFIX_FRAME);
}


//
//  /unenfix: native:intrinsic [
//
//  "For removing enfixedness from functions (prefix is a common var name)"
//
//      return: "Isotopic action"
//          [antiform?]  ; [action?] comes after ENFIX in bootstrap
//      original [<unrun> frame!]
//  ]
//
DECLARE_INTRINSIC(unenfix)
{
    UNUSED(phase);

    Actionify(Copy_Cell(out, arg));
    Clear_Cell_Flag(out, ENFIX_FRAME);
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
        fail ("FREE only implemented for ANY-SERIES? at the moment");

    if (Not_Node_Accessible(Cell_Node1(v)))
        fail ("Cannot FREE already freed series");

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
{
    INCLUDE_PARAMS_OF_FREE_Q;

    Value* v = ARG(value);

    if (Is_Void(v) or Is_Nulled(v))
        return Init_Logic(OUT, false);

    // All freeable values put their freeable Flex in the payload's "first".
    //
    if (Not_Cell_Flag(v, FIRST_IS_NODE))
        return Init_Logic(OUT, false);

    Node* n = Cell_Node1(v);

    // If the node is not a Flex (e.g. a Pairing), it cannot be freed (as
    // a freed version of a Pairing is the same size as the Pairing).
    //
    // !!! Technically speaking a PAIR! could be freed as ANY-LIST? could, it
    // would mean converting the Node.  Review.
    //
    if (n == nullptr or Is_Node_A_Cell(n))
        return Init_Logic(OUT, false);

    return Init_Logic(OUT, Not_Node_Accessible(n));
}


//
//  As_String_May_Fail: C
//
// Shared code from the refinement-bearing AS-TEXT and AS TEXT!.
//
bool Try_As_String(
    Sink(Value*) out,
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
                    bp = Back_Scan_UTF8_Char(&c, bp, &bytes_left);
                    if (bp == NULL)  // !!! Should Back_Scan() fail?
                        fail (Error_Bad_Utf8_Raw());

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
        if (Get_Cell_Flag(v, STRINGLIKE_HAS_NODE)) {
            assert(Is_Flex_Frozen(Cell_Issue_String(v)));
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

        String* str = Make_String_Core(size, FLEX_FLAGS_NONE);
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
//          issue! url!
//          any-sequence? any-series? any-word?
//          frame!
//      ]
//      type [type-block!]
//      value [
//          <maybe>
//          integer!
//          issue! url!
//          any-sequence? any-series? any-word? frame!
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
        fail ("New kind can't be quoted/quasiform/antiform");

    Heart new_heart = cast(Heart, new_kind);
    if (new_heart == Cell_Heart_Ensure_Noquote(v))
        return COPY(v);

    if (Any_List_Kind(new_heart)) {

  //=//// CONVERSION TO ANY-ARRAY! ////////////////////////////////////////=//

        if (Any_Sequence(v)) {  // internals vary based on optimization
            if (Not_Cell_Flag(v, SEQUENCE_HAS_NODE))
                fail ("Array Conversions of byte-oriented sequences TBD");

            const Node* node1 = Cell_Node1(v);
            if (Is_Node_A_Cell(node1)) {  // reusing node complicated [1]
                const Pairing* p = c_cast(Pairing*, node1);
                Context *binding = Cell_List_Binding(v);
                Array* a = Make_Array_Core(2, NODE_FLAG_MANAGED);
                Set_Flex_Len(a, 2);
                Derelativize(Array_At(a, 0), Pairing_First(p), binding);
                Derelativize(Array_At(a, 1), Pairing_Second(p), binding);
                Freeze_Array_Shallow(a);
                Init_Block(v, a);
            }
            else switch (Stub_Flavor(c_cast(Flex*, node1))) {
              case FLAVOR_SYMBOL: {
                Array* a = Make_Array_Core(2, NODE_FLAG_MANAGED);
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
                Freeze_Array_Shallow(a);
                Init_Block(v, a);
                break; }

              case FLAVOR_ARRAY: {
                const Array* a = Cell_Array(v);
                if (MIRROR_BYTE(a)) {  // .[a] or (xxx): compression
                    Array* two = Make_Array(2);
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
                    assert(Is_Array_Frozen_Shallow(a));
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
            if (not Is_Array_Frozen_Shallow(Cell_Array(v)))
                Freeze_Array_Shallow(Cell_Array_Ensure_Mutable(v));

            DECLARE_ELEMENT (temp);  // need to rebind
            Option(Error*) error = Trap_Init_Any_Sequence_At_Listlike(
                temp,
                new_heart,
                Cell_Array(v),
                VAL_INDEX(v)
            );
            if (error)
                fail (unwrap error);

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
            if (Get_Cell_Flag(v, STRINGLIKE_HAS_NODE)) {
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
            //
            // !!! This uses the same path as Scan_Word() to try and run
            // through the same validation.  Review efficiency.
            //
            Size size;
            Utf8(const*) utf8 = Cell_Utf8_Size_At(&size, v);
            if (nullptr == Scan_Any_Word(OUT, new_heart, utf8, size))
                fail (Error_Bad_Char_Raw(v));

            return Inherit_Const(stable_OUT, v);
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
                    fail (Error_Alias_Constrains_Raw());

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
                fail ("Cannot convert BINARY! to WORD! unless at the head");

            // We have to permanently freeze the underlying String from any
            // mutation to use it in a WORD! (and also, may add STRING flag);
            //
            const Binary* b = Cell_Binary(v);
            if (not Is_Flex_Frozen(b))
                if (Get_Cell_Flag(v, CONST))  // can't freeze or add IS_STRING
                    fail (Error_Alias_Constrains_Raw());

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
            fail ("AS INTEGER! only supports what-were-CHAR! issues ATM");
        return Init_Integer(OUT, Cell_Codepoint(v)); }

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
                    FLAG_HEART_BYTE(REB_ISSUE) | CELL_MASK_NO_NODES
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
            HEART_BYTE(OUT) = REB_ISSUE;
            return OUT;
        }

        goto bad_cast; }

      case REB_TEXT:
      case REB_TAG:
      case REB_FILE:
      case REB_URL:
      case REB_EMAIL:
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
            if (Get_Cell_Flag(v, STRINGLIKE_HAS_NODE))
                goto any_string_as_binary;  // had a String allocation

            // Data lives in Cell--make new frozen String for BINARY!

            Size size;
            Utf8(const*) utf8 = Cell_Utf8_Size_At(&size, v);
            Binary* b = Make_Binary_Core(size, NODE_FLAG_MANAGED);
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

        fail (v); }

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
        Freeze_Array_Shallow(Varlist_Array(Cell_Varlist(v)));
        return Init_Frame_Details(
            OUT,
            VAL_FRAME_PHASE(v),
            ANONYMOUS,  // see note, we might have stored this in varlist slot
            Cell_Frame_Coupling(v)
        );
      }

      fail (v); }

      default:  // all applicable types should be handled above
        break;
    }

  bad_cast:
    fail (Error_Bad_Cast_Raw(v, ARG(type)));

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
        fail (PARAM(value));

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
        fail (Error_Bad_Cast_Raw(v, Datatype_From_Kind(REB_TEXT)));
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
//  /null?: native:intrinsic [
//
//  "Tells you if the argument is a ~null~ antiform (branch inhibitor)"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(null_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Nulled(arg));
}


//
//  /okay?: native:intrinsic [
//
//  "Tells you if the argument is an ~okay~ antiform (canon branch trigger)"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(okay_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Okay(arg));
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
//  /logic?: native:intrinsic [
//
//  "Tells you if the argument is NULL or #"
//
//      return: "null or okay"  ; can't use LOGIC? to typecheck
//      value
//  ]
//
DECLARE_INTRINSIC(logic_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Logic(arg));
}


//
//  /logical: native:intrinsic [
//
//  "Produces NULL for 0, or # for all other integers"
//
//      return: [logic?]
//      value [integer!]
//  ]
//
DECLARE_INTRINSIC(logical)
{
    UNUSED(phase);

    Init_Logic(out, VAL_INT64(arg) != 0);
}


//
//  /boolean?: native:intrinsic [
//
//  "Tells you if the argument is the TRUE or FALSE word"
//
//      return: [logic?]
//      value [any-value?]
//  ]
//
DECLARE_INTRINSIC(boolean_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Boolean(arg));
}


//
//  /onoff?: native:intrinsic [
//
//  "Tells you if the argument is the ON or OFF word"
//
//      return: [logic?]
//      value [any-value?]
//  ]
//
DECLARE_INTRINSIC(onoff_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_OnOff(arg));
}


//
//  /yesno?: native:intrinsic [
//
//  "Tells you if the argument is the YES or NO word"
//
//      return: [logic?]
//      value [any-value?]
//  ]
//
DECLARE_INTRINSIC(yesno_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_YesNo(arg));
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
