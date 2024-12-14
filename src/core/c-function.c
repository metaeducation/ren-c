//
//  File: %c-function.c
//  Summary: "support for functions, actions, and routines"
//  Section: core
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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


//
//  Make_Action_Words_Array: C
//
// Returns array of function words, unbound.
//
// 1. This used to go through multiple passes, in order to put the refinement
//    words after all the non-refinement ones.  It's not clear exactly what
//    the purpose of this routine is any longer, but it was being used by
//    UPARSE to detect if a parser had a `negated` parameter, which was
//    a refinement.  Try doing this unsorted for now, as a bare minimum for
//    making that work.
//
Source* Make_Action_Words_Array(Phase* act)
{
    StackIndex base = TOP_INDEX;

    const Key* key_tail;
    const Key* key = Phase_Keys(&key_tail, act);
    Param* param = Phase_Params_Head(act);
    for (; key != key_tail; ++key, ++param) {
        if (Is_Specialized(param))
            continue;
        possibly(Get_Parameter_Flag(param, REFINEMENT));  // sorted? [1]
        Init_Word(PUSH(), *key);
    }
    return Pop_Source_From_Stack(base);
}


enum Reb_Spec_Mode {
    SPEC_MODE_DEFAULT,  // waiting, words seen will be arguments
    SPEC_MODE_PUSHED,  // argument pushed, information can be augmented
    SPEC_MODE_LOCAL,  // words are locals
    SPEC_MODE_WITH  // words are "extern"
};


static void Force_Adjunct(VarList* *adjunct_out) {
    if (*adjunct_out)
        return;

    *adjunct_out = Copy_Varlist_Shallow_Managed(
        Cell_Varlist(Root_Action_Adjunct)
    );
}


