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
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"


struct Params_Of_State {
    REBARR *arr;
    REBCNT num_visible;
    RELVAL *dest;
};

// Reconstitute parameter back into a full value, e.g. REB_P_REFINEMENT
// becomes `/spelling`.
//
// !!! See notes on Is_Param_Hidden() for why caller isn't filtering locals.
//
static bool Params_Of_Hook(
    REBVAL *param,
    bool sorted_pass,
    void *opaque
){
    struct Params_Of_State *s = cast(struct Params_Of_State*, opaque);

    if (not sorted_pass) { // first pass we just count unspecialized params
        ++s->num_visible;
        return true;
    }

    if (not s->arr) { // if first step on second pass, make the array
        s->arr = Make_Array(s->num_visible);
        s->dest = ARR_HEAD(s->arr);
    }

    Init_Any_Word(s->dest, REB_WORD, VAL_PARAM_SPELLING(param));

    if (TYPE_CHECK(param, REB_TS_REFINEMENT))
        Refinify(KNOWN(s->dest));

    switch (VAL_PARAM_CLASS(param)) {
      case REB_P_NORMAL:
        break;

      case REB_P_HARD_QUOTE:
        Getify(KNOWN(s->dest));
        break;

      case REB_P_SOFT_QUOTE:
        Quotify(KNOWN(s->dest), 1);
        break;

      default:
        assert(false);
        DEAD_END;
    }

    ++s->dest;
    return true;
}

//
//  Make_Action_Parameters_Arr: C
//
// Returns array of function words, unbound.
//
REBARR *Make_Action_Parameters_Arr(REBACT *act)
{
    struct Params_Of_State s;
    s.arr = nullptr;
    s.num_visible = 0;

    For_Each_Unspecialized_Param(act, &Params_Of_Hook, &s);

    if (not s.arr)
        return Make_Array(1); // no unspecialized parameters, empty array

    TERM_ARRAY_LEN(s.arr, s.num_visible);
    ASSERT_ARRAY(s.arr);
    return s.arr;
}


static bool Typesets_Of_Hook(
    REBVAL *param,
    bool sorted_pass,
    void *opaque
){
    struct Params_Of_State *s = cast(struct Params_Of_State*, opaque);

    if (not sorted_pass) { // first pass we just count unspecialized params
        ++s->num_visible;
        return true;
    }

    if (not s->arr) { // if first step on second pass, make the array
        s->arr = Make_Array(s->num_visible);
        s->dest = ARR_HEAD(s->arr);
    }

    // It's already a typeset, but remove the parameter spelling.
    //
    // !!! Typesets must be revisited in a world with user-defined types, as
    // well as to accomodate multiple quoting levels.
    //
    Move_Value(s->dest, param);
    assert(IS_TYPESET(s->dest));
    VAL_TYPESET_STRING_NODE(s->dest) = nullptr;
    ++s->dest;

    return true;
}

//
//  Make_Action_Typesets_Arr: C
//
// Return a block of function arg typesets.
// Note: skips 0th entry.
//
REBARR *Make_Action_Typesets_Arr(REBACT *act)
{
    struct Params_Of_State s;
    s.arr = nullptr;
    s.num_visible = 0;

    For_Each_Unspecialized_Param(act, &Typesets_Of_Hook, &s);

    if (not s.arr)
        return Make_Array(1); // no unspecialized parameters, empty array

    TERM_ARRAY_LEN(s.arr, s.num_visible);
    ASSERT_ARRAY(s.arr);
    return s.arr;
}


