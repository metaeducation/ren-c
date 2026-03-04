//
//  file: %c-function.c
//  summary: "support for functions, actions, and routines"
//  section: core
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2020 Ren-C Open Source Contributors
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


// This code should be shared with CONSTRUCT, for building an object on the
// stack...and share features with that dialect.
//
static Result(None) Push_Keys_And_Params_For_Fence(
    Level* const L,
    const Element* fence
){
    USE_LEVEL_SHORTHANDS (L);

    assert(Is_Fence(fence));

    require (
      Level* sub = Make_Level_At(&Stepper_Executor, fence, LEVEL_MASK_NONE)
    );
    Push_Level(Erase_Cell(SPARE), sub);

    while (Not_Level_At_End(sub)) {
        const Element* item = At_Level(sub);

        const Symbol* symbol;
        bool must_be_action = false;
        bool meta = false;
        SymId initializer;

        Option(SingleHeart) singleheart;

        if (Is_Word(item)) {
            symbol = Word_Symbol(item);
            initializer = SYM_NULL;
        }
        else if (Is_Meta_Form_Of(WORD, item)) {
            symbol = Word_Symbol(item);
            initializer = SYM_VOID;
            meta = true;
        }
        else if (Is_Quasi_Word(item)) {
            symbol = Word_Symbol(item);
            initializer = SYM_TRIPWIRE;
        }
        else if (
            Is_Path(item)
            and (singleheart = Try_Get_Sequence_Singleheart(item))
            and (
                singleheart == LEADING_BLANK_AND(WORD)
                /* or (meta = (singleheart == LEADING_BLANK_AND(META_WORD))) */
            )
        ){
            symbol = Word_Symbol(item);
            initializer = SYM_VOID;  // null is *bad* for "optional actions"
            must_be_action = true;
        }
        else
            return fail (Error_Bad_Value(item));

        Init_Word(PUSH(), symbol);

        Fetch_Next_In_Feed(sub->feed);

        if (
            Is_Level_At_End(sub)
            or not Is_Group(At_Level(sub))
        ) {
            switch (initializer) {
              case SYM_NULL: Init_Null(PUSH()); break;
              case SYM_VOID: Init_Void_Signifying_Unset(PUSH()); break;
              case SYM_TRIPWIRE: Init_Tripwire(PUSH()); break;
              default: assert(false);
            }
            continue;
        }

        Sync_Toplevel_Baseline_After_Pushes(sub);

        if (Trampoline_With_Top_As_Root_Throws()) {  // run the group
            Drop_Level_Unbalanced(sub);
            return fail (Error_No_Catch_For_Throw(sub));
        }

        if (meta or Is_Non_Meta_Assignable_Unstable_Antiform(SPARE)) {
            if (Is_Failure(SPARE))  // don't want to quietly store errors
                return fail (Cell_Error(SPARE));

            Move_Cell(PUSH(), SPARE);
        }
        else {
            require (
              Stable* decayed = Decay_If_Unstable(SPARE)
            );

            Move_Cell(PUSH(), decayed);

            if (must_be_action) {
                if (not Is_Frame(TOP_ELEMENT))
                    return fail (
                        "Assignment using /FOO must be an action or frame"
                    );

                Activate_Frame(TOP);
            }
        }

        Sync_Toplevel_Baseline_After_Pushes(sub);
        Reset_Stepper_Erase_Out(sub);
    }

    Drop_Level_Unbalanced(sub);  // we pushed values
    return none;
}


