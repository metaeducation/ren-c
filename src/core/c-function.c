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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"

//
//  List_Func_Words: C
//
// Return a block of function words, unbound.
// Note: skips 0th entry.
//
REBARR *List_Func_Words(const RELVAL *func, REBOOL pure_locals)
{
    REBARR *array = Make_Array(VAL_FUNC_NUM_PARAMS(func));
    REBVAL *param = VAL_FUNC_PARAMS_HEAD(func);

    for (; NOT_END(param); param++) {
        enum Reb_Kind kind;

        switch (VAL_PARAM_CLASS(param)) {
        case PARAM_CLASS_NORMAL:
            kind = REB_WORD;
            break;

        case PARAM_CLASS_TIGHT:
            kind = REB_ISSUE;
            break;

        case PARAM_CLASS_REFINEMENT:
            kind = REB_REFINEMENT;
            break;

        case PARAM_CLASS_HARD_QUOTE:
            kind = REB_GET_WORD;
            break;

        case PARAM_CLASS_SOFT_QUOTE:
            kind = REB_LIT_WORD;
            break;

        case PARAM_CLASS_LOCAL:
        case PARAM_CLASS_RETURN: // "magic" local - prefilled invisibly
        case PARAM_CLASS_LEAVE: // "magic" local - prefilled invisibly
            if (!pure_locals)
                continue; // treat as invisible, e.g. for WORDS-OF

            kind = REB_SET_WORD;
            break;

        default:
            assert(FALSE);
            DEAD_END;
        }

        Init_Any_Word(
            Alloc_Tail_Array(array), kind, VAL_PARAM_SPELLING(param)
        );
    }

    return array;
}