enum Reb_Spec_Mode {
    SPEC_MODE_NORMAL, // words are arguments
    SPEC_MODE_LOCAL, // words are locals
    SPEC_MODE_WITH // words are "extern"
};


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
    const REBVAL *spec,
    REBFLGS flags
) {
    assert(IS_BLOCK(spec));

    REBDSP dsp_orig = DSP;
    assert(DS_TOP == DS_AT(dsp_orig));

    REBDSP definitional_return_dsp = 0;

    // As we go through the spec block, we push TYPESET! BLOCK! TEXT! triples.
    // These will be split out into separate arrays after the process is done.
    // The first slot of the paramlist needs to be the function canon value,
    // while the other two first slots need to be rootkeys.  Get the process
    // started right after a BLOCK! so it's willing to take a string for
    // the function description--it will be extracted from the slot before
    // it is turned into a rootkey for param_notes.
    //
    Init_Unreadable_Blank(DS_PUSH()); // paramlist[0] becomes ACT_ARCHETYPE()
    Move_Value(DS_PUSH(), EMPTY_BLOCK); // param_types[0] (object canon)
    Move_Value(DS_PUSH(), EMPTY_TEXT); // param_notes[0] (desc, then canon)

    bool has_description = false;
    bool has_types = false;
    bool has_notes = false;

    bool is_voider = false;
    bool has_return = false;

    enum Reb_Spec_Mode mode = SPEC_MODE_NORMAL;

    bool refinement_seen = false;

    const RELVAL *value = VAL_ARRAY_AT(spec);

    while (NOT_END(value)) {
        const RELVAL *item = value; // "faked", e.g. <return> => RETURN:
        ++value; // go ahead and consume next

    //=//// STRING! FOR FUNCTION DESCRIPTION OR PARAMETER NOTE ////////////=//

        if (IS_TEXT(item)) {
            //
            // Consider `[<with> some-extern "description of that extern"]` to
            // be purely commentary for the implementation, and don't include
            // it in the meta info.
            //
            if (mode == SPEC_MODE_WITH)
                continue;

            if (IS_PARAM(DS_TOP))
                Move_Value(DS_PUSH(), EMPTY_BLOCK); // need block in position

            if (IS_BLOCK(DS_TOP)) { // we're in right spot to push notes/title
                Init_Text(DS_PUSH(),  Copy_String_At(item));
            }
            else { // !!! A string was already pushed.  Should we append?
                assert(IS_TEXT(DS_TOP));
                Init_Text(DS_TOP, Copy_String_At(item));
            }

            if (DS_TOP == DS_AT(dsp_orig + 3))
                has_description = true;
            else
                has_notes = true;

            continue;
        }

    //=//// TOP-LEVEL SPEC TAGS LIKE <local>, <with> etc. /////////////////=//

        if (IS_TAG(item) and (flags & MKF_KEYWORDS)) {
            if (0 == Compare_String_Vals(item, Root_With_Tag, true)) {
                mode = SPEC_MODE_WITH;
                continue;
            }
            else if (0 == Compare_String_Vals(item, Root_Local_Tag, true)) {
                mode = SPEC_MODE_LOCAL;
                continue;
            }
            else if (0 == Compare_String_Vals(item, Root_Void_Tag, true)) {
                is_voider = true; // use Voider_Dispatcher()

                // Fake as if they said [void!] !!! make more efficient
                //
                item = Get_System(SYS_STANDARD, STD_PROC_RETURN_TYPE);
                goto process_typeset_block;
            }
            else
                fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));
        }

    //=//// BLOCK! OF TYPES TO MAKE TYPESET FROM (PLUS PARAMETER TAGS) ////=//

        if (IS_BLOCK(item)) {
          process_typeset_block:
            if (IS_BLOCK(DS_TOP)) // two blocks of types!
                fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));

            // You currently can't say `<local> x [integer!]`, because they
            // are always void when the function runs.  You can't say
            // `<with> x [integer!]` because "externs" don't have param slots
            // to store the type in.
            //
            // !!! A type constraint on a <with> parameter might be useful,
            // though--and could be achieved by adding a type checker into
            // the body of the function.  However, that would be more holistic
            // than this generation of just a paramlist.  Consider for future.
            //
            if (mode != SPEC_MODE_NORMAL)
                fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));

            // Save the block for parameter types.
            //
            REBVAL *param;
            if (IS_PARAM(DS_TOP)) {
                REBSPC *derived = Derive_Specifier(VAL_SPECIFIER(spec), item);
                Init_Block(
                    DS_PUSH(),
                    Copy_Array_At_Deep_Managed(
                        VAL_ARRAY(item),
                        VAL_INDEX(item),
                        derived
                    )
                );

                param = DS_TOP - 1; // volatile if you DS_PUSH()!
            }
            else {
                assert(IS_TEXT(DS_TOP)); // !!! are blocks after notes good?

                if (IS_BLANK_RAW(DS_TOP - 2)) {
                    //
                    // No parameters pushed, e.g. func [[integer!] {<-- bad}]
                    //
                    fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));
                }

                assert(IS_PARAM(DS_TOP - 2));
                param = DS_TOP - 2;

                assert(IS_BLOCK(DS_TOP - 1));
                if (VAL_ARRAY(DS_TOP - 1) != EMPTY_ARRAY)
                    fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));

                REBSPC *derived = Derive_Specifier(VAL_SPECIFIER(spec), item);
                Init_Block(
                    DS_TOP - 1,
                    Copy_Array_At_Deep_Managed(
                        VAL_ARRAY(item),
                        VAL_INDEX(item),
                        derived
                    )
                );
            }

            // Turn block into typeset for parameter at current index.
            // Leaves VAL_TYPESET_SYM as-is.
            //
            bool was_refinement = TYPE_CHECK(param, REB_TS_REFINEMENT);
            REBSPC *derived = Derive_Specifier(VAL_SPECIFIER(spec), item);
            VAL_TYPESET_LOW_BITS(param) = 0;
            VAL_TYPESET_HIGH_BITS(param) = 0;
            Add_Typeset_Bits_Core(
                param,
                VAL_ARRAY_HEAD(item),
                derived
            );
            if (was_refinement)
                TYPE_SET(param, REB_TS_REFINEMENT);

            has_types = true;
            continue;
        }

    //=//// ANY-WORD! PARAMETERS THEMSELVES (MAKE TYPESETS w/SYMBOL) //////=//

        bool quoted = false;  // single quoting level used as signal in spec
        if (VAL_NUM_QUOTES(item) > 0) {
            if (VAL_NUM_QUOTES(item) > 1)
                fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));
            quoted = true;
        }

        const REBCEL *cell = VAL_UNESCAPED(item);

        REBSTR *spelling;
        Reb_Param_Class pclass = REB_P_DETECT;

        bool refinement = false;  // paths with blanks at head are refinements
        if (ANY_PATH_KIND(CELL_KIND(cell))) {
            if (
                KIND_BYTE(VAL_ARRAY_AT(cell)) != REB_BLANK
                or KIND_BYTE(VAL_ARRAY_AT(cell) + 1) != REB_WORD
                or KIND_BYTE(VAL_ARRAY_AT(cell) + 2) != REB_0_END
            ){
                fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));
            }

            refinement = true;
            refinement_seen = true;

            // !!! If you say [<with> x /foo y] the <with> terminates and a
            // refinement is started.  Same w/<local>.  Is this a good idea?
            // Note that historically, help hides any refinements that appear
            // behind a /local, but this feature has no parallel in Ren-C.
            //
            mode = SPEC_MODE_NORMAL;

            spelling = VAL_WORD_SPELLING(VAL_ARRAY_AT(cell) + 1);
            if (STR_SYMBOL(spelling) == SYM_LOCAL)  // /local
                if (ANY_WORD_KIND(KIND_BYTE(item + 1)))  // END is 0
                    fail (Error_Legacy_Local_Raw(spec));  // -> <local>

            if (CELL_KIND(cell) == REB_GET_PATH) {
                if (not quoted)
                    pclass = REB_P_HARD_QUOTE;
            }
            else if (CELL_KIND(cell) == REB_PATH) {
                if (quoted)
                    pclass = REB_P_SOFT_QUOTE;
                else
                    pclass = REB_P_NORMAL;
            }
        }
        else if (ANY_WORD_KIND(CELL_KIND(cell))) {
            spelling = VAL_WORD_SPELLING(cell);
            if (CELL_KIND(cell) == REB_SET_WORD) {
                if (not quoted)
                    pclass = REB_P_LOCAL;
            }
            else {
                if (refinement_seen and mode == SPEC_MODE_NORMAL)
                    fail (Error_Legacy_Refinement_Raw(spec));

                if (CELL_KIND(cell) == REB_GET_WORD) {
                    if (not quoted)
                        pclass = REB_P_HARD_QUOTE;
                }
                else if (CELL_KIND(cell) == REB_WORD) {
                    if (quoted)
                        pclass = REB_P_SOFT_QUOTE;
                    else
                        pclass = REB_P_NORMAL;
                }
            }
        }
        else
            fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));

        if (pclass == REB_P_DETECT)  // didn't match
            fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));

        if (mode != SPEC_MODE_NORMAL) {
            if (pclass != REB_P_NORMAL and pclass != REB_P_LOCAL)
                fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));

            if (mode == SPEC_MODE_LOCAL)
                pclass = REB_P_LOCAL;
        }

        REBSTR *canon = STR_CANON(spelling);
        if (STR_SYMBOL(canon) == SYM_RETURN and pclass != REB_P_LOCAL) {
            //
            // Cancel definitional return if any non-SET-WORD! uses the name
            // RETURN when defining a FUNC.
            //
            flags &= ~MKF_RETURN;
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

        // In rhythm of TYPESET! BLOCK! TEXT! we want to be on a string spot
        // at the time of the push of each new typeset.
        //
        if (IS_PARAM(DS_TOP))
            Move_Value(DS_PUSH(), EMPTY_BLOCK);
        if (IS_BLOCK(DS_TOP))
            Move_Value(DS_PUSH(), EMPTY_TEXT);
        assert(IS_TEXT(DS_TOP));

        // Non-annotated arguments disallow ACTION!, VOID! and NULL.  Not
        // having to worry about ACTION! and NULL means by default, code
        // does not have to worry about "disarming" arguments via GET-WORD!.
        // Also, keeping NULL a bit "prickly" helps discourage its use as
        // an input parameter...because it faces problems being used in
        // SPECIALIZE and other scenarios.
        //
        // Note there are currently two ways to get NULL: <opt> and <end>.
        // If the typeset bits contain REB_NULLED, that indicates <opt>.
        // But Is_Param_Endable() indicates <end>.

        if (refinement) {
            Init_Param(
                DS_PUSH(),
                pclass,
                spelling,  // don't canonize, see #2258
                FLAGIT_KIND(REB_TS_REFINEMENT)  // must preserve if type block
            );
        }
        else
            Init_Param(
                DS_PUSH(),
                pclass,
                spelling,  // don't canonize, see #2258
                (flags & MKF_ANY_VALUE)
                    ? TS_OPT_VALUE
                    : TS_VALUE & ~(
                        FLAGIT_KIND(REB_ACTION)
                        | FLAGIT_KIND(REB_VOID)
                    )
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
        if (STR_SYMBOL(canon) == SYM_RETURN) {
            if (definitional_return_dsp != 0) {
                DECLARE_LOCAL (word);
                Init_Word(word, canon);
                fail (Error_Dup_Vars_Raw(word)); // most dup checks done later
            }
            if (pclass == REB_P_LOCAL)
                definitional_return_dsp = DSP; // RETURN: explicitly tolerated
            else
                flags &= ~MKF_RETURN;
        }
    }

    // Go ahead and flesh out the TYPESET! BLOCK! TEXT! triples.
    //
    if (IS_PARAM(DS_TOP))
        Move_Value(DS_PUSH(), EMPTY_BLOCK);
    if (IS_BLOCK(DS_TOP))
        Move_Value(DS_PUSH(), EMPTY_TEXT);
    assert((DSP - dsp_orig) % 3 == 0); // must be a multiple of 3

    // Definitional RETURN slots must have their argument value fulfilled with
    // an ACTION! specific to the action called on *every instantiation*.
    // They are marked with special parameter classes to avoid needing to
    // separately do canon comparison of their symbols to find them.  In
    // addition, since RETURN's typeset holds types that need to be checked at
    // the end of the function run, it is moved to a predictable location:
    // last slot of the paramlist.
    //
    // !!! The ability to add locals anywhere in the frame exists to make it
    // possible to expand frames, so it might work to put it in the first
    // slot--these mechanisms should have some review.

    if (flags & MKF_RETURN) {
        if (definitional_return_dsp == 0) { // no explicit RETURN: pure local
            //
            // While default arguments disallow ACTION!, VOID!, and NULL...
            // they are allowed to return anything.  Generally speaking, the
            // checks are on the input side, not the output.
            //
            Init_Param(
                DS_PUSH(),
                REB_P_RETURN,
                Canon(SYM_RETURN),
                TS_OPT_VALUE
            );
            definitional_return_dsp = DSP;

            Move_Value(DS_PUSH(), EMPTY_BLOCK);
            Move_Value(DS_PUSH(), EMPTY_TEXT);
            // no need to move it--it's already at the tail position
        }
        else {
            REBVAL *param = DS_AT(definitional_return_dsp);

            assert(VAL_PARAM_CLASS(param) == REB_P_LOCAL);
            mutable_KIND_BYTE(param) = REB_P_RETURN;

            assert(MIRROR_BYTE(param) == REB_TYPESET);

            // definitional_return handled specially when paramlist copied
            // off of the stack...
        }
        has_return = true;
    }

    // Slots, which is length +1 (includes the rootvar or rootparam)
    //
    REBCNT num_slots = (DSP - dsp_orig) / 3;

    // There should be no more pushes past this point, so a stable pointer
    // into the stack for the definitional return can be found.
    //
    REBVAL *definitional_return =
        definitional_return_dsp == 0
            ? NULL
            : DS_AT(definitional_return_dsp);

    // Must make the function "paramlist" even if "empty", for identity.
    //
    REBARR *paramlist = Make_Array_Core(num_slots, SERIES_MASK_PARAMLIST);

    // Note: not a valid ACTION! paramlist yet, don't use SET_ACTION_FLAG()
    //
    if (is_voider)
        SER(paramlist)->info.bits |= ARRAY_INFO_MISC_VOIDER;  // !!! see note
    if (has_return)
        SER(paramlist)->header.bits |= PARAMLIST_FLAG_HAS_RETURN;

    if (true) {
        REBVAL *archetype = RESET_CELL(
            ARR_HEAD(paramlist),
            REB_ACTION,
            CELL_MASK_ACTION
        );
        VAL_ACT_PARAMLIST_NODE(archetype) = NOD(paramlist);
        INIT_BINDING(archetype, UNBOUND);

        REBVAL *dest = archetype + 1;

        // We want to check for duplicates and a Binder can be used for that
        // purpose--but note that a fail() cannot happen while binders are
        // in effect UNLESS the BUF_COLLECT contains information to undo it!
        // There's no BUF_COLLECT here, so don't fail while binder in effect.
        //
        // (This is why we wait until the parameter list gathering process
        // is over to do the duplicate checks--it can fail.)
        //
        struct Reb_Binder binder;
        INIT_BINDER(&binder);

        REBSTR *duplicate = NULL;

        REBVAL *src = DS_AT(dsp_orig + 1) + 3;

        // Weird due to Spectre/MSVC: https://stackoverflow.com/q/50399940
        //
        for (; src != DS_TOP + 1; src += 3) {
            if (not Try_Add_Binder_Index(&binder, VAL_PARAM_CANON(src), 1020))
                duplicate = VAL_PARAM_SPELLING(src);

            if (definitional_return and src == definitional_return)
                continue;

            Move_Value(dest, src);
            ++dest;
        }

        if (definitional_return) {
            assert(flags & MKF_RETURN);
            Move_Value(dest, definitional_return);
            ++dest;
        }

        // Must remove binder indexes for all words, even if about to fail
        //
        src = DS_AT(dsp_orig + 1) + 3;

        // Weird due to Spectre/MSVC: https://stackoverflow.com/q/50399940
        //
        for (; src != DS_TOP + 1; src += 3, ++dest) {
            if (
                Remove_Binder_Index_Else_0(&binder, VAL_PARAM_CANON(src))
                == 0
            ){
                assert(duplicate);
            }
        }

        SHUTDOWN_BINDER(&binder);

        if (duplicate) {
            DECLARE_LOCAL (word);
            Init_Word(word, duplicate);
            fail (Error_Dup_Vars_Raw(word));
        }

        TERM_ARRAY_LEN(paramlist, num_slots);
        Manage_Array(paramlist);
    }

    //=///////////////////////////////////////////////////////////////////=//
    //
    // BUILD META INFORMATION OBJECT (IF NEEDED)
    //
    //=///////////////////////////////////////////////////////////////////=//

    // !!! See notes on ACTION-META in %sysobj.r

    REBCTX *meta = nullptr;

    if (has_description or has_types or has_notes)
        meta = Copy_Context_Shallow_Managed(VAL_CONTEXT(Root_Action_Meta));

    MISC_META_NODE(paramlist) = NOD(meta);

    // If a description string was gathered, it's sitting in the first string
    // slot, the third cell we pushed onto the stack.  Extract it if so.
    //
    if (has_description) {
        assert(IS_TEXT(DS_AT(dsp_orig + 3)));
        Move_Value(
            CTX_VAR(meta, STD_ACTION_META_DESCRIPTION),
            DS_AT(dsp_orig + 3)
        );
    }

    // Only make `parameter-types` if there were blocks in the spec
    //
    if (has_types) {
        REBARR *types_varlist = Make_Array_Core(
            num_slots,
            SERIES_MASK_VARLIST | NODE_FLAG_MANAGED
        );
        MISC_META_NODE(types_varlist) = nullptr;  // GC sees, must initialize
        INIT_CTX_KEYLIST_SHARED(CTX(types_varlist), paramlist);

        REBVAL *rootvar = RESET_CELL(
            ARR_HEAD(types_varlist),
            REB_FRAME,
            CELL_MASK_CONTEXT
        );
        INIT_VAL_CONTEXT_VARLIST(rootvar, types_varlist);  // "canon FRAME!"
        INIT_VAL_CONTEXT_PHASE(rootvar, ACT(paramlist));
        INIT_BINDING(rootvar, UNBOUND);

        REBVAL *dest = rootvar + 1;

        REBVAL *src = DS_AT(dsp_orig + 2);
        src += 3;
        for (; src <= DS_TOP; src += 3) {
            assert(IS_BLOCK(src));
            if (definitional_return and src == definitional_return + 1)
                continue;

            if (VAL_ARRAY_LEN_AT(src) == 0)
                Init_Nulled(dest);
            else
                Move_Value(dest, src);
            ++dest;
        }

        if (definitional_return) {
            //
            // We put the return note in the top-level meta information, not
            // on the local itself (the "return-ness" is a distinct property
            // of the function from what word is used for RETURN:, and it
            // is possible to use the word RETURN for a local or refinement
            // argument while having nothing to do with the exit value of
            // the function.)
            //
            if (VAL_ARRAY_LEN_AT(definitional_return + 1) != 0) {
                Move_Value(
                    CTX_VAR(meta, STD_ACTION_META_RETURN_TYPE),
                    &definitional_return[1]
                );
            }

            Init_Nulled(dest); // clear the local RETURN: var's description
            ++dest;
        }

        TERM_ARRAY_LEN(types_varlist, num_slots);

        Init_Any_Context(
            CTX_VAR(meta, STD_ACTION_META_PARAMETER_TYPES),
            REB_FRAME,
            CTX(types_varlist)
        );
    }

    // Only make `parameter-notes` if there were strings (besides description)
    //
    if (has_notes) {
        REBARR *notes_varlist = Make_Array_Core(
            num_slots,
            SERIES_MASK_VARLIST | NODE_FLAG_MANAGED
        );
        MISC_META_NODE(notes_varlist) = nullptr;  // GC sees, must initialize
        INIT_CTX_KEYLIST_SHARED(CTX(notes_varlist), paramlist);

        REBVAL *rootvar = RESET_CELL(
            ARR_HEAD(notes_varlist),
            REB_FRAME,
            CELL_MASK_CONTEXT
        );
        INIT_VAL_CONTEXT_VARLIST(rootvar, notes_varlist); // canon FRAME!
        INIT_VAL_CONTEXT_PHASE(rootvar, ACT(paramlist));
        INIT_BINDING(rootvar, UNBOUND);

        REBVAL *dest = rootvar + 1;

        REBVAL *src = DS_AT(dsp_orig + 3);
        src += 3;
        for (; src <= DS_TOP; src += 3) {
            assert(IS_TEXT(src));
            if (definitional_return and src == definitional_return + 2)
                continue;

            if (SER_LEN(VAL_SERIES(src)) == 0)
                Init_Nulled(dest);
            else
                Move_Value(dest, src);
            ++dest;
        }

        if (definitional_return) {
            //
            // See remarks on the return type--the RETURN is documented in
            // the top-level META-OF, not the "incidentally" named RETURN
            // parameter in the list
            //
            if (SER_LEN(VAL_SERIES(definitional_return + 2)) == 0)
                Init_Nulled(CTX_VAR(meta, STD_ACTION_META_RETURN_NOTE));
            else {
                Move_Value(
                    CTX_VAR(meta, STD_ACTION_META_RETURN_NOTE),
                    &definitional_return[2]
                );
            }

            Init_Nulled(dest);
            ++dest;
        }

        TERM_ARRAY_LEN(notes_varlist, num_slots);

        Init_Frame(
            CTX_VAR(meta, STD_ACTION_META_PARAMETER_NOTES),
            CTX(notes_varlist)
        );
    }

    // With all the values extracted from stack to array, restore stack pointer
    //
    DS_DROP_TO(dsp_orig);

    return paramlist;
}



