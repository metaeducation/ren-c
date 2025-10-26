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


typedef enum {
    SPEC_MODE_DEFAULT,  // waiting, words seen will be arguments
    SPEC_MODE_PUSHED,  // argument pushed, information can be augmented
    SPEC_MODE_LOCAL,  // words are locals
    SPEC_MODE_WITH  // words are "extern"
} SpecMode;


static void Force_Adjunct(VarList* *adjunct_out) {
    if (*adjunct_out)
        return;

    *adjunct_out = Copy_Varlist_Shallow_Managed(
        Cell_Varlist(Root_Action_Adjunct)
    );
}


// This is an implementation helper for Make_Paramlist_Managed().
//
// It was broken out into its own separate routine so that the AUGMENT
// function could reuse the logic for function spec analysis.  It may not
// be broken out in a particularly elegant way, but it's a start.
//
// 1. Definitional RETURN slots must have their argument value fulfilled with
//    an ACTION! specific to the action called on *every instantiation*.
//    They are marked with special parameter classes to avoid needing to
//    separately do canon comparison of their symbols to find them.
//
//    Note: Since RETURN's typeset holds types that need to be checked at
//    the end of the function run, it is moved to a predictable location:
//    first slot of the paramlist.  Initially it was the last slot...but this
//    enables adding more arguments/refinements/locals in derived functions.
//
static Result(Zero) Push_Keys_And_Params_Core(
    VarList* *adjunct,
    Level* L,
    Flags flags,
    Option(SymId) returner  // e.g. SYM_RETURN or SYM_YIELD
){
    StackIndex base = L->baseline.stack_base;

    if (returner) {
        Init_Word(PUSH(), Canon_Symbol(unwrap returner));  // top of stack
        Init_Unreadable(PUSH());  // becomes parameter (explicitly or implicit)
    }

    SpecMode mode = SPEC_MODE_DEFAULT;

    Atom* eval = Level_Lifetime_Atom(L);
    Push_Level_Erase_Out_If_State_0(eval, L);

    if (*adjunct) {
        assert(flags & MKF_PARAMETER_SEEN);
        assert(Is_Stub_Varlist(*adjunct));
        Push_Lifeguard(*adjunct);  // need to guard (we do evals :-/)
    }
    else  // may push guard during enumeration, has to be after Level push
        assert(*adjunct == nullptr);

    for (; Not_Level_At_End(L); Fetch_Next_In_Feed(L->feed)) {

      loop_dont_fetch_next: ;

        const Element* item = At_Level(L);

  //=//// TOP-LEVEL SPEC TAGS LIKE <local>, <with> etc. ///////////////////=//

        bool strict = false;
        if (Is_Tag(item)) {
            flags |= MKF_PARAMETER_SEEN;  // don't look for description after
            if (0 == CT_Utf8(item, g_tag_with, strict)) {
                mode = SPEC_MODE_WITH;
                continue;
            }
            else if (0 == CT_Utf8(item, g_tag_local, strict)) {
                mode = SPEC_MODE_LOCAL;
                continue;
            }
            else
                return fail (item);
        }

  //=//// CHECK BINDING FOR <with> WORD!s /////////////////////////////////=//

    // The higher-level function generator that implemented <static> and <in>
    // was removed, in favor of BIND operations on the body as a more generic
    // answer that could apply in arbitrary situations.  But there were some
    // cases of <with> hanging around that were non-useless, though they were
    // only comments.
    //
    // 1. Enforce that they're at least WORD!s that are bound.

    if (mode == SPEC_MODE_WITH) {
        if (not Is_Word(item))
            return fail (
                "<with> must be followed by WORD!s in FUNCTION spec"
            );

        DECLARE_ATOM (dummy);  // don't care about the actual value [1]
        trap (
          Get_Any_Word_Maybe_Trash(
            dummy, item, Level_Binding(L)
        ));
        continue;
    }

  //=//// ALLOW <local> WITH PLAIN WORD, POSSIBLE GROUP! INITIALIZER //////=//

    // The higher-level function generator that implemented <local> default
    // was removed, but this means that it can actually be done efficiently
    // because the frame cells can be set with the value.

    if (mode == SPEC_MODE_LOCAL) {
        const Symbol* symbol;
        bool must_be_action;
        bool meta = false;
        Option(SingleHeart) singleheart;
        if (Is_Word(item) or (meta = Is_Meta_Form_Of(WORD, item))) {
            symbol = Word_Symbol(item);
            must_be_action = false;
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
            must_be_action = true;
        }
        else
            return fail (item);

        Init_Word(PUSH(), symbol);

        Fetch_Next_In_Feed(L->feed);

        if (Is_Level_At_End(L)) {  // default initializer for local
            Init_Unset_Due_To_End(PUSH());
            break;
        }

        if (not Is_Group(At_Level(L))) {
            Init_Unset_Due_To_End(PUSH());
            goto loop_dont_fetch_next;
        }

        if (Trampoline_With_Top_As_Root_Throws())  // run the group
            return fail (Error_No_Catch_For_Throw(L));

        if (not meta) {
            require (
              Value* decayed = Decay_If_Unstable(eval)
            );

            if (must_be_action and not Is_Action(decayed))
                return fail ("Assignment using /FOO must be an action");
        }

        Move_Cell(PUSH(), cast(Value*, eval));

        if (Is_Level_At_End(L))
            break;

        goto loop_dont_fetch_next;
    }

  //=//// TEXT! FOR FUNCTION DESCRIPTION OR PARAMETER NOTE ////////////////=//

    // 1. Consider `[<with> some-extern "description of that extern"]` to be
    //    purely commentary for the implementation (there's nowhere to put
    //    the information for <with> or <local>)

        if (Is_Text(item)) {
            if (not (flags & MKF_PARAMETER_SEEN)) {
                assert(mode != SPEC_MODE_PUSHED);  // none seen, none pushed!
                // no keys seen yet, act as overall description

                assert(not *adjunct);
                Force_Adjunct(adjunct);

                require (
                  Strand* strand = Copy_String_At(item)
                );
                Manage_Stub(strand);
                Freeze_Flex(strand);
                Init_Text(
                    Slot_Hack(
                        Varlist_Slot(*adjunct, STD_ACTION_ADJUNCT_DESCRIPTION)
                    ),
                    strand
                );
                Push_Lifeguard(*adjunct);
            }
            else {
                // act as description for current parameter
                assert(mode == SPEC_MODE_PUSHED);

                if (Parameter_Strand(TOP_ELEMENT))
                    return fail (Error_Bad_Func_Def_Raw(item));

                require (
                  Strand* strand = Copy_String_At(item)
                );
                Manage_Stub(strand);
                Freeze_Flex(strand);
                Set_Parameter_Strand(TOP_ELEMENT, strand);
            }

            continue;
        }

  //=//// BLOCK! OF TYPES TO MAKE TYPESET FROM (PLUS PARAMETER TAGS) //////=//

    // 1. We disallow `func [[integer!]]`, but also `<local> x [integer!]`,
    //    because locals are hidden from the interface, and hidden values
    //    (notably specialized-out values) use the `param` slot for the value,
    //    not type information.  So local has `~` antiform.
    //
    //    Even if you *could* give locals a type, it could only be given a
    //    meaning if it were used to check assignments during the function.
    //    There's currently no mechanism for doing that.
    //
    //    You can't say `<with> y [integer!]` either...though it might be nice
    //    to check the type of an imported value at time of calling.

        if (Is_Block(item)) {
            if (mode != SPEC_MODE_PUSHED)  // must come after parameter [1]
                return fail (Error_Bad_Func_Def_Raw(item));

            DECLARE_ELEMENT (param);  // we'll call GET, which uses the stack
            Copy_Cell(param, TOP_ELEMENT);

            if (Parameter_Spec(param))  // `func [x [integer!] [integer!]]`
                return fail (Error_Bad_Func_Def_Raw(item));  // too many blocks

            Context* derived = Derive_Binding(Level_Binding(L), item);
            Push_Lifeguard(param);
            Option(Error*) e = rescue (
                Set_Parameter_Spec(param, item, derived)
            );
            Drop_Lifeguard(param);

            if (e)
                return fail (unwrap e);

            Copy_Cell(TOP_ELEMENT, param);  // put modification back on stack

            Copy_Cell(TOP_ELEMENT, param);  // put modification back on stack

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
        bool local = false;
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

                // !!! There's currently the ability to shift to
                // parameter mode via [<local> x :foo y].  This is
                // used to create dummy variables in mid-spec.  Review.
                //
                mode = SPEC_MODE_DEFAULT;
                break; }

              case TRAILING_SPACE_AND(WORD):
                if (not returner)
                    return fail (
                        "SET-WORD in spec but no RETURN or YIELD in effect"
                    );
                if (not quoted and Word_Id(item) == unwrap returner) {
                    symbol = Word_Symbol(item);
                    pclass = PARAMCLASS_NORMAL;
                    is_returner = true;
                }
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

        if (mode == SPEC_MODE_LOCAL or mode == SPEC_MODE_WITH) {
            if (pclass != PARAMCLASS_NORMAL)
                return fail (Error_Bad_Func_Def_Raw(item));

            if (mode == SPEC_MODE_LOCAL)
                local = true;
        }

        if (
            returner
            and Symbol_Id(symbol) == unwrap returner
            and not is_returner
        ){
            if (SYM_RETURN == unwrap returner)
                return fail (
                    "Generator provides RETURN:, use LAMBDA if not desired"
                );
            assert(SYM_YIELD == unwrap returner);
            return fail (
                "Generator provides YIELD:, can't have YIELD parameter"
            );
        }

        // Because FUNC does not do any locals gathering by default, the main
        // purpose of tolerating <with> is for instructing it not to do the
        // definitional returns.  However, it also makes changing between
        // FUNC and FUNCTION more fluid.
        //
        // !!! If you write something like `func [x <with> x] [...]` that
        // should be sanity checked with an error...TBD.
        //
        if (mode == SPEC_MODE_WITH)
            continue;

        flags |= MKF_PARAMETER_SEEN;  // don't look for description after

        OnStack(Value*) param;
        if (
            returner
            and Symbol_Id(symbol) == unwrap returner
        ){
            assert(
                Word_Id(Data_Stack_At(Element, base + 1))
                == unwrap returner
            );
            param = Data_Stack_At(Value, base + 2);
            if (Is_Cell_Readable(param)) {
                assert(
                    LIFT_BYTE(param) == ONEQUOTE_NONQUASI_4
                    and Heart_Of(param) == TYPE_PARAMETER
                );
                if (SYM_RETURN == unwrap returner)
                    return fail ("Duplicate RETURN: in function spec");
                assert(SYM_YIELD == unwrap returner);
                return fail ("Duplicate YIELD: in function spec");
            }
        }
        else {  // Pushing description values for a new named element...
            Init_Word(PUSH(), symbol);  // duplicates caught when popping
            param = u_cast(Value*, PUSH());
        }

        // Non-annotated arguments allow all parameter types.

        if (local) {
            Init_Unset_Due_To_End(u_cast(Atom*, param));
            assert(mode == SPEC_MODE_LOCAL);
        }
        else if (refinement) {
            Init_Unconstrained_Parameter(
                param,
                FLAG_PARAMCLASS_BYTE(pclass)
                    | PARAMETER_FLAG_REFINEMENT  // must preserve if type block
                    | PARAMETER_FLAG_NULL_DEFINITELY_OK  // need if refinement
            );
            mode = SPEC_MODE_PUSHED;
        }
        else {
            Init_Unconstrained_Parameter(
                param,
                FLAG_PARAMCLASS_BYTE(pclass)
            );
            mode = SPEC_MODE_PUSHED;
        }
    }

    if (returner) {  // default RETURN: or YIELD: to unconstrained if not seen
        assert(
            Word_Id(Data_Stack_At(Element, base + 1))
            == unwrap returner
        );
        OnStack(Value*) param_1 = Data_Stack_At(Value, base + 2);
        if (Not_Cell_Readable(param_1)) {
            Init_Unconstrained_Parameter(  // return anything by default
                param_1,
                FLAG_PARAMCLASS_BYTE(PARAMCLASS_NORMAL)
            );
        }
        else
            assert(Is_Parameter(param_1));
        LIFT_BYTE(param_1) = ONEQUOTE_NONQUASI_4;  // quoted parameter
    }

    if (*adjunct)
        Drop_Lifeguard(*adjunct);

    return zero;
}