//
//  List_Func_Typesets: C
//
// Return a block of function arg typesets.
// Note: skips 0th entry.
//
REBARR *List_Func_Typesets(REBVAL *func)
{
    REBARR *array = Make_Array(VAL_FUNC_NUM_PARAMS(func));
    REBVAL *typeset = VAL_FUNC_PARAMS_HEAD(func);

    for (; NOT_END(typeset); typeset++) {
        assert(IS_TYPESET(typeset));

        REBVAL *value = Alloc_Tail_Array(array);
        Move_Value(value, typeset);

        // !!! It's already a typeset, but this will clear out the header
        // bits.  This may not be desirable over the long run (what if
        // a typeset wishes to encode hiddenness, protectedness, etc?)
        //
        VAL_RESET_HEADER(value, REB_TYPESET);
    }

    return array;
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
// MAKE FUNCTION! for parameters in the evaluator.  Then it is the job
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
// In Ren/C's case it just means you want a local called return, but the
// generator will be "initializing it with a definitional return" for you.
// You don't have to use it if you don't want to...and may overwrite the
// variable.  But it won't be a void at the start.
//
REBARR *Make_Paramlist_Managed_May_Fail(
    const REBVAL *spec,
    REBFLGS flags
) {
    assert(ANY_ARRAY(spec));

    REBUPT header_bits = 0;

#if !defined(NDEBUG)
    //
    // Debug builds go ahead and include a RETURN field and hang onto the
    // typeset for fake returns (e.g. natives).  But they make a note that
    // they are doing this, which helps know what the actual size of the
    // frame would be in a release build (e.g. for a FRM_CELL() assert)
    //
    if (flags & MKF_FAKE_RETURN) {
        header_bits |= FUNC_FLAG_RETURN_DEBUG;
        flags &= ~MKF_FAKE_RETURN;
        assert(NOT(flags & MKF_RETURN));
        flags |= MKF_RETURN;
    }
#endif

    REBDSP dsp_orig = DSP;
    assert(DS_TOP == DS_AT(dsp_orig));

    REBDSP definitional_return_dsp = 0;
    REBDSP definitional_leave_dsp = 0;

    // As we go through the spec block, we push TYPESET! BLOCK! STRING! triples.
    // These will be split out into separate arrays after the process is done.
    // The first slot of the paramlist needs to be the function canon value,
    // while the other two first slots need to be rootkeys.  Get the process
    // started right after a BLOCK! so it's willing to take a string for
    // the function description--it will be extracted from the slot before
    // it is turned into a rootkey for param_notes.
    //
    DS_PUSH_TRASH; // paramlist[0] (will become FUNCTION! canon value)
    Init_Unreadable_Blank(DS_TOP);
    DS_PUSH(EMPTY_BLOCK); // param_types[0] (to be OBJECT! canon value, if any)
    DS_PUSH(EMPTY_STRING); // param_notes[0] (holds description, then canon)

    REBOOL has_description = FALSE;
    REBOOL has_types = FALSE;
    REBOOL has_notes = FALSE;

    enum Reb_Spec_Mode mode = SPEC_MODE_NORMAL;

    REBOOL refinement_seen = FALSE;

    DECLARE_FRAME (f);
    Push_Frame(f, spec);

    while (NOT_END(f->value)) {
        const RELVAL *item = f->value; // "faked", e.g. <return> => RETURN:
        Fetch_Next_In_Frame(f); // go ahead and consume next

    //=//// STRING! FOR FUNCTION DESCRIPTION OR PARAMETER NOTE ////////////=//

        if (IS_STRING(item)) {
            //
            // Consider `[<with> some-extern "description of that extern"]` to
            // be purely commentary for the implementation, and don't include
            // it in the meta info.
            //
            if (mode == SPEC_MODE_WITH)
                continue;

            if (IS_TYPESET(DS_TOP))
                DS_PUSH(EMPTY_BLOCK); // need a block to be in position

            if (IS_BLOCK(DS_TOP)) { // we're in right spot to push notes/title
                DS_PUSH_TRASH;
                Init_String(
                    DS_TOP,
                    Copy_String_Slimming(VAL_SERIES(item), VAL_INDEX(item), -1)
                );
            }
            else {
                assert(IS_STRING(DS_TOP));

                // !!! A string was already pushed.  Should we append?
                //
                Init_String(
                    DS_TOP,
                    Copy_String_Slimming(VAL_SERIES(item), VAL_INDEX(item), -1)
                );
            }

            if (DS_TOP == DS_AT(dsp_orig + 3))
                has_description = TRUE;
            else
                has_notes = TRUE;

            continue;
        }

    //=//// TOP-LEVEL SPEC TAGS LIKE <local>, <with> etc. /////////////////=//

        if (IS_TAG(item) && (flags & MKF_KEYWORDS)) {
            if (0 == Compare_String_Vals(item, ROOT_WITH_TAG, TRUE)) {
                mode = SPEC_MODE_WITH;
            }
            else if (0 == Compare_String_Vals(item, ROOT_LOCAL_TAG, TRUE)) {
                mode = SPEC_MODE_LOCAL;
            }
            else
                fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));

            continue;
        }

    //=//// BLOCK! OF TYPES TO MAKE TYPESET FROM (PLUS PARAMETER TAGS) ////=//

        if (IS_BLOCK(item)) {
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
            REBVAL *typeset;
            if (IS_TYPESET(DS_TOP)) {
                REBSPC *derived = Derive_Specifier(VAL_SPECIFIER(spec), item);
                DS_PUSH_TRASH;
                Init_Block(
                    DS_TOP,
                    Copy_Array_At_Deep_Managed(
                        VAL_ARRAY(item),
                        VAL_INDEX(item),
                        derived
                    )
                );

                typeset = DS_TOP - 1; // volatile if you DS_PUSH!
            }
            else {
                assert(IS_STRING(DS_TOP)); // !!! are blocks after notes good?

                if (IS_BLANK_RAW(DS_TOP - 2)) {
                    //
                    // No typesets pushed yet, so this is a block before any
                    // parameters have been named.  This was legal in Rebol2
                    // for e.g. `func [[catch] x y][...]`, and R3-Alpha
                    // ignored it.  Ren-C only tolerates this in <r3-legacy>,
                    // (with the tolerance implemented in compatibility FUNC)
                    //
                    fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));
                }

                assert(IS_TYPESET(DS_TOP - 2));
                typeset = DS_TOP - 2;

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
            REBSPC *derived = Derive_Specifier(VAL_SPECIFIER(spec), item);
            Update_Typeset_Bits_Core(
                typeset,
                VAL_ARRAY_HEAD(item),
                derived
            );

            // Refinements and refinement arguments cannot be specified as
            // <opt>.  Although refinement arguments may be void, they are
            // not "passed in" that way...the refinement is inactive.
            //
            if (refinement_seen) {
                if (TYPE_CHECK(typeset, REB_MAX_VOID))
                    fail (Error_Refinement_Arg_Opt_Raw());
            }


            // A hard quote can only get a void if it is an <end>, and that
            // is not reflected in the typeset but in TYPESET_FLAG_ENDABLE
            //
            if (VAL_PARAM_CLASS(typeset) == PARAM_CLASS_HARD_QUOTE) {
                if (TYPE_CHECK(typeset, REB_MAX_VOID)) {
                    DECLARE_LOCAL (param_name);
                    Init_Word(param_name, VAL_PARAM_SPELLING(typeset));
                    fail (Error_Hard_Quote_Void_Raw(param_name));
                }
            }

            has_types = TRUE;
            continue;
        }

    //=//// ANY-WORD! PARAMETERS THEMSELVES (MAKE TYPESETS w/SYMBOL) //////=//

        if (!ANY_WORD(item))
            fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));

        // !!! If you say [<with> x /foo y] the <with> terminates and a
        // refinement is started.  Same w/<local>.  Is this a good idea?
        // Note that historically, help hides any refinements that appear
        // behind a /local, but this feature has no parallel in Ren-C.
        //
        if (mode != SPEC_MODE_NORMAL) {
            if (IS_REFINEMENT(item)) {
                mode = SPEC_MODE_NORMAL;
            }
            else if (!IS_WORD(item) && !IS_SET_WORD(item))
                fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));
        }

        REBSTR *canon = VAL_WORD_CANON(item);

        // In rhythm of TYPESET! BLOCK! STRING! we want to be on a string spot
        // at the time of the push of each new typeset.
        //
        if (IS_TYPESET(DS_TOP))
            DS_PUSH(EMPTY_BLOCK);
        if (IS_BLOCK(DS_TOP))
            DS_PUSH(EMPTY_STRING);
        assert(IS_STRING(DS_TOP));

        // By default allow "all datatypes but function and void".  Note that
        // since void isn't a "datatype" the use of the REB_MAX_VOID bit is for
        // expedience.  Also that there are two senses of void signal...the
        // typeset REB_MAX_VOID represents <opt> sense, not the <end> sense,
        // which is encoded by TYPESET_FLAG_ENDABLE.
        //
        // We do not canonize the saved symbol in the paramlist, see #2258.
        //
        DS_PUSH_TRASH;
        REBVAL *typeset = DS_TOP; // volatile if you DS_PUSH!
        Init_Typeset(
            typeset,
            (flags & MKF_ANY_VALUE)
                ? ALL_64
                : ALL_64 & ~(FLAGIT_KIND(REB_MAX_VOID) | FLAGIT_KIND(REB_FUNCTION)),
            VAL_WORD_SPELLING(item)
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
        if (STR_SYMBOL(canon) == SYM_RETURN && NOT(flags & MKF_LEAVE)) {
            assert(definitional_return_dsp == 0);
            if (IS_SET_WORD(item))
                definitional_return_dsp = DSP; // RETURN: explicitly tolerated
            else
                flags &= ~(MKF_RETURN | MKF_FAKE_RETURN);
        }
        else if (
            STR_SYMBOL(canon) == SYM_LEAVE
            && NOT(flags & (MKF_RETURN | MKF_FAKE_RETURN))
        ) {
            assert(definitional_leave_dsp == 0);
            if (IS_SET_WORD(item))
                definitional_leave_dsp = DSP; // LEAVE: explicitly tolerated
            else
                flags &= ~MKF_LEAVE;
        }

        if (mode == SPEC_MODE_WITH && !IS_SET_WORD(item)) {
            //
            // Because FUNC does not do any locals gathering by default, the
            // main purpose of <with> is for instructing it not to do the
            // definitional returns.  However, it also makes changing between
            // FUNC and FUNCTION more fluid.
            //
            // !!! If you write something like `func [x <with> x] [...]` that
            // should be sanity checked with an error...TBD.
            //
            DS_DROP; // forge the typeset, used in `definitional_return` case
            continue;
        }

        switch (VAL_TYPE(item)) {
        case REB_WORD:
            assert(mode != SPEC_MODE_WITH); // should have continued...
            INIT_VAL_PARAM_CLASS(
                typeset,
                (mode == SPEC_MODE_LOCAL)
                    ? PARAM_CLASS_LOCAL
                    : PARAM_CLASS_NORMAL
            );
            break;

        case REB_GET_WORD:
            assert(mode == SPEC_MODE_NORMAL);
            INIT_VAL_PARAM_CLASS(typeset, PARAM_CLASS_HARD_QUOTE);
            break;

        case REB_LIT_WORD:
            assert(mode == SPEC_MODE_NORMAL);
            INIT_VAL_PARAM_CLASS(typeset, PARAM_CLASS_SOFT_QUOTE);
            break;

        case REB_REFINEMENT:
            refinement_seen = TRUE;
            INIT_VAL_PARAM_CLASS(typeset, PARAM_CLASS_REFINEMENT);

            // !!! The typeset bits of a refinement are not currently used.
            // They are checked for TRUE or FALSE but this is done literally
            // by the code.  This means that every refinement has some spare
            // bits available in it for another purpose.
            break;

        case REB_SET_WORD:
            // tolerate as-is if in <local> or <with> mode...
            INIT_VAL_PARAM_CLASS(typeset, PARAM_CLASS_LOCAL);
            //
            // !!! Typeset bits of pure locals also not currently used,
            // though definitional return should be using it for the return
            // type of the function.
            //
            break;

        case REB_ISSUE:
            //
            // !!! Because of their role in the preprocessor in Red, and a
            // likely need for a similar behavior in Rebol, ISSUE! might not
            // be the ideal choice to mark tight parameters.
            //
            assert(mode == SPEC_MODE_NORMAL);
            INIT_VAL_PARAM_CLASS(typeset, PARAM_CLASS_TIGHT);
            break;

        default:
            fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));
        }
    }

    Drop_Frame(f);

    // Go ahead and flesh out the TYPESET! BLOCK! STRING! triples.
    //
    if (IS_TYPESET(DS_TOP))
        DS_PUSH(EMPTY_BLOCK);
    if (IS_BLOCK(DS_TOP))
        DS_PUSH(EMPTY_STRING);
    assert((DSP - dsp_orig) % 3 == 0); // must be a multiple of 3

    // Definitional RETURN and LEAVE slots must have their argument values
    // fulfilled with FUNCTION! values specific to the function being called
    // on *every instantiation*.  They are marked with special parameter
    // classes to avoid needing to separately do canon comparison of their
    // symbols to find them.  In addition, since RETURN's typeset holds
    // types that need to be checked at the end of the function run, it
    // is moved to a predictable location: last slot of the paramlist.
    //
    // Note: Trying to take advantage of the "predictable first position"
    // by swapping is not legal, as the first argument's position matters
    // in the ordinary arity of calling.

    if (flags & MKF_LEAVE) {
        if (definitional_leave_dsp == 0) { // no LEAVE: pure local explicit
            REBSTR *canon_leave = Canon(SYM_LEAVE);

            DS_PUSH_TRASH;
            Init_Typeset(DS_TOP, FLAGIT_KIND(REB_MAX_VOID), canon_leave);
            INIT_VAL_PARAM_CLASS(DS_TOP, PARAM_CLASS_LEAVE);
            definitional_leave_dsp = DSP;

            DS_PUSH(EMPTY_BLOCK);
            DS_PUSH(EMPTY_STRING);
        }
        else {
            REBVAL *definitional_leave = DS_AT(definitional_leave_dsp);
            assert(VAL_PARAM_CLASS(definitional_leave) == PARAM_CLASS_LOCAL);
            INIT_VAL_PARAM_CLASS(definitional_leave, PARAM_CLASS_LEAVE);
        }
        header_bits |= FUNC_FLAG_LEAVE;
    }

    if (flags & MKF_RETURN) {
        if (definitional_return_dsp == 0) { // no RETURN: pure local explicit
            REBSTR *canon_return = Canon(SYM_RETURN);

            // !!! The current experiment for dealing with default type
            // checking on definitional returns is to be somewhat restrictive
            // if there are *any* documentation notes or typesets on the
            // function.  Hence:
            //
            //     >> foo: func [x] [] ;-- no error, void return allowed
            //     >> foo: func [{a} x] [] ;-- will error, can't return void
            //
            // The idea is that if any effort has been expended on documenting
            // the interface at all, it has some "public" component...so
            // problems like leaking arbitrary values (vs. using PROC) are
            // more likely to be relevant.  Whereas no effort indicates a
            // likely more ad-hoc experimentation.
            //
            // (A "strict" mode, selectable per module, could control this and
            // other settings.  But the goal is to attempt to define something
            // that is as broadly usable as possible.)
            //
            DS_PUSH_TRASH;
            Init_Typeset(
                DS_TOP,
                (flags & MKF_ANY_VALUE)
                || NOT(has_description || has_types || has_notes)
                    ? ALL_64
                    : ALL_64 & ~(
                        FLAGIT_KIND(REB_MAX_VOID) | FLAGIT_KIND(REB_FUNCTION)
                    ),
                canon_return
            );
            INIT_VAL_PARAM_CLASS(DS_TOP, PARAM_CLASS_RETURN);
            definitional_return_dsp = DSP;

            DS_PUSH(EMPTY_BLOCK);
            DS_PUSH(EMPTY_STRING);
            // no need to move it--it's already at the tail position
        }
        else {
            REBVAL *definitional_return = DS_AT(definitional_return_dsp);
            assert(VAL_PARAM_CLASS(definitional_return) == PARAM_CLASS_LOCAL);
            INIT_VAL_PARAM_CLASS(definitional_return, PARAM_CLASS_RETURN);

            // definitional_return handled specially when paramlist copied
            // off of the stack...
        }
        header_bits |= FUNC_FLAG_RETURN;
    }

    // Slots, which is length +1 (includes the rootvar or rootparam)
    //
    REBCNT num_slots = (DSP - dsp_orig) / 3;

    // If we pushed a typeset for a return and it's a native, it actually
    // doesn't want a RETURN: key in the frame in release builds.  We'll omit
    // from the copy.
    //
    if (definitional_return_dsp != 0 && (flags & MKF_FAKE_RETURN))
        --num_slots;

    // There should be no more pushes past this point, so a stable pointer
    // into the stack for the definitional return can be found.
    //
    REBVAL *definitional_return =
        definitional_return_dsp == 0
            ? NULL
            : DS_AT(definitional_return_dsp);

    // Must make the function "paramlist" even if "empty", for identity.
    // Also make sure the parameter list does not expand.
    //
    // !!! Expanding the parameter list might be part of an advanced feature
    // under the hood in the future, but users should not themselves grow
    // function frames by appending to them.
    //
    REBARR *paramlist = Make_Array_Core(
        num_slots,
        ARRAY_FLAG_PARAMLIST | SERIES_FLAG_FIXED_SIZE
    );

    // In order to use this paramlist as a ->phase in a frame below, it must
    // have a valid facade so CTX_KEYLIST() will work.  The Make_Function()
    // calls that provide facades all currently build the full function before
    // trying to add any meta information that includes frames, so they do
    // not have to do this.
    //
    SER(paramlist)->misc.facade = paramlist;

    if (TRUE) {
        RELVAL *dest = ARR_HEAD(paramlist); // canon function value
        VAL_RESET_HEADER(dest, REB_FUNCTION);
        SET_VAL_FLAGS(dest, header_bits);
        dest->payload.function.paramlist = paramlist;
        INIT_BINDING(dest, UNBOUND);
        ++dest;

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

        for (; src <= DS_TOP; src += 3) {
            assert(IS_TYPESET(src));
            if (!Try_Add_Binder_Index(&binder, VAL_PARAM_CANON(src), 1020))
                duplicate = VAL_PARAM_SPELLING(src);

            if (definitional_return && src == definitional_return)
                continue;

            Move_Value(dest, src);
            ++dest;
        }

        if (definitional_return) {
            if (flags & MKF_FAKE_RETURN) {
                //
                // This is where you don't actually want a RETURN key in the
                // function frame (e.g. because it's native code and would be
                // wasteful and unused).
                //
                // !!! The debug build uses real returns, not fake ones.
                // This means actions and natives have an extra slot.
                //
            }
            else {
                assert(flags & MKF_RETURN);
                *dest = *definitional_return;
                ++dest;
            }
        }

        // Must remove binder indexes for all words, even if about to fail
        //
        src = DS_AT(dsp_orig + 1) + 3;
        for (; src <= DS_TOP; src += 3, ++dest) {
            if (!Try_Remove_Binder_Index(&binder, VAL_PARAM_CANON(src)))
                assert(duplicate != NULL);
        }

        SHUTDOWN_BINDER(&binder);

        if (duplicate != NULL) {
            DECLARE_LOCAL (word);
            Init_Word(word, duplicate);
            fail (Error_Dup_Vars_Raw(word));
        }

        TERM_ARRAY_LEN(paramlist, num_slots);
        MANAGE_ARRAY(paramlist);
    }

    //=///////////////////////////////////////////////////////////////////=//
    //
    // BUILD META INFORMATION OBJECT (IF NEEDED)
    //
    //=///////////////////////////////////////////////////////////////////=//

    // !!! See notes on FUNCTION-META in %sysobj.r

    REBCTX *meta = NULL;

    if (has_description || has_types || has_notes) {
        meta = Copy_Context_Shallow(VAL_CONTEXT(ROOT_FUNCTION_META));
        MANAGE_ARRAY(CTX_VARLIST(meta));
    }

    SER(paramlist)->link.meta = meta;

    // If a description string was gathered, it's sitting in the first string
    // slot, the third cell we pushed onto the stack.  Extract it if so.
    //
    if (has_description) {
        assert(IS_STRING(DS_AT(dsp_orig + 3)));
        Move_Value(
            CTX_VAR(meta, STD_FUNCTION_META_DESCRIPTION),
            DS_AT(dsp_orig + 3)
        );
    }
    else if (meta)
        Init_Void(CTX_VAR(meta, STD_FUNCTION_META_DESCRIPTION));

    // Only make `parameter-types` if there were blocks in the spec
    //
    if (NOT(has_types)) {
        if (meta) {
            Init_Void(CTX_VAR(meta, STD_FUNCTION_META_PARAMETER_TYPES));
            Init_Void(CTX_VAR(meta, STD_FUNCTION_META_RETURN_TYPE));
        }
    }
    else {
        REBARR *types_varlist = Make_Array_Core(
            num_slots, ARRAY_FLAG_VARLIST
        );
        INIT_CTX_KEYLIST_SHARED(CTX(types_varlist), paramlist);

        REBVAL *dest = SINK(ARR_HEAD(types_varlist)); // "rootvar"
        VAL_RESET_HEADER(dest, REB_FRAME);
        dest->payload.any_context.varlist = types_varlist; // canon FRAME!
        dest->payload.any_context.phase = FUN(paramlist);
        INIT_BINDING(dest, UNBOUND);

        ++dest;

        REBVAL *src = DS_AT(dsp_orig + 2);
        src += 3;
        for (; src <= DS_TOP; src += 3) {
            assert(IS_BLOCK(src));
            if (definitional_return && src == definitional_return + 1)
                continue;

            if (VAL_ARRAY_LEN_AT(src) == 0)
                Init_Void(dest);
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
            if (VAL_ARRAY_LEN_AT(definitional_return + 1) == 0)
                Init_Void(CTX_VAR(meta, STD_FUNCTION_META_RETURN_TYPE));
            else {
                Move_Value(
                    CTX_VAR(meta, STD_FUNCTION_META_RETURN_TYPE),
                    &definitional_return[1]
                );
            }

            if (NOT(flags & MKF_FAKE_RETURN)) {
                Init_Void(dest); // clear the local RETURN: var's description
                ++dest;
            }
        }

        TERM_ARRAY_LEN(types_varlist, num_slots);
        MANAGE_ARRAY(types_varlist);

        Init_Any_Context(
            CTX_VAR(meta, STD_FUNCTION_META_PARAMETER_TYPES),
            REB_FRAME,
            CTX(types_varlist)
        );
    }

    // Only make `parameter-notes` if there were strings (besides description)
    //
    if (NOT(has_notes)) {
        if (meta) {
            Init_Void(CTX_VAR(meta, STD_FUNCTION_META_PARAMETER_NOTES));
            Init_Void(CTX_VAR(meta, STD_FUNCTION_META_RETURN_NOTE));
        }
    }
    else {
        REBARR *notes_varlist = Make_Array_Core(
            num_slots, ARRAY_FLAG_VARLIST
        );
        INIT_CTX_KEYLIST_SHARED(CTX(notes_varlist), paramlist);

        REBVAL *dest = SINK(ARR_HEAD(notes_varlist)); // "rootvar"
        VAL_RESET_HEADER(dest, REB_FRAME);
        dest->payload.any_context.varlist = notes_varlist; // canon FRAME!
        dest->payload.any_context.phase = FUN(paramlist);
        INIT_BINDING(dest, UNBOUND);

        ++dest;

        REBVAL *src = DS_AT(dsp_orig + 3);
        src += 3;
        for (; src <= DS_TOP; src += 3) {
            assert(IS_STRING(src));
            if (definitional_return && src == definitional_return + 2)
                continue;

            if (SER_LEN(VAL_SERIES(src)) == 0)
                Init_Void(dest);
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
                Init_Void(CTX_VAR(meta, STD_FUNCTION_META_RETURN_NOTE));
            else {
                Move_Value(
                    CTX_VAR(meta, STD_FUNCTION_META_RETURN_NOTE),
                    &definitional_return[2]
                );
            }

            if (NOT(flags & MKF_FAKE_RETURN)) {
                Init_Void(dest);
                ++dest;
            }
        }

        TERM_ARRAY_LEN(notes_varlist, num_slots);
        MANAGE_ARRAY(notes_varlist);

        Init_Any_Context(
            CTX_VAR(meta, STD_FUNCTION_META_PARAMETER_NOTES),
            REB_FRAME,
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
            || canon == VAL_PARAM_CANON(param)
        ) {
            return n;
        }
    }

    return 0;
}