//
//  Find_Param_Index: C
//
// Find function param word in function "frame".
//
// !!! This is semi-redundant with similar functions for Find_Word_In_Array
// and key finding for objects, review...
//
REBCNT Find_Param_Index(REBARR *paramlist, REBSTR *spelling)
{
    REBSTR *canon = STR_CANON(spelling); // don't recalculate each time

    RELVAL *param = ARR_AT(paramlist, 1);
    REBCNT len = ARR_LEN(paramlist);

    REBCNT n;
    for (n = 1; n < len; ++n, ++param) {
        if (
            spelling == VAL_PARAM_SPELLING(param)
            or canon == VAL_PARAM_CANON(param)
        ){
            return n;
        }
    }

    return 0;
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
REBACT *Make_Action(
    REBARR *paramlist,
    REBNAT dispatcher, // native C function called by Eval_Core
    REBACT *opt_underlying, // optional underlying function
    REBCTX *opt_exemplar, // if provided, should be consistent w/next level
    REBCNT details_capacity // desired capacity of the ACT_DETAILS() array
){
    ASSERT_ARRAY_MANAGED(paramlist);

    RELVAL *rootparam = ARR_HEAD(paramlist);
    assert(KIND_BYTE(rootparam) == REB_ACTION); // !!! not fully formed...
    assert(VAL_ACT_PARAMLIST(rootparam) == paramlist);
    assert(EXTRA(Binding, rootparam).node == UNBOUND); // archetype

    // "details" for an action is an array of cells which can be anything
    // the dispatcher understands it to be, by contract.  Terminate it
    // at the given length implicitly.

    REBARR *details = Make_Array_Core(
        details_capacity,
        SERIES_MASK_DETAILS | NODE_FLAG_MANAGED
    );
    TERM_ARRAY_LEN(details, details_capacity);

    VAL_ACT_DETAILS_NODE(rootparam) = NOD(details);

    MISC(details).dispatcher = dispatcher; // level of indirection, hijackable

    assert(IS_POINTER_SAFETRASH_DEBUG(LINK(paramlist).trash));

    if (opt_underlying) {
        LINK_UNDERLYING_NODE(paramlist) = NOD(opt_underlying);

        // Note: paramlist still incomplete, don't use SET_ACTION_FLAG....
        //
        if (GET_ACTION_FLAG(opt_underlying, HAS_RETURN))
            SER(paramlist)->header.bits |= PARAMLIST_FLAG_HAS_RETURN;
    }
    else {
        // To avoid NULL checking when a function is called and looking for
        // underlying, just use the action's own paramlist if needed.
        //
        LINK_UNDERLYING_NODE(paramlist) = NOD(paramlist);
    }

    if (not opt_exemplar) {
        //
        // No exemplar is used as a cue to set the "specialty" to paramlist,
        // so that Push_Action() can assign f->special directly from it in
        // dispatch, and be equal to f->param.
        //
        LINK_SPECIALTY_NODE(details) = NOD(paramlist);
    }
    else {
        // The parameters of the paramlist should line up with the slots of
        // the exemplar (though some of these parameters may be hidden due to
        // specialization, see REB_TS_HIDDEN).
        //
        assert(GET_SERIES_FLAG(opt_exemplar, MANAGED));
        assert(CTX_LEN(opt_exemplar) == ARR_LEN(paramlist) - 1);

        LINK_SPECIALTY_NODE(details) = NOD(CTX_VARLIST(opt_exemplar));
    }

    // The meta information may already be initialized, since the native
    // version of paramlist construction sets up the FUNCTION-META information
    // used by HELP.  If so, it must be a valid REBCTX*.  Otherwise NULL.
    //
    assert(
        not MISC_META(paramlist)
        or GET_ARRAY_FLAG(CTX_VARLIST(MISC_META(paramlist)), IS_VARLIST)
    );

    assert(NOT_ARRAY_FLAG(paramlist, HAS_FILE_LINE_UNMASKED));
    assert(NOT_ARRAY_FLAG(details, HAS_FILE_LINE_UNMASKED));

    REBACT *act = ACT(paramlist); // now it's a legitimate REBACT

    // Precalculate cached function flags.  This involves finding the first
    // unspecialized argument which would be taken at a callsite, which can
    // be tricky to figure out with partial refinement specialization.  So
    // the work of doing that is factored into a routine (`PARAMETERS OF`
    // uses it as well).

    if (GET_ACTION_FLAG(act, HAS_RETURN)) {
        REBVAL *param = ACT_PARAM(act, ACT_NUM_PARAMS(act));
        assert(VAL_PARAM_SYM(param) == SYM_RETURN);

        if (Is_Typeset_Invisible(param))  // e.g. `return []`
            SET_ACTION_FLAG(act, IS_INVISIBLE);

        if (TYPE_CHECK(param, REB_TS_DEQUOTE_REQUOTE))
            SET_ACTION_FLAG(act, RETURN_REQUOTES);
    }

    REBVAL *first_unspecialized = First_Unspecialized_Param(act);
    if (first_unspecialized) {
        switch (VAL_PARAM_CLASS(first_unspecialized)) {
          case REB_P_NORMAL:
            break;

          case REB_P_HARD_QUOTE:
          case REB_P_SOFT_QUOTE:
            SET_ACTION_FLAG(act, QUOTES_FIRST);
            break;

          default:
            assert(false);
        }

        if (TYPE_CHECK(first_unspecialized, REB_TS_SKIPPABLE))
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
    // Since passing SERIES_MASK_VARLIST includes SERIES_FLAG_ALWAYS_DYNAMIC,
    // don't pass it in to the allocation...it needs to be set, but will be
    // overridden by SERIES_INFO_INACCESSIBLE.
    //
    REBARR *varlist = Alloc_Singular(NODE_FLAG_STACK | NODE_FLAG_MANAGED);
    SER(varlist)->header.bits |= SERIES_MASK_VARLIST;
    SET_SERIES_INFO(varlist, INACCESSIBLE);
    MISC_META_NODE(varlist) = nullptr;

    RELVAL *rootvar = RESET_CELL(
        ARR_SINGLE(varlist),
        REB_FRAME,
        CELL_MASK_CONTEXT
    );
    INIT_VAL_CONTEXT_VARLIST(rootvar, varlist);
    INIT_VAL_CONTEXT_PHASE(rootvar, a);
    INIT_BINDING(rootvar, UNBOUND); // !!! is a binding relevant?

    REBCTX *expired = CTX(varlist);
    INIT_CTX_KEYLIST_SHARED(expired, ACT_PARAMLIST(a));

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
    REBSPC *binding = VAL_BINDING(action);
    REBACT *a = VAL_ACTION(action);

    // A Hijacker *might* not need to splice itself in with a dispatcher.
    // But if it does, bypass it to get to the "real" action implementation.
    //
    // !!! Should the source inject messages like {This is a hijacking} at
    // the top of the returned body?
    //
    while (ACT_DISPATCHER(a) == &Hijacker_Dispatcher) {
        a = VAL_ACTION(ARR_HEAD(ACT_DETAILS(a)));
        // !!! Review what should happen to binding
    }

    REBARR *details = ACT_DETAILS(a);

    // !!! Should the binding make a difference in the returned body?  It is
    // exposed programmatically via CONTEXT OF.
    //
    UNUSED(binding);

    if (
        ACT_DISPATCHER(a) == &Null_Dispatcher
        or ACT_DISPATCHER(a) == &Void_Dispatcher
        or ACT_DISPATCHER(a) == &Unchecked_Dispatcher
        or ACT_DISPATCHER(a) == &Voider_Dispatcher
        or ACT_DISPATCHER(a) == &Returner_Dispatcher
        or ACT_DISPATCHER(a) == &Block_Dispatcher
    ){
        // Interpreted code, the body is a block with some bindings relative
        // to the action.

        RELVAL *body = ARR_HEAD(details);

        // The PARAMLIST_HAS_RETURN tricks for definitional return make it
        // seem like a generator authored more code in the action's body...but
        // the code isn't *actually* there and an optimized internal trick is
        // used.  Fake the code if needed.

        REBVAL *example;
        REBCNT real_body_index;
        if (ACT_DISPATCHER(a) == &Voider_Dispatcher) {
            example = Get_System(SYS_STANDARD, STD_PROC_BODY);
            real_body_index = 4;
        }
        else if (GET_ACTION_FLAG(a, HAS_RETURN)) {
            example = Get_System(SYS_STANDARD, STD_FUNC_BODY);
            real_body_index = 4;
        }
        else {
            example = NULL;
            real_body_index = 0; // avoid compiler warning
            UNUSED(real_body_index);
        }

        REBARR *real_body = VAL_ARRAY(body);

        REBARR *maybe_fake_body;
        if (example == NULL) {
            maybe_fake_body = real_body;
        }
        else {
            // See %sysobj.r for STANDARD/FUNC-BODY and STANDARD/PROC-BODY
            //
            maybe_fake_body = Copy_Array_Shallow_Flags(
                VAL_ARRAY(example),
                VAL_SPECIFIER(example),
                NODE_FLAG_MANAGED
            );

            // Index 5 (or 4 in zero-based C) should be #BODY, a "real" body.
            // To give it the appearance of executing code in place, we use
            // a GROUP!.

            RELVAL *slot = ARR_AT(maybe_fake_body, real_body_index); // #BODY
            assert(IS_ISSUE(slot));

            // Note: clears VAL_FLAG_LINE
            //
            RESET_VAL_HEADER(slot, REB_GROUP, CELL_FLAG_FIRST_IS_NODE);
            INIT_VAL_NODE(slot, VAL_ARRAY(body));
            VAL_INDEX(slot) = 0;
            INIT_BINDING(slot, a);  // relative binding
        }

        // Cannot give user a relative value back, so make the relative
        // body specific to a fabricated expired frame.  See #2221

        RESET_VAL_HEADER(out, REB_BLOCK, CELL_FLAG_FIRST_IS_NODE);
        INIT_VAL_NODE(out, maybe_fake_body);
        VAL_INDEX(out) = 0;
        INIT_BINDING(out, Make_Expired_Frame_Ctx_Managed(a));
        return;
    }

    if (ACT_DISPATCHER(a) == &Specializer_Dispatcher) {
        //
        // The FRAME! stored in the body for the specialization has a phase
        // which is actually the function to be run.
        //
        REBVAL *frame = KNOWN(ARR_HEAD(details));
        assert(IS_FRAME(frame));
        Move_Value(out, frame);
        return;
    }

    if (ACT_DISPATCHER(a) == &Generic_Dispatcher) {
        REBVAL *verb = KNOWN(ARR_HEAD(details));
        assert(IS_WORD(verb));
        Move_Value(out, verb);
        return;
    }

    Init_Blank(out); // natives, ffi routines, etc.
    return;
}


//
//  Make_Interpreted_Action_May_Fail: C
//
// This is the support routine behind both `MAKE ACTION!` and FUNC.
//
// Ren-C's schematic is *very* different from R3-Alpha, whose definition of
// FUNC was simply:
//
//     make function! copy/deep reduce [spec body]
//
// Ren-C's `make action!` doesn't need to copy the spec (it does not save
// it--parameter descriptions are in a meta object).  The body is copied
// implicitly (as it must be in order to relativize it).
//
// There is also a "definitional return" MKF_RETURN option used by FUNC, so
// the body will introduce a RETURN specific to each action invocation, thus
// acting more like:
//
//     return: make action! [
//         [{Returns a value from a function.} value [<opt> any-value!]]
//         [unwind/with (binding of 'return) :value]
//     ]
//     (body goes here)
//
// This pattern addresses "Definitional Return" in a way that does not need to
// build in RETURN as a language keyword in any specific form (in the sense
// that MAKE ACTION! does not itself require it).
//
// FUNC optimizes by not internally building or executing the equivalent body,
// but giving it back from BODY-OF.  This gives FUNC the edge to pretend to
// add containing code and simulate its effects, while really only holding
// onto the body the caller provided.
//
// While plain MAKE ACTION! has no RETURN, UNWIND can be used to exit frames
// but must be explicit about what frame is being exited.  This can be used
// by usermode generators that want to create something return-like.
//
REBACT *Make_Interpreted_Action_May_Fail(
    const REBVAL *spec,
    const REBVAL *body,
    REBFLGS mkf_flags  // MKF_RETURN, etc.
) {
    assert(IS_BLOCK(spec) and IS_BLOCK(body));

    REBACT *a = Make_Action(
        Make_Paramlist_Managed_May_Fail(spec, mkf_flags),
        &Null_Dispatcher,  // will be overwritten if non-[] body
        nullptr,  // no underlying action (use paramlist)
        nullptr,  // no specialization exemplar (or inherited exemplar)
        1  // details array capacity
    );

    // We look at the *actual* function flags; e.g. the person may have used
    // the FUNC generator (with MKF_RETURN) but then named a parameter RETURN
    // which overrides it, so the value won't have PARAMLIST_HAS_RETURN.

    REBARR *copy;
    if (VAL_ARRAY_LEN_AT(body) == 0) {  // optimize empty body case

        if (GET_ACTION_FLAG(a, IS_INVISIBLE)) {
            ACT_DISPATCHER(a) = &Commenter_Dispatcher;
        }
        else if (SER(a)->info.bits & ARRAY_INFO_MISC_VOIDER) {
            ACT_DISPATCHER(a) = &Voider_Dispatcher;  // !!! ^-- see info note
        }
        else if (GET_ACTION_FLAG(a, HAS_RETURN)) {
            REBVAL *typeset = ACT_PARAM(a, ACT_NUM_PARAMS(a));
            assert(VAL_PARAM_SYM(typeset) == SYM_RETURN);
            if (not TYPE_CHECK(typeset, REB_NULLED))  // `do []` returns
                ACT_DISPATCHER(a) = &Returner_Dispatcher;  // error when run
        }
        else {
            // Keep the Null_Dispatcher passed in above
        }

        // Reusing EMPTY_ARRAY won't allow adding ARRAY_HAS_FILE_LINE bits
        //
        copy = Make_Array_Core(1, NODE_FLAG_MANAGED);
    }
    else {  // body not empty, pick dispatcher based on output disposition

        if (GET_ACTION_FLAG(a, IS_INVISIBLE))
            ACT_DISPATCHER(a) = &Elider_Dispatcher; // no f->out mutation
        else if (SER(a)->info.bits & ARRAY_INFO_MISC_VOIDER) // !!! see note
            ACT_DISPATCHER(a) = &Voider_Dispatcher; // forces f->out void
        else if (GET_ACTION_FLAG(a, HAS_RETURN))
            ACT_DISPATCHER(a) = &Returner_Dispatcher; // type checks f->out
        else
            ACT_DISPATCHER(a) = &Unchecked_Dispatcher; // unchecked f->out

        copy = Copy_And_Bind_Relative_Deep_Managed(
            body,  // new copy has locals bound relatively to the new action
            ACT_PARAMLIST(a),
            TS_WORD
        );
    }

    RELVAL *rebound = RESET_CELL(
        ARR_HEAD(ACT_DETAILS(a)),
        REB_BLOCK,
        CELL_FLAG_FIRST_IS_NODE
    );
    INIT_VAL_NODE(rebound, copy);
    VAL_INDEX(rebound) = 0;
    INIT_BINDING(rebound, a);  // Record that block is relative to a function

    // Favor the spec first, then the body, for file and line information.
    //
    if (GET_ARRAY_FLAG(VAL_ARRAY(spec), HAS_FILE_LINE_UNMASKED)) {
        LINK_FILE_NODE(copy) = LINK_FILE_NODE(VAL_ARRAY(spec));
        MISC(copy).line = MISC(VAL_ARRAY(spec)).line;
        SET_ARRAY_FLAG(copy, HAS_FILE_LINE_UNMASKED);
    }
    else if (GET_ARRAY_FLAG(VAL_ARRAY(body), HAS_FILE_LINE_UNMASKED)) {
        LINK_FILE_NODE(copy) = LINK_FILE_NODE(VAL_ARRAY(body));
        MISC(copy).line = MISC(VAL_ARRAY(body)).line;
        SET_ARRAY_FLAG(copy, HAS_FILE_LINE_UNMASKED);
    }
    else {
        // Ideally all source series should have a file and line numbering
        // At the moment, if a function is created in the body of another
        // function it doesn't work...trying to fix that.
    }

    // Capture the mutability flag that was in effect when this action was
    // created.  This allows the following to work:
    //
    //    >> do mutable [f: function [] [b: [1 2 3] clear b]]
    //    >> f
    //    == []
    //
    // So even though the invocation is outside the mutable section, we have
    // a memory that it was created under those rules.  (It's better to do
    // this based on the frame in effect than by looking at the CONST flag of
    // the incoming body block, because otherwise ordinary Ren-C functions
    // whose bodies were created from dynamic code would have mutable bodies
    // by default--which is not a desirable consequence from merely building
    // the body dynamically.)
    //
    // Note: besides the general concerns about mutability-by-default, when
    // functions are allowed to modify their bodies with words relative to
    // their frame, the words would refer to that specific recursion...and not
    // get picked up by other recursions that see the common structure.  This
    // means compatibility would be with the behavior of R3-Alpha CLOSURE,
    // not with FUNCTION.
    //
    if (GET_CELL_FLAG(body, CONST))
        SET_CELL_FLAG(rebound, CONST);  // Inherit_Const() would need REBVAL*

    return a;
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
//  Generic_Dispatcher: C
//
// A "generic" is what R3-Alpha/Rebol2 had called "ACTION!" (until Ren-C took
// that as the umbrella term for all "invokables").  This kind of dispatch is
// based on the first argument's type, with the idea being a single C function
// for the type has a switch() statement in it and can handle many different
// such actions for that type.
//
// (e.g. APPEND copy [a b c] [d] would look at the type of the first argument,
// notice it was a BLOCK!, and call the common C function for arrays with an
// append instruction--where that instruction also handles insert, length,
// etc. for BLOCK!s.)
//
// !!! This mechanism is a very primitive kind of "multiple dispatch".  Rebol
// will certainly need to borrow from other languages to develop a more
// flexible idea for user-defined types, vs. this very limited concept.
//
// https://en.wikipedia.org/wiki/Multiple_dispatch
// https://en.wikipedia.org/wiki/Generic_function
// https://stackoverflow.com/q/53574843/
//
REB_R Generic_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    REBVAL *verb = KNOWN(ARR_HEAD(details));
    assert(IS_WORD(verb));

    enum Reb_Kind kind = VAL_TYPE(FRM_ARG(f, 1));
    return Run_Generic_Dispatch(f, kind, verb);
}


//
//  Dummy_Dispatcher: C
//
// Used for frame levels that want a varlist solely for the purposes of tying
// API handle lifetimes to.  These levels should be ignored by stack walks
// that the user sees, and this associated dispatcher should never run.
//
REB_R Dummy_Dispatcher(REBFRM *f)
{
    UNUSED(f);
    panic ("Dummy_Dispatcher() ran, but it never should get called");
}


//
//  Void_Dispatcher: C
//
// If you write `func [return: <void> ...] []` it uses this dispatcher instead
// of running Eval_Core() on an empty block.  This serves more of a point than
// it sounds, because you can make fast stub actions that only cost if they
// are HIJACK'd (e.g. ASSERT is done this way).
//
REB_R Void_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    assert(VAL_LEN_AT(ARR_HEAD(details)) == 0);
    UNUSED(details);

    return Init_Void(f->out);
}


//
//  Null_Dispatcher: C
//
// Analogue to Void_Dispatcher() for `func [return: [<opt>] ...] [null]`
// situations.
//
REB_R Null_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    assert(VAL_LEN_AT(ARR_HEAD(details)) == 0);
    UNUSED(details);

    return nullptr;
}


