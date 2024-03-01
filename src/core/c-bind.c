//
//  File: %c-bind.c
//  Summary: "Word Binding Routines"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Binding relates a word to a context.  Every word can be either bound,
// specifically bound to a particular context, or bound relatively to a
// function (where additional information is needed in order to find the
// specific instance of the variable for that word as a key).
//

#include "sys-core.h"


//
//  Bind_Values_Inner_Loop: C
//
// Bind_Values_Core() sets up the binding table and then calls
// this recursive routine to do the actual binding.
//
void Bind_Values_Inner_Loop(
    struct Reb_Binder *binder,
    Element* head,
    const Element* tail,
    Context* context,
    Option(SymId) add_midstream_types,
    Flags flags
){
    Element* v = head;
    for (; v != tail; ++v) {
        Heart heart = Cell_Heart(v);

        if (Any_Word_Kind(heart)) {
            const Symbol* symbol = Cell_Word_Symbol(v);

          if (CTX_TYPE(context) == REB_MODULE) {
            bool strict = true;
            Value* lookup = MOD_VAR(context, symbol, strict);
            if (lookup) {
                INIT_VAL_WORD_INDEX(v, INDEX_PATCHED);
                BINDING(v) = Singular_From_Cell(lookup);
            }
            else if (
                add_midstream_types == SYM_ANY
                or (
                    add_midstream_types == SYM_SET
                    and heart == REB_SET_WORD
                )
            ){
                Init_Trash(Append_Context_Bind_Word(context, v));
            }
          }
          else {
            REBINT n = Get_Binder_Index_Else_0(binder, symbol);
            if (n > 0) {
                //
                // A binder index of 0 should clearly not be bound.  But
                // negative binder indices are also ignored by this process,
                // which provides a feature of building up state about some
                // words while still not including them in the bind.
                //
                assert(cast(REBLEN, n) <= CTX_LEN(context));

                // We're overwriting any previous binding, which may have
                // been relative.

                INIT_VAL_WORD_INDEX(v, n);
                BINDING(v) = context;
            }
            else if (
                add_midstream_types == SYM_ANY
                or (
                    add_midstream_types == SYM_SET
                    and heart == REB_SET_WORD
                )
            ){
                //
                // Word is not in context, so add it if option is specified
                //
                Append_Context_Bind_Word(context, v);
                Add_Binder_Index(binder, symbol, VAL_WORD_INDEX(v));
            }
          }
        }
        else if (flags & BIND_DEEP) {
            if (Any_Arraylike(v)) {
                const Element* sub_tail;
                Element* sub_at = Cell_Array_At_Mutable_Hack(&sub_tail, v);
                Bind_Values_Inner_Loop(
                    binder,
                    sub_at,
                    sub_tail,
                    context,
                    add_midstream_types,
                    flags
                );
            }
        }
    }
}


//
//  Bind_Values_Core: C
//
// Bind words in an array of values to a specified context.
//
// NOTE: If types are added, then they will be added in "midstream".  Only
// bindings that come after the added value is seen will be bound.
//
void Bind_Values_Core(
    Element* head,
    const Element* tail,
    const Value* context,
    Option(SymId) add_midstream_types,
    Flags flags // see %sys-core.h for BIND_DEEP, etc.
) {
    struct Reb_Binder binder;
    INIT_BINDER(&binder);

    Context* c = VAL_CONTEXT(context);

    // Associate the canon of a word with an index number.  (This association
    // is done by poking the index into the stub of the series behind the
    // ANY-WORD?, so it must be cleaned up to not break future bindings.)
    //
  if (not Is_Module(context)) {
    REBLEN index = 1;
    const Key* key_tail;
    const Key* key = CTX_KEYS(&key_tail, c);
    const Value* var = CTX_VARS_HEAD(c);
    for (; key != key_tail; key++, var++, index++)
        Add_Binder_Index(&binder, KEY_SYMBOL(key), index);
  }

    Bind_Values_Inner_Loop(
        &binder,
        head,
        tail,
        c,
        add_midstream_types,
        flags
    );

  if (not Is_Module(context)) {  // Reset all the binder indices to zero
    const Key* key_tail;
    const Key* key = CTX_KEYS(&key_tail, c);
    const Value* var = CTX_VARS_HEAD(c);
    for (; key != key_tail; ++key, ++var)
        Remove_Binder_Index(&binder, KEY_SYMBOL(key));
  }

    SHUTDOWN_BINDER(&binder);
}


//
//  Unbind_Values_Core: C
//
// Unbind words in a block, optionally unbinding those which are
// bound to a particular target (if target is NULL, then all
// words will be unbound regardless of their VAL_WORD_CONTEXT).
//
void Unbind_Values_Core(
    Element* head,
    const Element* tail,
    Option(Context*) context,
    bool deep
){
    Element* v = head;
    for (; v != tail; ++v) {
        if (
            Any_Wordlike(v)
            and (not context or BINDING(v) == unwrap(context))
        ){
            Unbind_Any_Word(v);
        }
        else if (Any_Arraylike(v) and deep) {
            const Element* sub_tail;
            Element* sub_at = Cell_Array_At_Mutable_Hack(&sub_tail, v);
            Unbind_Values_Core(sub_at, sub_tail, context, true);
        }
    }
}


//
//  Try_Bind_Word: C
//
// Returns 0 if word is not part of the context, otherwise the index of the
// word in the context.
//
bool Try_Bind_Word(const Value* context, Value* word)
{
    const bool strict = true;
    if (Is_Module(context)) {
        Stub* patch = try_unwrap(MOD_PATCH(
            VAL_CONTEXT(context),
            Cell_Word_Symbol(word),
            strict
        ));
        if (not patch)
            return false;
        INIT_VAL_WORD_INDEX(word, INDEX_PATCHED);
        BINDING(word) = patch;
        return true;
    }

    REBLEN n = Find_Symbol_In_Context(
        context,
        Cell_Word_Symbol(word),
        strict
    );
    if (n == 0)
        return false;
    if (n != 0) {
        INIT_VAL_WORD_INDEX(word, n);  // ^-- may have been relative
        BINDING(word) = VAL_CONTEXT(context);
    }
    return true;
}