//
//  Make_Function: C
//
// Create an archetypal form of a function, given C code implementing a
// dispatcher that will be called by Do_Core.  Dispatchers are of the form:
//
//     REB_R Dispatcher(REBFRM *f) {...}
//
// The REBFUN returned is "archetypal" because individual REBVALs which hold
// the same REBFUN may differ in a per-REBVAL piece of "instance" data.
// (This is how one RETURN is distinguished from another--the instance
// data stored in the REBVAL identifies the pointer of the FRAME! to exit).
//
// Functions have an associated REBVAL-sized cell of data, accessible via
// FUNC_BODY().  This is where they can store information that will be
// available when the dispatcher is called.  Despite being called "body", it
// doesn't have to be an array--it can be any REBVAL.
//
REBFUN *Make_Function(
    REBARR *paramlist,
    REBNAT dispatcher, // native C function called by Do_Core
    REBARR *opt_facade, // if provided, 0 element must be underlying function
    REBCTX *opt_exemplar // if provided, should be consistent w/next level
){
    ASSERT_ARRAY_MANAGED(paramlist);

    RELVAL *rootparam = ARR_HEAD(paramlist);
    assert(IS_FUNCTION(rootparam)); // !!! body not fully formed...
    assert(rootparam->payload.function.paramlist == paramlist);
    assert(VAL_BINDING(rootparam) == UNBOUND); // archetype

    // Precalculate FUNC_FLAG_DEFERS_LOOKBACK
    //
    // Note that this flag is only relevant for *un-refined-calls*.  There
    // are no lookback function calls via PATH! and brancher dispatch is done
    // from a raw function value.  HOWEVER: specialization does come into play
    // because it may change what the first "real" argument is.  But again,
    // we're only interested in specialization's removal of *non-refinement*
    // arguments.  Looking at the surface interface is good enough--that is
    // what will be relevant after the specializations are accounted for.

    REBVAL *param = KNOWN(rootparam) + 1;
    for (; NOT_END(param); ++param) {
        switch (VAL_PARAM_CLASS(param)) {
        case PARAM_CLASS_LOCAL:
        case PARAM_CLASS_RETURN:
        case PARAM_CLASS_LEAVE:
            break; // skip.

        case PARAM_CLASS_REFINEMENT:
            //
            // hit before hitting any basic args, so not a brancher, and not
            // a candidate for deferring lookback arguments.
            //
            goto done_caching;

        case PARAM_CLASS_NORMAL:
            //
            // First argument is not tight, cache flag to report it.
            //
            SET_VAL_FLAG(rootparam, FUNC_FLAG_DEFERS_LOOKBACK);
            goto done_caching;

        // Otherwise, at least one argument but not one that requires the
        // deferring of lookback.

        case PARAM_CLASS_TIGHT:
            //
            // First argument is tight, no flag needed
            //
            goto done_caching;

        case PARAM_CLASS_HARD_QUOTE:
        case PARAM_CLASS_SOFT_QUOTE:
            SET_VAL_FLAG(rootparam, FUNC_FLAG_QUOTES_FIRST_ARG);
            goto done_caching;

        default:
            assert(FALSE);
        }
    }

done_caching:;

    // The "body" for a function can be any REBVAL.  It doesn't have to be
    // a block--it's anything that the dispatcher might wish to interpret.

    REBARR *body_holder = Alloc_Singular_Array();
    Init_Blank(ARR_HEAD(body_holder));
    MANAGE_ARRAY(body_holder);

    rootparam->payload.function.body_holder = body_holder;

    // The C function pointer is stored inside the REBSER node for the body.
    // Hence there's no need for a `switch` on a function class in Do_Core,
    // Having a level of indirection from the REBVAL bits themself also
    // facilitates the "Hijacker" to change multiple REBVALs behavior.

    SER(body_holder)->misc.dispatcher = dispatcher;

    // When this function is run, it needs to push a stack frame with a
    // certain number of arguments, and do type checking and parameter class
    // conventions based on that.  This frame must be compatible with the
    // number of arguments expected by the underlying function, and must not
    // allow any types to be passed to that underlying function it is not
    // expecting (e.g. natives written to only take INTEGER! may crash if
    // they get BLOCK!).  But beyond those constraints, the outer function
    // may have new parameter classes through a "facade".  This facade is
    // initially just the underlying function's paramlist, but may change.
    //
    if (opt_facade == NULL) {
        //
        // To avoid NULL checking when a function is called and looking for
        // the facade, just use the functions own paramlist if needed.  See
        // notes in Make_Paramlist_Managed_May_Fail() on why this has to be
        // pre-filled to avoid crashing on CTX_KEYLIST when making frames.
        //
        assert(SER(paramlist)->misc.facade == paramlist);
    }
    else
        SER(paramlist)->misc.facade = opt_facade;

    if (opt_exemplar == NULL) {
        //
        // !!! There may be some efficiency hack where this could be END, so
        // that when a REBFRM's ->special field is set there's no need to
        // check for NULL.
        //
        SER(body_holder)->link.exemplar = NULL;
    }
    else {
        // Because a dispatcher can update the phase and swap in the next
        // function with R_REDO_XXX, consistency checking isn't easily
        // done on whether the exemplar is "compatible" (and there may be
        // dispatcher forms which intentionally muck with the exemplar to
        // be incompatible, but these don't exist yet.)  So just check it's
        // compatible with the underlying frame.
        //
        // Base it off the facade since FUNC_NUM_PARAMS(FUNC_UNDERLYING())
        // would assert, since the function we're making is incomplete..
        //
        assert(
            CTX_LEN(opt_exemplar)
            == ARR_LEN(SER(paramlist)->misc.facade) - 1
        );

        SER(body_holder)->link.exemplar = opt_exemplar;
    }

    // The meta information may already be initialized, since the native
    // version of paramlist construction sets up the FUNCTION-META information
    // used by HELP.  If so, it must be a valid REBCTX*.  Otherwise NULL.
    //
    assert(
        SER(paramlist)->link.meta == NULL
        || GET_SER_FLAG(
            CTX_VARLIST(SER(paramlist)->link.meta), ARRAY_FLAG_VARLIST
        )
    );

    // Note: used to set the keys of natives as read-only so that the debugger
    // couldn't manipulate the values in a native frame out from under it,
    // potentially crashing C code (vs. just causing userspace code to
    // error).  That protection is now done to the frame series on reification
    // in order to be able to MAKE FRAME! and reuse the native's paramlist.

    assert(NOT_SER_FLAG(paramlist, SERIES_FLAG_FILE_LINE));
    assert(NOT_SER_FLAG(body_holder, SERIES_FLAG_FILE_LINE));

    return FUN(paramlist);
}