//
//  Push_Keys_And_Holes_May_Fail: C
//
// This is an implementation routine for Make_Paramlist_Managed_May_Fail().
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
void Push_Keys_And_Holes_May_Fail(
    VarList* *adjunct,
    const Value* spec,
    Flags flags
){
    StackIndex base = TOP_INDEX;

    assert(Is_Block(spec));

    if (flags & MKF_RETURN) {
        Init_Word(PUSH(), CANON(RETURN));  // top of stack
        Init_Unreadable(PUSH());  // becomes parameter (explicitly or implicit)
    }

    if (flags & MKF_YIELD) {
        Init_Word(PUSH(), CANON(YIELD));
        Init_Nothing(PUSH());  // locals are initialized as nothing
    }

    enum Reb_Spec_Mode mode = SPEC_MODE_DEFAULT;

    DECLARE_ATOM (eval);
    Level* L = Make_Level_At(
        &Stepper_Executor, spec, LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
    );
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
            if (0 == CT_String(item, Root_With_Tag, strict)) {
                mode = SPEC_MODE_WITH;
                continue;
            }
            else if (0 == CT_String(item, Root_Local_Tag, strict)) {
                mode = SPEC_MODE_LOCAL;
                continue;
            }
            else
                fail (Error_Bad_Func_Def_Raw(item));
        }

  //=//// CHECK BINDING FOR <with> WORD!s /////////////////////////////////=//

    // The higher-level function generator that implemented <static> and <in>
    // was removed, in favor of BIND operations on the body as a more generic
    // answer that could apply in arbitrary situations.  But there were some
    // cases of <with> hanging around that were non-useless, though they were
    // only comments.
    //
    // Enforce that they're at least WORD!s that are bound.

    if (mode == SPEC_MODE_WITH) {
        if (not Is_Word(item))
            fail ("<with> must be followed by WORD!s in FUNCTION spec");

        if (not Lookup_Word(item, Cell_Binding(spec)))
            fail (Error_Not_Bound_Raw(item));

        continue;
    }

  //=//// ALLOW <local> WITH PLAIN WORD, POSSIBLE GROUP! INITIALIZER //////=//

    // The higher-level function generator that implemented <local> default
    // was removed, but this means that it can actually be done efficiently
    // because the frame cells can be set with the value.

    if (mode == SPEC_MODE_LOCAL) {
        const Symbol* symbol;
        bool must_be_action;
        if (Is_Word(item)) {
            symbol = Cell_Word_Symbol(item);
            must_be_action = false;
        }
        else if (
            Is_Path(item) and
            LEADING_BLANK_AND(WORD) == Try_Get_Sequence_Singleheart(item)
        ){
            symbol = Cell_Word_Symbol(item);
            must_be_action = true;
        }
        else
            fail (Error_Bad_Func_Def_Raw(item));

        Init_Word(PUSH(), symbol);

        Fetch_Next_In_Feed(L->feed);

        if (Is_Level_At_End(L)) {
            Init_Nothing(PUSH());  // default initializer for local
            break;
        }

        if (not Is_Group(At_Level(L))) {
            Init_Nothing(PUSH());
            goto loop_dont_fetch_next;
        }

        if (Trampoline_With_Top_As_Root_Throws())  // run the group
            fail (Error_No_Catch_For_Throw(L));

        Decay_If_Unstable(eval);

        if (must_be_action and not Is_Action(eval))
            fail ("Assignment using /FOO must be an action");

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

                String* string = Copy_String_At(item);
                Manage_Flex(string);
                Freeze_Flex(string);
                Init_Text(
                    Varlist_Slot(*adjunct, STD_ACTION_ADJUNCT_DESCRIPTION),
                    string
                );
                Push_Lifeguard(*adjunct);
            }
            else {
                // act as description for current parameter
                assert(mode == SPEC_MODE_PUSHED);

                if (Cell_Parameter_String(TOP))
                    fail (Error_Bad_Func_Def_Raw(item));

                String* string = Copy_String_At(item);
                Manage_Flex(string);
                Freeze_Flex(string);
                Set_Parameter_String(TOP, string);
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
                fail (Error_Bad_Func_Def_Raw(item));

            OnStack(Value*) param = TOP;

            if (Cell_Parameter_Spec(param))  // `func [x [integer!] [blank!]]`
                fail (Error_Bad_Func_Def_Raw(item));  // too many spec blocks

            Context* derived = Derive_Binding(Cell_List_Binding(spec), item);
            Set_Parameter_Spec(param, item, derived);

            continue;
        }

  //=//// ANY-WORD? PARAMETERS THEMSELVES /////////////////////////////////=//

        bool quoted = false;  // single quoting level used as signal in spec
        if (Cell_Num_Quotes(item) > 0) {
            if (Cell_Num_Quotes(item) > 1)
                fail (Error_Bad_Func_Def_Raw(item));
            quoted = true;
        }

        Heart heart = Cell_Heart(item);

        const Symbol* symbol = nullptr;  // avoids compiler warning
        ParamClass pclass = PARAMCLASS_0;  // error if not changed

        bool refinement = false;  // paths with blanks at head are refinements
        bool local = false;
        bool is_return = false;
        if (heart == REB_CHAIN or heart == REB_META_CHAIN) {
            switch (Try_Get_Sequence_Singleheart(item)) {
              case LEADING_BLANK_AND(WORD): {
                refinement = true;
                symbol = Cell_Refinement_Symbol(item);
                if (heart == REB_META_PATH) {
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

              case TRAILING_BLANK_AND(WORD):
                if (not quoted and Cell_Word_Id(item) == SYM_RETURN) {
                    symbol = Cell_Word_Symbol(item);
                    pclass = PARAMCLASS_NORMAL;
                    is_return = true;
                }
                break;

              default:
                break;
            }
        }
        else if (Is_The_Group(item)) {  // @(...) is PARAMCLASS_SOFT for now
            if (Cell_Series_Len_At(item) == 1) {
                const Element* word = Cell_List_Item_At(item);
                if (Is_Word(word)) {
                    pclass = PARAMCLASS_SOFT;
                    symbol = Cell_Word_Symbol(word);
                }
            }
        }
        else if (Any_Word_Kind(heart)) {
            symbol = Cell_Word_Symbol(item);

            if (heart == REB_THE_WORD) {  // output
                if (quoted)
                    fail ("Can't quote THE-WORD! parameters");
                pclass = PARAMCLASS_THE;
            }
            else {
                if (heart == REB_WORD) {
                    if (quoted)
                        pclass = PARAMCLASS_JUST;
                    else
                        pclass = PARAMCLASS_NORMAL;
                }
                else if (heart == REB_META_WORD) {
                    if (not quoted)
                        pclass = PARAMCLASS_META;
                }
            }
        }
        else
            fail (Error_Bad_Func_Def_Raw(item));

        if (pclass == PARAMCLASS_0)  // didn't match
            fail (Error_Bad_Func_Def_Raw(item));

        if (mode == SPEC_MODE_LOCAL or mode == SPEC_MODE_WITH) {
            if (pclass != PARAMCLASS_NORMAL)
                fail (Error_Bad_Func_Def_Raw(item));

            if (mode == SPEC_MODE_LOCAL)
                local = true;
        }

        if (
            (flags & MKF_RETURN)
            and Symbol_Id(symbol) == SYM_RETURN
            and not is_return
        ){
            fail ("Generator provides RETURN:, use LAMBDA if not desired");
        }

        if (
            (flags & MKF_YIELD)
            and Symbol_Id(symbol) == SYM_YIELD
        ){
            fail ("Generator provides YIELD, don't include in spec");
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
            symbol == CANON(RETURN)
            and (flags & MKF_RETURN)  // explicit return: specification
        ){
            assert(
                Cell_Word_Symbol(Data_Stack_At(Element, base + 1))
                == CANON(RETURN)
            );
            param = Data_Stack_At(Value, base + 2);
            if (Is_Cell_Readable(param)) {
                assert(Is_Hole(param));
                fail ("Duplicate RETURN: in function spec");
            }
        }
        else {  // Pushing description values for a new named element...
            Init_Word(PUSH(), symbol);  // duplicates caught when popping
            param = PUSH();
        }

        // Non-annotated arguments allow all parameter types.

        if (local) {
            Init_Nothing(param);
            assert(mode == SPEC_MODE_LOCAL);
        }
        else if (refinement) {
            Init_Unconstrained_Hole(
                param,
                FLAG_PARAMCLASS_BYTE(pclass)
                    | PARAMETER_FLAG_REFINEMENT  // must preserve if type block
                    | PARAMETER_FLAG_NULL_DEFINITELY_OK  // need if refinement
            );
            mode = SPEC_MODE_PUSHED;
        }
        else {
            Init_Unconstrained_Hole(
                param,
                FLAG_PARAMCLASS_BYTE(pclass)
            );
            mode = SPEC_MODE_PUSHED;
        }
    }

    if (flags & MKF_RETURN) {  // default RETURN: to unconstrained if not seen
        assert(
            Cell_Word_Symbol(Data_Stack_At(Element, base + 1)) == CANON(RETURN)
        );
        OnStack(Value*) param_1 = Data_Stack_At(Value, base + 2);
        if (Not_Cell_Readable(param_1)) {
            Init_Unconstrained_Hole(  // return anything by default
                param_1,
                FLAG_PARAMCLASS_BYTE(PARAMCLASS_NORMAL)
            );
        }
        else
            assert(Is_Hole(param_1));
        QUOTE_BYTE(param_1) = NOQUOTE_1;  // normal parameter
    }

    if (*adjunct)
        Drop_Lifeguard(*adjunct);

    Drop_Level_Unbalanced(L);
}