// This is an implementation helper for Make_Paramlist_Managed().
//
// It was broken out into its own separate routine so that the AUGMENT
// function could reuse the logic for function spec analysis.  It may not
// be broken out in a particularly elegant way, but it's a start.
//
static Result(None) Push_Keys_And_Params_Core(
    Element* methodization,
    Level* const L,
    Flags flags,
    Option(SymId) returner_id  // e.g. SYM_RETURN, SYM_YIELD, SYM_DUMMY1 ([]:)
){
    USE_LEVEL_SHORTHANDS (L);

    assert(Is_Quasar(methodization) or Is_Parameter(methodization));

    bool augment_initial_entry;
    bool seen_returner = false;
    StackIndex returner_index;

    Sink(Value) eval = Stepper_Primed_Value(L);
    Push_Level(Erase_Cell(eval), L);

  push_returner_if_not_augmenting: {

  // Not all functions have a RETURN or YIELD parameter slot.  But we always
  // have a slot at the head of the pushed parameters where we can put a
  // description for the function as a whole.  If the caller wants to handle
  // the return parameter in some special way vs. popping it into the
  // paramlist, they can use MKF_DONT_POP_RETURN.
  //
  // Since the returner typeset holds types that need to be checked at the
  // end of the function run, it is moved to a predictable location:
  // first slot of the paramlist.  (Initially it was the last slot...but this
  // enables adding more arguments/refinements/locals in derived functions).

    if (flags & MKF_AUGMENTING) {
        augment_initial_entry = true;
        returner_index = 0;
    }
    else {
        augment_initial_entry = false;

        if (returner_id and returner_id != SYM_DUMMY1)
            Init_Word(
                PUSH(),
                Canon_Symbol(Starred_Returner_Id(unwrap returner_id))
            );
        else
            Init_Word(PUSH(), Canon_Symbol(SYM_DUMMY1));

        Init_Unconstrained_Parameter(
            PUSH(),
            FLAG_PARAMCLASS_BYTE(PARAMCLASS_META)
        );
        Unspecialize_Parameter(TOP_SLOT);
        returner_index = TOP_INDEX;
    }

} loop: {

    if (Is_Level_At_End(L))
       goto finished;

    const Element* v = At_Level(L);

    if (Is_Text(v))
        goto handle_description_or_parameter_note;

    if (Heart_Of(v) == HEART_BLOCK)  // may be quoted to suppress typechecking
        goto handle_block_of_types_for_typeset;

    if (Is_Fence(v))
        goto handle_locals_in_fence;

    if (Is_Tag(v))
        goto handle_top_level_spec_tag;

    if (Is_Quasar(v))
        goto handle_quasar_for_auto_named_trash_return;

    goto handle_any_word_parameters_themselves;

  handle_description_or_parameter_note: { ////////////////////////////////////

    if (augment_initial_entry) {
        return fail (
            "Function description not allowed in AUGMENT spec"
        );
    }

    if (not Is_Cell_A_Bedrock_Hole(TOP_SLOT))
        return fail (
            "Text strings must describe parameters, not locals"
        );

    require (
        Strand* strand = Copy_String_At(v)
    );
    Manage_Stub(strand);
    Freeze_Flex(strand);
    Set_Parameter_Strand(TOP_SLOT, strand);
    goto next_spec_item;

} handle_block_of_types_for_typeset: { ///////////////////////////////////////

  // Includes parameter tags, like <opt>

    if (augment_initial_entry) {
        return fail (
            "Function return spec block not allowed in AUGMENT spec"
        );
    }

    if (Parameter_Spec(TOP_SLOT))  // `func [x [word!] [word!]]`
        return fail (Error_Bad_Func_Def_Raw(v));  // too many blocks

    Context* derived = Derive_Binding(Level_Binding(L), v);
    bool is_returner = (TOP_INDEX == returner_index);
    trap (
        Set_Spec_Of_Parameter_In_Top(L, v, derived, is_returner)
    );

    goto next_spec_item;

} handle_locals_in_fence: { //////////////////////////////////////////////////

  // The {...} syntax is used to create local variables.  The same syntax is
  // used that is used with CONSTRUCT (object creation dialect).
  //
  // !!! Since {...} in the evaluator does not run CONSTRUCT, the wisdom of
  // having this be dialected is questionable.  It may be that it should act
  // as a plain FENCE!, unless you use {{...}} or some other syntax.  Or,
  // maybe the divergence is okay in the wacky world of dialecting.

    Element* spare = Copy_Cell_May_Bind(SPARE, v, Level_Binding(L));
    trap (
        Push_Keys_And_Params_For_Fence(L, spare)
    );
    goto next_spec_item;

} handle_top_level_spec_tag: { /////////////////////////////////////////////

  // Currently the only top-level spec tag that is handled is <.> for saying
  // a function is "methodized".

    bool strict = false;

    if (augment_initial_entry) {
        return fail (
            "Augmentation cannot add methodization via <.>"
        );
    }

    if (0 == CT_Utf8(v, g_tag_dot_1, strict)) {
        if (not Is_Quasar(methodization))
            return fail ("Duplicate <.> in spec");

        Init_Unconstrained_Parameter(
            methodization,
            FLAG_PARAMCLASS_BYTE(PARAMCLASS_NORMAL)
        );
        Unspecialize_Parameter(methodization);
        goto next_spec_item;
    }

    return fail (Error_Bad_Value(v));

} handle_quasar_for_auto_named_trash_return: { ///////////////////////////////

  // [return: ~] means the function returns TRASH!, but also that the name of
  // the trash will be automatically generated from the WORD! the function
  // was dispatched from (if it was dispatched from a word...)

    if (augment_initial_entry)
        return fail (
            "Function return indicator not allowed in AUGMENT spec"
        );

    if (TOP_INDEX != returner_index)
        return fail (
            "Quasar (~) must be used to indicate function return spec"
        );

    if (Parameter_Spec(TOP_SLOT))  // `func [return: [integer!] ~]`
        return fail (Error_Bad_Func_Def_Raw(v));

    const Strand* notes = opt Parameter_Strand(TOP_SLOT);
    Copy_Cell(u_cast(Init(Slot), TOP_SLOT), g_auto_trash_param);
    Set_Parameter_Strand(TOP_SLOT, notes);
    Unspecialize_Parameter(TOP_SLOT);

    goto next_spec_item;

} handle_any_word_parameters_themselves: { ///////////////////////////////////

    bool quoted = false;  // single quote is "unbind" signal in spec
    Element* spare = Copy_Cell(SPARE, v);

    v = spare;  // need to mutate if CHAIN! or quoted

    if (Quotes_Of(v) > 0) {
        if (Type_Of(v) != TYPE_QUOTED_1_TIME_NONQUASI)
            return fail (Error_Bad_Func_Def_Raw(v));

        Clear_Cell_Quotes_And_Quasi(spare);
        quoted = true;

        if (Is_Word(v))
            panic (Error_Bad_Func_Def_Raw(v));  // until all 'word => '@word
    }

    bool refinement = false;  // paths with blanks at head are refinements
    const Symbol* symbol = nullptr;  // avoids compiler warning
    ParamClass pclass = PARAMCLASS_0;  // error if not changed

    if (not Is_Chain(v))
        goto post_chain_handling;

  handle_chain: {

    Option(SingleHeart) singleheart = Try_Get_Sequence_Singleheart(v);
    if (not singleheart)
        return fail (Error_Bad_Func_Def_Raw(v));

    trap (
      Unsingleheart_Sequence(spare)  // updates v
    );

    switch (unwrap singleheart) {
      case TRAILING_BLANK_AND(BLOCK): {
        if (
            returner_id != SYM_DUMMY1  // used by LAMBDA (hack)
            or Series_Len_At(spare) != 0
        ){
            return fail (
                "SET-BLOCK! result spec only allowed as []: in LAMBDA"
            );
        }
        symbol = CANON(DUMMY1);
        pclass = PARAMCLASS_META;
        break; }

      case TRAILING_BLANK_AND(WORD): {
        if (
            quoted
            or not returner_id
            or Word_Id(v) != unwrap returner_id
        ){
            return fail (
                "SET-WORD in spec must match RETURN:/YIELD: name"
            );
        }
        symbol = Word_Symbol(v);
        break; }

      default: {
        if (not Singleheart_Has_Leading_Blank(unwrap singleheart))
            return fail (Error_Bad_Func_Def_Raw(v));

        refinement = true;
        goto post_chain_handling; }
    }

    if (
        returner_id
        and Symbol_Id(symbol) == unwrap returner_id
    ){
        if (seen_returner) {
            if (SYM_DUMMY1 == unwrap returner_id)
                return fail ("Duplicate []: in lambda spec");
            if (SYM_RETURN == unwrap returner_id)
                return fail ("Duplicate RETURN: in function spec");
            assert(SYM_YIELD == unwrap returner_id);
            return fail ("Duplicate YIELD: in yielder spec");
        }
        goto next_spec_item;
    }

    goto post_chain_handling;

} post_chain_handling: { /////////////////////////////////////////////////////

    if (Heart_Of(v) == HEART_WORD) {
        symbol = Word_Symbol(v);

        switch (opt Sigil_Of(v)) {
          case SIGIL_0_constexpr:
            pclass = PARAMCLASS_NORMAL;
            break;

          case SIGIL_PIN:
            pclass = PARAMCLASS_LITERAL;
            break;

          case SIGIL_META:
            pclass = PARAMCLASS_META;
            break;

          case SIGIL_TIE:  // aliasable parmaeters... coming soon!
            return fail (Error_Bad_Func_Def_Raw(v));
        }

        goto push_parameter;
    }

    if (Is_Pinned_Form_Of(GROUP, v)) {  // @(...) PARAMCLASS_SOFT
        if (Series_Len_At(v) == 1) {
            const Element* word = List_Item_At(v);
            if (Is_Word(word)) {
                pclass = PARAMCLASS_SOFT;
                symbol = Word_Symbol(word);
            }
        }
        goto push_parameter;
    }

    return fail (Error_Bad_Func_Def_Raw(v));

} push_parameter: { //////////////////////////////////////////////////////////

    if (pclass == PARAMCLASS_0)  // didn't match
        return fail (Error_Bad_Func_Def_Raw(v));

    if (returner_id and Symbol_Id(symbol) == unwrap returner_id) {
        if (SYM_DUMMY1 == unwrap returner_id)
            return fail (
                "DUMMY1 is a reserved arg name in LAMBDA due to a hack :-("
            );
        if (SYM_RETURN == unwrap returner_id)
            return fail (
                "Generator provides RETURN:, use LAMBDA if not desired"
            );
        assert(SYM_YIELD == unwrap returner_id);
        return fail (
            "Generator provides YIELD:, can't have YIELD parameter"
        );
    }

    Init_Word(PUSH(), symbol);  // duplicates caught when popping

    if (refinement) {
        Init_Unconstrained_Parameter(
            PUSH(),
            FLAG_PARAMCLASS_BYTE(pclass)
                | PARAMETER_FLAG_REFINEMENT  // must preserve if type block
                | PARAMETER_FLAG_NULL_DEFINITELY_OK  // need if refinement
                | (quoted ? PARAMETER_FLAG_UNBIND_ARG : 0)
        );
    }
    else {
        Init_Unconstrained_Parameter(
            PUSH(),
            FLAG_PARAMCLASS_BYTE(pclass)
                | (quoted ? PARAMETER_FLAG_UNBIND_ARG : 0)
        );
    }
    Unspecialize_Parameter(TOP_SLOT);

    // Non-annotated arguments allow all parameter types.

    goto next_spec_item;

}} next_spec_item: {

    Fetch_Next_In_Feed(L->feed);
    augment_initial_entry = false;

    goto loop;

}} finished: {

    if (returner_index)  // plain param would gather arg, trick is to quote it
        Regularize_Parameter_Local(Data_Stack_At(Param, returner_index));

    return none;
}}


