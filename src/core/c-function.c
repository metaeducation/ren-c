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
    const REBKEY *key,
    const REBPAR *param,
    REBFLGS flags,
    void *opaque
){
    struct Params_Of_State *s = cast(struct Params_Of_State*, opaque);

    Init_Word(DS_PUSH(), KEY_SYMBOL(key));

    if (not s->just_words) {
        if (
            not (flags & PHF_UNREFINED)
            and GET_PARAM_FLAG(param, REFINEMENT)
        ){
            Refinify(DS_TOP);
        }

        switch (VAL_PARAM_CLASS(param)) {
          case PARAM_CLASS_RETURN:
          case PARAM_CLASS_OUTPUT:
          case PARAM_CLASS_NORMAL:
            break;

          case PARAM_CLASS_META:
            Metafy(DS_TOP);
            break;

          case PARAM_CLASS_SOFT:
            Getify(DS_TOP);
            break;

          case PARAM_CLASS_MEDIUM:
            Quotify(Getify(DS_TOP), 1);
            break;

          case PARAM_CLASS_HARD:
            Quotify(DS_TOP, 1);
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
REBARR *Make_Action_Parameters_Arr(REBACT *act, bool just_words)
{
    struct Params_Of_State s;
    s.just_words = just_words;

    REBDSP dsp_orig = DSP;
    For_Each_Unspecialized_Param(act, &Params_Of_Hook, &s);
    return Pop_Stack_Values(dsp_orig);
}



enum Reb_Spec_Mode {
    SPEC_MODE_NORMAL, // words are arguments
    SPEC_MODE_LOCAL, // words are locals
    SPEC_MODE_WITH // words are "extern"
};


#define KEY_SLOT(dsp)       DS_AT((dsp) - 3)
#define PARAM_SLOT(dsp)     DS_AT((dsp) - 2)
#define TYPES_SLOT(dsp)     DS_AT((dsp) - 1)
#define NOTES_SLOT(dsp)     DS_AT(dsp)

#define PUSH_SLOTS() \
    do { DS_PUSH(); DS_PUSH(); DS_PUSH(); DS_PUSH(); } while (0)

//
//  Push_Paramlist_Triads_May_Fail: C
//
// This is an implementation routine for Make_Paramlist_Managed_May_Fail().
// It was broken out into its own separate routine so that the AUGMENT
// function could reuse the logic for function spec analysis.  It may not
// be broken out in a particularly elegant way, but it's a start.
//
void Push_Paramlist_Triads_May_Fail(
    const REBVAL *spec,
    REBFLGS *flags,
    REBDSP *definitional_return_dsp
){
    assert(IS_BLOCK(spec));

    enum Reb_Spec_Mode mode = SPEC_MODE_NORMAL;

    bool refinement_seen = false;

    const RELVAL *tail;
    const RELVAL *value = VAL_ARRAY_AT(&tail, spec);

    while (value != tail) {
        const RELVAL* item = value;  // "faked"
        ++value;  // go ahead and consume next

    //=//// STRING! FOR FUNCTION DESCRIPTION OR PARAMETER NOTE ////////////=//

        if (IS_TEXT(item)) {
            //
            // Consider `[<with> some-extern "description of that extern"]` to
            // be purely commentary for the implementation, and don't include
            // it in the meta info.
            //
            if (mode == SPEC_MODE_WITH)
                continue;

            STKVAL(*) notes = NOTES_SLOT(DSP);
            assert(
                IS_NULLED(notes)  // hasn't been written to yet
                or IS_TEXT(notes)  // !!! we overwrite, but should we append?
            );

            if (IS_BAD_WORD(KEY_SLOT(DSP))) {  // no keys seen, act as description
                Init_Text(notes, Copy_String_At(item));
                *flags |= MKF_HAS_DESCRIPTION;
            }
            else {
                Init_Text(notes, Copy_String_At(item));
                *flags |= MKF_HAS_NOTES;
            }

            continue;
        }

    //=//// TOP-LEVEL SPEC TAGS LIKE <local>, <with> etc. /////////////////=//

        bool strict = false;
        if (IS_TAG(item) and (*flags & MKF_KEYWORDS)) {
            if (0 == CT_String(item, Root_With_Tag, strict)) {
                mode = SPEC_MODE_WITH;
                continue;
            }
            else if (0 == CT_String(item, Root_Local_Tag, strict)) {
                mode = SPEC_MODE_LOCAL;
                continue;
            }
            else if (0 == CT_String(item, Root_None_Tag, strict)) {
                *flags |= MKF_HAS_OPAQUE_RETURN;  // use Opaque_Dispatcher()

                // Fake as if they said []
                //
                STKVAL(*) param = PARAM_SLOT(DSP);
                CLEAR_ALL_TYPESET_BITS(param);
                continue;
            }
            else if (0 == CT_String(item, Root_Void_Tag, strict)) {
                *flags |= MKF_IS_ELIDER;

                // Fake as if they said [<invisible>] !!! make more efficient
                //
                STKVAL(*) param = PARAM_SLOT(DSP);
                CLEAR_ALL_TYPESET_BITS(param);
                SET_PARAM_FLAG(param, ENDABLE);
                continue;
            }
            else
                fail (Error_Bad_Func_Def_Raw(item));
        }

    //=//// BLOCK! OF TYPES TO MAKE TYPESET FROM (PLUS PARAMETER TAGS) ////=//

        if (IS_BLOCK(item)) {
            if (IS_BAD_WORD(KEY_SLOT(DSP)))  // too early, `func [[integer!] {!}]`
                fail (Error_Bad_Func_Def_Raw(item));

            STKVAL(*) types = TYPES_SLOT(DSP);

            if (IS_BLOCK(types))  // too many, `func [x [integer!] [blank!]]`
                fail (Error_Bad_Func_Def_Raw(item));

            assert(IS_NULLED(types));

            // You currently can't say `<local> x [integer!]`, because locals
            // are hidden from the interface, and hidden values (notably
            // specialized-out values) use the `param` slot for the value,
            // not type information.  So local has `~unset~ in that slot.
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

            STKVAL(*) param = PARAM_SLOT(DSP);

            if (Is_Specialized(cast(REBPAR*, cast(REBVAL*, param))))
                continue;

            REBSPC* derived = Derive_Specifier(VAL_SPECIFIER(spec), item);
            Init_Block(
                types,
                Copy_Array_At_Deep_Managed(
                    VAL_ARRAY(item),
                    VAL_INDEX(item),
                    derived
                )
            );

            // Turn block into typeset for parameter at current index.
            // Leaves VAL_TYPESET_SYM as-is.

            bool was_refinement = GET_PARAM_FLAG(param, REFINEMENT);
            VAL_TYPESET_LOW_BITS(param) = 0;
            VAL_TYPESET_HIGH_BITS(param) = 0;

            const RELVAL *types_tail;
            const RELVAL *types_at = VAL_ARRAY_AT(&types_tail, item);
            Add_Typeset_Bits_Core(
                cast_PAR(param),
                types_at,
                types_tail,
                derived
            );
            if (was_refinement)
                SET_PARAM_FLAG(param, REFINEMENT);

            *flags |= MKF_HAS_TYPES;
            continue;
        }

    //=//// ANY-WORD! PARAMETERS THEMSELVES (MAKE TYPESETS w/SYMBOL) //////=//

        bool quoted = false;  // single quoting level used as signal in spec
        if (VAL_NUM_QUOTES(item) > 0) {
            if (VAL_NUM_QUOTES(item) > 1)
                fail (Error_Bad_Func_Def_Raw(item));
            quoted = true;
        }

        REBCEL(const*) cell = VAL_UNESCAPED(item);
        enum Reb_Kind kind = CELL_KIND(cell);

        const REBSYM* symbol = nullptr;  // avoids compiler warning
        enum Reb_Param_Class pclass = PARAM_CLASS_0;  // error if not changed

        bool local = false;
        bool refinement = false;  // paths with blanks at head are refinements
        if (ANY_PATH_KIND(kind)) {
            if (not IS_REFINEMENT_CELL(cell))
                fail (Error_Bad_Func_Def_Raw(item));

            refinement = true;
            refinement_seen = true;

            // !!! If you say [<with> x /foo y] the <with> terminates and a
            // refinement is started.  Same w/<local>.  Is this a good idea?
            // Note that historically, help hides any refinements that appear
            // behind a /local, but this feature has no parallel in Ren-C.
            //
            mode = SPEC_MODE_NORMAL;

            symbol = VAL_REFINEMENT_SYMBOL(cell);
            if (ID_OF_SYMBOL(symbol) == SYM_LOCAL)  // /LOCAL
                if (ANY_WORD_KIND(KIND3Q_BYTE(item + 1)))  // END is 0
                    fail (Error_Legacy_Local_Raw(spec));  // -> <local>

            if (CELL_KIND(cell) == REB_GET_PATH) {
                if (quoted)
                    pclass = PARAM_CLASS_MEDIUM;
                else
                    pclass = PARAM_CLASS_SOFT;
            }
            else if (CELL_KIND(cell) == REB_PATH) {
                if (quoted)
                    pclass = PARAM_CLASS_HARD;
                else
                    pclass = PARAM_CLASS_NORMAL;
            }
        }
        else if (ANY_TUPLE_KIND(kind)) {
            //
            // !!! Tuples are theorized as a way to "name parameters out of
            // the way" so there can be an interface name, but then a local
            // name...so that something like /ALL can be named out of the
            // way without disrupting use of ALL.  That's not implemented yet,
            // but it's now another way to name locals.
            //
            if (IS_PREDICATE1_CELL(cell) and not quoted) {
                local = true;
                symbol = VAL_PREDICATE1_SYMBOL(cell);
            }
        }
        else if (ANY_WORD_KIND(kind)) {
            symbol = VAL_WORD_SYMBOL(cell);

            if (kind == REB_SET_WORD) {
                //
                // Outputs are set to refinements, because they can act like
                // refinements and be passed the word to set.
                //
                if (VAL_WORD_ID(cell) == SYM_RETURN and not quoted) {
                    pclass = PARAM_CLASS_RETURN;
                }
                else if (not quoted) {
                    refinement = true;  // sets PARAM_FLAG_REFINEMENT
                    pclass = PARAM_CLASS_OUTPUT;
                }
            }
            else {
                if (  // let RETURN: presence indicate you know new rules
                    refinement_seen and mode == SPEC_MODE_NORMAL
                    and *definitional_return_dsp == 0
                ){
                    fail (Error_Legacy_Refinement_Raw(spec));
                }

                if (kind == REB_GET_WORD) {
                    if (quoted)
                        pclass = PARAM_CLASS_MEDIUM;
                    else
                        pclass = PARAM_CLASS_SOFT;
                }
                else if (kind == REB_WORD) {
                    if (quoted)
                        pclass = PARAM_CLASS_HARD;
                    else
                        pclass = PARAM_CLASS_NORMAL;
                }
                else if (kind == REB_META_WORD) {
                    if (not quoted)
                        pclass = PARAM_CLASS_META;
                }
            }
        }
        else
            fail (Error_Bad_Func_Def_Raw(item));

        if (not local and pclass == PARAM_CLASS_0)  // didn't match
            fail (Error_Bad_Func_Def_Raw(item));

        if (mode != SPEC_MODE_NORMAL) {
            if (pclass != PARAM_CLASS_NORMAL and not local)
                fail (Error_Bad_Func_Def_Raw(item));

            if (mode == SPEC_MODE_LOCAL)
                local = true;
        }

        if (ID_OF_SYMBOL(symbol) == SYM_RETURN and pclass != PARAM_CLASS_RETURN) {
            //
            // Cancel definitional return if any non-SET-WORD! uses the name
            // RETURN when defining a FUNC.
            //
            *flags &= ~MKF_RETURN;
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
        //
        PUSH_SLOTS();

        Init_Word(KEY_SLOT(DSP), symbol);
        Init_Nulled(TYPES_SLOT(DSP));  // may or may not add later
        Init_Nulled(NOTES_SLOT(DSP));  // may or may not add later

        STKVAL(*) param = PARAM_SLOT(DSP);

        // Non-annotated arguments allow all parameter types, but a normal
        // parameter cannot pick up a non-isotope form of BAD-WORD!.
        //
        // Note there are currently two ways to get NULL: <opt> and <end>.
        // If the typeset bits contain REB_NULL, that indicates <opt>.
        // But Is_Param_Endable() indicates <end>.

        if (local) {
            Init_Unset(param);
        }
        else if (refinement) {
            Init_Param(
                param,
                FLAG_PARAM_CLASS_BYTE(pclass)
                    | PARAM_FLAG_REFINEMENT,  // must preserve if type block
                TS_NOTHING
            );
        }
        else
            Init_Param(
                param,
                FLAG_PARAM_CLASS_BYTE(pclass),
                TS_OPT_VALUE  // By default <opt> ANY-VALUE! is legal
            );

        // All these would cancel a definitional return (leave has same idea):
        //
        //     func [return [integer!]]
        //     func [/refinement return]
        //     func [<local> return]
        //     func [<with> return]
        //
        // ...although `return:` is explicitly tolerated ATM for compatibility
        // (despite violating the "pure locals are NULL" premise)
        //
        if (symbol == Canon(RETURN)) {
            if (*definitional_return_dsp != 0) {
                DECLARE_LOCAL (word);
                Init_Word(word, symbol);
                fail (Error_Dup_Vars_Raw(word));  // most dup checks are later
            }
            if (pclass == PARAM_CLASS_RETURN)
                *definitional_return_dsp = DSP;  // RETURN: explicit
            else
                *flags &= ~MKF_RETURN;
        }
    }
}


//
//  Pop_Paramlist_And_Meta_May_Fail: C
//
// Assuming the stack is formed in a rhythm of the parameter, a type spec
// block, and a description...produce a paramlist in a state suitable to be
// passed to Make_Action().  It may not succeed because there could be
// duplicate parameters on the stack, and the checking via a binder is done
// as part of this popping process.
//
REBARR *Pop_Paramlist_With_Meta_May_Fail(
    REBCTX **meta,
    REBDSP dsp_orig,
    REBFLGS flags,
    REBDSP definitional_return_dsp
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
        if (definitional_return_dsp == 0) { // no explicit RETURN: pure local
            PUSH_SLOTS();

            Init_Word(KEY_SLOT(DSP), Canon(RETURN));
            definitional_return_dsp = DSP;

            STKVAL(*) param = PARAM_SLOT(DSP);

            // By default, you can return anything.  This goes with the bias
            // that checks happen on the reading side of things, not writing.
            //
            Init_Param(
                param,
                FLAG_PARAM_CLASS_BYTE(PARAM_CLASS_RETURN)
                    | PARAM_FLAG_ENDABLE,  // return/void ok
                TS_OPT_VALUE
            );

            Init_Nulled(TYPES_SLOT(DSP));
            Init_Nulled(NOTES_SLOT(DSP));
        }
        else {
            assert(
                VAL_WORD_ID(KEY_SLOT(definitional_return_dsp)) == SYM_RETURN
            );
        }

        // definitional_return handled specially when paramlist copied
        // off of the stack...moved to head position.

        flags |= MKF_HAS_RETURN;
    }

    // Slots, which is length +1 (includes the rootvar or rootparam)
    //
    assert((DSP - dsp_orig) % 4 == 0);
    REBLEN num_slots = (DSP - dsp_orig) / 4;

    // Must make the function "paramlist" even if "empty", for identity.
    //
    // !!! This is no longer true, since details is the identity.  Review
    // optimization potential.
    //
    REBARR *paramlist = Make_Array_Core(
        num_slots,
        SERIES_MASK_PARAMLIST
    );

    REBSER *keylist = Make_Series(
        (num_slots - 1),  // - 1 archetype
        SERIES_MASK_KEYLIST | NODE_FLAG_MANAGED
    );
    mutable_LINK(Ancestor, keylist) = keylist;  // chain ends with self

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

    const REBSYM *duplicate = nullptr;

  blockscope {
    REBVAL *param = Init_Isotope(ARR_HEAD(paramlist), Canon(ROOTVAR)) + 1;
    REBKEY *key = SER_HEAD(REBKEY, keylist);

    if (definitional_return_dsp != 0) {
        assert(flags & MKF_RETURN);
        Init_Key(key, VAL_WORD_SYMBOL(KEY_SLOT(definitional_return_dsp)));
        ++key;

        Copy_Cell(param, PARAM_SLOT(definitional_return_dsp));
        ++param;
    }

    REBDSP dsp = dsp_orig + 8;
    for (; dsp <= DSP; dsp += 4) {
        const REBSYM *symbol = VAL_WORD_SYMBOL(KEY_SLOT(dsp));

        STKVAL(*) slot = PARAM_SLOT(dsp);

        // "Sealed" parameters do not count in the binding.  See AUGMENT for
        // notes on why we do this (you can augment a function that has a
        // local called `x` with a new parameter called `x`, and that's legal.)
        //
        if (GET_CELL_FLAG(slot, STACK_NOTE_SEALED)) {
            assert(Is_Specialized(cast(REBPAR*, cast(REBVAL*, slot))));

            // !!! This flag was being set on an uninitialized param, with the
            // remark "survives copy over".  But the copy puts the flag on
            // regardless below.  Was this specific to RETURNs?
            //
            SET_END(param);
            SET_CELL_FLAG(param, VAR_MARKED_HIDDEN);
        }
        else {
            if (not Try_Add_Binder_Index(&binder, symbol, 1020))
                duplicate = symbol;
        }

        if (dsp == definitional_return_dsp)
            continue;  // was added to the head of the list already

        Init_Key(key, symbol);

        Copy_Cell_Core(
            param,
            slot,
            CELL_MASK_COPY | CELL_FLAG_VAR_MARKED_HIDDEN
        );

      #if !defined(NDEBUG)
        SET_CELL_FLAG(param, PROTECTED);
      #endif

        ++key;
        ++param;
    }

    SET_SERIES_LEN(paramlist, num_slots);
    Manage_Series(paramlist);

    SET_SERIES_USED(keylist, num_slots - 1);  // no terminator
    INIT_LINK_KEYSOURCE(paramlist, keylist);
    mutable_MISC(VarlistMeta, paramlist) = nullptr;
    mutable_BONUS(Patches, paramlist) = nullptr;
  }

    // Must remove binder indexes for all words, even if about to fail
    //
  blockscope {
    const REBKEY *tail = SER_TAIL(REBKEY, keylist);
    const REBKEY *key = SER_HEAD(REBKEY, keylist);
    const REBPAR *param = SER_AT(REBPAR, paramlist, 1);
    for (; key != tail; ++key, ++param) {
        //
        // See notes in AUGMENT on why we don't do binder indices on "sealed"
        // arguments (we can add `x` to the interface of a func with local `x`)
        //
        if (GET_CELL_FLAG(param, VAR_MARKED_HIDDEN)) {
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
    // BUILD META INFORMATION OBJECT (IF NEEDED)
    //
    //=///////////////////////////////////////////////////////////////////=//

    // !!! See notes on ACTION-META in %sysobj.r

    if (flags & (MKF_HAS_DESCRIPTION | MKF_HAS_TYPES | MKF_HAS_NOTES))
        *meta = Copy_Context_Shallow_Managed(VAL_CONTEXT(Root_Action_Meta));
    else
        *meta = nullptr;

    // If a description string was gathered, it's sitting in the first string
    // slot, the third cell we pushed onto the stack.  Extract it if so.
    //
    if (flags & MKF_HAS_DESCRIPTION) {
        STKVAL(*) description = NOTES_SLOT(dsp_orig + 4);
        assert(IS_TEXT(description));
        Copy_Cell(
            CTX_VAR(*meta, STD_ACTION_META_DESCRIPTION),
            description
        );
    }

    // Only make `parameter-types` if there were blocks in the spec
    //
    if (flags & MKF_HAS_TYPES) {
        REBARR *types_varlist = Make_Array_Core(
            num_slots,
            SERIES_MASK_VARLIST | NODE_FLAG_MANAGED
        );
        mutable_MISC(VarlistMeta, types_varlist) = nullptr;
        mutable_BONUS(Patches, types_varlist) = nullptr;
        INIT_CTX_KEYLIST_SHARED(CTX(types_varlist), keylist);

        RELVAL *rootvar = ARR_HEAD(types_varlist);
        INIT_VAL_CONTEXT_ROOTVAR(rootvar, REB_OBJECT, types_varlist);

        REBVAL *dest = SPECIFIC(rootvar) + 1;
        const RELVAL *param = ARR_AT(paramlist, 1);

        if (definitional_return_dsp != 0) {
            assert(flags & MKF_RETURN);
            ++param;

            Copy_Cell(dest, TYPES_SLOT(definitional_return_dsp));
            ++dest;
        }

        REBDSP dsp = dsp_orig + 8;
        for (; dsp <= DSP; dsp += 4) {
            STKVAL(*) types = TYPES_SLOT(dsp);
            assert(IS_NULLED(types) or IS_BLOCK(types));

            if (dsp == definitional_return_dsp)
                continue;  // was added to the head of the list already

            Copy_Cell(dest, types);

            ++dest;
            ++param;
        }

        SET_SERIES_LEN(types_varlist, num_slots);

        Init_Object(
            CTX_VAR(*meta, STD_ACTION_META_PARAMETER_TYPES),
            CTX(types_varlist)
        );
    }

    // Only make `parameter-notes` if there were strings (besides description)
    //
    if (flags & MKF_HAS_NOTES) {
        REBARR *notes_varlist = Make_Array_Core(
            num_slots,
            SERIES_MASK_VARLIST | NODE_FLAG_MANAGED
        );
        mutable_MISC(VarlistMeta, notes_varlist) = nullptr;
        mutable_BONUS(Patches, notes_varlist) = nullptr;
        INIT_CTX_KEYLIST_SHARED(CTX(notes_varlist), keylist);

        RELVAL *rootvar = ARR_HEAD(notes_varlist);
        INIT_VAL_CONTEXT_ROOTVAR(rootvar, REB_OBJECT, notes_varlist);

        const RELVAL *param = ARR_AT(paramlist, 1);
        REBVAL *dest = SPECIFIC(rootvar) + 1;

        if (definitional_return_dsp != 0) {
            assert(flags & MKF_RETURN);
            ++param;

            Copy_Cell(dest, NOTES_SLOT(definitional_return_dsp));
            ++dest;
        }

        REBDSP dsp = dsp_orig + 8;
        for (; dsp <= DSP; dsp += 4) {
            STKVAL(*) notes = NOTES_SLOT(dsp);
            assert(IS_TEXT(notes) or IS_NULLED(notes));

            if (dsp == definitional_return_dsp)
                continue;  // was added to the head of the list already

            Copy_Cell(dest, notes);

            ++dest;
            ++param;
        }

        SET_SERIES_LEN(notes_varlist, num_slots);

        Init_Object(
            CTX_VAR(*meta, STD_ACTION_META_PARAMETER_NOTES),
            CTX(notes_varlist)
        );
    }

    // With all the values extracted from stack to array, restore stack pointer
    //
    DS_DROP_TO(dsp_orig);

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
REBARR *Make_Paramlist_Managed_May_Fail(
    REBCTX **meta,
    const REBVAL *spec,
    REBFLGS *flags  // flags may be modified to carry additional information
){
    REBDSP dsp_orig = DSP;
    assert(DS_TOP == DS_AT(dsp_orig));

    REBDSP definitional_return_dsp = 0;

    PUSH_SLOTS();

    // As we go through the spec block, we push TYPESET! BLOCK! TEXT! triples.
    // These will be split out into separate arrays after the process is done.
    // The first slot of the paramlist needs to be the function canon value,
    // while the other two first slots need to be rootkeys.  Get the process
    // started right after a BLOCK! so it's willing to take a string for
    // the function description--it will be extracted from the slot before
    // it is turned into a rootkey for param_notes.
    //
    Init_None(KEY_SLOT(DSP));  // signal for no parameters pushed
    Init_Trash(PARAM_SLOT(DSP));  // not used at all
    Init_Trash(TYPES_SLOT(DSP));  // not used at all
    Init_Nulled(NOTES_SLOT(DSP));  // overwritten if description

    // The process is broken up into phases so that the spec analysis code
    // can be reused in AUGMENT.
    //
    Push_Paramlist_Triads_May_Fail(
        spec,
        flags,
        &definitional_return_dsp
    );
    REBARR *paramlist = Pop_Paramlist_With_Meta_May_Fail(
        meta,
        dsp_orig,
        *flags,
        definitional_return_dsp
    );

    return paramlist;
}


//
//  Make_Action: C
//
// Create an archetypal form of a function, given C code implementing a
// dispatcher that will be called by Eval_Core.  Dispatchers are of the form:
//
//     const REBVAL *Dispatcher(REBFRM *f) {...}
//
// The REBACT returned is "archetypal" because individual REBVALs which hold
// the same REBACT may differ in a per-REBVAL "binding".  (This is how one
// RETURN is distinguished from another--the binding data stored in the REBVAL
// identifies the pointer of the FRAME! to exit).
//
// Actions have an associated REBARR of data, accessible via ACT_DETAILS().
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
REBACT *Make_Action(
    REBARR *specialty,  // paramlist, exemplar, partials -> exemplar/paramlist
    REBNAT dispatcher,  // native C function called by Eval_Core
    REBLEN details_capacity  // capacity of ACT_DETAILS (including archetype)
){
    assert(details_capacity >= 1);  // must have room for archetype

    REBARR *paramlist;
    if (IS_PARTIALS(specialty)) {
        paramlist = CTX_VARLIST(LINK(PartialsExemplar, specialty));
    }
    else {
        assert(IS_VARLIST(specialty));
        paramlist = specialty;
    }

    assert(GET_SERIES_FLAG(paramlist, MANAGED));
    assert(
        Is_Isotope_With_Id(ARR_HEAD(paramlist), SYM_ROOTVAR)  // must fill in
        or CTX_TYPE(CTX(paramlist)) == REB_FRAME
    );

    // !!! There used to be more validation code needed here when it was
    // possible to pass a specialization frame separately from a paramlist.
    // But once paramlists were separated out from the function's identity
    // array (using ACT_DETAILS() as the identity instead of ACT_KEYLIST())
    // then all the "shareable" information was glommed up minus redundancy
    // into the ACT_SPECIALTY().  Here's some of the residual checking, as
    // a placeholder for more useful consistency checking which might be done.
    //
  blockscope {
    REBSER *keylist = SER(LINK(KeySource, paramlist));

    ASSERT_SERIES_MANAGED(keylist);  // paramlists/keylists, can be shared
    assert(SER_USED(keylist) + 1 == ARR_LEN(paramlist));
    if (GET_SUBCLASS_FLAG(VARLIST, paramlist, PARAMLIST_HAS_RETURN)) {
        const REBKEY *key = SER_AT(const REBKEY, keylist, 0);
        assert(KEY_SYM(key) == SYM_RETURN);
        UNUSED(key);
    }
  }

    // "details" for an action is an array of cells which can be anything
    // the dispatcher understands it to be, by contract.  Terminate it
    // at the given length implicitly.
    //
    REBARR *details = Make_Array_Core(
        details_capacity,  // leave room for archetype
        SERIES_MASK_DETAILS | NODE_FLAG_MANAGED
    );
    RELVAL *archetype = ARR_HEAD(details);
    Reset_Cell_Header_Untracked(TRACK(archetype), REB_ACTION, CELL_MASK_ACTION);
    INIT_VAL_ACTION_DETAILS(archetype, details);
    mutable_BINDING(archetype) = UNBOUND;

  #if !defined(NDEBUG)  // notice attempted mutation of the archetype cell
    SET_CELL_FLAG(archetype, PROTECTED);
  #endif

    // Leave rest of the cells in the capacity uninitialized (caller fills in)
    //
    SET_SERIES_LEN(details, details_capacity);

    mutable_LINK_DISPATCHER(details) = cast(CFUNC*, dispatcher);
    mutable_MISC(DetailsMeta, details) = nullptr;  // caller can fill in

    INIT_VAL_ACTION_SPECIALTY_OR_LABEL(archetype, specialty);

    REBACT *act = ACT(details); // now it's a legitimate REBACT

    // !!! We may have to initialize the exemplar rootvar.
    //
    REBVAL *rootvar = SER_HEAD(REBVAL, paramlist);
    if (Is_Isotope_With_Id(rootvar, SYM_ROOTVAR)) {
        INIT_VAL_FRAME_ROOTVAR(rootvar, paramlist, act, UNBOUND);
    }

    // The exemplar needs to be frozen, it can't change after this point.
    // You can't change the types or parameter conventions of an existing
    // action...you have to make a new variation.  Note that the exemplar
    // can be exposed by AS FRAME! of this action...
    //
    Freeze_Array_Shallow(paramlist);

    // Precalculate cached function flags.  This involves finding the first
    // unspecialized argument which would be taken at a callsite, which can
    // be tricky to figure out with partial refinement specialization.  So
    // the work of doing that is factored into a routine (`PARAMETERS OF`
    // uses it as well).

    const REBPAR *first = First_Unspecialized_Param(nullptr, act);
    if (first) {
        switch (VAL_PARAM_CLASS(first)) {
          case PARAM_CLASS_RETURN:
          case PARAM_CLASS_OUTPUT:
          case PARAM_CLASS_NORMAL:
          case PARAM_CLASS_META:
            break;

          case PARAM_CLASS_SOFT:
          case PARAM_CLASS_MEDIUM:
          case PARAM_CLASS_HARD:
            SET_ACTION_FLAG(act, QUOTES_FIRST);
            break;

          default:
            assert(false);
        }

        if (GET_PARAM_FLAG(first, SKIPPABLE))
            SET_ACTION_FLAG(act, SKIPPABLE_FIRST);
    }

    return act;
}


//
//  Make_Expired_Frame_Ctx_Managed: C
//
// FUNC/PROC bodies contain relative words and relative arrays.  Arrays from
// this relativized body may only be put into a specified REBVAL once they
// have been combined with a frame.
//
// Reflection asks for action body data, when no instance is called.  Hence
// a REBVAL must be produced somehow.  If the body is being copied, then the
// option exists to convert all the references to unbound...but this isn't
// representative of the actual connections in the body.
//
// There could be an additional "archetype" state for the relative binding
// machinery.  But making a one-off expired frame is an inexpensive option.
//
REBCTX *Make_Expired_Frame_Ctx_Managed(REBACT *a)
{
    // Since passing SERIES_MASK_VARLIST includes SERIES_FLAG_DYNAMIC,
    // don't pass it in to the allocation...it needs to be set, but will be
    // overridden by SERIES_FLAG_INACCESSIBLE.
    //
    REBARR *varlist = Alloc_Singular(
        FLAG_FLAVOR(VARLIST) | NODE_FLAG_MANAGED  // !!! not dynamic
    );
    varlist->leader.bits |= SERIES_MASK_VARLIST;  // !!! adds dynamic
    CLEAR_SERIES_FLAG(varlist, DYNAMIC);  // !!! removes (review cleaner way)
    SET_SERIES_FLAG(varlist, INACCESSIBLE);
    mutable_MISC(VarlistMeta, varlist) = nullptr;
    // no BONUS (it's INACCESSIBLE, so non-dynamic)

    RELVAL *rootvar = ARR_SINGLE(varlist);
    INIT_VAL_FRAME_ROOTVAR(rootvar, varlist, a, UNBOUND);  // !!! binding?

    REBCTX *expired = CTX(varlist);
    INIT_CTX_KEYLIST_SHARED(expired, ACT_KEYLIST(a));

    return expired;
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
void Get_Maybe_Fake_Action_Body(REBVAL *out, const REBVAL *action)
{
    REBCTX *binding = VAL_ACTION_BINDING(action);
    REBACT *a = VAL_ACTION(action);

    // A Hijacker *might* not need to splice itself in with a dispatcher.
    // But if it does, bypass it to get to the "real" action implementation.
    //
    // !!! Should the source inject messages like {This is a hijacking} at
    // the top of the returned body?
    //
    while (ACT_DISPATCHER(a) == &Hijacker_Dispatcher) {
        a = VAL_ACTION(ARR_AT(ACT_DETAILS(a), 1));
        // !!! Review what should happen to binding
    }

    REBARR *details = ACT_DETAILS(a);

    // !!! Should the binding make a difference in the returned body?  It is
    // exposed programmatically via CONTEXT OF.
    //
    UNUSED(binding);

    if (
        ACT_DISPATCHER(a) == &None_Dispatcher
        or ACT_DISPATCHER(a) == &Empty_Dispatcher
        or ACT_DISPATCHER(a) == &Unchecked_Dispatcher
        or ACT_DISPATCHER(a) == &Opaque_Dispatcher
        or ACT_DISPATCHER(a) == &Returner_Dispatcher
        or ACT_DISPATCHER(a) == &Block_Dispatcher
    ){
        // Interpreted code, the body is a block with some bindings relative
        // to the action.

        RELVAL *body = ARR_AT(details, IDX_DETAILS_1);

        // The PARAMLIST_HAS_RETURN tricks for definitional return make it
        // seem like a generator authored more code in the action's body...but
        // the code isn't *actually* there and an optimized internal trick is
        // used.  Fake the code if needed.

        REBVAL *example;
        REBLEN real_body_index;
        if (ACT_DISPATCHER(a) == &Opaque_Dispatcher) {
            example = Get_System(SYS_STANDARD, STD_PROC_BODY);
            real_body_index = 4;
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

        const REBARR *maybe_fake_body;
        if (example == nullptr) {
            maybe_fake_body = VAL_ARRAY(body);
        }
        else {
            // See %sysobj.r for STANDARD/FUNC-BODY and STANDARD/PROC-BODY
            //
            REBARR *fake = Copy_Array_Shallow_Flags(
                VAL_ARRAY(example),
                VAL_SPECIFIER(example),
                NODE_FLAG_MANAGED
            );

            // Index 5 (or 4 in zero-based C) should be #BODY, a "real" body.
            // To give it the appearance of executing code in place, we use
            // a GROUP!.

            RELVAL *slot = ARR_AT(fake, real_body_index);  // #BODY
            assert(IS_ISSUE(slot));

            // Note: clears VAL_FLAG_LINE
            //
            Reset_Cell_Header_Untracked(
                TRACK(slot),
                REB_GROUP,
                CELL_FLAG_FIRST_IS_NODE
            );
            INIT_VAL_NODE1(slot, VAL_ARRAY(body));
            VAL_INDEX_RAW(slot) = 0;
            INIT_SPECIFIER(slot, a);  // relative binding

            maybe_fake_body = fake;
        }

        // Cannot give user a relative value back, so make the relative
        // body specific to a fabricated expired frame.  See #2221

        Reset_Cell_Header_Untracked(
            TRACK(out),
            REB_BLOCK,
            CELL_FLAG_FIRST_IS_NODE
        );
        INIT_VAL_NODE1(out, maybe_fake_body);
        VAL_INDEX_RAW(out) = 0;
        INIT_SPECIFIER(out, Make_Expired_Frame_Ctx_Managed(a));
        return;
    }

    if (ACT_DISPATCHER(a) == &Specializer_Dispatcher) {
        //
        // The FRAME! stored in the body for the specialization has a phase
        // which is actually the function to be run.
        //
        const REBVAL *frame = CTX_ARCHETYPE(ACT_EXEMPLAR(a));
        assert(IS_FRAME(frame));
        Copy_Cell(out, frame);
        return;
    }

    if (ACT_DISPATCHER(a) == &Generic_Dispatcher) {
        REBVAL *verb = DETAILS_AT(details, 1);
        assert(IS_WORD(verb));
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
    UNUSED(frame_);
    UNUSED(verb);

    fail ("Datatype does not have a dispatcher registered.");
}


//
//  Get_If_Word_Or_Path_Throws: C
//
// Originally this routine was used by APPLY and SPECIALIZE type routines
// to allow them to take WORD!s and PATH!s, since there had been no way to
// get the label for an ACTION! after it had been fetched:
//
//     >> applique 'append [value: 'c]  ; APPLIQUE would see the word APPEND
//     ** Script error: append is missing its series argument  ; so name here
//
// That became unnecessary once the mechanics behind VAL_ACTION_LABEL() were
// introduced.  So the interfaces were changed to only take ACTION!, so as
// to be more clear.
//
// This function remains as a utility for other purposes.  It is able to push
// refinements in the process of its fetching, if you want to avoid an
// intermediate specialization when used in apply-like scenarios.
//
bool Get_If_Word_Or_Path_Throws(
    REBVAL *out,
    const RELVAL *v,
    REBSPC *specifier,
    bool push_refinements
) {
    if (IS_WORD(v) or IS_GET_WORD(v)) {
      get_as_word:
        Get_Word_May_Fail(out, v, specifier);
        if (IS_ACTION(out))
            INIT_VAL_ACTION_LABEL(out, VAL_WORD_SYMBOL(v));
    }
    else if (
        IS_PATH(v) or IS_GET_PATH(v)
        or IS_TUPLE(v) or IS_GET_TUPLE(v)
    ){
        if (ANY_WORD_KIND(HEART_BYTE(v)))  // e.g. `/`
            goto get_as_word;  // faster than calling Eval_Path_Throws_Core?

        if (Eval_Path_Throws_Core(
            out,
            v,  // !!! may not be array based
            specifier,
            EVAL_MASK_DEFAULT | (push_refinements
                ? EVAL_FLAG_PUSH_PATH_REFINES  // pushed in reverse order
                : 0)
        )){
            return true;
        }
    }
    else
        Derelativize(out, v, specifier);

    return false;
}


//
//  tweak: native [
//
//  {Modify a special property (currently only for ACTION!)}
//
//      return: "Same action identity as input"
//          [action!]
//      action "(modified) Action to modify property of"
//          [action!]
//      property "Currently must be [defer postpone]"
//          [word!]
//      enable [logic!]
//  ]
//
REBNATIVE(tweak)
{
    INCLUDE_PARAMS_OF_TWEAK;

    REBACT *act = VAL_ACTION(ARG(action));
    const REBPAR *first = First_Unspecialized_Param(nullptr, act);

    enum Reb_Param_Class pclass = first
        ? VAL_PARAM_CLASS(first)
        : PARAM_CLASS_NORMAL;  // imagine it as <end>able

    REBFLGS flag;

    switch (VAL_WORD_ID(ARG(property))) {
      case SYM_BARRIER:   // don't allow being taken as an argument, e.g. |
        flag = DETAILS_FLAG_IS_BARRIER;
        break;

      case SYM_DEFER:  // Special enfix behavior used by THEN, ELSE, ALSO...
        if (pclass != PARAM_CLASS_NORMAL and pclass != PARAM_CLASS_META)
            fail ("TWEAK defer only actions with evaluative 1st params");
        flag = DETAILS_FLAG_DEFERS_LOOKBACK;
        break;

      case SYM_POSTPONE:  // Wait as long as it can to run w/o changing order
        if (
            pclass != PARAM_CLASS_NORMAL
            and pclass != PARAM_CLASS_SOFT
            and pclass != PARAM_CLASS_META
        ){
            fail ("TWEAK postpone only actions with evaluative 1st params");
        }
        flag = DETAILS_FLAG_POSTPONES_ENTIRELY;
        break;

      default:
        fail ("TWEAK currently only supports [barrier defer postpone]");
    }

    if (VAL_LOGIC(ARG(enable)))
        ACT_DETAILS(act)->leader.bits |= flag;
    else
        ACT_DETAILS(act)->leader.bits &= ~flag;

    RETURN (ARG(action));
}