//
//  Datatype_Checker_Dispatcher: C
//
// Dispatcher used by TYPECHECKER generator for when argument is a datatype.
//
REB_R Datatype_Checker_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    RELVAL *datatype = ARR_HEAD(details);
    assert(IS_DATATYPE(datatype));

    return Init_Logic(
        f->out,
        VAL_TYPE(FRM_ARG(f, 1)) == VAL_TYPE_KIND(datatype)
    );
}


//
//  Typeset_Checker_Dispatcher: C
//
// Dispatcher used by TYPECHECKER generator for when argument is a typeset.
//
REB_R Typeset_Checker_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    RELVAL *typeset = ARR_HEAD(details);
    assert(IS_TYPESET(typeset));

    return Init_Logic(f->out, TYPE_CHECK(typeset, VAL_TYPE(FRM_ARG(f, 1))));
}


// Common behavior shared by dispatchers which execute on BLOCK!s of code.
//
inline static bool Interpreted_Dispatch_Throws(
    REBVAL *out,  // Note: Elider_Dispatcher() doesn't have `out = f->out`
    REBFRM *f
){
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    RELVAL *body = ARR_HEAD(details);  // usually CONST (doesn't have to be)
    assert(IS_BLOCK(body) and IS_RELATIVE(body) and VAL_INDEX(body) == 0);

    // The function body contains relativized words, that point to the
    // paramlist but do not have an instance of an action to line them up
    // with.  We use the frame (identified by varlist) as the "specifier".
    //
    return Do_Any_Array_At_Throws(out, body, SPC(f->varlist));
}