//
//  Make_Expired_Frame_Ctx_Managed: C
//
// Function bodies contain relative words and relative arrays.  Arrays from
// this relativized body may only be put into a specified REBVAL once they
// have been combined with a frame.
//
// Reflection asks for function body data, when no instance is called.  Hence
// a REBVAL must be produced somehow.  If the body is being copied, then the
// option exists to convert all the references to unbound...but this isn't
// representative of the actual connections in the body.
//
// There could be an additional "archetype" state for the relative binding
// machinery.  But making a one-off expired frame is an inexpensive option,
// at least while the specific binding is coming online.
//
// !!! To be written...was started for MOLD of function, and realized it's
// really only needed for the BODY-OF reflector that gives back REBVAL*
//
REBCTX *Make_Expired_Frame_Ctx_Managed(REBFUN *func)
{
    REBARR *varlist = Alloc_Singular_Array_Core(ARRAY_FLAG_VARLIST);
    SET_SER_INFO(varlist, CONTEXT_INFO_STACK);
    Init_Blank(ARR_HEAD(varlist));
    MANAGE_ARRAY(varlist);

    SET_SER_INFO(varlist, SERIES_INFO_INACCESSIBLE);

    REBCTX *expired = CTX(varlist);

    INIT_CTX_KEYLIST_SHARED(expired, FUNC_PARAMLIST(func));

    CTX_VALUE(expired)->payload.any_context.varlist = varlist;

    // A NULL stored by the misc field of a REB_FRAME context's varlist which
    // indicates that the frame has finished running.  If it is stack-based,
    // then that also means the data values are unavailable.
    //
    SER(varlist)->misc.f = NULL;

    return expired;
}


