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
    Push_Level_Erase_Out_If_State_0(SPARE, sub);

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
            initializer = SYM_GHOST;
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
                singleheart == LEADING_SPACE_AND(WORD)
                /* or (meta = (singleheart == LEADING_SPACE_AND(META_WORD))) */
            )
        ){
            symbol = Word_Symbol(item);
            initializer = SYM_GHOST;  // null is *bad* for "optional actions"
            must_be_action = true;
        }
        else
            return fail (item);

        Init_Word(PUSH(), symbol);

        Fetch_Next_In_Feed(sub->feed);

        if (
            Is_Level_At_End(sub)
            or not Is_Group(At_Level(sub))
        ) {
            switch (initializer) {
              case SYM_NULL: Init_Nulled(PUSH()); break;
              case SYM_GHOST: Init_Ghost(PUSH()); break;
              case SYM_TRIPWIRE: Init_Tripwire(PUSH()); break;
              default: assert(false);
            }
            continue;
        }

        if (Trampoline_With_Top_As_Root_Throws()) {  // run the group
            Drop_Level_Unbalanced(sub);
            return fail (Error_No_Catch_For_Throw(sub));
        }

        if (meta) {
            if (Is_Error(OUT))  // don't want to quietly store errors
                return fail (Cell_Error(OUT));

            Move_Value(PUSH(), SPARE);
        }
        else {
            require (
              Stable* decayed = Decay_If_Unstable(SPARE)
            );

            if (must_be_action and not Is_Action(decayed))
                return fail ("Assignment using /FOO must be an action");

            Move_Cell(PUSH(), decayed);
        }
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

        if (returner_id)
            Init_Word(PUSH(), Canon_Symbol(unwrap returner_id));
        else
            Init_Word(PUSH(), Canon_Symbol(SYM_DUMMY1));

        Init_Unconstrained_Parameter(
            PUSH(),
            FLAG_PARAMCLASS_BYTE(PARAMCLASS_META)
        );
        returner_index = TOP_INDEX;
    }

} do_more_stuff: {

    Value* eval = Level_Lifetime_Value(L);
    Push_Level_Erase_Out_If_State_0(eval, L);

    for (
        ;
        Not_Level_At_End(L);
        Fetch_Next_In_Feed(L->feed), augment_initial_entry = false
    ){
        const Element* item = At_Level(L);

  //=//// TOP-LEVEL SPEC TAGS (only <.> at the moment) ////////////////////=//

        bool strict = false;
        if (Is_Tag(item)) {
            if (augment_initial_entry) {
                return fail (
                    "Augmentation cannot add methodization via <.>"
                );
            }

            if (0 == CT_Utf8(item, g_tag_dot_1, strict)) {
                if (not Is_Quasar(methodization))
                    return fail ("Duplicate <.> in spec");

                Init_Unconstrained_Parameter(
                    methodization,
                    FLAG_PARAMCLASS_BYTE(PARAMCLASS_NORMAL)
                );
                continue;
            }
            else
                return fail (item);
        }

  //=//// TEXT! FOR FUNCTION DESCRIPTION OR PARAMETER NOTE ////////////////=//

        if (Is_Text(item)) {
            if (augment_initial_entry) {
                return fail (
                    "Function description not allowed in AUGMENT spec"
                );
            }

            if (not Is_Parameter(TOP_ELEMENT))
                return fail (
                    "Text strings must describe parameters, not locals"
                );

            require (
                Strand* strand = Copy_String_At(item)
            );
            Manage_Stub(strand);
            Freeze_Flex(strand);
            Set_Parameter_Strand(TOP_ELEMENT, strand);
            continue;
        }

  //=//// QUASAR (~) TO SAY YOU WANT AUTO-NAMED TRASH! ////////////////////=//

        if (Is_Quasar(item)) {
            if (augment_initial_entry)
                return fail (
                    "Function return indicator not allowed in AUGMENT spec"
                );

            if (TOP_INDEX != returner_index)
                return fail (
                    "Quasar (~) must be used to indicate function return spec"
                );

            if (Parameter_Spec(TOP_ELEMENT))  // `func [return: [integer!] ~]`
                return fail (Error_Bad_Func_Def_Raw(item));

            const Strand* notes = opt Parameter_Strand(TOP_ELEMENT);
            Copy_Cell(TOP_ELEMENT, g_auto_trash_param);
            Set_Parameter_Strand(TOP_ELEMENT, notes);

            continue;
        }

  //=//// BLOCK! OF TYPES TO MAKE TYPESET FROM (PLUS PARAMETER TAGS) //////=//

        if (Is_Block(item)) {
            if (augment_initial_entry) {
                return fail (
                    "Function return spec block not allowed in AUGMENT spec"
                );
            }

            if (Parameter_Spec(TOP_ELEMENT))  // `func [x [word!] [word!]]`
                return fail (Error_Bad_Func_Def_Raw(item));  // too many blocks

            Context* derived = Derive_Binding(Level_Binding(L), item);
            trap (
                Set_Spec_Of_Parameter_In_Top(L, item, derived)
            );

            continue;
        }

  //=//// LOCALS IN FENCE! (OBJECT CREATION DIALECT) //////////////////////=//

        if (Is_Fence(item)) {
            Element* spare = Derelativize(SPARE, item, Level_Binding(L));
            trap (
              Push_Keys_And_Params_For_Fence(L, spare)
            );
            continue;
        }

  //=//// ANY-WORD? PARAMETERS THEMSELVES /////////////////////////////////=//

        bool quoted = false;  // single quoting level used as signal in spec
        if (Quotes_Of(item) > 0) {
            if (Quotes_Of(item) > 1)
                return fail (Error_Bad_Func_Def_Raw(item));
            quoted = true;
        }

        Option(Type) type = Type_Of_Unquoted(item);
        if (not type)
            return fail (
                "Extension types not supported in function spec"
            );

        const Symbol* symbol = nullptr;  // avoids compiler warning
        ParamClass pclass = PARAMCLASS_0;  // error if not changed

        bool refinement = false;  // paths with blanks at head are refinements
        bool is_returner = false;
        if (type == TYPE_CHAIN) {
            switch (opt Try_Get_Sequence_Singleheart(item)) {
              case LEADING_SPACE_AND(WORD): {
                refinement = true;
                symbol = Cell_Refinement_Symbol(item);
                if ((type == TYPE_METAFORM) and Heart_Of(item) == TYPE_WORD) {
                    if (not quoted)
                        pclass = PARAMCLASS_META;
                }
                else {
                    if (quoted)
                        pclass = PARAMCLASS_JUST;
                    else
                        pclass = PARAMCLASS_NORMAL;
                }
                break; }

              case TRAILING_SPACE_AND(BLOCK): {
                Element* spare = Copy_Cell(SPARE, item);
                trap (
                  Unsingleheart_Sequence(spare)
                );
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
                is_returner = true;
                break; }

              case TRAILING_SPACE_AND(WORD):
                if (
                    quoted
                    or not returner_id
                    or Word_Id(item) != unwrap returner_id
                ){
                    return fail (
                        "SET-WORD in spec must match RETURN:/YIELD: name"
                    );
                }
                symbol = Word_Symbol(item);
                pclass = PARAMCLASS_META;
                is_returner = true;
                break;

              default:
                break;
            }
        }
        else if (Is_Pinned_Form_Of(GROUP, item)) {  // @(...) PARAMCLASS_SOFT
            if (Series_Len_At(item) == 1) {
                const Element* word = List_Item_At(item);
                if (Is_Word(word)) {
                    pclass = PARAMCLASS_SOFT;
                    symbol = Word_Symbol(word);
                }
            }
        }
        else if (Heart_Of(item) == TYPE_WORD) {
            symbol = Word_Symbol(item);

            if (Is_Pinned_Form_Of(WORD, item)) {  // output
                if (quoted)
                    return fail ("Can't quote @WORD! parameters");
                pclass = PARAMCLASS_THE;
            }
            else if (Is_Meta_Form_Of(WORD, item)) {
                if (not quoted)
                    pclass = PARAMCLASS_META;
            }
            else if (type == TYPE_WORD) {
                if (quoted)
                    pclass = PARAMCLASS_JUST;
                else
                    pclass = PARAMCLASS_NORMAL;
            }
        }
        else
            return fail (Error_Bad_Func_Def_Raw(item));

        if (pclass == PARAMCLASS_0)  // didn't match
            return fail (Error_Bad_Func_Def_Raw(item));

        if (
            returner_id
            and Symbol_Id(symbol) == unwrap returner_id
            and not is_returner
        ){
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
            continue;
        }

        // Pushing description values for a new named element...

        Init_Word(PUSH(), symbol);  // duplicates caught when popping

        if (refinement) {
            Init_Unconstrained_Parameter(
                PUSH(),
                FLAG_PARAMCLASS_BYTE(pclass)
                    | PARAMETER_FLAG_REFINEMENT  // must preserve if type block
                    | PARAMETER_FLAG_NULL_DEFINITELY_OK  // need if refinement
            );
        }
        else {
            Init_Unconstrained_Parameter(
                PUSH(),
                FLAG_PARAMCLASS_BYTE(pclass)
            );
        }

        // Non-annotated arguments allow all parameter types.
    }

    if (returner_index)  // plain param would gather arg, trick is to quote it
        Quotify_Parameter_Local(Data_Stack_At(Element, returner_index));

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
    Option(VarList*) prior_coupling
){
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
    DECLARE_BINDER (binder);
    Construct_Binder(binder);

    const Symbol* duplicate = nullptr;

    Stable* rootvar = Flex_Head(Stable, paramlist);
    if (prior)
        Init_Frame(rootvar, unwrap prior, ANONYMOUS, prior_coupling);
    else
        Init_Frame_Unchecked(
            rootvar,
            u_cast(Phase*, paramlist),
            ANONYMOUS,
            UNCOUPLED
        );

    Stable* param = 1 + rootvar;
    Key* key = Flex_Head(Key, keylist);

    if (methodization) {  // put dot first if applicable (find it fastest...)
        *key = CANON(DOT_1);
        Copy_Cell(param, unwrap methodization);
        assert(Is_Parameter(param));
        Quotify_Parameter_Local(Known_Element(param));
        Set_Cell_Flag(param, PARAM_NOTE_TYPECHECKED);
        ++key;
        ++param;

        Set_Flavor_Flag(VARLIST, paramlist, METHODIZED);
    }

    StackIndex stackindex = base + 1;  // empty stack base would be 0, bad cell
    for (; stackindex <= TOP_INDEX; stackindex += 2) {
        const Symbol* symbol = Word_Symbol(Data_Stack_Cell_At(stackindex));
        OnStack(Element) slot = Data_Stack_At(Element, stackindex + 1);

        assert(Not_Cell_Flag(slot, VAR_MARKED_HIDDEN));  // use NOTE_SEALED

        // "Sealed" parameters do not count in the binding.  See AUGMENT for
        // notes on why we do this (you can augment a function that has a
        // local called `x` with a new parameter called `x`, and that's legal.)
        //
        bool hidden;
        if (Get_Cell_Flag(slot, STACK_NOTE_SEALED)) {
            assert(Is_Specialized(cast(Param*, cast(Stable*, slot))));

            // !!! This flag was being set on an uninitialized param, with the
            // remark "survives copy over".  But the copy puts the flag on
            // regardless below.  Was this specific to RETURNs?
            //
            hidden = true;
        }
        else {
            if (not Try_Add_Binder_Index(binder, symbol, 1020))
                duplicate = symbol;

            hidden = false;
        }

        *key = symbol;

        Copy_Cell_Core(
            param,
            slot,
            CELL_MASK_COPY
                | CELL_FLAG_VAR_MARKED_HIDDEN
        );
        if (Is_Parameter(param))
            Set_Parameter_Flag(param, FINAL_TYPECHECK);
        else
            Set_Cell_Flag(param, PARAM_NOTE_TYPECHECKED);  // locals "checked"

        if (hidden)
            Set_Cell_Flag(param, VAR_MARKED_HIDDEN);

      #if DEBUG_PROTECT_PARAM_CELLS
        Protect_Cell(param);
      #endif

        ++key;
        ++param;
    }

    assert(param == Array_Tail(paramlist));

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
}


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
    Option(SymId) returner  // e.g. SYM_YIELD, SYM_RETURN, or SYM_DUMMY1 ([]:)
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
        prior_coupling
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

    Copy_Cell(out, TOP_ELEMENT);
    Unquotify_Parameter_Local(out);;
    DROP();

    assert(Word_Id(TOP) == SYM_RETURN or Word_Id(TOP) == SYM_DUMMY1);
    DROP();

    assert(Is_Quasar(TOP_ELEMENT) or Is_Parameter(TOP_ELEMENT));
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
    const Stable* exemplar,  // FRAME! "interface" is keyword in MSVC :-(
    Dispatcher* dispatcher,  // native C function called by Action_Executor()
    Option(Ordinal) details_max  // 1-based max index desired for Phase_Details
){
    assert(Heart_Of(exemplar) == TYPE_FRAME);
    assert(
        LIFT_BYTE(exemplar) == NOQUOTE_2
        or LIFT_BYTE(exemplar) == ANTIFORM_1  // allow action antiform
    );

    assert(0 == (flags & (~ (  // make sure no stray flags passed in
        BASE_FLAG_MANAGED
            | DETAILS_FLAG_CAN_DISPATCH_AS_INTRINSIC
            | DETAILS_FLAG_API_CONTINUATIONS_OK
            | DETAILS_FLAG_RAW_NATIVE
            | DETAILS_FLAG_OWNS_PARAMLIST
            | DETAILS_FLAG_METHODIZED  // inherit from exemplar
    ))));

    if (Get_Flavor_Flag(VARLIST, Cell_Varlist(exemplar), METHODIZED))
        flags |= DETAILS_FLAG_METHODIZED;

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
    LIFT_BYTE(rootvar) = NOQUOTE_2;  // canonize action antiforms to FRAME!
    Protect_Rootvar_If_Debug(rootvar);

    // Leave rest of the cells in the capacity uninitialized (caller fills in)

    Tweak_Details_Dispatcher(cast(Details*, a), dispatcher);
    Tweak_Misc_Details_Adjunct(a, nullptr);  // caller can fill in

    Details* details = cast(Details*, a);  // now it's legitimate, can be cast

    // Precalculate cached function flags.  This involves finding the first
    // unspecialized argument which would be taken at a callsite, which can
    // be tricky to figure out with partial refinement specialization.  So
    // the work of doing that is factored into a routine (`PARAMETERS OF`
    // uses it as well).  !!! Wrong place for this!

    ParamList* paramlist = Phase_Paramlist(details);
    const Param* first = First_Unspecialized_Param(nullptr, details);
    if (first) {
        ParamClass pclass = Parameter_Class(first);
        switch (pclass) {
          case PARAMCLASS_NORMAL:
          case PARAMCLASS_META:
            break;

          case PARAMCLASS_SOFT:
          case PARAMCLASS_JUST:
          case PARAMCLASS_THE:
            Set_Flavor_Flag(VARLIST, paramlist, PARAMLIST_LITERAL_FIRST);
            break;

          default:
            assert(false);
        }
    }

    // The exemplar needs to be frozen, it can't change after this point.
    // You can't change the types or parameter conventions of an existing
    // action...you have to make a new variation.  Note that the exemplar
    // can be exposed by AS FRAME! of this action...
    //
    Set_Flex_Flag(Varlist_Array(paramlist), FIXED_SIZE);
    Set_Flavor_Flag(VARLIST, paramlist, IMMUTABLE);

    assert(Details_Querier(details));  // must register querier
    return details;
}


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
//  couple: native [
//
//  "Associate an ACTION! with OBJECT! to use for `.field` member references"
//
//      return: [action! frame!]
//      frame [action! frame!]
//      coupling [<opt> object! frame!]
//  ]
//
DECLARE_NATIVE(COUPLE)
//
// !!! Should this require an :OVERRIDE if the action already has a non-null
// coupling in its cell?
{
    INCLUDE_PARAMS_OF_COUPLE;

    Stable* action_or_frame = ARG(FRAME);  // could also be a ACTION!

    Details* details = Phase_Details(Frame_Phase(action_or_frame));
    if (Not_Details_Flag(details, METHODIZED))
        return fail ("FRAME! is not methodized, cannot COUPLE it");

    Option(Stable*) coupling = ARG(COUPLING);

    if (not coupling)
        Tweak_Frame_Coupling(action_or_frame, nullptr);
    else {
        assert(Is_Object(unwrap coupling) or Is_Frame(unwrap coupling));
        Tweak_Frame_Coupling(action_or_frame, Cell_Varlist(unwrap coupling));
    }

    return COPY(action_or_frame);
}


//
//  uncouple: native [
//
//  "Disassociate an ACTION from OBJECT!"
//
//      return: [~[action!]~]
//      action [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(UNCOUPLE)
{
    INCLUDE_PARAMS_OF_UNCOUPLE;

    Stable* action_or_frame = ARG(ACTION);  // could also be a FRAME!

    assert(Heart_Of(action_or_frame) == TYPE_FRAME);

    Tweak_Frame_Coupling(action_or_frame, UNCOUPLED);

    Actionify(Copy_Cell(OUT, action_or_frame));
    return Packify_Action(OUT);
}