//
//  Unchecked_Dispatcher: C
//
// Runs block, then no typechecking (e.g. had no RETURN: [...] type spec)
//
REB_R Unchecked_Dispatcher(REBFRM *f)
{
    if (Interpreted_Dispatch_Throws(f->out, f))
        return R_THROWN;
    return f->out;
}


//
//  Voider_Dispatcher: C
//
// Runs block, then overwrites result w/void (e.g. RETURN: <void>)
//
REB_R Voider_Dispatcher(REBFRM *f)
{
    if (Interpreted_Dispatch_Throws(f->out, f))  // action body is a BLOCK!
        return R_THROWN;
    return Init_Void(f->out);
}


//
//  Returner_Dispatcher: C
//
// Runs block, ensure type matches RETURN: [...] specification, else fail.
//
// Note: Natives get this check only in the debug build, but not here (their
// dispatcher *is* the native!)  So the extra check is in Eval_Core().
//
REB_R Returner_Dispatcher(REBFRM *f)
{
    if (Interpreted_Dispatch_Throws(f->out, f))
        return R_THROWN;

    REBACT *phase = FRM_PHASE(f);
    REBVAL *typeset = ACT_PARAM(phase, ACT_NUM_PARAMS(phase));
    assert(VAL_PARAM_SYM(typeset) == SYM_RETURN);

    // Typeset bits for locals in frames are usually ignored, but the RETURN:
    // local uses them for the return types of a function.
    //
    if (not Typecheck_Including_Quoteds(typeset, f->out))
        fail (Error_Bad_Return_Type(f, VAL_TYPE(f->out)));

    return f->out;
}