//
//  Get_Maybe_Fake_Func_Body: C
//
// The FUNC_FLAG_LEAVE and FUNC_FLAG_RETURN tricks used for definitional
// scoping make it seem like a generator authored more code in the function's
// body...but the code isn't *actually* there and an optimized internal
// trick is used.
//
// If the body is fake, it needs to be freed by the caller with
// Free_Series.  This means that the body must currently be shallow
// copied, and the splicing slot must be in the topmost series.
//
REBARR *Get_Maybe_Fake_Func_Body(REBOOL *is_fake, const REBVAL *func)
{
    REBARR *fake_body;
    REBVAL *example = NULL;

    assert(IS_FUNCTION(func) && IS_FUNCTION_INTERPRETED(func));

    REBCNT body_index;
    if (GET_VAL_FLAG(func, FUNC_FLAG_RETURN)) {
        if (GET_VAL_FLAG(func, FUNC_FLAG_LEAVE)) {
            example = Get_System(SYS_STANDARD, STD_FUNC_BODY);
            body_index = 8;
        }
        else {
            example = Get_System(SYS_STANDARD, STD_FUNC_NO_LEAVE_BODY);
            body_index = 4;
        }
        *is_fake = TRUE;
    }
    else if (GET_VAL_FLAG(func, FUNC_FLAG_LEAVE)) {
        example = Get_System(SYS_STANDARD, STD_PROC_BODY);
        body_index = 4;
        *is_fake = TRUE;
    }
    else {
        *is_fake = FALSE;
        return VAL_ARRAY(VAL_FUNC_BODY(func));
    }

    // See comments in sysobj.r on standard/func-body and standard/proc-body
    //
    fake_body = Copy_Array_Shallow(VAL_ARRAY(example), VAL_SPECIFIER(example));

    // Index 5 (or 4 in zero-based C) should be #BODY, a "real" body.  Since
    // the body has relative words and relative arrays and this is not pairing
    // that with a frame from any specific invocation, the value must be
    // marked as relative.
    {
        RELVAL *slot = ARR_AT(fake_body, body_index); // #BODY
        assert(IS_ISSUE(slot));

        VAL_RESET_HEADER_EXTRA(slot, REB_GROUP, VALUE_FLAG_LINE);
        INIT_VAL_ARRAY(slot, VAL_ARRAY(VAL_FUNC_BODY(func)));
        VAL_INDEX(slot) = 0;
        INIT_BINDING(slot, VAL_FUNC(func)); // relative binding
    }

    return fake_body;
}


//
//  Make_Interpreted_Function_May_Fail: C
//
// This is the support routine behind `MAKE FUNCTION!`, FUNC, and PROC.
//
// Ren/C's schematic for the FUNC and PROC generators is *very* different
// from R3-Alpha, whose definition of FUNC was simply:
//
//     make function! copy/deep reduce [spec body]
//
// Ren/C's `make function!` doesn't need to copy the spec (it does not save
// it--parameter descriptions are in a meta object).  It also copies the body
// by virtue of the need to relativize it.  They also have "definitional
// return" constructs so that the body introduces RETURN and LEAVE constructs
// specific to each function invocation, so the body acts more like:
//
//     return: make function! [
//         [{Returns a value from a function.} value [<opt> any-value!]]
//         [exit/from/with (context-of 'return) :value]
//     ]
//     (body goes here)
//
// This pattern addresses "Definitional Return" in a way that does not
// technically require building RETURN or LEAVE in as a language keyword in
// any specific form (in the sense that MAKE FUNCTION! does not itself
// require it, and one can pretend FUNC and PROC don't exist).
//
// FUNC and PROC optimize by not internally building or executing the
// equivalent body, but giving it back from BODY-OF.  This is another benefit
// of making a copy--since the user cannot access the new root, it makes it
// possible to "lie" about what the body "above" is.  This gives FUNC and PROC
// the edge to pretend to add containing code and simulate its effects, while
// really only holding onto the body the caller provided.
//
// While MAKE FUNCTION! has no RETURN, all functions still have EXIT as a
// non-definitional alternative.  Ren/C adds a /WITH refinement so it can
// behave equivalently to old-non-definitonal return.  There is even a way to
// identify specific points up the call stack to exit from via EXIT/FROM, so
// not having definitional return has several alternate options for generators
// that wish to use them.
//
REBFUN *Make_Interpreted_Function_May_Fail(
    const REBVAL *spec,
    const REBVAL *code,
    REBFLGS mkf_flags // MKF_RETURN, MKF_LEAVE, etc.
) {
    assert(IS_BLOCK(spec));
    assert(IS_BLOCK(code));

    REBFUN *fun = Make_Function(
        Make_Paramlist_Managed_May_Fail(spec, mkf_flags),
        &Noop_Dispatcher, // will be overwritten if non-NULL body
        NULL, // no facade (use paramlist)
        NULL // no specialization exemplar (or inherited exemplar)
    );

    // We look at the *actual* function flags; e.g. the person may have used
    // the FUNC generator (with MKF_RETURN) but then named a parameter RETURN
    // which overrides it, so the value won't have FUNC_FLAG_RETURN.
    //
    REBVAL *value = FUNC_VALUE(fun);

    REBARR *body_array;
    if (VAL_ARRAY_LEN_AT(code) == 0) {
        if (GET_VAL_FLAG(value, FUNC_FLAG_RETURN)) {
            //
            // Since we're bypassing type checking in the dispatcher for
            // speed, we need to make sure that the return type allows void
            // (which is all the Noop dispatcher will return).  If not, we
            // don't want to fail here (it would reveal the optimization)...
            // just fall back on the Returner_Dispatcher instead.
            //
            REBVAL *typeset = FUNC_PARAM(fun, FUNC_NUM_PARAMS(fun));
            assert(VAL_PARAM_SYM(typeset) == SYM_RETURN);
            if (!TYPE_CHECK(typeset, REB_MAX_VOID))
                FUNC_DISPATCHER(fun) = &Returner_Dispatcher;
        }

        body_array = EMPTY_ARRAY; // just reuse empty array if empty, no copy
    }
    else {
        // Body is not empty, so we need to pick the right dispatcher based
        // on how the output value is to be handled.
        //
        if (GET_VAL_FLAG(value, FUNC_FLAG_RETURN))
            FUNC_DISPATCHER(fun) = &Returner_Dispatcher; // type checks f->out
        else if (GET_VAL_FLAG(value, FUNC_FLAG_LEAVE))
            FUNC_DISPATCHER(fun) = &Voider_Dispatcher; // forces f->out void
        else
            FUNC_DISPATCHER(fun) = &Unchecked_Dispatcher; // leaves f->out

        // We need to copy the body in order to relativize its references to
        // args and locals to refer to the parameter list.  Future work
        // might be able to "image" the bindings virtually, and not require
        // this to be copied if the input code is read-only.
        //
        body_array = Copy_And_Bind_Relative_Deep_Managed(
            code,
            FUNC_PARAMLIST(fun),
            TS_ANY_WORD
        );
    }

    // We need to do a raw initialization of this block RELVAL because it is
    // relative to a function.  (Init_Block assumes all specific values)
    //
    RELVAL *body = FUNC_BODY(fun);
    VAL_RESET_HEADER(body, REB_BLOCK);
    INIT_VAL_ARRAY(body, body_array);
    VAL_INDEX(body) = 0;
    INIT_BINDING(body, fun); // relative binding

#if !defined(NDEBUG)
    //
    // If FUNC or MAKE FUNCTION! are being invoked from an array of code that
    // has been flagged "legacy" (e.g. the body of a function created after
    // `do <r3-legacy>` has been run) then mark the function with the setting
    // to make refinements and args blank instead of FALSE/void...if that
    // option is on.
    //
    if (
        LEGACY_RUNNING(OPTIONS_REFINEMENTS_BLANK)
        || GET_SER_INFO(VAL_ARRAY(spec), SERIES_INFO_LEGACY_DEBUG)
        || GET_SER_INFO(VAL_ARRAY(code), SERIES_INFO_LEGACY_DEBUG)
    ) {
        SET_VAL_FLAG(FUNC_VALUE(fun), FUNC_FLAG_LEGACY_DEBUG);
    }
#endif

    // All the series inside of a function body are "relatively bound".  This
    // means that there's only one copy of the body, but the series handle
    // is "viewed" differently based on which call it represents.  Though
    // each of these views compares uniquely, there's only one series behind
    // it...hence the series must be read only to keep modifying a view
    // that seems to have one identity but then affecting another.
    //
#if defined(NDEBUG)
    Deep_Freeze_Array(VAL_ARRAY(body));
#else
    if (!LEGACY(OPTIONS_UNLOCKED_SOURCE))
        Deep_Freeze_Array(VAL_ARRAY(body));
#endif

    return fun;
}