//
//  Push_Keys_And_Params: C
//
// Wrapper allowing `return fail` from Push_Keys_And_Params_Core() to properly
// balance the stack.
//
Result(None) Push_Keys_And_Params(
    Element* methodization,
    const Element* spec,
    Flags flags,
    Option(SymId) returner  // e.g. SYM_RETURN or SYM_YIELD
){
    require (
      Level* L = Make_Level_At(
        &Stepper_Executor,
        spec,
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
    ));

    Push_Keys_And_Params_Core(
        methodization, L, flags, returner
    ) except (Error* e) {
        Drop_Data_Stack_To(L->baseline.stack_base);
        Drop_Level(L);
        return fail (e);
    }

    Drop_Level_Unbalanced(L);  // pushed values on stack meant to be there
    return none;
}


//
//  Pop_Paramlist: C
//
// Assuming the stack is formed in pairs of symbol WORD! for key and a
// parameter (possibly an antiform PARAMETER!, or specialized local), this
// produces a paramlist in a state suitable for Make_Dispatch_Details().
//
// It may not succeed because there could be duplicate parameters on the stack,
// and the checking via a binder is done as part of this popping process.
//
Result(ParamList*) Pop_Paramlist(
    StackIndex base,
    Option(const Element*) methodization,
    Option(Phase*) prior,
    Option(VarList*) prior_coupling,
    Option(Element*) gather
){
  gather_locals_if_needed: {

  // The locals gathering mechanic has to match the logic of FENCE! in general
  // but with a twist: it's putting these into the FRAME! of the function vs.
  // in a separate object, and it's not re-adding any parameters or locals
  // that were already in the spec.
  //
  // This is an awkward re-use of the WRAP code, which accomplishes the goal
  // of not re-creating that collection logic here.  However, we wind up
  // walking the parameter list twice.

    if (gather) {
        assert(Is_Fence(unwrap gather));

        CollectFlags flags = COLLECT_ONLY_SET_WORDS;

        DECLARE_COLLECTOR (cl);
        Construct_Collector(cl, flags, nullptr);

      add_existing_parameters_and_locals: {

        StackIndex stackindex = base + 1;
        for (; stackindex <= TOP_INDEX; stackindex += 2) {
            const Symbol* symbol = Word_Symbol(Data_Stack_Cell_At(stackindex));

            if (Try_Add_Binder_Index(
                cl->binder,
                symbol,
                cl->next_index
            )){
                ++cl->next_index;
            }
            else {
                // Duplicate parameter found, code below has error for it
                // Just fall through to whatever that error is for now.
                // (Ultimately we want to avoid duplicating this work.)
            }
        }

      } gather_top_level_declarations_to_stack: {

        Option(Stump*) params_stump = cl->binder->stump_list;

        const Element* tail;
        const Element* at = List_At(&tail, unwrap gather);

        Collect_Inner_Loop(
            cl, flags, at, tail
        ) except (Error* e) {
            Destruct_Collector(cl);
            return fail (e);
        }

        Option(Stump*) stump = cl->binder->stump_list;
        for (; stump != params_stump; stump = Link_Stump_Next(unwrap stump)) {
            const Symbol* symbol = Info_Stump_Bind_Symbol(unwrap stump);
            Init_Word(PUSH(), symbol);
            Init_Void_Signifying_Unset(PUSH());
        }

        Destruct_Collector(cl);
    }}

} process_parameters_and_locals_on_stack: {

    Count num_params = (TOP_INDEX - base) / 2 + (methodization ? 1 : 0);

    require (
      KeyList* keylist = u_downcast Make_Flex(
        STUB_MASK_KEYLIST | BASE_FLAG_MANAGED,
        num_params
    ));
    Set_Flex_Used(keylist, num_params);  // no terminator
    Tweak_Link_Keylist_Ancestor(keylist, keylist);  // chain

    Array* paramlist = Make_Array_Core(
        STUB_MASK_PARAMLIST,
        num_params + 1  // +1 for rootvar
    );
    Set_Flex_Len(paramlist, num_params + 1);

    // We want to check for duplicates and a Binder can be used for that
    // purpose--but panic() isn't allowed while binders are in effect.
    //
    // (This is why we wait until the parameter list gathering process
    // is over to do the duplicate checks--it can fail.)
    //
    Binder* binder = Construct_Binder();

    const Symbol* duplicate = nullptr;

    Param* rootvar = Flex_Head(Param, paramlist);
    if (prior)
        Init_Frame(rootvar, unwrap prior, ANONYMOUS, prior_coupling);
    else
        Init_Frame_Unchecked(
            rootvar,
            u_cast(ParamList*, paramlist),  // partially formed, u_cast() !
            ANONYMOUS,
            UNCOUPLED
        );

    Param* param = 1 + rootvar;
    Key* key = Flex_Head(Key, keylist);

    if (methodization) {  // put dot first if applicable (find it fastest...)
        *key = CANON(DOT_1);
        Copy_Cell(param, unwrap methodization);
        Regularize_Parameter_Local(param);
        Set_Cell_Flag(param, PARAM_NOTE_TYPECHECKED);
        ++key;
        ++param;

        Set_Flavor_Flag(VARLIST, paramlist, METHODIZED);
    }

    StackIndex stackindex = base + 1;  // empty stack base would be 0, bad cell
    for (; stackindex <= TOP_INDEX; stackindex += 2) {
        const Symbol* symbol = Word_Symbol(Data_Stack_Cell_At(stackindex));
        OnStack(Param*) slot = Data_Stack_At(Param, stackindex + 1);

        // "Sealed" parameters do not count in the binding.  See AUGMENT for
        // notes on why we do this (you can augment a function that has a
        // local called `x` with a new parameter called `x`, and that's legal.)
        //
        if (Get_Cell_Flag(slot, PARAM_MARKED_SEALED)) {
            assert(Is_Specialized(slot));
        }
        else {
            if (not Try_Add_Binder_Index(binder, symbol, 1020))
                duplicate = symbol;
        }

        *key = symbol;

        Copy_Cell_Core(
            param,
            slot,
            CELL_MASK_COPY
                | CELL_FLAG_PARAM_MARKED_SEALED
        );
        if (Is_Cell_A_Bedrock_Hole(param))
            Set_Parameter_Flag(param, FINAL_TYPECHECK);
        else
            Set_Cell_Flag(param, PARAM_NOTE_TYPECHECKED);  // locals "checked"

        Shield_Param_If_Tracking(param);

        ++key;
        ++param;
    }

    assert(param == Flex_Tail(Param, paramlist));

    Manage_Stub(paramlist);

    Tweak_Bonus_Keylist_Unique(paramlist, keylist);
    Tweak_Misc_Varlist_Adjunct_Raw(paramlist, nullptr);
    Tweak_Link_Inherit_Bind_Raw(paramlist, nullptr);

    // With all the values extracted from stack to array, restore stack pointer
    //
    Drop_Data_Stack_To(base);

    // Must remove binder indexes for all words, even if about to fail
    //
    Destruct_Binder(binder);

    if (duplicate)
        return fail (Error_Dup_Vars_Raw(duplicate));

    Assert_Flex_Term_If_Needed(paramlist);
    return cast(ParamList*, paramlist);
}}