//
//  Make_Let_Patch: C
//
// Efficient form of "mini-object" allocation that can hold exactly one
// variable.  Unlike a context, it does not have the ability to hold an
// archetypal form of that context...because the only value cell in the
// singular array is taken for the variable content itself.
//
// 1. The way it is designed, the list of lets terminates in either a nullptr
//    or a context pointer that represents the specifying frame for the chain.
//    So we can simply point to the existing specifier...whether it is a let,
//    a use, a frame context, or nullptr.
//
Stub* Make_Let_Patch(
    const Symbol* symbol,
    Specifier* specifier
){
    Stub* let = Alloc_Singular(  // payload is one variable
        FLAG_FLAVOR(LET)
            | NODE_FLAG_MANAGED
            | SERIES_FLAG_LINK_NODE_NEEDS_MARK  // link to next virtual bind
            | SERIES_FLAG_INFO_NODE_NEEDS_MARK  // inode of symbol
    );

    Init_Trash(x_cast(Value*, Stub_Cell(let)));  // start as unset

    if (specifier) {
        assert(IS_LET(specifier) or IS_USE(specifier) or IS_VARLIST(specifier));
        assert(Is_Node_Managed(specifier));
    }
    LINK(NextLet, let) = specifier;  // linked list [1]

    MISC(LetReserved, let) = nullptr;  // not currently used

    INODE(LetSymbol, let) = symbol;  // surrogate for context "key"

    return let;
}


//
//  Get_Word_Container: C
//
// Find the context a word is bound into.  This must account for the various
// binding forms: Relative Binding, Derived Binding, and Virtual Binding.
//
// The reason this is broken out from the Lookup_Word() routines is because
// sometimes read-only-ness of the context is heeded, and sometimes it is not.
// Splitting into a step that returns the context and the index means the
// main work of finding where to look up doesn't need to be parameterized
// with that.
//
// This function is used by Derelativize(), and so it shouldn't have any
// failure mode while it's running...even if the context is inaccessible or
// the word is unbound.  Errors should be raised by callers if applicable.
//
// 1. We want to continue the next_virtual loop from inside sub-loops, which
//    means we need a `goto` and not a `continue`.  But putting the goto at
//    the end of the loop would jump over variable initializations.  Stylizing
//    this way makes it work without a warning.
//
// 2. !!! One original goal with Sea of Words was to enable something like
//    JavaScript's "strict mode", to prevent writing to variables that had not
//    been somehow previously declared.  However, that is a bit too ambitious
//    for a first rollout...as just having the traditional behavior of "any
//    assignment works" is something people are used to.  Don't do it for the
//    Lib_Context (so mezzanine is still guarded) but as a first phase, permit
//    the "emergence" of any variable that is attached to a module.
//
// 3. RELATIVE BINDING: The word was made during a deep copy of the block
//    that was given as a function's body, and stored a reference to that
//    ACTION! as its binding.  To get a variable for the word, we must
//    find the right function call on the stack (if any) for the word to
//    refer to (the FRAME!)
//
//    We can only check for a match of the underlying function.  If we checked
//    for an exact match, then the same function body could not be repurposed
//    for dispatch e.g. in copied, hijacked, or adapted code, because the
//    identity of the derived function would not match up with the body it
//    intended to reuse.
//
// 4. !!! FOR-EACH uses the slots in an object to count how many arguments
//    there are...and if a slot is reusing an existing variable it holds that
//    variable.  This ties into general questions of hiding (same bit).  Don't
//    count it as a hit.
//
Option(Series*) Get_Word_Container(
    REBLEN *index_out,
    const Cell* any_word,
    Specifier* specifier_in,
    enum Reb_Attach_Mode mode
){
    Corrupt_If_Debug(*index_out);  // corrupt index to make sure it gets set

    Series* binding = BINDING(any_word);
    const Symbol* symbol = Cell_Word_Symbol(any_word);

    if (VAL_WORD_INDEX_I32(any_word) == INDEX_ATTACHED) {
        //
        // Variable may have popped into existence since the original attach.
        //
        Context* ctx = cast(Context*, binding);
        Value* var = MOD_VAR(ctx, symbol, true);
        if (var) {
            *index_out = INDEX_PATCHED;
            return Singular_From_Cell(var);
        }
        if (mode != ATTACH_WRITE) {
            *index_out = INDEX_ATTACHED;
            return binding;
        }
        *index_out = INDEX_PATCHED;
        var = Append_Context(ctx, symbol);
        Init_Trash(var);
        return Singular_From_Cell(var);
    }

    Specifier* specifier = specifier_in;

    if (IS_WORD_BOUND(any_word)) {  // leave binding alone
        *index_out = VAL_WORD_INDEX(any_word);
        return binding;
    }

    Context* attach = nullptr;  // where to attach variable if not found

    while (specifier) {
        goto loop_body;  // avoid compiler warnings on `goto next_virtual` [1]

      next_virtual:
        specifier = NextVirtual(specifier);
        continue;

      loop_body:

        if (IS_VARLIST(specifier)) {
            Context* ctx = cast(Context*, specifier);

            if (CTX_TYPE(ctx) == REB_MODULE) {
                Value* var = MOD_VAR(ctx, symbol, true);
                if (var) {
                    *index_out = INDEX_PATCHED;
                    return Singular_From_Cell(var);
                }

                if (ctx == Lib_Context or ctx == Sys_Context)  // "strict"
                    goto next_virtual;

                if (mode == ATTACH_WRITE) {  // only write to first module
                    *index_out = INDEX_PATCHED;
                    var = Append_Context(ctx, symbol);
                    Init_Trash(var);
                    return Singular_From_Cell(var);
                }

                if (not attach)  // non-strict, allow later emergence
                    attach = ctx;

                goto next_virtual;
            }

            assert(CTX_TYPE(cast(Context*, specifier)) == REB_FRAME);

            if (
                binding  // word has a cache for if it's in an action frame
                and Action_Is_Base_Of(
                    cast(Action*, binding),
                    CTX_FRAME_PHASE(ctx)
                )
            ){
                assert(VAL_WORD_INDEX_I32(any_word) <= 0);
                if (VAL_WORD_INDEX_I32(any_word) == 0)
                    goto check_method_members;
                *index_out = -(VAL_WORD_INDEX_I32(any_word));
                return CTX_VARLIST(ctx);
            }
            else {  // have to search frame manually
                REBINT len = Find_Symbol_In_Context(
                    CTX_ARCHETYPE(cast(Context*, specifier)),
                    symbol,
                    true
                );
                // Note: caching here seems to slow things down?
              #ifdef CACHE_FINDINGS_BUT_SEEMS_TO_SLOW_THINGS_DOWN
                if (VAL_WORD_INDEX_I32(any_word) <= 0) {  // cache in unbounds
                    VAL_WORD_INDEX_I32(m_cast(Cell*, any_word)) = -(len);
                    BINDING(m_cast(Cell*, any_word)) = CTX_FRAME_PHASE(ctx);
                }
              #endif
                if (len != 0) {
                    *index_out = len;
                    return specifier;
                }
            }

          check_method_members: {
            Level* level = CTX_LEVEL_IF_ON_STACK(cast(Context*, specifier));
            if (not level)
                goto next_virtual;
            Context* object = Level_Binding(level);
            if (not object)
                goto next_virtual;

            REBLEN len = Find_Symbol_In_Context(
                CTX_ARCHETYPE(object),
                symbol,
                true
            );
            if (len == 0)
                goto next_virtual;

            *index_out = len;
            return object;
          }
        }

        if (IS_LET(specifier)) {
            if (INODE(LetSymbol, specifier) == symbol) {
                *index_out = INDEX_PATCHED;
                return specifier;
            }
            goto next_virtual;
        }

        if (Is_Module(Stub_Cell(specifier))) {
            Context* mod = VAL_CONTEXT(Stub_Cell(specifier));

            Value* var = MOD_VAR(mod, symbol, true);
            if (var) {
                *index_out = INDEX_PATCHED;
                return Singular_From_Cell(var);
            }
            goto next_virtual;
        }

        Stub* overbind = BINDING(Stub_Cell(specifier));
        if (not IS_VARLIST(overbind)) {  // a patch-formed LET overload
            if (INODE(LetSymbol, overbind) == symbol) {
                *index_out = INDEX_PATCHED;
                return overbind;
            }
            goto next_virtual;
        }

        if (
            Is_Set_Word(Stub_Cell(specifier))
            and REB_SET_WORD != Cell_Heart(any_word)  // "affected"
        ){
            goto next_virtual;
        }

        Context* overload = cast(Context*, overbind);

        REBLEN index = 1;
        const Key* key_tail;
        const Key* key = CTX_KEYS(&key_tail, overload);
        for (; key != key_tail; ++key, ++index) {
            if (KEY_SYMBOL(key) != symbol)
                continue;

            if (Get_Cell_Flag(CTX_VAR(overload, index), BIND_NOTE_REUSE))
                break;  // FOR-EACH uses context slots weirdly [4]

            *index_out = index;
            return CTX_VARLIST(overload);
        }

        goto next_virtual;
    }

    if (attach) {
        assert(mode == ATTACH_READ);
        *index_out = INDEX_ATTACHED;
        return attach;
    }

    return nullptr;
}


