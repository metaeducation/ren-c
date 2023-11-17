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
    Cell* head,
    const Cell* tail,
    Context* context,
    REBU64 bind_types, // !!! REVIEW: force word types low enough for 32-bit?
    REBU64 add_midstream_types,
    Flags flags
){
    Cell* v = head;
    for (; v != tail; ++v) {
        NoQuote(const Cell*) cell = VAL_UNESCAPED(v);
        enum Reb_Kind heart = CELL_HEART(cell);

        // !!! Review use of `heart` bit here, e.g. when a REB_PATH has an
        // REB_BLOCK heart, why would it be bound?  Problem is that if we
        // do not bind `/` when REB_WORD is asked then `/` won't be bound.
        //
        REBU64 type_bit = FLAGIT_KIND(heart);

        if (type_bit & bind_types) {
            const Symbol* symbol = VAL_WORD_SYMBOL(cell);

          if (CTX_TYPE(context) == REB_MODULE) {
            bool strict = true;
            REBVAL *lookup = MOD_VAR(context, symbol, strict);
            if (lookup) {
                INIT_VAL_WORD_BINDING(v, Singular_From_Cell(lookup));
                INIT_VAL_WORD_INDEX(v, 1);
            }
            else if (type_bit & add_midstream_types) {
                Finalize_None(Append_Context_Bind_Word(context, v));
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

                INIT_VAL_WORD_BINDING(v, context);
                INIT_VAL_WORD_INDEX(v, n);
            }
            else if (type_bit & add_midstream_types) {
                //
                // Word is not in context, so add it if option is specified
                //
                Append_Context_Bind_Word(context, v);
                Add_Binder_Index(binder, symbol, VAL_WORD_INDEX(v));
            }
          }
        }
        else if (flags & BIND_DEEP) {
            if (ANY_ARRAYLIKE(v)) {
                const Cell* sub_tail;
                Cell* sub_at = VAL_ARRAY_AT_MUTABLE_HACK(
                    &sub_tail,
                    VAL_UNESCAPED(v)
                );
                Bind_Values_Inner_Loop(
                    binder,
                    sub_at,
                    sub_tail,
                    context,
                    bind_types,
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
// Bind words in an array of values terminated with END
// to a specified context.  See warnings on the functions like
// Bind_Values_Deep() about not passing just a singular REBVAL.
//
// NOTE: If types are added, then they will be added in "midstream".  Only
// bindings that come after the added value is seen will be bound.
//
void Bind_Values_Core(
    Cell* head,
    const Cell* tail,
    const Cell* context,
    REBU64 bind_types,
    REBU64 add_midstream_types,
    Flags flags // see %sys-core.h for BIND_DEEP, etc.
) {
    struct Reb_Binder binder;
    INIT_BINDER(&binder);

    Context* c = VAL_CONTEXT(context);

    // Associate the canon of a word with an index number.  (This association
    // is done by poking the index into the stub of the series behind the
    // ANY-WORD!, so it must be cleaned up to not break future bindings.)
    //
  if (not IS_MODULE(context)) {
    REBLEN index = 1;
    const REBKEY *key_tail;
    const REBKEY *key = CTX_KEYS(&key_tail, c);
    const REBVAR *var = CTX_VARS_HEAD(c);
    for (; key != key_tail; key++, var++, index++)
        Add_Binder_Index(&binder, KEY_SYMBOL(key), index);
  }

    Bind_Values_Inner_Loop(
        &binder,
        head,
        tail,
        c,
        bind_types,
        add_midstream_types,
        flags
    );

  if (not IS_MODULE(context)) {  // Reset all the binder indices to zero
    const REBKEY *key_tail;
    const REBKEY *key = CTX_KEYS(&key_tail, c);
    const REBVAR *var = CTX_VARS_HEAD(c);
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
    Cell* head,
    const Cell* tail,
    Option(Context*) context,
    bool deep
){
    Cell* v = head;
    for (; v != tail; ++v) {
        if (
            ANY_WORDLIKE(v)
            and (not context or BINDING(v) == unwrap(context))
        ){
            Unbind_Any_Word(v);
        }
        else if (ANY_ARRAYLIKE(v) and deep) {
            const Cell* sub_tail;
            Cell* sub_at = VAL_ARRAY_AT_MUTABLE_HACK(&sub_tail, v);
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
REBLEN Try_Bind_Word(const Cell* context, REBVAL *word)
{
    const bool strict = true;
    REBLEN n = Find_Symbol_In_Context(
        context,
        VAL_WORD_SYMBOL(word),
        strict
    );
    if (n != 0) {
        INIT_VAL_WORD_BINDING(word, VAL_CONTEXT(context));
        INIT_VAL_WORD_INDEX(word, n);  // ^-- may have been relative
    }
    return n;
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
Array* Make_Let_Patch(
    const Symbol* symbol,
    REBSPC *specifier
){
    Array* let = Alloc_Singular(  // payload is one variable
        FLAG_FLAVOR(LET)
            | NODE_FLAG_MANAGED
            | SERIES_FLAG_LINK_NODE_NEEDS_MARK  // link to next virtual bind
            | SERIES_FLAG_INFO_NODE_NEEDS_MARK  // inode of symbol
    );

    Finalize_None(VAL(Array_Single(let)));  // start variable as unset

    if (specifier) {
        assert(IS_LET(specifier) or IS_USE(specifier) or IS_VARLIST(specifier));
        assert(Is_Node_Managed(specifier));
    }
    mutable_LINK(NextLet, let) = specifier;  // linked list, see [1]

    node_MISC(LetReserved, let) = nullptr;  // not currently used

    mutable_INODE(LetSymbol, let) = symbol;  // surrogate for context "key"

    return let;
}


//
//  let: native [
//
//  {Dynamically add a new binding into the stream of evaluation}
//
//      return: "Expression result if SET form, else gives the new vars"
//          [<opt> <void> any-value!]
//      'vars "Variable(s) to create, GROUP!s must evaluate to BLOCK! or WORD!"
//          [word! block! set-word! set-block! group! set-group!]
//      :expression "Optional Expression to assign"
//          [<variadic> <end> <opt> any-value!]
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
//        block: just [x y]:
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

    REBVAL *vars = ARG(vars);

    UNUSED(ARG(expression));
    Level(*) L = level_;  // fake variadic, see [1]
    REBSPC* L_specifier = Level_Specifier(L);

    REBVAL *bindings_holder = ARG(return);

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
        IS_GROUP(vars) or IS_SET_GROUP(vars)
    ){
        if (Do_Any_Array_At_Throws(SPARE, vars, SPECIFIED))
            return THROWN;

        if (Is_Quoted(SPARE))  // should (let 'x: <whatever>) be legal? see [3]
            fail ("QUOTED! escapes not supported at top level of LET");

        switch (CELL_HEART(SPARE)) {  // QUASI! states mean isotopes ok
          case REB_WORD:
          case REB_BLOCK:
            if (IS_SET_GROUP(vars))
                Setify(stable_SPARE);  // convert `(word):` to be SET-WORD!
            break;

          case REB_SET_WORD:
          case REB_SET_BLOCK:
            if (IS_SET_GROUP(vars)) {
                // Allow `(set-word):` to ignore "redundant colon", see [2]
            }
            break;

          default:
            fail ("LET GROUP! limited to WORD! and BLOCK!");  // see [4]
        }

        vars = stable_SPARE;
    }

    //=//// GENERATE NEW BLOCK IF QUOTED! OR GROUP! ELEMENTS ///////////////=//

    // Writes rebound copy of `vars` to SPARE if it's a SET-WORD!/SET-BLOCK!
    // so it can be used in a reevaluation.  For WORD!/BLOCK! forms of LET it
    // just writes the rebound copy into the OUT cell.

    REBSPC *bindings = L_specifier;  // specifier chain we may be adding to

    if (bindings and Not_Node_Managed(bindings))
        Set_Node_Managed_Bit(bindings);  // natives don't always manage

    if (CELL_HEART(vars) == REB_WORD or CELL_HEART(vars) == REB_SET_WORD) {
        const Symbol* symbol = VAL_WORD_SYMBOL(vars);
        bindings = Make_Let_Patch(symbol, bindings);

        REBVAL *where;
        if (CELL_HEART(vars) == REB_SET_WORD) {
            STATE = ST_LET_EVAL_STEP;
            where = stable_SPARE;
        }
        else
            where = stable_OUT;

        Copy_Cell_Header(where, vars);  // keep QUASI! state and word/setword
        INIT_VAL_WORD_SYMBOL(where, symbol);
        INIT_VAL_WORD_BINDING(where, bindings);
        INIT_VAL_WORD_INDEX(where, INDEX_ATTACHED);

        Trash_Pointer_If_Debug(vars);  // if in spare, we may have overwritten
    }
    else {
        assert(IS_BLOCK(vars) or IS_SET_BLOCK(vars));

        const Cell* tail;
        const Cell* item = VAL_ARRAY_AT(&tail, vars);
        REBSPC *item_specifier = VAL_SPECIFIER(vars);

        StackIndex base = TOP_INDEX;

        bool altered = false;

        for (; item != tail; ++item) {
            const Cell* temp = item;
            REBSPC *temp_specifier = item_specifier;

            if (Is_Quoted(temp)) {
                Derelativize(PUSH(), temp, temp_specifier);
                Unquotify(TOP, 1);  // drop quote in output block, see [5]
                altered = true;
                continue;  // do not make binding
            }

            if (IS_GROUP(temp)) {  // evaluate non-QUOTED! groups in LET block
                if (Do_Any_Array_At_Throws(OUT, temp, item_specifier))
                    return THROWN;

                temp = OUT;
                temp_specifier = SPECIFIED;

                altered = true;
            }

            switch (CELL_HEART(temp)) {  // permit QUASI!
              case REB_ISSUE:  // is multi-return opt-in for dialect, passthru
              case REB_BLANK:  // is multi-return opt-out for dialect, passthru
                Derelativize(PUSH(), temp, temp_specifier);
                break;

              case REB_WORD:
              case REB_SET_WORD:
              case REB_META_WORD:
              case REB_THE_WORD: {
                Derelativize(PUSH(), temp, temp_specifier);
                const Symbol* symbol = VAL_WORD_SYMBOL(temp);
                bindings = Make_Let_Patch(symbol, bindings);
                break; }

              default:
                fail (rebUnrelativize(temp));  // default to passthru, see [6]
            }
        }

        REBVAL *where;
        if (IS_SET_BLOCK(vars)) {
            STATE = ST_LET_EVAL_STEP;
            where = stable_SPARE;
        }
        else
            where = stable_OUT;

        if (altered) {  // elements altered, can't reuse input block rebound
            Init_Array_Cell(
                where,  // may be SPARE, and vars may point to it
                VAL_TYPE(vars),
                Pop_Stack_Values_Core(base, NODE_FLAG_MANAGED)
            );
        }
        else {
            Drop_Data_Stack_To(base);

            if (vars != where)
                Copy_Cell(where, vars);  // Move_Cell() of ARG() not allowed
        }
        INIT_BINDING_MAY_MANAGE(where, bindings);

        Trash_Pointer_If_Debug(vars);  // if in spare, we may have overwritten
    }

    //=//// ONE EVAL STEP WITH OLD BINDINGS IF SET-WORD! or SET-BLOCK! /////=//

    // We want the left hand side to use the *new* LET bindings, but the right
    // hand side should use the *old* bindings.  For instance:
    //
    //     let assert: specialize :assert [handler: [print "should work!"]]
    //
    // Leverage same mechanism as REEVAL to preload the next execution step
    // with the rebound SET-WORD! or SET-BLOCK!

    mutable_BINDING(bindings_holder) = bindings;
    Trash_Pointer_If_Debug(bindings);  // catch uses after this point in scope

    if (STATE != ST_LET_EVAL_STEP) {
        assert(IS_WORD(OUT) or IS_BLOCK(OUT));  // should have written output
        goto update_feed_binding;
    }

    assert(CELL_HEART(SPARE) == REB_SET_WORD or IS_SET_BLOCK(SPARE));

    Flags flags =
        FLAG_STATE_BYTE(ST_EVALUATOR_REEVALUATING)
        | (L->flags.bits & EVAL_EXECUTOR_FLAG_FULFILLING_ARG)
        | (L->flags.bits & LEVEL_FLAG_RAISED_RESULT_OK);

    Level(*) sub = Make_Level(LEVEL->feed, flags);
    sub->u.eval.current = SPARE;
    sub->u.eval.current_gotten = nullptr;
    sub->u.eval.enfix_reevaluate = 'N';  // detect?

    Push_Level(OUT, sub);

    assert(STATE == ST_LET_EVAL_STEP);  // checked above
    return CONTINUE_SUBLEVEL(sub);

} integrate_eval_bindings: {  ////////////////////////////////////////////////

    REBSPC *bindings = VAL_SPECIFIER(bindings_holder);

    if (L_specifier and IS_LET(L_specifier)) { // add bindings, see [7]
        bindings = Merge_Patches_May_Reuse(L_specifier, bindings);
        mutable_BINDING(bindings_holder) = bindings;
    }

    L->feed->gotten = nullptr;  // invalidate next word's cache, see [8]
    goto update_feed_binding;

} update_feed_binding: {  /////////////////////////////////////////////////////

    // Going forward we want the feed's binding to include the LETs.  Note
    // that this can create the problem of applying the binding twice; this
    // needs systemic review.

    REBSPC *bindings = VAL_SPECIFIER(bindings_holder);
    mutable_BINDING(FEED_SINGLE(L->feed)) = bindings;

    if (Is_Pack(OUT))
        Decay_If_Unstable(OUT);

    return OUT;
}}


//
//  add-let-binding: native [
//
//  {Experimental function for adding a new variable binding to a frame}
//
//      return: [any-word!]
//      frame [frame!]
//      word [any-word!]
//      value [<opt> any-value!]
//  ]
//
DECLARE_NATIVE(add_let_binding) {
    INCLUDE_PARAMS_OF_ADD_LET_BINDING;

    Level(*) L = CTX_LEVEL_MAY_FAIL(VAL_CONTEXT(ARG(frame)));

    REBSPC* L_specifier = Level_Specifier(L);
    if (L_specifier)
        Set_Node_Managed_Bit(L_specifier);

    REBSPC *let = Make_Let_Patch(VAL_WORD_SYMBOL(ARG(word)), L_specifier);

    Move_Cell(Array_Single(let), ARG(value));

    mutable_BINDING(FEED_SINGLE(L->feed)) = let;

    Move_Cell(OUT, ARG(word));
    INIT_VAL_WORD_BINDING(OUT, let);
    INIT_VAL_WORD_INDEX(OUT, 1);

    return OUT;
}


//
//  add-use-object: native [
//
//  {Experimental function for adding an object's worth of binding to a frame}
//
//      return: <none>
//      frame [frame!]
//      object [object!]
//  ]
//
DECLARE_NATIVE(add_use_object) {
    INCLUDE_PARAMS_OF_ADD_USE_OBJECT;

    Level(*) L = CTX_LEVEL_MAY_FAIL(VAL_CONTEXT(ARG(frame)));
    REBSPC* L_specifier = Level_Specifier(L);

    Context* ctx = VAL_CONTEXT(ARG(object));

    if (L_specifier)
        Set_Node_Managed_Bit(L_specifier);

    REBSPC *use = Make_Or_Reuse_Use(ctx, L_specifier, REB_WORD);

    mutable_BINDING(FEED_SINGLE(L->feed)) = use;

    return NONE;
}


//
//  Clonify_And_Bind_Relative: C
//
// Recursive function for relative function word binding.  The code for
// Clonify() is merged in for efficiency, because it recurses...and we want
// to do the binding in the same pass.
//
// !!! Since the ultimate desire is to factor out common code, try not to
// constant-fold the Clonify implementation here--to make the factoring clear.
//
// !!! Should this return true if any relative bindings were made?
//
static void Clonify_And_Bind_Relative(
    REBVAL *v,  // Note: incoming value is not relative
    Flags flags,
    REBU64 deep_types,
    struct Reb_Binder *binder,
    Action* relative
){
    if (C_STACK_OVERFLOWING(&relative))
        Fail_Stack_Overflow();

    assert(flags & NODE_FLAG_MANAGED);

    // !!! Could theoretically do what COPY does and generate a new hijackable
    // identity.  There's no obvious use for this; hence not implemented.
    //
    assert(not (deep_types & FLAGIT_KIND(REB_FRAME)));

    // !!! This used to use KIND3Q_BYTE_UNCHECKED to get a "kind", but it
    // applied it on a dequoted form.  This was effectively the heart.  That
    // means if `deep_types` is passed in with something like REB_PATH it
    // will get paths at arbitrary levels of quoting too.  Review.
    //
    enum Reb_Kind heart = CELL_HEART_UNCHECKED(v);

    if (deep_types & FLAGIT_KIND(heart) & TS_SERIES_OBJ) {
        //
        // Objects and series get shallow copied at minimum
        //
        Series* series;
        bool would_need_deep;

        if (ANY_CONTEXT_KIND(heart)) {
            INIT_VAL_CONTEXT_VARLIST(
                v,
                CTX_VARLIST(Copy_Context_Shallow_Managed(VAL_CONTEXT(v)))
            );
            series = CTX_VARLIST(VAL_CONTEXT(v));

            would_need_deep = true;
        }
        else if (ANY_ARRAYLIKE(v)) {
            series = Copy_Array_At_Extra_Shallow(
                VAL_ARRAY(v),
                0, // !!! what if VAL_INDEX() is nonzero?
                VAL_SPECIFIER(v),
                0,
                NODE_FLAG_MANAGED
            );

            INIT_VAL_NODE1(v, series);  // copies args
            INIT_SPECIFIER(v, UNBOUND);  // copied w/specifier--not relative

            // See notes in Clonify()...need to copy immutable paths so that
            // binding pointers can be changed in the "immutable" copy.
            //
            if (ANY_SEQUENCE_KIND(heart))
                Freeze_Array_Shallow(ARR(series));

            would_need_deep = true;
        }
        else if (ANY_SERIES_KIND(heart)) {
            series = Copy_Series_Core(
                VAL_SERIES(v),
                NODE_FLAG_MANAGED
            );
            INIT_VAL_NODE1(v, series);

            would_need_deep = false;
        }
        else {
            would_need_deep = false;
            series = nullptr;
        }

        // If we're going to copy deeply, we go back over the shallow
        // copied series and "clonify" the values in it.
        //
        if (would_need_deep and (deep_types & FLAGIT_KIND(heart))) {
            Cell* sub = Array_Head(ARR(series));
            Cell* sub_tail = Array_Tail(ARR(series));
            for (; sub != sub_tail; ++sub)
                Clonify_And_Bind_Relative(
                    SPECIFIC(sub),
                    flags,
                    deep_types,
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

    if (ANY_WORDLIKE(v)) {
        REBINT n = Get_Binder_Index_Else_0(binder, VAL_WORD_SYMBOL(v));
        if (n != 0) {
            //
            // Word' symbol is in frame.  Relatively bind it.  Note that the
            // action bound to can be "incomplete" (LETs still gathering)
            //
            INIT_VAL_WORD_BINDING(v, relative);
            INIT_VAL_WORD_INDEX(v, n);
        }
    }
    else if (ANY_ARRAYLIKE(v)) {

        // !!! Technically speaking it is not necessary for an array to
        // be marked relative if it doesn't contain any relative words
        // under it.  However, for uniformity in the near term, it's
        // easiest to debug if there is a clear mark on arrays that are
        // part of a deep copy of a function body either way.
        //
        INIT_SPECIFIER(v, relative);  // "incomplete func" (LETs gathering?)
    }
}


//
//  Copy_And_Bind_Relative_Deep_Managed: C
//
// This routine is called by Make_Action in order to take the raw material
// given as a function body, and de-relativize any IS_RELATIVE(value)s that
// happen to be in it already (as any Copy does).  But it also needs to make
// new relative references to ANY-WORD! that are referencing function
// parameters, as well as to relativize the copies of ANY-ARRAY! that contain
// these relative words...so that they refer to the archetypal function
// to which they should be relative.
//
Array* Copy_And_Bind_Relative_Deep_Managed(
    const REBVAL *body,
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
    const Array* original = VAL_ARRAY(body);
    REBLEN index = VAL_INDEX(body);
    REBSPC *specifier = VAL_SPECIFIER(body);
    REBLEN tail = VAL_LEN_AT(body);
    assert(tail <= Array_Len(original));

    if (index > tail)  // !!! should this be asserted?
        index = tail;

    Flags flags = ARRAY_MASK_HAS_FILE_LINE | NODE_FLAG_MANAGED;
    REBU64 deep_types = (TS_SERIES | TS_SEQUENCE) & ~TS_NOT_COPIED;

    REBLEN len = tail - index;

    // Currently we start by making a shallow copy and then adjust it

    copy = Make_Array_For_Copy(len, flags, original);
    Set_Series_Len(copy, len);

    const Cell* src = Array_At(original, index);
    Cell* dest = Array_Head(copy);
    REBLEN count = 0;
    for (; count < len; ++count, ++dest, ++src) {
        Clonify_And_Bind_Relative(
            Derelativize(dest, src, specifier),
            flags | NODE_FLAG_MANAGED,
            deep_types,
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
    Cell* head,
    const Cell* tail,
    Context* from,
    Context* to,
    Option(struct Reb_Binder*) binder
) {
    Cell* v = head;
    for (; v != tail; ++v) {
        if (Is_Activation(v)) {
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
        else if (Is_Isotope(v))
            NOOP;
        else if (ANY_ARRAYLIKE(v)) {
            const Cell* sub_tail;
            Cell* sub_at = VAL_ARRAY_AT_MUTABLE_HACK(&sub_tail, v);
            Rebind_Values_Deep(sub_at, sub_tail, from, to, binder);
        }
        else if (ANY_WORDLIKE(v) and BINDING(v) == from) {
            INIT_VAL_WORD_BINDING(v, to);

            if (binder) {
                REBLEN index = Get_Binder_Index_Else_0(
                    unwrap(binder),
                    VAL_WORD_SYMBOL(v)
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
//     x-word: none
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
    REBVAL *body_in_out, // input *and* output parameter
    REBVAL *spec
){
    // !!! This just hacks in GROUP! behavior, because the :param convention
    // does not support groups and gives GROUP! by value.  In the stackless
    // build the preprocessing would most easily be done in usermode.
    //
    if (IS_GROUP(spec)) {
        DECLARE_LOCAL (temp);
        if (Do_Any_Array_At_Throws(temp, spec, SPECIFIED))
            fail (Error_No_Catch_For_Throw(TOP_LEVEL));
        Move_Cell(spec, temp);
    }

    REBLEN num_vars = IS_BLOCK(spec) ? VAL_LEN_AT(spec) : 1;
    if (num_vars == 0)
        fail (spec);  // !!! should fail() take unstable?

    const Cell* tail;
    const Cell* item;

    REBSPC *specifier;
    bool rebinding;
    if (IS_BLOCK(spec)) {  // walk the block for errors BEFORE making binder
        specifier = VAL_SPECIFIER(spec);
        item = VAL_ARRAY_AT(&tail, spec);

        const Cell* check = item;

        rebinding = false;
        for (; check != tail; ++check) {
            if (IS_BLANK(check)) {
                // Will be transformed into dummy item, no rebinding needed
            }
            else if (IS_WORD(check) or IS_META_WORD(check))
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
        item = spec;
        tail = spec;
        specifier = SPECIFIED;
        rebinding = IS_WORD(item) or IS_META_WORD(item);
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

        if (IS_BLANK(item)) {
            if (dummy_sym == SYM_DUMMY9)
                fail ("Current limitation: only up to 9 BLANK! keys");

            symbol = Canon_Symbol(dummy_sym);
            dummy_sym = cast(SymId, cast(int, dummy_sym) + 1);

            REBVAR *var = Append_Context(c, symbol);
            Init_Blank(var);
            Set_Cell_Flag(var, BIND_NOTE_REUSE);
            Set_Cell_Flag(var, PROTECTED);

            goto add_binding_for_check;
        }
        else if (IS_WORD(item) or IS_META_WORD(item)) {
            symbol = VAL_WORD_SYMBOL(item);
            REBVAR *var = Append_Context(c, symbol);

            // !!! For loops, nothing should be able to be aware of this
            // synthesized variable until the loop code has initialized it
            // with something.  But this code is shared with USE, so the user
            // can get their hands on the variable.  Can't be trash.
            //
            Finalize_None(var);

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
            symbol = VAL_WORD_SYMBOL(VAL_UNESCAPED(item));

          blockscope {
            REBVAL *var = Append_Context(c, symbol);
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

    // !!! In virtual binding, there would not be a Bind_Values call below;
    // so it wouldn't necessarily be required to manage the augmented
    // information.  For now it's a requirement for any references that
    // might be found...and INIT_BINDING_MAY_MANAGE() won't auto-manage
    // things unless they are stack-based.  Virtual bindings will be, but
    // contexts like this won't.
    //
    Manage_Series(CTX_VARLIST(c));

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
    const REBKEY *key_tail;
    const REBKEY *key = CTX_KEYS(&key_tail, c);
    REBVAL *var = CTX_VARS_HEAD(c); // only needed for debug, optimized out
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
        DECLARE_LOCAL (word);
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
    REBVAL *any_array,
    Context* context,
    struct Reb_Binder *binder,
    enum Reb_Kind kind
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
        VAL_ARRAY_AT_MUTABLE_HACK(ARG(def)),  // mutates bindings
        exemplar,
        FLAGIT_KIND(REB_SET_WORD),  // types to bind (just set-word!),
        0,  // types to "add midstream" to binding as we go (nothing)
        BIND_DEEP
    );
 */

    Virtual_Bind_Patchify(any_array, context, kind);
}


void Bind_Nonspecifically(Cell* head, const Cell* tail, Context* context)
{
    Cell* v = head;
    for (; v != tail; ++v) {
        if (ANY_ARRAYLIKE(v)) {
            const Cell* sub_tail;
            Cell* sub_head = VAL_ARRAY_AT_MUTABLE_HACK(&sub_tail, v);
            Bind_Nonspecifically(sub_head, sub_tail, context);
        }
        else if (ANY_WORDLIKE(v)) {
            //
            // Give context but no index; this is how we attach to modules.
            //
            mutable_BINDING(v) = context;
            INIT_VAL_WORD_INDEX(v, INDEX_ATTACHED);  // may be quoted
        }
    }
}


//
//  intern*: native [
//      {Overwrite all bindings of a block deeply}
//
//      return: [block!]
//      where [module!]
//      data [block!]
//  ]
//
DECLARE_NATIVE(intern_p)
{
    INCLUDE_PARAMS_OF_INTERN_P;

    assert(IS_BLOCK(ARG(data)));

    const Cell* tail;
    Cell* head = VAL_ARRAY_AT_MUTABLE_HACK(&tail, ARG(data));
    Bind_Nonspecifically(head, tail, VAL_CONTEXT(ARG(where)));

    return COPY(ARG(data));
}