//
//  Elider_Dispatcher: C
//
// Used by "invisible" functions (who in their spec say `RETURN: []`).  Runs
// block but without changing any value already in f->out.
//
REB_R Elider_Dispatcher(REBFRM *f)
{
    REBVAL * const discarded = FRM_SPARE(f);  // spare usable during dispatch

    if (Interpreted_Dispatch_Throws(discarded, f)) {
        //
        // !!! In the implementation of invisibles, it seems reasonable to
        // want to be able to RETURN to its own frame.  But in that case, we
        // don't want to actually overwrite the f->out content or this would
        // be no longer invisible.  Until a better idea comes along, repeat
        // the work of catching here.  (Note this does not handle REDO too,
        // and the hypothetical better idea should do so.)
        //
        const REBVAL *label = VAL_THROWN_LABEL(discarded);
        if (IS_ACTION(label)) {
            if (
                VAL_ACTION(label) == NAT_ACTION(unwind)
                and VAL_BINDING(label) == NOD(f->varlist)
            ){
                CATCH_THROWN(discarded, discarded);
                if (IS_NULLED(discarded)) // !!! catch loses "endish" flag
                    return R_INVISIBLE;

                fail ("Only 0-arity RETURN should be used in invisibles.");
            }
        }

        Move_Value(f->out, discarded);
        return R_THROWN;
    }

    return R_INVISIBLE;
}