//
//  let: native [
//
//  "Dynamically add a new binding into the stream of evaluation"
//
//      return: "Expression result if SET form, else gives the new vars"
//          [any-value?]
//      'vars "Variable(s) to create, GROUP!s must evaluate to BLOCK! or WORD!"
//          [word! block! set-word! set-block! group! set-group!]
//      :expression "Optional Expression to assign"
//          [<variadic> any-value?]
//  ]
//
DECLARE_NATIVE(let)
//
// 1. Though LET shows as a variadic function on its interface, it does not
//    need to use the variadic argument...since it is a native (and hence
//    can access the frame and feed directly).
//
// 2. For convenience, the group can evaluate to a SET-BLOCK,  e.g.
//
//        block: inside [] '[x y]:
//        (block): <whatever>  ; no real reason to prohibit this
//
//    But there are conflicting demands where we want `(thing):` equivalent
//    to `[(thing)]:`, while at the same time we don't want to wind up with
//    "mixed decorations" where `('^thing):` would become both SET! and SYM!.
//
// 3. Question: Should it be allowed to write `let 'x: <whatever>` and have it
//    act as if you had written `x: <whatever>`, e.g. no LET behavior at all?
//    This may seem useless, but it could be useful in generated code to
//    "escape out of" a LET in some boilerplate.  And it would be consistent
//    with the behavior of `let ['x]: <whatever>`
//
// 4. Right now what is permitted is conservative, due to things like the
//    potential confusion when someone writes:
//
//        get-word: first [:b]
//        let [a (get-word) c]: transcode "<whatever>"
//
//    They could reasonably think that this would behave as if they had
//    written in source `let [a :b c]: transcode <whatever>`.  If that meant
//    to look up the word B to find out were to actually write, we wouldn't
//    want to create a LET binding for B...but for what B looked up to.
//
//    Bias it so that if you want something to just "pass through the LET"
//    that you use a quote mark on it, and the LET will ignore it.
//
// 5. In the "LET dialect", quoted words are a way to pass through things with
//    their existing binding, but allowing them to participate in the same
//    multi-return operation:
//
//        let [value error]
//        [value position error]: transcode data  ; awkward
//
//        let [value 'position error]: transcode data  ; better
//
//    This is applied generically, that no quoted items are processed by the
//    LET...it merely removes the quoting level and generates a new block as
//    output which doesn't have the quote.
//
// 6. The multi-return dialect is planned to be able to use things like
//    refinement names to reinforce the name of what is being returned.
//
//        words: [foo position]
//        let [value /position (second words) 'error]: transcode "abc"
//
//    This doesn't have any meaning to LET and must be skipped...yet retained
//    in the product.  Other things (like INTEGER!) might be useful also to
//    consumers of the bound block product, so they are skipped.
//
// 7. The evaluation may have expanded the bindings, as in:
//
//        let y: let x: 1 + 2 print [x y]
//
//    The LET Y: is running the LET X step, but if it doesn't incorporate that
//    it will be setting the feed's bindings to just include Y.  We have to
//    merge them, with the outer one taking priority:
//
//        >> x: 10, let x: 1000 + let x: x + 10, print [x]
//        1020
//
// 8. When it was looking at enfix, the evaluator caches the fetched value of
//    the word for the next execution.  But we are pulling the rug out from
//    under that if the immediately following item is the same as what we
//    have... or a path starting with it, etc.
//
//        (x: 10 let x: 20 x)  (x: 10 let x: make object! [y: 20] x.y)
//
//    We could try to be clever and maintain that cache in the cases that call
//    for it.  But with evaluator hooks we don't know what kinds of overrides
//    it may have (maybe the binding for items not at the head of a path is
//    relevant?)  Simplest thing to do is drop the cache.
{
    INCLUDE_PARAMS_OF_LET;

    Value* vars = ARG(vars);

    UNUSED(ARG(expression));
    Level* L = level_;  // fake variadic [1]
    Specifier* L_specifier = Level_Specifier(L);

    Value* bindings_holder = ARG(return);

    enum {
        ST_LET_INITIAL_ENTRY = STATE_0,
        ST_LET_EVAL_STEP
    };

    switch (STATE) {
      case ST_LET_INITIAL_ENTRY :
        Init_Block(bindings_holder, EMPTY_ARRAY);
        goto initial_entry;

      case ST_LET_EVAL_STEP :
        goto integrate_eval_bindings;

      default : assert (false);
    }

  initial_entry: {  ///////////////////////////////////////////////////////////

    //=//// HANDLE LET (GROUP): VARIANTS ///////////////////////////////////=//

    // A first amount of indirection is permitted since LET allows the syntax
    // [let (word_or_block): <whatever>].  Handle those groups in such a way
    // that it updates `At_Level(L)` itself to reflect the group product.

    if (
        Is_Group(vars) or Is_Set_Group(vars)
    ){
        if (Do_Any_Array_At_Throws(SPARE, vars, SPECIFIED))
            return THROWN;

        if (Is_Quoted(SPARE))  // should (let 'x: <whatever>) be legal? [3]
            fail ("QUOTED? escapes not supported at top level of LET");

        switch (Cell_Heart(SPARE)) {  // quasi states mean antiforms ok
          case REB_WORD:
          case REB_BLOCK:
            if (Is_Set_Group(vars))
                Setify(stable_SPARE);  // convert `(word):` to be SET-WORD!
            break;

          case REB_SET_WORD:
          case REB_SET_BLOCK:
            if (Is_Set_Group(vars)) {
                // Allow `(set-word):` to ignore "redundant colon" [2]
            }
            break;

          default:
            fail ("LET GROUP! limited to WORD! and BLOCK!");  // [4]
        }

        vars = stable_SPARE;
    }

    //=//// GENERATE NEW BLOCK IF QUOTED? OR GROUP! ELEMENTS ///////////////=//

    // Writes rebound copy of `vars` to SPARE if it's a SET-WORD!/SET-BLOCK!
    // so it can be used in a reevaluation.  For WORD!/BLOCK! forms of LET it
    // just writes the rebound copy into the OUT cell.

    Specifier* bindings = L_specifier;  // specifier chain we may be adding to

    if (bindings and Not_Node_Managed(bindings))
        Set_Node_Managed_Bit(bindings);  // natives don't always manage

    if (Cell_Heart(vars) == REB_WORD or Cell_Heart(vars) == REB_SET_WORD) {
        const Symbol* symbol = Cell_Word_Symbol(vars);
        bindings = Make_Let_Patch(symbol, bindings);

        Value* where;
        if (Cell_Heart(vars) == REB_SET_WORD) {
            STATE = ST_LET_EVAL_STEP;
            where = stable_SPARE;
        }
        else
            where = stable_OUT;

        Copy_Cell_Header(where, vars);  // keep quasi state and word/setword
        INIT_CELL_WORD_SYMBOL(where, symbol);
        INIT_VAL_WORD_INDEX(where, INDEX_PATCHED);
        BINDING(where) = bindings;

        Corrupt_Pointer_If_Debug(vars);  // if in spare, we may have overwritten
    }
    else {
        assert(Is_Block(vars) or Is_Set_Block(vars));

        const Element* tail;
        const Element* item = Cell_Array_At(&tail, vars);
        Specifier* item_specifier = Cell_Specifier(vars);

        StackIndex base = TOP_INDEX;

        bool altered = false;

        for (; item != tail; ++item) {
            const Element* temp = item;
            Specifier* temp_specifier = item_specifier;

            if (Is_Quoted(temp)) {
                Derelativize(PUSH(), temp, temp_specifier);
                Unquotify(TOP, 1);  // drop quote in output block [5]
                altered = true;
                continue;  // do not make binding
            }

            if (Is_Group(temp)) {  // evaluate non-QUOTED? groups in LET block
                if (Do_Any_Array_At_Throws(OUT, temp, item_specifier))
                    return THROWN;

                if (Is_Void(OUT)) {
                    Init_Blank(OUT);
                }
                else if (Is_Antiform(OUT))
                    fail (Error_Bad_Antiform(OUT));

                temp = cast(Element*, OUT);
                temp_specifier = SPECIFIED;

                altered = true;
            }

            switch (Cell_Heart(temp)) {  // permit quasi
              case REB_ISSUE:  // is multi-return opt-in for dialect, passthru
              case REB_BLANK:  // is multi-return opt-out for dialect, passthru
                Derelativize(PUSH(), temp, temp_specifier);
                break;

              case REB_WORD:
              case REB_SET_WORD:
              case REB_META_WORD:
              case REB_THE_WORD: {
                Derelativize(PUSH(), temp, temp_specifier);  // !!! no derel
                const Symbol* symbol = Cell_Word_Symbol(temp);
                bindings = Make_Let_Patch(symbol, bindings);
                VAL_WORD_INDEX_I32(TOP) = INDEX_PATCHED;
                BINDING(TOP) = bindings;
                break; }

              default:
                fail (temp);  // default to passthru [6]
            }
        }

        Value* where;
        if (Is_Set_Block(vars)) {
            STATE = ST_LET_EVAL_STEP;
            where = stable_SPARE;
        }
        else
            where = stable_OUT;

        if (altered) {  // elements altered, can't reuse input block rebound
            Init_Array_Cell(
                where,  // may be SPARE, and vars may point to it
                Cell_Heart_Ensure_Noquote(vars),
                Pop_Stack_Values_Core(base, NODE_FLAG_MANAGED)
            );
        }
        else {
            Drop_Data_Stack_To(base);

            if (vars != where)
                Copy_Cell(where, vars);  // Move_Cell() of ARG() not allowed
        }
        BINDING(where) = bindings;

        Corrupt_Pointer_If_Debug(vars);  // if in spare, we may have overwritten
    }

    //=//// ONE EVAL STEP WITH OLD BINDINGS IF SET-WORD! or SET-BLOCK! /////=//

    // We want the left hand side to use the *new* LET bindings, but the right
    // hand side should use the *old* bindings.  For instance:
    //
    //     let assert: specialize :assert [handler: [print "should work!"]]
    //
    // Leverage same mechanism as REEVAL to preload the next execution step
    // with the rebound SET-WORD! or SET-BLOCK!

    BINDING(bindings_holder) = bindings;
    Corrupt_Pointer_If_Debug(bindings);  // catch uses after this point in scope

    if (STATE != ST_LET_EVAL_STEP) {
        assert(Is_Word(OUT) or Is_Block(OUT));  // should have written output
        goto update_feed_binding;
    }

    assert(Is_Set_Word(SPARE) or Is_Set_Block(SPARE));

    Flags flags =
        FLAG_STATE_BYTE(ST_EVALUATOR_REEVALUATING)
        | (L->flags.bits & EVAL_EXECUTOR_FLAG_FULFILLING_ARG)
        | (L->flags.bits & LEVEL_FLAG_RAISED_RESULT_OK);

    Level* sub = Make_Level(LEVEL->feed, flags);
    Copy_Cell(&sub->u.eval.current, cast(Element*, SPARE));
    sub->u.eval.current_gotten = nullptr;
    sub->u.eval.enfix_reevaluate = 'N';  // detect?

    Push_Level(OUT, sub);

    assert(STATE == ST_LET_EVAL_STEP);  // checked above
    return CONTINUE_SUBLEVEL(sub);

} integrate_eval_bindings: {  ////////////////////////////////////////////////

    // !!! Currently, any bindings added during the eval are lost.
    // Rethink this.

    L->feed->gotten = nullptr;  // invalidate next word's cache [8]
    goto update_feed_binding;

} update_feed_binding: {  /////////////////////////////////////////////////////

    // Going forward we want the feed's binding to include the LETs.  Note
    // that this can create the problem of applying the binding twice; this
    // needs systemic review.

    Specifier* bindings = Cell_Specifier(bindings_holder);
    BINDING(FEED_SINGLE(L->feed)) = bindings;

    if (Is_Pack(OUT))
        Decay_If_Unstable(OUT);

    return OUT;
}}


