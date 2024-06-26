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


struct Params_Of_State {
    bool just_words;
};

// Reconstitute parameter back into a full value, e.g. REB_P_REFINEMENT
// becomes `/spelling`.
//
// !!! Review why caller isn't filtering locals.
//
static bool Params_Of_Hook(
    const Key* key,
    const Param* param,
    Flags flags,
    void *opaque
){
    struct Params_Of_State *s = cast(struct Params_Of_State*, opaque);

    ParamClass pclass = Cell_ParamClass(param);
    if (pclass == PARAMCLASS_OUTPUT)
        return true;  // use `outputs of` instead of `parameters of` to get

    Init_Word(PUSH(), KEY_SYMBOL(key));

    if (not s->just_words) {
        if (
            not (flags & PHF_UNREFINED)
            and Get_Parameter_Flag(param, REFINEMENT)
        ){
            Refinify(TOP);
        }

        switch (Cell_ParamClass(param)) {
          case PARAMCLASS_RETURN:
          case PARAMCLASS_NORMAL:
            break;

          case PARAMCLASS_META:
            Metafy(TOP);
            break;

          case PARAMCLASS_SOFT:
            Getify(TOP);
            break;

          case PARAMCLASS_MEDIUM:
            Quotify(Getify(TOP), 1);
            break;

          case PARAMCLASS_HARD:
            Quotify(TOP, 1);
            break;

          default:
            assert(false);
            DEAD_END;
        }
    }

    return true;
}

//
//  Make_Action_Parameters_Arr: C
//
// Returns array of function words, unbound.
//
Array* Make_Action_Parameters_Arr(Action* act, bool just_words)
{
    struct Params_Of_State s;
    s.just_words = just_words;

    StackIndex base = TOP_INDEX;
    For_Each_Unspecialized_Param(act, &Params_Of_Hook, &s);
    return Pop_Stack_Values(base);
}



static bool Outputs_Of_Hook(
    const Key* key,
    const Param* param,
    Flags flags,
    void *opaque
){
    UNUSED(opaque);
    UNUSED(flags);
    if (Cell_ParamClass(param) == PARAMCLASS_OUTPUT)
        Init_Word(PUSH(), KEY_SYMBOL(key));
    return true;
}

//
//  Make_Action_Outputs_Arr: C
//
// Returns array of function words, unbound.
//
Array* Make_Action_Outputs_Arr(Action* act)
{
    StackIndex base = TOP_INDEX;
    For_Each_Unspecialized_Param(act, &Outputs_Of_Hook, nullptr);
    return Pop_Stack_Values(base);
}


enum Reb_Spec_Mode {
    SPEC_MODE_DEFAULT,  // waiting, words seen will be arguments
    SPEC_MODE_PUSHED,  // argument pushed, information can be augmented
    SPEC_MODE_LOCAL,  // words are locals
    SPEC_MODE_WITH  // words are "extern"
};


static void Ensure_Adjunct(Context* *adjunct_out) {
    if (*adjunct_out)
        return;

    *adjunct_out = Copy_Context_Shallow_Managed(
        VAL_CONTEXT(Root_Action_Adjunct)
    );
}