//
//  Commenter_Dispatcher: C
//
// This is a specialized version of Elider_Dispatcher() for when the body of
// a function is empty.  This helps COMMENT and functions like it run faster.
//
REB_R Commenter_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    RELVAL *body = ARR_HEAD(details);
    assert(VAL_LEN_AT(body) == 0);
    UNUSED(body);
    return R_INVISIBLE;
}


//
//  Hijacker_Dispatcher: C
//
// A hijacker takes over another function's identity, replacing it with its
// own implementation, injecting directly into the paramlist and body_holder
// nodes held onto by all the victim's references.
//
// Sometimes the hijacking function has the same underlying function
// as the victim, in which case there's no need to insert a new dispatcher.
// The hijacker just takes over the identity.  But otherwise it cannot,
// and a "shim" is needed...since something like an ADAPT or SPECIALIZE
// or a MAKE FRAME! might depend on the existing paramlist shape.
//
REB_R Hijacker_Dispatcher(REBFRM *f)
{
    REBACT *phase = FRM_PHASE(f);
    REBARR *details = ACT_DETAILS(phase);
    RELVAL *hijacker = ARR_HEAD(details);

    // We need to build a new frame compatible with the hijacker, and
    // transform the parameters we've gathered to be compatible with it.
    //
    if (Redo_Action_Throws(f->out, f, VAL_ACTION(hijacker)))
        return R_THROWN;

    if (GET_ACTION_FLAG(phase, IS_INVISIBLE))
        return R_INVISIBLE;

    return f->out;
}