//
//  Make_Paramlist_Managed: C
//
// Check function spec of the form:
//
//     ["description" arg "notes" [type! type2! ...] :ref ...]
//
// !!! The spec language was not formalized in R3-Alpha.  Strings were left
// in and it was HELP's job (and any other clients) to make sense of it, e.g.:
//
//     [foo [type!] "doc string :-)"]
//     [foo "doc string :-/" [type!]]
//     [foo "doc string1 :-/" "doc string2 :-(" [type!]]
//
// Ren-C centralizes the processing, and clients can use FRAME! access to
// pick out the parameters and descriptions.  So although the spec could use
// some formalization, at least only one place has to worry about it.
//
// 1. The process is broken up into phases so that the spec analysis code can
//    be reused in AUGMENT.
//
Result(ParamList*) Make_Paramlist_Managed(
    const Element* spec,
    Flags flags,  // flags may be modified to carry additional information
    Option(SymId) returner,  // e.g. SYM_YIELD, SYM_RETURN, or SYM_DUMMY1 ([]:)
    Option(Element*) gather
){
    Element* methodization = Init_Quasar(PUSH());

    StackIndex base = TOP_INDEX;  // note we have to pop the methodization too

    Push_Keys_And_Params(  // separate Push() phase [1]
        methodization, spec, flags, returner
    ) except (Error* e) {
        return fail (e);
    }

    Option(Phase*) prior = nullptr;
    Option(VarList*) prior_coupling = nullptr;

    if (flags & MKF_DONT_POP_RETURN) {  // will have to pop methodization too
        assert(TOP_INDEX - base >= 2);
        base += 2;
    }

    trap (
      ParamList* paramlist = Pop_Paramlist(  // separate Pop() phase [1]
        base,
        Is_Quasar(methodization) ? nullptr : methodization,
        prior,
        prior_coupling,
        gather
      )
    );

    if (not (flags & MKF_DONT_POP_RETURN))
        DROP();  // pop methodization (wasn't popped in Pop_Paramlist)

    return paramlist;
}