//
//  add-let-binding: native [
//
//  "Experimental function for adding a new variable binding"
//
//      return: [frame! any-array?]
//      environment [frame! any-array?]
//      word [any-word?]
//      value [any-value?]
//  ]
//
DECLARE_NATIVE(add_let_binding)
//
// !!! At time of writing, there are no "first class environments" that
// expose the "Specifier" chain in arrays.  So the arrays themselves are
// used as proxies for that.  Usermode dialects (like UPARSE) update their
// environment by passing in a rule block with a version of that rule block
// with an updated specifier.  A function that wants to add to the evaluator
// environment uses the frame at the moment.
{
    INCLUDE_PARAMS_OF_ADD_LET_BINDING;

    Value* env = ARG(environment);
    Specifier* before;

    if (Is_Frame(env)) {
        Level* L = CTX_LEVEL_MAY_FAIL(VAL_CONTEXT(env));
        before = Level_Specifier(L);
        if (before)
            Set_Node_Managed_Bit(before);
    } else {
        assert(Any_Array(env));
        before = Cell_Specifier(env);
    }

    Specifier* let = Make_Let_Patch(Cell_Word_Symbol(ARG(word)), before);

    Move_Cell(Stub_Cell(let), ARG(value));

    if (Is_Frame(env)) {
        Level* L = CTX_LEVEL_MAY_FAIL(VAL_CONTEXT(env));
        BINDING(FEED_SINGLE(L->feed)) = let;
    }
    else {
        BINDING(env) = let;
    }

    return COPY(env);
}