//
//  Pop_Paramlist_May_Fail: C
//
// Assuming the stack is formed in pairs of symbol WORD! for key and a
// parameter (possibly an antiform PARAMETER!, or specialized local), this
// produces a paramlist in a state suitable for Make_Dispatch_Details().
//
// It may not succeed because there could be duplicate parameters on the stack,
// and the checking via a binder is done as part of this popping process.
//
ParamList* Pop_Paramlist_May_Fail(
    StackIndex base,
    Option(Phase*) prior,
    Option(VarList*) prior_coupling
){
    Count num_params = (TOP_INDEX - base) / 2;

    KeyList* keylist = Make_Flex(
        FLEX_MASK_KEYLIST | NODE_FLAG_MANAGED,
        KeyList,
        num_params
    );
    Set_Flex_Used(keylist, num_params);  // no terminator
    LINK(Ancestor, keylist) = keylist;  // chain

    Array* paramlist = Make_Array_Core(
        FLEX_MASK_PARAMLIST,
        num_params + 1
    );
    Set_Flex_Len(paramlist, num_params + 1);

    // We want to check for duplicates and a Binder can be used for that
    // purpose--but fail() isn't allowed while binders are in effect.
    //
    // (This is why we wait until the parameter list gathering process
    // is over to do the duplicate checks--it can fail.)
    //
    DECLARE_BINDER (binder);
    Construct_Binder(binder);

    const Symbol* duplicate = nullptr;

    Value* rootvar = Flex_Head(Value, paramlist);
    if (prior)
        Init_Frame(rootvar, prior, ANONYMOUS, prior_coupling);
    else
        Init_Frame_Unchecked(rootvar, paramlist, ANONYMOUS, NONMETHOD);

    Value* param = 1 + rootvar;
    Key* key = Flex_Head(Key, keylist);

    StackIndex stackindex = base + 1;  // empty stack base would be 0, bad cell
    for (; stackindex <= TOP_INDEX; stackindex += 2) {
        const Symbol* symbol = Cell_Word_Symbol(Data_Stack_Cell_At(stackindex));
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
            CELL_MASK_COPY | CELL_FLAG_VAR_MARKED_HIDDEN
        );
        if (Is_Hole(param))
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

    Manage_Flex(paramlist);

    BONUS(KeyList, paramlist) = keylist;
    MISC(VarlistAdjunct, paramlist) = nullptr;
    node_LINK(NextVirtual, paramlist) = nullptr;

    // With all the values extracted from stack to array, restore stack pointer
    //
    Drop_Data_Stack_To(base);

    // Must remove binder indexes for all words, even if about to fail
    //
    Destruct_Binder(binder);

    if (duplicate) {
        DECLARE_ATOM (word);
        Init_Word(word, duplicate);
        fail (Error_Dup_Vars_Raw(word));
    }

    Assert_Flex_Term_If_Needed(paramlist);
    return cast(ParamList*, paramlist);
}


