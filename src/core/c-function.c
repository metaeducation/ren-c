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
    SPEC_MODE_NORMAL, // words are arguments
    SPEC_MODE_LOCAL, // words are locals
    SPEC_MODE_WITH // words are "extern"
};


static void Finalize_Param(Value(*) param) {
    //
    // This used to guard against nullptr in Cell_Parameter_Spec() and canonize
    // the refinement case to EMPTY_ARRAY and the non-refinement case to
    // the ANY-VALUE! typeset.  New philosophy is to allow null arrays in
    // order to establish &(ANY-VALUE?) as a type annotation, because
    // ANY-VALUE? has to bootstrap itself somehow.
    //
    UNUSED(param);
}


//
//  Push_Paramlist_Quads_May_Fail: C
//
// This is an implementation routine for Make_Paramlist_Managed_May_Fail().
// It was broken out into its own separate routine so that the AUGMENT
// function could reuse the logic for function spec analysis.  It may not
// be broken out in a particularly elegant way, but it's a start.
//
void Push_Paramlist_Quads_May_Fail(
    const REBVAL *spec,
    Flags *flags,
    StackIndex *return_stackindex
){
    assert(Is_Block(spec));

    enum Reb_Spec_Mode mode = SPEC_MODE_NORMAL;

    bool refinement_seen = false;

    const Cell* tail;
    const Cell* value = Cell_Array_At(&tail, spec);

    while (value != tail) {
        const Cell* item = value;  // "faked"
        ++value;  // go ahead and consume next

    //=//// STRING! FOR FUNCTION DESCRIPTION OR PARAMETER NOTE ////////////=//

        if (Is_Text(item)) {
            //
            // Consider `[<with> some-extern "description of that extern"]` to
            // be purely commentary for the implementation, and don't include
            // it in the meta info.
            //
            if (mode == SPEC_MODE_WITH)
                continue;

            StackValue(*) notes = NOTES_SLOT(TOP_INDEX);
            assert(
                Is_Nulled(notes)  // hasn't been written to yet
                or Is_Text(notes)  // !!! we overwrite, but should we append?
            );

            if (Is_Word_Isotope_With_Id(KEY_SLOT(TOP_INDEX), SYM_KEY)) {
                // no keys seen yet, act as description
                Init_Text(notes, Copy_String_At(item));
                *flags |= MKF_HAS_DESCRIPTION;
            }
            else {
                assert(Is_Word(KEY_SLOT(TOP_INDEX)));
                Init_Text(notes, Copy_String_At(item));
                *flags |= MKF_HAS_NOTES;
            }

            continue;
        }

    //=//// TOP-LEVEL SPEC TAGS LIKE <local>, <with> etc. /////////////////=//

        bool strict = false;
        if (Is_Tag(item) and (*flags & MKF_KEYWORDS)) {
            if (0 == CT_String(item, Root_With_Tag, strict)) {
                mode = SPEC_MODE_WITH;
                continue;
            }
            else if (0 == CT_String(item, Root_Local_Tag, strict)) {
                mode = SPEC_MODE_LOCAL;
                continue;
            }
            else if (0 == CT_String(item, Root_None_Tag, strict)) {
                StackValue(*) param = PARAM_SLOT(TOP_INDEX);
                Set_Parameter_Flag(param, RETURN_NONE);  // enforce RETURN NONE

                assert(Cell_Parameter_Spec(param) == nullptr);
                continue;
            }
            else if (0 == CT_String(item, Root_Nihil_Tag, strict)) {
                StackValue(*) param = PARAM_SLOT(TOP_INDEX);
                Set_Parameter_Flag(param, RETURN_NIHIL);  // enforce RETURN NIHIL

                assert(Cell_Parameter_Spec(param) == nullptr);
                continue;
            }
            else
                fail (Error_Bad_Func_Def_Raw(item));
        }

    //=//// BLOCK! OF TYPES TO MAKE TYPESET FROM (PLUS PARAMETER TAGS) ////=//

        if (Is_Block(item)) {
            if (Is_Word_Isotope_With_Id(KEY_SLOT(TOP_INDEX), SYM_KEY))
                fail (Error_Bad_Func_Def_Raw(item));   // `func [[integer!]]`

            Specifier* derived = Derive_Specifier(Cell_Specifier(spec), item);

          blockscope {
            StackValue(*) types = TYPES_SLOT(TOP_INDEX);

            if (Is_Block(types))  // too many, `func [x [integer!] [blank!]]`
                fail (Error_Bad_Func_Def_Raw(item));

            assert(Is_Nulled(types));

            // You currently can't say `<local> x [integer!]`, because locals
            // are hidden from the interface, and hidden values (notably
            // specialized-out values) use the `param` slot for the value,
            // not type information.  So local has `~` isotope in that slot.
            //
            // Even if you could give locals a type, it could only be given
            // a meaning if it were used to check assignments during the
            // function.  There's currently no mechanism for doing that.
            //
            // You can't say `<with> y [integer!]` either...though it might
            // be a nice feature to check the type of an imported value at
            // the time of calling.
            //
            if (mode != SPEC_MODE_NORMAL)  // <local> <with>
                fail (Error_Bad_Func_Def_Raw(item));

            // Turn block into typeset for parameter at current index.
            // Leaves VAL_TYPESET_SYM as-is.

            StackValue(*) param = PARAM_SLOT(TOP_INDEX);

            if (Is_Specialized(cast(Param*, cast(REBVAL*, param))))
                continue;

            Init_Block(
                types,
                Copy_Array_At_Deep_Managed(
                    Cell_Array(item),
                    VAL_INDEX(item),
                    derived
                )
            );
          }

            StackValue(*) param = PARAM_SLOT(TOP_INDEX);
            Flags param_flags = PARAMETER_FLAGS(param);
            assert(Cell_Parameter_Spec(param) == nullptr);

            Init_Parameter(param, param_flags, item, derived);

            *flags |= MKF_HAS_TYPES;
            continue;
        }

    //=//// ANY-WORD! PARAMETERS THEMSELVES (MAKE TYPESETS w/SYMBOL) //////=//

        bool quoted = false;  // single quoting level used as signal in spec
        if (Cell_Num_Quotes(item) > 0) {
            if (Cell_Num_Quotes(item) > 1)
                fail (Error_Bad_Func_Def_Raw(item));
            quoted = true;
        }

        enum Reb_Kind heart = Cell_Heart(item);

        const Symbol* symbol = nullptr;  // avoids compiler warning
        ParamClass pclass = PARAMCLASS_0;  // error if not changed

        bool local = false;
        bool refinement = false;  // paths with blanks at head are refinements
        if (Any_Path_Kind(heart)) {
            if (not IS_REFINEMENT_CELL(item))
                fail (Error_Bad_Func_Def_Raw(item));

            refinement = true;
            refinement_seen = true;

            // !!! If you say [<with> x /foo y] the <with> terminates and a
            // refinement is started.  Same w/<local>.  Is this a good idea?
            // Note that historically, help hides any refinements that appear
            // behind a /local, but this feature has no parallel in Ren-C.
            //
            mode = SPEC_MODE_NORMAL;

            symbol = VAL_REFINEMENT_SYMBOL(item);
            if (Symbol_Id(symbol) == SYM_LOCAL) {  // /LOCAL
                if (item + 1 != tail and Any_Word(item + 1))
                    fail (Error_Legacy_Local_Raw(spec));  // -> <local>
            }

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
                if (VAL_WORD_ID(item) == SYM_RETURN and not quoted) {
                    pclass = PARAMCLASS_RETURN;
                }
            }
            else if (heart == REB_THE_WORD) {
                //
                // Outputs are set to refinements, because they can act like
                // refinements and be passed the word to set.
                //
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
                if (  // let RETURN: presence indicate you know new rules
                    refinement_seen and mode == SPEC_MODE_NORMAL
                    and *return_stackindex == 0
                ){
                    fail (Error_Legacy_Refinement_Raw(spec));
                }

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

        if (not local and pclass == PARAMCLASS_0)  // didn't match
            fail (Error_Bad_Func_Def_Raw(item));

        if (mode != SPEC_MODE_NORMAL) {
            if (pclass != PARAMCLASS_NORMAL and not local)
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

        // Pushing description values for a new named element...

        PUSH_SLOTS();

        Init_Word(KEY_SLOT(TOP_INDEX), symbol);
        Init_Nulled(TYPES_SLOT(TOP_INDEX));  // may or may not add later
        Init_Nulled(NOTES_SLOT(TOP_INDEX));  // may or may not add later

        StackValue(*) param = PARAM_SLOT(TOP_INDEX);

        // Non-annotated arguments allow all parameter types.

        if (local) {
            Finalize_None(param);
        }
        else if (refinement) {
            Init_Unconstrained_Parameter(
                param,
                FLAG_PARAMCLASS_BYTE(pclass)
                    | PARAMETER_FLAG_REFINEMENT  // must preserve if type block
                    | PARAMETER_FLAG_NULLS_DEFINITELY_OK  // need if refinement
            );
        }
        else {
            Init_Unconstrained_Parameter(
                param,
                FLAG_PARAMCLASS_BYTE(pclass)
            );
        }

        if (symbol == Canon(RETURN)) {
            if (*return_stackindex != 0) {
                DECLARE_LOCAL (word);
                Init_Word(word, symbol);
                fail (Error_Dup_Vars_Raw(word));  // most dup checks are later
            }
            if (*flags & MKF_RETURN) {
                assert(pclass == PARAMCLASS_RETURN);
                *return_stackindex = TOP_INDEX;  // RETURN: explicit
            }
        }
    }
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
            PUSH_SLOTS();

            Init_Word(KEY_SLOT(TOP_INDEX), Canon(RETURN));
            return_stackindex = TOP_INDEX;

            StackValue(*) param = PARAM_SLOT(TOP_INDEX);

            // By default, you can return anything.  This goes with the bias
            // that checks happen on the reading side of things, not writing.
            //
            // This includes void.  Returning void is a bit rare when your
            // function has a body and you don't use RETURN, because the entire
            // body has to be void.  If it does, we want to allow it:
            //
            //    >> wrapper: func [x] [return comment x]
            //
            //    >> 1 + 2 wrapper "This is desirable"
            //    == 3
            //
            // If you have a RETURN spec, however, you must explicitly say
            // you can return void.
            //
            Init_Unconstrained_Parameter(
                param,
                FLAG_PARAMCLASS_BYTE(PARAMCLASS_RETURN)
            );

            Init_Nulled(TYPES_SLOT(TOP_INDEX));
            Init_Nulled(NOTES_SLOT(TOP_INDEX));
        }
        else {
            StackValue(*) param = PARAM_SLOT(return_stackindex);
            assert(
                VAL_WORD_ID(KEY_SLOT(return_stackindex)) == SYM_RETURN
            );
            UNUSED(param);
        }

        // definitional_return handled specially when paramlist copied
        // off of the stack...moved to head position.

        flags |= MKF_HAS_RETURN;
    }

    // Slots, which is length +1 (includes the rootvar or rootparam)
    //
    assert((TOP_INDEX - base) % 4 == 0);
    Count num_slots = (TOP_INDEX - base) / 4;

    // Must make the function "paramlist" even if "empty", for identity.
    //
    // !!! This is no longer true, since details is the identity.  Review
    // optimization potential.
    //
    Array* paramlist = Make_Array_Core(
        num_slots,
        SERIES_MASK_PARAMLIST
    );
    Set_Series_Len(paramlist, num_slots);

    KeyList* keylist = Make_Series(KeyList,
        (num_slots - 1),  // - 1 archetype
        SERIES_MASK_KEYLIST | NODE_FLAG_MANAGED
    );
    Set_Series_Used(keylist, num_slots - 1);  // no terminator
    LINK(Ancestor, keylist) = keylist;  // chain ends with self

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
    REBVAL *param = 1 + Init_Word_Isotope(
        x_cast(Value(*), Array_Head(paramlist)), Canon(ROOTVAR)
    );
    Key* key = Series_Head(Key, keylist);

    if (return_stackindex != 0) {
        assert(flags & MKF_RETURN);
        Init_Key(key, Cell_Word_Symbol(KEY_SLOT(return_stackindex)));
        ++key;

        Finalize_Param(cast(REBVAL*, PARAM_SLOT(return_stackindex)));
        Copy_Cell(param, PARAM_SLOT(return_stackindex));
        ++param;
    }

    StackIndex stackindex = base + 8;
    for (; stackindex <= TOP_INDEX; stackindex += 4) {
        const Symbol* symbol = Cell_Word_Symbol(KEY_SLOT(stackindex));

        StackValue(*) slot = PARAM_SLOT(stackindex);

        assert(Not_Cell_Flag(slot, VAR_MARKED_HIDDEN));  // use NOTE_SEALED

        // "Sealed" parameters do not count in the binding.  See AUGMENT for
        // notes on why we do this (you can augment a function that has a
        // local called `x` with a new parameter called `x`, and that's legal.)
        //
        bool hidden;
        if (Get_Cell_Flag(slot, STACK_NOTE_SEALED)) {
            assert(Is_Specialized(cast(Param*, cast(REBVAL*, slot))));

            // !!! This flag was being set on an uninitialized param, with the
            // remark "survives copy over".  But the copy puts the flag on
            // regardless below.  Was this specific to RETURNs?
            //
            hidden = true;
        }
        else {
            if (not Is_Specialized(cast(Param*, cast(REBVAL*, slot))))
                Finalize_Param(cast(REBVAL*, slot));

            if (not Try_Add_Binder_Index(&binder, symbol, 1020))
                duplicate = symbol;

            hidden = false;
        }

        if (stackindex == return_stackindex)
            continue;  // was added to the head of the list already

        Init_Key(key, symbol);

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

    Manage_Series(paramlist);

    INIT_BONUS_KEYSOURCE(paramlist, keylist);
    MISC(VarlistAdjunct, paramlist) = nullptr;
    LINK(Patches, paramlist) = nullptr;
  }

    // Must remove binder indexes for all words, even if about to fail
    //
  blockscope {
    const Key* tail = Series_Tail(Key, keylist);
    const Key* key = Series_Head(Key, keylist);
    const Param* param = Series_At(Param, paramlist, 1);
    for (; key != tail; ++key, ++param) {
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
        DECLARE_LOCAL (word);
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

    if (flags & (MKF_HAS_DESCRIPTION | MKF_HAS_TYPES | MKF_HAS_NOTES))
        *adjunct_out = Copy_Context_Shallow_Managed(
            VAL_CONTEXT(Root_Action_Adjunct)
        );
    else
        *adjunct_out = nullptr;

    // If a description string was gathered, it's sitting in the first string
    // slot, the third cell we pushed onto the stack.  Extract it if so.
    //
    if (flags & MKF_HAS_DESCRIPTION) {
        StackValue(*) description = NOTES_SLOT(base + 4);
        assert(Is_Text(description));
        Copy_Cell(
            CTX_VAR(*adjunct_out, STD_ACTION_ADJUNCT_DESCRIPTION),
            description
        );
    }

    // Only make `parameter-types` if there were blocks in the spec
    //
    if (flags & MKF_HAS_TYPES) {
        Array* types_varlist = Make_Array_Core(
            num_slots,
            SERIES_MASK_VARLIST | NODE_FLAG_MANAGED
        );
        Set_Series_Len(types_varlist, num_slots);

        MISC(VarlistAdjunct, types_varlist) = nullptr;
        LINK(Patches, types_varlist) = nullptr;
        INIT_CTX_KEYLIST_SHARED(cast(Context*, types_varlist), keylist);

        Cell* rootvar = Array_Head(types_varlist);
        INIT_VAL_CONTEXT_ROOTVAR(rootvar, REB_OBJECT, types_varlist);

        REBVAL *dest = SPECIFIC(rootvar) + 1;
        const Cell* param = Array_At(paramlist, 1);

        if (return_stackindex != 0) {
            assert(flags & MKF_RETURN);
            ++param;

            Copy_Cell(dest, TYPES_SLOT(return_stackindex));
            ++dest;
        }

        StackIndex stackindex = base + 8;
        for (; stackindex <= TOP_INDEX; stackindex += 4) {
            StackValue(*) types = TYPES_SLOT(stackindex);
            assert(Is_Nulled(types) or Is_Block(types));

            if (stackindex == return_stackindex)
                continue;  // was added to the head of the list already

            Copy_Cell(dest, types);

            ++dest;
            ++param;
        }

        Init_Object(
            CTX_VAR(*adjunct_out, STD_ACTION_ADJUNCT_PARAMETER_TYPES),
            cast(Context*, types_varlist)
        );

        USED(param);
    }

    // Only make `parameter-notes` if there were strings (besides description)
    //
    if (flags & MKF_HAS_NOTES) {
        Array* notes_varlist = Make_Array_Core(
            num_slots,
            SERIES_MASK_VARLIST | NODE_FLAG_MANAGED
        );
        Set_Series_Len(notes_varlist, num_slots);

        MISC(VarlistAdjunct, notes_varlist) = nullptr;
        LINK(Patches, notes_varlist) = nullptr;
        INIT_CTX_KEYLIST_SHARED(cast(Context*, notes_varlist), keylist);

        Cell* rootvar = Array_Head(notes_varlist);
        INIT_VAL_CONTEXT_ROOTVAR(rootvar, REB_OBJECT, notes_varlist);

        const Cell* param = Array_At(paramlist, 1);
        REBVAL *dest = SPECIFIC(rootvar) + 1;

        if (return_stackindex != 0) {
            assert(flags & MKF_RETURN);
            ++param;

            Copy_Cell(dest, NOTES_SLOT(return_stackindex));
            ++dest;
        }

        StackIndex stackindex = base + 8;
        for (; stackindex <= TOP_INDEX; stackindex += 4) {
            StackValue(*) notes = NOTES_SLOT(stackindex);
            assert(Is_Text(notes) or Is_Nulled(notes));

            if (stackindex == return_stackindex)
                continue;  // was added to the head of the list already

            Copy_Cell(dest, notes);

            ++dest;
            ++param;
        }

        Init_Object(
            CTX_VAR(*adjunct_out, STD_ACTION_ADJUNCT_PARAMETER_NOTES),
            cast(Context*, notes_varlist)
        );

        USED(param);
    }

    // With all the values extracted from stack to array, restore stack pointer
    //
    Drop_Data_Stack_To(base);

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
    const REBVAL *spec,
    Flags *flags  // flags may be modified to carry additional information
){
    StackIndex base = TOP_INDEX;

    StackIndex return_stackindex = 0;

    PUSH_SLOTS();

    // As we go through the spec block, we push TYPESET! BLOCK! TEXT! triples.
    // These will be split out into separate arrays after the process is done.
    // The first slot of the paramlist needs to be the function canon value,
    // while the other two first slots need to be rootkeys.  Get the process
    // started right after a BLOCK! so it's willing to take a string for
    // the function description--it will be extracted from the slot before
    // it is turned into a rootkey for param_notes.
    //
    Init_Word_Isotope(KEY_SLOT(TOP_INDEX), Canon(KEY));  // signal no pushes yet
    Init_Trash(PARAM_SLOT(TOP_INDEX));  // not used at all
    Init_Trash(TYPES_SLOT(TOP_INDEX));  // not used at all
    Init_Nulled(NOTES_SLOT(TOP_INDEX));  // overwritten if description

    // The process is broken up into phases so that the spec analysis code
    // can be reused in AUGMENT.
    //
    Push_Paramlist_Quads_May_Fail(
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
//     const REBVAL *Dispatcher(Level* L) {...}
//
// The REBACT returned is "archetypal" because individual REBVALs which hold
// the same REBACT may differ in a per-REBVAL "binding".  (This is how one
// RETURN is distinguished from another--the binding data stored in the REBVAL
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
        Is_Word_Isotope_With_Id(Array_Head(paramlist), SYM_ROOTVAR)  // fills in
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
    REBVAL *rootvar = Series_Head(REBVAL, paramlist);
    if (Is_Word_Isotope_With_Id(rootvar, SYM_ROOTVAR)) {
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
void Get_Maybe_Fake_Action_Body(Sink(Value(*)) out, Value(const*) action)
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
        or ACT_DISPATCHER(a) == &Block_Dispatcher
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

        REBVAL *example;
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
                Cell_Specifier(example),
                NODE_FLAG_MANAGED
            );

            // Index 5 (or 4 in zero-based C) should be #BODY, a "real" body.
            // To give it the appearance of executing code in place, we use
            // a GROUP!.

            Cell* slot = Array_At(fake, real_body_index);  // #BODY
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

        // Don't use INIT_SPECIFIER(), because it does not expect to get an
        // inaccessible series.
        //
        BINDING(out) = &PG_Inaccessible_Series;
        return;
    }

    if (ACT_DISPATCHER(a) == &Specializer_Dispatcher) {
        //
        // The FRAME! stored in the body for the specialization has a phase
        // which is actually the function to be run.
        //
        const REBVAL *frame = CTX_ARCHETYPE(ACT_EXEMPLAR(a));
        assert(Is_Frame(frame));
        Copy_Cell(out, frame);
        return;
    }

    if (ACT_DISPATCHER(a) == &Generic_Dispatcher) {
        Details* details = Phase_Details(ACT_IDENTITY(a));
        REBVAL *verb = Details_At(details, 1);
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
//  {Modify a special property (currently only for ACTION!)}
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

    switch (VAL_WORD_ID(ARG(property))) {
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