//
//  add-use-object: native [
//
//  "Experimental function for adding an object's worth of binding to a frame"
//
//      return: [~]
//      frame [frame!]
//      object [object!]
//  ]
//
DECLARE_NATIVE(add_use_object) {
    INCLUDE_PARAMS_OF_ADD_USE_OBJECT;

    Level* L = CTX_LEVEL_MAY_FAIL(VAL_CONTEXT(ARG(frame)));
    Specifier* L_specifier = Level_Specifier(L);

    Context* ctx = VAL_CONTEXT(ARG(object));

    if (L_specifier)
        Set_Node_Managed_Bit(L_specifier);

    Specifier* use = Make_Use_Core(ctx, L_specifier, REB_WORD);

    BINDING(FEED_SINGLE(L->feed)) = use;

    return TRASH;
}


//
//  Clonify_And_Bind_Relative: C
//
// Clone the series embedded in a value *if* it's in the given set of types
// (and if "cloning" makes sense for them, e.g. they are not simple scalars).
//
// Note: The resulting clones will be managed.  The model for lists only
// allows the topmost level to contain unmanaged values...and we *assume* the
// values we are operating on here live in an array.
//
// 1. In Ren-C's binding model, function bodies are conceptually unbound, and
//    need the specifier of the frame instance plus whatever specifier was
//    on the body to resolve the words.  That resolution happens every time
//    the body is run.  Since function frames don't expand in size, we can
//    speed the lookup process for unbound words by reusing their binding
//    space to say whether a word is present in this function's frame or not.
//
// 2. To make the test for IS_WORD_UNBOUND() easy, it's just that the index
//    is less than or equal to zero.  If a word has a negative index then
//    that means it is caching the fact that the word CAN be found in the
//    action at the positive index.  If it has zero and a binding, that
//    means it CAN'T be found in the action's frame.
//
void Clonify_And_Bind_Relative(
    Value* v,
    Flags flags,
    bool deeply,
    Option(struct Reb_Binder*) binder,
    Option(Action*) relative
){
    if (C_STACK_OVERFLOWING(&relative))
        Fail_Stack_Overflow();

    assert(flags & NODE_FLAG_MANAGED);

    Heart heart = Cell_Heart_Unchecked(v);

    if (
        relative
        and Any_Wordlike(v)
        and IS_WORD_UNBOUND(v)  // use unbound words as "in frame" cache [1]
    ){
        REBINT n = Get_Binder_Index_Else_0(unwrap(binder), Cell_Word_Symbol(v));
        VAL_WORD_INDEX_I32(v) = -(n);  // negative or zero signals unbound [2]
        BINDING(v) = unwrap(relative);
    }
    else if (deeply and (Any_Series_Kind(heart) or Any_Sequence_Kind(heart))) {
        //
        // Objects and series get shallow copied at minimum
        //
        Element* deep = nullptr;
        Element* deep_tail = nullptr;

        if (Any_Pairlike(v)) {
            Value* copy = Copy_Pairing(
                VAL_PAIRING(v),
                NODE_FLAG_MANAGED
            );
            Init_Cell_Node1(v, copy);

            deep = cast(Element*, copy);
            deep_tail = cast(Element*, Pairing_Tail(copy));
        }
        else if (Any_Arraylike(v)) {  // ruled out pairlike sequences above...
            Array* copy = Copy_Array_At_Extra_Shallow(
                Cell_Array(v),
                0,  // !!! what if VAL_INDEX() is nonzero?
                0,
                NODE_FLAG_MANAGED
            );

            Init_Cell_Node1(v, copy);

            // See notes in Clonify()...need to copy immutable paths so that
            // binding pointers can be changed in the "immutable" copy.
            //
            if (Any_Sequence_Kind(heart))
                Freeze_Array_Shallow(copy);

            // !!! At one point, arrays were marked relative as well as the
            // words in function bodies.  Now it's needed to consider them
            // to be unbound most of the time.

            deep = Array_Head(copy);
            deep_tail = Array_Tail(copy);
        }
        else if (Any_Series_Kind(heart)) {
            Series* copy = Copy_Series_Core(Cell_Series(v), NODE_FLAG_MANAGED);
            Init_Cell_Node1(v, copy);
        }

        // If we're going to copy deeply, we go back over the shallow
        // copied series and "clonify" the values in it.
        //
        if (deep) {
            for (; deep != deep_tail; ++deep)
                Clonify_And_Bind_Relative(
                    deep,
                    flags,
                    deeply,
                    binder,
                    relative
                );
        }
    }
    else {
        // We're not copying the value, so inherit the const bit from the
        // original value's point of view, if applicable.
        //
        if (Not_Cell_Flag(v, EXPLICITLY_MUTABLE))
            v->header.bits |= (flags & ARRAY_FLAG_CONST_SHALLOW);
    }
}