//
//  Push_Keys_And_Parameters_May_Fail: C
//
// This is an implementation routine for Make_Paramlist_Managed_May_Fail().
// It was broken out into its own separate routine so that the AUGMENT
// function could reuse the logic for function spec analysis.  It may not
// be broken out in a particularly elegant way, but it's a start.
//
void Push_Keys_And_Parameters_May_Fail(
    Context* *adjunct_out,
    const Value* spec,
    Flags *flags,
    StackIndex *return_stackindex
){
    assert(Is_Block(spec));

    enum Reb_Spec_Mode mode = SPEC_MODE_DEFAULT;

    const Element* tail;
    const Element* item = Cell_Array_At(&tail, spec);

    for (; item != tail; ++item) {

  //=//// TOP-LEVEL SPEC TAGS LIKE <local>, <with> etc. ///////////////////=//

        bool strict = false;
        if (Is_Tag(item)) {
            *flags |= MKF_PARAMETER_SEEN;  // don't look for description after
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

  //=//// TEXT! FOR FUNCTION DESCRIPTION OR PARAMETER NOTE ////////////////=//

    // 1. Consider `[<with> some-extern "description of that extern"]` to be
    //    purely commentary for the implementation (there's nowhere to put
    //    the information for <with> or <local>)

        if (Is_Text(item)) {
            if (mode == SPEC_MODE_LOCAL or mode == SPEC_MODE_WITH)
                continue;  // treat as a comment [1]

            if (not (*flags & MKF_PARAMETER_SEEN)) {
                assert(mode != SPEC_MODE_PUSHED);  // none seen, none pushed!
                // no keys seen yet, act as overall description

                Ensure_Adjunct(adjunct_out);

                String* string = Copy_String_At(item);
                Manage_Series(string);
                Freeze_Series(string);
                Init_Text(
                    CTX_VAR(*adjunct_out, STD_ACTION_ADJUNCT_DESCRIPTION),
                    string
                );
            }
            else {
                // act as description for current parameter
                assert(mode == SPEC_MODE_PUSHED);

                if (Cell_Parameter_String(TOP))
                    fail (Error_Bad_Func_Def_Raw(item));

                String* string = Copy_String_At(item);
                Manage_Series(string);
                Freeze_Series(string);
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

            StackValue(*) param = TOP;

            if (Cell_Parameter_Spec(param))  // `func [x [integer!] [blank!]]`
                fail (Error_Bad_Func_Def_Raw(item));  // too many spec blocks

            Specifier* derived = Derive_Specifier(Cell_Specifier(spec), item);
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
        if (Any_Path_Kind(heart)) {
            if (not IS_REFINEMENT_CELL(item))
                fail (Error_Bad_Func_Def_Raw(item));

            refinement = true;

            // !!! There's currently the ability to shift back into parameter
            // mode via [<local> x /foo y].  This is used to create dummy
            // variables in mid-spec.  Review.
            //
            mode = SPEC_MODE_DEFAULT;

            symbol = VAL_REFINEMENT_SYMBOL(item);

            if (heart == REB_GET_PATH) {
                if (quoted)
                    pclass = PARAMCLASS_MEDIUM;
                else
                    pclass = PARAMCLASS_SOFT;
            }
            else if (heart == REB_PATH) {
                if (quoted)
                    pclass = PARAMCLASS_HARD;
                else
                    pclass = PARAMCLASS_NORMAL;
            }
            else if (heart == REB_META_PATH) {
                pclass = PARAMCLASS_META;
            }
        }
        else if (Any_Tuple_Kind(heart)) {
            //
            // !!! Tuples are theorized as a way to "name parameters out of
            // the way" so there can be an interface name, but then a local
            // name...so that something like /ALL can be named out of the
            // way without disrupting use of ALL:
            //
            // https://forum.rebol.info/t/1793
            //
            fail ("TUPLE! behavior in func spec not defined at present");
        }
        else if (Any_Word_Kind(heart)) {
            symbol = Cell_Word_Symbol(item);

            if (heart == REB_SET_WORD) {
                if (Cell_Word_Id(item) == SYM_RETURN and not quoted) {
                    pclass = PARAMCLASS_RETURN;
                }
            }
            else if (heart == REB_THE_WORD) {  // output
                if (not quoted) {
                    if (not (*flags & MKF_RETURN)) {
                        fail (
                            "Function generator does not provide multi-RETURN"
                        );
                    }

                    pclass = PARAMCLASS_OUTPUT;
                }
            }
            else {
                if (heart == REB_GET_WORD) {
                    if (quoted)
                        pclass = PARAMCLASS_MEDIUM;
                    else
                        pclass = PARAMCLASS_SOFT;
                }
                else if (heart == REB_WORD) {
                    if (quoted)
                        pclass = PARAMCLASS_HARD;
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
            (*flags & MKF_RETURN)
            and Symbol_Id(symbol) == SYM_RETURN
            and pclass != PARAMCLASS_RETURN
        ){
            fail ("Generator provides RETURN:, use LAMBDA if not desired");
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

        *flags |= MKF_PARAMETER_SEEN;  // don't look for description after

        // Pushing description values for a new named element...

        Init_Word(PUSH(), symbol);

        if (symbol == Canon(RETURN)) {
            if (*return_stackindex != 0) {
                DECLARE_ATOM (word);
                Init_Word(word, symbol);
                fail (Error_Dup_Vars_Raw(word));  // most dup checks are later
            }
            if (*flags & MKF_RETURN) {
                assert(pclass == PARAMCLASS_RETURN);
                *return_stackindex = TOP_INDEX;  // RETURN: explicit
            }
        }

        StackValue(*) param = PUSH();

        // Non-annotated arguments allow all parameter types.

        if (local) {
            Init_Trash(param);
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

    *flags |= MKF_PARAMETER_SEEN;  // don't look for description after
}


//
//  Pop_Paramlist_With_Adjunct_May_Fail: C
//
// Assuming the stack is formed in a rhythm of the parameter, a type spec
// block, and a description...produce a paramlist in a state suitable to be
// passed to Make_Action().  It may not succeed because there could be
// duplicate parameters on the stack, and the checking via a binder is done
// as part of this popping process.
//
Array* Pop_Paramlist_With_Adjunct_May_Fail(
    Context* *adjunct_out,
    StackIndex base,
    Flags flags,
    StackIndex return_stackindex
){
    // Definitional RETURN slots must have their argument value fulfilled with
    // an ACTION! specific to the action called on *every instantiation*.
    // They are marked with special parameter classes to avoid needing to
    // separately do canon comparison of their symbols to find them.
    //
    // Note: Since RETURN's typeset holds types that need to be checked at
    // the end of the function run, it is moved to a predictable location:
    // first slot of the paramlist.  Initially it was the last slot...but this
    // enables adding more arguments/refinements/locals in derived functions.

    if (flags & MKF_RETURN) {
        if (return_stackindex == 0) { // no explicit RETURN: pure local

            Init_Word(PUSH(), Canon(RETURN));
            return_stackindex = TOP_INDEX;

            Init_Unconstrained_Parameter(  // return anything by default
                PUSH(),
                FLAG_PARAMCLASS_BYTE(PARAMCLASS_RETURN)
            );
        }
        else {
            assert(
                Cell_Word_Id(Data_Stack_At(return_stackindex)) == SYM_RETURN
            );
        }

        // definitional_return handled specially when paramlist copied
        // off of the stack...moved to head position.

        flags |= MKF_HAS_RETURN;
    }

    Count num_params = (TOP_INDEX - base) / 2;

    KeyList* keylist = Make_Series(KeyList,
        num_params,
        SERIES_MASK_KEYLIST | NODE_FLAG_MANAGED
    );
    Set_Series_Used(keylist, num_params);  // no terminator
    LINK(Ancestor, keylist) = keylist;  // chain

    Array* paramlist = Make_Array_Core(
        num_params + 1,
        SERIES_MASK_PARAMLIST
    );
    Set_Series_Len(paramlist, num_params + 1);

    if (flags & MKF_HAS_RETURN)
        paramlist->leader.bits |= VARLIST_FLAG_PARAMLIST_HAS_RETURN;

    // We want to check for duplicates and a Binder can be used for that
    // purpose--but fail() isn't allowed while binders are in effect.
    //
    // (This is why we wait until the parameter list gathering process
    // is over to do the duplicate checks--it can fail.)
    //
    struct Reb_Binder binder;
    INIT_BINDER(&binder);

    const Symbol* duplicate = nullptr;

  blockscope {
    Value* param = 1 + Init_Anti_Word(
        x_cast(Value*, Array_Head(paramlist)), Canon(ROOTVAR)
    );
    Key* key = Series_Head(Key, keylist);

    if (return_stackindex != 0) {
        assert(flags & MKF_RETURN);
        *key = Cell_Word_Symbol(Data_Stack_At(return_stackindex));
        ++key;

        Copy_Cell(param, Data_Stack_At(return_stackindex + 1));
        ++param;
    }

    StackIndex stackindex = base + 1;  // empty stack base would be 0, bad cell
    for (; stackindex <= TOP_INDEX; stackindex += 2) {
        const Symbol* symbol = Cell_Word_Symbol(Data_Stack_At(stackindex));
        StackValue(*) slot = Data_Stack_At(stackindex + 1);

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
            if (not Try_Add_Binder_Index(&binder, symbol, 1020))
                duplicate = symbol;

            hidden = false;
        }

        if (stackindex == return_stackindex)
            continue;  // was added to the head of the list already

        *key = symbol;

        Copy_Cell_Core(
            param,
            slot,
            CELL_MASK_COPY | CELL_FLAG_VAR_MARKED_HIDDEN
        );

        if (hidden)
            Set_Cell_Flag(param, VAR_MARKED_HIDDEN);

      #if !defined(NDEBUG)
        Set_Cell_Flag(param, PROTECTED);
      #endif

        ++key;
        ++param;
    }
    assert(param == Array_Tail(paramlist));

    Manage_Series(paramlist);

    INIT_BONUS_KEYSOURCE(paramlist, keylist);
    MISC(VarlistAdjunct, paramlist) = nullptr;
    LINK(Patches, paramlist) = nullptr;

    // With all the values extracted from stack to array, restore stack pointer
    //
    Drop_Data_Stack_To(base);
  }

    // Must remove binder indexes for all words, even if about to fail
    //
  blockscope {
    const Key* key_tail = Series_Tail(Key, keylist);
    const Key* key = Series_Head(Key, keylist);
    const Param* param = Series_At(Param, paramlist, 1);
    for (; key != key_tail; ++key, ++param) {
        //
        // See notes in AUGMENT on why we don't do binder indices on "sealed"
        // arguments (we can add `x` to the interface of a func with local `x`)
        //
        if (Get_Cell_Flag(param, VAR_MARKED_HIDDEN)) {
            assert(Is_Specialized(param));
        }
        else {
            if (Remove_Binder_Index_Else_0(&binder, KEY_SYMBOL(key)) == 0)
                assert(duplicate);  // erroring on this is pending
        }
    }

    SHUTDOWN_BINDER(&binder);

    if (duplicate) {
        DECLARE_ATOM (word);
        Init_Word(word, duplicate);
        fail (Error_Dup_Vars_Raw(word));
    }
  }

    //=///////////////////////////////////////////////////////////////////=//
    //
    // BUILD ADJUNCT INFORMATION OBJECT (IF NEEDED)
    //
    //=///////////////////////////////////////////////////////////////////=//

    // !!! See notes on ACTION-ADJUNCT in %sysobj.r
    //
    // Currently only contains description, assigned during parameter pushes.

    UNUSED(adjunct_out);

    return paramlist;
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
//     func [return: [integer!] {returns an integer}]
//
// In Ren-C's case it just means you want a local called return, but the
// generator will be "initializing it with a definitional return" for you.
// You don't have to use it if you don't want to...and may overwrite the
// variable.  But it won't be a void at the start.
//
Array* Make_Paramlist_Managed_May_Fail(
    Context* *adjunct_out,
    const Element* spec,
    Flags *flags  // flags may be modified to carry additional information
){
    StackIndex base = TOP_INDEX;

    StackIndex return_stackindex = 0;

    *adjunct_out = nullptr;

    // The process is broken up into phases so that the spec analysis code
    // can be reused in AUGMENT.
    //
    Push_Keys_And_Parameters_May_Fail(
        adjunct_out,
        spec,
        flags,
        &return_stackindex
    );
    Array* paramlist = Pop_Paramlist_With_Adjunct_May_Fail(
        adjunct_out,
        base,
        *flags,
        return_stackindex
    );

    return paramlist;
}


//
//  Make_Action: C
//
// Create an archetypal form of a function, given C code implementing a
// dispatcher that will be called by Eval_Core.  Dispatchers are of the form:
//
//     const Value* Dispatcher(Level* L) {...}
//
// The REBACT returned is "archetypal" because individual cells which hold
// the same REBACT may differ in a per-Cell "binding".  (This is how one
// RETURN is distinguished from another--the binding data stored in the cell
// identifies the pointer of the FRAME! to exit).
//
// Actions have an associated Array* of data, accessible via Phase_Details().
// This is where they can store information that will be available when the
// dispatcher is called.
//
// The `specialty` argument is an interface structure that holds information
// that can be shared between function instances.  It encodes information
// about the parameter names and types, specialization data, as well as any
// partial specialization or parameter reordering instructions.  This can
// take several forms depending on how much detail there is.  See the
// ACT_SPECIALTY() definition for more information on how this is laid out.
//
Phase* Make_Action(
    Array* paramlist,
    Option(Array*) partials,
    Dispatcher* dispatcher,  // native C function called by Action_Executor()
    REBLEN details_capacity  // capacity of Phase_Details (including archetype)
){
    assert(details_capacity >= 1);  // need archetype, maybe 1 (singular array)

    assert(Is_Node_Managed(paramlist));
    assert(
        Is_Anti_Word_With_Id(Series_Head(Value, paramlist), SYM_ROOTVAR)
        or CTX_TYPE(cast(Context*, paramlist)) == REB_FRAME
    );

    // !!! There used to be more validation code needed here when it was
    // possible to pass a specialization frame separately from a paramlist.
    // But once paramlists were separated out from the function's identity
    // array (using Phase_Details() as the identity instead of ACT_KEYLIST())
    // then all the "shareable" information was glommed up minus redundancy
    // into the ACT_SPECIALTY().  Here's some of the residual checking, as
    // a placeholder for more useful consistency checking which might be done.
    //
  blockscope {
    KeyList* keylist = cast(KeyList*, node_BONUS(KeySource, paramlist));

    Assert_Series_Managed(keylist);  // paramlists/keylists, can be shared
    assert(Series_Used(keylist) + 1 == Array_Len(paramlist));
    if (Get_Subclass_Flag(VARLIST, paramlist, PARAMLIST_HAS_RETURN)) {
        const Key* key = Series_At(const Key, keylist, 0);
        assert(KEY_SYM(key) == SYM_RETURN);
        UNUSED(key);
    }
  }

    // "details" for an action is an array of cells which can be anything
    // the dispatcher understands it to be, by contract.  Terminate it
    // at the given length implicitly.
    //
    Array* details = Make_Array_Core(
        details_capacity,  // Note: may be just 1 (so non-dynamic!)
        SERIES_MASK_DETAILS | NODE_FLAG_MANAGED
    );
    Set_Series_Len(details, details_capacity);

    Cell* archetype = Array_Head(details);
    Reset_Unquoted_Header_Untracked(TRACK(archetype), CELL_MASK_FRAME);
    INIT_VAL_ACTION_DETAILS(archetype, details);
    BINDING(archetype) = UNBOUND;
    INIT_VAL_ACTION_PARTIALS_OR_LABEL(archetype, partials);

  #if !defined(NDEBUG)  // notice attempted mutation of the archetype cell
    Set_Cell_Flag(archetype, PROTECTED);
  #endif

    // Leave rest of the cells in the capacity uninitialized (caller fills in)

    mutable_LINK_DISPATCHER(details) = cast(CFunction*, dispatcher);
    MISC(DetailsAdjunct, details) = nullptr;  // caller can fill in

    INODE(Exemplar, details) = cast(Context*, paramlist);

    Action* act = cast(Action*, details);  // now it's a legitimate Action

    // !!! We may have to initialize the exemplar rootvar.
    //
    Value* rootvar = Series_Head(Value, paramlist);
    if (Is_Anti_Word_With_Id(rootvar, SYM_ROOTVAR)) {
        INIT_VAL_FRAME_ROOTVAR(rootvar, paramlist, ACT_IDENTITY(act), UNBOUND);
    }

    // Precalculate cached function flags.  This involves finding the first
    // unspecialized argument which would be taken at a callsite, which can
    // be tricky to figure out with partial refinement specialization.  So
    // the work of doing that is factored into a routine (`PARAMETERS OF`
    // uses it as well).

    const Param* first = First_Unspecialized_Param(nullptr, act);
    if (first) {
        ParamClass pclass = Cell_ParamClass(first);
        switch (pclass) {
          case PARAMCLASS_RETURN:
          case PARAMCLASS_OUTPUT:
          case PARAMCLASS_NORMAL:
          case PARAMCLASS_META:
            break;

          case PARAMCLASS_SOFT:
          case PARAMCLASS_MEDIUM:
          case PARAMCLASS_HARD:
            Set_Subclass_Flag(VARLIST, paramlist, PARAMLIST_QUOTES_FIRST);
            break;

          default:
            assert(false);
        }

        if (Get_Parameter_Flag(first, SKIPPABLE))
            Set_Subclass_Flag(VARLIST, paramlist, PARAMLIST_SKIPPABLE_FIRST);
    }

    // The exemplar needs to be frozen, it can't change after this point.
    // You can't change the types or parameter conventions of an existing
    // action...you have to make a new variation.  Note that the exemplar
    // can be exposed by AS FRAME! of this action...
    //
    Freeze_Array_Shallow(paramlist);

    return ACT_IDENTITY(act);
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
void Get_Maybe_Fake_Action_Body(Sink(Value*) out, const Value* action)
{
    Context* binding = VAL_FRAME_BINDING(action);
    Action* a = VAL_ACTION(action);

    // A Hijacker *might* not need to splice itself in with a dispatcher.
    // But if it does, bypass it to get to the "real" action implementation.
    //
    // !!! Should the source inject messages like {This is a hijacking} at
    // the top of the returned body?
    //
    while (ACT_DISPATCHER(a) == &Hijacker_Dispatcher) {
        a = VAL_ACTION(ACT_ARCHETYPE(a));
        // !!! Review what should happen to binding
    }

    // !!! Should the binding make a difference in the returned body?  It is
    // exposed programmatically via CONTEXT OF.
    //
    UNUSED(binding);

    if (
        ACT_DISPATCHER(a) == &Func_Dispatcher
        or ACT_DISPATCHER(a) == &Lambda_Unoptimized_Dispatcher
    ){
        // Interpreted code, the body is a block with some bindings relative
        // to the action.

        Details* details = Phase_Details(ACT_IDENTITY(a));
        Cell* body = Array_At(details, IDX_DETAILS_1);

        // The PARAMLIST_HAS_RETURN tricks for definitional return make it
        // seem like a generator authored more code in the action's body...but
        // the code isn't *actually* there and an optimized internal trick is
        // used.  Fake the code if needed.

        Value* example;
        REBLEN real_body_index;
        if (ACT_DISPATCHER(a) == &Lambda_Dispatcher) {
            example = nullptr;
            real_body_index = 0;
            UNUSED(real_body_index);
        }
        else if (ACT_HAS_RETURN(a)) {
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
                Cell_Array(example),
                NODE_FLAG_MANAGED
            );

            // Index 5 (or 4 in zero-based C) should be #BODY, a "real" body.
            // To give it the appearance of executing code in place, we use
            // a GROUP!.

            Element* slot = Array_At(fake, real_body_index);  // #BODY
            assert(Is_Issue(slot));

            // Note: clears VAL_FLAG_LINE
            //
            Reset_Unquoted_Header_Untracked(TRACK(slot), CELL_MASK_GROUP);
            Init_Cell_Node1(slot, Cell_Array(body));
            VAL_INDEX_RAW(slot) = 0;
            INIT_SPECIFIER(slot, a);  // relative binding

            maybe_fake_body = fake;
        }

        // Cannot give user a relative value back, so make the relative
        // body specific to a fabricated expired frame.  See #2221

        Reset_Unquoted_Header_Untracked(TRACK(out), CELL_MASK_BLOCK);
        Init_Cell_Node1(out, maybe_fake_body);
        VAL_INDEX_RAW(out) = 0;

        return;
    }

    if (ACT_DISPATCHER(a) == &Specializer_Dispatcher) {
        //
        // The FRAME! stored in the body for the specialization has a phase
        // which is actually the function to be run.
        //
        const Value* frame = CTX_ARCHETYPE(ACT_EXEMPLAR(a));
        assert(Is_Frame(frame));
        Copy_Cell(out, frame);
        return;
    }

    if (ACT_DISPATCHER(a) == &Generic_Dispatcher) {
        Details* details = Phase_Details(ACT_IDENTITY(a));
        Value* verb = Details_At(details, 1);
        assert(Is_Word(verb));
        Copy_Cell(out, verb);
        return;
    }

    Init_Blank(out); // natives, ffi routines, etc.
    return;
}


//
//  REBTYPE: C
//
// This handler is used to fail for a type which cannot handle actions.
//
// !!! Currently all types have a REBTYPE() handler for either themselves or
// their class.  But having a handler that could be "swapped in" from a
// default failing case is an idea that could be used as an interim step
// to allow something like REB_GOB to fail by default, but have the failing
// type handler swapped out by an extension.
//
REBTYPE(Fail)
{
    UNUSED(verb);

    return RAISE("Datatype does not have a dispatcher registered.");
}


//
//  tweak: native [
//
//  "Modify a special property (currently only for ACTION!)"
//
//      return: "Same action identity as input"
//          [action?]
//      frame "(modified) Action to modify property of"
//          [<unrun> frame!]
//      property "Currently must be [defer postpone]"
//          [word!]
//      enable ; should be LOGIC!, but logic constraint not loaded yet
//  ]
//
DECLARE_NATIVE(tweak)
{
    INCLUDE_PARAMS_OF_TWEAK;

    Action* act = VAL_ACTION(ARG(frame));
    const Param* first = First_Unspecialized_Param(nullptr, act);

    ParamClass pclass = first
        ? Cell_ParamClass(first)
        : PARAMCLASS_NORMAL;  // imagine it as <end>able

    Flags flag;

    switch (Cell_Word_Id(ARG(property))) {
      case SYM_DEFER:  // Special enfix behavior used by THEN, ELSE, ALSO...
        if (pclass != PARAMCLASS_NORMAL and pclass != PARAMCLASS_META)
            fail ("TWEAK defer only actions with evaluative 1st params");
        flag = DETAILS_FLAG_DEFERS_LOOKBACK;
        break;

      case SYM_POSTPONE:  // Wait as long as it can to run w/o changing order
        if (
            pclass != PARAMCLASS_NORMAL
            and pclass != PARAMCLASS_SOFT
            and pclass != PARAMCLASS_META
        ){
            fail ("TWEAK postpone only actions with evaluative 1st params");
        }
        flag = DETAILS_FLAG_POSTPONES_ENTIRELY;
        break;

      default:
        fail ("TWEAK currently only supports [barrier defer postpone]");
    }

    if (Cell_Logic(ARG(enable)))
        ACT_IDENTITY(act)->leader.bits |= flag;
    else
        ACT_IDENTITY(act)->leader.bits &= ~flag;

    return Actionify(Copy_Cell(OUT, ARG(frame)));;
}