//
//  Pop_Unpopped_Return: C
//
// If you use MKF_DONT_POP_RETURN, the return won't be part of the paramlist
// but left on the stack.  Natives put this in the Details array, as does
// LAMBDA because it doesn't have a definitional returning concept.
//
void Pop_Unpopped_Return(Sink(Element) out, StackIndex base)
{
    assert(TOP_INDEX == base + 3);

    assert(Is_Parameter(TOP_ELEMENT));
    Copy_Cell(out, TOP_ELEMENT);
    DROP();

    assert(
        Word_Id(TOP) == Starred_Returner_Id(SYM_RETURN)
        or Word_Id(TOP) == SYM_DUMMY1
    );
    DROP();

    assert(Is_Cell_A_Bedrock_Hole(TOP_SLOT) or Is_Quasar(TOP_ELEMENT));
    DROP();  // pop methodization storage cell

    UNUSED(base);
}


//
//  Make_Dispatch_Details: C
//
// Create an archetypal form of a function, given C code implementing a
// dispatcher that will be called by Eval_Core.  Dispatchers are of the form:
//
//     Bounce Dispatcher(Level* L) {...}
//
// The `paramlist` argument is an interface structure that holds information
// that can be shared between function instances.  It encodes information
// about the parameter names and types and specialization data.
//
// Details are Arrays, but for reasons of inheritance are not exposed as such.
// You need to use Details_At() to access the array items.  This is where they
// can store information that will be available when the dispatcher is called.
//
// The Details returned is "archetypal" because individual cells which hold
// the same Details may differ in a per-Cell "binding".  (This is how one
// RETURN is distinguished from another--the binding data stored in the cell
// identifies the pointer of the FRAME! to exit).
//
Details* Make_Dispatch_Details(
    Flags flags,
    const Value* exemplar,  // FRAME!/ACTION!, "interface" is MSVC keyword :-(
    Dispatcher* dispatcher,  // native C function called by Action_Executor()
    Option(Ordinal) details_max  // 1-based max index desired for Phase_Details
){
    assert(Is_Possibly_Unstable_Value_Frame(exemplar) or Is_Action(exemplar));

  check_flags: {

  // 1. Callers don't pass DETAILS_FLAG_PURE or DETAILS_FLAG_IMPURE because we
  //    we want these to be inherited from the exemplar typically.  And if
  //    they're added or tweaked after the fact, that's done by the PURE and
  //    IMPURE functions.
  //
  //    (You can turn PURE:OFF or IMPURE:OFF if the default inheritance is
  //    not what you want, and purity will still be respected when the phase
  //    is adjusted, see Tweak_Level_Phase())
  //
  // 2. We can't build actions on top of other ones unless we can trust they
  //    are not going to change.  Regardless of whether you are building on
  //    a FRAME! or an ACTION!, the phase must be final.  This locks down
  //    the bit for STUB_PHASE_LITERAL_FIRST so we can inherit it.

    assert(0 == (flags & (~ (  // make sure no stray flags passed in
        BASE_FLAG_MANAGED
            | DETAILS_FLAG_CAN_DISPATCH_AS_INTRINSIC
            | DETAILS_FLAG_API_CONTINUATIONS_OK
            | DETAILS_FLAG_RAW_NATIVE
            | DETAILS_FLAG_METHODIZED  // inherit from exemplar
            | (not DETAILS_FLAG_IMPURE)  // inherit [1]
            | (not DETAILS_FLAG_PURE)  // inherit [1]
    ))));

    Phase* exemplar_phase = Frame_Phase(exemplar);
    Force_Phase_Final(exemplar_phase);  // [2]

    flags |= exemplar_phase->header.bits & (  // inherit flags
        STUB_FLAG_PHASE_PURE
            | STUB_FLAG_PHASE_IMPURE
            | STUB_FLAG_PHASE_LITERAL_FIRST
    );

    ParamList* exemplar_paramlist = Phase_Paramlist(exemplar_phase);
    assert(Get_Flavor_Flag(VARLIST, exemplar_paramlist, IMMUTABLE));  // [2]
    if (Get_Flavor_Flag(VARLIST, exemplar_paramlist, METHODIZED))
        flags |= DETAILS_FLAG_METHODIZED;

} make_details: {

    // "details" for an action is an array of cells which can be anything
    // the dispatcher understands it to be, by contract.
    //
    Array* a = Make_Array_Core(
        STUB_MASK_DETAILS | flags,  // don't add BASE_FLAG_MANAGED
        (opt details_max) + 1  // if max is 0, then only Phase_Archetype()
    );
    Set_Flex_Len(a, (opt details_max) + 1);

    Cell* rootvar = Array_Head(a);
    Copy_Cell(rootvar, exemplar);
    Tweak_Cell_Type_Byte(rootvar, TYPE_FRAME);  // canonize ACTION! to FRAME!
    Shield_Rootvar_If_Tracking(rootvar);

    // Leave rest of the cells in the capacity uninitialized (caller fills in)

    Tweak_Details_Dispatcher(cast(Details*, a), dispatcher);
    Tweak_Misc_Details_Adjunct(a, nullptr);  // caller can fill in

    Details* details = cast(Details*, a);  // now it's legitimate, can be cast

    assert(Details_Querier(details));  // must register querier
    return details;
}}