//
//  Copy_And_Bind_Relative_Deep_Managed: C
//
// This routine is called by Make_Action to copy the body deeply, and while
// it is doing that it puts a cache in any unbound words of whether or not
// that words can be found in the function's frame.
//
Array* Copy_And_Bind_Relative_Deep_Managed(
    const Value* body,
    Action* relative,
    enum Reb_Var_Visibility visibility
){
    struct Reb_Binder binder;
    INIT_BINDER(&binder);

    // Setup binding table from the argument word list.  Note that some cases
    // (like an ADAPT) reuse the exemplar from the function they are adapting,
    // and should not have the locals visible from their binding.  Other cases
    // such as the plain binding of the body of a FUNC created the exemplar
    // from scratch, and should see the locals.  Caller has to decide.
    //
  blockscope {
    EVARS e;
    Init_Evars(&e, ACT_ARCHETYPE(relative));
    e.visibility = visibility;
    while (Did_Advance_Evars(&e))
        Add_Binder_Index(&binder, KEY_SYMBOL(e.key), e.index);
    Shutdown_Evars(&e);
  }

    Array* copy;

  blockscope {
    const Array* original = Cell_Array(body);
    REBLEN index = VAL_INDEX(body);
   /* Specifier* specifier = Cell_Specifier(body); */
    REBLEN tail = Cell_Series_Len_At(body);
    assert(tail <= Array_Len(original));

    if (index > tail)  // !!! should this be asserted?
        index = tail;

    Flags flags = ARRAY_MASK_HAS_FILE_LINE | NODE_FLAG_MANAGED;
    bool deeply = true;

    REBLEN len = tail - index;

    // Currently we start by making a shallow copy and then adjust it

    copy = Make_Array_For_Copy(len, flags, original);
    Set_Series_Len(copy, len);

    const Element* src = Array_At(original, index);
    Element* dest = Array_Head(copy);
    REBLEN count = 0;
    for (; count < len; ++count, ++dest, ++src) {
        Copy_Cell(dest, src);
        Clonify_And_Bind_Relative(
            dest,
            flags | NODE_FLAG_MANAGED,
            deeply,
            &binder,
            relative
        );
    }
  }

  blockscope {  // Reset binding table, see notes above regarding locals
    EVARS e;
    Init_Evars(&e, ACT_ARCHETYPE(relative));
    e.visibility = visibility;
    while (Did_Advance_Evars(&e))
        Remove_Binder_Index(&binder, KEY_SYMBOL(e.key));
    Shutdown_Evars(&e);
  }

    SHUTDOWN_BINDER(&binder);
    return copy;
}


//
//  Rebind_Values_Deep: C
//
// Rebind all words that reference src target to dst target.
// Rebind is always deep.
//
void Rebind_Values_Deep(
    Value* head,
    const Value* tail,
    Context* from,
    Context* to,
    Option(struct Reb_Binder*) binder
) {
    Value* v = head;
    for (; v != tail; ++v) {
        if (Is_Action(v)) {
            //
            // !!! This is a new take on R3-Alpha's questionable feature of
            // deep copying function bodies and rebinding them when a
            // derived object was made.  Instead, if a function is bound to
            // a "base class" of the object we are making, that function's
            // binding pointer (in the function's value cell) is changed to
            // be this object.
            //
            Context* stored = VAL_FRAME_BINDING(v);
            if (stored == UNBOUND) {
                //
                // Leave NULL bindings alone.  Hence, unlike in R3-Alpha, an
                // ordinary FUNC won't forward its references.  An explicit
                // BIND to an object must be performed, or METHOD should be
                // used to do it implicitly.
            }
            else if (REB_FRAME == CTX_TYPE(stored)) {
                //
                // Leave bindings to frame alone, e.g. RETURN's definitional
                // reference...may be an unnecessary optimization as they
                // wouldn't match any derivation since there are no "derived
                // frames" (would that ever make sense?)
            }
            else {
                if (Is_Overriding_Context(stored, to))
                    INIT_VAL_FRAME_BINDING(v, to);
                else {
                    // Could be bound to a reified frame context, or just
                    // to some other object not related to this derivation.
                }
            }
        }
        else if (Is_Antiform(v))
            NOOP;
        else if (Any_Arraylike(v)) {
            const Element* sub_tail;
            Element* sub_at = Cell_Array_At_Mutable_Hack(&sub_tail, v);
            Rebind_Values_Deep(sub_at, sub_tail, from, to, binder);
        }
        else if (Any_Wordlike(v) and BINDING(v) == from) {
            BINDING(v) = to;

            if (binder) {
                REBLEN index = Get_Binder_Index_Else_0(
                    unwrap(binder),
                    Cell_Word_Symbol(v)
                );
                assert(index != 0);
                INIT_VAL_WORD_INDEX(v, index);
            }
        }
    }
}


