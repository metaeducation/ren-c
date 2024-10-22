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
    VarList* context,
    Option(SymId) add_midstream_types,
    Flags flags
){
    Element* v = head;
    for (; v != tail; ++v) {
        if (Any_Wordlike(v)) {
          const Symbol* symbol = Cell_Word_Symbol(v);

          if (CTX_TYPE(context) == REB_MODULE) {
            bool strict = true;
            Value* lookup = MOD_VAR(cast(SeaOfVars*, context), symbol, strict);
            if (lookup) {
                Tweak_Cell_Word_Index(v, INDEX_PATCHED);
                BINDING(v) = Singular_From_Cell(lookup);
            }
            else if (
                add_midstream_types == SYM_ANY
                or (
                    add_midstream_types == SYM_SET
                    and Is_Set_Word(v)
                )
            ){
                Init_Nothing(Append_Context_Bind_Word(context, v));
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
                assert(n <= Varlist_Len(context));

                // We're overwriting any previous binding, which may have
                // been relative.

                Tweak_Cell_Word_Index(v, n);
                BINDING(v) = context;
            }
            else if (
                add_midstream_types == SYM_ANY
                or (
                    add_midstream_types == SYM_SET
                    and Is_Set_Word(v)
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
            if (Any_Listlike(v)) {
                const Element* sub_tail;
                Element* sub_at = Cell_List_At_Mutable_Hack(&sub_tail, v);
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

    VarList* c = Cell_Varlist(context);

    // Associate the canon of a word with an index number.  (This association
    // is done by poking the index into the Stub of the Symbol behind the
    // ANY-WORD?, so it must be cleaned up to not break future bindings.)
    //
  if (not Is_Module(context)) {
    REBLEN index = 1;
    const Key* key_tail;
    const Key* key = Varlist_Keys(&key_tail, c);
    const Value* var = Varlist_Slots_Head(c);
    for (; key != key_tail; key++, var++, index++)
        Add_Binder_Index(&binder, Key_Symbol(key), index);
  }

    Bind_Values_Inner_Loop(
        &binder,
        head,
        tail,
        c,
        add_midstream_types,
        flags
    );

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
    Option(VarList*) context,
    bool deep
){
    Element* v = head;
    for (; v != tail; ++v) {
        if (
            Any_Wordlike(v)
            and (not context or BINDING(v) == unwrap context)
        ){
            Unbind_Any_Word(v);
        }
        else if (Any_Listlike(v) and deep) {
            const Element* sub_tail;
            Element* sub_at = Cell_List_At_Mutable_Hack(&sub_tail, v);
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
        Stub* patch = maybe MOD_PATCH(
            cast(SeaOfVars*, Cell_Varlist(context)),
            Cell_Word_Symbol(word),
            strict
        );
        if (not patch)
            return false;
        Tweak_Cell_Word_Index(word, INDEX_PATCHED);
        BINDING(word) = patch;
        return true;
    }

    Option(Index) index = Find_Symbol_In_Context(
        context,
        Cell_Word_Symbol(word),
        strict
    );
    if (not index)
        return false;

    Tweak_Cell_Word_Index(word, unwrap index);  // ^-- may have been relative
    BINDING(word) = Cell_Varlist(context);
    return true;
}


//
//  Make_Let_Variable: C
//
// Efficient form of "mini-object" allocation that can hold exactly one
// variable.  Unlike a context, it does not have the ability to hold an
// archetypal form of that context...because the only value cell in the
// singular array is taken for the variable content itself.
//
// 1. The way it is designed, the list of lets terminates in either a nullptr
//    or a context pointer that represents the specifying frame for the chain.
//    So we can simply point to the existing binding...whether it is a let,
//    a use, a frame context, or nullptr.
//
Let* Make_Let_Variable(
    const Symbol* symbol,
    Context* parent
){
    Stub* let = Alloc_Singular(  // payload is one variable
        FLAG_FLAVOR(LET)
            | NODE_FLAG_MANAGED
            | FLEX_FLAG_LINK_NODE_NEEDS_MARK  // link to next virtual bind
            | FLEX_FLAG_INFO_NODE_NEEDS_MARK  // inode of symbol
    );

    Init_Nothing(x_cast(Value*, Stub_Cell(let)));  // start as unset

    if (parent) {
        assert(
            Is_Stub_Let(parent)
            or Is_Stub_Use(parent)
            or Is_Stub_Varlist(parent)
        );
        assert(Is_Node_Managed(parent));
    }
    LINK(NextLet, let) = parent;  // linked list [1]

    MISC(LetReserved, let) = nullptr;  // not currently used

    INODE(LetSymbol, let) = symbol;  // surrogate for context "key"

    return let;
}


#define CELL_FLAG_BIND_NOTE_REUSE CELL_FLAG_NOTE


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
// 1. We want to continue the next_context loop from inside sub-loops, which
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
Option(Stub*) Get_Word_Container(
    REBLEN *index_out,
    const Element* any_word,
    Context* context
){
    Corrupt_If_Debug(*index_out);  // corrupt index to make sure it gets set

    Context* binding = BINDING(any_word);
    const Symbol* symbol = Cell_Word_Symbol(any_word);

    if (IS_WORD_BOUND(any_word)) {  // leave binding alone
        *index_out = VAL_WORD_INDEX(any_word);
        return binding;
    }

    Context* c = context;

  #if DEBUG
    Corrupt_Pointer_If_Debug(context);  // make sure we use `c` below
    Context* context_in = c;  // save in local for easier debugging
    USED(context_in);
  #endif

    while (c) {
        goto loop_body;  // avoid compiler warnings on `goto next_context` [1]

      next_context:
        c = Context_Parent(c);
        continue;

      loop_body:

        if (Is_Stub_Varlist(c)) {
            VarList* vlist = cast(VarList*, c);

            if (CTX_TYPE(vlist) == REB_MODULE) {
                Value* slot = MOD_VAR(cast(SeaOfVars*, vlist), symbol, true);
                if (slot) {
                    *index_out = INDEX_PATCHED;
                    return Singular_From_Cell(slot);
                }

                goto next_context;
            }

            if (
                CTX_TYPE(vlist) == REB_FRAME
                and binding  // word has a cache for if it's in an action frame
                and Action_Is_Base_Of(
                    cast(Action*, binding),
                    CTX_FRAME_PHASE(vlist)
                )
            ){
                assert(CELL_WORD_INDEX_I32(any_word) <= 0);
                if (CELL_WORD_INDEX_I32(any_word) == 0)
                    goto next_context;
                *index_out = -(CELL_WORD_INDEX_I32(any_word));
                return vlist;
            }

            Option(Index) index = Find_Symbol_In_Context(  // must search
                Varlist_Archetype(vlist),
                symbol,
                true
            );

            // Note: if frame, caching here seems to slow things down?
          #ifdef CACHE_FINDINGS_BUT_SEEMS_TO_SLOW_THINGS_DOWN
            if (CTX_TYPE(vlist) == REB_FRAME) {
                if (CELL_WORD_INDEX_I32(any_word) <= 0) {  // cache in unbounds
                    CELL_WORD_INDEX_I32(
                        m_cast(Cell*, any_word)
                    ) = -(maybe index);
                    BINDING(m_cast(Cell*, any_word)) = CTX_FRAME_PHASE(vlist);
                }
            }
          #endif

            if (index) {
                *index_out = unwrap index;
                return vlist;
            }

          goto next_context;
        }

        if (Is_Stub_Let(c)) {
            if (INODE(LetSymbol, c) == symbol) {
                *index_out = INDEX_PATCHED;
                return c;
            }
            goto next_context;
        }

        assert(Is_Stub_Use(c));

        if (  // some USEs only affect SET-WORD!s
            Get_Cell_Flag(Stub_Cell(c), USE_NOTE_SET_WORDS)
            and not Is_Set_Word(any_word)
        ){
            goto next_context;
        }

        if (Is_Module(Stub_Cell(c))) {
            SeaOfVars* sea = cast(SeaOfVars*, Cell_Varlist(Stub_Cell(c)));

            Value* var = MOD_VAR(sea, symbol, true);
            if (var) {
                *index_out = INDEX_PATCHED;
                return Singular_From_Cell(var);
            }
            goto next_context;
        }

        if (Is_Word(Stub_Cell(c))) {  // OVERBIND use of single WORD!
            Element* word = u_cast(Element*, Stub_Cell(c));
            if (Cell_Word_Symbol(word) == symbol) {
                *index_out = VAL_WORD_INDEX(word);
                return BINDING(word);
            }
            goto next_context;
        }

        VarList* overload = Cell_Varlist(Stub_Cell(c));

        REBLEN index = 1;
        const Key* key_tail;
        const Key* key = Varlist_Keys(&key_tail, overload);
        for (; key != key_tail; ++key, ++index) {
            if (Key_Symbol(key) != symbol)
                continue;

            *index_out = index;
            return overload;
        }

        goto next_context;
    }

    return nullptr;
}


//
//  /let: native [
//
//  "Dynamically add a new binding into the stream of evaluation"
//
//      return: "Expression result if SET form, else gives the new vars"
//          [any-value?]
//      'vars "Variable(s) to create"  ; can't soft quote ATM [0]
//          [word! block! group! set-run-word? set-word? set-block? set-group?]
//      @expression "Optional Expression to assign"
//          [<variadic> element?]
//  ]
//
DECLARE_NATIVE(let)
//
// 0. There's a contention at the moment with `let (...): default [...]`, as
//    we want the LET to win.  So this means we have to make left win in a
//    prioritization battle with right, if they're both soft literal.  At
//    the moment, left will defer if right is literal at all.  It needs some
//    conscious tweaking when there is time for it...but the code was
//    written to do the eval here in LET with a hard literal...works for now.
//
// 1. Though LET shows as a variadic function on its interface, it does not
//    need to use the variadic argument...since it is a native (and hence
//    can access the frame and feed directly).
//
// 2. For convenience, the group can evaluate to a SET-BLOCK,  e.g.
//
//        block: $[x y]:
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
// 4. Right now what is permitted is conservative.  Bias it so that if you
//    want something to just "pass through the LET" that you use a quote mark
//    on it, and the LET will ignore it.
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
// 6. Once the multi-return dialect was planned to have more features like
//    naming arguments literally.  That wouldn't have any meaning to LET and
//    would be skipped.  That feature of naming outputs has been scrapped,
//    though...so questions about what to do if things like integers etc.
//    appear in blocks are open at this point.
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
// 8. When it was looking at infix, the evaluator caches the fetched value of
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
    Context* L_binding = Level_Binding(L);

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
        if (Eval_Any_List_At_Throws(SPARE, vars, SPECIFIED))
            return THROWN;

        Decay_If_Unstable(SPARE);

        if (Is_Quoted(SPARE))  // should (let 'x: <whatever>) be legal? [3]
            fail ("QUOTED? escapes not supported at top level of LET");

        if (
            Try_Get_Settable_Word_Symbol(cast(Element*, SPARE))
            or Is_Set_Block(stable_SPARE)
        ){
            // Allow `(set-word):` to ignore redundant colon [2]
        }
        else if (Is_Word(stable_SPARE) or Is_Block(stable_SPARE)) {
            if (Is_Set_Group(vars))
                Setify(cast(Element*, SPARE));  //  let ('word): -> let word:
        }
        else
            fail ("LET GROUP! limited to WORD! and BLOCK!");  // [4]

        vars = stable_SPARE;
    }

    //=//// GENERATE NEW BLOCK IF QUOTED? OR GROUP! ELEMENTS ///////////////=//

    // Writes rebound copy of `vars` to SPARE if it's a SET-WORD! or SET-BLOCK!
    // so it can be used in a reevaluation.  For WORD!/BLOCK! forms of LET it
    // just writes the rebound copy into the OUT cell.
    //
    // 1. It would be nice if we could just copy the input variable and
    //    rewrite the binding, but at time of writing /foo: is not binding
    //    compatible with WORD!...it has a pairing allocation for the chain
    //    holding the word and a blank, and no binding index of its own.
    //    This will all be reviewed at some point, but for now we do a
    //    convoluted rebuilding of the matching structure from a word basis.

    Context* bindings = L_binding;  // context chain we may be adding to

    if (bindings and Not_Node_Managed(bindings))
        Set_Node_Managed_Bit(bindings);  // natives don't always manage

    const Symbol* symbol;
    if (
        (Is_Word(vars) and (symbol = Cell_Word_Symbol(vars)))
        or
        (symbol = maybe Try_Get_Settable_Word_Symbol(cast(Element*, vars)))
    ){
        bindings = Make_Let_Variable(symbol, bindings);

        Sink(Element) where;
        if (Is_Word(vars))
            where = OUT;
        else {
            STATE = ST_LET_EVAL_STEP;
            where = SPARE;
        }

        Init_Any_Word_Bound(where, REB_WORD, symbol, bindings, INDEX_PATCHED);
        if (HEART_BYTE(vars) != REB_WORD) {  // more complex than we'd like [1]
            Setify(where);
            if (HEART_BYTE(vars) == REB_PATH) {
                Option(Error*) error = Trap_Blank_Head_Or_Tail_Sequencify(
                    where, REB_PATH, CELL_FLAG_LEADING_BLANK
                );
                assert(not error);  // was a path when we got it!
                UNUSED(error);
            }
            else
                assert(HEART_BYTE(vars) == REB_CHAIN);
        }

        Corrupt_Pointer_If_Debug(vars);  // if in spare, we may have overwritten
    }
    else {
        assert(Is_Block(vars) or Is_Set_Block(vars));

        const Element* tail;
        const Element* item = Cell_List_At(&tail, vars);
        Context* item_binding = Cell_List_Binding(vars);

        assert(TOP_INDEX == STACK_BASE);

        bool altered = false;

        for (; item != tail; ++item) {
            const Element* temp = item;
            Context* temp_binding = item_binding;

            if (Is_Quoted(temp)) {
                Derelativize(PUSH(), temp, temp_binding);
                Unquotify(TOP, 1);  // drop quote in output block [5]
                altered = true;
                continue;  // do not make binding
            }

            if (Is_Group(temp)) {  // evaluate non-QUOTED? groups in LET block
                if (Eval_Any_List_At_Throws(OUT, temp, item_binding))
                    return THROWN;

                if (Is_Void(OUT)) {
                    Init_Blank(OUT);
                }
                else if (Is_Antiform(OUT))
                    fail (Error_Bad_Antiform(OUT));

                temp = cast(Element*, OUT);
                temp_binding = SPECIFIED;

                altered = true;
            }

            if (Is_Set_Word(temp))
                goto wordlike;
            else switch (Cell_Heart(temp)) {  // permit quasi
              case REB_ISSUE:  // is multi-return opt-in for dialect, passthru
              case REB_BLANK:  // is multi-return opt-out for dialect, passthru
                Derelativize(PUSH(), temp, temp_binding);
                break;

              wordlike:
              case REB_WORD:
              case REB_META_WORD:
              case REB_THE_WORD: {
                Derelativize(PUSH(), temp, temp_binding);  // !!! no derel
                symbol = Cell_Word_Symbol(temp);
                bindings = Make_Let_Variable(symbol, bindings);
                CELL_WORD_INDEX_I32(TOP) = INDEX_PATCHED;
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
            assert(Is_Set_Block(vars));
            Setify(Init_Any_List(
                where,  // may be SPARE, and vars may point to it
                REB_BLOCK,
                Pop_Stack_Values_Core(STACK_BASE, NODE_FLAG_MANAGED)
            ));
        }
        else {
            Drop_Data_Stack_To(STACK_BASE);

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
    //     let /assert: specialize assert/ [handler: [print "should work!"]]
    //
    // Leverage same mechanism as REEVAL to preload the next execution step
    // with the rebound SET-WORD! or SET-BLOCK!

    BINDING(bindings_holder) = bindings;
    Corrupt_Pointer_If_Debug(bindings);  // catch uses after this point in scope

    if (STATE != ST_LET_EVAL_STEP) {
        assert(Is_Word(OUT) or Is_Block(OUT));  // should have written output
        goto update_feed_binding;
    }

    assert(
        Try_Get_Settable_Word_Symbol(cast(Element*, SPARE))
        or Is_Set_Block(stable_SPARE)
    );

    Flags flags =
        FLAG_STATE_BYTE(ST_STEPPER_REEVALUATING)
        | (L->flags.bits & EVAL_EXECUTOR_FLAG_FULFILLING_ARG)
        | (L->flags.bits & LEVEL_FLAG_RAISED_RESULT_OK);

    Level* sub = Make_Level(&Stepper_Executor, LEVEL->feed, flags);
    Copy_Cell(&sub->u.eval.current, cast(Element*, SPARE));
    sub->u.eval.current_gotten = nullptr;

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

    Context* bindings = Cell_List_Binding(bindings_holder);
    BINDING(FEED_SINGLE(L->feed)) = bindings;

    if (Is_Pack(OUT))
        Decay_If_Unstable(OUT);

    return OUT;
}}


//
//  /add-let-binding: native [
//
//  "Experimental function for adding a new variable binding"
//
//      return: [frame! any-list?]
//      environment [frame! any-list?]
//      word [word!]
//      value [any-value?]
//  ]
//
DECLARE_NATIVE(add_let_binding)
//
// !!! At time of writing, there are no "first class environments" that
// expose the "Specifier" chain in arrays.  So the arrays themselves are
// used as proxies for that.  Usermode dialects (like UPARSE) update their
// environment by passing in a rule block with a version of that rule block
// with an updated binding.  A function that wants to add to the evaluator
// environment uses the frame at the moment.
{
    INCLUDE_PARAMS_OF_ADD_LET_BINDING;

    Value* env = ARG(environment);
    Context* parent;

    if (Is_Frame(env)) {
        Level* L = Level_Of_Varlist_May_Fail(Cell_Varlist(env));
        parent = Level_Binding(L);
        if (parent)
            Set_Node_Managed_Bit(parent);
    } else {
        assert(Any_List(env));
        parent = Cell_List_Binding(env);
    }

    Let* let = Make_Let_Variable(Cell_Word_Symbol(ARG(word)), parent);

    Move_Cell(Stub_Cell(let), ARG(value));

    if (Is_Frame(env)) {
        Level* L = Level_Of_Varlist_May_Fail(Cell_Varlist(env));
        BINDING(FEED_SINGLE(L->feed)) = let;
    }
    else {
        BINDING(env) = let;
    }

    return COPY(env);
}


//
//  /add-use-object: native [
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

    Element* object = cast(Element*, ARG(object));

    Level* L = Level_Of_Varlist_May_Fail(Cell_Varlist(ARG(frame)));
    Context* L_binding = Level_Binding(L);

    if (L_binding)
        Set_Node_Managed_Bit(L_binding);

    Use* use = Make_Use_Core(object, L_binding, CELL_MASK_0);

    BINDING(FEED_SINGLE(L->feed)) = use;

    return NOTHING;
}


//
//  Clonify_And_Bind_Relative: C
//
// Clone the Flex embedded in a value *if* it's in the given set of types
// (and if "cloning" makes sense for them, e.g. they are not simple scalars).
//
// Note: The resulting clones will be managed.  The model for lists only
// allows the topmost level to contain unmanaged values...and we *assume* the
// values we are operating on here live in an array.
//
// 1. In Ren-C's binding model, function bodies are conceptually unbound, and
//    need the binding of the frame instance plus whatever binding was
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
// 3. If we're cloning a sequence, we have to copy the mirror byte.  If it's
//    a plain array that happens to have been aliased somewhere as a sequence,
//    we don't know if it's going to be aliased as that same sequence type
//    again...but is that worth testing if it's a sequence here?
//
void Clonify_And_Bind_Relative(
    Value* v,
    Flags flags,
    bool deeply,
    Option(struct Reb_Binder*) binder,
    Option(Action*) relative
){
    assert(flags & NODE_FLAG_MANAGED);

    Heart heart = Cell_Heart_Unchecked(v);

    if (
        relative
        and Any_Wordlike(v)
        and IS_WORD_UNBOUND(v)  // use unbound words as "in frame" cache [1]
    ){
        REBINT n = Get_Binder_Index_Else_0(unwrap binder, Cell_Word_Symbol(v));
        CELL_WORD_INDEX_I32(v) = -(n);  // negative or zero signals unbound [2]
        BINDING(v) = unwrap relative;
    }
    else if (deeply and (Any_Series_Kind(heart) or Any_Sequence_Kind(heart))) {
        //
        // Objects and Flexes get shallow copied at minimum
        //
        Element* deep = nullptr;
        Element* deep_tail = nullptr;

        if (Any_Pairlike(v)) {
            Pairing* copy = Copy_Pairing(
                Cell_Pairing(v),
                NODE_FLAG_MANAGED
            );
            Tweak_Cell_Pairing(v, copy);

            deep = Pairing_Head(copy);
            deep_tail = Pairing_Tail(copy);
        }
        else if (Any_Listlike(v)) {  // ruled out pairlike sequences above...
            Array* copy = Copy_Array_At_Extra_Shallow(
                Cell_Array(v),
                0,  // !!! what if VAL_INDEX() is nonzero?
                0,
                NODE_FLAG_MANAGED
            );
            /* if (Any_Sequence(v)) */  // copy regardless? [3]
                Copy_Mirror_Byte(copy, Cell_Array(v));

            Tweak_Cell_Node1(v, copy);

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
            Flex* copy = Copy_Flex_Core(Cell_Flex(v), NODE_FLAG_MANAGED);
            Tweak_Cell_Node1(v, copy);
        }

        // If we're going to copy deeply, we go back over the shallow
        // copied Array and "clonify" the values in it.
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
    while (Try_Advance_Evars(&e))
        Add_Binder_Index(&binder, Key_Symbol(e.key), e.index);
    Shutdown_Evars(&e);
  }

    Array* copy;

  blockscope {
    const Array* original = Cell_Array(body);
    REBLEN index = VAL_INDEX(body);
   /* Context* binding = Cell_List_Binding(body); */
    REBLEN tail = Cell_Series_Len_At(body);
    assert(tail <= Array_Len(original));

    if (index > tail)  // !!! should this be asserted?
        index = tail;

    Flags flags = ARRAY_MASK_HAS_FILE_LINE | NODE_FLAG_MANAGED;
    bool deeply = true;

    REBLEN len = tail - index;

    // Currently we start by making a shallow copy and then adjust it

    copy = Make_Array_For_Copy(len, flags, original);
    Set_Flex_Len(copy, len);

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

    SHUTDOWN_BINDER(&binder);
    return copy;
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
//     x-word: ~
//     for-each x [1 2 3] [x-word: $x, break]
//     get x-word  ; returns 3
//
// Ren-C adds a feature of letting THE-WORD!s be used to indicate that the
// loop variable should be written into the existing bound variable that the
// THE-WORD! specified.  If all loop variables are of this form, then no
// copy will be made.
//
// !!! Loops should probably free their objects by default when finished
//
VarList* Virtual_Bind_Deep_To_New_Context(
    Value* body_in_out, // input *and* output parameter
    Value* spec
){
    // !!! This just hacks in GROUP! behavior, because the :param convention
    // does not support groups and gives GROUP! by value.  In the stackless
    // build the preprocessing would most easily be done in usermode.
    //
    if (Is_Group(spec)) {
        DECLARE_ATOM (temp);
        if (Eval_Any_List_At_Throws(temp, spec, SPECIFIED))
            fail (Error_No_Catch_For_Throw(TOP_LEVEL));
        Decay_If_Unstable(temp);
        Move_Cell(spec, cast(Value*, temp));
    }

    REBLEN num_vars = Is_Block(spec) ? Cell_Series_Len_At(spec) : 1;
    if (num_vars == 0)
        fail (spec);  // !!! should fail() take unstable?

    const Element* tail;
    const Element* item;

    Context* binding;  // needed if looking up @var to write to
    bool rebinding;
    if (Is_Block(spec)) {  // walk the block for errors BEFORE making binder
        binding = Cell_List_Binding(spec);
        item = Cell_List_At(&tail, spec);

        const Element* check = item;

        rebinding = false;
        for (; check != tail; ++check) {
            if (Is_Blank(check)) {
                // Will be transformed into dummy item, no rebinding needed
            }
            else if (Is_Word(check) or Is_Meta_Word(check))
                rebinding = true;
            else if (not Is_The_Word(check)) {
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
        binding = SPECIFIED;
        rebinding = Is_Word(item) or Is_Meta_Word(item);
    }

    // KeyLists are always managed, but varlist is unmanaged by default (so
    // it can be freed if there is a problem)
    //
    VarList* c = Alloc_Varlist(REB_OBJECT, num_vars);

    // We want to check for duplicates and a Binder can be used for that
    // purpose--but note that a fail() cannot happen while binders are
    // in effect UNLESS the BUF_COLLECT contains information to undo it!
    // There's no BUF_COLLECT here, so don't fail while binder in effect.
    //
    struct Reb_Binder binder;
    if (rebinding)
        INIT_BINDER(&binder);

    Option(Error*) error = nullptr;

    SymId dummy_sym = SYM_DUMMY1;

    REBLEN index = 1;
    while (index <= num_vars) {
        const Symbol* symbol;

        if (Is_Blank(item)) {
            if (dummy_sym == SYM_DUMMY9)
                fail ("Current limitation: only up to 9 foreign/blank keys");

            symbol = Canon_Symbol(dummy_sym);
            dummy_sym = cast(SymId, cast(int, dummy_sym) + 1);

            Value* var = Append_Context(c, symbol);
            Init_Blank(var);
            Set_Cell_Flag(var, BIND_NOTE_REUSE);
            Set_Cell_Flag(var, PROTECTED);

            if (rebinding)
                Add_Binder_Index(&binder, symbol, -1);  // for remove
        }
        else if (Is_Word(item) or Is_Meta_Word(item)) {
            assert(rebinding); // shouldn't get here unless we're rebinding

            symbol = Cell_Word_Symbol(item);

            if (Try_Add_Binder_Index(&binder, symbol, index)) {
                Value* var = Append_Context(c, symbol);
                Init_Nothing(var);  // code shared with USE, user may see
            }
            else {  // note for-each [x @x] is bad, too
                DECLARE_ELEMENT (word);
                Init_Word(word, symbol);
                error = Error_Dup_Vars_Raw(word);
                break;
            }
        }
        else if (Is_The_Word(item)) {

            // A THE-WORD! indicates that we wish to use the original binding.
            // So `for-each @x [1 2 3] [...]` will actually set that x
            // instead of creating a new one.
            //
            // !!! Enumerations in the code walks through the context varlist,
            // setting the loop variables as they go.  It doesn't walk through
            // the array the user gave us, so if it's a THE-WORD! the
            // information is lost.  Do a trick where we put the THE-WORD!
            // itself into the slot, and give it CELL_FLAG_NOTE...then
            // hide it from the context and binding.
            //
            if (dummy_sym == SYM_DUMMY9)
                fail ("Current limitation: only up to 9 foreign/blank keys");

            symbol = Canon_Symbol(dummy_sym);
            dummy_sym = cast(SymId, cast(int, dummy_sym) + 1);

            Value* var = Append_Context(c, symbol);
            Derelativize(var, item, binding);
            Set_Cell_Flag(var, BIND_NOTE_REUSE);
            Set_Cell_Flag(var, PROTECTED);

            if (rebinding)
                Add_Binder_Index(&binder, symbol, -1);  // for remove
        }
        else {
            error = Error_User("Bad datatype in variable spec");
            break;
        }

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
    // !!! Because FLEX_FLAG_DONT_RELOCATE is just a synonym for
    // FLEX_FLAG_FIXED_SIZE at this time, it means that there has to be
    // unwritable cells in the extra capacity, to help catch overwrites.  If
    // we wait too late to add the flag, that won't be true...but if we pass
    // it on creation we can't make the context via Append_Context().  Review
    // this mechanic; and for now forego the protection.
    //
    /* Set_Flex_Flag(c, DONT_RELOCATE); */

    if (rebinding)  // even if failing, must remove bind indices for words
        SHUTDOWN_BINDER(&binder);

    if (error) {
        Free_Unmanaged_Flex(c);
        fail (unwrap error);
    }

    Manage_Flex(c);  // must be managed to use in binding

    // If the user gets ahold of these contexts, we don't want them to be
    // able to expand them...because things like FOR-EACH have historically
    // not been robust to the memory moving.
    //
    Set_Flex_Flag(c, FIXED_SIZE);

    // Effectively `Bind_Values_Deep(Array_Head(body_out), context)`
    // but we want to reuse the binder we had anyway for detecting the
    // duplicates.
    //
    if (rebinding)
        Virtual_Bind_Deep_To_Existing_Context(
            body_in_out,
            c,
            &binder,
            CELL_MASK_0
        );

    return c;
}


//
//  Real_Var_From_Pseudo: C
//
// Virtual_Bind_To_New_Context() allows THE-WORD! syntax to reuse an existing
// variables binding:
//
//     x: 10
//     for-each @x [20 30 40] [...]
//     ; The 10 will be overwritten, and x will be equal to 40, here
//
// It accomplishes this by putting a word into the "variable" slot, and having
// a flag to indicate a dereference is necessary.
//
Value* Real_Var_From_Pseudo(Value* pseudo_var) {
    if (Not_Cell_Flag(pseudo_var, BIND_NOTE_REUSE))
        return pseudo_var;
    if (Is_Blank(pseudo_var))  // e.g. `for-each _ [1 2 3] [...]`
        return nullptr;  // signal to throw generated quantity away

    // Note: these variables are fetched across running arbitrary user code.
    // So the address cannot be cached...e.g. the object it lives in might
    // expand and invalidate the location.  (The `context` for fabricated
    // variables is locked at fixed size.)
    //
    assert(Is_The_Word(pseudo_var));
    return Lookup_Mutable_Word_May_Fail(cast(Element*, pseudo_var), SPECIFIED);
}


//
//  Virtual_Bind_Deep_To_Existing_Context: C
//
void Virtual_Bind_Deep_To_Existing_Context(
    Value* list,
    VarList* context,
    struct Reb_Binder *binder,
    Flags note
){
    // Most of the time if the context isn't trivially small then it's
    // probably best to go ahead and cache bindings.
    //
    UNUSED(binder);

    BINDING(list) = Make_Use_Core(
        Varlist_Archetype(context),
        Cell_List_Binding(list),
        note
    );
}


#if DEBUG

//
//  Assert_Cell_Binding_Valid_Core: C
//
void Assert_Cell_Binding_Valid_Core(const Cell* cell)
{
    /* assert(Is_Bindable_Heart(cell)); */  // called with nullptr on text/etc.

    Context* binding = BINDING(cell);  // read doesn't assert, only write
    if (not binding)
        return;

    Heart heart = Cell_Heart_Unchecked(cell);
    if (heart != REB_COMMA)  // weird trick used by va_list feeds
        assert(Is_Bindable_Heart(heart));

    assert(Is_Node(binding));
    assert(Is_Node_Managed(binding));
    assert(Is_Node_A_Stub(binding));
    assert(Not_Node_Free(binding));

    if (heart == REB_FRAME) {
        assert(Is_Stub_Varlist(binding));  // actions/frames bind contexts only
        return;
    }

    if (Is_Stub_Let(binding)) {
        if (Any_Wordlike(cell))
            assert(CELL_WORD_INDEX_I32(cell) == INDEX_PATCHED);
        return;
    }

    if (Is_Stub_Patch(binding)) {
        assert(
            Any_Wordlike(cell)
            and CELL_WORD_INDEX_I32(cell) == INDEX_PATCHED
        );
        return;
    }

    if (Is_Stub_Use(binding)) {
        assert(Any_Listlike(cell));  // can't bind words to use
        return;
    }

    if (Is_Stub_Details(binding)) {  // relative binding
        /* assert(Any_Listlike(cell)); */  // weird word cache trick uses
        return;
    }

    assert(Is_Stub_Varlist(binding));  // or SeaOfVars...

    if (CTX_TYPE(cast(VarList*, binding)) == REB_MODULE) {
        assert(
            Any_Listlike(cell)
            or heart == REB_COMMA  // feed cells, use for binding ATM
        );
        // attachment binding no longer exists
    }
}

#endif