//
//  Make_Paramlist_Managed_May_Fail: C
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
// MAKE ACTION! for parameters in the evaluator.  Then it is the job
// of a generator to tag the resulting function with a "meta object" with any
// descriptions.  As a proxy for the work of a usermode generator, this
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
// variable.  But it won't be a void at the start.
//
ParamList* Make_Paramlist_Managed_May_Fail(
    Sink(VarList*) adjunct,
    const Element* spec,
    Flags flags  // flags may be modified to carry additional information
){
    StackIndex base = TOP_INDEX;

    // The process is broken up into phases so that the spec analysis code
    // can be reused in AUGMENT.
    //
    *adjunct = nullptr;
    Push_Keys_And_Holes_May_Fail(
        adjunct,
        spec,
        flags
    );

    Option(Phase*) prior = nullptr;
    Option(VarList*) prior_coupling = nullptr;

    ParamList* paramlist = Pop_Paramlist_May_Fail(
        base, prior, prior_coupling
    );

    return paramlist;
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
    ParamList* paramlist,
    Dispatcher* dispatcher,  // native C function called by Action_Executor()
    REBLEN details_capacity  // capacity of Phase_Details (including archetype)
){
    assert(0 == (flags & (~ DETAILS_MASK_PROXY)));  // ensure only legal flags
    assert(details_capacity >= 1);  // need archetype, maybe 1 (singular array)

    assert(Is_Node_Managed(paramlist));
    assert(CTX_TYPE(paramlist) == REB_FRAME);

    // "details" for an action is an array of cells which can be anything
    // the dispatcher understands it to be, by contract.  Terminate it
    // at the given length implicitly.
    //
    Array* details = Make_Array_Core(
        FLEX_MASK_DETAILS | NODE_FLAG_MANAGED | flags,
        details_capacity  // Note: may be just 1 (so non-dynamic!)
    );
    Set_Flex_Len(details, details_capacity);

    Cell* rootvar = Array_Head(details);
    Init_Frame(rootvar, paramlist, ANONYMOUS, NONMETHOD);
    Protect_Rootvar_If_Debug(rootvar);

    // Leave rest of the cells in the capacity uninitialized (caller fills in)

    Tweak_Details_Dispatcher(cast(Details*, details), dispatcher);
    MISC(DetailsAdjunct, details) = nullptr;  // caller can fill in

    Phase* act = cast(Phase*, details);  // now it's a legitimate Action

    // Precalculate cached function flags.  This involves finding the first
    // unspecialized argument which would be taken at a callsite, which can
    // be tricky to figure out with partial refinement specialization.  So
    // the work of doing that is factored into a routine (`PARAMETERS OF`
    // uses it as well).

    const Param* first = First_Unspecialized_Param(nullptr, act);
    if (first) {
        ParamClass pclass = Cell_ParamClass(first);
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
    Set_Flex_Flag(paramlist, FIXED_SIZE);
    Set_Flavor_Flag(VARLIST, paramlist, IMMUTABLE);

    return Phase_Details(act);
}


//
//  Get_Maybe_Fake_Action_Body: C
//
// !!! While the interface as far as the evaluator is concerned is satisfied
// with the OneAction ACTION!, the various dispatchers have different ideas
// of what "source" would be like.  There should be some mapping from the
// dispatchers to code to get the BODY OF an ACTION.  For the moment, just
// handle common kinds so the SOURCE command works adquately, revisit later.
//
void Get_Maybe_Fake_Action_Body(Sink(Element) out, const Value* action)
{
    Option(VarList*) coupling = Cell_Frame_Coupling(action);
    Phase* a = VAL_ACTION(action);

    if (Is_Stub_Varlist(a)) {  // specialization or similar
        Copy_Meta_Cell(out, action);
        QUOTE_BYTE(out) = NOQUOTE_1;
        return;
    }

    Details* details = cast(Details*, a);

    // A Hijacker *might* not need to splice itself in with a dispatcher.
    // But if it does, bypass it to get to the "real" action implementation.
    //
    // !!! Should the source inject messages like {This is a hijacking} at
    // the top of the returned body?
    //
    if (Details_Dispatcher(details) == &Hijacker_Dispatcher) {
        Get_Maybe_Fake_Action_Body(out, Phase_Archetype(details));
        return;
    }

    // !!! Should the coupling make a difference in the returned body?  It is
    // exposed programmatically via COUPLING OF.
    //
    UNUSED(coupling);

    if (
        Details_Dispatcher(details) == &Func_Dispatcher
        or Details_Dispatcher(details) == &Lambda_Unoptimized_Dispatcher
    ){
        // Interpreted code, the body is a block with some bindings relative
        // to the action.

        Cell* body = Array_At(details, IDX_DETAILS_1);

        // The PARAMLIST_HAS_RETURN tricks for definitional return make it
        // seem like a generator authored more code in the action's body...but
        // the code isn't *actually* there and an optimized internal trick is
        // used.  Fake the code if needed.

        Value* example;
        REBLEN real_body_index;
        if (Details_Dispatcher(details) == &Lambda_Dispatcher) {
            example = nullptr;
            real_body_index = 0;
            UNUSED(real_body_index);
        }
        else if (Details_Has_Return(details)) {
            example = Get_System(SYS_STANDARD, STD_FUNC_BODY);
            real_body_index = 4;
        }
        else {
            example = NULL;
            real_body_index = 0; // avoid compiler warning
            UNUSED(real_body_index);
        }

        const Array* maybe_fake_body;
        if (example == nullptr) {
            maybe_fake_body = Cell_Array(body);
        }
        else {
            // See %sysobj.r for STANDARD/FUNC-BODY
            //
            Array* fake = Copy_Array_Shallow_Flags(
                FLEX_MASK_MANAGED_SOURCE,
                Cell_Array(example)
            );

            // Index 5 (or 4 in zero-based C) should be #BODY, a "real" body.
            // To give it the appearance of executing code in place, we use
            // a GROUP!.

            Element* slot = Array_At(fake, real_body_index);  // #BODY
            assert(Is_Issue(slot));

            // Note: clears VAL_FLAG_LINE
            //
            Reset_Cell_Header_Noquote(TRACK(slot), CELL_MASK_GROUP);
            Tweak_Cell_Node1(slot, Cell_Array(body));
            VAL_INDEX_RAW(slot) = 0;
            BINDING(slot) = a;  // relative binding

            maybe_fake_body = fake;
        }

        // Cannot give user a relative value back, so make the relative
        // body specific to a fabricated expired frame.  See #2221

        Reset_Cell_Header_Noquote(TRACK(out), CELL_MASK_BLOCK);
        Tweak_Cell_Node1(out, maybe_fake_body);
        VAL_INDEX_RAW(out) = 0;

        return;
    }

    Init_Blank(out); // natives, ffi routines, etc.
    return;
}


//
//  DECLARE_GENERICS: C
//
// This handler is used to fail for a type which cannot handle actions.
//
// !!! Currently all types have a DECLARE_GENERICS() handler for either themselves or
// their class.  But having a handler that could be "swapped in" from a
// default failing case is an idea that could be used as an interim step
// to allow something like REB_GOB to fail by default, but have the failing
// type handler swapped out by an extension.
//
DECLARE_GENERICS(Fail)
{
    UNUSED(verb);

    return RAISE("Datatype does not have a dispatcher registered.");
}


//
//  /couple: native [
//
//  "Associate an ACTION! with OBJECT! to use for `.field` member references"
//
//      return: [action? frame!]
//      action [action? frame!]
//      coupling [~null~ object! frame!]
//  ]
//
DECLARE_NATIVE(couple)
//
// !!! Should this require an :OVERRIDE if the action already has a non-null
// coupling in its cell?
{
    INCLUDE_PARAMS_OF_COUPLE;

    Value* action_or_frame = ARG(action);  // could also be a FRAME!
    Value* coupling = ARG(coupling);

    assert(Cell_Heart(action_or_frame) == REB_FRAME);

    if (Is_Nulled(coupling))
        Tweak_Cell_Frame_Coupling(action_or_frame, nullptr);
    else {
        assert(Is_Object(coupling) or Is_Frame(coupling));
        Tweak_Cell_Frame_Coupling(action_or_frame, Cell_Varlist(coupling));
    }

    return COPY(action_or_frame);
}


//
//  /uncouple: native [
//
//  "Disassociate an ACTION from OBJECT!"
//
//      return: [action? frame!]
//      action [action? frame!]
//  ]
//
DECLARE_NATIVE(uncouple)
{
    INCLUDE_PARAMS_OF_UNCOUPLE;

    Value* action_or_frame = ARG(action);  // could also be a FRAME!

    assert(Cell_Heart(action_or_frame) == REB_FRAME);

    Tweak_Cell_Frame_Coupling(action_or_frame, UNCOUPLED);

    return COPY(action_or_frame);
}