//
//  Virtual_Bind_Deep_To_New_Context: C
//
// Looping constructs which are parameterized by WORD!s to set each time
// through the loop must copy the body in R3-Alpha's model.  For instance:
//
//    for-each [x y] [1 2 3] [print ["this body must be copied for" x y]]
//
// The reason is because the context in which X and Y live does not exist
// prior to the execution of the FOR-EACH.  And if the body were destructively
// rebound, then this could mutate and disrupt bindings of code that was
// intended to be reused.
//
// (Note that R3-Alpha was somewhat inconsistent on the idea of being
// sensitive about non-destructively binding arguments in this way.
// MAKE OBJECT! purposefully mutated bindings in the passed-in block.)
//
// The context is effectively an ordinary object, and outlives the loop:
//
//     x-word: ~s
//     for-each x [1 2 3] [x-word: 'x, break]
//     get x-word  ; returns 3
//
// Ren-C adds a feature of letting LIT-WORD!s be used to indicate that the
// loop variable should be written into the existing bound variable that the
// LIT-WORD! specified.  If all loop variables are of this form, then no
// copy will be made.
//
// !!! Loops should probably free their objects by default when finished
//
Context* Virtual_Bind_Deep_To_New_Context(
    Value* body_in_out, // input *and* output parameter
    Value* spec
){
    // !!! This just hacks in GROUP! behavior, because the :param convention
    // does not support groups and gives GROUP! by value.  In the stackless
    // build the preprocessing would most easily be done in usermode.
    //
    if (Is_Group(spec)) {
        DECLARE_ATOM (temp);
        if (Do_Any_Array_At_Throws(temp, spec, SPECIFIED))
            fail (Error_No_Catch_For_Throw(TOP_LEVEL));
        Decay_If_Unstable(temp);
        Move_Cell(spec, cast(Value*, temp));
    }

    REBLEN num_vars = Is_Block(spec) ? Cell_Series_Len_At(spec) : 1;
    if (num_vars == 0)
        fail (spec);  // !!! should fail() take unstable?

    const Element* tail;
    const Element* item;

    Specifier* specifier;
    bool rebinding;
    if (Is_Block(spec)) {  // walk the block for errors BEFORE making binder
        specifier = Cell_Specifier(spec);
        item = Cell_Array_At(&tail, spec);

        const Element* check = item;

        rebinding = false;
        for (; check != tail; ++check) {
            if (Is_Blank(check)) {
                // Will be transformed into dummy item, no rebinding needed
            }
            else if (Is_Word(check) or Is_Meta_Word(check))
                rebinding = true;
            else if (not IS_QUOTED_WORD(check)) {
                //
                // Better to fail here, because if we wait until we're in
                // the middle of building the context, the managed portion
                // (keylist) would be incomplete and tripped on by the GC if
                // we didn't do some kind of workaround.
                //
                fail (Error_Bad_Value(check));
            }
        }
    }
    else {
        item = cast(Element*, spec);
        tail = cast(Element*, spec);
        specifier = SPECIFIED;
        rebinding = Is_Word(item) or Is_Meta_Word(item);
    }

    // KeyLists are always managed, but varlist is unmanaged by default (so
    // it can be freed if there is a problem)
    //
    Context* c = Alloc_Context(REB_OBJECT, num_vars);

    // We want to check for duplicates and a Binder can be used for that
    // purpose--but note that a fail() cannot happen while binders are
    // in effect UNLESS the BUF_COLLECT contains information to undo it!
    // There's no BUF_COLLECT here, so don't fail while binder in effect.
    //
    struct Reb_Binder binder;
    if (rebinding)
        INIT_BINDER(&binder);

    const Symbol* duplicate = nullptr;

    SymId dummy_sym = SYM_DUMMY1;

    REBLEN index = 1;
    while (index <= num_vars) {
        const Symbol* symbol;

        if (Is_Blank(item)) {
            if (dummy_sym == SYM_DUMMY9)
                fail ("Current limitation: only up to 9 BLANK! keys");

            symbol = Canon_Symbol(dummy_sym);
            dummy_sym = cast(SymId, cast(int, dummy_sym) + 1);

            Value* var = Append_Context(c, symbol);
            Init_Blank(var);
            Set_Cell_Flag(var, BIND_NOTE_REUSE);
            Set_Cell_Flag(var, PROTECTED);

            goto add_binding_for_check;
        }
        else if (Is_Word(item) or Is_Meta_Word(item)) {
            symbol = Cell_Word_Symbol(item);
            Value* var = Append_Context(c, symbol);

            // !!! For loops, nothing should be able to be aware of this
            // synthesized variable until the loop code has initialized it
            // with something.  But this code is shared with USE, so the user
            // can get their hands on the variable.  Can't be unreadable.
            //
            Init_Trash(var);

            assert(rebinding); // shouldn't get here unless we're rebinding

            if (not Try_Add_Binder_Index(&binder, symbol, index)) {
                //
                // We just remember the first duplicate, but we go ahead
                // and fill in all the keylist slots to make a valid array
                // even though we plan on failing.  Duplicates count as a
                // problem even if they are LIT-WORD! (negative index) as
                // `for-each [x 'x] ...` is paradoxical.
                //
                if (duplicate == nullptr)
                    duplicate = symbol;
            }
        }
        else if (IS_QUOTED_WORD(item)) {

            // A LIT-WORD! indicates that we wish to use the original binding.
            // So `for-each 'x [1 2 3] [...]` will actually set that x
            // instead of creating a new one.
            //
            // !!! Enumerations in the code walks through the context varlist,
            // setting the loop variables as they go.  It doesn't walk through
            // the array the user gave us, so if it's a LIT-WORD! the
            // information is lost.  Do a trick where we put the LIT-WORD!
            // itself into the slot, and give it NODE_FLAG_MARKED...then
            // hide it from the context and binding.
            //
            symbol = Cell_Word_Symbol(item);

          blockscope {
            Value* var = Append_Context(c, symbol);
            Derelativize(var, item, specifier);
            Set_Cell_Flag(var, BIND_NOTE_REUSE);
            Set_Cell_Flag(var, PROTECTED);
          }

          add_binding_for_check:

            // We don't want to stop `for-each ['x 'x] ...` necessarily,
            // because if we're saying we're using the existing binding they
            // could be bound to different things.  But if they're not bound
            // to different things, the last one in the list gets the final
            // assignment.  This would be harder to check against, but at
            // least allowing it doesn't make new objects with duplicate keys.
            // For now, don't bother trying to use a binder or otherwise to
            // stop it.
            //
            // However, `for-each [x 'x] ...` is intrinsically contradictory.
            // So we use negative indices in the binder, which the binding
            // process will ignore.
            //
            if (rebinding) {
                REBINT stored = Get_Binder_Index_Else_0(&binder, symbol);
                if (stored > 0) {
                    if (duplicate == nullptr)
                        duplicate = symbol;
                }
                else if (stored == 0) {
                    Add_Binder_Index(&binder, symbol, -1);
                }
                else {
                    assert(stored == -1);
                }
            }
        }
        else
            fail (item);

        ++item;
        ++index;
    }

    // As currently written, the loop constructs which use these contexts
    // will hold pointers into the arrays across arbitrary user code running.
    // If the context were allowed to expand, then this can cause memory
    // corruption:
    //
    // https://github.com/rebol/rebol-issues/issues/2274
    //
    // !!! Because SERIES_FLAG_DONT_RELOCATE is just a synonym for
    // SERIES_FLAG_FIXED_SIZE at this time, it means that there has to be
    // unwritable cells in the extra capacity, to help catch overwrites.  If
    // we wait too late to add the flag, that won't be true...but if we pass
    // it on creation we can't make the context via Append_Context().  Review
    // this mechanic; and for now forego the protection.
    //
    /* Set_Series_Flag(CTX_VARLIST(c), DONT_RELOCATE); */

    Manage_Series(CTX_VARLIST(c));  // must be managed to use in binding

    if (not rebinding)
        return c;  // nothing else needed to do

    if (not duplicate) {
        //
        // Effectively `Bind_Values_Deep(Array_Head(body_out), context)`
        // but we want to reuse the binder we had anyway for detecting the
        // duplicates.
        //
        Virtual_Bind_Deep_To_Existing_Context(
            body_in_out,
            c,
            &binder,
            REB_WORD
        );
    }

    // Must remove binder indexes for all words, even if about to fail
    //
  blockscope {
    const Key* key_tail;
    const Key* key = CTX_KEYS(&key_tail, c);
    Value* var = CTX_VARS_HEAD(c); // only needed for debug, optimized out
    for (; key != key_tail; ++key, ++var) {
        REBINT stored = Remove_Binder_Index_Else_0(
            &binder, KEY_SYMBOL(key)
        );
        if (stored == 0)
            assert(duplicate);
        else if (stored > 0)
            assert(Not_Cell_Flag(var, BIND_NOTE_REUSE));
        else
            assert(Get_Cell_Flag(var, BIND_NOTE_REUSE));
    }
  }

    SHUTDOWN_BINDER(&binder);

    if (duplicate) {
        DECLARE_ATOM (word);
        Init_Word(word, duplicate);
        fail (Error_Dup_Vars_Raw(word));
    }

    // If the user gets ahold of these contexts, we don't want them to be
    // able to expand them...because things like FOR-EACH have historically
    // not been robust to the memory moving.
    //
    Set_Series_Flag(CTX_VARLIST(c), FIXED_SIZE);

    return c;
}