//
//  Push_Keys_And_Params: C
//
// Wrapper allowing `return fail` from Push_Keys_And_Params_Core() to properly
// balance the stack.
//
Result(Zero) Push_Keys_And_Params(
    VarList* *adjunct,
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

    Push_Keys_And_Params_Core(adjunct, L, flags, returner) except (Error* e) {
        Drop_Data_Stack_To(L->baseline.stack_base);
        Drop_Level(L);
        return fail (e);
    }

    Drop_Level_Unbalanced(L);  // pushed values on stack meant to be there
    return zero;
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
    Option(Phase*) prior,
    Option(VarList*) prior_coupling
){
    Count num_params = (TOP_INDEX - base) / 2;

    require (
      KeyList* keylist = u_downcast Make_Flex(
        STUB_MASK_KEYLIST | BASE_FLAG_MANAGED,
        num_params
    ));
    Set_Flex_Used(keylist, num_params);  // no terminator
    Tweak_Link_Keylist_Ancestor(keylist, keylist);  // chain

    Array* paramlist = Make_Array_Core(
        STUB_MASK_PARAMLIST,
        num_params + 1
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

    Value* rootvar = Flex_Head(Value, paramlist);
    if (prior)
        Init_Frame(rootvar, unwrap prior, ANONYMOUS, prior_coupling);
    else
        Init_Frame_Unchecked(
            rootvar,
            u_cast(Phase*, paramlist),
            ANONYMOUS,
            NONMETHOD
        );

    Value* param = 1 + rootvar;
    Key* key = Flex_Head(Key, keylist);

    StackIndex stackindex = base + 1;  // empty stack base would be 0, bad cell
    for (; stackindex <= TOP_INDEX; stackindex += 2) {
        const Symbol* symbol = Word_Symbol(Data_Stack_Cell_At(stackindex));
        OnStack(Element*) slot = Data_Stack_At(Element, stackindex + 1);

        assert(Not_Cell_Flag(slot, VAR_MARKED_HIDDEN));  // use NOTE_SEALED

        // "Sealed" parameters do not count in the binding.  See AUGMENT for
        // notes on why we do this (you can augment a function that has a
        // local called `x` with a new parameter called `x`, and that's legal.)
        //
        bool hidden;
        if (Get_Cell_Flag(slot, STACK_NOTE_SEALED)) {
            assert(Is_Specialized(cast(Param*, cast(Value*, slot))));

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
        Set_Cell_Flag(param, PROTECTED);
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
//     ["description" arg "notes" [type! type2! ...] /ref ...]
//
// !!! The spec language was not formalized in R3-Alpha.  Strings were left
// in and it was HELP's job (and any other clients) to make sense of it, e.g.:
//
//     [foo [type!] {doc string :-)}]
//     [foo {doc string :-/} [type!]]
//     [foo {doc string1 :-/} {doc string2 :-(} [type!]]
//
// Ren-C breaks this into two parts: one is the mechanical understanding of
// FUNC and LAMBDA for parameters in the evaluator.  Then it is the job
// of a generator to tag the resulting function with an adjunct object" with
// any descriptions.  As a proxy for the work of a usermode generator, this
// routine tries to fill in FUNCTION-META (see %sysobj.r) as well as to
// produce a paramlist suitable for the function.
//
// Note a "true local" (indicated by a set-word) is considered to be tacit
// approval of wanting a definitional return by the generator.  This helps
// because Red's model for specifying returns uses a SET-WORD!
//
//     func [return: [integer!] "returns an integer"]
//
// In Ren-C's case it just means you want a local called return, but the
// generator will be "initializing it with a definitional return" for you.
// You don't have to use it if you don't want to...and may overwrite the
// variable.
//
Result(ParamList*) Make_Paramlist_Managed(
    Sink(VarList*) adjunct,
    const Element* spec,
    Flags flags,  // flags may be modified to carry additional information
    Option(SymId) returner  // e.g. SYM_YIELD or SYM_RETURN
){
    StackIndex base = TOP_INDEX;

    // The process is broken up into phases so that the spec analysis code
    // can be reused in AUGMENT.
    //
    *adjunct = nullptr;

    Push_Keys_And_Params(adjunct, spec, flags, returner) except (Error* e) {
        return fail (e);
    }

    Option(Phase*) prior = nullptr;
    Option(VarList*) prior_coupling = nullptr;

    if (flags & MKF_DONT_POP_RETURN) {
        assert(returner);
        assert(TOP_INDEX - base >= 2);
        base += 2;
    }

    return Pop_Paramlist(base, prior, prior_coupling);
}


//
//  Pop_Unpopped_Return: C
//
// If you use MKF_DONT_POP_RETURN, the return won't be part of the paramlist
// but left on the stack.  Natives put this in the Details array.
//
void Pop_Unpopped_Return(Sink(Element) out, StackIndex base)
{
    assert(TOP_INDEX == base + 2);
    assert(
        Heart_Of(TOP) == TYPE_PARAMETER
        and LIFT_BYTE(TOP) == ONEQUOTE_NONQUASI_4
    );
    LIFT_BYTE(TOP) = NOQUOTE_2;
    Copy_Cell(out, TOP_ELEMENT);
    DROP();
    assert(Word_Id(TOP) == SYM_RETURN);
    DROP();

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
    const Value* exemplar,  // FRAME! "interface" is keyword in MSVC :-(
    Dispatcher* dispatcher,  // native C function called by Action_Executor()
    Option(Index) details_max  // 1-based max index desired for Phase_Details
){
    assert(0 == (flags & (~ (  // make sure no stray flags passed in
        BASE_FLAG_MANAGED
            | DETAILS_FLAG_CAN_DISPATCH_AS_INTRINSIC
            | DETAILS_FLAG_API_CONTINUATIONS_OK
            | DETAILS_FLAG_RAW_NATIVE
            | DETAILS_FLAG_OWNS_PARAMLIST
    ))));

    // "details" for an action is an array of cells which can be anything
    // the dispatcher understands it to be, by contract.
    //
    Array* a = Make_Array_Core(
        STUB_MASK_DETAILS | flags,  // don't add BASE_FLAG_MANAGED
        (opt details_max) + 1  // if max is 0, then only Phase_Archetype()
    );
    Set_Flex_Len(a, (opt details_max) + 1);

    assert(Heart_Of(exemplar) == TYPE_FRAME);
    assert(
        LIFT_BYTE(exemplar) == NOQUOTE_2
        or LIFT_BYTE(exemplar) == ANTIFORM_1  // allow action antiform
    );
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
//      action [action! frame!]
//      coupling [<opt> object! frame!]
//  ]
//
DECLARE_NATIVE(COUPLE)
//
// !!! Should this require an :OVERRIDE if the action already has a non-null
// coupling in its cell?
{
    INCLUDE_PARAMS_OF_COUPLE;

    Value* action_or_frame = ARG(ACTION);  // could also be a FRAME!
    Value* coupling = ARG(COUPLING);

    assert(Heart_Of(action_or_frame) == TYPE_FRAME);

    if (Is_Nulled(coupling))
        Tweak_Frame_Coupling(action_or_frame, nullptr);
    else {
        assert(Is_Object(coupling) or Is_Frame(coupling));
        Tweak_Frame_Coupling(action_or_frame, Cell_Varlist(coupling));
    }

    return COPY(action_or_frame);
}


//
//  uncouple: native [
//
//  "Disassociate an ACTION from OBJECT!"
//
//      return: [action!]
//      action [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(UNCOUPLE)
{
    INCLUDE_PARAMS_OF_UNCOUPLE;

    Value* action_or_frame = ARG(ACTION);  // could also be a FRAME!

    assert(Heart_Of(action_or_frame) == TYPE_FRAME);

    Tweak_Frame_Coupling(action_or_frame, UNCOUPLED);

    Actionify(Copy_Cell(OUT, action_or_frame));
    return UNSURPRISING(OUT);
}