//
//  Adapter_Dispatcher: C
//
// Dispatcher used by ADAPT.
//
REB_R Adapter_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == 2);

    RELVAL* prelude = ARR_AT(details, 0);
    REBVAL* adaptee = KNOWN(ARR_AT(details, 1));

    // The first thing to do is run the prelude code, which may throw.  If it
    // does throw--including a RETURN--that means the adapted function will
    // not be run.

    REBVAL * const discarded = FRM_SPARE(f);

    if (Do_Any_Array_At_Throws(discarded, prelude, SPC(f->varlist))) {
        Move_Value(f->out, discarded);
        return R_THROWN;
    }

    INIT_FRM_PHASE(f, VAL_ACTION(adaptee));
    FRM_BINDING(f) = VAL_BINDING(adaptee);

    return R_REDO_CHECKED;  // the redo will use the updated phase & binding
}


//
//  Encloser_Dispatcher: C
//
// Dispatcher used by ENCLOSE.
//
REB_R Encloser_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == 2);

    REBVAL *inner = KNOWN(ARR_AT(details, 0));  // same args as f
    assert(IS_ACTION(inner));
    REBVAL *outer = KNOWN(ARR_AT(details, 1));  // takes 1 arg (a FRAME!)
    assert(IS_ACTION(outer));

    assert(GET_SERIES_FLAG(f->varlist, STACK_LIFETIME));

    // We want to call OUTER with a FRAME! value that will dispatch to INNER
    // when (and if) it runs DO on it.  That frame is the one built for this
    // call to the encloser.  If it isn't managed, there's no worries about
    // user handles on it...so just take it.  Otherwise, "steal" its vars.
    //
    REBCTX *c = Steal_Context_Vars(CTX(f->varlist), NOD(FRM_PHASE(f)));
    INIT_LINK_KEYSOURCE(c, NOD(VAL_ACTION(inner)));
    CLEAR_SERIES_FLAG(c, STACK_LIFETIME);

    assert(GET_SERIES_INFO(f->varlist, INACCESSIBLE));  // look dead

    // f->varlist may or may not have wound up being managed.  It was not
    // allocated through the usual mechanisms, so if unmanaged it's not in
    // the tracking list Init_Any_Context() expects.  Just fiddle the bit.
    //
    SET_SERIES_FLAG(c, MANAGED);

    // When the DO of the FRAME! executes, we don't want it to run the
    // encloser again (infinite loop).
    //
    REBVAL *rootvar = CTX_ARCHETYPE(c);
    INIT_VAL_CONTEXT_PHASE(rootvar, VAL_ACTION(inner));
    INIT_BINDING_MAY_MANAGE(rootvar, VAL_BINDING(inner));

    // We don't actually know how long the frame we give back is going to
    // live, or who it might be given to.  And it may contain things like
    // bindings in a RETURN or a VARARGS! which are to the old varlist, which
    // may not be managed...and so when it goes off the stack it might try
    // and think that since nothing managed it then it can be freed.  Go
    // ahead and mark it managed--even though it's dead--so that returning
    // won't free it if there are outstanding references.
    //
    // Note that since varlists aren't added to the manual series list, the
    // bit must be tweaked vs. using Ensure_Array_Managed.
    //
    SET_SERIES_FLAG(f->varlist, MANAGED);

    const bool fully = true;
    if (RunQ_Throws(f->out, fully, rebU1(outer), rootvar, rebEND))
        return R_THROWN;

    return f->out;
}


//
//  Chainer_Dispatcher: C
//
// Dispatcher used by CHAIN.
//
REB_R Chainer_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    REBARR *pipeline = VAL_ARRAY(ARR_HEAD(details));

    // The post-processing pipeline has to be "pushed" so it is not forgotten.
    // Go in reverse order, so the function to apply last is at the bottom of
    // the stack.
    //
    REBVAL *chained = KNOWN(ARR_LAST(pipeline));
    for (; chained != ARR_HEAD(pipeline); --chained) {
        assert(IS_ACTION(chained));
        Move_Value(DS_PUSH(), KNOWN(chained));
    }

    // Extract the first function, itself which might be a chain.
    //
    INIT_FRM_PHASE(f, VAL_ACTION(chained));
    FRM_BINDING(f) = VAL_BINDING(chained);

    return R_REDO_UNCHECKED;  // signatures should match
}


//
//  Get_If_Word_Or_Path_Throws: C
//
// Some routines like APPLY and SPECIALIZE are willing to take a WORD! or
// PATH! instead of just the value type they are looking for, and perform
// the GET for you.  By doing the GET inside the function, they are able
// to preserve the symbol:
//
//     >> applique 'append [value: 'c]
//     ** Script error: append is missing its series argument
//
// If push_refinements is used, then it avoids intermediate specializations...
// e.g. `specialize 'append/dup [part: true]` can be done with one FRAME!.
//
bool Get_If_Word_Or_Path_Throws(
    REBVAL *out,
    REBSTR **opt_name_out,
    const RELVAL *v,
    REBSPC *specifier,
    bool push_refinements
) {
    if (IS_WORD(v) or IS_GET_WORD(v)) {
        *opt_name_out = VAL_WORD_SPELLING(v);
        Move_Opt_Var_May_Fail(out, v, specifier);
    }
    else if (IS_PATH(v) or IS_GET_PATH(v)) {
        REBSPC *derived = Derive_Specifier(specifier, v);
        if (Eval_Path_Throws_Core(
            out,
            opt_name_out,  // requesting says we run functions (not GET-PATH!)
            VAL_ARRAY(v),
            VAL_INDEX(v),
            derived,
            NULL,  // `setval`: null means don't treat as SET-PATH!
            EVAL_MASK_DEFAULT | (push_refinements
                ? EVAL_FLAG_PUSH_PATH_REFINES  // pushed in reverse order
                : 0)
        )){
            return true;
        }
    }
    else {
        *opt_name_out = NULL;
        Derelativize(out, v, specifier);
    }

    return false;
}
