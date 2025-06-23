//
//  file: %c-bind.c
//  summary: "Word Binding Routines"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
    Binder* binder,
    Element* head,
    const Element* tail,
    Context* context,
    Option(SymId) add_midstream_types,
    Flags flags
){
    Element* v = head;
    for (; v != tail; ++v) {
        if (Wordlike_Cell(v)) {
          const Symbol* symbol = Cell_Word_Symbol(v);

          if (Is_Stub_Sea(context)) {
            SeaOfVars* sea = cast(SeaOfVars*, context);
            bool strict = true;
            Patch* patch = maybe Sea_Patch(sea, symbol, strict);
            if (patch) {
                Tweak_Cell_Binding(v, sea);
                Tweak_Cell_Word_Stub(v, patch);
            }
            else if (
                add_midstream_types == SYM_ANY
                or (
                    add_midstream_types == SYM_SET
                    and Is_Set_Word(v)
                )
            ){
                Init_Tripwire(Append_Context_Bind_Word(context, v));
            }
          }
          else {
            assert(Is_Stub_Varlist(context));
            VarList* varlist = cast(VarList*, context);

            REBINT n = maybe Try_Get_Binder_Index(binder, symbol);
            if (n > 0) {
                //
                // A binder index of 0 should clearly not be bound.  But
                // negative binder indices are also ignored by this process,
                // which provides a feature of building up state about some
                // words while still not including them in the bind.
                //
                assert(n <= Varlist_Len(varlist));

                // We're overwriting any previous binding, which may have
                // been relative.

                Tweak_Cell_Binding(v, varlist);
                Tweak_Cell_Word_Index(v, n);
            }
            else if (
                add_midstream_types == SYM_ANY
                or (
                    add_midstream_types == SYM_SET
                    and Is_Set_Word(v)
                )
            ){
                //
                // Word is not in varlist, so add it if option is specified
                //
                Append_Context_Bind_Word(varlist, v);
                Add_Binder_Index(binder, symbol, VAL_WORD_INDEX(v));
            }
          }
        }
        else if (flags & BIND_DEEP) {
            if (Listlike_Cell(v)) {
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
){
    DECLARE_BINDER (binder);
    Construct_Binder(binder);

    VarList* c = Cell_Varlist(context);

    // Associate the canon of a word with an index number.  (This association
    // is done by poking the index into the Stub of the Symbol behind the
    // ANY-WORD?, so it must be cleaned up to not break future bindings.)
    //
  if (not Is_Module(context)) {
    REBLEN index = 1;
    const Key* key_tail;
    const Key* key = Varlist_Keys(&key_tail, c);
    const Slot* slot = Varlist_Slots_Head(c);
    for (; key != key_tail; key++, slot++, index++)
        Add_Binder_Index(binder, Key_Symbol(key), index);
  }

    Bind_Values_Inner_Loop(
        binder,
        head,
        tail,
        c,
        add_midstream_types,
        flags
    );

    Destruct_Binder(binder);
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
            Wordlike_Cell(v)
            and (not context or Cell_Binding(v) == unwrap context)
        ){
            Unbind_Any_Word(v);
        }
        else if (Listlike_Cell(v) and deep) {
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
bool Try_Bind_Word(const Element* context, Element* word)
{
    const bool strict = true;
    if (Is_Module(context)) {
        Option(Patch*) patch = Sea_Patch(
            Cell_Module_Sea(context),
            Cell_Word_Symbol(word),
            strict
        );
        if (not patch)
            return false;
        Tweak_Cell_Binding(word, Cell_Module_Sea(context));
        Tweak_Cell_Word_Stub(word, unwrap patch);
        return true;
    }

    Option(Index) index = Find_Symbol_In_Context(
        context,
        Cell_Word_Symbol(word),
        strict
    );
    if (not index)
        return false;

    Tweak_Cell_Binding(word, Cell_Varlist(context));
    Tweak_Cell_Word_Index(word, unwrap index);
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
    Option(Context*) inherit
){
    Stub* let = Make_Untracked_Stub(STUB_MASK_LET);  // one variable

    Init(Slot) slot = Slot_Init_Hack(u_cast(Slot*, Stub_Cell(let)));
    Init_Dual_Unset(slot);

    Tweak_Link_Inherit_Bind(let, inherit);  // linked list [1]
    Corrupt_Unused_Field(let->misc.corrupt);  // not currently used
    INFO_LET_SYMBOL(let) = m_cast(Symbol*, symbol);

    return let;
}


//
//  Try_Get_Binding_Of: C
//
// Find the context a word is bound into.  This has to account for things
// including what "Lens" should be used for the phase of a function.
//
bool Try_Get_Binding_Of(Sink(Element) out, const Element* wordlike)
{
    Context* binding = Cell_Binding(wordlike);
    const Symbol* symbol = Cell_Word_Symbol(wordlike);

    Context* c = binding;
    Context* next;

    goto loop_body;  // stylize loop to avoid annoying indentation level

  next_context: //////////////////////////////////////////////////////////////

  // We want to continue the next_context loop from inside sub-loops, which
  // means we need a `goto` and not a `continue`.  But putting the goto at
  // the end of the loop would jump over variable initializations.  Stylizing
  // this way makes it work without a warning.

    c = next;

  loop_body: /////////////////////////////////////////////////////////////////

    if (c == nullptr)
        return false;

    Flavor flavor = Stub_Flavor(c);
    Option(Phase*) lens = nullptr;

  //=//// ALTER `c` IF USE STUB ///////////////////////////////////////////=//

  // Sometimes the Link_Inherit_Bind() is already taken by another binding
  // chain, and so a Use Stub has to be fabricated to hold an alternative
  // binding to use in another chain.  But the effect should be the same, so
  // we transform the context to the one that the Use Stub points to.
  //
  // 1. The "final phase" of a function application is allowed to use the
  //    VarList of the Level directly as a context, and put the next context
  //    into Link_Inherit_Bind().  There's no Lens in that case, so we use the
  //    Details of the function as the Lens--providing full visibility to all
  //    non-sealed locals and arguments.  A null lens cues this behavior
  //    below, so we can't leave it null when we have a USE of a Lens-less
  //    FRAME!.  Lensing with Details would incorrectly give full visibility,
  //    so Lens with the ParamList.

    next = maybe Link_Inherit_Bind(c);  // save so we can update `c`

    if (flavor == FLAVOR_USE) {
        if (  // some USEs only affect SET-WORD!s
            Get_Flavor_Flag(USE, c, SET_WORDS_ONLY)
            and not Is_Set_Word(wordlike)
        ){
            goto next_context;
        }

        Element* overbind = Known_Element(Stub_Cell(c));

        if (Is_Word(overbind)) {  // OVERBIND use of single WORD!
            if (Cell_Word_Symbol(overbind) != symbol)
                goto next_context;

            c = Cell_Binding(overbind);  // use its context, I guess?
            Corrupt_If_Debug(next);  // don't need it
            goto loop_body;  // skips assignment via next
        }

        if (Is_Frame(overbind)) {
            lens = Cell_Frame_Lens(overbind);
            if (not lens)  // need lens to not default to full visibility [1]
                lens = Phase_Paramlist(Cell_Frame_Phase(overbind));
        }

        c = Cell_Context(overbind);  // do search on other contexts
        flavor = Stub_Flavor(c);
    }

  //=//// MODULE LOOKUP ///////////////////////////////////////////////////=//

  // Module lookup is very common so we do it first.  It's relatively fast in
  // most cases, see the definition of SeaOfVars for an explanation of the
  // linked list of "Patch" pointed to by each Symbol, holding variables for
  // any module that has that symbol in it.

    if (flavor == FLAVOR_SEA) {
        SeaOfVars* sea = cast(SeaOfVars*, c);
        bool strict = true;
        Patch* patch = maybe Sea_Patch(sea, symbol, strict);
        if (patch) {
            Init_Module(out, sea);
            Tweak_Cell_Word_Stub(wordlike, patch);
            return true;
        }
        goto next_context;
    }

  //=//// LET STUBS ///////////////////////////////////////////////////////=//

  // A Let Stub is currently very simple, it just holds a single variable.
  // There may be a way to unify this with VarList in such a way that the use
  // of a single Symbol* key in the keylist position could cue it to know that
  // it's a single element context, which could unify the way that Let and
  // VarList work, though it would mean sacrificing the [0] slot which is
  // needed by ParamList to hold the inherited phase.

    if (flavor == FLAVOR_LET) {
        if (Let_Symbol(c) == symbol) {
            Init_Let(out, c);
            Tweak_Cell_Word_Stub(wordlike, c);
            return true;
        }
        goto next_context;
    }

    assert(flavor == FLAVOR_VARLIST);

  //=//// VARLIST LOOKUP //////////////////////////////////////////////////=//

  // VarLists are currently very basic, and require us to do a linear search
  // on the KeyList to see if a Symbol is present.  There aren't any fancy
  // hashings to accelerate the search by accelerating with some method that
  // might have some false positives about whether the key is there.  (Symbols
  // are immutable, and hence there could be some fingerprinting done that is
  // tested against information stored in KeyLists.)  It's technically not as
  // big a problem as it used to be, because modules are based on SeaOfVars
  // and not VarLists...so VarLists have many fewer keys than they used to.
  //
  // But there are a couple of things that make searching in VarList more
  // complicated.  One is that the VarList may be a frame, and the frame can
  // even have duplicate keys--where only some of keys are applicable when
  // viewing the frame through a certain "Lens".

    VarList* vlist = cast(VarList*, c);

    if (CTX_TYPE(vlist) == TYPE_FRAME) {
        if (not lens) {  // want full visibility (Use would have defaulted...)
            lens = Phase_Details(cast(ParamList*, vlist));
        }
        Init_Lensed_Frame(
            out, cast(ParamList*, vlist), lens, NONMETHOD
        );
    }
    else {
        Copy_Cell(out, Varlist_Archetype(vlist));
    }

    Option(Index) index = Find_Symbol_In_Context(  // must search
        out,
        symbol,
        true
    );

    if (index) {
        Tweak_Cell_Word_Index(wordlike, unwrap index);
        return true;
    }

    goto next_context;
}


// We remove the decoration from the VARS argument, but remember whether we
// were setting or not.  e.g.
//
//     let x: ...
//     let [x y]: ...
//     let (expr): ...
//
// As opposed to (let x) or (let [x y]) or (let (expr)), which just LET.
//
#define LEVEL_FLAG_LET_IS_SETTING  LEVEL_FLAG_MISCELLANEOUS


//
//  let: native [
//
//  "Dynamically add a new binding into the stream of evaluation"
//
//      return: "Expression result if SET form, else gives the new vars"
//          [any-value?]  ; should vanish if (let x), give var if (let $x)
//      'vars "Variable(s) to create"  ; can't soft quote due to DEFAULT
//          [word! ^word! set-word? ^set-word? set-run-word? group!
//          block! set-block? set-group?]
//      @expression "Optional Expression to assign"
//          [<variadic> element?]  ; fake variadic [1]
//      <local> bindings-holder  ; workaround [2]
//  ]
//
DECLARE_NATIVE(LET)
//
// 1. Though LET shows as a variadic function on its interface, it does not
//    need to use the variadic argument...since it is a native (and hence
//    can access the frame and feed directly).
//
// 2. Because we stacklessly evaluate the right hand side, we can't persist
//    a `Context*` stack variable of the LET bindings chain we built across
//    that evaluation.  We need to put the `Context*` somewhere that it will
//    be GC safe during the evaluation, and retrievable after it.
//
//    But there's not currently any kind of CONTEXT! abstraction exposed that
//    captures the binding environment that a Context* does.  That needs to
//    change... and there need to be individual parts like LET! as well.
//    Until such an abstraction exists, we have things like BLOCK! which can
//    hold a Context* in its Cell->extra slot as a binding, so that serves as
//    a proxy for the functionality.
{
    INCLUDE_PARAMS_OF_LET;

    Element* vars = Element_ARG(VARS);

    UNUSED(ARG(EXPRESSION));
    Level* L = level_;  // fake variadic [2]
    Context* L_binding = Level_Binding(L);

    enum {
        ST_LET_INITIAL_ENTRY = STATE_0,
        ST_LET_EVAL_STEP  // only used when LEVEL_FLAG_LET_IS_SETTING
    };

    switch (STATE) {
      case ST_LET_INITIAL_ENTRY:
        Init_Block(LOCAL(BINDINGS_HOLDER), g_empty_array);
        goto initial_entry;

      case ST_LET_EVAL_STEP:
        goto integrate_eval_bindings;

      default:
        assert (false);
    }

  initial_entry: {  ///////////////////////////////////////////////////////////

    if (Is_Group(vars))
        goto escape_groups;

    if (Is_Set_Group(vars)) {
        Set_Level_Flag(L, LET_IS_SETTING);
        Unchain(vars);  // turn into a normal GROUP!
        goto escape_groups;
    }

    goto generate_new_block_if_quoted_or_group;

} escape_groups: { //=//// let (...): ... /////////////////////////////////=//

    // Though the evaluator offers parameter conventions that escape groups
    // for you before your function gets called, we can't use that convention
    // here due to a contention with DEFAULT:
    //
    //     let (...): default [...]
    //
    // We want the LET to win.  So this means we have to make left win in a
    // prioritization battle with right, if they're both soft literal.  At
    // the moment, left will defer if right is literal at all.  It needs some
    // conscious tweaking when there is time for it...but the code was
    // written to do the eval here in LET with a hard literal...works for now.
    //
    // 1. Question: Should it be allowed to write (let 'x: ...) and have it
    //    act as if you had written (x: ...), e.g. no LET behavior at all?
    //    This may seem useless, but it could be useful in generated code to
    //    "escape out of" a LET in some boilerplate.  And it would be
    //    consistent with the behavior of (let ['x]: ...)
    //
    // 2. For convenience, the group can evaluate to a SET-BLOCK,  e.g.
    //
    //        block: $[x y]:
    //        (block): <whatever>  ; no real reason to prohibit this
    //
    //    There are conflicting demands where we want `(thing):` equivalent
    //    to `[(thing)]:`, while we don't want "mixed decorations" where
    //    `('^thing):` would become both SET! and METAFORM!.
    //
    // 3. Right now what is permitted is conservative.  Bias it so that if you
    //    want something to just "pass through the LET" that you use a quote
    //    mark on it, and the LET will ignore it.

    assert(Is_Group(vars));

    if (Eval_Any_List_At_Throws(SPARE, vars, SPECIFIED))
        return THROWN;

    Decay_If_Unstable(SPARE);
    if (Is_Antiform(SPARE))
        return PANIC(Error_Bad_Antiform(SPARE));

    Element* spare = Known_Element(SPARE);

    if (Is_Quoted(spare))  // should (let 'x: <whatever>) be legal? [1]
        return PANIC("QUOTED? escapes not supported at top level of LET");

    if (Try_Get_Settable_Word_Symbol(nullptr, spare) or Is_Set_Block(spare)) {
        if (Get_Level_Flag(L, LET_IS_SETTING)) {
            // Allow `(set-word):` to ignore redundant colon [2]
            Clear_Level_Flag(L, LET_IS_SETTING);  // let block/word signal it
        }
        else {
            return PANIC(
                "[let (expr)] can't have expr be SET-XXX!, use [let (expr):]"
            );
        }
    }
    else if (Is_Word(spare) or Is_Block(spare)) {
        if (Get_Level_Flag(L, LET_IS_SETTING)) {
            Setify(spare);  // graft the colon off of (...): onto word/block
            Clear_Level_Flag(L, LET_IS_SETTING);  // let block/word signal it
        }
    }
    else
        return PANIC("LET GROUP! limited to WORD! and BLOCK!");  // [3]

    vars = spare;

} generate_new_block_if_quoted_or_group: {

    // Writes rebound copy of `vars` to SPARE if it's a SET-WORD! or SET-BLOCK!
    // so it can be used in a reevaluation.  For WORD!/BLOCK! forms of LET it
    // just writes the rebound copy into the OUT cell.
    //
    // 1. It would be nice if we could just copy the input variable and
    //    rewrite the binding, but at time of writing /foo: is not binding
    //    compatible with WORD!...it has a pairing allocation for the chain
    //    holding the word and a space, and no binding index of its own.
    //    This will all be reviewed at some point, but for now we do a
    //    convoluted rebuilding of the matching structure from a word basis.

    Context* bindings = L_binding;  // context chain we may be adding to

    if (bindings and Not_Base_Managed(bindings))
        Set_Base_Managed_Bit(bindings);  // natives don't always manage

    assert(Not_Level_Flag(L, LET_IS_SETTING));

    if (Is_Block(vars))
        goto handle_block_or_set_block;

    if (Is_Set_Block(vars)) {
        Set_Level_Flag(L, LET_IS_SETTING);
        goto handle_block_or_set_block;
    }

  detect_word_or_set_word: {

    const Symbol* symbol;

    if (Is_Word(vars) or Is_Meta_Form_Of(WORD, vars)) {
        symbol = Cell_Word_Symbol(vars);
        goto handle_word_or_set_word;
    }

    symbol = maybe Try_Get_Settable_Word_Symbol(nullptr, cast(Element*, vars));
    if (symbol) {
        Set_Level_Flag(L, LET_IS_SETTING);
        goto handle_word_or_set_word;
    }

    return PANIC("Malformed LET.");

  handle_word_or_set_word: {

    bindings = Make_Let_Variable(symbol, bindings);

    Sink(Element) where = Get_Level_Flag(L, LET_IS_SETTING)
        ? SPARE
        : OUT;

    Init_Word_Bound(where, symbol, bindings);
    if (Heart_Of(vars) != TYPE_WORD) {  // more complex than we'd like [1]
        Setify(where);
        if (Heart_Of(vars) == TYPE_PATH) {
            Option(Error*) error = Trap_Blank_Head_Or_Tail_Sequencify(
                where, TYPE_PATH, CELL_FLAG_LEADING_SPACE
            );
            assert(not error);  // was a path when we got it!
            UNUSED(error);
        }
        else
            assert(Heart_Of(vars) == TYPE_CHAIN);
    }
    if (Is_Metaform(vars))
        Metafy(where);

    Corrupt_Pointer_If_Debug(vars);  // if in spare, we may have overwritten

    goto finished_making_let_bindings;

}} handle_block_or_set_block: {

    // 1. In the "LET dialect", quoted words pass through things with their
    //    existing binding, but allowing them to participate in the same
    //    multi-return operation:
    //
    //        let [value error]
    //        [value position error]: transcode data  ; awkward
    //
    //        let [value 'position error]: transcode data  ; better
    //
    //    This is applied generically: no quoted items are processed by the
    //    LET...it merely removes the quoting level and generates a new block
    //    as output which doesn't have the quote.
    //
    // 2. Once the multi-return dialect was planned to have more features like
    //    naming arguments literally.  That wouldn't have any meaning to LET
    //    and would be skipped.  That feature of naming outputs has been
    //    scrapped, though...so questions about what to do if things like
    //    integers etc. appear in blocks are open at this point.

    if (Is_Set_Block(vars))
        assert(Get_Level_Flag(L, LET_IS_SETTING));
    else {
        assert(Not_Level_Flag(L, LET_IS_SETTING));
        assert(Is_Block(vars));
    }

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
            Unquotify(TOP_ELEMENT);  // drop quote in output block [1]
            altered = true;
            continue;  // do not make binding
        }

        if (Is_Group(temp)) {  // evaluate non-QUOTED? groups in LET block
            if (Eval_Any_List_At_Throws(OUT, temp, item_binding))
                return THROWN;

            if (Is_Void(OUT)) {
                Init_Space(OUT);
            }
            else {
                Decay_If_Unstable(OUT);
                if (Is_Antiform(OUT))
                    return PANIC(Error_Bad_Antiform(OUT));
            }

            temp = cast(Element*, OUT);
            temp_binding = SPECIFIED;

            altered = true;
        }

        if (Is_Set_Word(temp))
            goto wordlike;

        if (Is_Path(temp)) {
            Option(SingleHeart) singleheart;
            switch ((singleheart = Try_Get_Sequence_Singleheart(temp))) {
              case LEADING_SPACE_AND(WORD):
                goto wordlike;

              case LEADING_SPACE_AND(TUPLE):  // should pass through!
              default:
                break;
            }
            return PANIC("LET only supports /WORD for paths for now...");
        }

        switch (Heart_Of(temp)) {  // permit quasi
          case TYPE_RUNE:  // is multi-return opt for dialect, passthru
            Derelativize(PUSH(), temp, temp_binding);
            break;

          wordlike:
          case TYPE_WORD: {
            Derelativize(PUSH(), temp, temp_binding);  // !!! no derel
            const Symbol* symbol = Cell_Word_Symbol(temp);
            bindings = Make_Let_Variable(symbol, bindings);
            Tweak_Cell_Binding(TOP_ELEMENT, bindings);
            Tweak_Cell_Word_Stub(TOP_ELEMENT, bindings);
            break; }

          default:
            return PANIC(temp);  // default to passthru [2]
        }
    }

    Sink(Element) where = Get_Level_Flag(L, LET_IS_SETTING)
        ? SPARE
        : OUT;

    if (altered) {  // elements altered, can't reuse input block rebound
        assert(Get_Level_Flag(L, LET_IS_SETTING));
        Setify(Init_Any_List(
            where,  // may be SPARE, and vars may point to it
            TYPE_BLOCK,
            Pop_Managed_Source_From_Stack(STACK_BASE)
        ));
    }
    else {
        Drop_Data_Stack_To(STACK_BASE);

        if (vars != where)
            Copy_Cell(where, vars);  // Move_Cell() of ARG() not allowed
    }
    Tweak_Cell_Binding(where, bindings);

    Corrupt_Pointer_If_Debug(vars);  // if in spare, we may have overwritten

} finished_making_let_bindings: {

    Tweak_Cell_Binding(Element_LOCAL(BINDINGS_HOLDER), bindings);

    if (Get_Level_Flag(L, LET_IS_SETTING))
        goto eval_right_hand_side_if_let_is_setting;

    Element* out = Known_Element(OUT);
    assert(Is_Word(out) or Is_Block(out) or Is_Meta_Form_Of(WORD, out));
    USED(out);
    goto integrate_let_bindings;

}} eval_right_hand_side_if_let_is_setting: {  // no `bindings` use after here

    // We want the left hand side to use the *new* LET bindings, but the right
    // hand side should use the *old* bindings.  For instance:
    //
    //     let /assert: specialize assert/ [handler: [print "should work!"]]
    //
    // Leverage same mechanism as REEVAL to preload the next execution step
    // with the rebound SET-WORD! or SET-BLOCK!
    //
    // (We want infix operations to be able to "see" the left side of the
    // LET, so this requires reevaluation--as opposed to just evaluating
    // the right hand side and then running SET on the result.)

    Element* spare = Known_Element(SPARE);
    assert(
        Try_Get_Settable_Word_Symbol(nullptr, spare)
        or Is_Set_Block(spare)
    );

    Flags flags =
        FLAG_STATE_BYTE(ST_STEPPER_REEVALUATING)
        | (L->flags.bits & EVAL_EXECUTOR_FLAG_FULFILLING_ARG);

    Level* sub = Make_Level(&Stepper_Executor, LEVEL->feed, flags);
    Copy_Cell(Evaluator_Level_Current(sub), spare);
    Force_Invalidate_Gotten(&sub->u.eval.current_gotten);

    Push_Level_Erase_Out_If_State_0(OUT, sub);

    STATE = ST_LET_EVAL_STEP;
    return CONTINUE_SUBLEVEL(sub);

} integrate_eval_bindings: {  ////////////////////////////////////////////////

    // The evaluation may have expanded the bindings, as in:
    //
    //     let y: let x: 1 + 2 print [x y]
    //
    // The LET Y: is running the LET X step, but if it doesn't incorporate that
    // it will be setting the feed's bindings to just include Y.  We have to
    // merge them, with the outer one taking priority:
    //
    //    >> x: 10, let x: 1000 + let x: x + 10, print [x]
    //    1020
    //
    // !!! Currently, any bindings added during eval are lost. Review this.
    //
    // 1. When it was looking at infix, the evaluator caches the fetched value
    //    of the word for the next execution.  But we are pulling the rug out
    //    from under that if the immediately following item is the same as
    //    what we have... or a TUPLE! starting with it, etc.
    //
    //        (x: 10 let x: 20 x)  (x: 10 let x: make object! [y: 20] x.y)
    //
    //    We could try to be clever and maintain that cache in the cases that
    //    call for it.  But with evaluator hooks we don't know what kinds of
    //    overrides it may have (maybe the binding for items not at the head
    //    of a path is relevant?)  Simplest thing to do is drop the cache.

    Invalidate_Gotten(&L->feed->gotten);  // invalidate next word's cache [1]
    goto integrate_let_bindings;

} integrate_let_bindings: {  /////////////////////////////////////////////////

    // Going forward we want the feed's binding to include the LETs.  Note
    // that this can create the problem of applying the binding twice; this
    // needs systemic review.

    Context* bindings = Cell_List_Binding(Element_LOCAL(BINDINGS_HOLDER));
    Tweak_Cell_Binding(Feed_Data(L->feed), bindings);

    return OUT;
}}


//
//  add-let-binding: native [
//
//  "Experimental function for adding a new variable binding"
//
//      return: [frame! any-list?]
//      environment [frame! any-list?]
//      word [word! ^word!]
//      ^value [any-atom?]
//  ]
//
DECLARE_NATIVE(ADD_LET_BINDING)
//
// !!! At time of writing, there are no "first class environments" that
// expose the "Specifier" chain in arrays.  So the arrays themselves are
// used as proxies for that.  Usermode dialects (like UPARSE) update their
// environment by passing in a rule block with a version of that rule block
// with an updated binding.  A function that wants to add to the evaluator
// environment uses the frame at the moment.
//
{
    INCLUDE_PARAMS_OF_ADD_LET_BINDING;

    Element* env = Element_ARG(ENVIRONMENT);
    Context* parent;

    Element* word = Element_ARG(WORD);

    if (Is_Frame(env)) {
        Level* L = Level_Of_Varlist_May_Panic(Cell_Varlist(env));
        parent = Level_Binding(L);
        if (parent)
            Set_Base_Managed_Bit(parent);
    } else {
        assert(Any_List(env));
        parent = Cell_List_Binding(env);
    }

    Let* let = Make_Let_Variable(Cell_Word_Symbol(ARG(WORD)), parent);

    Atom* atom = Atom_ARG(VALUE);
    if (Is_Meta_Form_Of(WORD, word)) {
        Copy_Cell(Stub_Cell(let), atom);  // don't decay
    }
    else {
        assert(Is_Word(word));
        Value* value = Decay_If_Unstable(atom);
        Copy_Cell(Stub_Cell(let), value);
    }

    if (Is_Frame(env)) {
        Level* L = Level_Of_Varlist_May_Panic(Cell_Varlist(env));
        Tweak_Cell_Binding(Feed_Data(L->feed), let);
    }
    else {
        Tweak_Cell_Binding(env, let);
    }

    return COPY(env);
}


//
//  add-use-object: native [
//
//  "Experimental function for adding an object's worth of binding to a frame"
//
//      return: []
//      frame [frame!]
//      object [object!]
//  ]
//
DECLARE_NATIVE(ADD_USE_OBJECT) {
    INCLUDE_PARAMS_OF_ADD_USE_OBJECT;

    Element* object = Element_ARG(OBJECT);

    Level* L = Level_Of_Varlist_May_Panic(Cell_Varlist(ARG(FRAME)));
    Context* L_binding = Level_Binding(L);

    if (L_binding)
        Set_Base_Managed_Bit(L_binding);

    Use* use = Alloc_Use_Inherits(L_binding);
    Copy_Cell(Stub_Cell(use), object);

    Tweak_Cell_Binding(Feed_Data(L->feed), use);

    return TRIPWIRE;
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
//    !!! This feature became disabled when $word bound to environments.
//    Review if a similar optimization could be helpful in the future.
//
//      https://rebol.metaeducation.com/t/copying-function-bodies/2119/4
//
// 3. If we're cloning a sequence, we have to copy the mirror byte.  If it's
//    a plain array that happens to have been aliased somewhere as a sequence,
//    we don't know if it's going to be aliased as that same sequence type
//    again...but is that worth testing if it's a sequence here?
//
void Clonify_And_Bind_Relative(
    Element* v,
    Flags flags,
    bool deeply,
    Option(Binder*) binder,
    Option(Phase*) relative
){
    assert(flags & BASE_FLAG_MANAGED);

    Option(Heart) heart = Unchecked_Heart_Of(v);

    if (
        relative
        and Wordlike_Cell(v)
        and IS_WORD_UNBOUND(v)  // use unbound words as "in frame" cache [1]
    ){
        // [2] is not active at this time
    }
    else if (deeply and (Any_Series_Type(heart) or Any_Sequence_Type(heart))) {
        //
        // Objects and Flexes get shallow copied at minimum
        //
        Element* deep = nullptr;
        Element* deep_tail = nullptr;

        if (Pairlike_Cell(v)) {
            Pairing* copy = Copy_Pairing(
                Cell_Pairing(v),
                BASE_FLAG_MANAGED
            );
            CELL_PAIRLIKE_PAIRING_NODE(v) = copy;

            deep = Pairing_Head(copy);
            deep_tail = Pairing_Tail(copy);
        }
        else if (Listlike_Cell(v)) {  // ruled out pairlike sequences above...
            Source* copy = cast(Source*, Copy_Array_At_Extra_Shallow(
                FLEX_MASK_MANAGED_SOURCE,
                Cell_Array(v),
                0,  // !!! what if VAL_INDEX() is nonzero?
                0
            ));
            /* if (Any_Sequence(v)) */  // copy regardless? [3]
                MIRROR_BYTE(copy) = MIRROR_BYTE(Cell_Array(v));

            CELL_PAIRLIKE_PAIRING_NODE(v) = copy;

            // See notes in Clonify()...need to copy immutable paths so that
            // binding pointers can be changed in the "immutable" copy.
            //
            if (Any_Sequence_Type(heart))
                Freeze_Source_Shallow(copy);

            // !!! At one point, arrays were marked relative as well as the
            // words in function bodies.  Now it's needed to consider them
            // to be unbound most of the time.

            deep = Array_Head(copy);
            deep_tail = Array_Tail(copy);
        }
        else if (Any_Series_Type(heart)) {
            Flex* copy = Copy_Flex_Core(BASE_FLAG_MANAGED, Cell_Flex(v));
            CELL_PAIRLIKE_PAIRING_NODE(v) = copy;
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
        v->header.bits |= (flags & ARRAY_FLAG_CONST_SHALLOW);
    }
}


//
//  Copy_And_Bind_Relative_Deep_Managed: C
//
// This routine is called by Make_Phase to copy the body deeply, and while
// it is doing that it puts a cache in any unbound words of whether or not
// that words can be found in the function's frame.
//
Source* Copy_And_Bind_Relative_Deep_Managed(
    const Value* body,
    Details* relative,
    LensMode lens_mode
){
    DECLARE_BINDER (binder);
    Construct_Binder(binder);

  add_binder_indices: {

  // Setup binding table from the argument word list.  Note that some cases
  // (like an ADAPT) reuse the exemplar from the function they are adapting,
  // and should not have the locals visible from their binding.  Other cases
  // such as the plain binding of the body of a FUNC created the exemplar
  // from scratch, and should see the locals.  Caller has to decide.

    EVARS e;
    Init_Evars(&e, Phase_Archetype(relative));
    e.lens_mode = lens_mode;
    while (Try_Advance_Evars(&e))
        Add_Binder_Index(binder, Key_Symbol(e.key), e.index);
    Shutdown_Evars(&e);

} shallow_copy_then_adjust: {

    const Source* original = Cell_Array(body);
    REBLEN index = VAL_INDEX(body);
   /* Context* binding = Cell_List_Binding(body); */
    REBLEN tail = Cell_Series_Len_At(body);
    assert(tail <= Array_Len(original));

    if (index > tail)  // !!! should this be asserted?
        index = tail;

    Flags flags = FLEX_MASK_MANAGED_SOURCE;
    bool deeply = true;

    REBLEN len = tail - index;

    // Currently we start by making a shallow copy and then adjust it

    Source* copy = cast(Source*, Make_Array_For_Copy(flags, original, len));
    Set_Flex_Len(copy, len);

    const Element* src = Array_At(original, index);
    Element* dest = Array_Head(copy);
    REBLEN count = 0;
    for (; count < len; ++count, ++dest, ++src) {
        Copy_Cell(dest, src);
        Clonify_And_Bind_Relative(
            dest,
            flags | BASE_FLAG_MANAGED,
            deeply,
            binder,
            relative
        );
    }

    Destruct_Binder(binder);
    return copy;
}}


//
//  Create_Loop_Context_May_Bind_Body: C
//
// Looping constructs are parameterized by WORD!s to set each time:
//
//    for-each [x y] [1 2 3] [print ["x is" x "and y is" y]]
//
// Ren-C adds a feature of letting @WORD! be used to indicate that the loop
// variable should be written into the existing bound variable that the @WORD!
// specified.
//
// 1. The returned VarList* is an ordinary object, that outlives the loop:
//
//        x-word: ~
//        for-each x [1 2 3] [x-word: $x, break]
//        get x-word  ; returns 3
//
//    !!! Loops should probably free their objects by default when finished
//
// 2. The binding of BODY will only be modified if there are actual new loop
//    variables created.  So (for-each @x ...) won't make any new variables.
//    But note that this means there won't be a reference to the VarList*
//    containing the alias that gets created in that case...which means that
//    the caller needs to be sure that VarList* gets GC-protected.
//
//    !!! Consider perhaps finding a way to put a "no-op" in the bind chain
//    that can still hold onto the VarList*, to relieve the caller of the
//    concern of protecting the VarList*.
//
Option(Error*) Trap_Create_Loop_Context_May_Bind_Body(
    Sink(VarList*) varlist_out,  // VarList outlives loop [1]
    Element* body,  // binding modified if new loop variables created [2]
    Element* spec  // spec BLOCK! -or- just a plain WORD!, @WORD!, SPACE, etc.
){
    // !!! This just hacks in GROUP! behavior, because the :param convention
    // does not support groups and gives GROUP! by value.  In the stackless
    // build the preprocessing would most easily be done in usermode.
    //
    if (Is_Group(spec)) {
        DECLARE_ATOM (temp);
        if (Eval_Any_List_At_Throws(temp, spec, SPECIFIED))
            return Error_No_Catch_For_Throw(TOP_LEVEL);
        Decay_If_Unstable(temp);
        if (Is_Antiform(temp))
            return Error_Bad_Antiform(temp);
        Move_Cell(spec, cast(Element*, temp));
    }

    REBLEN num_vars = Is_Block(spec) ? Cell_Series_Len_At(spec) : 1;
    if (num_vars == 0)
        return Error_Bad_Value(spec);

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
            if (Is_Space(check)) {
                // Will be transformed into dummy item, no rebinding needed
            }
            else if (
                Is_Word(check)
                or Is_Meta_Form_Of(WORD, check)
                or Is_Tied_Form_Of(WORD, check)
            ){
                rebinding = true;
            }
            else if (not Is_Pinned_Form_Of(WORD, check)) {
                //
                // Better to error here, because if we wait until we're in
                // the middle of building the context, the managed portion
                // (keylist) would be incomplete and tripped on by the GC if
                // we didn't do some kind of workaround.
                //
                return Error_Bad_Value(check);
            }
        }
    }
    else {
        item = cast(Element*, spec);
        tail = cast(Element*, spec);
        binding = SPECIFIED;
        rebinding = Is_Word(item) or Is_Meta_Form_Of(WORD, item);
    }

    // KeyLists are always managed, but varlist is unmanaged by default (so
    // it can be freed if there is a problem)
    //
    VarList* varlist = Alloc_Varlist(TYPE_OBJECT, num_vars);

    // We want to check for duplicates and a Binder can be used for that
    // purpose--but note that a panic() cannot happen while binders are
    // in effect UNLESS the BUF_COLLECT contains information to undo it!
    // There's no BUF_COLLECT here, so don't panic while binder in effect.
    //
    DECLARE_BINDER (binder);
    Construct_Binder(binder);  // only used if `rebinding`

    Option(Error*) e = SUCCESS;

    SymId dummy_sym = SYM_DUMMY1;

    REBLEN index = 1;
    while (index <= num_vars) {
        const Symbol* symbol;

        if (Is_Space(item)) {
            if (dummy_sym == SYM_DUMMY9)
                e = Error_User(
                    "Current limitation: only up to 9 foreign/space keys"
                );

            symbol = Canon_Symbol(dummy_sym);
            dummy_sym = cast(SymId, cast(int, dummy_sym) + 1);

            Init(Slot) slot = Append_Context(varlist, symbol);
            Init_Space(slot);
            Set_Cell_Flag(slot, SLOT_WEIRD_DUAL);
            Set_Cell_Flag(slot, PROTECTED);

            if (rebinding)
                Add_Binder_Index(binder, symbol, -1);  // for remove
        }
        else if (
            Is_Word(item)
            or Is_Meta_Form_Of(WORD, item)
            or Is_Tied_Form_Of(WORD, item)
        ){
            assert(rebinding); // shouldn't get here unless we're rebinding

            symbol = Cell_Word_Symbol(item);

            if (Try_Add_Binder_Index(binder, symbol, index)) {
                Value* var = Init_Tripwire(Append_Context(varlist, symbol));
                if (Is_Meta_Form_Of(WORD, item))
                    Set_Cell_Flag(var, LOOP_SLOT_ROOT_META);
                else if (Is_Tied_Form_Of(WORD, item))
                    Set_Cell_Flag(var, LOOP_SLOT_NOTE_TIE);
            }
            else {  // note for-each [x @x] is bad, too
                DECLARE_ELEMENT (word);
                Init_Word(word, symbol);
                e = Error_Dup_Vars_Raw(word);
                break;
            }
        }
        else if (Is_Pinned_Form_Of(WORD, item)) {

            // Pinned word indicates that we wish to use the original binding.
            // So `for-each @x [1 2 3] [...]` will actually set that x
            // instead of creating a new one.
            //
            // We use the ALIAS dual convention, of storing the pinned word
            // in the slot with CELL_FLAG_SLOT_WEIRD_DUAL
            //
            if (dummy_sym == SYM_DUMMY9)
                e = Error_User(
                    "Current limitation: only up to 9 foreign/space keys"
                );

            symbol = Canon_Symbol(dummy_sym);
            dummy_sym = cast(SymId, cast(int, dummy_sym) + 1);

            Init(Slot) slot = Append_Context(varlist, symbol);
            Derelativize(slot, item, binding);
            Set_Cell_Flag(slot, SLOT_WEIRD_DUAL);

            if (rebinding)
                Add_Binder_Index(binder, symbol, -1);  // for remove
        }
        else {
            e = Error_User("Bad datatype in variable spec");
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

    Destruct_Binder(binder);  // must remove bind indices even if failing

    if (e) {
        Free_Unmanaged_Flex(varlist);
        return e;
    }

    Manage_Flex(varlist);  // must be managed to use in binding

    // If the user gets ahold of these contexts, we don't want them to be
    // able to expand them...because things like FOR-EACH have historically
    // not been robust to the memory moving.
    //
    Set_Flex_Flag(varlist, FIXED_SIZE);

    // Virtual version of `Bind_Values_Deep(Array_Head(body_out), context)`
    //
    if (rebinding) {
        Use* use = Alloc_Use_Inherits(Cell_List_Binding(body));
        Copy_Cell(Stub_Cell(use), Varlist_Archetype(varlist));
        Tweak_Cell_Binding(body, use);
    }

    *varlist_out = varlist;
    return SUCCESS;
}


//
//  Trap_Read_Slot_Meta: C
//
// Trap_Create_Loop_Context_May_Bind_Body() allows @WORD! for reusing an
// existing variable's binding:
//
//     x: 10
//     for-each @x [20 30 40] [...]
//     ; The 10 will be overwritten, and x will be equal to 40, here
//
// It accomplishes this by putting a word into the "variable" slot, and having
// a flag to indicate a dereference is necessary.
//
Option(Error*) Trap_Read_Slot_Meta(Sink(Atom) out, const Slot* slot)
{
    if (Get_Cell_Flag(slot, SLOT_WEIRD_DUAL)) {
        if (not Any_Lifted_Dual(slot))
            goto handle_dual_signal;

        Copy_Cell(out, u_cast(Value*, slot));
        Unliftify_Undecayed(out);
        return SUCCESS;
    }

  handle_non_weird: {

    const Value* var = Slot_Hack(slot);

    Copy_Cell(out, var);
    return SUCCESS;

} handle_dual_signal: { //////////////////////////////////////////////////////

    // e.g. `for-each _ [1 2 3] [...]` sets slot to "toss values"

    assert(not Is_Space(u_cast(Value*, slot)));

    Sink(Value) out_value = u_cast(Value*, out);
    assert(Is_Pinned_Form_Of(WORD, slot));
    if (rebRunThrows(out_value, CANON(GET), slot))
        return Error_No_Catch_For_Throw(TOP_LEVEL);

    return SUCCESS;
}}


//
//  Trap_Read_Slot: C
//
Option(Error*) Trap_Read_Slot(Sink(Value) out, const Slot* slot) {
    Sink(Atom) atom_out = u_cast(Atom*, out);
    Option(Error*) e = Trap_Read_Slot_Meta(atom_out, slot);
    if (e)
        return e;
    if (not Is_Stable(atom_out))
        return Error_User("Cannot read unstable slot with Trap_Read_Slot()");
    return SUCCESS;  // out is Known_Stable()
}


//
//  Trap_Write_Slot: C
//
Option(Error*) Trap_Write_Slot(Slot* slot, const Atom* write)
{
    Flags persist = (slot->header.bits & CELL_MASK_PERSIST_SLOT);

    if (Get_Cell_Flag(slot, SLOT_WEIRD_DUAL)) {
        if (Is_Dual_Unset(slot))
            goto handle_non_weird;  // can be overwritten

        if (not Any_Lifted_Dual(slot))
            goto handle_dual_signal;

        // fallthrough, just overwrite
    }

  handle_non_weird: {

    Atom* var = u_cast(Atom*, slot);

    Copy_Cell(var, write);

    slot->header.bits |= persist;  // preserve persist bits
    return SUCCESS;

} handle_dual_signal: { //////////////////////////////////////////////////////

    if (Is_Space(u_cast(Value*, slot)))  // e.g. `for-each _ [1 2 3] [...]`
        return SUCCESS;  // toss it

    assert(Is_Stable(write));

    assert(Is_Pinned_Form_Of(WORD, slot));
    rebElide(CANON(SET), slot, rebQ(u_c_cast(Value*, write)));

    slot->header.bits |= persist;  // preserve persist bits
    return SUCCESS;
}}


//
//  Trap_Write_Loop_Slot_May_Bind_Or_Decay: C
//
Option(Error*) Trap_Write_Loop_Slot_May_Bind_Or_Decay(
    Slot* slot,
    Option(Atom*) write,
    const Value* container
){
    if (not write)
        return Trap_Write_Slot(slot, LIB(NULL));

    if (
        Is_Atom_Action(unwrap write)
        and Not_Cell_Flag(slot, LOOP_SLOT_ROOT_META)
    ){
        return Error_User(
            "Cannot write to loop slot with ACTION! unless it is ^META"
        );
    }
    else if (
        not Is_Stable(unwrap write)
        and Not_Cell_Flag(slot, LOOP_SLOT_ROOT_META)
    ){
        Decay_If_Unstable(unwrap write);
    }

    if (Not_Cell_Flag(slot, LOOP_SLOT_NOTE_TIE))
        return Trap_Write_Slot(slot, unwrap write);

    if (not Any_List(container))
        return Error_User("Loop data must be list to use $var notation");

    DECLARE_ELEMENT (temp);
    Derelativize(
        temp,
        cast(const Element*, unwrap write),
        Cell_List_Binding(cast(const Element*, container))
    );
    Copy_Cell(unwrap write, temp);
    return Trap_Write_Slot(slot, temp);
}


//
//  Trap_Write_Loop_Slot_May_Bind: C
//
Option(Error*) Trap_Write_Loop_Slot_May_Bind(
    Slot* slot,
    Option(Value*) write,
    const Value* container
){
    return Trap_Write_Loop_Slot_May_Bind_Or_Decay(slot, write, container);
}


#if RUNTIME_CHECKS

//
//  Assert_Cell_Binding_Valid_Core: C
//
void Assert_Cell_Binding_Valid_Core(const Value* cell)
{
    Option(Heart) heart = Unchecked_Heart_Of(cell);
    assert(Is_Bindable_Heart(heart));

    Context* binding = u_cast(Context*, cell->extra.base);
    if (not binding)
        return;

    assert(not Is_Antiform(cell));  // antiforms should not have bindings

    assert(Is_Base(binding));
    assert(Is_Base_Managed(binding));
    assert(Is_Base_A_Stub(binding));
    assert(Is_Base_Readable(binding));

    if (heart == TYPE_FRAME) {
        assert(Is_Stub_Varlist(binding));  // actions/frames bind contexts only
        return;
    }

    if (Is_Stub_Let(binding)) {
        return;
    }

    if (Is_Stub_Patch(binding)) {
        assert(!"Direct binding to module patch cells is not allowed");
        return;
    }

    if (Is_Stub_Use(binding)) {
        /*assert(Listlike_Cell(cell));  // can't bind words to use */
        return;
    }

    if (Is_Stub_Details(binding)) {  // relative binding
        /* assert(Listlike_Cell(cell)); */  // weird word cache trick uses
        return;
    }

    if (Is_Stub_Sea(binding)) {
        // attachment binding no longer exists
        return;
    }

    assert(Is_Stub_Varlist(binding));
}

#endif