//
//  Make_Frame_For_Function: C
//
// This creates a *non-stack-allocated* FRAME!, which can be used in function
// applications or specializations.  It reuses the keylist of the function
// but makes a new varlist.
//
REBCTX *Make_Frame_For_Function(const REBVAL *value) {
    //
    // Note that this cannot take just a REBFUN* directly, because definitional
    // RETURN and LEAVE only have their unique `binding` bits in the REBVAL.
    //
    REBFUN *func = VAL_FUNC(value);

    // In order to have the frame survive the call to MAKE and be returned to
    // the user it can't be stack allocated, because it would immediately
    // become useless.  Allocate dynamically.  It will be used as an
    // "exemplar" when executed, so it must be the length of the *underlying*
    // frame of the function call (same length as the "facade")
    //
    // A FRAME! defaults *new* args and locals to not being set.  If the frame
    // is then used as the storage for a function specialization, unset
    // vars indicate *unspecialized* arguments...not <opt> ones.  (This is
    // a good argument for not making <opt> have meaning that is interesting
    // to APPLY or SPECIALIZE cases, but to revoke the function's effects.)
    //
    // But since the frame's varlist is used as an exemplar when running,
    // anything from the function chains in the exemplar needs to make it into
    // the varlist.  These should be invisible to the user due to the
    // paramlist for f->phase not mentioning them, but that is pending.
    //
    REBCTX *exemplar = FUNC_EXEMPLAR(func);
    REBARR *varlist;
    if (exemplar != NULL) {
        //
        // Existing exemplars should already have void in the unspecialized slots.
        //
        varlist = Copy_Array_Shallow(CTX_VARLIST(exemplar), SPECIFIED);
        SET_SER_FLAGS(varlist, ARRAY_FLAG_VARLIST | SERIES_FLAG_FIXED_SIZE);
    }
    else {
        // A FRAME! defaults all args and locals to not being set.  Unset
        // vars indicate *unspecialized* arguments...not <opt> ones.  (This is
        // a good argument for not making <opt> have meaning that interesting
        // to APPLY or SPECIALIZE cases, but to revoke the function's effects.
        //
        varlist = Make_Array_Core(
            ARR_LEN(FUNC_PARAMLIST(func)),
            ARRAY_FLAG_VARLIST | SERIES_FLAG_FIXED_SIZE
        );

        REBVAL *temp = SINK(ARR_HEAD(varlist)) + 1;
        REBCNT n;
        for (n = 1; n <= FUNC_NUM_PARAMS(func); ++n, ++temp)
            Init_Void(temp);

        TERM_ARRAY_LEN(varlist, ARR_LEN(FUNC_PARAMLIST(func)));
    }

    // Fill in the rootvar information for the context canon REBVAL
    //
    REBVAL *var = SINK(ARR_HEAD(varlist));
    VAL_RESET_HEADER(var, REB_FRAME);
    var->payload.any_context.varlist = varlist;
    var->payload.any_context.phase = func;
    INIT_BINDING(var, VAL_BINDING(value));

    // We have to use the keylist of the underlying function, because that
    // is how many values the frame has to have.  So knowing the actual
    // function the frame represents is done with the phase.  Also, for things
    // like definitional RETURN and LEAVE we had to stow the `binding` field
    // in the FRAME! REBVAL, since the single archetype paramlist does not
    // hold enough information to know where to return *to*.
    //
    INIT_CTX_KEYLIST_SHARED(
        CTX(varlist),
        FUNC_PARAMLIST(FUNC_UNDERLYING(func))
    );
    ASSERT_ARRAY_MANAGED(CTX_KEYLIST(CTX(varlist)));

    // While it was once possible to execute a FRAME! value created with
    // `make frame! :some-func`, that had a number of problems.  One is that
    // it exposed the state of a function after its execution; since in
    // Rebol functions are allowed to mutate their arguments, it would mean
    // you had no promise you could reuse the frame after it had been used.
    // Another is that it complicated the "all frames start with their values
    // on the chunk stack".  This should never become non-NULL.
    //
    SER(varlist)->misc.f = NULL;

    return CTX(varlist);
}


//
//  Specialize_Function_Throws: C
//
// This produces a new REBVAL for a function that specializes another.  It
// uses a FRAME! to do this, where the frame intrinsically stores the
// reference to the function it is specializing.
//
REBOOL Specialize_Function_Throws(
    REBVAL *out,
    REBVAL *specializee,
    REBSTR *opt_specializee_name,
    REBVAL *block // !!! REVIEW: gets binding modified directly (not copied)
){
    assert(out != specializee);

    REBCTX *exemplar = Make_Frame_For_Function(specializee);

    // Bind all the SET-WORD! in the body that match params in the frame
    // into the frame.  This means `value: value` can very likely have
    // `value:` bound for assignments into the frame while `value` refers
    // to whatever value was in the context the specialization is running
    // in, but this is likely the more useful behavior.  Review.
    //
    // !!! This binds the actual arg data, not a copy of it--following
    // OBJECT!'s lead.  However, ordinary functions make a copy of the body
    // they are passed before rebinding.  Rethink.
    //
    Bind_Values_Core(
        VAL_ARRAY_AT(block),
        exemplar,
        FLAGIT_KIND(REB_SET_WORD), // types to bind (just set-word!)
        0, // types to "add midstream" to binding as we go (nothing)
        BIND_DEEP
    );

    // Do the block into scratch space--we ignore the result (unless it is
    // thrown, in which case it must be returned.)
    //
    PUSH_GUARD_ARRAY(CTX_VARLIST(exemplar));
    if (Do_Any_Array_At_Throws(out, block)) {
        DROP_GUARD_ARRAY(CTX_VARLIST(exemplar));
        return TRUE;
    }
    DROP_GUARD_ARRAY(CTX_VARLIST(exemplar));

    // Generate paramlist by way of the data stack.  Push inherited value (to
    // become the function value afterward), then all the args that remain
    // unspecialized (indicated by being void...<opt> is not supported)
    //
    REBDSP dsp_orig = DSP;
    DS_PUSH(FUNC_VALUE(VAL_FUNC(specializee))); // !!! is inheriting good?

    REBVAL *param = CTX_KEYS_HEAD(exemplar);
    REBVAL *arg = CTX_VARS_HEAD(exemplar);
    for (; NOT_END(param); ++param, ++arg) {
        if (IS_VOID(arg))
            DS_PUSH(param);

        // !!! Should the VALUE_FLAG_UNEVALUATED bit be set on elements of
        // the exemplar?  If it is not, then attempts to specialize things
        // like branches with raw literals won't work.  Review in light of
        // whatever rules are designed to help make dealing with wrapping
        // the evaluated/unevaluated bit easier.
    }

    REBARR *paramlist = Pop_Stack_Values_Core(
        dsp_orig,
        ARRAY_FLAG_PARAMLIST | SERIES_FLAG_FIXED_SIZE
    );
    MANAGE_ARRAY(paramlist);

    RELVAL *rootparam = ARR_HEAD(paramlist);
    rootparam->payload.function.paramlist = paramlist;

    // Frames for specialized functions contain the number of parameters of
    // the underlying function, while they should for practical purposes seem
    // to have the number of parameters of the specialization.  It would
    // be technically possible to complicate enumeration to look for symbol
    // matches in the shortened paramlist and line them up with the full
    // underlying function's paramlist, but it's easier to use the "facade"
    // mechanic to create a compatible paramlist with items that are hidden
    // from binding...and use that for enumerations like FOR-EACH etc.
    //
    // Note that facades are like paramlists, but distinct because the [0]
    // element is not the function the paramlist is for, but the underlying
    // function REBVAL.
    //
    DS_PUSH(FUNC_VALUE(FUNC_UNDERLYING(VAL_FUNC(specializee))));
    param = CTX_KEYS_HEAD(exemplar);
    arg = CTX_VARS_HEAD(exemplar);
    for (; NOT_END(param); ++param, ++arg) {
        DS_PUSH(param);
        if (NOT(IS_VOID(arg)))
            SET_VAL_FLAG(DS_TOP, TYPESET_FLAG_HIDDEN);
    }

    REBARR *facade = Pop_Stack_Values_Core(dsp_orig, SERIES_FLAG_FIXED_SIZE);
    MANAGE_ARRAY(facade);

    // See %sysobj.r for `specialized-meta:` object template

    REBVAL *example = Get_System(SYS_STANDARD, STD_SPECIALIZED_META);

    REBCTX *meta = Copy_Context_Shallow(VAL_CONTEXT(example));

    Init_Void(CTX_VAR(meta, STD_SPECIALIZED_META_DESCRIPTION)); // default
    Move_Value(
        CTX_VAR(meta, STD_SPECIALIZED_META_SPECIALIZEE),
        specializee
    );
    if (opt_specializee_name == NULL)
        Init_Void(CTX_VAR(meta, STD_SPECIALIZED_META_SPECIALIZEE_NAME));
    else
        Init_Word(
            CTX_VAR(meta, STD_SPECIALIZED_META_SPECIALIZEE_NAME),
            opt_specializee_name
        );

    MANAGE_ARRAY(CTX_VARLIST(meta));
    SER(paramlist)->link.meta = meta;

    REBFUN *fun = Make_Function(
        paramlist,
        &Specializer_Dispatcher,
        facade, // use facade with specialized parameters flagged hidden
        exemplar // also provide a context of specialization values
    );

    // The "body" is the FRAME! value of the specialization.  Though we may
    // not be able to touch the keylist of that frame to update the "archetype"
    // binding, we can patch this cell in the "body array" to hold it.
    //
    Move_Value(FUNC_BODY(fun), CTX_VALUE(exemplar));
    assert(VAL_BINDING(FUNC_BODY(fun)) == VAL_BINDING(specializee));

    Move_Value(out, FUNC_VALUE(fun));
    assert(VAL_BINDING(out) == UNBOUND);

    return FALSE;
}