//
//  Virtual_Bind_Deep_To_Existing_Context: C
//
void Virtual_Bind_Deep_To_Existing_Context(
    Value* any_array,
    Context* context,
    struct Reb_Binder *binder,
    Heart affected
){
    // Most of the time if the context isn't trivially small then it's
    // probably best to go ahead and cache bindings.
    //
    UNUSED(binder);

/*
    // Bind any SET-WORD!s in the supplied code block into the FRAME!, so
    // e.g. APPLY 'APPEND [VALUE: 10]` will set VALUE in exemplar to 10.
    //
    // !!! Today's implementation mutates the bindings on the passed-in block,
    // like R3-Alpha's MAKE OBJECT!.  See Virtual_Bind_Deep_To_New_Context()
    // for potential future directions.
    //
    Bind_Values_Inner_Loop(
        &binder,
        Cell_Array_At_Mutable_Hack(ARG(def)),  // mutates bindings
        exemplar,
        FLAGIT_KIND(REB_SET_WORD),  // types to bind (just set-word!),
        0,  // types to "add midstream" to binding as we go (nothing)
        BIND_DEEP
    );
 */

    BINDING(any_array) = Make_Use_Core(
        context,
        Cell_Specifier(any_array),
        affected
    );
}


#if DEBUG

//
//  Assert_Cell_Binding_Valid_Core: C
//
void Assert_Cell_Binding_Valid_Core(const Cell* cell)
{
    Stub* binding = BINDING(cell);
    if (not binding)
        return;

    Heart heart = Cell_Heart_Unchecked(cell);

    assert(Is_Node(binding));
    assert(Is_Node_Managed(binding));
    assert(Is_Node_A_Stub(binding));
    assert(Not_Node_Free(binding));

    if (heart == REB_FRAME) {
        assert(IS_VARLIST(binding));  // actions/frames bind to contexts only
        return;
    }

    if (IS_LET(binding)) {
        if (Any_Word_Kind(heart))
            assert(VAL_WORD_INDEX_I32(cell) == INDEX_PATCHED);
        return;
    }

    if (
        IS_VARLIST(binding)
        and CTX_TYPE(cast(Context*, binding)) == REB_MODULE
    ){
        if (not (
            Any_Array_Kind(heart)
            or Any_Sequence_Kind(heart)
            or heart == REB_COMMA  // feed cells, use for specifier ATM
        )){
            assert(Any_Word_Kind(heart));
            assert(VAL_WORD_INDEX_I32(cell) == INDEX_ATTACHED);
        }
    }
}

#endif