//
//  Register_Dispatcher: C
//
// There is querying functionality specific to each dispatcher (getting the
// RETURN or BODY, etc.)  This is provided by a DetailsQuerier, that is
// mapped to on a per-dispatcher basis.  e.g. the FUNC dispatcher knows how
// to tell you what its return value is.
//
// (Note that all natives use a common DetailsQuerier.)
//
void Register_Dispatcher(Dispatcher* dispatcher, DetailsQuerier* querier)
{
    if (Is_Flex_Full(g_dispatcher_table)) {
        require (
          Extend_Flex_If_Necessary_But_Dont_Change_Used(g_dispatcher_table, 8)
        );
    }

    Sink(DispatcherAndQuerier) d_and_q =
        &(Flex_Head_Dynamic(DispatcherAndQuerier, g_dispatcher_table)[
            g_dispatcher_table->content.dynamic.used++
        ]);

    d_and_q->dispatcher = dispatcher;
    d_and_q->querier = querier;
}


//
//  Details_Querier: C
//
// This just does a linear search through the registered dispatchers.  It
// is thus not particularly fast to find a DetailsQuerier for a Dispatcher
// that isn't a native, so it shouldn't be used in performance-sensitive
// spots (e.g. a Func_Dispatcher() or DEFINITIONAL-RETURN shouldn't call
// Details_Querier() to get its RETURN slot, rather use its internal knowledge
// to find the slot, this is for generalized usermode queries only).
//
DetailsQuerier* Details_Querier(Details *details) {
    if (Get_Details_Flag(details, RAW_NATIVE))
        return &Raw_Native_Details_Querier;

    Dispatcher* dispatcher = Details_Dispatcher(details);

    DispatcherAndQuerier* d_and_q = Flex_Head_Dynamic(
        DispatcherAndQuerier, g_dispatcher_table
    );
    Count used = g_dispatcher_table->content.dynamic.used;
    for (; used > 0; ++d_and_q, --used) {
        if (d_and_q->dispatcher == dispatcher)
            return d_and_q->querier;
    }
    crash ("Non-native Dispatcher used without calling Register_Dispatcher()");
}