//
//  Clonify_Function: C
//
// (A "Clonify" interface takes in a raw duplicate value that one wishes to
// mutate in-place into a full-fledged copy of the value it is a clone of.
// This interface can be more efficient than a "source in, dest out" copy...
// and clarifies the dangers when the source and destination are the same.)
//
// !!! Function bodies in R3-Alpha were mutable.  This meant that you could
// effectively have static data in cases like:
//
//     foo: does [static: [] | append static 1]
//
// Hence, it was meaningful to be able to COPY a function; because that copy
// would get any such static state snapshotted at wherever it was in time.
//
// Ren-C eliminated this idea.  But functions are still copied in the special
// case of object "member functions", so that each "derived" object will
// have functions with bindings to its specific context variables.  Some
// plans are in the work to use function REBVAL's `binding` parameter to
// make a lighter-weight way of connecting methods to objects without actually
// needing to mutate the archetypal REBFUN to do so ("virtual binding").
//
void Clonify_Function(REBVAL *value)
{
    assert(IS_FUNCTION(value));

    // Function compositions point downwards through their layers in a linked
    // list.  Each step in the chain has identity, and we need a copied
    // identity for all steps that require a copy and everything *above* it.
    // So for instance, although R3-Alpha did not see a need to copy natives,
    // if you ADAPT a native with code, the adapting Rebol code may need to
    // take into account new bindings to a derived object...just as the body
    // to an interpreted function would.
    //
    // !!! For the moment, this work is not done...and only functions that
    // are raw interpreted functions are cloned.  That means old code will
    // stay compatible but new features won't necessarily work the same way
    // with object binding.  All of this needs to be rethought in light of
    // "virtual binding" anyway!
    //
    if (!IS_FUNCTION_INTERPRETED(value))
        return;

    REBFUN *original_fun = VAL_FUNC(value);
    REBARR *paramlist = Copy_Array_Shallow(
        FUNC_PARAMLIST(original_fun),
        SPECIFIED
    );
    SET_SER_FLAG(paramlist, ARRAY_FLAG_PARAMLIST);
    MANAGE_ARRAY(paramlist);
    ARR_HEAD(paramlist)->payload.function.paramlist = paramlist;

    // !!! Meta: copy, inherit?
    //
    SER(paramlist)->link.meta = FUNC_META(original_fun);
    SER(paramlist)->misc.facade = paramlist;

    REBFUN *new_fun = Make_Function(
        paramlist,
        FUNC_DISPATCHER(original_fun),
        NULL, // no facade (use paramlist)
        NULL // no specialization exemplar (or inherited exemplar)
    );

    RELVAL *body = FUNC_BODY(new_fun);

    // Since we rebind the body, we need to instruct the interpreted dispatcher
    // that it's o.k. to tell the frame lookup that it can find variables
    // under the "new paramlist".
    //
    VAL_RESET_HEADER(body, REB_BLOCK);
    INIT_VAL_ARRAY(
        body,
        Copy_Rerelativized_Array_Deep_Managed(
            VAL_ARRAY(FUNC_BODY(original_fun)),
            original_fun,
            FUN(paramlist)
        )
    );
    VAL_INDEX(body) = 0;
    INIT_BINDING(body, paramlist); // relative binding

    Move_Value(value, FUNC_VALUE(new_fun));
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
    UNUSED(action);

    fail ("Datatype does not have a dispatcher registered.");
}


//
//  Action_Dispatcher: C
//
// "actions" are historically a kind of dispatch based on the first argument's
// type, and then calling a common function for that type parameterized with
// a word for the action.  e.g. APPEND X [...] would look at the type of X,
// and call a function based on that parameterized with APPEND and the list
// of arguments.
//
REB_R Action_Dispatcher(REBFRM *f)
{
    enum Reb_Kind type = VAL_TYPE(FRM_ARG(f, 1));
    assert(type < REB_MAX); // actions should not allow void first arguments
    REBSYM sym = STR_SYMBOL(VAL_WORD_SPELLING(FUNC_BODY(f->phase)));
    assert(sym != SYM_0);

    REBACT subdispatch = Value_Dispatch[type];
    return subdispatch(f, sym);
}


//
//  Noop_Dispatcher: C
//
// If a function's body is an empty block, rather than bother running the
// equivalent of `DO []` and generating a frame for specific binding, this
// just returns void.  What makes this a semi-interesting optimization is
// for functions like ASSERT whose default implementation is an empty block,
// but intended to be hijacked in "debug mode" with an implementation.  So
// you can minimize the cost of instrumentation hooks.
//
REB_R Noop_Dispatcher(REBFRM *f)
{
    UNUSED(f);
    return R_VOID;
}


//
//  Datatype_Checker_Dispatcher: C
//
// Dispatcher used by TYPECHECKER generator for when argument is a datatype.
//
REB_R Datatype_Checker_Dispatcher(REBFRM *f)
{
    RELVAL *datatype = FUNC_BODY(f->phase);
    assert(IS_DATATYPE(datatype));
    if (VAL_TYPE(FRM_ARG(f, 1)) == VAL_TYPE_KIND(datatype))
        return R_TRUE;
    return R_FALSE;
}


//
//  Typeset_Checker_Dispatcher: C
//
// Dispatcher used by TYPECHECKER generator for when argument is a typeset.
//
REB_R Typeset_Checker_Dispatcher(REBFRM *f)
{
    RELVAL *typeset = FUNC_BODY(f->phase);
    assert(IS_TYPESET(typeset));
    if (TYPE_CHECK(typeset, VAL_TYPE(FRM_ARG(f, 1))))
        return R_TRUE;
    return R_FALSE;
}


//
//  Unchecked_Dispatcher: C
//
// This is the default MAKE FUNCTION! dispatcher for interpreted functions
// (whose body is a block that runs through DO []).  There is no return type
// checking done on these simple functions.
//
REB_R Unchecked_Dispatcher(REBFRM *f)
{
    RELVAL *body = FUNC_BODY(f->phase);
    assert(IS_BLOCK(body) && IS_RELATIVE(body) && VAL_INDEX(body) == 0);

    if (Do_At_Throws(
        f->out,
        VAL_ARRAY(body),
        0, // VAL_INDEX(body) asserted 0 above
        AS_SPECIFIER(f)
    )){
        return R_OUT_IS_THROWN;
    }

    return R_OUT;
}


//
//  Voider_Dispatcher: C
//
// Variant of Unchecked_Dispatcher, except sets the output value to void.
// Pushing that code into the dispatcher means there's no need to do flag
// testing in the main loop.
//
REB_R Voider_Dispatcher(REBFRM *f)
{
    RELVAL *body = FUNC_BODY(f->phase);
    assert(IS_BLOCK(body) && IS_RELATIVE(body) && VAL_INDEX(body) == 0);

    if (Do_At_Throws(
        f->out,
        VAL_ARRAY(body),
        0, // VAL_INDEX(body) asserted 0 above
        AS_SPECIFIER(f)
    )){
        return R_OUT_IS_THROWN;
    }

    return R_VOID;
}


//
//  Returner_Dispatcher: C
//
// Contrasts with the Unchecked_Dispatcher since it ensures the return type is
// correct.  (Note that natives do not get this type checking, and they
// probably shouldn't pay for it except in the debug build.)
//
REB_R Returner_Dispatcher(REBFRM *f)
{
    RELVAL *body = FUNC_BODY(f->phase);
    assert(IS_BLOCK(body) && IS_RELATIVE(body) && VAL_INDEX(body) == 0);

    if (Do_At_Throws(
        f->out,
        VAL_ARRAY(body),
        0, // VAL_INDEX(body) asserted 0 above
        AS_SPECIFIER(f)
    )){
        return R_OUT_IS_THROWN;
    }

    REBVAL *typeset = FUNC_PARAM(f->phase, FUNC_NUM_PARAMS(f->phase));
    assert(VAL_PARAM_SYM(typeset) == SYM_RETURN);

    // The type bits of the definitional return are not applicable
    // to the `return` word being associated with a FUNCTION!
    // vs. an INTEGER! (for instance).  It is where the type
    // information for the non-existent return function specific
    // to this call is hidden.
    //
    if (!TYPE_CHECK(typeset, VAL_TYPE(f->out)))
        fail (Error_Bad_Return_Type(f, VAL_TYPE(f->out)));

    return R_OUT;
}


//
//  Specializer_Dispatcher: C
//
// The evaluator does not do any special "running" of a specialized frame.
// All of the contribution that the specialization has to make was taken care
// of at the time of generating the arguments to the underlying function.
//
// Though an attempt is made to use the work of "digging" past specialized
// frames, some exist deep as chains of specializations etc.  These have
// to just be peeled off when the chain runs.
//
REB_R Specializer_Dispatcher(REBFRM *f)
{
    REBVAL *exemplar = KNOWN(FUNC_BODY(f->phase));
    f->phase = exemplar->payload.any_context.phase;
    f->binding = VAL_BINDING(exemplar);

    return R_REDO_UNCHECKED;
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
    RELVAL *hijacker = FUNC_BODY(f->phase);

    // We need to build a new frame compatible with the hijacker, and
    // transform the parameters we've gathered to be compatible with it.
    //
    if (Redo_Func_Throws(f, VAL_FUNC(hijacker)))
        return R_OUT_IS_THROWN;

    return R_OUT;
}


//
//  Adapter_Dispatcher: C
//
// Dispatcher used by ADAPT.
//
REB_R Adapter_Dispatcher(REBFRM *f)
{
    RELVAL *adaptation = FUNC_BODY(f->phase);
    assert(ARR_LEN(VAL_ARRAY(adaptation)) == 2);

    RELVAL* prelude = VAL_ARRAY_AT_HEAD(adaptation, 0);
    REBVAL* adaptee = KNOWN(VAL_ARRAY_AT_HEAD(adaptation, 1));

    // The first thing to do is run the prelude code, which may throw.  If it
    // does throw--including a RETURN--that means the adapted function will
    // not be run.
    //
    // (Note that when the adapter was created, the prelude code was bound to
    // the paramlist of the *underlying* function--because that's what a
    // compatible frame gets pushed for.)
    //
    if (Do_At_Throws(
        f->out,
        VAL_ARRAY(prelude),
        VAL_INDEX(prelude),
        AS_SPECIFIER(f)
    )){
        return R_OUT_IS_THROWN;
    }

    f->phase = VAL_FUNC(adaptee);
    f->binding = VAL_BINDING(adaptee);
    return R_REDO_CHECKED; // Have Do_Core run the adaptee updated into f->phase
}


//
//  Chainer_Dispatcher: C
//
// Dispatcher used by CHAIN.
//
REB_R Chainer_Dispatcher(REBFRM *f)
{
    REBVAL *pipeline = KNOWN(FUNC_BODY(f->phase)); // array of functions

    // Before skipping off to find the underlying non-chained function
    // to kick off the execution, the post-processing pipeline has to
    // be "pushed" so it is not forgotten.  Go in reverse order so
    // the function to apply last is at the bottom of the stack.
    //
    REBVAL *value = KNOWN(ARR_LAST(VAL_ARRAY(pipeline)));
    while (value != VAL_ARRAY_HEAD(pipeline)) {
        assert(IS_FUNCTION(value));
        DS_PUSH(KNOWN(value));
        --value;
    }

    // Extract the first function, itself which might be a chain.
    //
    f->phase = VAL_FUNC(value);
    f->binding = VAL_BINDING(value);

    return R_REDO_UNCHECKED; // signatures should match
}


//
//  Get_If_Word_Or_Path_Arg: C
//
// Some routines like APPLY and SPECIALIZE are willing to take a WORD! or
// PATH! instead of just the value type they are looking for, and perform
// the GET for you.  By doing the GET inside the function, they are able
// to preserve the symbol:
//
//     >> apply 'append [value: 'c]
//     ** Script error: append is missing its series argument
//
void Get_If_Word_Or_Path_Arg(
    REBVAL *out,
    REBSTR **opt_name_out,
    const REBVAL *value
) {
    DECLARE_LOCAL (adjusted);
    Move_Value(adjusted, value);

    if (ANY_WORD(value)) {
        *opt_name_out = VAL_WORD_SPELLING(value);
        VAL_SET_TYPE_BITS(adjusted, REB_GET_WORD);
    }
    else if (ANY_PATH(value)) {
        //
        // In theory we could get a symbol here, assuming we only do non
        // evaluated GETs.  Not implemented at the moment.
        //
        *opt_name_out = NULL;
        VAL_SET_TYPE_BITS(adjusted, REB_GET_PATH);
    }
    else {
        *opt_name_out = NULL;
        Move_Value(out, value);
        return;
    }

    if (Eval_Value_Throws(out, adjusted)) {
        //
        // !!! GET_PATH should not evaluate GROUP!, and hence shouldn't be
        // able to throw.  TBD.
        //
        fail (Error_No_Catch_For_Throw(out));
    }
}


//
//  Apply_Def_Or_Exemplar: C
//
// Factors out common code used by DO of a FRAME!, and APPLY.
//
// !!! Because APPLY is being written as a regular native (and not a
// special exception case inside of Do_Core) it has to "re-enter" Do_Core
// and jump to the argument processing.  This is the first example of
// such a re-entry, and is not particularly streamlined yet.
//
// This could also be accomplished if function dispatch were a subroutine
// that would be called both here and from the evaluator loop.  But if
// the subroutine were parameterized with the frame state, it would be
// basically equivalent to a re-entry.  And re-entry is interesting to
// experiment with for other reasons (e.g. continuations), so that is what
// is used here.
//
REB_R Apply_Def_Or_Exemplar(
    REBVAL *out,
    REBFUN *fun,
    REBNOD *binding,
    REBSTR *label,
    REBNOD *def_or_exemplar // REBVAL of a def block, or REBARR varlist
){
    DECLARE_FRAME (f);

    f->out = out;
    TRASH_POINTER_IF_DEBUG(f->gotten); // shouldn't be looked at (?)
    f->binding = binding;

    f->eval_type = REB_FUNCTION;
    SET_FRAME_LABEL(f, label);

    // We pretend our "input source" has ended.
    //
    SET_FRAME_VALUE(f, END);
    f->index = 0;
    f->source.array = EMPTY_ARRAY;
    f->specifier = SPECIFIED;
    TRASH_POINTER_IF_DEBUG(f->pending);

    f->dsp_orig = DSP;

    Init_Endlike_Header(&f->flags, DO_FLAG_APPLYING);

    // !!! We have to push a call here currently because prior to specific
    // binding, the stack gets walked to resolve variables.   Hence in the
    // apply case, Do_Core doesn't do its own push to the frame stack.
    //
    Push_Frame_Core(f);

#if !defined(NDEBUG)
    //
    // We may push a data chunk, which is one of the things the snapshot state
    // checks.  It also checks the top of stack, so that has to be set as well.
    // So this has to come before Push_Args
    //
    SNAP_STATE(&f->state_debug);
#endif

    f->refine = m_cast(REBVAL*, END);

    Push_Args_For_Underlying_Func(f, fun, binding);

    if (NOT(def_or_exemplar->header.bits & NODE_FLAG_CELL)) {
        //
        // When you DO a FRAME!, it feeds its varlist in to be copied into
        // the stack positions.
        //
        assert(def_or_exemplar->header.bits & ARRAY_FLAG_VARLIST);
        REBCTX *exemplar = CTX(def_or_exemplar);
        f->special = CTX_VARS_HEAD(exemplar);
    }
    else {
        // The APPLY native takes in a block of code that needs to be bound
        // and run to fill the frame.
        //
        REBVAL *def = cast(REBVAL*, def_or_exemplar);

        // Ordinary function dispatch does not pre-fill the arguments; they
        // are left as garbage until the parameter enumeration gets to them.
        // If the function has an exemplar, or if a pre-built FRAME! is being
        // used, those fields are copied one by one into the stack.
        //
        // But when we are doing a one-off APPLY with a BLOCK!, we don't
        // want to make an "exemplar" object for that one usage, and then
        // go through and copy it into the frame.  It's better to go ahead
        // and DO the block with the bindings into the frame variables.
        // But to do this user code can't see garbage, go ahead and fill it.
        // This means we have to take over the filling from the exemplar,
        // and the only thing Do_Core() will do is type check.
        //
        f->param = FUNC_FACADE_HEAD(f->phase);
        f->arg = f->args_head;

        // Note we can't use f->arg enumeration to look for END, because
        // the way the stack works the arg cells are not formatted.  We
        // could use a counter if it were more efficient (we know the length
        // of the frame) but for now, just walk f->param.
        //
        while (NOT_END(f->param)) {
            Prep_Stack_Cell(f->arg);

            // f->special was initialized to the applicable exemplar by
            // Push_Args_For_Underlying_Func()
            //
            if (f->special == END)
                Init_Void(f->arg);
            else {
                //
                // !!! Specialized arguments *should* be invisible to the
                // binding process of the apply.  They have been set, should
                // not be reset.  Removing them from the binding process is
                // TBD, so for now if you apply a specialization and change
                // arguments you shouldn't that is a client error.
                //
                assert(!THROWN(f->special));
                Move_Value(f->arg, f->special);
                ++f->special;
            }

            ++f->arg;
            ++f->param;
        }
        assert(IS_END(f->param));

        // In today's implementation, the body must be rebound to the frame.
        // Ideally if it were read-only (at least), then the opt_def value
        // should be able to carry a virtual binding into the new context.
        // That feature is not currently implemented, so this mutates the
        // bindings on the passed in block...as OBJECTs and other things do
        //
        Bind_Values_Core(
            VAL_ARRAY_AT(def),
            Context_For_Frame_May_Reify_Managed(f),
            FLAGIT_KIND(REB_SET_WORD), // types to bind (just set-word!)
            0, // types to "add midstream" to binding as we go (nothing)
            BIND_DEEP
        );

        // Do the block into scratch space--we ignore the result (unless it is
        // thrown, in which case it must be returned.)
        //
        if (Do_Any_Array_At_Throws(f->out, def)) {
            Drop_Frame_Core(f);
            return R_OUT_IS_THROWN;
        }

        // Do_Core() checks if f->special == f->arg, and if so it knows it
        // is only type checking the existing data
        //
        f->special = f->args_head;
    }

    SET_END(f->out);

    (*PG_Do)(f);

    Drop_Frame_Core(f);

    if (THROWN(f->out))
        return R_OUT_IS_THROWN; // prohibits recovery from exits

    assert(IS_END(f->value)); // we started at END_FLAG, can only throw

    return R_OUT;
}