//
//  /couple: native [
//
//  "Associate an ACTION! with OBJECT! to use for `.field` member references"
//
//      return: [action! frame!]
//      ^value [action! frame!]
//      coupling [<opt> object! frame!]
//  ]
//
DECLARE_NATIVE(COUPLE)
//
// !!! Should this require an :OVERRIDE if the action already has a non-null
// coupling in its cell?
{
    INCLUDE_PARAMS_OF_COUPLE;

    Copy_Cell(OUT, ARG(VALUE));

    Details* details = Ensure_Phase_Details(Frame_Phase(OUT));
    if (Not_Details_Flag(details, METHODIZED))
        return fail ("FRAME! is not methodized, cannot COUPLE it");

    Option(Stable*) coupling = ARG(COUPLING);

    if (not coupling)
        Tweak_Frame_Coupling(OUT, nullptr);
    else {
        assert(Is_Object(unwrap coupling) or Is_Frame(unwrap coupling));
        Tweak_Frame_Coupling(OUT, Cell_Varlist(unwrap coupling));
    }

    return OUT;
}


//
//  /uncouple: native [
//
//  "Disassociate an ACTION from OBJECT!"
//
//      return: [action! frame!]
//      ^action [action! frame!]
//  ]
//
DECLARE_NATIVE(UNCOUPLE)
{
    INCLUDE_PARAMS_OF_UNCOUPLE;

    Value* action_or_frame = ARG(ACTION);

    Tweak_Frame_Coupling(action_or_frame, UNCOUPLED);

    return COPY_TO_OUT(action_or_frame);
}
